import 'dart:async';

import 'package:cyd_aquarium_mobile/connectivity/ble_controller_transport.dart';
import 'package:cyd_aquarium_mobile/connectivity/controller_transport.dart';
import 'package:cyd_aquarium_mobile/full_controller/ble_remote_api.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test(
    'remote API reconnects when the transport is no longer connected',
    () async {
      final transport = _FakeCommandTransport();
      final api = BleRemoteApi(transport);
      addTearDown(api.disconnect);

      await api.connect();
      transport.setState(ControllerTransportState.disconnected);
      await api.connect();

      expect(transport.connectCalls, 2);
      expect(transport.currentState, ControllerTransportState.connected);
    },
  );

  test(
    'serializes data requests that cannot be correlated by firmware',
    () async {
      final transport = _FakeCommandTransport();
      final api = BleRemoteApi(transport);
      addTearDown(api.disconnect);
      await api.connect();

      final first = api.logs('1234');
      final second = api.logs('1234');
      await Future<void>.delayed(Duration.zero);

      expect(transport.dataCommandCalls, 1);

      transport.emitMessage({
        'type': 'logs',
        'data': {'sequence': 1},
      });
      final firstResult = await first;
      expect(firstResult['sequence'], 1);

      await Future<void>.delayed(Duration.zero);
      expect(transport.dataCommandCalls, 2);

      transport.emitMessage({
        'type': 'logs',
        'data': {'sequence': 2},
      });
      final secondResult = await second;
      expect(secondResult['sequence'], 2);
    },
  );
}

class _FakeCommandTransport implements BleCommandTransport {
  final StreamController<ControllerTransportState> _states =
      StreamController<ControllerTransportState>.broadcast(sync: true);
  final StreamController<ControllerSnapshot> _snapshots =
      StreamController<ControllerSnapshot>.broadcast(sync: true);
  final StreamController<Map<String, dynamic>> _messages =
      StreamController<Map<String, dynamic>>.broadcast(sync: true);

  ControllerTransportState _state = ControllerTransportState.disconnected;
  int connectCalls = 0;
  int dataCommandCalls = 0;
  bool _disposed = false;

  @override
  String get displayName => 'Fake BLE';

  @override
  bool get isDeveloperTransport => false;

  @override
  ControllerTransportState get currentState => _state;

  @override
  Stream<ControllerTransportState> get stateChanges => _states.stream;

  @override
  Stream<ControllerSnapshot> get snapshots => _snapshots.stream;

  @override
  Stream<Map<String, dynamic>> get messages => _messages.stream;

  @override
  Future<void> connect() async {
    if (_disposed) {
      throw StateError('Transport zamknięty.');
    }
    connectCalls += 1;
    setState(ControllerTransportState.connected);
  }

  void setState(ControllerTransportState state) {
    _state = state;
    _states.add(state);
  }

  void emitMessage(Map<String, dynamic> message) {
    _messages.add(message);
  }

  @override
  Future<ControllerCommandResult> sendCommand(
    Map<String, dynamic> command,
  ) async {
    if (command['op'] == 'logs') {
      dataCommandCalls += 1;
    }
    return const ControllerCommandResult(
      success: true,
      code: 'ok',
      message: 'OK',
    );
  }

  @override
  Future<ControllerCommandResult> setOutput(
    OutputChannel channel,
    bool enabled,
    String pin,
  ) {
    return sendCommand({
      'op': 'set',
      'target': channel.protocolName,
      'state': enabled,
      'pin': pin,
    });
  }

  @override
  Future<ControllerCommandResult> feed(String pin) {
    return sendCommand({'op': 'feed', 'pin': pin});
  }

  @override
  Future<void> disconnect() async {
    setState(ControllerTransportState.disconnected);
  }

  @override
  Future<void> dispose() async {
    if (_disposed) {
      return;
    }
    _disposed = true;
    _state = ControllerTransportState.disconnected;
    await _states.close();
    await _snapshots.close();
    await _messages.close();
  }
}
