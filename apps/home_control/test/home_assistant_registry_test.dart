import 'package:aquacyd_home/src/data/home_assistant_socket.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('registry metadata tolerates malformed rows and disabled entities', () {
    final metadata = HaRegistryMetadata.fromResponses(
      areas: <Object?>[
        <String, Object?>{
          'area_id': 'living_room',
          'name': 'Salon',
          'icon': 'mdi:sofa',
        },
        <String, Object?>{'area_id': '', 'name': 'Broken'},
      ],
      devices: <Object?>[
        <String, Object?>{
          'id': 'device-1',
          'name_by_user': 'Lampa przy sofie',
          'area_id': 'living_room',
          'manufacturer': 'Acme',
          'model': 'Light 2',
          'sw_version': '1.4.0',
        },
        'invalid',
      ],
      entities: <Object?>[
        <String, Object?>{
          'entity_id': 'light.sofa',
          'device_id': 'device-1',
          'original_name': 'Sofa light',
        },
        <String, Object?>{
          'entity_id': 'sensor.disabled',
          'disabled_by': 'user',
        },
      ],
      services: <String, Object?>{
        'light': <String, Object?>{},
        'climate': <String, Object?>{},
      },
    );

    expect(metadata.areas['living_room']?.name, 'Salon');
    expect(metadata.devices['device-1']?.areaId, 'living_room');
    expect(metadata.entities['light.sofa']?.deviceId, 'device-1');
    expect(metadata.entities, isNot(contains('sensor.disabled')));
    expect(metadata.serviceDomains, <String>{'light', 'climate'});
  });

  test('registry metadata degrades to empty immutable collections', () {
    final metadata = HaRegistryMetadata.fromResponses(
      areas: null,
      devices: <Object?>[null],
      entities: <String, Object?>{},
      services: 'invalid',
    );

    expect(metadata.areas, isEmpty);
    expect(metadata.devices, isEmpty);
    expect(metadata.entities, isEmpty);
    expect(metadata.serviceDomains, isEmpty);
    expect(() => metadata.serviceDomains.add('light'), throwsUnsupportedError);
  });
}
