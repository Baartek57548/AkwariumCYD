import 'dart:async';
import 'dart:typed_data';

import '../connectivity/ble_controller_transport.dart';
import '../connectivity/controller_transport.dart';
import 'controller_api.dart';
import 'data_access.dart';

class BleRemoteApi implements ControllerRemoteApi, ControllerProtocolV2Api {
  BleRemoteApi(this.transport);

  final BleCommandTransport transport;
  ControllerSnapshot? _latestSnapshot;
  StreamSubscription<ControllerSnapshot>? _snapshotSubscription;
  Future<void>? _connectOperation;
  Future<void> _dataRequestTail = Future<void>.value();
  bool _protocolV2 = true;
  bool _disposed = false;

  @override
  Uri? get baseUri => null;

  @override
  bool get supportsFirmwareUpload => false;

  @override
  bool get supportsFileDownload => false;

  @override
  bool get supportsWebSession => false;

  @override
  Future<void> connect() {
    if (_disposed) {
      return Future<void>.error(StateError('Interfejs BLE został zamknięty.'));
    }
    _snapshotSubscription ??= transport.snapshots.listen((snapshot) {
      _latestSnapshot = snapshot;
    });
    if (transport.currentState == ControllerTransportState.connected) {
      return Future<void>.value();
    }
    final running = _connectOperation;
    if (running != null) {
      return running;
    }

    late final Future<void> operation;
    operation = transport.connect().whenComplete(() {
      if (identical(_connectOperation, operation)) {
        _connectOperation = null;
      }
    });
    _connectOperation = operation;
    return operation;
  }

  @override
  Future<void> disconnect() async {
    if (_disposed) {
      return;
    }
    _disposed = true;
    await _snapshotSubscription?.cancel();
    _snapshotSubscription = null;
    await transport.dispose();
  }

  @override
  Future<JsonMap> status({bool includeHistory = false}) async {
    if (_protocolV2) {
      try {
        final message = await _requestData(
          expectedType: 'full_status',
          command: {'op': 'full_status', 'history': includeHistory},
        );
        final data = jsonMap(message['data']);
        if (data.isEmpty) {
          throw const ControllerApiException(
            code: 'invalid_ble_status',
            message: 'Sterownik BLE zwrócił pusty status.',
          );
        }
        return data;
      } on ControllerApiException catch (error) {
        if (error.code != 'unknown_operation') rethrow;
        _protocolV2 = false;
      }
    }
    final result = await transport.sendCommand({'op': 'status'});
    _ensureSuccess(result);
    final snapshot = _latestSnapshot;
    if (snapshot == null) {
      throw const ControllerApiException(
        code: 'status_unavailable',
        message: 'Sterownik nie przesłał jeszcze telemetrii BLE.',
      );
    }
    return _legacyStatus(snapshot);
  }

  @override
  Future<JsonMap> capabilities() async {
    _requireV2('Odczyt możliwości sterownika przez BLE');
    final message = await _requestData(
      expectedType: 'capabilities',
      command: const {'v': 2, 'op': 'capabilities'},
    );
    final data = jsonMap(message['data']);
    return data.isNotEmpty ? data : message;
  }

  @override
  Future<JsonMap> logs(String pin) async {
    _requireV2('Logi przez BLE');
    final message = await _requestData(
      expectedType: 'logs',
      command: {'op': 'logs', 'pin': pin},
    );
    return jsonMap(message['data']);
  }

  @override
  Future<JsonMap> busDiagnostics(String pin) async {
    _requireV2('Diagnostyka przez BLE');
    final message = await _requestData(
      expectedType: 'diagnostics',
      command: {'op': 'diagnostics', 'pin': pin},
      timeout: const Duration(seconds: 20),
    );
    return jsonMap(message['data']);
  }

  @override
  Future<List<dynamic>> historyFiles() async => const [];

  @override
  Future<ControllerActionResult> authenticate(String pin) async {
    _requireV2('Logowanie administratora przez BLE');
    final result = await transport.sendCommand({'op': 'auth', 'pin': pin});
    return _actionResult(result);
  }

  @override
  Future<ControllerAdminSession> authenticateSession(String pin) async {
    _requireV2('Bezpieczna sesja administratora przez BLE');
    final message = await _requestData(
      expectedType: 'auth',
      command: {'v': 2, 'op': 'auth', 'pin': pin},
    );
    final ok = message['ok'];
    if (ok == false) {
      throw ControllerApiException(
        code: message['code']?.toString() ?? 'authentication_failed',
        message:
            message['message']?.toString() ??
            'Sterownik odrzucił sesję administratora.',
      );
    }
    final data = jsonMap(message['data']);
    return ControllerAdminSession.fromJson(data);
  }

  @override
  Future<ControllerActionResult> action(
    String action, {
    Map<String, Object?> payload = const {},
    String? pin,
    bool includePin = true,
  }) async {
    final effectivePin = includePin ? pin : null;
    final legacyCommand = _legacyAction(action, payload, effectivePin);
    if (legacyCommand != null) {
      final result = await transport.sendCommand(legacyCommand);
      return _actionResult(result);
    }
    _requireV2('Akcja $action przez BLE');
    final result = await transport.sendCommand({
      'op': 'action',
      'name': action,
      'args': payload,
      'pin': ?effectivePin,
    });
    return _actionResult(result);
  }

  @override
  Future<ControllerActionResult> actionV2(
    String action, {
    required String commandId,
    required String token,
    Map<String, Object?> payload = const {},
  }) async {
    _requireV2('Akcja $action przez BLE');
    if (!RegExp(r'^[a-zA-Z0-9_-]{8,48}$').hasMatch(commandId)) {
      throw const ControllerApiException(
        code: 'invalid_command_id',
        message: 'Identyfikator polecenia ma nieprawidłowy format.',
      );
    }
    if (!RegExp(r'^[a-fA-F0-9]{32}$').hasMatch(token)) {
      throw const ControllerApiException(
        code: 'invalid_session_token',
        message: 'Token sesji administratora ma nieprawidłowy format.',
      );
    }
    final result = await transport.sendCommand({
      'v': 2,
      'op': 'action',
      'name': action,
      'commandId': commandId,
      'token': token.toLowerCase(),
      'args': payload,
    });
    return _actionResult(result);
  }

  @override
  Future<void> setBrowserTime(int epochSeconds, String pin) async {
    _requireV2('Synchronizacja czasu przez BLE');
    final result = await transport.sendCommand({
      'op': 'action',
      'name': 'set_time',
      'args': {'epoch': epochSeconds},
      'pin': pin,
    });
    _ensureSuccess(result);
  }

  @override
  Future<Uint8List> download(
    String path, {
    Map<String, String>? queryParameters,
    int maximumBytes = 64 * 1024 * 1024,
  }) {
    throw const ControllerApiException(
      code: 'transport_unsupported',
      message: 'Pobieranie plików wymaga połączenia Wi-Fi.',
    );
  }

  @override
  Future<ControllerActionResult> uploadFirmware(
    Uint8List firmware,
    String fileName,
    String pin, {
    void Function(int sent, int total)? onProgress,
  }) {
    throw const ControllerApiException(
      code: 'transport_unsupported',
      message: 'Aktualizacja firmware wymaga połączenia Wi-Fi.',
    );
  }

  @override
  Future<void> webSession(String sessionId, String state) async {}

  Future<JsonMap> _requestData({
    required String expectedType,
    required Map<String, dynamic> command,
    Duration timeout = const Duration(seconds: 15),
  }) async {
    final previous = _dataRequestTail;
    final release = Completer<void>();
    _dataRequestTail = release.future;
    await previous;
    try {
      return await _requestDataUnlocked(
        expectedType: expectedType,
        command: command,
        timeout: timeout,
      );
    } finally {
      release.complete();
    }
  }

  Future<JsonMap> _requestDataUnlocked({
    required String expectedType,
    required Map<String, dynamic> command,
    required Duration timeout,
  }) async {
    final data = Completer<JsonMap>();
    late final StreamSubscription<Map<String, dynamic>> subscription;
    subscription = transport.messages.listen(
      (message) {
        if (message['type'] == expectedType && !data.isCompleted) {
          data.complete(message);
        }
      },
      onError: (Object error, StackTrace stackTrace) {
        if (!data.isCompleted) {
          data.completeError(error, stackTrace);
        }
      },
      onDone: () {
        if (!data.isCompleted) {
          data.completeError(
            StateError('Strumień wiadomości BLE został zamknięty.'),
          );
        }
      },
    );
    try {
      try {
        final result = await transport.sendCommand(command);
        _ensureSuccess(result);
        try {
          return await data.future.timeout(timeout);
        } on TimeoutException {
          throw ControllerApiException(
            code: 'ble_data_timeout',
            message: 'Sterownik nie przesłał danych $expectedType przez BLE.',
          );
        }
      } on ControllerApiException {
        rethrow;
      } on Object catch (error) {
        throw ControllerApiException(
          code: 'ble_stream_error',
          message: 'Połączenie BLE przerwało odbiór danych: $error',
        );
      }
    } finally {
      await subscription.cancel();
    }
  }

  void _requireV2(String operation) {
    if (!_protocolV2) {
      throw ControllerApiException(
        code: 'firmware_update_required',
        message: '$operation wymaga firmware z protokołem BLE v2.',
      );
    }
  }

  static Map<String, dynamic>? _legacyAction(
    String action,
    Map<String, Object?> payload,
    String? pin,
  ) {
    final target = switch (action) {
      'set_light' => 'light',
      'set_light1' => 'light1',
      'set_plant' => 'plant',
      'set_light2' => 'light2',
      'set_filter' => 'filter',
      'set_heater' => 'heater',
      'set_aeration' => 'aeration',
      _ => null,
    };
    if (target != null) {
      return {
        'op': 'set',
        'target': target,
        'state': payload['state'] == true || payload['state'] == 1,
        'pin': ?pin,
      };
    }
    if (action == 'feed_now') {
      return {'op': 'feed', 'pin': ?pin};
    }
    return null;
  }

  static ControllerActionResult _actionResult(ControllerCommandResult result) {
    _ensureSuccess(result);
    return ControllerActionResult(
      success: true,
      code: result.code,
      message: result.message,
    );
  }

  static void _ensureSuccess(ControllerCommandResult result) {
    if (!result.success) {
      throw ControllerApiException(code: result.code, message: result.message);
    }
  }

  static JsonMap _legacyStatus(ControllerSnapshot snapshot) => {
    'device': 'cydAkwarium',
    'mode': 'BLE_V1',
    'heap_free': snapshot.freeHeapBytes,
    'uptime_ms': snapshot.uptimeSeconds * 1000,
    'sd_mounted': false,
    'sensors': {
      'temp_c': snapshot.temperatureValid ? snapshot.temperature : null,
      'temp_valid': snapshot.temperatureValid,
      'ph': snapshot.phValid ? snapshot.ph : null,
      'ph_valid': snapshot.phValid,
      'ec': snapshot.ecValid ? snapshot.ec : null,
      'ec_valid': snapshot.ecValid,
      'ldr': snapshot.ldrValid ? snapshot.ldr : null,
      'ldr_valid': snapshot.ldrValid,
      'mcp_present': true,
      'mcp_valid': true,
      'mcp_ok': true,
      'water_level_high': snapshot.waterLevelHigh,
      'water_level_valid': true,
      'leak_detected': snapshot.leakDetected,
      'leak_valid': true,
      'flow_active': false,
      'flow_valid': false,
    },
    'alarms': {
      'flags': snapshot.alarmFlags,
      'activeCount': _bitCount(snapshot.alarmFlags),
      'temperatureHigh': snapshot.alarmFlags & 1 != 0,
      'temperatureLow': snapshot.alarmFlags & 2 != 0,
      'phOutOfRange': snapshot.alarmFlags & 4 != 0,
      'waterLevelLow': snapshot.alarmFlags & 8 != 0,
      'leak': snapshot.alarmFlags & 16 != 0,
      'supplyLow': snapshot.alarmFlags & 32 != 0,
    },
    'config': {
      'target_temp': snapshot.targetTemperature,
      'temp_hysteresis': 0.5,
      'dev_mode': snapshot.developerMode,
    },
    'modules': {
      'light_on': snapshot.outputs[OutputChannel.light] ?? false,
      'plant_light_on': snapshot.outputs[OutputChannel.plantLight] ?? false,
      'light1_on': snapshot.outputs[OutputChannel.light] ?? false,
      'light2_on': snapshot.outputs[OutputChannel.plantLight] ?? false,
      'filter_on': snapshot.outputs[OutputChannel.filter] ?? false,
      'heater_on': snapshot.outputs[OutputChannel.heater] ?? false,
      'air_on': snapshot.outputs[OutputChannel.aeration] ?? false,
    },
    'temperature': {
      'current': snapshot.temperature,
      'target': snapshot.targetTemperature,
      'hysteresis': 0.5,
      'history': <dynamic>[],
    },
    'system': {
      'uptime': snapshot.uptimeSeconds,
      'freeHeap': snapshot.freeHeapBytes,
      'largestHeap': 0,
      'powerMode': 'unknown',
    },
    'feeding': {'active': false, 'freq': 0, 'lastResult': 'unknown'},
    'relays': {
      'light': snapshot.outputs[OutputChannel.light] ?? false,
      'plantLight': snapshot.outputs[OutputChannel.plantLight] ?? false,
      'light1': snapshot.outputs[OutputChannel.light] ?? false,
      'light2': snapshot.outputs[OutputChannel.plantLight] ?? false,
      'pump': snapshot.outputs[OutputChannel.filter] ?? false,
      'heater': snapshot.outputs[OutputChannel.heater] ?? false,
      'aeration': snapshot.outputs[OutputChannel.aeration] ?? false,
    },
  };

  static int _bitCount(int value) {
    var count = 0;
    var remaining = value;
    while (remaining != 0) {
      count += remaining & 1;
      remaining >>= 1;
    }
    return count;
  }
}
