import 'package:sqflite_common/sqlite_api.dart';

import '../local_history/local_history_repository.dart';
import 'alarm_models.dart';

abstract interface class AlarmStoreTransaction {
  Future<Map<String, AlarmRecord>> records();
  Future<Map<String, AlarmSignalState>> signalStates();
  Future<void> saveRecord(AlarmRecord record);
  Future<void> saveSignalState(AlarmSignalState state);
  Future<void> deleteSignalState(String key);
}

abstract interface class AlarmStore {
  Future<T> transaction<T>(
    Future<T> Function(AlarmStoreTransaction transaction) action,
  );
  Future<List<AlarmRecord>> list({
    bool includeResolved = true,
    int limit = 200,
  });
}

final class SqliteAlarmStore implements AlarmStore {
  const SqliteAlarmStore(this.database);

  final LocalHistoryRepository database;

  @override
  Future<T> transaction<T>(
    Future<T> Function(AlarmStoreTransaction transaction) action,
  ) async {
    final db = await database.database;
    return db.transaction(
      (transaction) => action(_SqliteAlarmTransaction(transaction)),
    );
  }

  @override
  Future<List<AlarmRecord>> list({
    bool includeResolved = true,
    int limit = 200,
  }) async {
    final rows = await (await database.database).query(
      'alarm_records',
      where: includeResolved ? null : 'lifecycle != ?',
      whereArgs: includeResolved
          ? null
          : <Object?>[AlarmLifecycle.resolved.storageValue],
      orderBy: 'last_triggered_ms DESC',
      limit: limit.clamp(1, 500),
    );
    return rows
        .map(AlarmRecord.tryFromDatabase)
        .whereType<AlarmRecord>()
        .toList(growable: false);
  }
}

final class _SqliteAlarmTransaction implements AlarmStoreTransaction {
  const _SqliteAlarmTransaction(this.transaction);

  final Transaction transaction;

  @override
  Future<Map<String, AlarmRecord>> records() async {
    final rows = await transaction.query('alarm_records');
    return <String, AlarmRecord>{
      for (final record
          in rows.map(AlarmRecord.tryFromDatabase).whereType<AlarmRecord>())
        record.key: record,
    };
  }

  @override
  Future<Map<String, AlarmSignalState>> signalStates() async {
    final rows = await transaction.query('alarm_signal_state');
    return <String, AlarmSignalState>{
      for (final state
          in rows
              .map(AlarmSignalState.tryFromDatabase)
              .whereType<AlarmSignalState>())
        state.key: state,
    };
  }

  @override
  Future<void> saveRecord(AlarmRecord record) async {
    await transaction.insert(
      'alarm_records',
      record.toDatabase(),
      conflictAlgorithm: ConflictAlgorithm.replace,
    );
    await transaction.rawDelete(
      '''
      DELETE FROM alarm_records
      WHERE alarm_key IN (
        SELECT alarm_key
        FROM alarm_records
        WHERE lifecycle = ?
        ORDER BY resolved_ms DESC
        LIMIT -1 OFFSET 200
      )
      ''',
      <Object?>[AlarmLifecycle.resolved.storageValue],
    );
  }

  @override
  Future<void> saveSignalState(AlarmSignalState state) {
    return transaction.insert(
      'alarm_signal_state',
      state.toDatabase(),
      conflictAlgorithm: ConflictAlgorithm.replace,
    );
  }

  @override
  Future<void> deleteSignalState(String key) {
    return transaction.delete(
      'alarm_signal_state',
      where: 'alarm_key = ?',
      whereArgs: <Object?>[key],
    );
  }
}
