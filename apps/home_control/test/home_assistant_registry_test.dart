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

  test('statistics parser selects useful values and normalizes ordering', () {
    final samples = HaStatisticSample.fromResponse(<String, Object?>{
      'sensor.temperature': <Object?>[
        <String, Object?>{'start': 1722506400000, 'mean': null, 'state': 24.2},
        <String, Object?>{'start': 1722502800000, 'mean': 23.8},
        <String, Object?>{'start': 1722510000000, 'sum': 72.5},
        <String, Object?>{'start': 1722513600000, 'mean': double.nan},
        <String, Object?>{'start': 'invalid', 'mean': 99},
        'malformed',
      ],
    }, 'sensor.temperature');

    expect(samples, hasLength(3));
    expect(samples.map((sample) => sample.value), <double>[23.8, 24.2, 72.5]);
    expect(samples.first.time.isBefore(samples.last.time), isTrue);
  });

  test('statistics parser tolerates missing columns and unknown series', () {
    final samples = HaStatisticSample.fromResponse(<String, Object?>{
      'sensor.energy': <Object?>[
        <String, Object?>{
          'start': '2026-08-12T10:00:00Z',
          'mean': null,
          'state': null,
          'sum': null,
          'max': 14,
        },
        <String, Object?>{'start': 1722513600000},
      ],
    }, 'sensor.energy');

    expect(samples, hasLength(1));
    expect(samples.single.value, 14);
    expect(
      HaStatisticSample.fromResponse(<String, Object?>{}, 'sensor.unknown'),
      isEmpty,
    );
  });
}
