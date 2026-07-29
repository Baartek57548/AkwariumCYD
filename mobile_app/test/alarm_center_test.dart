import 'package:cyd_aquarium_mobile/alarm_center/alarm_center.dart';
import 'package:cyd_aquarium_mobile/alarm_center/alarm_engine.dart';
import 'package:cyd_aquarium_mobile/alarm_center/alarm_models.dart';
import 'package:cyd_aquarium_mobile/alarm_center/alarm_notifications.dart';
import 'package:cyd_aquarium_mobile/alarm_center/alarm_preferences.dart';
import 'package:cyd_aquarium_mobile/alarm_center/alarm_relay.dart';
import 'package:cyd_aquarium_mobile/alarm_center/alarm_repository.dart';
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

  late LocalHistoryRepository database;
  late _FakeNotificationSink notifications;
  late AlarmCenter center;

  setUp(() {
    SharedPreferencesAsyncPlatform.instance =
        InMemorySharedPreferencesAsync.empty();
    database = LocalHistoryRepository(
      factory: databaseFactoryFfi,
      databasePath: inMemoryDatabasePath,
    );
    notifications = _FakeNotificationSink();
    final store = SqliteAlarmStore(database);
    center = AlarmCenter(
      engine: AlarmEngine(store),
      store: store,
      history: database,
      notifications: notifications,
      preferences: AlarmPreferencesStore(preferences: SharedPreferencesAsync()),
    );
  });

  tearDown(() => database.close());

  test('fasada wysyła powiadomienie i zapisuje historię alarmu', () async {
    await center.preferences.save(
      const AlarmNotificationPreferences(enabled: true),
    );
    final at = DateTime.utc(2026, 7, 26, 12);
    final report = await center.ingestSignals(<AlarmSignal>[
      AlarmSignal(
        key: 'leak',
        severity: AlarmSeverity.critical,
        title: 'Wyciek',
        message: 'Wykryto wodę.',
      ),
    ], observedAt: at);

    expect(report.notificationFailures, 0);
    expect(notifications.shown.map((alarm) => alarm.key), ['leak']);
    expect(
      await database.latest(category: LocalHistoryCategory.alarm),
      hasLength(1),
    );
    expect((await center.alarms()).single.lastNotifiedAt, at);
  });

  test('preferencje potrafią wyłączyć ostrzeżenia bez utraty alarmu', () async {
    final preferences = center.preferences;
    await preferences.save(
      const AlarmNotificationPreferences(enabled: true, warningEnabled: false),
    );
    final signal = AlarmSignal(
      key: 'supply_low',
      severity: AlarmSeverity.warning,
      title: 'Niskie napięcie',
      message: 'Sprawdź zasilacz.',
    );
    final at = DateTime.utc(2026, 7, 26, 12);

    await center.ingestSignals(<AlarmSignal>[signal], observedAt: at);
    await center.ingestSignals(<AlarmSignal>[
      signal,
    ], observedAt: at.add(const Duration(minutes: 1)));

    expect(notifications.shown, isEmpty);
    expect((await center.alarms()).single.lifecycle, AlarmLifecycle.newAlarm);
    expect((await center.alarms()).single.lastNotifiedAt, isNotNull);
  });

  test(
    'udany webhook nie wycisza ponowienia nieudanego powiadomienia',
    () async {
      final failingNotifications = _FakeNotificationSink(failAlarm: true);
      final relay = _FakeRelay();
      final store = SqliteAlarmStore(database);
      center = AlarmCenter(
        engine: AlarmEngine(store),
        store: store,
        history: database,
        notifications: failingNotifications,
        preferences: AlarmPreferencesStore(
          preferences: SharedPreferencesAsync(),
        ),
        relay: relay,
      );
      await center.preferences.save(
        const AlarmNotificationPreferences(
          enabled: true,
          webhookRelayEnabled: true,
        ),
      );
      final at = DateTime.utc(2026, 7, 26, 12);
      final signal = AlarmSignal(
        key: 'leak',
        severity: AlarmSeverity.critical,
        title: 'Wyciek',
        message: 'Wykryto wodę.',
      );

      final first = await center.ingestSignals(<AlarmSignal>[
        signal,
      ], observedAt: at);
      final second = await center.ingestSignals(<AlarmSignal>[
        signal,
      ], observedAt: at.add(const Duration(minutes: 1)));

      expect(first.notificationFailures, 1);
      expect(first.relayFailures, 0);
      expect(second.processing.notificationCandidates, hasLength(1));
      expect((await center.alarms()).single.lastNotifiedAt, isNull);
      expect(relay.transitions, hasLength(2));
    },
  );
}

final class _FakeNotificationSink implements AlarmNotificationSink {
  _FakeNotificationSink({this.failAlarm = false});

  final bool failAlarm;
  final List<AlarmRecord> shown = <AlarmRecord>[];

  @override
  Future<void> cancelAlarm(String alarmKey) async {}

  @override
  Future<void> initialize() async {}

  @override
  Future<bool> requestPermission() async => true;

  @override
  Future<void> showAlarm(AlarmRecord alarm) async {
    if (failAlarm) throw StateError('Kanał powiadomień niedostępny.');
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

final class _FakeRelay implements AlarmRelay {
  final List<AlarmTransition> transitions = <AlarmTransition>[];

  @override
  Future<void> send(AlarmTransition transition) async {
    transitions.add(transition);
  }
}
