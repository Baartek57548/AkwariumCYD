import 'dart:collection';
import 'dart:convert';

enum LocalHistoryCategory {
  measurement,
  alarm,
  command,
  service,
  synchronization,
}

/// Niewielki, niezależny od UI rekord historii przeznaczony do pracy offline.
///
/// Konstruktor celowo odrzuca nadmiarowe i wrażliwe pola. Historia nie jest
/// magazynem konfiguracji ani sekretów sterownika.
final class LocalHistoryEntry {
  LocalHistoryEntry({
    required String id,
    required this.category,
    required this.timestamp,
    required String title,
    required String detail,
    required String source,
    Map<String, Object?> values = const <String, Object?>{},
  }) : id = _validatedId(id),
       title = _cleanText(title, maximumLength: 160, fieldName: 'title'),
       detail = _cleanText(
         detail,
         maximumLength: 800,
         fieldName: 'detail',
         allowEmpty: true,
       ),
       source = _cleanText(source, maximumLength: 48, fieldName: 'source'),
       values = UnmodifiableMapView(_sanitizeValues(values)) {
    final year = timestamp.toUtc().year;
    if (year < 2020 || year > 2200) {
      throw ArgumentError.value(timestamp, 'timestamp', 'Data poza zakresem.');
    }
  }

  final String id;
  final LocalHistoryCategory category;
  final DateTime timestamp;
  final String title;
  final String detail;
  final String source;
  final UnmodifiableMapView<String, Object?> values;

  Map<String, Object?> toDatabase() => <String, Object?>{
    'id': id,
    'category': category.name,
    'timestamp_ms': timestamp.toUtc().millisecondsSinceEpoch,
    'title': title,
    'detail': detail,
    'source': source,
    'payload_json': jsonEncode(values),
  };

  static LocalHistoryEntry? tryFromDatabase(Map<String, Object?> row) {
    try {
      final timestampMs = row['timestamp_ms'];
      final categoryName = row['category'];
      final payloadText = row['payload_json'];
      if (timestampMs is! int ||
          categoryName is! String ||
          payloadText is! String) {
        return null;
      }
      final category = LocalHistoryCategory.values
          .where((candidate) => candidate.name == categoryName)
          .firstOrNull;
      if (category == null) return null;
      final decoded = jsonDecode(payloadText);
      if (decoded is! Map) return null;
      return LocalHistoryEntry(
        id: row['id']?.toString() ?? '',
        category: category,
        timestamp: DateTime.fromMillisecondsSinceEpoch(
          timestampMs,
          isUtc: true,
        ),
        title: row['title']?.toString() ?? '',
        detail: row['detail']?.toString() ?? '',
        source: row['source']?.toString() ?? '',
        values: decoded.map(
          (Object? key, Object? value) => MapEntry(key.toString(), value),
        ),
      );
    } on Object {
      return null;
    }
  }

  static String createId({
    required DateTime timestamp,
    required LocalHistoryCategory category,
    required String discriminator,
  }) {
    final seed =
        '${timestamp.toUtc().microsecondsSinceEpoch}|${category.name}|$discriminator';
    var hash = 0x811c9dc5;
    for (final unit in seed.codeUnits) {
      hash ^= unit;
      hash = (hash * 0x01000193) & 0x7fffffff;
    }
    return '${timestamp.toUtc().microsecondsSinceEpoch}-${hash.toRadixString(16)}';
  }

  static String _validatedId(String value) {
    final candidate = value.trim();
    if (!RegExp(r'^[a-zA-Z0-9_.:-]{1,96}$').hasMatch(candidate)) {
      throw ArgumentError.value(value, 'id', 'Nieprawidłowy identyfikator.');
    }
    return candidate;
  }

  static String _cleanText(
    String value, {
    required int maximumLength,
    required String fieldName,
    bool allowEmpty = false,
  }) {
    final normalized = value
        .replaceAll(RegExp(r'[\u0000-\u0008\u000B\u000C\u000E-\u001F]'), '')
        .trim();
    if (!allowEmpty && normalized.isEmpty) {
      throw ArgumentError.value(value, fieldName, 'Pole nie może być puste.');
    }
    return normalized.length <= maximumLength
        ? normalized
        : normalized.substring(0, maximumLength);
  }

  static Map<String, Object?> _sanitizeValues(Map<String, Object?> source) {
    final result = <String, Object?>{};
    for (final entry in source.entries) {
      if (result.length >= 24) break;
      final key = entry.key.trim();
      if (!RegExp(r'^[a-zA-Z0-9_.-]{1,48}$').hasMatch(key) ||
          _isSensitiveKey(key)) {
        continue;
      }
      final value = entry.value;
      if (value == null || value is bool) {
        result[key] = value;
      } else if (value is num && value.isFinite) {
        result[key] = value;
      } else if (value is String) {
        result[key] = _cleanText(
          value,
          maximumLength: 160,
          fieldName: key,
          allowEmpty: true,
        );
      }
    }
    return result;
  }

  static bool _isSensitiveKey(String key) {
    final normalized = key.toLowerCase();
    return normalized.contains('password') ||
        normalized.contains('secret') ||
        normalized.contains('token') ||
        normalized == 'pin' ||
        normalized.endsWith('_pin') ||
        normalized.contains('authorization');
  }
}
