import 'dart:convert';

import 'package:shared_preferences/shared_preferences.dart';

/// Ostatni bezpiecznie utrwalony stan sterownika używany w trybie offline.
final class ControllerSnapshot {
  ControllerSnapshot({
    required Map<String, dynamic> status,
    required this.savedAt,
  }) : status = _deepFreezeMap(status);

  final Map<String, dynamic> status;
  final DateTime savedAt;

  static Map<String, dynamic> _deepFreezeMap(Map<String, dynamic> source) {
    return Map<String, dynamic>.unmodifiable(
      source.map((key, value) => MapEntry(key, _deepFreeze(value))),
    );
  }

  static Object? _deepFreeze(Object? value) {
    if (value is Map<String, dynamic>) {
      return _deepFreezeMap(value);
    }
    if (value is Map) {
      return _deepFreezeMap(
        value.map((key, item) => MapEntry(key.toString(), item)),
      );
    }
    if (value is List) {
      return List<Object?>.unmodifiable(value.map(_deepFreeze));
    }
    return value;
  }
}

/// Wersjonowany kodek ograniczający dane przed zapisaniem ich na urządzeniu.
final class ControllerSnapshotCodec {
  const ControllerSnapshotCodec();

  static const int schemaVersion = 1;
  static const int maximumEncodedBytes = 96 * 1024;
  static const int maximumDepth = 8;
  static const int maximumMapEntries = 128;
  static const int maximumListItems = 100;
  static const int maximumHistoryItems = 48;
  static const int maximumStringLength = 2048;
  static const int maximumKeyLength = 96;

  String? encode(Map<String, dynamic> status, DateTime savedAt) {
    final sanitized = _sanitizeMap(status, depth: 0);
    final encoded = jsonEncode(<String, Object?>{
      'schemaVersion': schemaVersion,
      'savedAt': savedAt.toUtc().toIso8601String(),
      'status': sanitized,
    });
    return utf8.encode(encoded).length <= maximumEncodedBytes ? encoded : null;
  }

  ControllerSnapshot? decode(String encoded) {
    if (utf8.encode(encoded).length > maximumEncodedBytes) {
      return null;
    }

    try {
      final decoded = jsonDecode(encoded);
      if (decoded is! Map) return null;
      if (decoded['schemaVersion'] != schemaVersion) return null;

      final savedAtValue = decoded['savedAt'];
      final statusValue = decoded['status'];
      if (savedAtValue is! String || statusValue is! Map) return null;

      final savedAt = DateTime.tryParse(savedAtValue);
      if (savedAt == null) return null;

      final status = statusValue.map(
        (key, value) => MapEntry(key.toString(), value),
      );
      return ControllerSnapshot(
        status: _sanitizeMap(status, depth: 0),
        savedAt: savedAt.toUtc(),
      );
    } on Object {
      return null;
    }
  }

  Map<String, dynamic> _sanitizeMap(
    Map<String, dynamic> source, {
    required int depth,
  }) {
    if (depth >= maximumDepth) return <String, dynamic>{};

    final result = <String, dynamic>{};
    for (final entry in source.entries) {
      if (result.length >= maximumMapEntries) break;

      final key = _truncate(entry.key, maximumKeyLength);
      if (key.isEmpty || _isSensitiveKey(key)) continue;

      final sanitized = _sanitizeValue(
        entry.value,
        depth: depth + 1,
        historyContext: _isHistoryKey(key),
      );
      if (!sanitized.isPresent) continue;
      result[key] = sanitized.value;
    }
    return result;
  }

  _SanitizedValue _sanitizeValue(
    Object? value, {
    required int depth,
    required bool historyContext,
  }) {
    if (depth > maximumDepth) return const _SanitizedValue.absent();
    if (value == null || value is bool || value is String) {
      return _SanitizedValue.present(
        value is String ? _truncate(value, maximumStringLength) : value,
      );
    }
    if (value is num) {
      return value.isFinite
          ? _SanitizedValue.present(value)
          : const _SanitizedValue.absent();
    }
    if (value is Map) {
      final map = value.map((key, item) => MapEntry(key.toString(), item));
      return _SanitizedValue.present(_sanitizeMap(map, depth: depth));
    }
    if (value is List) {
      final limit = historyContext ? maximumHistoryItems : maximumListItems;
      final result = <Object?>[];
      final start = historyContext && value.length > limit
          ? value.length - limit
          : 0;
      for (final item in value.skip(start).take(limit)) {
        final sanitized = _sanitizeValue(
          item,
          depth: depth + 1,
          historyContext: historyContext,
        );
        if (sanitized.isPresent) result.add(sanitized.value);
      }
      return _SanitizedValue.present(result);
    }
    return const _SanitizedValue.absent();
  }

  bool _isSensitiveKey(String key) {
    final normalized = key.toLowerCase();
    if (normalized.contains('password') ||
        normalized.contains('token') ||
        normalized.contains('secret')) {
      return true;
    }
    return normalized == 'pin' ||
        normalized == 'adminpin' ||
        normalized == 'userpin' ||
        normalized == 'otapin' ||
        normalized.endsWith('_pin');
  }

  bool _isHistoryKey(String key) {
    final normalized = key.toLowerCase();
    return normalized.contains('history') ||
        normalized.contains('historia') ||
        normalized.contains('events') ||
        normalized.contains('logs');
  }

  String _truncate(String value, int maximumLength) {
    return value.length <= maximumLength
        ? value
        : value.substring(0, maximumLength);
  }
}

final class _SanitizedValue {
  const _SanitizedValue.present(this.value) : isPresent = true;
  const _SanitizedValue.absent() : value = null, isPresent = false;

  final Object? value;
  final bool isPresent;
}

/// Repozytorium pojedynczego snapshotu. Uszkodzony wpis jest samonaprawialnie
/// usuwany, dzięki czemu nie blokuje kolejnych uruchomień aplikacji.
final class ControllerSnapshotCache {
  ControllerSnapshotCache({
    SharedPreferencesAsync? preferences,
    ControllerSnapshotCodec codec = const ControllerSnapshotCodec(),
  }) : _injectedPreferences = preferences,
       _codec = codec;

  static const String storageKey = 'controller_last_status_snapshot_v1';

  final SharedPreferencesAsync? _injectedPreferences;
  final ControllerSnapshotCodec _codec;
  SharedPreferencesAsync? _defaultPreferences;

  SharedPreferencesAsync get _preferences =>
      _injectedPreferences ??
      (_defaultPreferences ??= SharedPreferencesAsync());

  Future<bool> save(Map<String, dynamic> status, {DateTime? savedAt}) async {
    final encoded = _codec.encode(status, savedAt ?? DateTime.now());
    if (encoded == null) return false;

    try {
      await _preferences.setString(storageKey, encoded);
      return true;
    } on Object {
      return false;
    }
  }

  Future<ControllerSnapshot?> load() async {
    try {
      final encoded = await _preferences.getString(storageKey);
      if (encoded == null) return null;

      final snapshot = _codec.decode(encoded);
      if (snapshot != null) return snapshot;

      await _preferences.remove(storageKey);
      return null;
    } on Object {
      return null;
    }
  }

  Future<bool> clear() async {
    try {
      await _preferences.remove(storageKey);
      return true;
    } on Object {
      return false;
    }
  }
}
