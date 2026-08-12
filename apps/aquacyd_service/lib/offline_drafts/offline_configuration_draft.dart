import 'dart:convert';

import 'package:crypto/crypto.dart';
import 'package:flutter_secure_storage/flutter_secure_storage.dart';

enum OfflineDraftKind { schedule, settings }

final class OfflineDraftDiffEntry {
  const OfflineDraftDiffEntry({
    required this.path,
    required this.before,
    required this.after,
  });

  final String path;
  final Object? before;
  final Object? after;

  String get beforeLabel => _label(before);
  String get afterLabel => _label(after);

  static String _label(Object? value) {
    if (value == null) return 'brak';
    if (value is bool) return value ? 'włączone' : 'wyłączone';
    final text = value.toString();
    return text.length <= 80 ? text : '${text.substring(0, 77)}…';
  }
}

final class OfflineConfigurationDraft {
  OfflineConfigurationDraft({
    required this.kind,
    required String controllerId,
    required String baseVersion,
    required Map<String, Object?> baseData,
    required Map<String, Object?> editedData,
    required this.createdAt,
    required this.updatedAt,
  }) : controllerId = _validateControllerId(controllerId),
       baseVersion = _validateFingerprint(baseVersion),
       baseData = _sanitizeMap(baseData),
       editedData = _sanitizeMap(editedData) {
    if (createdAt.toUtc().year < 2020 ||
        updatedAt.toUtc().isBefore(createdAt.toUtc())) {
      throw ArgumentError('Daty szkicu offline są nieprawidłowe.');
    }
  }

  factory OfflineConfigurationDraft.create({
    required OfflineDraftKind kind,
    required String controllerId,
    required Map<String, Object?> baseData,
    required Map<String, Object?> editedData,
    DateTime? now,
    DateTime? createdAt,
  }) {
    final timestamp = (now ?? DateTime.now()).toUtc();
    return OfflineConfigurationDraft(
      kind: kind,
      controllerId: controllerId,
      baseVersion: fingerprint(baseData),
      baseData: baseData,
      editedData: editedData,
      createdAt: (createdAt ?? timestamp).toUtc(),
      updatedAt: timestamp,
    );
  }

  final OfflineDraftKind kind;
  final String controllerId;
  final String baseVersion;
  final Map<String, Object?> baseData;
  final Map<String, Object?> editedData;
  final DateTime createdAt;
  final DateTime updatedAt;

  bool conflictsWith(Map<String, Object?> currentData) {
    return fingerprint(currentData) != baseVersion;
  }

  List<OfflineDraftDiffEntry> get diff {
    final result = <OfflineDraftDiffEntry>[];
    _collectDiff('', baseData, editedData, result);
    return List<OfflineDraftDiffEntry>.unmodifiable(result);
  }

  Map<String, Object?> toJson() => <String, Object?>{
    'schema': 1,
    'kind': kind.name,
    'controllerId': controllerId,
    'baseVersion': baseVersion,
    'baseData': baseData,
    'editedData': editedData,
    'createdAt': createdAt.toUtc().toIso8601String(),
    'updatedAt': updatedAt.toUtc().toIso8601String(),
  };

  static OfflineConfigurationDraft? tryFromJson(Object? value) {
    if (value is! Map) return null;
    if (value['schema'] != 1) return null;
    final kindName = value['kind'];
    final controllerId = value['controllerId'];
    final baseVersion = value['baseVersion'];
    final baseData = value['baseData'];
    final editedData = value['editedData'];
    final createdAt = value['createdAt'];
    final updatedAt = value['updatedAt'];
    if (kindName is! String ||
        controllerId is! String ||
        baseVersion is! String ||
        baseData is! Map ||
        editedData is! Map ||
        createdAt is! String ||
        updatedAt is! String) {
      return null;
    }
    final kind = OfflineDraftKind.values
        .where((candidate) => candidate.name == kindName)
        .firstOrNull;
    final created = DateTime.tryParse(createdAt);
    final updated = DateTime.tryParse(updatedAt);
    if (kind == null || created == null || updated == null) return null;
    try {
      return OfflineConfigurationDraft(
        kind: kind,
        controllerId: controllerId,
        baseVersion: baseVersion,
        baseData: baseData.map((key, item) => MapEntry(key.toString(), item)),
        editedData: editedData.map(
          (key, item) => MapEntry(key.toString(), item),
        ),
        createdAt: created.toUtc(),
        updatedAt: updated.toUtc(),
      );
    } on Object {
      return null;
    }
  }

  static String fingerprint(Map<String, Object?> value) {
    final canonical = jsonEncode(_canonicalize(_sanitizeMap(value)));
    return sha256.convert(utf8.encode(canonical)).toString();
  }

  static Object? _canonicalize(Object? value) {
    if (value is Map<String, Object?>) {
      final keys = value.keys.toList()..sort();
      return <String, Object?>{
        for (final key in keys) key: _canonicalize(value[key]),
      };
    }
    if (value is List<Object?>) return value.map(_canonicalize).toList();
    return value;
  }

  static Map<String, Object?> _sanitizeMap(
    Map<String, Object?> source, {
    int depth = 0,
  }) {
    if (depth > 8) return const <String, Object?>{};
    final result = <String, Object?>{};
    for (final entry in source.entries) {
      if (result.length >= 128) break;
      final key = entry.key.trim();
      if (!RegExp(r'^[A-Za-z0-9_.-]{1,64}$').hasMatch(key) ||
          _isSensitiveKey(key)) {
        continue;
      }
      final value = _sanitizeValue(entry.value, depth: depth + 1);
      if (value != _absent) result[key] = value;
    }
    final encoded = utf8.encode(jsonEncode(result));
    if (encoded.length > 64 * 1024) {
      throw ArgumentError('Szkic konfiguracji jest zbyt duży.');
    }
    return Map<String, Object?>.unmodifiable(result);
  }

  static const Object _absent = Object();

  static Object? _sanitizeValue(Object? value, {required int depth}) {
    if (depth > 8) return _absent;
    if (value == null || value is bool || value is String) {
      if (value is String && value.length > 512) {
        return value.substring(0, 512);
      }
      return value;
    }
    if (value is num) return value.isFinite ? value : _absent;
    if (value is Map) {
      return _sanitizeMap(
        value.map((key, item) => MapEntry(key.toString(), item)),
        depth: depth + 1,
      );
    }
    if (value is List) {
      return value
          .take(64)
          .map((item) => _sanitizeValue(item, depth: depth + 1))
          .where((item) => item != _absent)
          .toList(growable: false);
    }
    return _absent;
  }

  static bool _isSensitiveKey(String key) {
    final normalized = key.toLowerCase();
    return normalized.contains('password') ||
        normalized.contains('token') ||
        normalized.contains('secret') ||
        normalized == 'pin' ||
        normalized == 'adminpin' ||
        normalized == 'userpin' ||
        normalized == 'otapin' ||
        normalized.endsWith('_pin') ||
        normalized.contains('authorization');
  }

  static String _validateControllerId(String value) {
    final normalized = value.trim();
    if (normalized.isEmpty || normalized.length > 128) {
      throw ArgumentError.value(value, 'controllerId');
    }
    return normalized;
  }

  static String _validateFingerprint(String value) {
    final normalized = value.toLowerCase();
    if (!RegExp(r'^[0-9a-f]{64}$').hasMatch(normalized)) {
      throw ArgumentError.value(value, 'baseVersion');
    }
    return normalized;
  }

  static void _collectDiff(
    String prefix,
    Object? before,
    Object? after,
    List<OfflineDraftDiffEntry> result,
  ) {
    if (result.length >= 100) return;
    if (before is Map && after is Map) {
      final keys = <String>{
        ...before.keys.map((key) => key.toString()),
        ...after.keys.map((key) => key.toString()),
      }.toList()..sort();
      for (final key in keys) {
        _collectDiff(
          prefix.isEmpty ? key : '$prefix.$key',
          before[key],
          after[key],
          result,
        );
      }
      return;
    }
    if (_deepEquals(before, after)) return;
    result.add(
      OfflineDraftDiffEntry(
        path: prefix.isEmpty ? 'wartość' : prefix,
        before: before,
        after: after,
      ),
    );
  }

  static bool _deepEquals(Object? left, Object? right) {
    return jsonEncode(_canonicalize(left)) == jsonEncode(_canonicalize(right));
  }
}

abstract interface class OfflineDraftStorage {
  Future<String?> read(String key);
  Future<void> write(String key, String value);
  Future<void> delete(String key);
}

final class SecureOfflineDraftStorage implements OfflineDraftStorage {
  SecureOfflineDraftStorage({
    FlutterSecureStorage storage = const FlutterSecureStorage(),
  }) : _storage = storage;

  final FlutterSecureStorage _storage;

  @override
  Future<String?> read(String key) => _storage.read(key: key);

  @override
  Future<void> write(String key, String value) {
    return _storage.write(key: key, value: value);
  }

  @override
  Future<void> delete(String key) => _storage.delete(key: key);
}

final class OfflineDraftRepository {
  OfflineDraftRepository({OfflineDraftStorage? storage})
    : _storage = storage ?? SecureOfflineDraftStorage();

  static const String _prefix = 'offline_configuration_draft.v1';
  final OfflineDraftStorage _storage;

  Future<void> save(OfflineConfigurationDraft draft) {
    final encoded = jsonEncode(draft.toJson());
    if (utf8.encode(encoded).length > 128 * 1024) {
      throw ArgumentError('Szkic konfiguracji jest zbyt duży.');
    }
    return _storage.write(_key(draft.kind, draft.controllerId), encoded);
  }

  Future<OfflineConfigurationDraft?> load({
    required OfflineDraftKind kind,
    required String controllerId,
  }) async {
    final key = _key(kind, controllerId);
    final encoded = await _storage.read(key);
    if (encoded == null) return null;
    try {
      final draft = OfflineConfigurationDraft.tryFromJson(jsonDecode(encoded));
      if (draft != null &&
          draft.kind == kind &&
          draft.controllerId == controllerId) {
        return draft;
      }
    } on Object {
      // Uszkodzony lub stary wpis jest usuwany poniżej.
    }
    await _storage.delete(key);
    return null;
  }

  Future<void> delete({
    required OfflineDraftKind kind,
    required String controllerId,
  }) {
    return _storage.delete(_key(kind, controllerId));
  }

  static String _key(OfflineDraftKind kind, String controllerId) {
    final idHash = sha256.convert(utf8.encode(controllerId.trim())).toString();
    return '$_prefix.${kind.name}.$idHash';
  }
}
