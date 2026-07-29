import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:cyd_aquarium_mobile/full_controller/controller_api.dart';
import 'package:flutter_test/flutter_test.dart';

import 'support/firmware_package_fixture.dart';

const _networkTestDeadline = Duration(milliseconds: 250);
const _slowNetworkResponse = Duration(milliseconds: 750);

void main() {
  test('ControllerApi preserves web API paths and form fields', () async {
    final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
    addTearDown(() => server.close(force: true));
    final requests = <_RecordedRequest>[];
    final subscription = server.listen((request) async {
      final body = await utf8.decoder.bind(request).join();
      requests.add(
        _RecordedRequest(
          request.method,
          request.uri,
          body,
          sessionHeader: request.headers.value('X-AquaCYD-Session'),
        ),
      );
      request.response.headers.contentType = ContentType.json;
      if (request.uri.path == '/api/status') {
        request.response.write(jsonEncode(_validStatus(includeHistory: true)));
      } else {
        request.response.write(
          jsonEncode({'success': true, 'code': 'ok', 'message': 'Zapisano.'}),
        );
      }
      await request.response.close();
    });
    addTearDown(subscription.cancel);
    final api = ControllerApi(
      Uri.parse('http://${server.address.address}:${server.port}'),
    );

    final status = await api.status(includeHistory: true);
    final result = await api.action(
      'save_temperature',
      legacyPin: '1234',
      payload: const {'heaterMode': 0, 'target': '25.5', 'enabled': true},
    );

    expect(status['device'], 'cydAkwarium');
    expect(result.success, isTrue);
    expect(requests, hasLength(2));
    expect(requests[0].method, 'GET');
    expect(requests[0].uri.path, '/api/status');
    expect(requests[0].uri.queryParameters['history'], '1');
    expect(requests[1].method, 'POST');
    expect(requests[1].uri.path, '/api/action');
    final fields = Uri.splitQueryString(requests[1].body);
    expect(fields['action'], 'save_temperature');
    expect(fields['pin'], '1234');
    expect(fields['heaterMode'], '0');
    expect(fields['target'], '25.5');
    expect(fields['enabled'], '1');
  });

  test(
    'ControllerApi maps rejected PIN response to authentication error',
    () async {
      final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
      addTearDown(() => server.close(force: true));
      final subscription = server.listen((request) async {
        await request.drain<void>();
        request.response
          ..statusCode = HttpStatus.forbidden
          ..headers.contentType = ContentType.json
          ..write(
            jsonEncode({
              'success': false,
              'code': 'invalid_pin',
              'message': 'Błędny PIN.',
            }),
          );
        await request.response.close();
      });
      addTearDown(subscription.cancel);
      final api = ControllerApi(
        Uri.parse('http://${server.address.address}:${server.port}'),
      );

      try {
        await api.authenticate('9999');
        fail('Oczekiwano odrzucenia PIN-u.');
      } on ControllerApiException catch (error) {
        expect(error.code, 'invalid_pin');
        expect(error.statusCode, HttpStatus.forbidden);
        expect(error.isAuthenticationError, isTrue);
      }
    },
  );

  test(
    'protocol v2 discovers capabilities and uses a tokenized command',
    () async {
      final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
      addTearDown(() => server.close(force: true));
      final requests = <_RecordedRequest>[];
      const token = '0123456789abcdef0123456789abcdef';
      final subscription = server.listen((request) async {
        final body = await utf8.decoder.bind(request).join();
        requests.add(
          _RecordedRequest(
            request.method,
            request.uri,
            body,
            sessionHeader: request.headers.value('X-AquaCYD-Session'),
          ),
        );
        request.response.headers.contentType = ContentType.json;
        switch (request.uri.path) {
          case '/api/v2/capabilities':
            request.response.write(
              jsonEncode({
                'ok': true,
                'data': {
                  'apiVersions': [1, 2],
                  'features': {'idempotency': true},
                },
              }),
            );
            break;
          case '/api/v2/auth':
            request.response.write(
              jsonEncode({
                'type': 'auth',
                'v': 2,
                'ok': true,
                'code': 'authenticated',
                'data': {'sessionToken': token, 'expiresInSec': 300},
              }),
            );
            break;
          default:
            request.response.write(
              jsonEncode({
                'ok': true,
                'code': 'ok',
                'message': 'Polecenie przyjęte.',
              }),
            );
        }
        await request.response.close();
      });
      addTearDown(subscription.cancel);
      final api = ControllerApi(
        Uri.parse('http://${server.address.address}:${server.port}'),
      );

      final capabilities = await api.capabilities();
      final session = await api.authenticateSession('1234');
      final result = await api.actionV2(
        'start_service_mode',
        commandId: 'mobile_command_0001',
        token: session.token,
        payload: const {'durationSec': 1800},
      );

      expect(capabilities['apiVersions'], [1, 2]);
      expect(session.token, token);
      expect(session.isValid, isTrue);
      expect(result.success, isTrue);
      expect(requests.map((request) => request.uri.path), [
        '/api/v2/capabilities',
        '/api/v2/auth',
        '/api/action',
      ]);
      expect(jsonDecode(requests[1].body), {'v': 2, 'pin': '1234'});
      final actionFields = Uri.splitQueryString(requests[2].body);
      expect(actionFields['action'], 'start_service_mode');
      expect(actionFields['v'], '2');
      expect(actionFields['commandId'], 'mobile_command_0001');
      expect(actionFields['token'], token);
      expect(actionFields['durationSec'], '1800');
      expect(actionFields, isNot(contains('pin')));
      expect(requests[2].sessionHeader, token);
    },
  );

  test(
    'protected HTTP endpoints use only the session header in secure mode',
    () async {
      final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
      addTearDown(() => server.close(force: true));
      const token = '0123456789abcdef0123456789abcdef';
      final requests = <_RecordedRequest>[];
      final subscription = server.listen((request) async {
        final body = await utf8.decoder.bind(request).join();
        requests.add(
          _RecordedRequest(
            request.method,
            request.uri,
            body,
            sessionHeader: request.headers.value('X-AquaCYD-Session'),
          ),
        );
        request.response.headers.contentType = ContentType.json;
        request.response.write(
          request.uri.path == '/api/v2/logout'
              ? jsonEncode({'ok': true, 'code': 'logged_out'})
              : jsonEncode({
                  'ok': true,
                  'success': true,
                  'code': 'ok',
                  'message': 'Wykonano.',
                }),
        );
        await request.response.close();
      });
      addTearDown(subscription.cancel);
      final api = ControllerApi(
        Uri.parse('http://${server.address.address}:${server.port}'),
      );

      await api.logs(sessionToken: token);
      await api.busDiagnostics(sessionToken: token);
      await api.action(
        'save_display',
        sessionToken: token,
        payload: const {'brightness': 80},
      );
      await api.setBrowserTime(1785276000, sessionToken: token);
      await api.revokeSession(token);

      expect(requests, hasLength(5));
      expect(requests.map((request) => request.uri.path), const [
        '/api/logs',
        '/api/bus-diagnostics',
        '/api/action',
        '/settime',
        '/api/v2/logout',
      ]);
      for (final request in requests) {
        expect(request.sessionHeader, token);
        expect(request.uri.queryParameters, isNot(contains('pin')));
        if (request.uri.path == '/api/action' ||
            request.uri.path == '/settime') {
          expect(Uri.splitQueryString(request.body), isNot(contains('pin')));
        }
      }
    },
  );

  test('deadline covers response headers and complete body', () async {
    final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
    addTearDown(() => server.close(force: true));
    final subscription = server.listen((request) async {
      request.response.headers.contentType = ContentType.json;
      request.response.write('{"ok":');
      await request.response.flush();
      await Future<void>.delayed(_slowNetworkResponse);
      try {
        request.response.write('true}');
        await request.response.close();
      } on Object {
        // The client correctly aborts the socket when the deadline expires.
      }
    });
    addTearDown(subscription.cancel);
    final api = ControllerApi(
      Uri.parse('http://${server.address.address}:${server.port}'),
      requestDeadline: _networkTestDeadline,
      maximumReadAttempts: 1,
    );

    await expectLater(
      api.getJson('/slow'),
      throwsA(
        isA<ControllerApiException>().having(
          (error) => error.code,
          'code',
          'timeout',
        ),
      ),
    );
  });

  test('idempotent GET is retried after a transient timeout', () async {
    final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
    addTearDown(() => server.close(force: true));
    var requests = 0;
    final subscription = server.listen((request) async {
      requests++;
      if (requests == 1) {
        await Future<void>.delayed(_slowNetworkResponse);
      }
      try {
        request.response.headers.contentType = ContentType.json;
        request.response.write(jsonEncode({'ok': true}));
        await request.response.close();
      } on Object {
        // The first request is expected to be cancelled by the deadline.
      }
    });
    addTearDown(subscription.cancel);
    final api = ControllerApi(
      Uri.parse('http://${server.address.address}:${server.port}'),
      requestDeadline: _networkTestDeadline,
      readRetryDelay: Duration.zero,
      maximumReadAttempts: 2,
    );

    final result = await api.getJson('/retry');

    expect(result['ok'], isTrue);
    expect(requests, 2);
  });

  test('mutating POST is never retried after a timeout', () async {
    final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
    addTearDown(() => server.close(force: true));
    var requests = 0;
    final subscription = server.listen((request) async {
      requests++;
      await request.drain<void>();
      await Future<void>.delayed(_slowNetworkResponse);
      try {
        request.response.headers.contentType = ContentType.json;
        request.response.write(
          jsonEncode({'success': true, 'code': 'ok', 'message': 'Zapisano.'}),
        );
        await request.response.close();
      } on Object {
        // The sole POST is expected to be cancelled by the deadline.
      }
    });
    addTearDown(subscription.cancel);
    final api = ControllerApi(
      Uri.parse('http://${server.address.address}:${server.port}'),
      requestDeadline: _networkTestDeadline,
      readRetryDelay: Duration.zero,
      maximumReadAttempts: 3,
    );

    await expectLater(
      api.action(
        'relay',
        payload: const {'channel': 1},
        sessionToken: '0123456789abcdef0123456789abcdef',
      ),
      throwsA(
        isA<ControllerApiException>().having(
          (error) => error.code,
          'code',
          'timeout',
        ),
      ),
    );
    expect(requests, 1);
  });

  test('mutating web-session GET is never retried', () async {
    final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
    addTearDown(() => server.close(force: true));
    var requests = 0;
    final subscription = server.listen((request) async {
      requests++;
      await Future<void>.delayed(_slowNetworkResponse);
      try {
        request.response.headers.contentType = ContentType.json;
        request.response.write(jsonEncode({'ok': true}));
        await request.response.close();
      } on Object {
        // The state-changing heartbeat must not be replayed.
      }
    });
    addTearDown(subscription.cancel);
    final api = ControllerApi(
      Uri.parse('http://${server.address.address}:${server.port}'),
      requestDeadline: _networkTestDeadline,
      readRetryDelay: Duration.zero,
      maximumReadAttempts: 3,
    );

    await expectLater(
      api.webSession('mobile-test', 'active'),
      throwsA(
        isA<ControllerApiException>().having(
          (error) => error.code,
          'code',
          'timeout',
        ),
      ),
    );
    expect(requests, 1);
  });

  test('download rejects an excessive byte limit before network access', () {
    final api = ControllerApi(Uri.parse('http://127.0.0.1:1'));

    expect(
      () => api.download(
        '/download',
        maximumBytes: ControllerApi.maximumDownloadBytes + 1,
      ),
      throwsA(
        isA<ControllerApiException>().having(
          (error) => error.code,
          'code',
          'invalid_download_limit',
        ),
      ),
    );
  });

  test('status maps a partial payload to invalid_status', () async {
    final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
    addTearDown(() => server.close(force: true));
    final subscription = server.listen((request) async {
      await request.drain<void>();
      request.response.headers.contentType = ContentType.json;
      request.response.write(jsonEncode({'device': 'cydAkwarium'}));
      await request.response.close();
    });
    addTearDown(subscription.cancel);
    final api = ControllerApi(
      Uri.parse('http://${server.address.address}:${server.port}'),
    );

    await expectLater(
      api.status(),
      throwsA(
        isA<ControllerApiException>().having(
          (error) => error.code,
          'code',
          'invalid_status',
        ),
      ),
    );
  });

  test('history file list is bounded', () async {
    final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
    addTearDown(() => server.close(force: true));
    final subscription = server.listen((request) async {
      await request.drain<void>();
      request.response.headers.contentType = ContentType.json;
      request.response.write(
        jsonEncode(
          List<String>.generate(
            ControllerApi.maximumHistoryFileEntries + 1,
            (index) => '/aq/data/history/$index.csv',
          ),
        ),
      );
      await request.response.close();
    });
    addTearDown(subscription.cancel);
    final api = ControllerApi(
      Uri.parse('http://${server.address.address}:${server.port}'),
    );

    await expectLater(
      api.historyFiles(),
      throwsA(
        isA<ControllerApiException>().having(
          (error) => error.code,
          'code',
          'history_files_too_large',
        ),
      ),
    );
  });

  test('firmware upload uses only the administrator session header', () async {
    final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
    addTearDown(() => server.close(force: true));
    Uri? requestUri;
    String? sessionHeader;
    Uint8List? requestBody;
    final subscription = server.listen((request) async {
      requestUri = request.uri;
      sessionHeader = request.headers.value('X-AquaCYD-Session');
      final body = BytesBuilder(copy: false);
      await for (final chunk in request) {
        body.add(chunk);
      }
      requestBody = body.takeBytes();
      request.response.headers.contentType = ContentType.json;
      request.response.write(
        jsonEncode({'ok': true, 'message': 'Pakiet zweryfikowany. Restart.'}),
      );
      await request.response.close();
    });
    addTearDown(subscription.cancel);
    final api = ControllerApi(
      Uri.parse('http://${server.address.address}:${server.port}'),
    );
    final firmware = buildFirmwarePackageFixture();
    const token = '0123456789abcdef0123456789abcdef';
    final progress = <(int, int)>[];

    final result = await api.uploadFirmware(
      firmware,
      'firmware-ili9341.aqfw',
      token,
      onProgress: (sent, total) => progress.add((sent, total)),
    );

    expect(result.success, isTrue);
    expect(result.code, 'ok');
    expect(requestUri?.path, '/update');
    expect(requestUri?.queryParameters, isEmpty);
    expect(sessionHeader, token);
    expect(progress.first, (0, firmware.length));
    expect(progress.last, (firmware.length, firmware.length));
    expect(requestBody, containsAllInOrder(<int>[...utf8.encode('AQCYDOTA')]));
    expect(
      utf8.decode(requestBody!, allowMalformed: true),
      contains('filename="firmware-ili9341.aqfw"'),
    );
  });

  test('firmware upload rejects ok:false even with HTTP 200', () async {
    final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
    addTearDown(() => server.close(force: true));
    final subscription = server.listen((request) async {
      await request.drain<void>();
      request.response.headers.contentType = ContentType.json;
      request.response.write(
        jsonEncode({
          'ok': false,
          'code': 'signature_invalid',
          'message': 'Podpis pakietu jest nieprawidłowy.',
        }),
      );
      await request.response.close();
    });
    addTearDown(subscription.cancel);
    final api = ControllerApi(
      Uri.parse('http://${server.address.address}:${server.port}'),
    );

    await expectLater(
      api.uploadFirmware(
        buildFirmwarePackageFixture(),
        'firmware.aqfw',
        '0123456789abcdef0123456789abcdef',
      ),
      throwsA(
        isA<ControllerApiException>().having(
          (error) => error.code,
          'code',
          'signature_invalid',
        ),
      ),
    );
  });

  test('firmware upload validates the package before network access', () async {
    final api = ControllerApi(Uri.parse('http://127.0.0.1:1'));
    final tampered = buildFirmwarePackageFixture();
    tampered[tampered.length - 1] ^= 1;

    await expectLater(
      api.uploadFirmware(
        tampered,
        'firmware.aqfw',
        '0123456789abcdef0123456789abcdef',
      ),
      throwsA(
        isA<ControllerApiException>().having(
          (error) => error.code,
          'code',
          'firmware_digest_mismatch',
        ),
      ),
    );
  });
}

Map<String, dynamic> _validStatus({bool includeHistory = false}) {
  return {
    'device': 'cydAkwarium',
    'sensors': {'temp_c': 24.6, 'temp_valid': true},
    'alarms': {'flags': 0},
    'config': {'target_temp': 25.0},
    'display': {'brightness': 80},
    'water': {'active': false},
    'leak': {'action': 'disable_all'},
    'modules': {'heater_on': false},
    'schedules': {'light': 'day'},
    'eco': {'safe_active': false},
    'clock': {'valid': true},
    'temperature': {
      'current': 24.6,
      'target': 25.0,
      'hysteresis': 0.5,
      'historyCapacity': 144,
      if (includeHistory) 'history': <dynamic>[],
    },
    'battery': {'voltage': null},
    'firmware': {'version': 'test'},
    'network': {'rssi': -52},
    'web': {'focus': false},
    'system': {'uptime': 120, 'freeHeap': 180000},
    'relays': {'heater': false},
    'schedule': {'heaterMode': 0},
    'feeding': {'active': false},
  };
}

class _RecordedRequest {
  const _RecordedRequest(
    this.method,
    this.uri,
    this.body, {
    this.sessionHeader,
  });

  final String method;
  final Uri uri;
  final String body;
  final String? sessionHeader;
}
