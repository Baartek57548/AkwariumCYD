import 'dart:io';

import 'package:connectivity_plus/connectivity_plus.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter/widgets.dart' hide LocalHistoryEntry;
import 'package:shared_preferences/shared_preferences.dart';
import 'package:workmanager/workmanager.dart';

import '../controller_preferences.dart';
import '../controller_address.dart';
import '../controller_snapshot_cache.dart';
import '../full_controller/command_center_models.dart';
import '../full_controller/controller_api.dart';
import '../full_controller/controller_session.dart';
import '../local_history/local_history_entry.dart';
import '../local_history/local_history_recorder.dart';
import '../local_history/local_history_repository.dart';
import '../local_history/service_reminders.dart';
import 'alarm_center.dart';
import 'alarm_engine.dart';
import 'alarm_notifications.dart';
import 'alarm_preferences.dart';
import 'alarm_relay.dart';
import 'command_center_alarm_adapter.dart';

abstract interface class BackgroundConnectivity {
  Future<bool> get isWifi;
}

final class PlatformBackgroundConnectivity implements BackgroundConnectivity {
  PlatformBackgroundConnectivity({Connectivity? connectivity})
    : _connectivity = connectivity ?? Connectivity();

  final Connectivity _connectivity;

  @override
  Future<bool> get isWifi async {
    final connections = await _connectivity.checkConnectivity();
    return connections.contains(ConnectivityResult.wifi);
  }
}

abstract interface class BackgroundStatusClient {
  Future<Map<String, dynamic>> status(Uri address);
}

final class ControllerBackgroundStatusClient implements BackgroundStatusClient {
  const ControllerBackgroundStatusClient();

  @override
  Future<Map<String, dynamic>> status(Uri address) {
    return ControllerApi(
      address,
      requestDeadline: const Duration(seconds: 8),
      maximumReadAttempts: 1,
    ).status();
  }
}

abstract final class LocalControllerAddressPolicy {
  static bool allows(Uri uri) {
    if ((uri.scheme != 'http' && uri.scheme != 'https') ||
        uri.host.isEmpty ||
        uri.userInfo.isNotEmpty ||
        uri.hasQuery ||
        uri.hasFragment) {
      return false;
    }
    return ControllerAddress.isLocalNetworkUri(uri);
  }
}

final class BackgroundSyncBackoffStore {
  BackgroundSyncBackoffStore({SharedPreferencesAsync? preferences})
    : _injectedPreferences = preferences;

  static const _failureCountKey = 'alarm_background_failure_count_v1';
  static const _nextAttemptKey = 'alarm_background_next_attempt_ms_v1';

  final SharedPreferencesAsync? _injectedPreferences;
  SharedPreferencesAsync? _defaultPreferences;

  SharedPreferencesAsync get _preferences =>
      _injectedPreferences ??
      (_defaultPreferences ??= SharedPreferencesAsync());

  Future<bool> canAttempt(DateTime now) async {
    try {
      final value = await _preferences.getInt(_nextAttemptKey);
      return value == null || now.toUtc().millisecondsSinceEpoch >= value;
    } on Object {
      return true;
    }
  }

  Future<Duration> recordFailure(DateTime now) async {
    final previous = await _preferences.getInt(_failureCountKey) ?? 0;
    final failures = (previous + 1).clamp(1, 20);
    const delays = <Duration>[
      Duration(minutes: 15),
      Duration(minutes: 30),
      Duration(hours: 1),
      Duration(hours: 2),
      Duration(hours: 6),
    ];
    final delay = delays[(failures - 1).clamp(0, delays.length - 1)];
    await _preferences.setInt(_failureCountKey, failures);
    await _preferences.setInt(
      _nextAttemptKey,
      now.toUtc().add(delay).millisecondsSinceEpoch,
    );
    return delay;
  }

  Future<void> recordSuccess() async {
    await Future.wait(<Future<void>>[
      _preferences.remove(_failureCountKey),
      _preferences.remove(_nextAttemptKey),
    ]);
  }
}

final class BackgroundSyncResult {
  const BackgroundSyncResult({
    required this.succeeded,
    required this.attempted,
    required this.reason,
  });

  final bool succeeded;
  final bool attempted;
  final String reason;
}

/// Jednorazowy przebieg synchronizacji. Nie jest usługą zdalnego push:
/// odczytuje zapamiętany sterownik wyłącznie przez aktywne lokalne Wi‑Fi.
final class AquariumBackgroundSyncRunner {
  AquariumBackgroundSyncRunner({
    ControllerPreferences? controllerPreferences,
    AlarmPreferencesStore? alarmPreferences,
    BackgroundConnectivity? connectivity,
    BackgroundStatusClient? statusClient,
    BackgroundSyncBackoffStore? backoff,
    ControllerSnapshotCache? snapshotCache,
    LocalHistoryRepository? history,
    AlarmNotificationSink? notifications,
    AlarmRelaySecretStore? relaySecrets,
  }) : controllerPreferences = controllerPreferences ?? ControllerPreferences(),
       alarmPreferences = alarmPreferences ?? AlarmPreferencesStore(),
       connectivity = connectivity ?? PlatformBackgroundConnectivity(),
       statusClient = statusClient ?? const ControllerBackgroundStatusClient(),
       backoff = backoff ?? BackgroundSyncBackoffStore(),
       snapshotCache = snapshotCache ?? ControllerSnapshotCache(),
       history = history ?? LocalHistoryRepository(),
       notifications = notifications ?? LocalAlarmNotificationSink(),
       relaySecrets = relaySecrets ?? SecureAlarmRelaySecretStore();

  final ControllerPreferences controllerPreferences;
  final AlarmPreferencesStore alarmPreferences;
  final BackgroundConnectivity connectivity;
  final BackgroundStatusClient statusClient;
  final BackgroundSyncBackoffStore backoff;
  final ControllerSnapshotCache snapshotCache;
  final LocalHistoryRepository history;
  final AlarmNotificationSink notifications;
  final AlarmRelaySecretStore relaySecrets;

  // Android może uruchomić zadanie okresowe później niż zadane 30 minut.
  // Dwie kolejne próbki tła nadal muszą tworzyć/rozwiązywać alarm, ale bardzo
  // stary odczyt nie może być traktowany jako ciągłość pomiaru.
  static const AlarmPolicy _backgroundAlarmPolicy = AlarmPolicy(
    maximumObservationGap: Duration(hours: 2),
  );

  Future<BackgroundSyncResult> run({DateTime? startedAt}) async {
    final now = (startedAt ?? DateTime.now()).toUtc();
    final settings = await alarmPreferences.load();
    if (!settings.backgroundSyncEnabled) {
      return const BackgroundSyncResult(
        succeeded: true,
        attempted: false,
        reason: 'disabled',
      );
    }
    if (!await backoff.canAttempt(now)) {
      return const BackgroundSyncResult(
        succeeded: true,
        attempted: false,
        reason: 'backoff',
      );
    }
    late final bool autoReconnect;
    late final Uri? address;
    try {
      autoReconnect = await controllerPreferences.loadAutoReconnect();
      address = await controllerPreferences.loadSavedAddress();
    } on Object {
      return _transientFailure(now, attempted: false);
    }
    if (!autoReconnect ||
        address == null ||
        !LocalControllerAddressPolicy.allows(address)) {
      return const BackgroundSyncResult(
        succeeded: true,
        attempted: false,
        reason: 'no_local_controller',
      );
    }
    late final bool wifi;
    try {
      wifi = await connectivity.isWifi;
    } on Object {
      return _transientFailure(now, attempted: false);
    }
    if (!wifi) {
      return const BackgroundSyncResult(
        succeeded: true,
        attempted: false,
        reason: 'not_wifi',
      );
    }

    try {
      final status = await statusClient.status(address);
      if (!_isCompleteStatus(status)) {
        throw const FormatException('Niepełny status sterownika.');
      }
      await snapshotCache.save(status, savedAt: now);
      final model = CommandCenterModel.fromStatus(
        status,
        ControllerSessionKind.wifi,
        connected: true,
        now: now,
      );
      AlarmRelay relay = const DisabledAlarmRelay();
      if (settings.webhookRelayEnabled) {
        final webhook = await relaySecrets.loadWebhook();
        if (webhook != null) {
          relay = HomeAssistantWebhookRelay(configuration: webhook);
        }
      }
      final alarmCenter = AlarmCenter.standard(
        database: history,
        notifications: notifications,
        preferences: alarmPreferences,
        relay: relay,
      );
      await alarmCenter.initialize();
      await alarmCenter.ingestCommandCenterModel(
        model,
        observedAt: now,
        policy: _backgroundAlarmPolicy,
      );
      await LocalHistoryRecorder(
        history,
      ).recordStatus(model, observedAt: now, source: 'background_wifi');
      if (settings.enabled) {
        final reminderManager = ServiceReminderManager(
          repository: ServiceReminderRepository(history),
          history: history,
          notifications: notifications,
        );
        await reminderManager.ensureDefaults(createdAt: now);
        await reminderManager.notifyDue(checkedAt: now);
      }
      await backoff.recordSuccess();
      return const BackgroundSyncResult(
        succeeded: true,
        attempted: true,
        reason: 'synchronized',
      );
    } on Object {
      return _transientFailure(now, attempted: true);
    }
  }

  Future<BackgroundSyncResult> _transientFailure(
    DateTime at, {
    required bool attempted,
  }) async {
    try {
      await backoff.recordFailure(at);
    } on Object {
      // Harmonogram WorkManager nadal otrzyma `false` i zastosuje swój backoff.
    }
    await _recordFailure(at);
    return BackgroundSyncResult(
      succeeded: false,
      attempted: attempted,
      reason: attempted ? 'transient_failure' : 'preflight_failure',
    );
  }

  static bool _isCompleteStatus(Map<String, dynamic> status) {
    final alarms = status['alarms'];
    final sensors = status['sensors'];
    final modules = status['modules'];
    if (alarms is! Map || sensors is! Map || modules is! Map) return false;
    final flags = alarms['flags'];
    return flags is int && flags >= 0;
  }

  Future<void> _recordFailure(DateTime at) async {
    try {
      await history.append(
        LocalHistoryEntry(
          id: LocalHistoryEntry.createId(
            timestamp: at,
            category: LocalHistoryCategory.synchronization,
            discriminator: 'background_failure',
          ),
          category: LocalHistoryCategory.synchronization,
          timestamp: at,
          title: 'Synchronizacja w tle nieudana',
          detail: 'Sterownik lokalny nie odpowiedział w wyznaczonym czasie.',
          source: 'background_wifi',
          values: const <String, Object?>{'succeeded': false},
        ),
      );
    } on Object {
      // Błąd telemetrii nie może zmienić wyniku bezpiecznego backoffu.
    }
  }
}

abstract final class AquariumBackgroundService {
  static const uniqueTaskName = 'aquacyd-local-wifi-sync-v1';
  static const workerTaskName = 'aquacydLocalWifiSync';

  /// Należy wywołać po WidgetsFlutterBinding.ensureInitialized(), przed runApp.
  static Future<void> initialize() async {
    if (kIsWeb || !Platform.isAndroid) return;
    await Workmanager().initialize(aquariumBackgroundCallbackDispatcher);
  }

  /// Rejestruje polling dopiero po włączeniu opcji przez użytkownika.
  static Future<void> applyPreferences(
    AlarmNotificationPreferences preferences,
  ) async {
    if (kIsWeb || !Platform.isAndroid) return;
    if (!preferences.backgroundSyncEnabled) {
      await Workmanager().cancelByUniqueName(uniqueTaskName);
      return;
    }
    await Workmanager().registerPeriodicTask(
      uniqueTaskName,
      workerTaskName,
      frequency: const Duration(minutes: 30),
      flexInterval: const Duration(minutes: 15),
      constraints: Constraints(
        networkType: NetworkType.connected,
        requiresBatteryNotLow: true,
        requiresStorageNotLow: true,
      ),
      existingWorkPolicy: ExistingPeriodicWorkPolicy.update,
      backoffPolicy: BackoffPolicy.exponential,
      backoffPolicyDelay: const Duration(minutes: 15),
      tag: 'aquacyd-local-sync',
    );
  }
}

@pragma('vm:entry-point')
void aquariumBackgroundCallbackDispatcher() {
  WidgetsFlutterBinding.ensureInitialized();
  Workmanager().executeTask((task, inputData) async {
    if (task != AquariumBackgroundService.workerTaskName &&
        task != AquariumBackgroundService.uniqueTaskName) {
      return true;
    }
    final result = await AquariumBackgroundSyncRunner().run();
    return result.succeeded;
  });
}
