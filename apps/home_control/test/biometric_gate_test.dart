import 'package:aquacyd_home/src/home_control/biometric_gate.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('Windows Hello uses the system credential flow', () {
    expect(biometricOnlyForPlatform(TargetPlatform.windows), isFalse);
  });

  test('mobile and Apple platforms retain biometric-only protection', () {
    expect(biometricOnlyForPlatform(TargetPlatform.android), isTrue);
    expect(biometricOnlyForPlatform(TargetPlatform.iOS), isTrue);
    expect(biometricOnlyForPlatform(TargetPlatform.macOS), isTrue);
  });
}
