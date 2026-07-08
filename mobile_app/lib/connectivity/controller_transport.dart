import 'dart:async';

enum ControllerTransportState {
  disconnected,
  scanning,
  connecting,
  connected,
  error,
}

enum OutputChannel {
  light('light', 'Światło 1'),
  plantLight('plant', 'Światło 2'),
  filter('filter', 'Filtr'),
  heater('heater', 'Grzałka'),
  aeration('aeration', 'Napowietrzanie');

  const OutputChannel(this.protocolName, this.label);

  final String protocolName;
  final String label;
}

class ControllerSnapshot {
  const ControllerSnapshot({
    required this.protocolVersion,
    required this.developerMode,
    required this.uptimeSeconds,
    required this.freeHeapBytes,
    required this.temperature,
    required this.temperatureValid,
    required this.targetTemperature,
    required this.ph,
    required this.phValid,
    required this.ec,
    required this.ecValid,
    required this.ldr,
    required this.ldrValid,
    required this.alarmFlags,
    required this.waterLevelHigh,
    required this.leakDetected,
    required this.outputs,
  });

  factory ControllerSnapshot.fromJson(Map<String, dynamic> json) {
    const supportedProtocolVersion = 1;
    final version = _readInt(json, 'v');
    if (version != supportedProtocolVersion) {
      throw FormatException('Nieobsługiwana wersja protokołu BLE: $version.');
    }

    final rawOutputs = json['outputs'];
    if (rawOutputs is! Map<String, dynamic>) {
      throw const FormatException('Brak stanów wyjść w telemetrii BLE.');
    }
    final outputs = <OutputChannel, bool>{
      for (final channel in OutputChannel.values)
        channel: _readBool(rawOutputs, channel.protocolName),
    };

    return ControllerSnapshot(
      protocolVersion: version,
      developerMode: _readBool(json, 'dev'),
      uptimeSeconds: _readInt(json, 'uptime'),
      freeHeapBytes: _readInt(json, 'heap'),
      temperature: _readDouble(json, 'temp'),
      temperatureValid: _readBool(json, 'tempValid'),
      targetTemperature: _readDouble(json, 'targetTemp'),
      ph: _readDouble(json, 'ph'),
      phValid: _readBool(json, 'phValid'),
      ec: _readDouble(json, 'ec'),
      ecValid: _readBool(json, 'ecValid'),
      ldr: _readInt(json, 'ldr'),
      ldrValid: _readBool(json, 'ldrValid'),
      alarmFlags: _readInt(json, 'alarmFlags'),
      waterLevelHigh: _readBool(json, 'water'),
      leakDetected: _readBool(json, 'leak'),
      outputs: Map.unmodifiable(outputs),
    );
  }

  final int protocolVersion;
  final bool developerMode;
  final int uptimeSeconds;
  final int freeHeapBytes;
  final double temperature;
  final bool temperatureValid;
  final double targetTemperature;
  final double ph;
  final bool phValid;
  final double ec;
  final bool ecValid;
  final int ldr;
  final bool ldrValid;
  final int alarmFlags;
  final bool waterLevelHigh;
  final bool leakDetected;
  final Map<OutputChannel, bool> outputs;

  ControllerSnapshot copyWith({
    int? uptimeSeconds,
    int? freeHeapBytes,
    double? temperature,
    double? ph,
    double? ec,
    int? ldr,
    int? alarmFlags,
    bool? waterLevelHigh,
    bool? leakDetected,
    Map<OutputChannel, bool>? outputs,
  }) {
    return ControllerSnapshot(
      protocolVersion: protocolVersion,
      developerMode: developerMode,
      uptimeSeconds: uptimeSeconds ?? this.uptimeSeconds,
      freeHeapBytes: freeHeapBytes ?? this.freeHeapBytes,
      temperature: temperature ?? this.temperature,
      temperatureValid: temperatureValid,
      targetTemperature: targetTemperature,
      ph: ph ?? this.ph,
      phValid: phValid,
      ec: ec ?? this.ec,
      ecValid: ecValid,
      ldr: ldr ?? this.ldr,
      ldrValid: ldrValid,
      alarmFlags: alarmFlags ?? this.alarmFlags,
      waterLevelHigh: waterLevelHigh ?? this.waterLevelHigh,
      leakDetected: leakDetected ?? this.leakDetected,
      outputs: Map.unmodifiable(outputs ?? this.outputs),
    );
  }

  static int _readInt(Map<String, dynamic> json, String key) {
    final value = json[key];
    if (value is int) {
      return value;
    }
    if (value is num && value.isFinite) {
      return value.toInt();
    }
    throw FormatException('Pole "$key" nie jest poprawną liczbą całkowitą.');
  }

  static double _readDouble(Map<String, dynamic> json, String key) {
    final value = json[key];
    if (value is num && value.isFinite) {
      return value.toDouble();
    }
    throw FormatException('Pole "$key" nie jest poprawną liczbą.');
  }

  static bool _readBool(Map<String, dynamic> json, String key) {
    final value = json[key];
    if (value is bool) {
      return value;
    }
    throw FormatException('Pole "$key" nie jest wartością logiczną.');
  }
}

class ControllerCommandResult {
  const ControllerCommandResult({
    required this.success,
    required this.code,
    required this.message,
  });

  final bool success;
  final String code;
  final String message;
}

abstract interface class ControllerTransport {
  String get displayName;
  bool get isDeveloperTransport;
  Stream<ControllerTransportState> get stateChanges;
  Stream<ControllerSnapshot> get snapshots;
  ControllerTransportState get currentState;

  Future<void> connect();
  Future<void> disconnect();
  Future<ControllerCommandResult> setOutput(
    OutputChannel channel,
    bool enabled,
    String pin,
  );
  Future<ControllerCommandResult> feed(String pin);
  Future<void> dispose();
}
