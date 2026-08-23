import 'dart:math' as math;

import 'package:secure_connectivity/secure_connectivity.dart';
import 'package:test/test.dart';

void main() {
  test('redactor removes bearer tokens and URL secrets', () {
    final value = LogRedactor.redact(
      'Authorization: Bearer abc.def?x=1&token=sensitive',
    );
    expect(value, isNot(contains('abc.def')));
    expect(value, isNot(contains('sensitive')));
  });

  test('retry policy is bounded and deterministic with injected random', () {
    final policy = RetryPolicy(
      maximumAttempts: 4,
      initialDelay: const Duration(seconds: 1),
      maximumDelay: const Duration(seconds: 4),
      random: math.Random(7),
    );
    expect(
      policy.delayForAttempt(3),
      lessThanOrEqualTo(const Duration(seconds: 5)),
    );
  });
}
