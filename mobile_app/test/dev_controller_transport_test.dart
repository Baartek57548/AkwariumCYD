import 'package:cyd_aquarium_mobile/connectivity/controller_transport.dart';
import 'package:cyd_aquarium_mobile/connectivity/dev_controller_transport.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test(
    'DEV transport emits telemetry and simulates authenticated output',
    () async {
      final transport = DevControllerTransport();
      final firstSnapshot = transport.snapshots.first;

      await transport.connect();
      final initial = await firstSnapshot;
      expect(initial.developerMode, isTrue);
      expect(initial.temperatureValid, isTrue);

      final rejected = await transport.setOutput(
        OutputChannel.light,
        false,
        '0000',
      );
      expect(rejected.success, isFalse);
      expect(rejected.code, 'pin_invalid');

      final changedSnapshot = transport.snapshots.first;
      final accepted = await transport.setOutput(
        OutputChannel.light,
        false,
        '1234',
      );
      expect(accepted.success, isTrue);
      expect((await changedSnapshot).outputs[OutputChannel.light], isFalse);

      await transport.dispose();
    },
  );
}
