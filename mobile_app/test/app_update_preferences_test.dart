import 'package:cyd_aquarium_mobile/app_update/app_update_models.dart';
import 'package:cyd_aquarium_mobile/app_update/app_update_preferences.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  setUp(() {
    SharedPreferences.setMockInitialValues({});
  });

  test('automatic check is throttled by the recorded UTC timestamp', () async {
    final preferences = await AppUpdatePreferences.load();
    final now = DateTime.utc(2026, 7, 24, 12);

    expect(
      preferences.shouldRunAutomaticCheck(now, const Duration(hours: 12)),
      isTrue,
    );
    await preferences.recordCheck(now);
    expect(
      preferences.shouldRunAutomaticCheck(
        now.add(const Duration(hours: 11)),
        const Duration(hours: 12),
      ),
      isFalse,
    );
    expect(
      preferences.shouldRunAutomaticCheck(
        now.add(const Duration(hours: 12)),
        const Duration(hours: 12),
      ),
      isTrue,
    );
  });

  test('skip applies only to the selected version', () async {
    final preferences = await AppUpdatePreferences.load();
    const skipped = SemanticVersion(3, 6, 0);

    await preferences.skip(skipped);

    expect(preferences.isSkipped(skipped), isTrue);
    expect(preferences.isSkipped(const SemanticVersion(3, 6, 1)), isFalse);
  });

  test('remind later expires and never suppresses a newer version', () async {
    final preferences = await AppUpdatePreferences.load();
    const deferred = SemanticVersion(3, 6, 0);
    final now = DateTime.utc(2026, 7, 24, 12);

    await preferences.remindLater(deferred, now, const Duration(hours: 24));

    expect(
      preferences.isDeferred(deferred, now.add(const Duration(hours: 23))),
      isTrue,
    );
    expect(
      preferences.isDeferred(deferred, now.add(const Duration(hours: 24))),
      isFalse,
    );
    expect(
      preferences.isDeferred(
        const SemanticVersion(3, 6, 1),
        now.add(const Duration(hours: 1)),
      ),
      isFalse,
    );
  });
}
