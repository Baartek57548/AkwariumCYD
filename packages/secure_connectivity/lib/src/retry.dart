import 'dart:math' as math;

final class RetryPolicy {
  RetryPolicy({
    this.maximumAttempts = 6,
    this.initialDelay = const Duration(milliseconds: 500),
    this.maximumDelay = const Duration(seconds: 30),
    this.jitterRatio = 0.2,
    math.Random? random,
  }) : _random = random ?? math.Random() {
    if (maximumAttempts < 1 ||
        initialDelay <= Duration.zero ||
        maximumDelay < initialDelay ||
        jitterRatio < 0 ||
        jitterRatio > 1) {
      throw ArgumentError('Invalid retry policy.');
    }
  }

  final int maximumAttempts;
  final Duration initialDelay;
  final Duration maximumDelay;
  final double jitterRatio;
  final math.Random _random;

  Duration delayForAttempt(int attempt) {
    if (attempt < 0) throw RangeError.range(attempt, 0, maximumAttempts - 1);
    final multiplier = math.pow(2, attempt).toInt();
    final baseMs = math.min(
      initialDelay.inMilliseconds * multiplier,
      maximumDelay.inMilliseconds,
    );
    final jitter = (baseMs * jitterRatio).round();
    final offset = jitter == 0 ? 0 : _random.nextInt(jitter * 2 + 1) - jitter;
    return Duration(milliseconds: math.max(1, baseMs + offset));
  }
}
