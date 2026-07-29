import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter_reactive_ble/flutter_reactive_ble.dart';
import 'package:permission_handler/permission_handler.dart';

import 'ble_protocol.dart';
import 'ble_security.dart';
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

/// Minimalna granica nad biblioteką BLE ułatwia testowanie cyklu połączenia
/// bez uruchamiania natywnego stosu Bluetooth.
abstract interface class BleCentral {
  Stream<ConnectionStateUpdate> connect({
    required String deviceId,
    required List<Uuid> services,
    required Duration prescanDuration,
    required Duration connectionTimeout,
  });

  Future<int> requestMtu({required String deviceId, required int mtu});

  Future<List<int>> read(QualifiedCharacteristic characteristic);

  Stream<List<int>> subscribe(QualifiedCharacteristic characteristic);

  Future<void> writeWithResponse(
    QualifiedCharacteristic characteristic, {
    required List<int> value,
  });
}

class ReactiveBleCentral implements BleCentral {
  ReactiveBleCentral(this._ble);

  final FlutterReactiveBle _ble;

  @override
  Stream<ConnectionStateUpdate> connect({
    required String deviceId,
    required List<Uuid> services,
    required Duration prescanDuration,
    required Duration connectionTimeout,
  }) {
    return _ble.connectToAdvertisingDevice(
      id: deviceId,
      withServices: services,
      prescanDuration: prescanDuration,
      connectionTimeout: connectionTimeout,
    );
  }

  @override
  Future<int> requestMtu({required String deviceId, required int mtu}) {
    return _ble.requestMtu(deviceId: deviceId, mtu: mtu);
  }

  @override
  Future<List<int>> read(QualifiedCharacteristic characteristic) {
    return _ble.readCharacteristic(characteristic);
  }

  @override
  Stream<List<int>> subscribe(QualifiedCharacteristic characteristic) {
    return _ble.subscribeToCharacteristic(characteristic);
  }

  @override
  Future<void> writeWithResponse(
    QualifiedCharacteristic characteristic, {
    required List<int> value,
  }) {
    return _ble.writeCharacteristicWithResponse(characteristic, value: value);
  }
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

abstract interface class BleCommandTransport implements ControllerTransport {
  Stream<Map<String, dynamic>> get messages;

  Future<ControllerCommandResult> sendCommand(Map<String, dynamic> command);
}

class BleControllerTransport implements BleCommandTransport {
  BleControllerTransport({
    required this.deviceId,
    required this.deviceName,
    BleCentral? central,
    Future<void> Function()? permissionRequester,
    BleBondRequester? bondRequester,
    this.connectionReadyDelay = const Duration(milliseconds: 250),
    this.commandTimeout = const Duration(seconds: 15),
    this.securityHandshakeTimeout = const Duration(seconds: 45),
    this.securityHandshakeRetryDelay = const Duration(milliseconds: 750),
  }) : _central =
           central ?? ReactiveBleCentral(BleControllerEnvironment.instance.ble),
       _permissionRequester =
           permissionRequester ??
           BleControllerEnvironment.instance.ensurePermissions,
       _bondRequester =
           bondRequester ?? PlatformBleBondCoordinator.ensureBonded,
       assert(securityHandshakeTimeout > Duration.zero),
       assert(securityHandshakeRetryDelay >= Duration.zero);

  final String deviceId;
  final String deviceName;
  final Duration connectionReadyDelay;
  final Duration commandTimeout;
  final Duration securityHandshakeTimeout;
  final Duration securityHandshakeRetryDelay;
  final BleCentral _central;
  final Future<void> Function() _permissionRequester;
  final BleBondRequester _bondRequester;
  final BleFrameAssembler _assembler = BleFrameAssembler();
  final StreamController<ControllerTransportState> _stateController =
      StreamController.broadcast();
  final StreamController<ControllerSnapshot> _snapshotController =
      StreamController.broadcast();
  final StreamController<Map<String, dynamic>> _messageController =
      StreamController.broadcast();
  final StreamController<BleLinkSecurityPhase> _securityController =
      StreamController.broadcast();
  final Map<int, Completer<ControllerCommandResult>> _pendingCommands = {};

  StreamSubscription<ConnectionStateUpdate>? _connectionSubscription;
  StreamSubscription<List<int>>? _notificationSubscription;
  QualifiedCharacteristic? _commandCharacteristic;
  QualifiedCharacteristic? _eventCharacteristic;
  QualifiedCharacteristic? _deviceInfoCharacteristic;
  Future<void>? _connectOperation;
  Future<void> _writeTail = Future<void>.value();
  Completer<void>? _connectionAttempt;
  ControllerTransportState _state = ControllerTransportState.disconnected;
  int _nextCommandId = 1;
  int _linkGeneration = 0;
  int _protocolErrorCount = 0;
  BleLinkSecurityPhase _securityPhase = BleLinkSecurityPhase.idle;
  BleSecurityException? _securityError;
  bool _disposed = false;

  @override
  String get displayName => deviceName.isEmpty ? 'AquaCYD BLE' : deviceName;

  @override
  bool get isDeveloperTransport => false;

  @override
  ControllerTransportState get currentState => _state;

  int get protocolErrorCount => _protocolErrorCount;

  BleLinkSecurityPhase get securityPhase => _securityPhase;

  BleSecurityException? get securityError => _securityError;

  Stream<BleLinkSecurityPhase> get securityChanges =>
      _securityController.stream;

  @override
  Stream<ControllerTransportState> get stateChanges => _stateController.stream;

  @override
  Stream<ControllerSnapshot> get snapshots => _snapshotController.stream;

  @override
  Stream<Map<String, dynamic>> get messages => _messageController.stream;

  @override
  Future<void> connect() {
    if (_disposed) {
      return Future<void>.error(StateError('Transport BLE został zamknięty.'));
    }
    if (_state == ControllerTransportState.connected) {
      return Future<void>.value();
    }
    final running = _connectOperation;
    if (running != null) {
      return running;
    }

    late final Future<void> operation;
    operation = _connectInternal().whenComplete(() {
      if (identical(_connectOperation, operation)) {
        _connectOperation = null;
      }
    });
    _connectOperation = operation;
    return operation;
  }

  Future<void> _connectInternal() async {
    final requestedGeneration = _linkGeneration;
    await _permissionRequester();
    if (_disposed) {
      throw StateError('Transport BLE został zamknięty.');
    }
    if (requestedGeneration != _linkGeneration) {
      throw StateError('Próba połączenia BLE została anulowana.');
    }

    await _clearLink(
      pendingMessage: 'Rozpoczęto ponowne łączenie BLE.',
      emitDisconnected: false,
    );
    final generation = _linkGeneration;
    _setState(ControllerTransportState.connecting);
    final connected = Completer<void>();
    _connectionAttempt = connected;
    final connectionStream = _central.connect(
      deviceId: deviceId,
      services: [AquariumBleProtocol.service],
      prescanDuration: const Duration(seconds: 3),
      connectionTimeout: const Duration(seconds: 12),
    );
    late final StreamSubscription<ConnectionStateUpdate> connectionSubscription;
    connectionSubscription = connectionStream.listen(
      (update) {
        if (!_isCurrentLink(generation, connectionSubscription)) {
          return;
        }
        switch (update.connectionState) {
          case DeviceConnectionState.connecting:
            _setState(ControllerTransportState.connecting);
          case DeviceConnectionState.connected:
            if (!connected.isCompleted) {
              connected.complete();
            }
          case DeviceConnectionState.disconnecting:
            _handleUnexpectedDisconnect(
              generation,
              connected,
              'Sterownik BLE rozpoczął rozłączanie.',
            );
          case DeviceConnectionState.disconnected:
            _handleUnexpectedDisconnect(
              generation,
              connected,
              'Sterownik BLE rozłączył się.',
            );
        }
      },
      onError: (Object error, StackTrace stackTrace) {
        if (!_isCurrentLink(generation, connectionSubscription)) {
          return;
        }
        _invalidateLink(
          generation,
          'Połączenie BLE zakończyło się błędem: $error',
          ControllerTransportState.error,
        );
        if (!connected.isCompleted) {
          connected.completeError(error, stackTrace);
        }
      },
    );
    _connectionSubscription = connectionSubscription;

    try {
      await connected.future.timeout(const Duration(seconds: 16));
      if (!_isCurrentLink(generation, connectionSubscription)) {
        throw StateError('Połączenie BLE zostało przerwane.');
      }
      if (_disposed || generation != _linkGeneration) {
        throw StateError('Połączenie BLE zostało przerwane.');
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
      _deviceInfoCharacteristic = QualifiedCharacteristic(
        serviceId: AquariumBleProtocol.service,
        characteristicId: AquariumBleProtocol.deviceInfo,
        deviceId: deviceId,
      );

      final securityClock = Stopwatch()..start();
      _setSecurityPhase(BleLinkSecurityPhase.pairing);
      final pairingTimeout = _remainingSecurityTime(securityClock);
      await _bondRequester(deviceId: deviceId, timeout: pairingTimeout).timeout(
        pairingTimeout,
        onTimeout: () {
          throw const BleSecurityException(
            code: BleSecurityFailureCode.pairingTimeout,
            message:
                'Upłynął czas bezpiecznego parowania Bluetooth. Spróbuj ponownie.',
          );
        },
      );
      _ensureCurrentLink(generation, connectionSubscription);
      _setSecurityPhase(BleLinkSecurityPhase.verifying);
      await _verifySecureDeviceInfo(
        generation: generation,
        connectionSubscription: connectionSubscription,
        securityClock: securityClock,
      );
      securityClock.stop();
      _ensureCurrentLink(generation, connectionSubscription);
      _setSecurityPhase(BleLinkSecurityPhase.secured);

      if (Platform.isAndroid) {
        try {
          await _central.requestMtu(deviceId: deviceId, mtu: 185);
        } on Exception {
          // Negocjacja MTU jest optymalizacją; ramkowanie działa również dla
          // wartości wybranej automatycznie przez system.
        }
        _ensureCurrentLink(generation, connectionSubscription);
      }

      late final StreamSubscription<List<int>> notificationSubscription;
      notificationSubscription = _central
          .subscribe(_eventCharacteristic!)
          .listen(
            (frame) {
              if (_isCurrentNotification(
                generation,
                notificationSubscription,
              )) {
                _handleNotification(frame);
              }
            },
            onError: (Object error) {
              if (!_isCurrentNotification(
                generation,
                notificationSubscription,
              )) {
                return;
              }
              _invalidateLink(
                generation,
                'Błąd powiadomień BLE: $error',
                ControllerTransportState.error,
              );
            },
          );
      _notificationSubscription = notificationSubscription;
      _setState(ControllerTransportState.connected);
      if (connectionReadyDelay > Duration.zero) {
        await Future<void>.delayed(connectionReadyDelay);
      }
      final result = await sendCommand({'op': 'status'});
      if (!result.success) {
        throw StateError(result.message);
      }
    } catch (error) {
      final securityError = error is BleSecurityException ? error : null;
      if (securityError != null) {
        _setSecurityFailure(securityError);
      }
      if (generation == _linkGeneration) {
        await _clearLink(
          pendingMessage: 'Nie udało się zakończyć łączenia BLE.',
          emitDisconnected: false,
          resetSecurity: securityError == null,
        );
        _setState(ControllerTransportState.error);
      }
      rethrow;
    } finally {
      if (identical(_connectionAttempt, connected)) {
        _connectionAttempt = null;
      }
    }
  }

  Duration _remainingSecurityTime(Stopwatch securityClock) {
    final remaining = securityHandshakeTimeout - securityClock.elapsed;
    if (remaining <= Duration.zero) {
      throw const BleSecurityException(
        code: BleSecurityFailureCode.pairingTimeout,
        message:
            'Upłynął czas bezpiecznego parowania Bluetooth. Spróbuj ponownie.',
      );
    }
    return remaining;
  }

  void _ensureCurrentLink(
    int generation,
    StreamSubscription<ConnectionStateUpdate> connectionSubscription,
  ) {
    if (!_isCurrentLink(generation, connectionSubscription)) {
      throw const BleSecurityException(
        code: BleSecurityFailureCode.linkClosed,
        message: 'Połączenie Bluetooth zostało przerwane podczas parowania.',
      );
    }
  }

  Future<void> _verifySecureDeviceInfo({
    required int generation,
    required StreamSubscription<ConnectionStateUpdate> connectionSubscription,
    required Stopwatch securityClock,
  }) async {
    final characteristic = _deviceInfoCharacteristic;
    if (characteristic == null) {
      throw const BleSecurityException(
        code: BleSecurityFailureCode.invalidDeviceInfo,
        message: 'Brak charakterystyki bezpieczeństwa sterownika Bluetooth.',
      );
    }

    Object? lastError;
    while (true) {
      _ensureCurrentLink(generation, connectionSubscription);
      final remaining = securityHandshakeTimeout - securityClock.elapsed;
      if (remaining <= Duration.zero) {
        throw BleSecurityException(
          code: BleSecurityFailureCode.handshakeTimeout,
          message:
              'Sterownik nie potwierdził bezpiecznego połączenia. Sprawdź kod parowania; jeśli sterownik był resetowany, usuń go z zapisanych urządzeń Bluetooth i sparuj ponownie.',
          cause: lastError,
        );
      }

      final operationTimeout = remaining < const Duration(seconds: 5)
          ? remaining
          : const Duration(seconds: 5);
      try {
        final bytes = await _central
            .read(characteristic)
            .timeout(operationTimeout);
        _ensureCurrentLink(generation, connectionSubscription);
        _validateSecureDeviceInfo(bytes);
        return;
      } on BleSecurityException {
        rethrow;
      } on Object catch (error) {
        lastError = error;
      }

      final afterAttempt = securityHandshakeTimeout - securityClock.elapsed;
      if (afterAttempt <= Duration.zero) {
        continue;
      }
      final retryDelay = afterAttempt < securityHandshakeRetryDelay
          ? afterAttempt
          : securityHandshakeRetryDelay;
      if (retryDelay > Duration.zero) {
        await Future<void>.delayed(retryDelay);
      }
    }
  }

  static void _validateSecureDeviceInfo(List<int> bytes) {
    if (bytes.isEmpty || bytes.length > 2048) {
      throw const BleSecurityException(
        code: BleSecurityFailureCode.invalidDeviceInfo,
        message:
            'Sterownik zwrócił nieprawidłowy opis bezpieczeństwa Bluetooth.',
      );
    }

    late final Object? decoded;
    try {
      decoded = jsonDecode(utf8.decode(bytes));
    } on Object catch (error) {
      throw BleSecurityException(
        code: BleSecurityFailureCode.invalidDeviceInfo,
        message:
            'Nie można zweryfikować opisu bezpieczeństwa sterownika Bluetooth.',
        cause: error,
      );
    }
    if (decoded is! Map) {
      throw const BleSecurityException(
        code: BleSecurityFailureCode.invalidDeviceInfo,
        message:
            'Opis bezpieczeństwa sterownika Bluetooth ma nieprawidłowy format.',
      );
    }

    final info = decoded.map((key, value) => MapEntry(key.toString(), value));
    final protocol = info['protocol'];
    if (protocol is! num || protocol.toInt() < 2) {
      throw const BleSecurityException(
        code: BleSecurityFailureCode.invalidDeviceInfo,
        message: 'Sterownik nie obsługuje bezpiecznego protokołu Bluetooth v2.',
      );
    }
    final rawSecurity = info['security'];
    if (rawSecurity is! Map) {
      throw const BleSecurityException(
        code: BleSecurityFailureCode.insecurePeripheral,
        message:
            'Firmware sterownika nie potwierdza szyfrowania Bluetooth. Zaktualizuj firmware przed sterowaniem przez BLE.',
      );
    }
    final security = rawSecurity.map(
      (key, value) => MapEntry(key.toString(), value),
    );
    final minimumKeySize = security['minimumKeySize'];
    if (security['linkEncryption'] != true ||
        security['bonding'] != true ||
        security['mitmProtection'] != true ||
        security['secureConnections'] != true ||
        minimumKeySize is! num ||
        minimumKeySize.toInt() < 16) {
      throw const BleSecurityException(
        code: BleSecurityFailureCode.insecurePeripheral,
        message:
            'Sterownik nie wymusza LE Secure Connections, bondingu, ochrony MITM i 128-bitowego klucza. Połączenie BLE zostało zablokowane.',
      );
    }
  }

  bool _isCurrentLink(
    int generation,
    StreamSubscription<ConnectionStateUpdate> subscription,
  ) {
    return !_disposed &&
        generation == _linkGeneration &&
        identical(_connectionSubscription, subscription);
  }

  bool _isCurrentNotification(
    int generation,
    StreamSubscription<List<int>> subscription,
  ) {
    return !_disposed &&
        generation == _linkGeneration &&
        identical(_notificationSubscription, subscription);
  }

  void _handleUnexpectedDisconnect(
    int generation,
    Completer<void> connected,
    String message,
  ) {
    if (generation != _linkGeneration) {
      return;
    }
    _invalidateLink(generation, message, ControllerTransportState.disconnected);
    if (!connected.isCompleted) {
      connected.completeError(StateError(message));
    }
  }

  void _invalidateLink(
    int generation,
    String message,
    ControllerTransportState nextState,
  ) {
    if (generation != _linkGeneration) {
      return;
    }
    _linkGeneration += 1;
    _failPending(message);
    _assembler.clear();
    _commandCharacteristic = null;
    _eventCharacteristic = null;
    _deviceInfoCharacteristic = null;
    final notificationSubscription = _notificationSubscription;
    _notificationSubscription = null;
    final connectionSubscription = _connectionSubscription;
    _connectionSubscription = null;
    if (notificationSubscription != null) {
      unawaited(_cancelSubscription(notificationSubscription));
    }
    if (connectionSubscription != null) {
      unawaited(_cancelSubscription(connectionSubscription));
    }
    _setSecurityPhase(BleLinkSecurityPhase.idle);
    _setState(nextState);
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
      if (!_messageController.isClosed) {
        _messageController.add(Map<String, dynamic>.unmodifiable(decoded));
      }
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
    } on FormatException {
      // Pojedyncza uszkodzona ramka nie może zrywać zdrowego linku ani kończyć
      // niezależnych komend. Telemetria błędów umożliwia diagnostykę.
      _protocolErrorCount += 1;
    } on Object {
      _protocolErrorCount += 1;
    }
  }

  @override
  Future<ControllerCommandResult> sendCommand(
    Map<String, dynamic> command,
  ) async {
    final characteristic = _commandCharacteristic;
    if (_state != ControllerTransportState.connected ||
        characteristic == null ||
        _disposed) {
      return const ControllerCommandResult(
        success: false,
        code: 'not_connected',
        message: 'Brak połączenia ze sterownikiem BLE.',
      );
    }
    final id = _allocateCommandId();
    final payload = <String, dynamic>{'id': id, ...command};
    final bytes = utf8.encode(jsonEncode(payload));
    if (bytes.length > 4096) {
      return const ControllerCommandResult(
        success: false,
        code: 'command_too_large',
        message: 'Komenda przekracza limit 4096 bajtów protokołu BLE.',
      );
    }

    final completer = Completer<ControllerCommandResult>();
    _pendingCommands[id] = completer;
    try {
      await _writeSerialized(characteristic, id, bytes);
      return await completer.future.timeout(
        commandTimeout,
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

  int _allocateCommandId() {
    for (var attempts = 0; attempts < 65535; attempts++) {
      final candidate = _nextCommandId;
      _nextCommandId = candidate >= 65535 ? 1 : candidate + 1;
      if (!_pendingCommands.containsKey(candidate)) {
        return candidate;
      }
    }
    throw StateError('Brak wolnych identyfikatorów komend BLE.');
  }

  Future<void> _writeSerialized(
    QualifiedCharacteristic characteristic,
    int id,
    List<int> bytes,
  ) async {
    final previous = _writeTail;
    final release = Completer<void>();
    _writeTail = release.future;
    await previous;
    try {
      if (_disposed ||
          _state != ControllerTransportState.connected ||
          !identical(_commandCharacteristic, characteristic)) {
        throw StateError('Połączenie BLE zostało zamknięte.');
      }
      if (bytes.length <= 160) {
        await _central.writeWithResponse(characteristic, value: bytes);
        return;
      }
      final frames = encodeBleFrames(
        bytes,
        messageId: id,
        maximumPayloadBytes: 156,
        maximumPartCount: 32,
      );
      for (final frame in frames) {
        if (_disposed ||
            _state != ControllerTransportState.connected ||
            !identical(_commandCharacteristic, characteristic)) {
          throw StateError('Połączenie BLE zostało zamknięte.');
        }
        await _central.writeWithResponse(characteristic, value: frame);
        await Future<void>.delayed(const Duration(milliseconds: 12));
      }
    } finally {
      release.complete();
    }
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
    if (_state == state) {
      return;
    }
    _state = state;
    if (!_stateController.isClosed) {
      _stateController.add(state);
    }
  }

  void _setSecurityPhase(BleLinkSecurityPhase phase) {
    if (phase != BleLinkSecurityPhase.failed) {
      _securityError = null;
    }
    if (_securityPhase == phase) {
      return;
    }
    _securityPhase = phase;
    if (!_securityController.isClosed) {
      _securityController.add(phase);
    }
  }

  void _setSecurityFailure(BleSecurityException error) {
    _securityError = error;
    if (_securityPhase == BleLinkSecurityPhase.failed) {
      return;
    }
    _securityPhase = BleLinkSecurityPhase.failed;
    if (!_securityController.isClosed) {
      _securityController.add(BleLinkSecurityPhase.failed);
    }
  }

  @override
  Future<void> disconnect() {
    return _clearLink(pendingMessage: 'Połączenie BLE zostało zamknięte.');
  }

  Future<void> _clearLink({
    required String pendingMessage,
    bool emitDisconnected = true,
    bool resetSecurity = true,
  }) async {
    _linkGeneration += 1;
    _failPending(pendingMessage);
    _assembler.clear();
    final connectionAttempt = _connectionAttempt;
    _connectionAttempt = null;
    if (connectionAttempt != null && !connectionAttempt.isCompleted) {
      connectionAttempt.completeError(StateError(pendingMessage));
    }

    final notificationSubscription = _notificationSubscription;
    _notificationSubscription = null;
    final connectionSubscription = _connectionSubscription;
    _connectionSubscription = null;
    _commandCharacteristic = null;
    _eventCharacteristic = null;
    _deviceInfoCharacteristic = null;

    await _cancelSubscription(notificationSubscription);
    await _cancelSubscription(connectionSubscription);
    if (resetSecurity) {
      _setSecurityPhase(BleLinkSecurityPhase.idle);
    }
    if (emitDisconnected && !_disposed) {
      _setState(ControllerTransportState.disconnected);
    } else {
      _state = ControllerTransportState.disconnected;
    }
  }

  static Future<void> _cancelSubscription<T>(
    StreamSubscription<T>? subscription,
  ) async {
    if (subscription == null) {
      return;
    }
    try {
      await subscription.cancel();
    } on Object {
      // Czyszczenie musi być idempotentne także po awarii natywnego kanału.
    }
  }

  @override
  Future<void> dispose() async {
    if (_disposed) {
      return;
    }
    _disposed = true;
    await _clearLink(
      pendingMessage: 'Transport BLE został zamknięty.',
      emitDisconnected: false,
    );
    await _stateController.close();
    await _snapshotController.close();
    await _messageController.close();
    await _securityController.close();
  }
}
