import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:aquacyd_home/src/data/credentials_store.dart';
import 'package:aquacyd_home/src/data/home_assistant_api.dart';
import 'package:aquacyd_home/src/domain/models.dart';
import 'package:aquacyd_home/src/home_control/home_assistant_data_source.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:http/http.dart' as http;
import 'package:http/testing.dart';
import 'package:secure_connectivity/secure_connectivity.dart';

void main() {
  test(
    'anulowanie podczas registry zamyka transport i jest propagowane',
    () async {
      final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
      final sockets = <WebSocket>[];
      final registryStarted = Completer<void>();
      final serverSubscription = server.listen((request) async {
        final socket = await WebSocketTransformer.upgrade(request);
        sockets.add(socket);
        socket.add(jsonEncode(<String, Object?>{'type': 'auth_required'}));
        socket.listen((message) {
          final payload = jsonDecode(message as String) as Map<String, Object?>;
          switch (payload['type']) {
            case 'auth':
              expect(payload['access_token'], _token);
              socket.add(jsonEncode(<String, Object?>{'type': 'auth_ok'}));
            case 'subscribe_events':
              socket.add(
                jsonEncode(<String, Object?>{
                  'id': payload['id'],
                  'type': 'result',
                  'success': true,
                  'result': null,
                }),
              );
            case 'config/area_registry/list':
            case 'config/device_registry/list':
            case 'config/entity_registry/list':
            case 'get_services':
              if (!registryStarted.isCompleted) registryStarted.complete();
          }
        });
      });
      addTearDown(() async {
        for (final socket in sockets) {
          await socket.close();
        }
        await serverSubscription.cancel();
        await server.close(force: true);
      });

      final credentials = HomeAssistantCredentials.parse(
        baseUrl: 'http://127.0.0.1:${server.port}',
        accessToken: _token,
      );
      final source = HomeAssistantDataSource(
        credentials: credentials,
        credentialsStore: _MemoryCredentialsStore(),
        apiFactory: (value) => HomeAssistantApi(
          value,
          client: MockClient((request) async {
            if (request.url.path.endsWith('/api/config')) {
              return http.Response(
                jsonEncode(<String, Object?>{
                  'location_name': 'Test Home',
                  'version': '2026.8.0',
                  'time_zone': 'Europe/Warsaw',
                }),
                HttpStatus.ok,
              );
            }
            if (request.url.path.endsWith('/api/states')) {
              return http.Response(
                jsonEncode(<Object?>[
                  <String, Object?>{
                    'entity_id': 'sensor.room_temperature',
                    'state': '23.4',
                    'attributes': <String, Object?>{
                      'friendly_name': 'Temperatura pokoju',
                      'unit_of_measurement': '°C',
                    },
                    'last_changed': '2026-08-13T08:00:00Z',
                    'last_updated': '2026-08-13T08:00:00Z',
                  },
                ]),
                HttpStatus.ok,
              );
            }
            return http.Response('{}', HttpStatus.notFound);
          }),
        ),
      );
      addTearDown(source.close);
      final cancellation = CancellationToken();

      final connection = source.connect(cancellation);
      await registryStarted.future.timeout(const Duration(seconds: 5));
      cancellation.cancel('Zmiana profilu podczas pobierania registry.');

      await expectLater(connection, throwsA(isA<OperationCancelled>()));
    },
  );
}

const _token = '12345678901234567890123456789012';

final class _MemoryCredentialsStore implements CredentialsStore {
  HomeAssistantCredentials? value;

  @override
  Future<void> clear() async => value = null;

  @override
  Future<HomeAssistantCredentials?> load() async => value;

  @override
  Future<void> save(HomeAssistantCredentials credentials) async {
    value = credentials;
  }
}
