enum HubEntityKind {
  sensor,
  binarySensor,
  switchEntity,
  number,
  select,
  button,
  light,
  unknown;

  static HubEntityKind parse(Object? value) => switch (value) {
    'sensor' => sensor,
    'binary_sensor' => binarySensor,
    'switch' => switchEntity,
    'number' => number,
    'select' => select,
    'button' => button,
    'light' => light,
    _ => unknown,
  };

  bool get isBoolean =>
      this == binarySensor || this == switchEntity || this == light;

  bool get supportsHistory => this == sensor || this == number;
}

enum HubUpdatePhase {
  disabled,
  idle,
  checking,
  available,
  upToDate,
  downloading,
  verifying,
  rebooting,
  failed;

  static HubUpdatePhase parse(Object? value) => switch (value) {
    'disabled' => disabled,
    'idle' => idle,
    'checking' => checking,
    'available' => available,
    'up_to_date' => upToDate,
    'downloading' => downloading,
    'verifying' => verifying,
    'rebooting' => rebooting,
    'failed' => failed,
    _ => throw const FormatException('Nieznany stan aktualizacji AquaHub.'),
  };

  bool get busy =>
      this == checking ||
      this == downloading ||
      this == verifying ||
      this == rebooting;
}

final class HubUpdateRelease {
  const HubUpdateRelease({
    required this.id,
    required this.version,
    required this.sizeBytes,
    required this.securityVersion,
    required this.mandatory,
    required this.notes,
  });

  factory HubUpdateRelease.fromJson(Map<String, Object?> json) =>
      HubUpdateRelease(
        id: _requiredText(json, 'release_id'),
        version: _requiredText(json, 'version'),
        sizeBytes: _integer(json, 'size'),
        securityVersion: _integer(json, 'security_version'),
        mandatory: json['mandatory'] == true,
        notes: _longText(json, 'notes', maximumLength: 1024),
      );

  final String id;
  final String version;
  final int sizeBytes;
  final int securityVersion;
  final bool mandatory;
  final String notes;
}

final class HubUpdateStatus {
  const HubUpdateStatus({
    required this.supported,
    required this.target,
    required this.currentVersion,
    required this.currentSecurityVersion,
    required this.phase,
    required this.progressPercent,
    required this.bytesReceived,
    required this.totalBytes,
    required this.error,
    required this.release,
  });

  factory HubUpdateStatus.fromJson(Map<String, Object?> json) {
    final progress = _integer(json, 'progress_percent');
    if (progress > 100) {
      throw const FormatException('Postęp OTA wykracza poza dozwolony zakres.');
    }
    final rawRelease = json['release'];
    if (rawRelease != null && rawRelease is! Map<String, Object?>) {
      throw const FormatException('Nieprawidłowy opis wydania OTA.');
    }
    return HubUpdateStatus(
      supported: json['supported'] == true,
      target: _requiredText(json, 'target'),
      currentVersion: _requiredText(json, 'current_version'),
      currentSecurityVersion: _integer(json, 'current_security_version'),
      phase: HubUpdatePhase.parse(json['phase']),
      progressPercent: progress,
      bytesReceived: _integer(json, 'bytes_received'),
      totalBytes: _integer(json, 'total_bytes'),
      error: _longText(json, 'error', maximumLength: 512),
      release: rawRelease == null
          ? null
          : HubUpdateRelease.fromJson(rawRelease as Map<String, Object?>),
    );
  }

  final bool supported;
  final String target;
  final String currentVersion;
  final int currentSecurityVersion;
  final HubUpdatePhase phase;
  final int progressPercent;
  final int bytesReceived;
  final int totalBytes;
  final String error;
  final HubUpdateRelease? release;
}

enum HubAutomationComparison {
  changed,
  equals,
  above,
  below;

  static HubAutomationComparison parse(Object? value) => switch (value) {
    'changed' => changed,
    'equals' => equals,
    'above' => above,
    'below' => below,
    _ => throw const FormatException('Nieznane porównanie automatyzacji.'),
  };

  String get wireName => switch (this) {
    changed => 'changed',
    equals => 'equals',
    above => 'above',
    below => 'below',
  };
}

final class HubAutomationClause {
  const HubAutomationClause({
    required this.entityId,
    required this.comparison,
    required this.value,
  });

  factory HubAutomationClause.fromJson(Map<String, Object?> json) =>
      HubAutomationClause(
        entityId: _requiredText(json, 'entity_id'),
        comparison: HubAutomationComparison.parse(json['comparison']),
        value: _automationValue(json['value'], allowNull: true),
      );

  final String entityId;
  final HubAutomationComparison comparison;
  final Object? value;

  Map<String, Object?> toJson() => <String, Object?>{
    'entity_id': entityId,
    'comparison': comparison.wireName,
    'value': value,
  };
}

final class HubAutomationAction {
  const HubAutomationAction({required this.entityId, required this.value});

  factory HubAutomationAction.fromJson(Map<String, Object?> json) =>
      HubAutomationAction(
        entityId: _requiredText(json, 'entity_id'),
        value: _automationValue(json['value'], allowNull: false)!,
      );

  final String entityId;
  final Object value;

  Map<String, Object?> toJson() => <String, Object?>{
    'entity_id': entityId,
    'value': value,
  };
}

final class HubAutomationRule {
  const HubAutomationRule({
    required this.id,
    required this.name,
    required this.enabled,
    required this.cooldown,
    required this.trigger,
    required this.condition,
    required this.action,
  });

  factory HubAutomationRule.fromJson(Map<String, Object?> json) {
    final trigger = json['trigger'];
    final condition = json['condition'];
    final action = json['action'];
    if (trigger is! Map<String, Object?> ||
        (condition != null && condition is! Map<String, Object?>) ||
        action is! Map<String, Object?>) {
      throw const FormatException('Nieprawidłowa struktura automatyzacji.');
    }
    return HubAutomationRule(
      id: _requiredText(json, 'id'),
      name: _requiredText(json, 'name'),
      enabled: json['enabled'] == true,
      cooldown: Duration(milliseconds: _integer(json, 'cooldown_ms')),
      trigger: HubAutomationClause.fromJson(trigger),
      condition: condition == null
          ? null
          : HubAutomationClause.fromJson(condition as Map<String, Object?>),
      action: HubAutomationAction.fromJson(action),
    );
  }

  final String id;
  final String name;
  final bool enabled;
  final Duration cooldown;
  final HubAutomationClause trigger;
  final HubAutomationClause? condition;
  final HubAutomationAction action;

  HubAutomationRule copyWith({bool? enabled}) => HubAutomationRule(
    id: id,
    name: name,
    enabled: enabled ?? this.enabled,
    cooldown: cooldown,
    trigger: trigger,
    condition: condition,
    action: action,
  );

  Map<String, Object?> toJson() => <String, Object?>{
    'id': id,
    'name': name,
    'enabled': enabled,
    'cooldown_ms': cooldown.inMilliseconds,
    'trigger': trigger.toJson(),
    'condition': condition?.toJson(),
    'action': action.toJson(),
  };
}

final class HubAutomationCollection {
  const HubAutomationCollection({required this.capacity, required this.rules});

  factory HubAutomationCollection.fromJson(Map<String, Object?> json) {
    final capacity = _integer(json, 'capacity');
    final count = _integer(json, 'count');
    final items = json['items'];
    if (capacity < 1 || capacity > 32 || items is! List<Object?>) {
      throw const FormatException('Nieprawidłowa lista automatyzacji.');
    }
    final rules = items
        .map((item) {
          if (item is! Map<String, Object?>) {
            throw const FormatException('Nieprawidłowa automatyzacja.');
          }
          return HubAutomationRule.fromJson(item);
        })
        .toList(growable: false);
    if (rules.length != count || rules.length > capacity) {
      throw const FormatException('Niespójna liczba automatyzacji.');
    }
    return HubAutomationCollection(capacity: capacity, rules: rules);
  }

  final int capacity;
  final List<HubAutomationRule> rules;
}

final class HubCredentials {
  const HubCredentials({
    required this.baseUri,
    required this.accessToken,
    required this.tlsFingerprint,
  });

  final Uri baseUri;
  final String accessToken;
  final String tlsFingerprint;

  static HubCredentials parse({
    required String baseUrl,
    required String accessToken,
    required String tlsFingerprint,
  }) {
    final uri = Uri.tryParse(baseUrl.trim());
    if (uri == null ||
        uri.scheme != 'https' ||
        uri.host.isEmpty ||
        uri.userInfo.isNotEmpty ||
        uri.query.isNotEmpty ||
        uri.fragment.isNotEmpty) {
      throw const FormatException('AquaHub wymaga pełnego adresu HTTPS.');
    }
    final token = accessToken.trim();
    if (token.length < 32 || token.length > 128) {
      throw const FormatException('Nieprawidłowy token AquaHub.');
    }
    final fingerprint = normalizeFingerprint(tlsFingerprint);
    if (!RegExp(r'^[0-9A-F]{64}$').hasMatch(fingerprint)) {
      throw const FormatException('Nieprawidłowy odcisk certyfikatu.');
    }
    final normalizedPath = uri.path == '/'
        ? ''
        : uri.path.replaceFirst(RegExp(r'/+$'), '');
    return HubCredentials(
      baseUri: uri.replace(path: normalizedPath),
      accessToken: token,
      tlsFingerprint: fingerprint,
    );
  }
}

String normalizeFingerprint(String value) =>
    value.replaceAll(RegExp('[^0-9a-fA-F]'), '').toUpperCase();

final class HubInfo {
  const HubInfo({
    required this.product,
    required this.apiVersion,
    required this.hostname,
    required this.tlsFingerprint,
    required this.pairingAvailable,
  });

  factory HubInfo.fromJson(Map<String, Object?> json) {
    final product = json['product'];
    final apiVersion = json['api_version'];
    final hostname = json['hostname'];
    final fingerprint = normalizeFingerprint(
      json['tls_fingerprint'] as String? ?? '',
    );
    final pairing = json['pairing_available'];
    if (product is! String ||
        product != 'aquahub-p4' ||
        apiVersion is! num ||
        apiVersion.toInt() != 1 ||
        hostname is! String ||
        !RegExp(r'^[0-9A-F]{64}$').hasMatch(fingerprint) ||
        pairing is! bool) {
      throw const FormatException(
        'Nieprawidłowa odpowiedź identyfikacyjna AquaHub.',
      );
    }
    return HubInfo(
      product: product,
      apiVersion: apiVersion.toInt(),
      hostname: hostname,
      tlsFingerprint: fingerprint,
      pairingAvailable: pairing,
    );
  }

  final String product;
  final int apiVersion;
  final String hostname;
  final String tlsFingerprint;
  final bool pairingAvailable;
}

final class HubSystem {
  const HubSystem({
    required this.uptime,
    required this.freeHeapBytes,
    required this.deviceCount,
    required this.onlineDeviceCount,
    required this.entityCount,
    required this.writableEntityCount,
    required this.acceptedMessages,
    required this.rejectedMessages,
    required this.registryRevision,
  });

  factory HubSystem.fromJson(Map<String, Object?> json) => HubSystem(
    uptime: Duration(milliseconds: _integer(json, 'uptime_ms')),
    freeHeapBytes: _integer(json, 'free_heap_bytes'),
    deviceCount: _integer(json, 'devices'),
    onlineDeviceCount: _integer(json, 'online_devices'),
    entityCount: _integer(json, 'entities'),
    writableEntityCount: _integer(json, 'writable_entities'),
    acceptedMessages: _integer(json, 'accepted_messages'),
    rejectedMessages: _integer(json, 'rejected_messages'),
    registryRevision: _integer(json, 'registry_revision'),
  );

  final Duration uptime;
  final int freeHeapBytes;
  final int deviceCount;
  final int onlineDeviceCount;
  final int entityCount;
  final int writableEntityCount;
  final int acceptedMessages;
  final int rejectedMessages;
  final int registryRevision;
}

final class HubDevice {
  const HubDevice({
    required this.id,
    required this.name,
    required this.model,
    required this.manufacturer,
    required this.firmwareVersion,
    required this.area,
    required this.online,
    required this.lastSeen,
  });

  factory HubDevice.fromJson(Map<String, Object?> json) => HubDevice(
    id: _requiredText(json, 'id'),
    name: _requiredText(json, 'name'),
    model: _text(json, 'model'),
    manufacturer: _text(json, 'manufacturer'),
    firmwareVersion: _text(json, 'firmware_version'),
    area: _text(json, 'area'),
    online: json['online'] == true,
    lastSeen: Duration(milliseconds: _integer(json, 'last_seen_ms')),
  );

  final String id;
  final String name;
  final String model;
  final String manufacturer;
  final String firmwareVersion;
  final String area;
  final bool online;
  final Duration lastSeen;
}

final class HubEntity {
  const HubEntity({
    required this.id,
    required this.deviceId,
    required this.name,
    required this.kind,
    required this.unit,
    required this.writable,
    required this.critical,
    required this.state,
    required this.changedAt,
    required this.updatedAt,
    required this.minimum,
    required this.maximum,
    required this.step,
    required this.options,
  });

  factory HubEntity.fromJson(Map<String, Object?> json) {
    final kind = HubEntityKind.parse(json['kind']);
    final state = json['state'];
    if (state != null && state is! bool && state is! num && state is! String) {
      throw const FormatException('Nieprawidłowy stan encji AquaHub.');
    }
    final rawOptions = json['options'];
    final options = rawOptions is List<Object?>
        ? rawOptions
              .map((value) {
                if (value is! String || value.isEmpty) {
                  throw const FormatException(
                    'Nieprawidłowa opcja encji AquaHub.',
                  );
                }
                return value;
              })
              .toList(growable: false)
        : const <String>[];
    return HubEntity(
      id: _requiredText(json, 'id'),
      deviceId: _requiredText(json, 'device_id'),
      name: _requiredText(json, 'name'),
      kind: kind,
      unit: _text(json, 'unit'),
      writable: json['writable'] == true,
      critical: json['critical'] == true,
      state: state,
      changedAt: Duration(milliseconds: _integer(json, 'changed_at_ms')),
      updatedAt: Duration(milliseconds: _integer(json, 'updated_at_ms')),
      minimum: _optionalNumber(json['minimum']),
      maximum: _optionalNumber(json['maximum']),
      step: _optionalNumber(json['step']),
      options: options,
    );
  }

  final String id;
  final String deviceId;
  final String name;
  final HubEntityKind kind;
  final String unit;
  final bool writable;
  final bool critical;
  final Object? state;
  final Duration changedAt;
  final Duration updatedAt;
  final double? minimum;
  final double? maximum;
  final double? step;
  final List<String> options;

  bool? get booleanState => state is bool ? state! as bool : null;
  double? get numericState => state is num ? (state! as num).toDouble() : null;

  String get formattedState {
    if (state == null) return 'brak danych';
    if (state is bool) return state == true ? 'Włączone' : 'Wyłączone';
    if (state is num) {
      final number = (state! as num).toDouble();
      final text = number == number.roundToDouble()
          ? number.toStringAsFixed(0)
          : number.toStringAsFixed(2);
      return unit.isEmpty ? text : '$text $unit';
    }
    return state.toString();
  }
}

final class HubHistoryPoint {
  const HubHistoryPoint({required this.changedAt, required this.value});

  factory HubHistoryPoint.fromJson(Map<String, Object?> json) {
    final state = json['state'];
    if (state != null && state is! bool && state is! num && state is! String) {
      throw const FormatException('Nieprawidłowy punkt historii.');
    }
    return HubHistoryPoint(
      changedAt: Duration(milliseconds: _integer(json, 'changed_at_ms')),
      value: state,
    );
  }

  final Duration changedAt;
  final Object? value;
}

int _integer(Map<String, Object?> json, String key) {
  final value = json[key];
  if (value is! num ||
      !value.isFinite ||
      value < 0 ||
      value != value.roundToDouble()) {
    throw FormatException('Pole $key nie jest poprawną liczbą całkowitą.');
  }
  return value.toInt();
}

String _requiredText(Map<String, Object?> json, String key) {
  final value = json[key];
  if (value is! String || value.isEmpty || value.length > 128) {
    throw FormatException('Pole $key nie jest poprawnym tekstem.');
  }
  return value;
}

String _text(Map<String, Object?> json, String key) {
  final value = json[key];
  if (value == null) return '';
  if (value is! String || value.length > 128) {
    throw FormatException('Pole $key nie jest poprawnym tekstem.');
  }
  return value;
}

String _longText(
  Map<String, Object?> json,
  String key, {
  required int maximumLength,
}) {
  final value = json[key];
  if (value == null) return '';
  if (value is! String || value.length > maximumLength) {
    throw FormatException('Pole $key nie jest poprawnym tekstem.');
  }
  return value;
}

double? _optionalNumber(Object? value) {
  if (value == null) return null;
  if (value is! num || !value.isFinite) {
    throw const FormatException('Nieprawidłowy zakres encji.');
  }
  return value.toDouble();
}

Object? _automationValue(Object? value, {required bool allowNull}) {
  if (value == null && allowNull) return null;
  if (value is bool || value is String) return value;
  if (value is num && value.isFinite) return value;
  throw const FormatException('Nieprawidłowa wartość automatyzacji.');
}
