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

  test('lightweight polling preserves history from the initial sync', () async {
    final api = _FakeRemoteApi()
      ..omitHistoryFromLightweightStatus = true
      ..historySamples = <dynamic>[
        <String, dynamic>{'epoch': 100, 'value': 24.8},
        <String, dynamic>{'epoch': 200, 'value': 25.0},
      ];
    final session = ControllerSession.wifi(api);
    addTearDown(session.dispose);

    await session.connect();
    expect(session.status.section('temperature').list('history'), hasLength(2));

    await session.refresh(reportBusy: false);

    expect(session.status.section('temperature').list('history'), hasLength(2));
    expect(api.includeHistoryRequests, <bool>[true, false]);
  });

  test(
    'disabled automatic reconnect survives pause and resume while offline',
    () async {
      final api = _FakeRemoteApi()..supportsHeartbeat = true;
      final session = ControllerSession.wifi(api);
      addTearDown(session.dispose);

      await session.connect();
      session.setAutomaticReconnect(false);
      api.failStatus = true;
      await session.refresh(reportBusy: false);

      expect(session.connected, isFalse);
      final connectCallsAfterFailure = api.connectCalls;
      final statusCallsAfterFailure = api.statusCalls;
      final heartbeatCallsAfterFailure = api.heartbeatCalls;

      session.setAppActive(false);
      session.setAppActive(true);
      await Future<void>.delayed(Duration.zero);
      await Future<void>.delayed(Duration.zero);

      expect(api.connectCalls, connectCallsAfterFailure);
      expect(api.statusCalls, statusCallsAfterFailure);
      expect(api.heartbeatCalls, heartbeatCallsAfterFailure);
    },
  );

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

  test('protocol v2 uses capabilities, short session and command id', () async {
    final api = _FakeV2RemoteApi();
    final session = ControllerSession.wifi(api);
    addTearDown(session.dispose);

    await session.connect();
    expect(session.protocolVersion, 2);
    expect(session.supportsFeature('timedOverrides'), isTrue);

    await session.login('1234');
    await session.action(
      'set_timed_override',
      payload: const {'target': 'filter', 'state': false, 'durationSec': 900},
      refreshAfter: false,
    );

    expect(api.authCalls, 1);
    expect(api.legacyActionCalls, 0);
    expect(api.v2ActionCalls, 1);
    expect(api.lastAction, 'set_timed_override');
    expect(api.lastToken, '0123456789abcdef0123456789abcdef');
    expect(api.lastCommandId, matches(r'^[a-zA-Z0-9_-]{8,48}$'));
  });

  test(
    'protocol v2 keeps legacy relay actions on the legacy endpoint',
    () async {
      final api = _FakeV2RemoteApi();
      final session = ControllerSession.wifi(api);
      addTearDown(session.dispose);

      await session.connect();
      await session.login('1234');
      await session.action(
        'set_light1',
        payload: const {'state': true},
        refreshAfter: false,
      );

      expect(api.legacyActionCalls, 1);
      expect(api.v2ActionCalls, 0);
    },
  );
}

class _FakeRemoteApi implements ControllerRemoteApi {
  Completer<void>? connectGate;
  Completer<ControllerActionResult>? actionGate;
  bool failStatus = false;
  bool omitHistoryFromLightweightStatus = false;
  bool supportsHeartbeat = false;
  List<dynamic> historySamples = <dynamic>[];
  final List<bool> includeHistoryRequests = <bool>[];
  int connectCalls = 0;
  int statusCalls = 0;
  int actionCalls = 0;
  int heartbeatCalls = 0;

  @override
  Uri get baseUri => Uri.parse('http://192.168.4.1');

  @override
  bool get supportsFileDownload => true;

  @override
  bool get supportsFirmwareUpload => true;

  @override
  bool get supportsWebSession => supportsHeartbeat;

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
    includeHistoryRequests.add(includeHistory);
    if (failStatus) {
      throw const ControllerApiException(
        code: 'timeout',
        message: 'Sterownik nie odpowiada.',
      );
    }
    final temperature = <String, dynamic>{};
    if (includeHistory || !omitHistoryFromLightweightStatus) {
      temperature['history'] = List<dynamic>.of(historySamples);
    }
    return <String, dynamic>{
      'device': 'AquaCYD Test',
      'network': <String, dynamic>{'rssi': -61},
      'temperature': temperature,
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
  Future<void> webSession(String sessionId, String state) async {
    if (state == 'active') heartbeatCalls += 1;
  }
}

class _FakeV2RemoteApi extends _FakeRemoteApi
    implements ControllerProtocolV2Api {
  int authCalls = 0;
  int legacyActionCalls = 0;
  int v2ActionCalls = 0;
  String? lastAction;
  String? lastCommandId;
  String? lastToken;

  @override
  Future<Map<String, dynamic>> capabilities() async => {
    'apiVersions': [1, 2],
    'features': {
      'timedOverrides': true,
      'feedingMode': true,
      'serviceMode': true,
      'idempotency': true,
    },
  };

  @override
  Future<ControllerAdminSession> authenticateSession(String pin) async {
    authCalls += 1;
    return ControllerAdminSession(
      token: '0123456789abcdef0123456789abcdef',
      expiresAt: DateTime.now().add(const Duration(minutes: 5)),
    );
  }

  @override
  Future<ControllerActionResult> action(
    String action, {
    Map<String, Object?> payload = const {},
    String? pin,
    bool includePin = true,
  }) async {
    legacyActionCalls += 1;
    return super.action(
      action,
      payload: payload,
      pin: pin,
      includePin: includePin,
    );
  }

  @override
  Future<ControllerActionResult> actionV2(
    String action, {
    required String commandId,
    required String token,
    Map<String, Object?> payload = const {},
  }) async {
    v2ActionCalls += 1;
    lastAction = action;
    lastCommandId = commandId;
    lastToken = token;
    return const ControllerActionResult(
      success: true,
      code: 'ok',
      message: 'Wykonano bezpiecznie.',
    );
  }
}
