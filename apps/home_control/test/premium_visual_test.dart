import 'package:aquacyd_home/src/aquahub/app_update.dart';
import 'package:aquacyd_home/src/aquahub/credentials_store.dart';
import 'package:aquacyd_home/src/aquahub/domain.dart';
import 'package:aquacyd_home/src/data/credentials_store.dart';
import 'package:aquacyd_home/src/domain/models.dart';
import 'package:aquacyd_home/src/home_control/app.dart';
import 'package:aquacyd_home/src/home_control/biometric_gate.dart';
import 'package:aquacyd_home/src/home_control/preferences.dart';
import 'package:aquacyd_home/src/home_control/snapshot_cache.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:home_entities/home_entities.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'package:shared_preferences_platform_interface/in_memory_shared_preferences_async.dart';
import 'package:shared_preferences_platform_interface/shared_preferences_async_platform_interface.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  setUp(() {
    SharedPreferencesAsyncPlatform.instance =
        InMemorySharedPreferencesAsync.empty();
  });

  for (final configuration in <({String name, Size size, ThemeMode mode})>[
    (name: 'mobile', size: const Size(393, 852), mode: ThemeMode.light),
    (name: 'panel', size: const Size(800, 480), mode: ThemeMode.light),
    (name: 'mobile_dark', size: const Size(393, 852), mode: ThemeMode.dark),
    (name: 'panel_dark', size: const Size(800, 480), mode: ThemeMode.dark),
  ]) {
    testWidgets('captures ${configuration.name} dashboard', (tester) async {
      tester.view.physicalSize = configuration.size;
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.resetPhysicalSize);
      addTearDown(tester.view.resetDevicePixelRatio);

      final preferences = HomeControlPreferences(
        storage: SharedPreferencesAsync(),
        fallbackLocale: const Locale('pl'),
      );
      await preferences.saveLocale(const Locale('pl'));
      await preferences.saveThemeMode(configuration.mode);
      const boundaryKey = ValueKey<String>('visual-capture');
      await tester.pumpWidget(
        RepaintBoundary(
          key: boundaryKey,
          child: _testApp(preferences: preferences),
        ),
      );
      await tester.pumpAndSettle();
      await tester.ensureVisible(find.text('Demo offline'));
      await tester.tap(find.text('Demo offline'));
      await tester.pumpAndSettle();

      await expectLater(
        find.byKey(boundaryKey),
        matchesGoldenFile(
          'goldens/dashboard_premium_${configuration.name}.png',
        ),
      );
    });
  }
}

Widget _testApp({required HomeControlPreferences preferences}) =>
    HomeControlApp(
      preferences: preferences,
      hubCredentialsStore: _MemoryHubCredentialsStore(),
      homeAssistantCredentialsStore: _MemoryHaCredentialsStore(),
      snapshotCache: _MemorySnapshotCache(),
      appUpdateService: const UnsupportedAppUpdateService(),
      biometricAuthenticator: _UnavailableBiometricAuthenticator(),
      enablePolling: false,
    );

final class _MemoryHubCredentialsStore implements HubCredentialsStore {
  HubCredentials? value;

  @override
  Future<void> clear() async => value = null;

  @override
  Future<HubCredentials?> load() async => value;

  @override
  Future<void> save(HubCredentials credentials) async => value = credentials;
}

final class _MemoryHaCredentialsStore implements CredentialsStore {
  HomeAssistantCredentials? value;

  @override
  Future<void> clear() async => value = null;

  @override
  Future<HomeAssistantCredentials?> load() async => value;

  @override
  Future<void> save(HomeAssistantCredentials credentials) async {
    value = credentials;
  }
}

final class _MemorySnapshotCache implements HomeSnapshotCache {
  final Map<String, HomeSnapshot> values = <String, HomeSnapshot>{};

  @override
  Future<void> clear(HomeSourceKind kind, {String? sourceId}) async {
    if (sourceId == null) {
      values.removeWhere((key, _) => key.startsWith('${kind.name}:'));
    } else {
      values.remove('${kind.name}:$sourceId');
    }
  }

  @override
  Future<HomeSnapshot?> load(HomeSourceKind kind, String sourceId) async =>
      values['${kind.name}:$sourceId'];

  @override
  Future<void> save(HomeSnapshot snapshot) async {
    values['${snapshot.sourceKind.name}:${snapshot.sourceId}'] = snapshot;
  }
}

final class _UnavailableBiometricAuthenticator
    implements BiometricAuthenticator {
  @override
  Future<BiometricAuthorization> authenticate({
    required String localizedReason,
  }) async => BiometricAuthorization.unavailable;

  @override
  Future<BiometricAvailability> availability() async =>
      BiometricAvailability.unavailable;
}
