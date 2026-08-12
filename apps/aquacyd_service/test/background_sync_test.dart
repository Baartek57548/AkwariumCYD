import 'package:cyd_aquarium_mobile/alarm_center/alarm_models.dart';
import 'package:cyd_aquarium_mobile/alarm_center/alarm_notifications.dart';
import 'package:cyd_aquarium_mobile/alarm_center/alarm_preferences.dart';
import 'package:cyd_aquarium_mobile/alarm_center/alarm_relay.dart';
import 'package:cyd_aquarium_mobile/alarm_center/background_sync.dart';
import 'package:cyd_aquarium_mobile/controller_preferences.dart';
import 'package:cyd_aquarium_mobile/controller_snapshot_cache.dart';
import 'package:cyd_aquarium_mobile/local_history/local_history_entry.dart';
import 'package:cyd_aquarium_mobile/local_history/local_history_repository.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';
// ignore: depend_on_referenced_packages
import 'package:shared_preferences_platform_interface/in_memory_shared_preferences_async.dart';
// ignore: depend_on_referenced_packages
import 'package:shared_preferences_platform_interface/shared_preferences_async_platform_interface.dart';
import 'package:sqflite_common_ffi/sqflite_ffi.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  setUpAll(sqfliteFfiInit);

  test('polityka akceptuje wyłącznie adresy sieci lokalnej', () {
    expect(
      LocalControllerAddressPolicy.allows(Uri.parse('http://akwarium.local')),
      isTrue,
    );
    expect(
      LocalControllerAddressPolicy.allows(Uri.parse('http://192.168.1.25')),
      isTrue,
    );
    expect(
      LocalControllerAddressPolicy.allows(Uri.parse('https://10.0.0.8')),
      isTrue,
    );
    expect(
      LocalControllerAddressPolicy.allows(Uri.parse('https://example.com')),
      isFalse,
    );
    expect(
      LocalControllerAddressPolicy.allows(Uri.parse('http://127.0.0.1')),
      isFalse,
    );
    expect(
      LocalControllerAddressPolicy.allows(
        Uri.parse('http://user:pass@akwarium.local'),
      ),
      isFalse,
    );
  });

  test(
    'sync odpytuje sterownik tylko po Wi-Fi i zapisuje dane offline',
    () async {
      SharedPreferencesAsyncPlatform.instance =
          InMemorySharedPreferencesAsync.empty();
      final preferences = SharedPreferencesAsync();
      final controllerPreferences = ControllerPreferences(
        preferences: preferences,
      );
      await controllerPreferences.saveAddress(Uri.parse('http://192.168.1.25'));
      final alarmPreferences = AlarmPreferencesStore(preferences: preferences);
      await alarmPreferences.save(
        const AlarmNotificationPreferences(
          enabled: false,
          backgroundSyncEnabled: true,
        ),
      );
      final database = LocalHistoryRepository(
        factory: databaseFactoryFfi,
        databasePath: inMemoryDatabasePath,
      );
      final statusClient = _FakeStatusClient();
      final snapshot = ControllerSnapshotCache(preferences: preferences);
      final runner = AquariumBackgroundSyncRunner(
        controllerPreferences: controllerPreferences,
        alarmPreferences: alarmPreferences,
        connectivity: const _FakeConnectivity(true),
        statusClient: statusClient,
        backoff: BackgroundSyncBackoffStore(preferences: preferences),
        snapshotCache: snapshot,
        history: database,
        notifications: _FakeNotifications(),
        relaySecrets: const _EmptySecretStore(),
      );

      final result = await runner.run(startedAt: DateTime.utc(2026, 7, 26, 12));

      expect(result.succeeded, isTrue);
      expect(result.attempted, isTrue);
      expect(statusClient.calls, 1);
      expect((await snapshot.load())?.status['device'], 'CYD');
      expect(
        await database.latest(category: LocalHistoryCategory.measurement),
        hasLength(1),
      );
      await database.close();
    },
  );

  test(
    'dwie próbki WorkManager oddalone o 30 minut aktywują ostrzeżenie',
    () async {
      SharedPreferencesAsyncPlatform.instance =
          InMemorySharedPreferencesAsync.empty();
      final preferences = SharedPreferencesAsync();
      final controllerPreferences = ControllerPreferences(
        preferences: preferences,
      );
      await controllerPreferences.saveAddress(Uri.parse('http://192.168.1.25'));
      final alarmPreferences = AlarmPreferencesStore(preferences: preferences);
      await alarmPreferences.save(
        const AlarmNotificationPreferences(
          enabled: true,
          backgroundSyncEnabled: true,
        ),
      );
      final database = LocalHistoryRepository(
        factory: databaseFactoryFfi,
        databasePath: inMemoryDatabasePath,
      );
      final notifications = _FakeNotifications();
      final runner = AquariumBackgroundSyncRunner(
        controllerPreferences: controllerPreferences,
        alarmPreferences: alarmPreferences,
        connectivity: const _FakeConnectivity(true),
        statusClient: _FakeStatusClient(
          response: <String, dynamic>{
            'device': 'CYD',
            'sensors': <String, dynamic>{'temp_c': 25.2, 'temp_valid': true},
            'alarms': <String, dynamic>{'flags': 32},
            'modules': <String, dynamic>{},
          },
        ),
        backoff: BackgroundSyncBackoffStore(preferences: preferences),
        snapshotCache: ControllerSnapshotCache(preferences: preferences),
        history: database,
        notifications: notifications,
        relaySecrets: const _EmptySecretStore(),
      );
      final firstAt = DateTime.utc(2026, 7, 26, 12);

      expect((await runner.run(startedAt: firstAt)).succeeded, isTrue);
      expect(notifications.shown, isEmpty);
      expect(
        (await runner.run(
          startedAt: firstAt.add(const Duration(minutes: 30)),
        )).succeeded,
        isTrue,
      );

      expect(notifications.shown.map((alarm) => alarm.key), ['supplylow']);
      await database.close();
    },
  );

  test('brak Wi-Fi nie uruchamia żadnego żądania', () async {
    SharedPreferencesAsyncPlatform.instance =
        InMemorySharedPreferencesAsync.empty();
    final preferences = SharedPreferencesAsync();
    final controllerPreferences = ControllerPreferences(
      preferences: preferences,
    );
    await controllerPreferences.saveAddress(Uri.parse('http://192.168.1.25'));
    final alarmPreferences = AlarmPreferencesStore(preferences: preferences);
    await alarmPreferences.save(
      const AlarmNotificationPreferences(backgroundSyncEnabled: true),
    );
    final statusClient = _FakeStatusClient();
    final database = LocalHistoryRepository(
      factory: databaseFactoryFfi,
      databasePath: inMemoryDatabasePath,
    );
    final runner = AquariumBackgroundSyncRunner(
      controllerPreferences: controllerPreferences,
      alarmPreferences: alarmPreferences,
      connectivity: const _FakeConnectivity(false),
      statusClient: statusClient,
      history: database,
    );

    final result = await runner.run(startedAt: DateTime.utc(2026, 7, 26, 12));

    expect(result.reason, 'not_wifi');
    expect(result.attempted, isFalse);
    expect(statusClient.calls, 0);
    await database.close();
  });

  test('niepełny status nie może rozwiązać istniejących alarmów', () async {
    SharedPreferencesAsyncPlatform.instance =
        InMemorySharedPreferencesAsync.empty();
    final preferences = SharedPreferencesAsync();
    final controllerPreferences = ControllerPreferences(
      preferences: preferences,
    );
    await controllerPreferences.saveAddress(Uri.parse('http://192.168.1.25'));
    final alarmPreferences = AlarmPreferencesStore(preferences: preferences);
    await alarmPreferences.save(
      const AlarmNotificationPreferences(
        enabled: false,
        backgroundSyncEnabled: true,
      ),
    );
    final database = LocalHistoryRepository(
      factory: databaseFactoryFfi,
      databasePath: inMemoryDatabasePath,
    );
    final statusClient = _FakeStatusClient(
      response: <String, dynamic>{'device': 'CYD'},
    );
    final runner = AquariumBackgroundSyncRunner(
      controllerPreferences: controllerPreferences,
      alarmPreferences: alarmPreferences,
      connectivity: const _FakeConnectivity(true),
      statusClient: statusClient,
      backoff: BackgroundSyncBackoffStore(preferences: preferences),
      snapshotCache: ControllerSnapshotCache(preferences: preferences),
      history: database,
      notifications: _FakeNotifications(),
      relaySecrets: const _EmptySecretStore(),
    );

    final result = await runner.run(startedAt: DateTime.utc(2026, 7, 26, 12));

    expect(result.succeeded, isFalse);
    expect(result.attempted, isTrue);
    expect(result.reason, 'transient_failure');
    expect(
      await database.latest(category: LocalHistoryCategory.measurement),
      isEmpty,
    );
    await database.close();
  });

  test('backoff rośnie po błędach i resetuje się po sukcesie', () async {
    SharedPreferencesAsyncPlatform.instance =
        InMemorySharedPreferencesAsync.empty();
    final store = BackgroundSyncBackoffStore(
      preferences: SharedPreferencesAsync(),
    );
    final at = DateTime.utc(2026, 7, 26, 12);

    expect(await store.recordFailure(at), const Duration(minutes: 15));
    expect(
      await store.canAttempt(at.add(const Duration(minutes: 14))),
      isFalse,
    );
    expect(
      await store.recordFailure(at.add(const Duration(minutes: 15))),
      const Duration(minutes: 30),
    );
    await store.recordSuccess();
    expect(await store.canAttempt(at), isTrue);
  });
}

final class _FakeConnectivity implements BackgroundConnectivity {
  const _FakeConnectivity(this.value);
  final bool value;

  @override
  Future<bool> get isWifi async => value;
}

final class _FakeStatusClient implements BackgroundStatusClient {
  _FakeStatusClient({Map<String, dynamic>? response})
    : response =
          response ??
          <String, dynamic>{
            'device': 'CYD',
            'sensors': <String, dynamic>{'temp_c': 25.2, 'temp_valid': true},
            'alarms': <String, dynamic>{'flags': 0},
            'modules': <String, dynamic>{},
          };

  int calls = 0;
  final Map<String, dynamic> response;

  @override
  Future<Map<String, dynamic>> status(Uri address) async {
    calls++;
    return response;
  }
}

final class _FakeNotifications implements AlarmNotificationSink {
  final List<AlarmRecord> shown = <AlarmRecord>[];

  @override
  Future<void> cancelAlarm(String alarmKey) async {}

  @override
  Future<void> initialize() async {}

  @override
  Future<bool> requestPermission() async => true;

  @override
  Future<void> showTestNotification() async {}

  @override
  Future<void> showAppUpdate({
    required String tagName,
    required String version,
    required bool downloaded,
  }) async {}

  @override
  Future<void> showAlarm(AlarmRecord alarm) async {
    shown.add(alarm);
  }

  @override
  Future<void> showResolved(AlarmRecord alarm) async {}

  @override
  Future<void> showServiceReminder({
    required String id,
    required String title,
    required String body,
  }) async {}
}

final class _EmptySecretStore implements AlarmRelaySecretStore {
  const _EmptySecretStore();

  @override
  Future<void> clearWebhook() async {}

  @override
  Future<HomeAssistantWebhookConfiguration?> loadWebhook() async => null;

  @override
  Future<void> saveWebhook(
    HomeAssistantWebhookConfiguration configuration,
  ) async {}
}
