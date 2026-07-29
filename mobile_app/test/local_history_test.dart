import 'package:cyd_aquarium_mobile/alarm_center/alarm_models.dart';
import 'package:cyd_aquarium_mobile/alarm_center/alarm_notifications.dart';
import 'package:cyd_aquarium_mobile/local_history/local_history_entry.dart';
import 'package:cyd_aquarium_mobile/local_history/local_history_repository.dart';
import 'package:cyd_aquarium_mobile/local_history/service_reminders.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:sqflite_common_ffi/sqflite_ffi.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  setUpAll(sqfliteFfiInit);

  late LocalHistoryRepository repository;

  setUp(() {
    repository = LocalHistoryRepository(
      factory: databaseFactoryFfi,
      databasePath: inMemoryDatabasePath,
      retention: const LocalHistoryRetention(maximumEntries: 3),
    );
  });

  tearDown(() => repository.close());

  test('SQLite zachowuje limit i kolejność najnowszych rekordów', () async {
    for (var index = 0; index < 4; index++) {
      final at = DateTime.utc(2026, 7, 26, 10, index);
      await repository.append(
        LocalHistoryEntry(
          id: 'entry-$index',
          category: LocalHistoryCategory.measurement,
          timestamp: at,
          title: 'Pomiar $index',
          detail: '',
          source: 'test',
          values: <String, Object?>{'temperature': 24 + index},
        ),
      );
    }

    final entries = await repository.latest();

    expect(await repository.count(), 3);
    expect(entries.map((entry) => entry.id), <String>[
      'entry-3',
      'entry-2',
      'entry-1',
    ]);
  });

  test('payload usuwa sekrety, nadmiar i wartości niefinitywne', () {
    final entry = LocalHistoryEntry(
      id: 'safe-entry',
      category: LocalHistoryCategory.command,
      timestamp: DateTime.utc(2026, 7, 26),
      title: 'Polecenie',
      detail: '',
      source: 'mobile',
      values: <String, Object?>{
        'temperature': 25.2,
        'wifi_password': 'sekret',
        'authToken': 'sekret',
        'pin': '1234',
        'invalid': double.nan,
      },
    );

    expect(entry.values, <String, Object?>{'temperature': 25.2});
    expect(() => entry.values['temperature'] = 30, throwsUnsupportedError);
  });

  test('przypomnienia są trwałe, mają cooldown i zapisują wykonanie', () async {
    final notifications = _FakeNotifications();
    final manager = ServiceReminderManager(
      repository: ServiceReminderRepository(repository),
      history: repository,
      notifications: notifications,
    );
    final created = DateTime.utc(2026, 1, 1);
    await manager.ensureDefaults(createdAt: created);

    final first = await manager.notifyDue(
      checkedAt: created.add(const Duration(days: 91)),
    );
    final repeatedTooSoon = await manager.notifyDue(
      checkedAt: created.add(const Duration(days: 91, hours: 2)),
    );
    final completed = await manager.complete(
      'water_change',
      completedAt: created.add(const Duration(days: 91, hours: 3)),
    );

    expect(first, hasLength(3));
    expect(repeatedTooSoon, isEmpty);
    expect(notifications.serviceIds.toSet(), {
      'water_change',
      'filter_cleaning',
      'probe_calibration',
    });
    expect(completed.lastCompletedAt, isNotNull);
    expect(
      await repository.latest(category: LocalHistoryCategory.service),
      hasLength(1),
    );
  });

  test('błąd kanału przypomnienia nie uruchamia cooldownu', () async {
    final notifications = _FakeNotifications()..failService = true;
    final reminderRepository = ServiceReminderRepository(repository);
    final manager = ServiceReminderManager(
      repository: reminderRepository,
      history: repository,
      notifications: notifications,
    );
    final created = DateTime.utc(2026, 1, 1);
    await manager.ensureDefaults(createdAt: created);

    expect(
      await manager.notifyDue(checkedAt: created.add(const Duration(days: 91))),
      isEmpty,
    );
    expect(
      (await reminderRepository.get('water_change'))?.lastNotifiedAt,
      isNull,
    );

    notifications.failService = false;
    expect(
      await manager.notifyDue(
        checkedAt: created.add(const Duration(days: 91, minutes: 1)),
      ),
      hasLength(3),
    );
  });
}

final class _FakeNotifications implements AlarmNotificationSink {
  bool failService = false;
  final List<String> serviceIds = <String>[];

  @override
  Future<void> cancelAlarm(String alarmKey) async {}

  @override
  Future<void> initialize() async {}

  @override
  Future<bool> requestPermission() async => true;

  @override
  Future<void> showTestNotification() async {}

  @override
  Future<void> showAlarm(AlarmRecord alarm) async {}

  @override
  Future<void> showResolved(AlarmRecord alarm) async {}

  @override
  Future<void> showServiceReminder({
    required String id,
    required String title,
    required String body,
  }) async {
    if (failService) throw StateError('Brak kanału systemowego.');
    serviceIds.add(id);
  }
}
