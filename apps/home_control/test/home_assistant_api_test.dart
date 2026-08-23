import 'dart:convert';
import 'dart:io';

import 'package:aquacyd_home/src/data/home_assistant_api.dart';
import 'package:aquacyd_home/src/data/home_assistant_network_policy.dart';
import 'package:aquacyd_home/src/data/home_assistant_socket.dart';
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

  test('nie przekazuje tokenu przez przekierowanie HTTP', () async {
    var requests = 0;
    final api = HomeAssistantApi(
      credentials,
      client: MockClient((_) async {
        requests++;
        return http.Response(
          '',
          HttpStatus.found,
          headers: const <String, String>{
            'location': 'http://public.example.net/steal-token',
          },
        );
      }),
    );

    await expectLater(
      api.fetchConfig(),
      throwsA(
        isA<HomeAssistantFailure>().having(
          (failure) => failure.type,
          'type',
          HomeAssistantFailureType.server,
        ),
      ),
    );
    expect(requests, 1);
  });

  test('odrzuca lokalną nazwę HTTP rozwiązaną do publicznego IP', () async {
    final localCredentials = HomeAssistantCredentials.parse(
      baseUrl: 'http://homeassistant.local:8123',
      accessToken: token,
    );
    final api = HomeAssistantApi(
      localCredentials,
      hostResolver: (_) async => <InternetAddress>[
        InternetAddress('203.0.113.10'),
      ],
      client: MockClient(
        (_) async => fail('Publiczny cel HTTP nie może otrzymać żądania.'),
      ),
    );

    await expectLater(
      api.fetchConfig(),
      throwsA(
        isA<HomeAssistantFailure>().having(
          (failure) => failure.type,
          'type',
          HomeAssistantFailureType.network,
        ),
      ),
    );
  });

  test('przypina lokalne żądanie HTTP do zweryfikowanego adresu IP', () async {
    final localCredentials = HomeAssistantCredentials.parse(
      baseUrl: 'http://homeassistant.local:8123',
      accessToken: token,
    );
    final api = HomeAssistantApi(
      localCredentials,
      hostResolver: (_) async => <InternetAddress>[
        InternetAddress('192.168.1.25'),
      ],
      client: MockClient((request) async {
        expect(request.url.host, '192.168.1.25');
        expect(
          request.headers[HttpHeaders.hostHeader],
          'homeassistant.local:8123',
        );
        expect(
          request.headers[HttpHeaders.authorizationHeader],
          'Bearer $token',
        );
        return http.Response(
          jsonEncode(<String, Object?>{
            'location_name': 'Dom',
            'version': '2026.8.0',
            'time_zone': 'Europe/Warsaw',
          }),
          200,
        );
      }),
    );

    final config = await api.fetchConfig();

    expect(config.locationName, 'Dom');
  });

  test(
    'nie otwiera ws ani nie wysyła tokenu po zmianie DNS na publiczny',
    () async {
      final localCredentials = HomeAssistantCredentials.parse(
        baseUrl: 'http://homeassistant.local:8123',
        accessToken: token,
      );
      var channelCreations = 0;
      final socket = HomeAssistantSocket(
        localCredentials,
        hostResolver: (_) async => <InternetAddress>[
          InternetAddress('203.0.113.10'),
        ],
        channelFactory: (_, _) {
          channelCreations++;
          fail('Kanał WebSocket nie może zostać utworzony dla publicznego IP.');
        },
      );
      addTearDown(socket.dispose);

      await expectLater(
        socket.connect(),
        throwsA(isA<HomeAssistantNetworkPolicyException>()),
      );
      expect(channelCreations, 0);
      expect(socket.status, HomeAssistantSocketStatus.disconnected);
    },
  );

  test('WebSocket nie podąża za przekierowaniem handshake', () async {
    var redirectedRequests = 0;
    final redirectedServer = await HttpServer.bind(
      InternetAddress.loopbackIPv4,
      0,
    );
    final redirectedSubscription = redirectedServer.listen((request) async {
      redirectedRequests++;
      request.response.statusCode = HttpStatus.badRequest;
      await request.response.close();
    });
    final redirectServer = await HttpServer.bind(
      InternetAddress.loopbackIPv4,
      0,
    );
    final redirectSubscription = redirectServer.listen((request) async {
      request.response
        ..statusCode = HttpStatus.found
        ..headers.set(
          HttpHeaders.locationHeader,
          'http://127.0.0.1:${redirectedServer.port}/api/websocket',
        );
      await request.response.close();
    });
    addTearDown(() async {
      await redirectSubscription.cancel();
      await redirectedSubscription.cancel();
      await redirectServer.close(force: true);
      await redirectedServer.close(force: true);
    });
    final localCredentials = HomeAssistantCredentials.parse(
      baseUrl: 'http://127.0.0.1:${redirectServer.port}',
      accessToken: token,
    );
    final socket = HomeAssistantSocket(
      localCredentials,
      connectionTimeout: const Duration(seconds: 1),
    );

    await socket.connect();
    await socket.dispose();

    expect(redirectedRequests, 0);
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
