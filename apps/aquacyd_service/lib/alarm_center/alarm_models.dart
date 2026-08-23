enum AlarmSeverity { warning, critical }

enum AlarmLifecycle {
  newAlarm('new'),
  acknowledged('acknowledged'),
  resolved('resolved');

  const AlarmLifecycle(this.storageValue);
  final String storageValue;

  static AlarmLifecycle? tryParse(Object? value) {
    return values.where((item) => item.storageValue == value).firstOrNull;
  }
}

final class AlarmSignal {
  AlarmSignal({
    required String key,
    required this.severity,
    required String title,
    required String message,
  }) : key = _alarmKey(key),
       title = _alarmText(title, 'title', 120),
       message = _alarmText(message, 'message', 400);

  final String key;
  final AlarmSeverity severity;
  final String title;
  final String message;
}

final class AlarmRecord {
  AlarmRecord({
    required String key,
    required this.severity,
    required this.lifecycle,
    required String title,
    required String message,
    required this.firstTriggeredAt,
    required this.lastTriggeredAt,
    required this.occurrences,
    this.acknowledgedAt,
    this.resolvedAt,
    this.lastNotifiedAt,
  }) : key = _alarmKey(key),
       title = _alarmText(title, 'title', 120),
       message = _alarmText(message, 'message', 400) {
    if (occurrences < 1) {
      throw ArgumentError.value(occurrences, 'occurrences');
    }
    if (lastTriggeredAt.isBefore(firstTriggeredAt)) {
      throw ArgumentError('lastTriggeredAt poprzedza firstTriggeredAt.');
    }
    if (lifecycle == AlarmLifecycle.resolved && resolvedAt == null) {
      throw ArgumentError('Rozwiązany alarm wymaga resolvedAt.');
    }
  }

  final String key;
  final AlarmSeverity severity;
  final AlarmLifecycle lifecycle;
  final String title;
  final String message;
  final DateTime firstTriggeredAt;
  final DateTime lastTriggeredAt;
  final DateTime? acknowledgedAt;
  final DateTime? resolvedAt;
  final DateTime? lastNotifiedAt;
  final int occurrences;

  bool get isActive => lifecycle != AlarmLifecycle.resolved;

  AlarmRecord copyWith({
    AlarmSeverity? severity,
    AlarmLifecycle? lifecycle,
    String? title,
    String? message,
    DateTime? lastTriggeredAt,
    DateTime? acknowledgedAt,
    bool clearAcknowledgedAt = false,
    DateTime? resolvedAt,
    bool clearResolvedAt = false,
    DateTime? lastNotifiedAt,
    bool clearLastNotifiedAt = false,
    int? occurrences,
  }) {
    return AlarmRecord(
      key: key,
      severity: severity ?? this.severity,
      lifecycle: lifecycle ?? this.lifecycle,
      title: title ?? this.title,
      message: message ?? this.message,
      firstTriggeredAt: firstTriggeredAt,
      lastTriggeredAt: lastTriggeredAt ?? this.lastTriggeredAt,
      acknowledgedAt: clearAcknowledgedAt
          ? null
          : acknowledgedAt ?? this.acknowledgedAt,
      resolvedAt: clearResolvedAt ? null : resolvedAt ?? this.resolvedAt,
      lastNotifiedAt: clearLastNotifiedAt
          ? null
          : lastNotifiedAt ?? this.lastNotifiedAt,
      occurrences: occurrences ?? this.occurrences,
    );
  }

  Map<String, Object?> toDatabase() => <String, Object?>{
    'alarm_key': key,
    'severity': severity.name,
    'lifecycle': lifecycle.storageValue,
    'title': title,
    'message': message,
    'first_triggered_ms': firstTriggeredAt.toUtc().millisecondsSinceEpoch,
    'last_triggered_ms': lastTriggeredAt.toUtc().millisecondsSinceEpoch,
    'acknowledged_ms': acknowledgedAt?.toUtc().millisecondsSinceEpoch,
    'resolved_ms': resolvedAt?.toUtc().millisecondsSinceEpoch,
    'last_notified_ms': lastNotifiedAt?.toUtc().millisecondsSinceEpoch,
    'occurrences': occurrences,
  };

  static AlarmRecord? tryFromDatabase(Map<String, Object?> row) {
    try {
      final severity = AlarmSeverity.values
          .where((item) => item.name == row['severity'])
          .firstOrNull;
      final lifecycle = AlarmLifecycle.tryParse(row['lifecycle']);
      final first = row['first_triggered_ms'];
      final last = row['last_triggered_ms'];
      final occurrences = row['occurrences'];
      if (severity == null ||
          lifecycle == null ||
          first is! int ||
          last is! int ||
          occurrences is! int) {
        return null;
      }
      DateTime? optionalDate(String key) {
        final value = row[key];
        return value is int
            ? DateTime.fromMillisecondsSinceEpoch(value, isUtc: true)
            : null;
      }

      return AlarmRecord(
        key: row['alarm_key']?.toString() ?? '',
        severity: severity,
        lifecycle: lifecycle,
        title: row['title']?.toString() ?? '',
        message: row['message']?.toString() ?? '',
        firstTriggeredAt: DateTime.fromMillisecondsSinceEpoch(
          first,
          isUtc: true,
        ),
        lastTriggeredAt: DateTime.fromMillisecondsSinceEpoch(last, isUtc: true),
        acknowledgedAt: optionalDate('acknowledged_ms'),
        resolvedAt: optionalDate('resolved_ms'),
        lastNotifiedAt: optionalDate('last_notified_ms'),
        occurrences: occurrences,
      );
    } on Object {
      return null;
    }
  }
}

final class AlarmSignalState {
  const AlarmSignalState({
    required this.key,
    required this.activeStreak,
    required this.clearStreak,
    required this.lastSeenAt,
  });

  final String key;
  final int activeStreak;
  final int clearStreak;
  final DateTime lastSeenAt;

  Map<String, Object?> toDatabase() => <String, Object?>{
    'alarm_key': key,
    'active_streak': activeStreak.clamp(0, 1000),
    'clear_streak': clearStreak.clamp(0, 1000),
    'last_seen_ms': lastSeenAt.toUtc().millisecondsSinceEpoch,
  };

  static AlarmSignalState? tryFromDatabase(Map<String, Object?> row) {
    final key = row['alarm_key'];
    final active = row['active_streak'];
    final clear = row['clear_streak'];
    final seen = row['last_seen_ms'];
    if (key is! String || active is! int || clear is! int || seen is! int) {
      return null;
    }
    return AlarmSignalState(
      key: key,
      activeStreak: active.clamp(0, 1000),
      clearStreak: clear.clamp(0, 1000),
      lastSeenAt: DateTime.fromMillisecondsSinceEpoch(seen, isUtc: true),
    );
  }
}

enum AlarmTransitionType { opened, repeated, acknowledged, resolved }

final class AlarmTransition {
  const AlarmTransition({
    required this.type,
    required this.record,
    required this.occurredAt,
  });

  final AlarmTransitionType type;
  final AlarmRecord record;
  final DateTime occurredAt;
}

final class AlarmProcessingResult {
  const AlarmProcessingResult({
    required this.transitions,
    required this.notificationCandidates,
  });

  final List<AlarmTransition> transitions;
  final List<AlarmRecord> notificationCandidates;
}

String _alarmKey(String value) {
  final normalized = value.trim().toLowerCase();
  if (!RegExp(r'^[a-z0-9_.:-]{1,64}$').hasMatch(normalized)) {
    throw ArgumentError.value(value, 'key', 'Nieprawidłowy klucz alarmu.');
  }
  return normalized;
}

String _alarmText(String value, String field, int maximumLength) {
  final normalized = value
      .replaceAll(RegExp(r'[\u0000-\u0008\u000B\u000C\u000E-\u001F]'), '')
      .trim();
  if (normalized.isEmpty) {
    throw ArgumentError.value(value, field, 'Pole nie może być puste.');
  }
  return normalized.length <= maximumLength
      ? normalized
      : normalized.substring(0, maximumLength);
}
