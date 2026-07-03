import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter_reactive_ble/flutter_reactive_ble.dart';
import 'package:permission_handler/permission_handler.dart';

import 'ble_protocol.dart';
import 'controller_transport.dart';

abstract final class AquariumBleProtocol {
  static final Uuid service = Uuid.parse(
    '7c4a0001-6e8d-4f84-9f3f-2c3a0a0c0001',
  );
  static final Uuid command = Uuid.parse(
    '7c4a0002-6e8d-4f84-9f3f-2c3a0a0c0001',
  );
  static final Uuid events = Uuid.parse('7c4a0003-6e8d-4f84-9f3f-2c3a0a0c0001');
  static final Uuid deviceInfo = Uuid.parse(
    '7c4a0004-6e8d-4f84-9f3f-2c3a0a0c0001',
  );
}

class BleControllerEnvironment {
  BleControllerEnvironment._();

  static final BleControllerEnvironment instance = BleControllerEnvironment._();
  final FlutterReactiveBle ble = FlutterReactiveBle();

  Future<void> ensurePermissions() async {
    if (!Platform.isAndroid) {
      final status = await Permission.bluetooth.request();
      if (!status.isGranted && !status.isLimited) {
        throw StateError('Brak uprawnienia Bluetooth.');
      }
      return;
    }

    final statuses = await <Permission>[
      Permission.bluetoothScan,
      Permission.bluetoothConnect,
    ].request();
    final denied = statuses.values.any(
      (status) => !status.isGranted && !status.isLimited,
    );
    if (denied) {
      throw StateError('Brak uprawnień do skanowania i łączenia Bluetooth.');
    }

    final sdkMatch = RegExp(
      r'SDK\s+(\d+)',
      caseSensitive: false,
    ).firstMatch(Platform.operatingSystemVersion);
    final sdkVersion = int.tryParse(sdkMatch?.group(1) ?? '');
    if (sdkVersion != null && sdkVersion <= 30) {
      final locationStatus = await Permission.locationWhenInUse.request();
      if (!locationStatus.isGranted && !locationStatus.isLimited) {
        throw StateError(
          'Android 11 lub starszy wymaga uprawnienia lokalizacji do skanowania BLE.',
        );
      }
    }
  }

  Stream<DiscoveredDevice> scanForControllers() {
    return ble.scanForDevices(
      withServices: [AquariumBleProtocol.service],
      scanMode: ScanMode.lowLatency,
    );
  }
}

class BleControllerTransport implements ControllerTransport {
  BleControllerTransport({required this.deviceId, required this.deviceName});

  final String deviceId;
  final String deviceName;
  final FlutterReactiveBle _ble = BleControllerEnvironment.instance.ble;
  final BleFrameAssembler _assembler = BleFrameAssembler();
  final StreamController<ControllerTransportState> _stateController =
      StreamController.broadcast();
  final StreamController<ControllerSnapshot> _snapshotController =
      StreamController.broadcast();
  final Map<int, Completer<ControllerCommandResult>> _pendingCommands = {};

  StreamSubscription<ConnectionStateUpdate>? _connectionSubscription;
  StreamSubscription<List<int>>? _notificationSubscription;
  QualifiedCharacteristic? _commandCharacteristic;
  QualifiedCharacteristic? _eventCharacteristic;
  ControllerTransportState _state = ControllerTransportState.disconnected;
  int _nextCommandId = 1;
  bool _disposed = false;

  @override
  String get displayName => deviceName.isEmpty ? 'cydAkwarium BLE' : deviceName;

  @override
  bool get isDeveloperTransport => false;

  @override
  ControllerTransportState get currentState => _state;

  @override
  Stream<ControllerTransportState> get stateChanges => _stateController.stream;

  @override
  Stream<ControllerSnapshot> get snapshots => _snapshotController.stream;

  @override
  Future<void> connect() async {
    if (_disposed) {
      throw StateError('Transport BLE został zamknięty.');
    }
    if (_state == ControllerTransportState.connected) {
      return;
    }
    await BleControllerEnvironment.instance.ensurePermissions();
    _setState(ControllerTransportState.connecting);
    final connected = Completer<void>();
    final connectionStream = _ble.connectToAdvertisingDevice(
      id: deviceId,
      withServices: [AquariumBleProtocol.service],
      prescanDuration: const Duration(seconds: 3),
      connectionTimeout: const Duration(seconds: 12),
    );
    _connectionSubscription = connectionStream.listen(
      (update) {
        switch (update.connectionState) {
          case DeviceConnectionState.connecting:
            _setState(ControllerTransportState.connecting);
          case DeviceConnectionState.connected:
            _setState(ControllerTransportState.connected);
            if (!connected.isCompleted) {
              connected.complete();
            }
          case DeviceConnectionState.disconnecting:
            _setState(ControllerTransportState.disconnected);
          case DeviceConnectionState.disconnected:
            _setState(ControllerTransportState.disconnected);
            if (!connected.isCompleted) {
              connected.completeError(
                StateError('Sterownik BLE rozłączył się podczas łączenia.'),
              );
            }
        }
      },
      onError: (Object error, StackTrace stackTrace) {
        _setState(ControllerTransportState.error);
        if (!connected.isCompleted) {
          connected.completeError(error, stackTrace);
        }
      },
    );

    try {
      await connected.future.timeout(const Duration(seconds: 16));
      if (Platform.isAndroid) {
        try {
          await _ble.requestMtu(deviceId: deviceId, mtu: 185);
        } on Exception {
          // MTU negotiation is an optimization; framed messages also work at
          // the MTU selected by the operating system.
        }
      }
      _commandCharacteristic = QualifiedCharacteristic(
        serviceId: AquariumBleProtocol.service,
        characteristicId: AquariumBleProtocol.command,
        deviceId: deviceId,
      );
      _eventCharacteristic = QualifiedCharacteristic(
        serviceId: AquariumBleProtocol.service,
        characteristicId: AquariumBleProtocol.events,
        deviceId: deviceId,
      );
      _notificationSubscription = _ble
          .subscribeToCharacteristic(_eventCharacteristic!)
          .listen(
            _handleNotification,
            onError: (Object error) {
              _failPending('Błąd powiadomień BLE: $error');
              _setState(ControllerTransportState.error);
            },
          );
      await Future<void>.delayed(const Duration(milliseconds: 250));
      final result = await _sendCommand({'op': 'status'});
      if (!result.success) {
        throw StateError(result.message);
      }
    } catch (_) {
      await disconnect();
      rethrow;
    }
  }

  void _handleNotification(List<int> frame) {
    try {
      final message = _assembler.add(frame);
      if (message == null) {
        return;
      }
      final decoded = jsonDecode(message);
      if (decoded is! Map<String, dynamic>) {
        throw const FormatException('Wiadomość BLE nie jest obiektem JSON.');
      }
      final type = decoded['type'];
      if (type == 'status') {
        final snapshot = ControllerSnapshot.fromJson(decoded);
        if (!_snapshotController.isClosed) {
          _snapshotController.add(snapshot);
        }
        return;
      }
      if (type == 'response') {
        final id = decoded['id'];
        if (id is! int) {
          throw const FormatException(
            'Odpowiedź BLE nie zawiera identyfikatora.',
          );
        }
        final completer = _pendingCommands.remove(id);
        if (completer != null && !completer.isCompleted) {
          completer.complete(
            ControllerCommandResult(
              success: decoded['ok'] == true,
              code: decoded['code'] is String
                  ? decoded['code'] as String
                  : 'invalid_response',
              message: decoded['message'] is String
                  ? decoded['message'] as String
                  : 'Sterownik nie zwrócił opisu odpowiedzi.',
            ),
          );
        }
      }
    } catch (error) {
      _failPending('Nieprawidłowa wiadomość BLE: $error');
      _setState(ControllerTransportState.error);
    }
  }

  Future<ControllerCommandResult> _sendCommand(
    Map<String, dynamic> command,
  ) async {
    final characteristic = _commandCharacteristic;
    if (_state != ControllerTransportState.connected ||
        characteristic == null) {
      return const ControllerCommandResult(
        success: false,
        code: 'not_connected',
        message: 'Brak połączenia ze sterownikiem BLE.',
      );
    }
    final id = _nextCommandId;
    _nextCommandId = _nextCommandId >= 65535 ? 1 : _nextCommandId + 1;
    final payload = <String, dynamic>{'id': id, ...command};
    final bytes = utf8.encode(jsonEncode(payload));
    if (bytes.length > 160) {
      return const ControllerCommandResult(
        success: false,
        code: 'command_too_large',
        message: 'Komenda przekracza limit pojedynczej ramki BLE.',
      );
    }

    final completer = Completer<ControllerCommandResult>();
    _pendingCommands[id] = completer;
    try {
      await _ble.writeCharacteristicWithResponse(characteristic, value: bytes);
      return await completer.future.timeout(
        const Duration(seconds: 8),
        onTimeout: () {
          _pendingCommands.remove(id);
          return const ControllerCommandResult(
            success: false,
            code: 'timeout',
            message: 'Sterownik nie odpowiedział na komendę BLE.',
          );
        },
      );
    } catch (error) {
      _pendingCommands.remove(id);
      return ControllerCommandResult(
        success: false,
        code: 'write_failed',
        message: 'Nie udało się wysłać komendy BLE: $error',
      );
    }
  }

  @override
  Future<ControllerCommandResult> setOutput(
    OutputChannel channel,
    bool enabled,
    String pin,
  ) {
    return _sendCommand({
      'op': 'set',
      'target': channel.protocolName,
      'state': enabled,
      'pin': pin,
    });
  }

  @override
  Future<ControllerCommandResult> feed(String pin) {
    return _sendCommand({'op': 'feed', 'pin': pin});
  }

  void _failPending(String message) {
    for (final completer in _pendingCommands.values) {
      if (!completer.isCompleted) {
        completer.complete(
          ControllerCommandResult(
            success: false,
            code: 'transport_error',
            message: message,
          ),
        );
      }
    }
    _pendingCommands.clear();
  }

  void _setState(ControllerTransportState state) {
    _state = state;
    if (!_stateController.isClosed) {
      _stateController.add(state);
    }
  }

  @override
  Future<void> disconnect() async {
    _failPending('Połączenie BLE zostało zamknięte.');
    _assembler.clear();
    await _notificationSubscription?.cancel();
    _notificationSubscription = null;
    await _connectionSubscription?.cancel();
    _connectionSubscription = null;
    _commandCharacteristic = null;
    _eventCharacteristic = null;
    _setState(ControllerTransportState.disconnected);
  }

  @override
  Future<void> dispose() async {
    if (_disposed) {
      return;
    }
    _disposed = true;
    await disconnect();
    await _stateController.close();
    await _snapshotController.close();
  }
}
