import 'dart:async';
import 'dart:math' as math;

import 'controller_transport.dart';

class DevControllerTransport implements ControllerTransport {
  DevControllerTransport();

  final StreamController<ControllerTransportState> _stateController =
      StreamController.broadcast();
  final StreamController<ControllerSnapshot> _snapshotController =
      StreamController.broadcast();
  Timer? _timer;
  Timer? _feedingTimer;
  ControllerTransportState _state = ControllerTransportState.disconnected;
  int _tick = 0;
  bool _feeding = false;
  late ControllerSnapshot _snapshot;

  @override
  String get displayName => 'Symulator DEV RAM';

  @override
  bool get isDeveloperTransport => true;

  @override
  ControllerTransportState get currentState => _state;

  @override
  Stream<ControllerTransportState> get stateChanges => _stateController.stream;

  @override
  Stream<ControllerSnapshot> get snapshots => _snapshotController.stream;

  @override
  Future<void> connect() async {
    if (_state == ControllerTransportState.connected) {
      return;
    }
    _setState(ControllerTransportState.connecting);
    await Future<void>.delayed(const Duration(milliseconds: 350));
    _snapshot = ControllerSnapshot(
      protocolVersion: 1,
      developerMode: true,
      uptimeSeconds: 0,
      freeHeapBytes: 93440,
      temperature: 25.3,
      temperatureValid: true,
      targetTemperature: 26,
      ph: 6.9,
      phValid: true,
      ec: 442,
      ecValid: true,
      ldr: 918,
      ldrValid: true,
      alarmFlags: 0,
      waterLevelHigh: true,
      leakDetected: false,
      outputs: const {
        OutputChannel.light: true,
        OutputChannel.plantLight: true,
        OutputChannel.filter: true,
        OutputChannel.heater: false,
        OutputChannel.aeration: false,
      },
    );
    _setState(ControllerTransportState.connected);
    _emitSnapshot();
    _timer = Timer.periodic(const Duration(seconds: 1), (_) => _advance());
  }

  void _advance() {
    _tick++;
    final phase = _tick / 8;
    final heaterOn = _snapshot.outputs[OutputChannel.heater] ?? false;
    final temperature =
        25.3 + math.sin(phase) * 0.22 + (heaterOn ? 0.03 : -0.01);
    final ph = 6.9 + math.sin(phase * 0.55) * 0.05;
    final ec = 442 + math.sin(phase * 0.3) * 14;
    final ldr = 918 + (math.sin(phase * 0.8) * 210).round();
    _snapshot = _snapshot.copyWith(
      uptimeSeconds: _snapshot.uptimeSeconds + 1,
      freeHeapBytes: 93440 - (_tick % 128) * 4,
      temperature: temperature,
      ph: ph,
      ec: ec,
      ldr: ldr,
    );
    _emitSnapshot();
  }

  void _emitSnapshot() {
    if (!_snapshotController.isClosed) {
      _snapshotController.add(_snapshot);
    }
  }

  @override
  Future<ControllerCommandResult> setOutput(
    OutputChannel channel,
    bool enabled,
    String pin,
  ) async {
    final rejected = _validateCommand(pin);
    if (rejected != null) {
      return rejected;
    }
    await Future<void>.delayed(const Duration(milliseconds: 180));
    final outputs = Map<OutputChannel, bool>.from(_snapshot.outputs);
    outputs[channel] = enabled;
    _snapshot = _snapshot.copyWith(outputs: outputs);
    _emitSnapshot();
    return ControllerCommandResult(
      success: true,
      code: 'dev_simulated',
      message: '${channel.label}: ${enabled ? 'ON' : 'OFF'} — symulacja DEV.',
    );
  }

  @override
  Future<ControllerCommandResult> feed(String pin) async {
    final rejected = _validateCommand(pin);
    if (rejected != null) {
      return rejected;
    }
    if (_feeding) {
      return const ControllerCommandResult(
        success: false,
        code: 'feed_busy',
        message: 'Symulowana dawka jest już wykonywana.',
      );
    }
    _feeding = true;
    _feedingTimer?.cancel();
    _feedingTimer = Timer(const Duration(seconds: 2), () => _feeding = false);
    return const ControllerCommandResult(
      success: true,
      code: 'dev_simulated',
      message: 'Karmienie zasymulowane w pamięci RAM.',
    );
  }

  ControllerCommandResult? _validateCommand(String pin) {
    if (_state != ControllerTransportState.connected) {
      return const ControllerCommandResult(
        success: false,
        code: 'not_connected',
        message: 'Transport DEV nie jest uruchomiony.',
      );
    }
    if (pin != '1234') {
      return const ControllerCommandResult(
        success: false,
        code: 'pin_invalid',
        message: 'Nieprawidłowy PIN administratora.',
      );
    }
    return null;
  }

  void _setState(ControllerTransportState value) {
    _state = value;
    if (!_stateController.isClosed) {
      _stateController.add(value);
    }
  }

  @override
  Future<void> disconnect() async {
    _timer?.cancel();
    _timer = null;
    _feedingTimer?.cancel();
    _feedingTimer = null;
    _feeding = false;
    _setState(ControllerTransportState.disconnected);
  }

  @override
  Future<void> dispose() async {
    await disconnect();
    await _stateController.close();
    await _snapshotController.close();
  }
}
