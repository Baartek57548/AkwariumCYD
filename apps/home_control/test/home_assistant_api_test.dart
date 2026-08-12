import 'dart:convert';

import 'package:aquacyd_home/src/data/home_assistant_api.dart';
import 'package:aquacyd_home/src/domain/entity_ids.dart';
import 'package:aquacyd_home/src/domain/models.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:http/http.dart' as http;
import 'package:http/testing.dart';

void main() {
  const token = '12345678901234567890123456789012';
  final credentials = HomeAssistantCredentials.parse(
    baseUrl: 'https://ha.example.net/root/',
    accessToken: token,
  );

  test('pobiera i parsuje konfigurację HA', () async {
    final api = HomeAssistantApi(
      credentials,
      client: MockClient((request) async {
        expect(request.url.path, '/root/api/config');
        expect(request.headers['authorization'], 'Bearer $token');
        return http.Response(
          jsonEncode(<String, Object?>{
            'location_name': 'Dom',
            'version': '2026.7.4',
            'time_zone': 'Europe/Warsaw',
          }),
          200,
        );
      }),
    );

    final config = await api.fetchConfig();

    expect(config.locationName, 'Dom');
    expect(config.version, '2026.7.4');
    expect(config.timeZone, 'Europe/Warsaw');
  });

  test('filtruje stany spoza kontraktu AquaCYD', () async {
    final api = HomeAssistantApi(
      credentials,
      client: MockClient(
        (_) async => http.Response(
          jsonEncode(<Object?>[
            _entityJson(AquaEntityIds.temperature, '24.1'),
            _entityJson('sensor.other_temperature', '18.0'),
          ]),
          200,
        ),
      ),
    );

    final states = await api.fetchAquaStates();

    expect(states.keys, <String>[AquaEntityIds.temperature]);
    expect(states[AquaEntityIds.temperature]?.number, 24.1);
  });

  test('wywołuje skrypt przez service API', () async {
    final api = HomeAssistantApi(
      credentials,
      client: MockClient((request) async {
        expect(request.method, 'POST');
        expect(
          request.url.path,
          '/root/api/services/script/aquacyd_save_schedule',
        );
        final Object? decoded = jsonDecode(request.body);
        expect(decoded, containsPair('schedule_target', 'filter'));
        return http.Response('[]', 200);
      }),
    );

    await api.callScript(AquaScripts.saveSchedule, <String, Object?>{
      'schedule_target': 'filter',
    });
  });

  test('parsuje historię i pomija wartości nienumeryczne', () async {
    final api = HomeAssistantApi(
      credentials,
      client: MockClient((request) async {
        expect(
          request.url.queryParameters['filter_entity_id'],
          AquaEntityIds.ph,
        );
        expect(request.url.queryParameters['minimal_response'], 'true');
        return http.Response(
          jsonEncode(<Object?>[
            <Object?>[
              <String, Object?>{
                'entity_id': AquaEntityIds.ph,
                'state': '7.10',
                'last_changed': '2026-07-30T08:00:00Z',
              },
              <String, Object?>{
                'state': 'unknown',
                'last_changed': '2026-07-30T08:05:00Z',
              },
              <String, Object?>{
                'state': '7.20',
                'last_changed': '2026-07-30T08:10:00Z',
              },
            ],
          ]),
          200,
        );
      }),
    );

    final history = await api.fetchHistory(
      AquaEntityIds.ph,
      const Duration(hours: 6),
    );

    expect(history, hasLength(2));
    expect(history.first.value, 7.1);
    expect(history.last.value, 7.2);
  });

  test('401 jest rozpoznawane jako błąd uwierzytelniania', () async {
    final api = HomeAssistantApi(
      credentials,
      client: MockClient((_) async => http.Response('Unauthorized', 401)),
    );

    await expectLater(
      api.fetchConfig(),
      throwsA(
        isA<HomeAssistantFailure>().having(
          (failure) => failure.type,
          'type',
          HomeAssistantFailureType.authentication,
        ),
      ),
    );
  });

  test('nieprawidłowy JSON jest raportowany', () async {
    final api = HomeAssistantApi(
      credentials,
      client: MockClient((_) async => http.Response('{broken', 200)),
    );

    await expectLater(
      api.fetchConfig(),
      throwsA(
        isA<HomeAssistantFailure>().having(
          (failure) => failure.type,
          'type',
          HomeAssistantFailureType.invalidResponse,
        ),
      ),
    );
  });

  test('odrzuca nieznaną encję historii przed wykonaniem żądania', () async {
    final api = HomeAssistantApi(
      credentials,
      client: MockClient(
        (_) async => fail('Żądanie HTTP nie powinno zostać wykonane.'),
      ),
    );

    expect(
      () => api.fetchHistory('sensor.unknown', const Duration(hours: 1)),
      throwsArgumentError,
    );
  });
}

Map<String, Object?> _entityJson(String entityId, String state) {
  return <String, Object?>{
    'entity_id': entityId,
    'state': state,
    'attributes': <String, Object?>{},
    'last_changed': '2026-07-30T08:00:00Z',
    'last_updated': '2026-07-30T08:00:00Z',
  };
}
