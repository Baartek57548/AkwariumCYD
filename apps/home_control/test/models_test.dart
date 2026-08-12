import 'package:aquacyd_home/src/domain/entity_ids.dart';
import 'package:aquacyd_home/src/domain/models.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  group('HomeAssistantCredentials', () {
    const token = '12345678901234567890123456789012';

    test('normalizuje końcowy ukośnik HTTPS', () {
      final credentials = HomeAssistantCredentials.parse(
        baseUrl: ' https://ha.example.net/ ',
        accessToken: ' $token ',
      );

      expect(credentials.baseUri.toString(), 'https://ha.example.net');
      expect(credentials.accessToken, token);
    });

    test('akceptuje lokalne HTTP', () {
      for (final url in <String>[
        'http://homeassistant.local:8123',
        'http://10.0.0.20:8123',
        'http://172.20.1.2:8123',
        'http://192.168.1.10:8123',
        'http://127.0.0.1:8123',
      ]) {
        expect(
          HomeAssistantCredentials.parse(
            baseUrl: url,
            accessToken: token,
          ).baseUri.scheme,
          'http',
        );
      }
    });

    test('odrzuca publiczne HTTP', () {
      expect(
        () => HomeAssistantCredentials.parse(
          baseUrl: 'http://ha.example.net',
          accessToken: token,
        ),
        throwsFormatException,
      );
    });

    test('odrzuca dane użytkownika, query i fragment', () {
      for (final url in <String>[
        'https://user@ha.example.net',
        'https://ha.example.net?token=x',
        'https://ha.example.net/#panel',
      ]) {
        expect(
          () =>
              HomeAssistantCredentials.parse(baseUrl: url, accessToken: token),
          throwsFormatException,
        );
      }
    });

    test('odrzuca krótki token', () {
      expect(
        () => HomeAssistantCredentials.parse(
          baseUrl: 'https://ha.example.net',
          accessToken: 'too-short',
        ),
        throwsFormatException,
      );
    });
  });

  group('modele encji', () {
    test('parsuje stan liczbowy i atrybuty', () {
      final state = HaEntityState.fromJson(<String, Object?>{
        'entity_id': AquaEntityIds.temperature,
        'state': '24.35',
        'attributes': <String, Object?>{
          'friendly_name': 'Temperatura',
          'unit_of_measurement': '°C',
        },
        'last_changed': '2026-07-30T10:00:00Z',
        'last_updated': '2026-07-30T10:01:00Z',
      });

      expect(state.number, 24.35);
      expect(state.friendlyName, 'Temperatura');
      expect(state.unit, '°C');
      expect(state.available, isTrue);
    });

    test('odrzuca encję bez identyfikatora', () {
      expect(
        () => HaEntityState.fromJson(<String, Object?>{'state': 'on'}),
        throwsFormatException,
      );
    });

    test('buduje harmonogram z wartości telemetrii', () {
      final entities = <String, HaEntityState>{
        for (final entry in <String, String>{
          'mode': '1',
          'profile': '2',
          'start': '480',
          'end': '1260',
        }.entries)
          AquaEntityIds.schedule('light_primary', entry.key): _state(
            AquaEntityIds.schedule('light_primary', entry.key),
            entry.value,
          ),
      };

      final schedule = AquaSchedule.fromEntities('light_primary', entities);

      expect(schedule.mode, 1);
      expect(schedule.profile, 2);
      expect(schedule.startText, '08:00');
      expect(schedule.endText, '21:00');
    });

    test('snapshot dekoduje alarmy krytyczne', () {
      final snapshot = AquariumSnapshot.fromEntities(<String, HaEntityState>{
        AquaEntityIds.alarms: _state(AquaEntityIds.alarms, '16'),
        AquaEntityIds.leak: _state(AquaEntityIds.leak, 'on'),
        AquaEntityIds.waterLow: _state(AquaEntityIds.waterLow, 'off'),
      });

      expect(snapshot.alarmFlags, 16);
      expect(snapshot.leak, isTrue);
      expect(snapshot.waterLow, isFalse);
      expect(snapshot.hasCriticalAlarm, isTrue);
    });

    test('normalizacja historii ogranicza liczbę i zachowuje kolejność', () {
      final now = DateTime(2026, 7, 30);
      final input = List<HistorySample>.generate(
        500,
        (index) => HistorySample(
          time: now.add(Duration(minutes: 499 - index)),
          value: index.toDouble(),
        ),
      );

      final normalized = AquariumSnapshot.normalizeHistory(input);

      expect(normalized, hasLength(180));
      for (var index = 1; index < normalized.length; index++) {
        expect(
          normalized[index].time.isAfter(normalized[index - 1].time),
          isTrue,
        );
      }
    });
  });
}

HaEntityState _state(String entityId, String value) {
  return HaEntityState(
    entityId: entityId,
    state: value,
    attributes: const <String, Object?>{},
    lastChanged: DateTime.now(),
    lastUpdated: DateTime.now(),
  );
}
