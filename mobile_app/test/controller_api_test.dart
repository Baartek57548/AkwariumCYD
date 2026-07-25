import 'dart:convert';
import 'dart:io';

import 'package:cyd_aquarium_mobile/full_controller/controller_api.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('ControllerApi preserves web API paths and form fields', () async {
    final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
    addTearDown(() => server.close(force: true));
    final requests = <_RecordedRequest>[];
    final subscription = server.listen((request) async {
      final body = await utf8.decoder.bind(request).join();
      requests.add(_RecordedRequest(request.method, request.uri, body));
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
      pin: '1234',
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

  test('deadline covers response headers and complete body', () async {
    final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
    addTearDown(() => server.close(force: true));
    final subscription = server.listen((request) async {
      request.response.headers.contentType = ContentType.json;
      request.response.write('{"ok":');
      await request.response.flush();
      await Future<void>.delayed(const Duration(milliseconds: 150));
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
      requestDeadline: const Duration(milliseconds: 40),
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
        await Future<void>.delayed(const Duration(milliseconds: 120));
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
      requestDeadline: const Duration(milliseconds: 40),
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
      await Future<void>.delayed(const Duration(milliseconds: 120));
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
      requestDeadline: const Duration(milliseconds: 40),
      readRetryDelay: Duration.zero,
      maximumReadAttempts: 3,
    );

    await expectLater(
      api.action('relay', payload: const {'channel': 1}),
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
      await Future<void>.delayed(const Duration(milliseconds: 120));
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
      requestDeadline: const Duration(milliseconds: 40),
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
  const _RecordedRequest(this.method, this.uri, this.body);

  final String method;
  final Uri uri;
  final String body;
}
