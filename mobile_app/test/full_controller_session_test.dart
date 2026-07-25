import 'dart:async';
import 'dart:typed_data';

import 'package:cyd_aquarium_mobile/full_controller/connection_health.dart';
import 'package:cyd_aquarium_mobile/full_controller/controller_api.dart';
import 'package:cyd_aquarium_mobile/full_controller/controller_session.dart';
import 'package:cyd_aquarium_mobile/full_controller/data_access.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('development session exposes complete web-compatible status', () async {
    final session = ControllerSession.development();
    addTearDown(session.dispose);

    await session.connect();

    expect(session.connected, isTrue);
    expect(session.status.section('sensors').flag('temp_valid'), isTrue);
    expect(session.status.section('schedules').section('light'), isNotEmpty);
    expect(
      session.status.section('network').text('configuredStaSsid'),
      isNotEmpty,
    );
    expect(
      session.status.section('display').integer('brightness'),
      inInclusiveRange(10, 100),
    );
  });

  test('development session authenticates and applies web actions', () async {
    final session = ControllerSession.development();
    addTearDown(session.dispose);
    await session.connect();
    await session.login('1234');

    await session.action('set_light', payload: const {'state': false});
    expect(session.status.section('modules').flag('light_on'), isFalse);

    await session.action(
      'save_temperature',
      payload: const {'heaterMode': 0, 'target': '26.2', 'hysteresis': '0.7'},
    );
    expect(session.status.section('temperature').number('target'), 26.2);
    expect(session.status.section('config').number('temp_hysteresis'), 0.7);

    await session.action(
      'save_display',
      payload: const {
        'autoBrightness': false,
        'profile': 'timeout_60s',
        'brightness': 65,
      },
    );
    expect(session.status.section('display').flag('autoBrightness'), isFalse);
    expect(session.status.section('display').text('profile'), 'timeout_60s');
    expect(session.status.section('display').integer('brightness'), 65);
  });

  test('development session rejects an invalid PIN', () async {
    final session = ControllerSession.development();
    addTearDown(session.dispose);

    expect(
      () => session.login('9999'),
      throwsA(
        isA<ControllerApiException>().having(
          (error) => error.code,
          'code',
          'invalid_pin',
        ),
      ),
    );
  });

  test('relay profile validation requires a full payload', () async {
    final session = ControllerSession.development();
    addTearDown(session.dispose);
    await session.connect();
    await session.login('1234');

    expect(
      () => session.action('save_relays', payload: const {'data': '{}'}),
      throwsA(
        isA<ControllerApiException>().having(
          (error) => error.code,
          'code',
          'invalid_relay_profile',
        ),
      ),
    );
  });

  test('concurrent connect calls share one network operation', () async {
    final api = _FakeRemoteApi();
    final connectGate = Completer<void>();
    api.connectGate = connectGate;
    final session = ControllerSession.wifi(api);
    addTearDown(session.dispose);

    final first = session.connect();
    final second = session.connect();
    await Future<void>.delayed(Duration.zero);

    expect(api.connectCalls, 1);
    connectGate.complete();
    await Future.wait([first, second]);

    expect(api.statusCalls, 1);
    expect(session.connectionPhase, ControllerConnectionPhase.online);
  });

  test('last good telemetry survives failures and reconnects', () async {
    final api = _FakeRemoteApi();
    final session = ControllerSession.wifi(api);
    addTearDown(session.dispose);

    await session.connect();
    final lastGoodStatus = session.status;
    expect(session.connectionHealth.rssi, -61);

    api.failStatus = true;
    await session.refresh(reportBusy: false);
    expect(session.status, same(lastGoodStatus));
    expect(session.connectionPhase, ControllerConnectionPhase.reconnecting);

    await session.refresh(reportBusy: false);
    await session.refresh(reportBusy: false);
    expect(session.connectionPhase, ControllerConnectionPhase.offline);

    api.failStatus = false;
    await session.connect(reportBusy: false);
    expect(session.connectionPhase, ControllerConnectionPhase.online);
    expect(session.error, isNull);
  });

  test('control actions are serialized to protect the controller', () async {
    final api = _FakeRemoteApi();
    final actionGate = Completer<ControllerActionResult>();
    api.actionGate = actionGate;
    final session = ControllerSession.wifi(api);
    addTearDown(session.dispose);

    await session.connect();
    await session.login('1234');
    final first = session.action('set_light');
    await Future<void>.delayed(Duration.zero);

    expect(session.activeAction, 'set_light');
    await expectLater(
      session.action('set_filter'),
      throwsA(
        isA<ControllerApiException>().having(
          (error) => error.code,
          'code',
          'action_in_progress',
        ),
      ),
    );
    expect(api.actionCalls, 1);

    actionGate.complete(
      const ControllerActionResult(
        success: true,
        code: 'ok',
        message: 'Zapisano.',
      ),
    );
    await first;
    expect(session.activeAction, isNull);
    expect(api.statusCalls, 2);
  });
}

class _FakeRemoteApi implements ControllerRemoteApi {
  Completer<void>? connectGate;
  Completer<ControllerActionResult>? actionGate;
  bool failStatus = false;
  int connectCalls = 0;
  int statusCalls = 0;
  int actionCalls = 0;

  @override
  Uri get baseUri => Uri.parse('http://192.168.4.1');

  @override
  bool get supportsFileDownload => true;

  @override
  bool get supportsFirmwareUpload => true;

  @override
  bool get supportsWebSession => false;

  @override
  Future<void> connect() async {
    connectCalls += 1;
    await connectGate?.future;
  }

  @override
  Future<void> disconnect() async {}

  @override
  Future<Map<String, dynamic>> status({bool includeHistory = false}) async {
    statusCalls += 1;
    if (failStatus) {
      throw const ControllerApiException(
        code: 'timeout',
        message: 'Sterownik nie odpowiada.',
      );
    }
    return <String, dynamic>{
      'device': 'AquaCYD Test',
      'network': <String, dynamic>{'rssi': -61},
      'temperature': <String, dynamic>{
        'history': includeHistory ? <dynamic>[] : <dynamic>[],
      },
    };
  }

  @override
  Future<ControllerActionResult> authenticate(String pin) async {
    return const ControllerActionResult(
      success: true,
      code: 'ok',
      message: 'Zalogowano.',
    );
  }

  @override
  Future<ControllerActionResult> action(
    String action, {
    Map<String, Object?> payload = const {},
    String? pin,
    bool includePin = true,
  }) async {
    actionCalls += 1;
    return actionGate?.future ??
        const ControllerActionResult(
          success: true,
          code: 'ok',
          message: 'Wykonano.',
        );
  }

  @override
  Future<Map<String, dynamic>> logs(String pin) async => <String, dynamic>{};

  @override
  Future<Map<String, dynamic>> busDiagnostics(String pin) async =>
      <String, dynamic>{};

  @override
  Future<List<dynamic>> historyFiles() async => <dynamic>[];

  @override
  Future<void> setBrowserTime(int epochSeconds, String pin) async {}

  @override
  Future<Uint8List> download(
    String path, {
    Map<String, String>? queryParameters,
    int maximumBytes = 16 * 1024 * 1024,
  }) async => Uint8List(0);

  @override
  Future<ControllerActionResult> uploadFirmware(
    Uint8List firmware,
    String fileName,
    String pin, {
    void Function(int sent, int total)? onProgress,
  }) async {
    onProgress?.call(firmware.length, firmware.length);
    return const ControllerActionResult(
      success: true,
      code: 'ok',
      message: 'Wgrano.',
    );
  }

  @override
  Future<void> webSession(String sessionId, String state) async {}
}
