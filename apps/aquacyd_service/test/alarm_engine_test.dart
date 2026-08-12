import 'package:cyd_aquarium_mobile/alarm_center/alarm_engine.dart';
import 'package:cyd_aquarium_mobile/alarm_center/alarm_models.dart';
import 'package:cyd_aquarium_mobile/alarm_center/alarm_repository.dart';
import 'package:cyd_aquarium_mobile/local_history/local_history_repository.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:sqflite_common_ffi/sqflite_ffi.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  late LocalHistoryRepository database;
  late SqliteAlarmStore store;
  late AlarmEngine engine;

  setUpAll(sqfliteFfiInit);

  setUp(() {
    database = LocalHistoryRepository(
      factory: databaseFactoryFfi,
      databasePath: inMemoryDatabasePath,
    );
    store = SqliteAlarmStore(database);
    engine = AlarmEngine(store);
  });

  tearDown(() => database.close());

  test('warning wymaga dwóch próbek i jest deduplikowany', () async {
    final signal = AlarmSignal(
      key: 'ph_out_of_range',
      severity: AlarmSeverity.warning,
      title: 'pH poza zakresem',
      message: 'Sprawdź sondę.',
    );
    final first = await engine.processSnapshot(<AlarmSignal>[
      signal,
      signal,
    ], observedAt: DateTime.utc(2026, 7, 26, 12));
    final second = await engine.processSnapshot(<AlarmSignal>[
      signal,
    ], observedAt: DateTime.utc(2026, 7, 26, 12, 1));

    expect(first.transitions, isEmpty);
    expect(second.transitions.single.type, AlarmTransitionType.opened);
    expect(second.notificationCandidates.single.key, 'ph_out_of_range');
    final records = await store.list();
    expect(records.single.lifecycle, AlarmLifecycle.newAlarm);
    expect(records.single.occurrences, 1);
  });

  test(
    'alarm krytyczny otwiera się natychmiast i respektuje cooldown',
    () async {
      final signal = AlarmSignal(
        key: 'leak',
        severity: AlarmSeverity.critical,
        title: 'Wykryto wyciek',
        message: 'Odłącz pompę.',
      );
      final started = DateTime.utc(2026, 7, 26, 12);
      final opened = await engine.processSnapshot(<AlarmSignal>[
        signal,
      ], observedAt: started);
      await engine.markNotified('leak', notifiedAt: started);
      final quiet = await engine.processSnapshot(<AlarmSignal>[
        signal,
      ], observedAt: started.add(const Duration(minutes: 29)));
      final repeated = await engine.processSnapshot(<AlarmSignal>[
        signal,
      ], observedAt: started.add(const Duration(minutes: 31)));

      expect(opened.transitions.single.type, AlarmTransitionType.opened);
      expect(quiet.notificationCandidates, isEmpty);
      expect(repeated.transitions.single.type, AlarmTransitionType.repeated);
    },
  );

  test('potwierdzenie wycisza powtórki, ale nie rozwiązuje alarmu', () async {
    final at = DateTime.utc(2026, 7, 26, 12);
    final signal = AlarmSignal(
      key: 'water_level',
      severity: AlarmSeverity.critical,
      title: 'Niski poziom',
      message: 'Uzupełnij wodę.',
    );
    await engine.processSnapshot(<AlarmSignal>[signal], observedAt: at);
    final acknowledged = await engine.acknowledge(
      'water_level',
      acknowledgedAt: at.add(const Duration(minutes: 1)),
    );
    final next = await engine.processSnapshot(<AlarmSignal>[
      signal,
    ], observedAt: at.add(const Duration(hours: 2)));

    expect(acknowledged?.lifecycle, AlarmLifecycle.acknowledged);
    expect(next.notificationCandidates, isEmpty);
    expect((await store.list()).single.isActive, isTrue);
  });

  test(
    'eskalacja ostrzeżenia do alarmu krytycznego alarmuje natychmiast',
    () async {
      final at = DateTime.utc(2026, 7, 26, 12);
      final warning = AlarmSignal(
        key: 'temperature',
        severity: AlarmSeverity.warning,
        title: 'Temperatura poza zakresem',
        message: 'Sprawdź temperaturę.',
      );
      await engine.processSnapshot(<AlarmSignal>[warning], observedAt: at);
      await engine.processSnapshot(<AlarmSignal>[
        warning,
      ], observedAt: at.add(const Duration(minutes: 1)));
      await engine.markNotified(
        'temperature',
        notifiedAt: at.add(const Duration(minutes: 1)),
      );
      await engine.acknowledge(
        'temperature',
        acknowledgedAt: at.add(const Duration(minutes: 2)),
      );

      final escalated = await engine.processSnapshot(<AlarmSignal>[
        AlarmSignal(
          key: 'temperature',
          severity: AlarmSeverity.critical,
          title: 'Temperatura krytyczna',
          message: 'Natychmiast sprawdź chłodzenie.',
        ),
      ], observedAt: at.add(const Duration(minutes: 3)));

      expect(escalated.transitions.single.type, AlarmTransitionType.repeated);
      expect(
        escalated.notificationCandidates.single.severity,
        AlarmSeverity.critical,
      );
      expect(
        escalated.notificationCandidates.single.lifecycle,
        AlarmLifecycle.newAlarm,
      );
      expect(escalated.notificationCandidates.single.acknowledgedAt, isNull);
      expect(escalated.notificationCandidates.single.lastNotifiedAt, isNull);
    },
  );

  test('ponowne potwierdzenie jest idempotentne', () async {
    final at = DateTime.utc(2026, 7, 26, 12);
    final signal = AlarmSignal(
      key: 'leak',
      severity: AlarmSeverity.critical,
      title: 'Wyciek',
      message: 'Wykryto wodę.',
    );
    await engine.processSnapshot(<AlarmSignal>[signal], observedAt: at);

    expect(
      await engine.acknowledge(
        'leak',
        acknowledgedAt: at.add(const Duration(minutes: 1)),
      ),
      isNotNull,
    );
    expect(
      await engine.acknowledge(
        'leak',
        acknowledgedAt: at.add(const Duration(minutes: 2)),
      ),
      isNull,
    );
  });

  test('cofnięcie zegara nie uszkadza aktywnego alarmu', () async {
    final at = DateTime.utc(2026, 7, 26, 12);
    final signal = AlarmSignal(
      key: 'leak',
      severity: AlarmSeverity.critical,
      title: 'Wyciek',
      message: 'Wykryto wodę.',
    );
    await engine.processSnapshot(<AlarmSignal>[signal], observedAt: at);

    await expectLater(
      engine.processSnapshot(<AlarmSignal>[
        signal,
      ], observedAt: at.subtract(const Duration(minutes: 5))),
      completes,
    );
    expect((await store.list()).single.lastTriggeredAt, at);
  });

  test(
    'dwie czyste próbki rozwiązują alarm, kolejna aktywacja go otwiera',
    () async {
      final at = DateTime.utc(2026, 7, 26, 12);
      final signal = AlarmSignal(
        key: 'temperature_high',
        severity: AlarmSeverity.critical,
        title: 'Za gorąco',
        message: 'Sprawdź chłodzenie.',
      );
      await engine.processSnapshot(<AlarmSignal>[signal], observedAt: at);
      final firstClear = await engine.processSnapshot(
        const <AlarmSignal>[],
        observedAt: at.add(const Duration(minutes: 1)),
      );
      final resolved = await engine.processSnapshot(
        const <AlarmSignal>[],
        observedAt: at.add(const Duration(minutes: 2)),
      );
      final reopened = await engine.processSnapshot(<AlarmSignal>[
        signal,
      ], observedAt: at.add(const Duration(minutes: 3)));

      expect(firstClear.transitions, isEmpty);
      expect(resolved.transitions.single.type, AlarmTransitionType.resolved);
      expect(reopened.transitions.single.type, AlarmTransitionType.opened);
      expect((await store.list()).single.occurrences, 2);
    },
  );

  test('uszkodzony klucz alarmu jest odrzucany', () {
    expect(
      () => AlarmSignal(
        key: '../../token',
        severity: AlarmSeverity.warning,
        title: 'Alarm',
        message: 'Treść',
      ),
      throwsArgumentError,
    );
  });
}
