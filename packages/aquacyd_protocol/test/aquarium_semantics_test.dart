import 'package:aquacyd_protocol/aquacyd_protocol.dart';
import 'package:home_entities/home_entities.dart';
import 'package:test/test.dart';

void main() {
  HomeEntity entity(String id) => HomeEntity(
    id: SourceScopedId(sourceId: 'demo', localId: id),
    deviceId: null,
    areaId: null,
    name: id,
    type: HomeEntityType.sensor,
    state: 24.5,
    attributes: const <String, Object?>{},
    unit: '',
    availability: EntityAvailability.available,
    writable: false,
    risk: HomeCommandRisk.routine,
    changedAt: null,
    updatedAt: null,
  );

  test('classifier recognizes aquarium measurements and outputs', () {
    expect(
      AquariumSemantics.classify(entity('sensor.aquacyd_temperature')),
      AquariumSemanticRole.temperature,
    );
    expect(
      AquariumSemantics.classify(entity('switch.aquacyd_filter')),
      AquariumSemanticRole.filter,
    );
  });
}
