import 'package:cyd_aquarium_mobile/display_refresh_rate.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('selects a standard animation profile for 60 Hz', () {
    final profile = DisplayRefreshProfile.fromRefreshRate(60);

    expect(profile.tier, DisplayRefreshTier.standard);
    expect(profile.roundedHertz, 60);
    expect(profile.frameBudgetMilliseconds, closeTo(16.666, 0.01));
    expect(profile.transitionDuration, const Duration(milliseconds: 240));
  });

  test('selects a high refresh profile for 120 Hz', () {
    final profile = DisplayRefreshProfile.fromRefreshRate(120);

    expect(profile.tier, DisplayRefreshTier.high);
    expect(profile.roundedHertz, 120);
    expect(profile.frameBudgetMilliseconds, closeTo(8.333, 0.01));
    expect(profile.transitionDuration, const Duration(milliseconds: 200));
  });

  test('selects an ultra refresh profile for 144 Hz', () {
    final profile = DisplayRefreshProfile.fromRefreshRate(144);

    expect(profile.tier, DisplayRefreshTier.ultra);
    expect(profile.roundedHertz, 144);
    expect(profile.frameBudgetMilliseconds, closeTo(6.944, 0.01));
    expect(profile.transitionDuration, const Duration(milliseconds: 180));
  });

  test('uses a safe 60 Hz fallback for invalid display data', () {
    final profile = DisplayRefreshProfile.fromRefreshRate(double.nan);

    expect(profile.tier, DisplayRefreshTier.standard);
    expect(profile.roundedHertz, 60);
  });
}
