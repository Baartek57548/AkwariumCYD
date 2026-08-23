import 'package:sqflite_common/sqlite_api.dart';

import '../alarm_center/alarm_notifications.dart';
import 'local_history_entry.dart';
import 'local_history_repository.dart';

final class ServiceReminder {
  ServiceReminder({
    required String id,
    required String title,
    required String detail,
    required this.interval,
    required this.createdAt,
    this.enabled = true,
    this.lastCompletedAt,
    this.lastNotifiedAt,
  }) : id = _id(id),
       title = _text(title, 'title', 120),
       detail = _text(detail, 'detail', 400) {
    if (interval < const Duration(days: 1) ||
        interval > const Duration(days: 3660)) {
      throw ArgumentError.value(interval, 'interval');
    }
  }

  final String id;
  final String title;
  final String detail;
  final Duration interval;
  final bool enabled;
  final DateTime createdAt;
  final DateTime? lastCompletedAt;
  final DateTime? lastNotifiedAt;

  DateTime get dueAt => (lastCompletedAt ?? createdAt).toUtc().add(interval);

  bool isDue(DateTime now) => enabled && !dueAt.isAfter(now.toUtc());

  ServiceReminder copyWith({
    String? title,
    String? detail,
    Duration? interval,
    bool? enabled,
    DateTime? lastCompletedAt,
    DateTime? lastNotifiedAt,
  }) {
    return ServiceReminder(
      id: id,
      title: title ?? this.title,
      detail: detail ?? this.detail,
      interval: interval ?? this.interval,
      enabled: enabled ?? this.enabled,
      createdAt: createdAt,
      lastCompletedAt: lastCompletedAt ?? this.lastCompletedAt,
      lastNotifiedAt: lastNotifiedAt ?? this.lastNotifiedAt,
    );
  }

  Map<String, Object?> toDatabase() => <String, Object?>{
    'reminder_id': id,
    'title': title,
    'detail': detail,
    'interval_days': interval.inDays,
    'enabled': enabled ? 1 : 0,
    'last_completed_ms': lastCompletedAt?.toUtc().millisecondsSinceEpoch,
    'last_notified_ms': lastNotifiedAt?.toUtc().millisecondsSinceEpoch,
    'created_ms': createdAt.toUtc().millisecondsSinceEpoch,
  };

  static ServiceReminder? tryFromDatabase(Map<String, Object?> row) {
    try {
      final intervalDays = row['interval_days'];
      final createdMs = row['created_ms'];
      if (intervalDays is! int || createdMs is! int) return null;
      DateTime? optional(String key) {
        final value = row[key];
        return value is int
            ? DateTime.fromMillisecondsSinceEpoch(value, isUtc: true)
            : null;
      }

      return ServiceReminder(
        id: row['reminder_id']?.toString() ?? '',
        title: row['title']?.toString() ?? '',
        detail: row['detail']?.toString() ?? '',
        interval: Duration(days: intervalDays),
        enabled: row['enabled'] == 1,
        createdAt: DateTime.fromMillisecondsSinceEpoch(createdMs, isUtc: true),
        lastCompletedAt: optional('last_completed_ms'),
        lastNotifiedAt: optional('last_notified_ms'),
      );
    } on Object {
      return null;
    }
  }

  static String _id(String value) {
    final normalized = value.trim().toLowerCase();
    if (!RegExp(r'^[a-z0-9_.-]{1,64}$').hasMatch(normalized)) {
      throw ArgumentError.value(value, 'id');
    }
    return normalized;
  }

  static String _text(String value, String field, int maximumLength) {
    final normalized = value.trim();
    if (normalized.isEmpty) throw ArgumentError.value(value, field);
    return normalized.length <= maximumLength
        ? normalized
        : normalized.substring(0, maximumLength);
  }
}

final class ServiceReminderRepository {
  const ServiceReminderRepository(this.database);

  final LocalHistoryRepository database;

  Future<void> save(ServiceReminder reminder) async {
    await (await database.database).insert(
      'service_reminders',
      reminder.toDatabase(),
      conflictAlgorithm: ConflictAlgorithm.replace,
    );
  }

  Future<void> insertIfAbsent(ServiceReminder reminder) async {
    await (await database.database).insert(
      'service_reminders',
      reminder.toDatabase(),
      conflictAlgorithm: ConflictAlgorithm.ignore,
    );
  }

  Future<ServiceReminder?> get(String id) async {
    final rows = await (await database.database).query(
      'service_reminders',
      where: 'reminder_id = ?',
      whereArgs: <Object?>[id],
      limit: 1,
    );
    return rows.isEmpty ? null : ServiceReminder.tryFromDatabase(rows.first);
  }

  Future<List<ServiceReminder>> list({bool enabledOnly = false}) async {
    final rows = await (await database.database).query(
      'service_reminders',
      where: enabledOnly ? 'enabled = 1' : null,
      orderBy: 'title COLLATE NOCASE ASC',
    );
    return rows
        .map(ServiceReminder.tryFromDatabase)
        .whereType<ServiceReminder>()
        .toList(growable: false);
  }

  Future<void> delete(String id) async {
    await (await database.database).delete(
      'service_reminders',
      where: 'reminder_id = ?',
      whereArgs: <Object?>[id],
    );
  }
}

final class ServiceReminderManager {
  const ServiceReminderManager({
    required this.repository,
    required this.history,
    required this.notifications,
  });

  final ServiceReminderRepository repository;
  final LocalHistoryRepository history;
  final AlarmNotificationSink notifications;

  Future<void> ensureDefaults({DateTime? createdAt}) async {
    final now = (createdAt ?? DateTime.now()).toUtc();
    for (final reminder in <ServiceReminder>[
      ServiceReminder(
        id: 'water_change',
        title: 'Wymiana wody',
        detail: 'Wykonaj częściową podmianę wody i sprawdź jej parametry.',
        interval: const Duration(days: 7),
        createdAt: now,
      ),
      ServiceReminder(
        id: 'filter_cleaning',
        title: 'Czyszczenie filtra',
        detail: 'Sprawdź przepływ i oczyść wkłady w wodzie z akwarium.',
        interval: const Duration(days: 30),
        createdAt: now,
      ),
      ServiceReminder(
        id: 'probe_calibration',
        title: 'Kalibracja sond',
        detail: 'Skalibruj sondę pH/EC zgodnie z instrukcją producenta.',
        interval: const Duration(days: 90),
        createdAt: now,
      ),
    ]) {
      await repository.insertIfAbsent(reminder);
    }
  }

  Future<List<ServiceReminder>> notifyDue({
    DateTime? checkedAt,
    Duration repeatCooldown = const Duration(hours: 24),
  }) async {
    final now = (checkedAt ?? DateTime.now()).toUtc();
    final notified = <ServiceReminder>[];
    for (final reminder in await repository.list(enabledOnly: true)) {
      if (!reminder.isDue(now)) continue;
      final previous = reminder.lastNotifiedAt;
      if (previous != null && now.difference(previous) < repeatCooldown) {
        continue;
      }
      // Nie oznaczamy przypomnienia jako wysłanego, jeśli kanał systemowy
      // odmówił publikacji. Dzięki temu kolejny przebieg może spróbować ponownie.
      try {
        await notifications.showServiceReminder(
          id: reminder.id,
          title: reminder.title,
          body: reminder.detail,
        );
      } on Object {
        continue;
      }
      final updated = reminder.copyWith(lastNotifiedAt: now);
      await repository.save(updated);
      notified.add(updated);
    }
    return List<ServiceReminder>.unmodifiable(notified);
  }

  Future<ServiceReminder> complete(String id, {DateTime? completedAt}) async {
    final reminder = await repository.get(id);
    if (reminder == null) {
      throw StateError('Nie znaleziono przypomnienia: $id.');
    }
    final at = (completedAt ?? DateTime.now()).toUtc();
    final updated = reminder.copyWith(lastCompletedAt: at);
    await repository.save(updated);
    try {
      await history.append(
        LocalHistoryEntry(
          id: LocalHistoryEntry.createId(
            timestamp: at,
            category: LocalHistoryCategory.service,
            discriminator: id,
          ),
          category: LocalHistoryCategory.service,
          timestamp: at,
          title: reminder.title,
          detail: 'Zadanie serwisowe oznaczone jako wykonane.',
          source: 'mobile',
        ),
      );
    } on Object {
      // Wykonanie zadania jest stanem nadrzędnym; błąd pomocniczej historii
      // nie może cofnąć poprawnie zapisanego terminu serwisu.
    }
    return updated;
  }
}
