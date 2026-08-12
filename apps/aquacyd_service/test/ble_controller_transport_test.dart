import 'dart:async';
import 'dart:convert';

import 'package:cyd_aquarium_mobile/connectivity/ble_controller_transport.dart';
import 'package:cyd_aquarium_mobile/connectivity/ble_protocol.dart';
import 'package:cyd_aquarium_mobile/connectivity/ble_security.dart';
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

  test(
    'finishes pairing and secure device-info handshake before commands',
    () async {
      final central = _FakeBleCentral();
      final bondGate = Completer<void>();
      final transport = BleControllerTransport(
        deviceId: 'controller-secure',
        deviceName: 'AquaCYD',
        central: central,
        permissionRequester: () async {},
        bondRequester: ({required deviceId, required timeout}) async {
          central.operations.add('bond');
          await bondGate.future;
        },
        connectionReadyDelay: Duration.zero,
        commandTimeout: const Duration(seconds: 1),
      );
      final phases = <BleLinkSecurityPhase>[];
      final phaseSubscription = transport.securityChanges.listen(phases.add);
      addTearDown(() async {
        await phaseSubscription.cancel();
        await transport.dispose();
        await central.dispose();
      });

      final connection = transport.connect();
      await Future<void>.delayed(Duration.zero);
      await Future<void>.delayed(Duration.zero);

      expect(transport.currentState, ControllerTransportState.connecting);
      expect(transport.securityPhase, BleLinkSecurityPhase.pairing);
      expect(central.readCalls, 0);
      expect(central.writes, 0);

      bondGate.complete();
      await connection;

      expect(transport.securityPhase, BleLinkSecurityPhase.secured);
      expect(
        phases,
        containsAllInOrder([
          BleLinkSecurityPhase.pairing,
          BleLinkSecurityPhase.verifying,
          BleLinkSecurityPhase.secured,
        ]),
      );
      expect(
        central.operations,
        orderedEquals(['connect', 'bond', 'read', 'subscribe', 'write']),
      );
    },
  );

  test(
    'retries protected device-info read while system pairing completes',
    () async {
      final central = _FakeBleCentral()..readFailuresRemaining = 2;
      final transport = BleControllerTransport(
        deviceId: 'controller-retry',
        deviceName: 'AquaCYD',
        central: central,
        permissionRequester: () async {},
        bondRequester: ({required deviceId, required timeout}) async {},
        connectionReadyDelay: Duration.zero,
        commandTimeout: const Duration(seconds: 1),
        securityHandshakeTimeout: const Duration(seconds: 1),
        securityHandshakeRetryDelay: Duration.zero,
      );
      addTearDown(() async {
        await transport.dispose();
        await central.dispose();
      });

      await transport.connect();

      expect(central.readCalls, 3);
      expect(transport.securityPhase, BleLinkSecurityPhase.secured);
      expect(transport.currentState, ControllerTransportState.connected);
    },
  );

  test(
    'blocks firmware that does not enforce encryption, bonding and MITM',
    () async {
      final central = _FakeBleCentral()
        ..deviceInfo = _deviceInfo(
          linkEncryption: true,
          bonding: true,
          mitmProtection: false,
        );
      final transport = BleControllerTransport(
        deviceId: 'controller-insecure',
        deviceName: 'AquaCYD',
        central: central,
        permissionRequester: () async {},
        bondRequester: ({required deviceId, required timeout}) async {},
        connectionReadyDelay: Duration.zero,
        commandTimeout: const Duration(seconds: 1),
      );
      addTearDown(() async {
        await transport.dispose();
        await central.dispose();
      });

      await expectLater(
        transport.connect(),
        throwsA(
          isA<BleSecurityException>().having(
            (error) => error.code,
            'code',
            BleSecurityFailureCode.insecurePeripheral,
          ),
        ),
      );

      expect(transport.currentState, ControllerTransportState.error);
      expect(transport.securityPhase, BleLinkSecurityPhase.failed);
      expect(central.writes, 0);
    },
  );

  test(
    'surfaces pairing rejection without reading or writing GATT data',
    () async {
      final central = _FakeBleCentral();
      final transport = BleControllerTransport(
        deviceId: 'controller-rejected',
        deviceName: 'AquaCYD',
        central: central,
        permissionRequester: () async {},
        bondRequester: ({required deviceId, required timeout}) async {
          throw const BleSecurityException(
            code: BleSecurityFailureCode.pairingRejected,
            message: 'Parowanie odrzucone.',
          );
        },
        connectionReadyDelay: Duration.zero,
        commandTimeout: const Duration(seconds: 1),
      );
      addTearDown(() async {
        await transport.dispose();
        await central.dispose();
      });

      await expectLater(
        transport.connect(),
        throwsA(
          isA<BleSecurityException>().having(
            (error) => error.protocolCode,
            'protocolCode',
            'ble_pairing_rejected',
          ),
        ),
      );

      expect(central.readCalls, 0);
      expect(central.writes, 0);
      expect(transport.securityPhase, BleLinkSecurityPhase.failed);
    },
  );

  test('bounds a pairing request that never completes', () async {
    final central = _FakeBleCentral();
    final stalledPairing = Completer<void>();
    final transport = BleControllerTransport(
      deviceId: 'controller-pairing-timeout',
      deviceName: 'AquaCYD',
      central: central,
      permissionRequester: () async {},
      bondRequester: ({required deviceId, required timeout}) {
        return stalledPairing.future;
      },
      connectionReadyDelay: Duration.zero,
      commandTimeout: const Duration(seconds: 1),
      securityHandshakeTimeout: const Duration(milliseconds: 30),
      securityHandshakeRetryDelay: Duration.zero,
    );
    addTearDown(() async {
      await transport.dispose();
      await central.dispose();
    });

    await expectLater(
      transport.connect(),
      throwsA(
        isA<BleSecurityException>().having(
          (error) => error.code,
          'code',
          BleSecurityFailureCode.pairingTimeout,
        ),
      ),
    );

    expect(central.readCalls, 0);
    expect(central.writes, 0);
    expect(transport.currentState, ControllerTransportState.error);
    expect(transport.securityPhase, BleLinkSecurityPhase.failed);
  });

  test(
    'ends a stalled protected-read handshake with a bounded timeout',
    () async {
      final central = _FakeBleCentral()..readAlwaysFails = true;
      final transport = BleControllerTransport(
        deviceId: 'controller-timeout',
        deviceName: 'AquaCYD',
        central: central,
        permissionRequester: () async {},
        bondRequester: ({required deviceId, required timeout}) async {},
        connectionReadyDelay: Duration.zero,
        commandTimeout: const Duration(seconds: 1),
        securityHandshakeTimeout: const Duration(milliseconds: 40),
        securityHandshakeRetryDelay: const Duration(milliseconds: 2),
      );
      addTearDown(() async {
        await transport.dispose();
        await central.dispose();
      });

      await expectLater(
        transport.connect(),
        throwsA(
          isA<BleSecurityException>().having(
            (error) => error.code,
            'code',
            BleSecurityFailureCode.handshakeTimeout,
          ),
        ),
      );

      expect(central.readCalls, greaterThan(1));
      expect(transport.currentState, ControllerTransportState.error);
      expect(central.writes, 0);
    },
  );

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
  int readCalls = 0;
  int writes = 0;
  int readFailuresRemaining = 0;
  bool autoRespond = true;
  bool readAlwaysFails = false;
  List<int> deviceInfo = _deviceInfo();
  final List<String> operations = [];

  @override
  Stream<ConnectionStateUpdate> connect({
    required String deviceId,
    required List<Uuid> services,
    required Duration prescanDuration,
    required Duration connectionTimeout,
  }) {
    connectionCalls += 1;
    operations.add('connect');
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
  Future<List<int>> read(QualifiedCharacteristic characteristic) async {
    operations.add('read');
    readCalls += 1;
    if (readAlwaysFails || readFailuresRemaining > 0) {
      if (readFailuresRemaining > 0) {
        readFailuresRemaining -= 1;
      }
      throw StateError('GATT authentication is still pending.');
    }
    return List<int>.of(deviceInfo);
  }

  @override
  Stream<List<int>> subscribe(QualifiedCharacteristic characteristic) {
    operations.add('subscribe');
    return _notifications.stream;
  }

  @override
  Future<void> writeWithResponse(
    QualifiedCharacteristic characteristic, {
    required List<int> value,
  }) async {
    operations.add('write');
    writes += 1;
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

List<int> _deviceInfo({
  bool linkEncryption = true,
  bool bonding = true,
  bool mitmProtection = true,
}) {
  return utf8.encode(
    jsonEncode({
      'name': 'cydAkwarium',
      'firmwareVersion': '5.1.0',
      'protocol': 2,
      'security': {
        'linkEncryption': linkEncryption,
        'bonding': bonding,
        'mitmProtection': mitmProtection,
        'secureConnections': true,
        'minimumKeySize': 16,
      },
    }),
  );
}
