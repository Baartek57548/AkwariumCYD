import 'package:aquacyd_home/src/home_control/preferences.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'package:shared_preferences_platform_interface/in_memory_shared_preferences_async.dart';
import 'package:shared_preferences_platform_interface/shared_preferences_async_platform_interface.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  setUp(() {
    SharedPreferencesAsyncPlatform.instance =
        InMemorySharedPreferencesAsync.empty();
  });

  test('uses a supported system locale when no preference is stored', () async {
    final english = HomeControlPreferences(
      storage: SharedPreferencesAsync(),
      fallbackLocale: const Locale('en', 'GB'),
    );
    expect(await english.loadLocale(), const Locale('en'));

    final unsupported = HomeControlPreferences(
      storage: SharedPreferencesAsync(),
      fallbackLocale: const Locale('de', 'DE'),
    );
    expect(await unsupported.loadLocale(), const Locale('pl'));
  });

  test('repairs duplicate and incomplete dashboard order', () async {
    final storage = SharedPreferencesAsync();
    await storage.setStringList('home_control_dashboard_order', <String>[
      'favorites',
      'favorites',
      'invalid-section',
      'aquarium',
    ]);
    final preferences = HomeControlPreferences(
      storage: storage,
      fallbackLocale: const Locale('pl'),
    );

    final dashboard = await preferences.loadDashboard();

    expect(dashboard.order, <String>[
      'favorites',
      'aquarium',
      'areas',
      'activity',
    ]);
    expect(dashboard.order.toSet(), hasLength(dashboard.order.length));
  });
}
