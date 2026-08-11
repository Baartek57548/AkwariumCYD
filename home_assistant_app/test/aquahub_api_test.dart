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
