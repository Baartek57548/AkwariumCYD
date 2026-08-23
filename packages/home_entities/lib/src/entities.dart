enum HomeSourceKind { aquaHub, homeAssistant, demo }

enum HomeEntityType {
  light,
  switchEntity,
  sensor,
  binarySensor,
  climate,
  cover,
  lock,
  alarmControlPanel,
  camera,
  mediaPlayer,
  fan,
  vacuum,
  weather,
  person,
  deviceTracker,
  scene,
  script,
  automation,
  button,
  inputButton,
  number,
  inputNumber,
  select,
  inputSelect,
  text,
  inputText,
  update,
  unknown;

  factory HomeEntityType.fromDomain(String value) => switch (value) {
    'light' => HomeEntityType.light,
    'switch' => HomeEntityType.switchEntity,
    'sensor' => HomeEntityType.sensor,
    'binary_sensor' => HomeEntityType.binarySensor,
    'climate' => HomeEntityType.climate,
    'cover' => HomeEntityType.cover,
    'lock' => HomeEntityType.lock,
    'alarm_control_panel' => HomeEntityType.alarmControlPanel,
    'camera' => HomeEntityType.camera,
    'media_player' => HomeEntityType.mediaPlayer,
    'fan' => HomeEntityType.fan,
    'vacuum' => HomeEntityType.vacuum,
    'weather' => HomeEntityType.weather,
    'person' => HomeEntityType.person,
    'device_tracker' => HomeEntityType.deviceTracker,
    'scene' => HomeEntityType.scene,
    'script' => HomeEntityType.script,
    'automation' => HomeEntityType.automation,
    'button' => HomeEntityType.button,
    'input_button' => HomeEntityType.inputButton,
    'number' => HomeEntityType.number,
    'input_number' => HomeEntityType.inputNumber,
    'select' => HomeEntityType.select,
    'input_select' => HomeEntityType.inputSelect,
    'text' => HomeEntityType.text,
    'input_text' => HomeEntityType.inputText,
    'update' => HomeEntityType.update,
    _ => HomeEntityType.unknown,
  };

  String get wireName => switch (this) {
    HomeEntityType.switchEntity => 'switch',
    HomeEntityType.binarySensor => 'binary_sensor',
    HomeEntityType.alarmControlPanel => 'alarm_control_panel',
    HomeEntityType.mediaPlayer => 'media_player',
    HomeEntityType.deviceTracker => 'device_tracker',
    HomeEntityType.inputButton => 'input_button',
    HomeEntityType.inputNumber => 'input_number',
    HomeEntityType.inputSelect => 'input_select',
    HomeEntityType.inputText => 'input_text',
    _ => name,
  };

  bool get supportsToggle => switch (this) {
    HomeEntityType.light ||
    HomeEntityType.switchEntity ||
    HomeEntityType.fan ||
    HomeEntityType.automation => true,
    _ => false,
  };

  bool get supportsPress => switch (this) {
    HomeEntityType.button ||
    HomeEntityType.inputButton ||
    HomeEntityType.scene ||
    HomeEntityType.script => true,
    _ => false,
  };

  bool get supportsHistory => switch (this) {
    HomeEntityType.sensor ||
    HomeEntityType.binarySensor ||
    HomeEntityType.number ||
    HomeEntityType.inputNumber ||
    HomeEntityType.climate => true,
    _ => false,
  };
}

enum EntityAvailability { available, unavailable, unknown, removed }

enum HomeCommandRisk { routine, consequential, critical }

enum HomeUpdatePhase {
  unsupported,
  idle,
  checking,
  available,
  downloading,
  verifying,
  installing,
  rebooting,
  complete,
  failed,
}

final class SourceScopedId implements Comparable<SourceScopedId> {
  SourceScopedId({required this.sourceId, required this.localId}) {
    if (!_partPattern.hasMatch(sourceId) || !_localPattern.hasMatch(localId)) {
      throw const FormatException('Invalid source-scoped identifier.');
    }
  }

  factory SourceScopedId.parse(String value) {
    final separator = value.indexOf(':');
    if (separator <= 0 || separator == value.length - 1) {
      throw const FormatException('Invalid source-scoped identifier.');
    }
    return SourceScopedId(
      sourceId: value.substring(0, separator),
      localId: value.substring(separator + 1),
    );
  }

  static final RegExp _partPattern = RegExp(r'^[a-z0-9][a-z0-9_-]{0,63}$');
  static final RegExp _localPattern = RegExp(r'^[A-Za-z0-9_.:-]{1,191}$');

  final String sourceId;
  final String localId;

  String get value => '$sourceId:$localId';

  @override
  int compareTo(SourceScopedId other) => value.compareTo(other.value);

  @override
  bool operator ==(Object other) =>
      other is SourceScopedId &&
      sourceId == other.sourceId &&
      localId == other.localId;

  @override
  int get hashCode => Object.hash(sourceId, localId);

  @override
  String toString() => value;
}

final class HomeArea {
  const HomeArea({required this.id, required this.name, this.icon});

  final SourceScopedId id;
  final String name;
  final String? icon;
}

final class HomeDevice {
  const HomeDevice({
    required this.id,
    required this.name,
    required this.areaId,
    required this.manufacturer,
    required this.model,
    required this.softwareVersion,
    required this.available,
    required this.lastSeen,
    this.isAquariumController = false,
  });

  final SourceScopedId id;
  final String name;
  final SourceScopedId? areaId;
  final String manufacturer;
  final String model;
  final String softwareVersion;
  final bool available;
  final DateTime? lastSeen;
  final bool isAquariumController;
}

final class EntityConstraints {
  const EntityConstraints({
    this.minimum,
    this.maximum,
    this.step,
    this.options = const <String>[],
    this.supportedFeatures = const <String>{},
  });

  final double? minimum;
  final double? maximum;
  final double? step;
  final List<String> options;
  final Set<String> supportedFeatures;
}

final class HomeEntity {
  const HomeEntity({
    required this.id,
    required this.deviceId,
    required this.areaId,
    required this.name,
    required this.type,
    required this.state,
    required this.attributes,
    required this.unit,
    required this.availability,
    required this.writable,
    required this.risk,
    required this.changedAt,
    required this.updatedAt,
    this.constraints = const EntityConstraints(),
  });

  final SourceScopedId id;
  final SourceScopedId? deviceId;
  final SourceScopedId? areaId;
  final String name;
  final HomeEntityType type;
  final Object? state;
  final Map<String, Object?> attributes;
  final String unit;
  final EntityAvailability availability;
  final bool writable;
  final HomeCommandRisk risk;
  final DateTime? changedAt;
  final DateTime? updatedAt;
  final EntityConstraints constraints;

  bool get available => availability == EntityAvailability.available;

  bool? get booleanValue {
    if (state is bool) return state! as bool;
    final normalized = state?.toString().toLowerCase();
    if (<String>{
      'on',
      'open',
      'unlocked',
      'home',
      'active',
    }.contains(normalized)) {
      return true;
    }
    if (<String>{
      'off',
      'closed',
      'locked',
      'not_home',
      'inactive',
    }.contains(normalized)) {
      return false;
    }
    return null;
  }

  double? get numericValue {
    if (state is num) return (state! as num).toDouble();
    return double.tryParse(state?.toString().replaceAll(',', '.') ?? '');
  }

  HomeEntity copyWith({
    Object? state,
    EntityAvailability? availability,
    DateTime? changedAt,
    DateTime? updatedAt,
    Map<String, Object?>? attributes,
  }) => HomeEntity(
    id: id,
    deviceId: deviceId,
    areaId: areaId,
    name: name,
    type: type,
    state: state ?? this.state,
    attributes: attributes ?? this.attributes,
    unit: unit,
    availability: availability ?? this.availability,
    writable: writable,
    risk: risk,
    changedAt: changedAt ?? this.changedAt,
    updatedAt: updatedAt ?? this.updatedAt,
    constraints: constraints,
  );
}

final class HistoryPoint {
  const HistoryPoint({required this.time, required this.value});

  final DateTime time;
  final Object? value;
}

final class HomeAutomation {
  const HomeAutomation({
    required this.id,
    required this.name,
    required this.enabled,
    required this.description,
    required this.lastTriggered,
  });

  final SourceScopedId id;
  final String name;
  final bool enabled;
  final String description;
  final DateTime? lastTriggered;
}

final class HomeUpdate {
  const HomeUpdate({
    required this.id,
    required this.name,
    required this.currentVersion,
    required this.latestVersion,
    required this.phase,
    required this.progress,
    required this.mandatory,
    required this.releaseNotes,
    required this.canInstall,
    this.error,
  });

  final SourceScopedId id;
  final String name;
  final String currentVersion;
  final String latestVersion;
  final HomeUpdatePhase phase;
  final double progress;
  final bool mandatory;
  final String releaseNotes;
  final bool canInstall;
  final String? error;
}
