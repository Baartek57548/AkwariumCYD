import 'package:cyd_aquarium_mobile/full_controller/connection_health.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('connection health maps RSSI to stable quality levels', () {
    const excellent = ControllerConnectionHealth(
      phase: ControllerConnectionPhase.online,
      failedAttempts: 0,
      rssi: -48,
    );
    const good = ControllerConnectionHealth(
      phase: ControllerConnectionPhase.online,
      failedAttempts: 0,
      rssi: -63,
    );
    const fair = ControllerConnectionHealth(
      phase: ControllerConnectionPhase.online,
      failedAttempts: 0,
      rssi: -72,
    );
    const weak = ControllerConnectionHealth(
      phase: ControllerConnectionPhase.online,
      failedAttempts: 0,
      rssi: -90,
    );

    expect(excellent.signalBars, 4);
    expect(excellent.signalLabel, 'Bardzo dobry');
    expect(good.signalBars, 3);
    expect(fair.signalBars, 2);
    expect(weak.signalBars, 1);
  });

  test('connection health reports a human-readable synchronization age', () {
    final now = DateTime(2026, 7, 25, 12);

    expect(
      ControllerConnectionHealth(
        phase: ControllerConnectionPhase.online,
        failedAttempts: 0,
        lastSync: now.subtract(const Duration(seconds: 28)),
      ).ageLabel(now),
      '28 s temu',
    );
    expect(
      ControllerConnectionHealth(
        phase: ControllerConnectionPhase.reconnecting,
        failedAttempts: 1,
        lastSync: now.subtract(const Duration(minutes: 4)),
      ).ageLabel(now),
      '4 min temu',
    );
    expect(
      const ControllerConnectionHealth(
        phase: ControllerConnectionPhase.connecting,
        failedAttempts: 0,
      ).ageLabel(now),
      'Oczekiwanie',
    );
  });
}
