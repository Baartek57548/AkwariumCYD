import 'dart:convert';

import 'package:aquacyd_home/src/aquahub/api.dart';
import 'package:aquacyd_home/src/aquahub/domain.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:http/http.dart' as http;
import 'package:http/testing.dart';

void main() {
  const fingerprint =
      '0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF';

  test('identyfikuje AquaHub przed parowaniem', () async {
    final api = HubApi.bootstrap(
      Uri.parse('https://aquahub.local:8443'),
      client: MockClient((request) async {
        expect(request.url.path, '/api/v1/info');
        expect(request.headers.containsKey('authorization'), isFalse);
        return http.Response(
          jsonEncode(<String, Object?>{
            'product': 'aquahub-p4',
            'api_version': 1,
            'hostname': 'aquahub.local',
            'tls_fingerprint': fingerprint,
            'pairing_available': true,
          }),
          200,
          headers: <String, String>{
            'content-type': 'application/json; charset=utf-8',
          },
        );
      }),
    );

    final info = await api.fetchInfo();

    expect(info.product, 'aquahub-p4');
    expect(info.pairingAvailable, isTrue);
    api.close();
  });

  test('pobiera wszystkie strony uniwersalnych encji', () async {
    final credentials = HubCredentials.parse(
      baseUrl: 'https://aquahub.local:8443/',
      accessToken: '12345678901234567890123456789012',
      tlsFingerprint: fingerprint,
    );
    var requests = 0;
    final capturedRequests = <http.Request>[];
    final api = HubApi.authenticated(
      credentials,
      client: MockClient((request) async {
        requests++;
        capturedRequests.add(request);
        final offset =
            int.tryParse(request.url.queryParameters['offset'] ?? '') ?? 0;
        return http.Response(
          jsonEncode(<String, Object?>{
            'total': 2,
            'offset': offset,
            'limit': 48,
            'items': offset == 0
                ? <Object?>[
                    _entityJson('temperature', 'Temperatura', 24.3),
                    _entityJson('filter', 'Filtr', true, writable: true),
                  ]
                : <Object?>[],
          }),
          200,
          headers: <String, String>{
            'content-type': 'application/json; charset=utf-8',
          },
        );
      }),
    );

    final entities = await api.fetchEntities();

    expect(requests, 1);
    expect(capturedRequests.single.url.path, '/api/v1/entities');
    expect(
      capturedRequests.single.headers['authorization'],
      'Bearer 12345678901234567890123456789012',
    );
    expect(entities, hasLength(2));
    expect(entities.first.formattedState, '24.30 °C');
    expect(entities.last.kind, HubEntityKind.switchEntity);
    api.close();
  });

  test('wysyła uniwersalną komendę encji', () async {
    final credentials = HubCredentials.parse(
      baseUrl: 'https://aquahub.local:8443',
      accessToken: '12345678901234567890123456789012',
      tlsFingerprint: fingerprint,
    );
    final api = HubApi.authenticated(
      credentials,
      client: MockClient((request) async {
        expect(request.method, 'POST');
        expect(request.url.path, '/api/v1/entities/filter/command');
        expect(jsonDecode(request.body), <String, Object?>{'value': true});
        return http.Response('{"accepted":true}', 200);
      }),
    );

    await api.sendCommand('filter', true);
    api.close();
  });

  test('odrzuca odpowiedź spoza kontraktu', () async {
    final api = HubApi.bootstrap(
      Uri.parse('https://aquahub.local:8443'),
      client: MockClient(
        (_) async => http.Response('{"product":"other"}', 200),
      ),
    );

    expect(api.fetchInfo(), throwsA(isA<FormatException>()));
    api.close();
  });

  test('odczytuje zweryfikowane wydanie i postęp OTA', () async {
    final credentials = HubCredentials.parse(
      baseUrl: 'https://aquahub.local:8443',
      accessToken: '12345678901234567890123456789012',
      tlsFingerprint: fingerprint,
    );
    final api = HubApi.authenticated(
      credentials,
      client: MockClient((request) async {
        expect(request.url.path, '/api/v1/updates');
        return http.Response(
          jsonEncode(<String, Object?>{
            'supported': true,
            'target': 'aquahub-p4',
            'current_version': '1.0.0',
            'current_security_version': 1,
            'phase': 'available',
            'progress_percent': 0,
            'bytes_received': 0,
            'total_bytes': 1114112,
            'error': '',
            'release': <String, Object?>{
              'release_id': 'stable-1.1.0',
              'version': '1.1.0',
              'size': 1114112,
              'security_version': 1,
              'mandatory': false,
              'notes': 'Poprawki stabilności i centrum automatyzacji.',
            },
          }),
          200,
          headers: <String, String>{
            'content-type': 'application/json; charset=utf-8',
          },
        );
      }),
    );

    final status = await api.fetchUpdateStatus();

    expect(status.phase, HubUpdatePhase.available);
    expect(status.release?.version, '1.1.0');
    expect(status.release?.sizeBytes, 1114112);
    api.close();
  });

  test('zapisuje, pobiera i usuwa automatyzację', () async {
    final credentials = HubCredentials.parse(
      baseUrl: 'https://aquahub.local:8443',
      accessToken: '12345678901234567890123456789012',
      tlsFingerprint: fingerprint,
    );
    final rule = HubAutomationRule(
      id: 'auto_temperature',
      name: 'Chłodzenie awaryjne',
      enabled: true,
      cooldown: const Duration(minutes: 1),
      trigger: const HubAutomationClause(
        entityId: 'temperature',
        comparison: HubAutomationComparison.above,
        value: 27.5,
      ),
      condition: null,
      action: const HubAutomationAction(entityId: 'aerator', value: true),
    );
    var saved = false;
    var deleted = false;
    final api = HubApi.authenticated(
      credentials,
      client: MockClient((request) async {
        if (request.method == 'POST') {
          expect(request.url.path, '/api/v1/automations');
          final body = jsonDecode(request.body) as Map<String, Object?>;
          expect(body['id'], rule.id);
          expect(body['cooldown_ms'], 60000);
          saved = true;
          return http.Response(
            '{"accepted":true}',
            200,
            headers: <String, String>{
              'content-type': 'application/json; charset=utf-8',
            },
          );
        }
        if (request.method == 'DELETE') {
          expect(request.url.path, '/api/v1/automations/${rule.id}');
          deleted = true;
          return http.Response(
            '{"accepted":true}',
            200,
            headers: <String, String>{
              'content-type': 'application/json; charset=utf-8',
            },
          );
        }
        return http.Response(
          jsonEncode(<String, Object?>{
            'capacity': 8,
            'count': 1,
            'items': <Object?>[rule.toJson()],
          }),
          200,
          headers: <String, String>{
            'content-type': 'application/json; charset=utf-8',
          },
        );
      }),
    );

    await api.saveAutomation(rule);
    final collection = await api.fetchAutomations();
    await api.deleteAutomation(rule.id);

    expect(saved, isTrue);
    expect(deleted, isTrue);
    expect(collection.rules.single.trigger.value, 27.5);
    api.close();
  });

  test('nieznany przyszły typ encji nie blokuje całego rejestru', () {
    final entity = HubEntity.fromJson(<String, Object?>{
      ..._entityJson('pump_speed', 'Prędkość pompy', 'auto'),
      'kind': 'fan',
    });

    expect(entity.kind, HubEntityKind.unknown);
    expect(entity.formattedState, 'auto');
  });
}

Map<String, Object?> _entityJson(
  String id,
  String name,
  Object state, {
  bool writable = false,
}) => <String, Object?>{
  'id': id,
  'device_id': 'aquacyd_aquarium',
  'name': name,
  'kind': state is bool ? 'switch' : 'sensor',
  'unit': state is num ? '°C' : '',
  'writable': writable,
  'critical': false,
  'state': state,
  'changed_at_ms': 100,
  'updated_at_ms': 100,
};
