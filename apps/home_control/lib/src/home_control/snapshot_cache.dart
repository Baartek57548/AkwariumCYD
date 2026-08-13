import 'dart:convert';

import 'package:flutter_secure_storage/flutter_secure_storage.dart';
import 'package:home_entities/home_entities.dart';

abstract interface class HomeSnapshotCache {
  Future<HomeSnapshot?> load(HomeSourceKind kind, String sourceId);

  Future<void> save(HomeSnapshot snapshot);

  Future<void> clear(HomeSourceKind kind, {String? sourceId});
}

final class SecureHomeSnapshotCache implements HomeSnapshotCache {
  SecureHomeSnapshotCache({FlutterSecureStorage? storage})
    : _storage = storage ?? const FlutterSecureStorage();

  static const int _schemaVersion = 1;
  static const int _maximumEntities = 512;
  static const int _maximumEncodedBytes = 512 * 1024;
  static const String _legacyKeyPrefix = 'home_control_snapshot_v1_';
  static const String _scopedKeyPrefix = 'home_control_snapshot_v2_';

  final FlutterSecureStorage _storage;

  @override
  Future<HomeSnapshot?> load(HomeSourceKind kind, String sourceId) async {
    final scopedKey = _key(kind, sourceId);
    final legacyKey = _legacyKey(kind);
    var storageKey = scopedKey;
    var encoded = await _storage.read(key: scopedKey);
    if (encoded == null || encoded.isEmpty) {
      storageKey = legacyKey;
      encoded = await _storage.read(key: legacyKey);
    }
    if (encoded == null || encoded.isEmpty) return null;
    try {
      final bytes = utf8.encode(encoded);
      if (bytes.length > _maximumEncodedBytes) {
        await _storage.delete(key: storageKey);
        return null;
      }
      final decoded = jsonDecode(encoded);
      if (decoded is! Map<String, Object?> ||
          decoded['cache_schema'] != _schemaVersion) {
        await _storage.delete(key: storageKey);
        return null;
      }
      final snapshot = _decodeSnapshot(decoded);
      if (snapshot.sourceKind != kind) throw const FormatException();
      if (snapshot.sourceId != sourceId) return null;
      if (storageKey == legacyKey) {
        await _storage.write(key: scopedKey, value: encoded);
        await _storage.delete(key: legacyKey);
      }
      return snapshot;
    } on Object {
      await _storage.delete(key: storageKey);
      return null;
    }
  }

  @override
  Future<void> save(HomeSnapshot snapshot) async {
    final maximumEntityCount = snapshot.entities.length > _maximumEntities
        ? _maximumEntities
        : snapshot.entities.length;
    var encoded = jsonEncode(
      _encodeSnapshot(snapshot, entityLimit: maximumEntityCount),
    );
    if (utf8.encode(encoded).length > _maximumEncodedBytes) {
      String? bounded;
      var lower = 0;
      var upper = maximumEntityCount;
      while (lower <= upper) {
        final candidateCount = lower + ((upper - lower) ~/ 2);
        final candidate = jsonEncode(
          _encodeSnapshot(
            snapshot,
            entityLimit: candidateCount,
            includeAttributes: false,
          ),
        );
        if (utf8.encode(candidate).length <= _maximumEncodedBytes) {
          bounded = candidate;
          lower = candidateCount + 1;
        } else {
          upper = candidateCount - 1;
        }
      }
      if (bounded != null) encoded = bounded;
    }
    if (utf8.encode(encoded).length > _maximumEncodedBytes) {
      throw const FormatException('Home Control snapshot cache is too large.');
    }
    await _storage.write(
      key: _key(snapshot.sourceKind, snapshot.sourceId),
      value: encoded,
    );
  }

  @override
  Future<void> clear(HomeSourceKind kind, {String? sourceId}) async {
    if (sourceId != null) {
      await _storage.delete(key: _key(kind, sourceId));
      final legacyKey = _legacyKey(kind);
      final legacy = await _storage.read(key: legacyKey);
      if (legacy != null && _encodedSourceId(legacy) == sourceId) {
        await _storage.delete(key: legacyKey);
      }
      return;
    }
    final prefix = '$_scopedKeyPrefix${kind.name}_';
    final values = await _storage.readAll();
    await Future.wait<void>(<Future<void>>[
      _storage.delete(key: _legacyKey(kind)),
      for (final key in values.keys)
        if (key.startsWith(prefix)) _storage.delete(key: key),
    ]);
  }

  String _key(HomeSourceKind kind, String sourceId) {
    final encodedId = base64Url
        .encode(utf8.encode(sourceId))
        .replaceAll('=', '');
    return '$_scopedKeyPrefix${kind.name}_$encodedId';
  }

  String _legacyKey(HomeSourceKind kind) => '$_legacyKeyPrefix${kind.name}';

  static String? _encodedSourceId(String encoded) {
    try {
      final decoded = jsonDecode(encoded);
      return decoded is Map<String, Object?>
          ? decoded['source_id'] as String?
          : null;
    } on Object {
      return null;
    }
  }

  Map<String, Object?> _encodeSnapshot(
    HomeSnapshot value, {
    required int entityLimit,
    bool includeAttributes = true,
  }) => <String, Object?>{
    'cache_schema': _schemaVersion,
    'snapshot_schema': value.schemaVersion,
    'source_id': value.sourceId,
    'source_name': value.sourceName,
    'source_kind': value.sourceKind.name,
    'synchronized_at': value.synchronizedAt.toUtc().toIso8601String(),
    'partial':
        value.isPartial ||
        entityLimit < value.entities.length ||
        !includeAttributes,
    'areas': <Object?>[
      for (final area in value.areas)
        <String, Object?>{
          'id': area.id.value,
          'name': area.name,
          'icon': area.icon,
        },
    ],
    'devices': <Object?>[
      for (final device in value.devices)
        <String, Object?>{
          'id': device.id.value,
          'name': device.name,
          'area_id': device.areaId?.value,
          'manufacturer': device.manufacturer,
          'model': device.model,
          'software_version': device.softwareVersion,
          'available': device.available,
          'last_seen': device.lastSeen?.toUtc().toIso8601String(),
          'aquarium': device.isAquariumController,
        },
    ],
    'entities': <Object?>[
      for (final entity in value.entities.take(entityLimit))
        <String, Object?>{
          'id': entity.id.value,
          'device_id': entity.deviceId?.value,
          'area_id': entity.areaId?.value,
          'name': entity.name,
          'type': entity.type.name,
          'state': _safeJson(entity.state),
          'attributes': includeAttributes
              ? _safeJson(entity.attributes)
              : const <String, Object?>{},
          'unit': entity.unit,
          'availability': entity.availability.name,
          'writable': entity.writable,
          'risk': entity.risk.name,
          'changed_at': entity.changedAt?.toUtc().toIso8601String(),
          'updated_at': entity.updatedAt?.toUtc().toIso8601String(),
          'constraints': <String, Object?>{
            'minimum': entity.constraints.minimum,
            'maximum': entity.constraints.maximum,
            'step': entity.constraints.step,
            'options': entity.constraints.options.take(100).toList(),
            'features': entity.constraints.supportedFeatures.take(100).toList(),
          },
        },
    ],
    'automations': <Object?>[
      for (final automation in value.automations)
        <String, Object?>{
          'id': automation.id.value,
          'name': automation.name,
          'enabled': automation.enabled,
          'description': automation.description,
          'last_triggered': automation.lastTriggered?.toUtc().toIso8601String(),
        },
    ],
    'updates': <Object?>[
      for (final update in value.updates)
        <String, Object?>{
          'id': update.id.value,
          'name': update.name,
          'current': update.currentVersion,
          'latest': update.latestVersion,
          'phase': update.phase.name,
          'progress': update.progress,
          'mandatory': update.mandatory,
          'notes': update.releaseNotes,
          'can_install': update.canInstall,
          'error': update.error,
        },
    ],
  };

  HomeSnapshot _decodeSnapshot(Map<String, Object?> value) {
    final sourceId = _string(value, 'source_id');
    final kind = _enumByName(
      HomeSourceKind.values,
      _string(value, 'source_kind'),
    );
    final areas = <HomeArea>[];
    for (final raw in _list(value['areas'])) {
      try {
        final item = _map(raw);
        areas.add(
          HomeArea(
            id: SourceScopedId.parse(_string(item, 'id')),
            name: _string(item, 'name'),
            icon: item['icon'] as String?,
          ),
        );
      } on Object {
        continue;
      }
    }
    final devices = <HomeDevice>[];
    for (final raw in _list(value['devices'])) {
      try {
        final item = _map(raw);
        devices.add(
          HomeDevice(
            id: SourceScopedId.parse(_string(item, 'id')),
            name: _string(item, 'name'),
            areaId: _optionalId(item['area_id']),
            manufacturer: _string(item, 'manufacturer'),
            model: _string(item, 'model'),
            softwareVersion: _string(item, 'software_version'),
            available: item['available'] == true,
            lastSeen: _optionalDate(item['last_seen']),
            isAquariumController: item['aquarium'] == true,
          ),
        );
      } on Object {
        continue;
      }
    }
    final entities = <HomeEntity>[];
    for (final raw in _list(value['entities']).take(_maximumEntities)) {
      try {
        final item = _map(raw);
        final constraints = _map(item['constraints']);
        final attributes = item['attributes'];
        entities.add(
          HomeEntity(
            id: SourceScopedId.parse(_string(item, 'id')),
            deviceId: _optionalId(item['device_id']),
            areaId: _optionalId(item['area_id']),
            name: _string(item, 'name'),
            type: _enumByName(HomeEntityType.values, _string(item, 'type')),
            state: item['state'],
            attributes: attributes is Map<String, Object?>
                ? attributes
                : const <String, Object?>{},
            unit: _string(item, 'unit'),
            availability: _enumByName(
              EntityAvailability.values,
              _string(item, 'availability'),
            ),
            writable: item['writable'] == true,
            risk: _enumByName(HomeCommandRisk.values, _string(item, 'risk')),
            changedAt: _optionalDate(item['changed_at']),
            updatedAt: _optionalDate(item['updated_at']),
            constraints: EntityConstraints(
              minimum: _optionalDouble(constraints['minimum']),
              maximum: _optionalDouble(constraints['maximum']),
              step: _optionalDouble(constraints['step']),
              options: _stringList(constraints['options']),
              supportedFeatures: _stringList(constraints['features']).toSet(),
            ),
          ),
        );
      } on Object {
        continue;
      }
    }
    final automations = <HomeAutomation>[];
    for (final raw in _list(value['automations'])) {
      try {
        final item = _map(raw);
        automations.add(
          HomeAutomation(
            id: SourceScopedId.parse(_string(item, 'id')),
            name: _string(item, 'name'),
            enabled: item['enabled'] == true,
            description: _string(item, 'description'),
            lastTriggered: _optionalDate(item['last_triggered']),
          ),
        );
      } on Object {
        continue;
      }
    }
    final updates = <HomeUpdate>[];
    for (final raw in _list(value['updates'])) {
      try {
        final item = _map(raw);
        updates.add(
          HomeUpdate(
            id: SourceScopedId.parse(_string(item, 'id')),
            name: _string(item, 'name'),
            currentVersion: _string(item, 'current'),
            latestVersion: _string(item, 'latest'),
            phase: _enumByName(HomeUpdatePhase.values, _string(item, 'phase')),
            progress: _optionalDouble(item['progress']) ?? 0,
            mandatory: item['mandatory'] == true,
            releaseNotes: _string(item, 'notes'),
            canInstall: item['can_install'] == true,
            error: item['error'] as String?,
          ),
        );
      } on Object {
        continue;
      }
    }
    return HomeSnapshot(
      schemaVersion: value['snapshot_schema'] is int
          ? value['snapshot_schema']! as int
          : HomeSnapshot.currentSchemaVersion,
      sourceId: sourceId,
      sourceName: _string(value, 'source_name'),
      sourceKind: kind,
      areas: areas,
      devices: devices,
      entities: entities,
      automations: automations,
      updates: updates,
      synchronizedAt: DateTime.parse(_string(value, 'synchronized_at')),
      isPartial:
          value['partial'] == true || entities.length >= _maximumEntities,
      isOffline: true,
    );
  }

  static Object? _safeJson(Object? value, [int depth = 0]) {
    if (depth >= 5) return value?.toString();
    if (value == null || value is bool || value is String) return value;
    if (value is num) return value.isFinite ? value : value.toString();
    if (value is List<Object?>) {
      return value.take(100).map((item) => _safeJson(item, depth + 1)).toList();
    }
    if (value is Map<Object?, Object?>) {
      return <String, Object?>{
        for (final entry in value.entries.take(100))
          entry.key.toString(): _safeJson(entry.value, depth + 1),
      };
    }
    return value.toString();
  }

  static Map<String, Object?> _map(Object? value) {
    if (value is! Map<String, Object?>) throw const FormatException();
    return value;
  }

  static List<Object?> _list(Object? value) =>
      value is List<Object?> ? value : const <Object?>[];

  static String _string(Map<String, Object?> value, String key) {
    final result = value[key];
    if (result is! String) throw const FormatException();
    return result;
  }

  static T _enumByName<T extends Enum>(List<T> values, String name) {
    for (final value in values) {
      if (value.name == name) return value;
    }
    throw const FormatException();
  }

  static SourceScopedId? _optionalId(Object? value) =>
      value is String ? SourceScopedId.parse(value) : null;

  static DateTime? _optionalDate(Object? value) =>
      value is String ? DateTime.tryParse(value)?.toLocal() : null;

  static double? _optionalDouble(Object? value) =>
      value is num ? value.toDouble() : null;

  static List<String> _stringList(Object? value) => value is List<Object?>
      ? value.whereType<String>().toList(growable: false)
      : const <String>[];
}
