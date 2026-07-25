import 'dart:async';
import 'dart:convert';

import 'package:cyd_aquarium_mobile/connectivity/ble_controller_transport.dart';
import 'package:cyd_aquarium_mobile/connectivity/ble_protocol.dart';
import 'package:cyd_aquarium_mobile/connectivity/controller_transport.dart';
import 'package:flutter_reactive_ble/flutter_reactive_ble.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('coalesces concurrent BLE connection attempts', () async {
    final central = _FakeBleCentral();
    final permissionGate = Completer<void>();
    final transport = BleControllerTransport(
      deviceId: 'controller-1',
      deviceName: 'AquaCYD',
      central: central,
      permissionRequester: () => permissionGate.future,
      connectionReadyDelay: Duration.zero,
      commandTimeout: const Duration(seconds: 1),
    );
    addTearDown(() async {
      await transport.dispose();
      await central.dispose();
    });

    final first = transport.connect();
    final second = transport.connect();
    expect(identical(first, second), isTrue);

    permissionGate.complete();
    await Future.wait([first, second]);

    expect(central.connectionCalls, 1);
    expect(transport.currentState, ControllerTransportState.connected);
  });

  test('connects again after a spontaneous disconnect', () async {
    final central = _FakeBleCentral();
    final transport = BleControllerTransport(
      deviceId: 'controller-2',
      deviceName: 'AquaCYD',
      central: central,
      permissionRequester: () async {},
      connectionReadyDelay: Duration.zero,
      commandTimeout: const Duration(seconds: 1),
    );
    addTearDown(() async {
      await transport.dispose();
      await central.dispose();
    });

    await transport.connect();
    central.emitConnectionState(DeviceConnectionState.disconnected);
    await Future<void>.delayed(Duration.zero);

    expect(transport.currentState, ControllerTransportState.disconnected);

    await transport.connect();

    expect(central.connectionCalls, 2);
    expect(transport.currentState, ControllerTransportState.connected);
  });

  test('drops a malformed packet without failing an active command', () async {
    final central = _FakeBleCentral();
    final transport = BleControllerTransport(
      deviceId: 'controller-3',
      deviceName: 'AquaCYD',
      central: central,
      permissionRequester: () async {},
      connectionReadyDelay: Duration.zero,
      commandTimeout: const Duration(seconds: 1),
    );
    addTearDown(() async {
      await transport.dispose();
      await central.dispose();
    });

    await transport.connect();
    central.autoRespond = false;
    final command = transport.sendCommand({'op': 'set', 'state': true});
    await Future<void>.delayed(Duration.zero);
    final commandId = central.lastCommandId;
    expect(commandId, isNotNull);

    central.emitNotification(const [1, 2, 3]);
    central.emitJson({
      'type': 'response',
      'id': commandId,
      'ok': true,
      'code': 'ok',
      'message': 'Zapisano.',
    });

    final result = await command;
    expect(result.success, isTrue);
    expect(transport.protocolErrorCount, 1);
    expect(transport.currentState, ControllerTransportState.connected);
  });
}

class _FakeBleCentral implements BleCentral {
  final List<StreamController<ConnectionStateUpdate>> _connections = [];
  final StreamController<List<int>> _notifications =
      StreamController<List<int>>.broadcast(sync: true);

  int connectionCalls = 0;
  int? lastCommandId;
  bool autoRespond = true;

  @override
  Stream<ConnectionStateUpdate> connect({
    required String deviceId,
    required List<Uuid> services,
    required Duration prescanDuration,
    required Duration connectionTimeout,
  }) {
    connectionCalls += 1;
    final controller = StreamController<ConnectionStateUpdate>.broadcast(
      sync: true,
    );
    _connections.add(controller);
    scheduleMicrotask(() {
      if (!controller.isClosed) {
        controller.add(
          ConnectionStateUpdate(
            deviceId: deviceId,
            connectionState: DeviceConnectionState.connected,
            failure: null,
          ),
        );
      }
    });
    return controller.stream;
  }

  void emitConnectionState(DeviceConnectionState state) {
    final controller = _connections.last;
    controller.add(
      ConnectionStateUpdate(
        deviceId: 'controller',
        connectionState: state,
        failure: null,
      ),
    );
  }

  void emitNotification(List<int> frame) {
    _notifications.add(frame);
  }

  void emitJson(Map<String, dynamic> message) {
    final payload = utf8.encode(jsonEncode(message));
    final frames = encodeBleFrames(
      payload,
      messageId: (message['id'] as int?) ?? 1,
    );
    for (final frame in frames) {
      _notifications.add(frame);
    }
  }

  @override
  Future<int> requestMtu({required String deviceId, required int mtu}) async {
    return mtu;
  }

  @override
  Stream<List<int>> subscribe(QualifiedCharacteristic characteristic) {
    return _notifications.stream;
  }

  @override
  Future<void> writeWithResponse(
    QualifiedCharacteristic characteristic, {
    required List<int> value,
  }) async {
    final command = jsonDecode(utf8.decode(value)) as Map<String, dynamic>;
    final commandId = command['id'] as int;
    lastCommandId = commandId;
    if (autoRespond) {
      scheduleMicrotask(() {
        emitJson({
          'type': 'response',
          'id': commandId,
          'ok': true,
          'code': 'ok',
          'message': 'OK',
        });
      });
    }
  }

  Future<void> dispose() async {
    for (final connection in _connections) {
      await connection.close();
    }
    await _notifications.close();
  }
}
