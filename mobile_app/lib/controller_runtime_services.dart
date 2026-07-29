import 'dart:async';

import 'package:flutter/foundation.dart';

import 'alarm_center/alarm_center_api.dart';
import 'full_controller/command_center_models.dart';
import 'full_controller/controller_session.dart';
import 'local_history/local_history_api.dart';

/// Trwałe usługi działające niezależnie od aktualnego transportu sterownika.
///
/// Obserwacje są koaleskowane: jeżeli odczyty przychodzą szybciej niż zapis
/// SQLite, przetwarzany jest najnowszy kompletny model zamiast rosnącej kolejki.
final class ControllerRuntimeServices extends ChangeNotifier {
  ControllerRuntimeServices({
    LocalHistoryRepository? repository,
    AlarmCenter? alarmCenter,
    AlarmPreferencesStore? alarmPreferences,
  }) : repository = repository ?? LocalHistoryRepository(),
       alarmPreferences = alarmPreferences ?? AlarmPreferencesStore() {
    this.alarmCenter =
        alarmCenter ??
        AlarmCenter.standard(
          database: this.repository,
          preferences: this.alarmPreferences,
        );
    historyRecorder = LocalHistoryRecorder(this.repository);
    reminderManager = ServiceReminderManager(
      repository: ServiceReminderRepository(this.repository),
      history: this.repository,
      notifications: this.alarmCenter.notifications,
    );
  }

  final LocalHistoryRepository repository;
  final AlarmPreferencesStore alarmPreferences;
  late final AlarmCenter alarmCenter;
  late final LocalHistoryRecorder historyRecorder;
  late final ServiceReminderManager reminderManager;

  static const Duration _measurementInterval = Duration(minutes: 5);
  static const Duration _reminderCheckInterval = Duration(hours: 1);

  Future<void>? _initialization;
  _RuntimeObservation? _pendingObservation;
  bool _draining = false;
  bool _disposed = false;
  bool _initialized = false;
  DateTime? _lastMeasurementAt;
  DateTime? _lastReminderCheckAt;
  String? _warning;
  AlarmNotificationPreferences _preferences =
      const AlarmNotificationPreferences();
  List<AlarmRecord> _alarms = const [];
  List<ServiceReminder> _reminders = const [];
  List<LocalHistoryEntry> _history = const [];
  DateTime? _lastCompleteMeasurementAt;

  bool get initialized => _initialized;
  String? get warning => _warning;
  AlarmNotificationPreferences get preferences => _preferences;
  List<AlarmRecord> get alarms => List<AlarmRecord>.unmodifiable(_alarms);
  List<ServiceReminder> get reminders =>
      List<ServiceReminder>.unmodifiable(_reminders);
  List<LocalHistoryEntry> get history =>
      List<LocalHistoryEntry>.unmodifiable(_history);
  DateTime? get lastCompleteMeasurementAt => _lastCompleteMeasurementAt;
  bool get hasEvaluatedCompleteSnapshot => _lastCompleteMeasurementAt != null;
  int get activeAlarmCount => _alarms.where((alarm) => alarm.isActive).length;
  int get criticalAlarmCount => _alarms
      .where(
        (alarm) => alarm.isActive && alarm.severity == AlarmSeverity.critical,
      )
      .length;

  Future<void> initialize() {
    if (_disposed) return Future<void>.value();
    return _initialization ??= _initialize();
  }

  Future<void> _initialize() async {
    String? warning;
    try {
      await repository.database;
      await reminderManager.ensureDefaults();
      _preferences = await alarmPreferences.load();
    } on Object {
      warning = 'Nie udało się otworzyć lokalnej historii.';
    }
    try {
      await alarmCenter.initialize();
    } on Object {
      warning =
          'Historia działa, ale systemowy kanał powiadomień jest niedostępny.';
    }
    try {
      await AquariumBackgroundService.applyPreferences(_preferences);
    } on Object {
      warning = 'Nie udało się zastosować harmonogramu synchronizacji w tle.';
    }
    _warning = warning;
    _initialized = true;
    await refresh();
  }

  void observeStatus({
    required Map<String, dynamic> status,
    required ControllerSessionKind sessionKind,
    required DateTime observedAt,
    required bool completeSnapshot,
  }) {
    if (_disposed || status.isEmpty) return;
    final model = CommandCenterModel.fromStatus(
      status,
      sessionKind,
      connected: completeSnapshot,
      offline: !completeSnapshot,
      now: observedAt,
    );
    _pendingObservation = _RuntimeObservation(
      model: model,
      observedAt: observedAt.toUtc(),
      completeSnapshot: completeSnapshot,
    );
    if (!_draining) unawaited(_drainObservations());
  }

  Future<void> _drainObservations() async {
    if (_draining || _disposed) return;
    _draining = true;
    try {
      await initialize();
      while (!_disposed && _pendingObservation != null) {
        final observation = _pendingObservation!;
        _pendingObservation = null;
        await _ingest(observation);
      }
    } finally {
      _draining = false;
      if (!_disposed && _pendingObservation != null) {
        unawaited(_drainObservations());
      }
    }
  }

  Future<void> _ingest(_RuntimeObservation observation) async {
    try {
      final report = await alarmCenter.ingestCommandCenterModel(
        observation.model,
        observedAt: observation.observedAt,
        completeSnapshot: observation.completeSnapshot,
      );
      final lastMeasurement = _lastMeasurementAt;
      if (observation.completeSnapshot &&
          (lastMeasurement == null ||
              observation.observedAt.difference(lastMeasurement) >=
                  _measurementInterval)) {
        await historyRecorder.recordStatus(
          observation.model,
          observedAt: observation.observedAt,
          source: 'controller_live',
        );
        _lastMeasurementAt = observation.observedAt;
      }
      final lastReminderCheck = _lastReminderCheckAt;
      if (lastReminderCheck == null ||
          observation.observedAt.difference(lastReminderCheck) >=
              _reminderCheckInterval) {
        await reminderManager.notifyDue(checkedAt: observation.observedAt);
        _lastReminderCheckAt = observation.observedAt;
      }
      if (report.processing.transitions.isNotEmpty ||
          _alarms.isEmpty ||
          _history.isEmpty) {
        await refresh();
      }
    } on Object {
      _warning =
          'Nie udało się zapisać najnowszej obserwacji w centrum alarmów.';
      _notify();
    }
  }

  Future<void> refresh() async {
    if (_disposed) return;
    try {
      final results = await Future.wait<Object>([
        alarmCenter.alarms(limit: 200),
        reminderManager.repository.list(),
        repository.latest(limit: 120),
        repository.latest(limit: 1, category: LocalHistoryCategory.measurement),
      ]);
      if (_disposed) return;
      _alarms = results[0] as List<AlarmRecord>;
      _reminders = results[1] as List<ServiceReminder>;
      _history = results[2] as List<LocalHistoryEntry>;
      final latestMeasurements = results[3] as List<LocalHistoryEntry>;
      _lastCompleteMeasurementAt = latestMeasurements.firstOrNull?.timestamp;
      _lastMeasurementAt ??= _lastCompleteMeasurementAt;
      _notify();
    } on Object {
      _warning = 'Nie udało się odświeżyć lokalnego centrum zdarzeń.';
      _notify();
    }
  }

  Future<void> acknowledgeAlarm(String key) async {
    await initialize();
    await alarmCenter.acknowledge(key);
    await refresh();
  }

  Future<void> completeReminder(String id) async {
    await initialize();
    await reminderManager.complete(id);
    await refresh();
  }

  Future<bool> requestNotificationPermission() async {
    await initialize();
    try {
      return await alarmCenter.requestNotificationPermission();
    } on Object {
      _warning = 'System nie udostępnił uprawnienia do powiadomień.';
      _notify();
      return false;
    }
  }

  Future<void> saveAlarmPreferences(
    AlarmNotificationPreferences preferences,
  ) async {
    await alarmPreferences.save(preferences);
    await AquariumBackgroundService.applyPreferences(preferences);
    _preferences = preferences;
    _notify();
  }

  Future<void> recordCommand({
    required String action,
    required bool succeeded,
    String detail = '',
  }) async {
    try {
      await initialize();
      await historyRecorder.recordCommand(
        action: action,
        succeeded: succeeded,
        detail: detail,
      );
      await refresh();
    } on Object {
      _warning = 'Nie udało się zapisać operacji w historii lokalnej.';
      _notify();
    }
  }

  void _notify() {
    if (!_disposed) notifyListeners();
  }

  @override
  void dispose() {
    if (_disposed) return;
    _disposed = true;
    _pendingObservation = null;
    unawaited(repository.close());
    super.dispose();
  }
}

final class _RuntimeObservation {
  const _RuntimeObservation({
    required this.model,
    required this.observedAt,
    required this.completeSnapshot,
  });

  final CommandCenterModel model;
  final DateTime observedAt;
  final bool completeSnapshot;
}
