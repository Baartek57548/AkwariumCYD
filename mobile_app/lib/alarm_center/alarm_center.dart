import '../local_history/local_history_entry.dart';
import '../local_history/local_history_repository.dart';
import 'alarm_engine.dart';
import 'alarm_models.dart';
import 'alarm_notifications.dart';
import 'alarm_preferences.dart';
import 'alarm_relay.dart';
import 'alarm_repository.dart';

final class AlarmDispatchReport {
  const AlarmDispatchReport({
    required this.processing,
    required this.notificationFailures,
    required this.relayFailures,
  });

  final AlarmProcessingResult processing;
  final int notificationFailures;
  final int relayFailures;
}

/// Publiczna fasada integracyjna dla ControllerSession.
///
/// Wywołanie [ingestCommandCenterModel] po każdym kompletnym odczycie statusu
/// aktualizuje cykl życia alarmów, historię, lokalne powiadomienia i opcjonalny
/// relay. Błędy pojedynczego kanału nie blokują zapisu stanu alarmu.
final class AlarmCenter {
  AlarmCenter({
    required this.engine,
    required this.store,
    required this.history,
    required this.notifications,
    required this.preferences,
    this.relay = const DisabledAlarmRelay(),
  });

  factory AlarmCenter.standard({
    LocalHistoryRepository? database,
    AlarmNotificationSink? notifications,
    AlarmPreferencesStore? preferences,
    AlarmRelay relay = const DisabledAlarmRelay(),
  }) {
    final db = database ?? LocalHistoryRepository();
    final store = SqliteAlarmStore(db);
    return AlarmCenter(
      engine: AlarmEngine(store),
      store: store,
      history: db,
      notifications: notifications ?? LocalAlarmNotificationSink(),
      preferences: preferences ?? AlarmPreferencesStore(),
      relay: relay,
    );
  }

  final AlarmEngine engine;
  final AlarmStore store;
  final LocalHistoryRepository history;
  final AlarmNotificationSink notifications;
  final AlarmPreferencesStore preferences;
  final AlarmRelay relay;

  Future<void> initialize() => notifications.initialize();

  Future<bool> requestNotificationPermission() =>
      notifications.requestPermission();

  Future<AlarmDispatchReport> ingestSignals(
    Iterable<AlarmSignal> signals, {
    DateTime? observedAt,
    bool completeSnapshot = true,
    AlarmPolicy policy = const AlarmPolicy(),
  }) async {
    final now = (observedAt ?? DateTime.now()).toUtc();
    final settings = await preferences.load();
    final result = await engine.processSnapshot(
      signals,
      observedAt: now,
      completeSnapshot: completeSnapshot,
      policy: policy.copyWith(notificationCooldown: settings.cooldown),
    );
    var notificationFailures = 0;
    var relayFailures = 0;
    final handledNotificationKeys = <String>{};

    for (final transition in result.transitions) {
      await _recordTransition(transition);
      if (transition.type == AlarmTransitionType.resolved) {
        try {
          await notifications.cancelAlarm(transition.record.key);
        } on Object {
          notificationFailures++;
        }
        if (settings.enabled && settings.resolvedEnabled) {
          try {
            await notifications.showResolved(transition.record);
          } on Object {
            notificationFailures++;
          }
        }
      }
      if (settings.webhookRelayEnabled) {
        try {
          await relay.send(transition);
        } on Object {
          relayFailures++;
        }
      }
    }

    if (settings.enabled) {
      for (final alarm in result.notificationCandidates) {
        final severityEnabled = alarm.severity == AlarmSeverity.critical
            ? settings.criticalEnabled
            : settings.warningEnabled;
        if (!severityEnabled) {
          handledNotificationKeys.add(alarm.key);
          continue;
        }
        try {
          await notifications.showAlarm(alarm);
          handledNotificationKeys.add(alarm.key);
        } on Object {
          notificationFailures++;
        }
      }
    } else {
      handledNotificationKeys.addAll(
        result.notificationCandidates.map((alarm) => alarm.key),
      );
    }
    for (final key in handledNotificationKeys) {
      await engine.markNotified(key, notifiedAt: now);
    }
    return AlarmDispatchReport(
      processing: result,
      notificationFailures: notificationFailures,
      relayFailures: relayFailures,
    );
  }

  Future<AlarmRecord?> acknowledge(
    String key, {
    DateTime? acknowledgedAt,
  }) async {
    final at = (acknowledgedAt ?? DateTime.now()).toUtc();
    final record = await engine.acknowledge(key, acknowledgedAt: at);
    if (record == null) return null;
    final transition = AlarmTransition(
      type: AlarmTransitionType.acknowledged,
      record: record,
      occurredAt: at,
    );
    await _recordTransition(transition);
    final settings = await preferences.load();
    if (settings.webhookRelayEnabled) {
      try {
        await relay.send(transition);
      } on Object {
        // Potwierdzenie lokalne jest ważniejsze niż niedostępny relay.
      }
    }
    return record;
  }

  Future<List<AlarmRecord>> alarms({
    bool includeResolved = true,
    int limit = 200,
  }) => store.list(includeResolved: includeResolved, limit: limit);

  Future<void> _recordTransition(AlarmTransition transition) {
    final record = transition.record;
    return history.append(
      LocalHistoryEntry(
        id: LocalHistoryEntry.createId(
          timestamp: transition.occurredAt,
          category: LocalHistoryCategory.alarm,
          discriminator: '${record.key}|${transition.type.name}',
        ),
        category: LocalHistoryCategory.alarm,
        timestamp: transition.occurredAt,
        title: record.title,
        detail: record.message,
        source: 'alarm_engine',
        values: <String, Object?>{
          'alarmKey': record.key,
          'severity': record.severity.name,
          'state': record.lifecycle.storageValue,
          'transition': transition.type.name,
          'occurrences': record.occurrences,
        },
      ),
    );
  }
}
