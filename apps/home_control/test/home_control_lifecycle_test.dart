import 'package:aquacyd_home/src/home_control/app.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('Windows keeps polling while a visible window is inactive', () {
    expect(
      homeControlLifecycleIsActive(
        AppLifecycleState.inactive,
        platform: TargetPlatform.windows,
        isWeb: false,
      ),
      isTrue,
    );
    expect(
      homeControlLifecycleIsActive(
        AppLifecycleState.hidden,
        platform: TargetPlatform.windows,
        isWeb: false,
      ),
      isFalse,
    );
  });

  test('mobile and web only poll while resumed', () {
    expect(
      homeControlLifecycleIsActive(
        AppLifecycleState.inactive,
        platform: TargetPlatform.android,
        isWeb: false,
      ),
      isFalse,
    );
    expect(
      homeControlLifecycleIsActive(
        AppLifecycleState.inactive,
        platform: TargetPlatform.windows,
        isWeb: true,
      ),
      isFalse,
    );
  });
}
