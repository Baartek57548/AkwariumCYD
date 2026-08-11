enum HubEntityKind {
  sensor,
  binarySensor,
  switchEntity,
  number,
  select,
  button,
  light;

  static HubEntityKind parse(Object? value) => switch (value) {
    'sensor' => sensor,
    'binary_sensor' => binarySensor,
    'switch' => switchEntity,
    'number' => number,
    'select' => select,
    'button' => button,
    'light' => light,
    _ => throw const FormatException('Nieznany typ encji AquaHub.'),
  };

  bool get isBoolean =>
      this == binarySensor || this == switchEntity || this == light;
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

double? _optionalNumber(Object? value) {
  if (value == null) return null;
  if (value is! num || !value.isFinite) {
    throw const FormatException('Nieprawidłowy zakres encji.');
  }
  return value.toDouble();
}
