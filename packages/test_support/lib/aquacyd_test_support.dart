library;

import 'package:home_entities/home_entities.dart';

final class MutableTestClock {
  MutableTestClock(this.value);

  DateTime value;

  DateTime call() => value;

  void advance(Duration duration) {
    if (duration.isNegative) throw ArgumentError.value(duration, 'duration');
    value = value.add(duration);
  }
}

HomeEntity testEntity({
  String sourceId = 'test',
  String localId = 'sensor.temperature',
  HomeEntityType type = HomeEntityType.sensor,
  Object? state = 24.5,
  bool writable = false,
}) => HomeEntity(
  id: SourceScopedId(sourceId: sourceId, localId: localId),
  deviceId: SourceScopedId(sourceId: sourceId, localId: 'device.test'),
  areaId: SourceScopedId(sourceId: sourceId, localId: 'area.test'),
  name: 'Test entity',
  type: type,
  state: state,
  attributes: const <String, Object?>{},
  unit: type == HomeEntityType.sensor ? '°C' : '',
  availability: EntityAvailability.available,
  writable: writable,
  risk: HomeCommandRisk.routine,
  changedAt: DateTime.utc(2026, 1, 1),
  updatedAt: DateTime.utc(2026, 1, 1),
);
