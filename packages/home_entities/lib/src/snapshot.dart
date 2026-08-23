import 'entities.dart';

final class HomeSnapshot {
  HomeSnapshot({
    required this.schemaVersion,
    required this.sourceId,
    required this.sourceName,
    required this.sourceKind,
    required Iterable<HomeArea> areas,
    required Iterable<HomeDevice> devices,
    required Iterable<HomeEntity> entities,
    required Iterable<HomeAutomation> automations,
    required Iterable<HomeUpdate> updates,
    required this.synchronizedAt,
    required this.isPartial,
    required this.isOffline,
  }) : areas = List<HomeArea>.unmodifiable(areas),
       devices = List<HomeDevice>.unmodifiable(devices),
       entities = List<HomeEntity>.unmodifiable(entities),
       automations = List<HomeAutomation>.unmodifiable(automations),
       updates = List<HomeUpdate>.unmodifiable(updates) {
    if (schemaVersion < 1 || sourceId.isEmpty || sourceName.isEmpty) {
      throw const FormatException('Invalid Home Control snapshot.');
    }
    final ids = <String>{};
    for (final entity in this.entities) {
      if (entity.id.sourceId != sourceId || !ids.add(entity.id.value)) {
        throw const FormatException(
          'Snapshot contains invalid entity identifiers.',
        );
      }
    }
  }

  static const int currentSchemaVersion = 1;

  final int schemaVersion;
  final String sourceId;
  final String sourceName;
  final HomeSourceKind sourceKind;
  final List<HomeArea> areas;
  final List<HomeDevice> devices;
  final List<HomeEntity> entities;
  final List<HomeAutomation> automations;
  final List<HomeUpdate> updates;
  final DateTime synchronizedAt;
  final bool isPartial;
  final bool isOffline;

  bool get isStale =>
      DateTime.now().difference(synchronizedAt) > const Duration(seconds: 45);

  HomeEntity? entity(SourceScopedId id) {
    for (final entity in entities) {
      if (entity.id == id) return entity;
    }
    return null;
  }

  List<HomeEntity> entitiesForDevice(SourceScopedId id) =>
      entities.where((entity) => entity.deviceId == id).toList(growable: false);

  List<HomeEntity> entitiesForArea(SourceScopedId id) =>
      entities.where((entity) => entity.areaId == id).toList(growable: false);

  HomeSnapshot replaceEntity(HomeEntity updated) => HomeSnapshot(
    schemaVersion: schemaVersion,
    sourceId: sourceId,
    sourceName: sourceName,
    sourceKind: sourceKind,
    areas: areas,
    devices: devices,
    entities: <HomeEntity>[
      for (final entity in entities)
        if (entity.id == updated.id) updated else entity,
    ],
    automations: automations,
    updates: updates,
    synchronizedAt: synchronizedAt,
    isPartial: isPartial,
    isOffline: isOffline,
  );
}
