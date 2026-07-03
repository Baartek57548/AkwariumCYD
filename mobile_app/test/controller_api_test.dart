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
        request.response.write(
          jsonEncode({
            'device': 'cydAkwarium',
            'temperature': {'history': <dynamic>[]},
          }),
        );
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
}

class _RecordedRequest {
  const _RecordedRequest(this.method, this.uri, this.body);

  final String method;
  final Uri uri;
  final String body;
}
