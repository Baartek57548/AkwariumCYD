import 'dart:io';

import 'package:sqflite/sqflite.dart' show databaseFactory;
import 'package:sqflite_common/sqlite_api.dart';

import 'local_history_entry.dart';

final class LocalHistoryRetention {
  const LocalHistoryRetention({
    this.maximumEntries = 2000,
    this.maximumAge = const Duration(days: 366),
  }) : assert(maximumEntries > 0);

  final int maximumEntries;
  final Duration maximumAge;
}

/// Trwała, ograniczona baza historii. SQLite zapewnia atomowe zapisy również,
/// gdy odczyt tła i ekran aplikacji spotkają się w tym samym czasie.
final class LocalHistoryRepository {
  LocalHistoryRepository({
    DatabaseFactory? factory,
    String? databasePath,
    this.retention = const LocalHistoryRetention(),
  }) : _factory = factory ?? databaseFactory,
       _explicitDatabasePath = databasePath {
    if (retention.maximumAge.inMicroseconds <= 0) {
      throw ArgumentError.value(
        retention.maximumAge,
        'retention.maximumAge',
        'Okres retencji musi być dodatni.',
      );
    }
  }

  static const String databaseFileName = 'aquacyd_local_history_v1.db';
  static const int schemaVersion = 1;

  final DatabaseFactory _factory;
  final String? _explicitDatabasePath;
  final LocalHistoryRetention retention;
  Database? _database;
  Future<Database>? _opening;
  Future<void>? _closing;

  Future<Database> _open() {
    final closing = _closing;
    if (closing != null) {
      return closing.then((_) => _open());
    }
    final current = _database;
    if (current != null && current.isOpen) return Future.value(current);
    return _opening ??= _openDatabase().whenComplete(() => _opening = null);
  }

  /// Wspólny uchwyt dla repozytoriów alarmów i przypomnień korzystających
  /// z tej samej transakcyjnej bazy.
  Future<Database> get database => _open();

  Future<Database> _openDatabase() async {
    final path =
        _explicitDatabasePath ??
        _join(await _factory.getDatabasesPath(), databaseFileName);
    final db = await _factory.openDatabase(
      path,
      options: OpenDatabaseOptions(
        version: schemaVersion,
        onConfigure: (database) async {
          await database.execute('PRAGMA foreign_keys = ON');
          await database.execute('PRAGMA busy_timeout = 5000');
        },
        onCreate: (database, version) async {
          await _createSchema(database);
        },
      ),
    );
    _database = db;
    return db;
  }

  Future<void> append(LocalHistoryEntry entry) async {
    final db = await _open();
    await db.transaction((transaction) async {
      await transaction.insert(
        'history_entries',
        entry.toDatabase(),
        conflictAlgorithm: ConflictAlgorithm.ignore,
      );
      await _prune(transaction, now: entry.timestamp);
    });
  }

  Future<void> appendAll(Iterable<LocalHistoryEntry> entries) async {
    final materialized = entries.toList(growable: false);
    if (materialized.isEmpty) return;
    materialized.sort(
      (left, right) => left.timestamp.compareTo(right.timestamp),
    );
    final db = await _open();
    await db.transaction((transaction) async {
      final batch = transaction.batch();
      for (final entry in materialized) {
        batch.insert(
          'history_entries',
          entry.toDatabase(),
          conflictAlgorithm: ConflictAlgorithm.ignore,
        );
      }
      await batch.commit(noResult: true);
      await _prune(transaction, now: materialized.last.timestamp);
    });
  }

  Future<List<LocalHistoryEntry>> latest({
    int limit = 100,
    LocalHistoryCategory? category,
  }) async {
    final safeLimit = limit.clamp(1, 500);
    final rows = await (await _open()).query(
      'history_entries',
      where: category == null ? null : 'category = ?',
      whereArgs: category == null ? null : <Object?>[category.name],
      orderBy: 'timestamp_ms DESC',
      limit: safeLimit,
    );
    return rows
        .map(LocalHistoryEntry.tryFromDatabase)
        .whereType<LocalHistoryEntry>()
        .toList(growable: false);
  }

  Future<List<LocalHistoryEntry>> between(
    DateTime from,
    DateTime to, {
    int limit = 500,
  }) async {
    final start = from.toUtc();
    final end = to.toUtc();
    if (end.isBefore(start)) {
      throw ArgumentError('Koniec zakresu nie może poprzedzać początku.');
    }
    final rows = await (await _open()).query(
      'history_entries',
      where: 'timestamp_ms >= ? AND timestamp_ms <= ?',
      whereArgs: <Object?>[
        start.millisecondsSinceEpoch,
        end.millisecondsSinceEpoch,
      ],
      orderBy: 'timestamp_ms ASC',
      limit: limit.clamp(1, 2000),
    );
    return rows
        .map(LocalHistoryEntry.tryFromDatabase)
        .whereType<LocalHistoryEntry>()
        .toList(growable: false);
  }

  Future<int> count() async {
    final rows = await (await _open()).rawQuery(
      'SELECT COUNT(*) AS count FROM history_entries',
    );
    return (rows.firstOrNull?['count'] as int?) ?? 0;
  }

  Future<void> clear() async {
    await (await _open()).delete('history_entries');
  }

  Future<void> close() {
    final closing = _closing;
    if (closing != null) return closing;
    late final Future<void> operation;
    operation = _closeDatabase().whenComplete(() {
      if (identical(_closing, operation)) _closing = null;
    });
    _closing = operation;
    return operation;
  }

  Future<void> _closeDatabase() async {
    final opening = _opening;
    if (opening != null) {
      try {
        await opening;
      } on Object {
        // Nieudane otwarcie nie pozostawia uchwytu wymagającego zamknięcia.
      }
    }
    final db = _database;
    _database = null;
    if (db != null && db.isOpen) await db.close();
  }

  Future<void> _prune(
    DatabaseExecutor database, {
    required DateTime now,
  }) async {
    final cutoff = now.toUtc().subtract(retention.maximumAge);
    await database.delete(
      'history_entries',
      where: 'timestamp_ms < ?',
      whereArgs: <Object?>[cutoff.millisecondsSinceEpoch],
    );
    await database.rawDelete(
      '''
      DELETE FROM history_entries
      WHERE id IN (
        SELECT id
        FROM history_entries
        ORDER BY timestamp_ms DESC
        LIMIT -1 OFFSET ?
      )
      ''',
      <Object?>[retention.maximumEntries],
    );
  }

  static Future<void> _createSchema(Database database) async {
    await database.execute('''
      CREATE TABLE history_entries (
        id TEXT PRIMARY KEY NOT NULL,
        category TEXT NOT NULL,
        timestamp_ms INTEGER NOT NULL,
        title TEXT NOT NULL,
        detail TEXT NOT NULL,
        source TEXT NOT NULL,
        payload_json TEXT NOT NULL
      )
    ''');
    await database.execute(
      'CREATE INDEX history_entries_timestamp '
      'ON history_entries(timestamp_ms DESC)',
    );
    await AlarmDatabaseSchema.create(database);
    await ServiceReminderDatabaseSchema.create(database);
  }

  static String _join(String root, String child) {
    if (root.endsWith('/') || root.endsWith(r'\')) return '$root$child';
    return '$root${Platform.pathSeparator}$child';
  }
}

/// Część wspólnego schematu rezerwowana dla alarmów. Implementacja znajduje
/// się w alarm_center, a brak importu odwrotnego zapobiega cyklom bibliotek.
abstract final class AlarmDatabaseSchema {
  static Future<void> create(Database database) async {
    await database.execute('''
      CREATE TABLE alarm_records (
        alarm_key TEXT PRIMARY KEY NOT NULL,
        severity TEXT NOT NULL,
        lifecycle TEXT NOT NULL,
        title TEXT NOT NULL,
        message TEXT NOT NULL,
        first_triggered_ms INTEGER NOT NULL,
        last_triggered_ms INTEGER NOT NULL,
        acknowledged_ms INTEGER,
        resolved_ms INTEGER,
        last_notified_ms INTEGER,
        occurrences INTEGER NOT NULL
      )
    ''');
    await database.execute('''
      CREATE TABLE alarm_signal_state (
        alarm_key TEXT PRIMARY KEY NOT NULL,
        active_streak INTEGER NOT NULL,
        clear_streak INTEGER NOT NULL,
        last_seen_ms INTEGER NOT NULL
      )
    ''');
  }
}

abstract final class ServiceReminderDatabaseSchema {
  static Future<void> create(Database database) async {
    await database.execute('''
      CREATE TABLE service_reminders (
        reminder_id TEXT PRIMARY KEY NOT NULL,
        title TEXT NOT NULL,
        detail TEXT NOT NULL,
        interval_days INTEGER NOT NULL,
        enabled INTEGER NOT NULL,
        last_completed_ms INTEGER,
        last_notified_ms INTEGER,
        created_ms INTEGER NOT NULL
      )
    ''');
  }
}
