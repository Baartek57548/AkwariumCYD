import 'package:aquacyd_home/src/aquahub/app_update.dart';
import 'package:aquacyd_home/src/aquahub/credentials_store.dart';
import 'package:aquacyd_home/src/aquahub/domain.dart';
import 'package:aquacyd_home/src/data/credentials_store.dart';
import 'package:aquacyd_home/src/domain/models.dart';
import 'package:aquacyd_home/src/home_control/biometric_gate.dart';
import 'package:aquacyd_home/src/home_control/controller.dart';
import 'package:aquacyd_home/src/home_control/operations_pages.dart';
import 'package:aquacyd_home/src/home_control/preferences.dart';
import 'package:aquacyd_home/src/home_control/snapshot_cache.dart';
import 'package:aquacyd_home/src/home_control/strings.dart';
import 'package:flutter/material.dart';
import 'package:flutter_localizations/flutter_localizations.dart';
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

  testWidgets(
    'UpdatesPage nie uruchamia OTA bez biometrii i uruchamia po zgodzie',
    (tester) async {
      final preferences = HomeControlPreferences(
        storage: SharedPreferencesAsync(),
        fallbackLocale: const Locale('pl'),
      );
      await preferences.saveBiometricProtection(true);
      final authenticator = _QueuedBiometricAuthenticator(
        <BiometricAuthorization>[
          BiometricAuthorization.cancelled,
          BiometricAuthorization.authorized,
        ],
      );
      final homeController = HomeControlController(
        preferences: preferences,
        hubCredentialsStore: _MemoryHubCredentialsStore(),
        homeAssistantCredentialsStore: _MemoryCredentialsStore(),
        snapshotCache: const _NoopSnapshotCache(),
        biometricAuthenticator: authenticator,
        enablePolling: false,
      );
      await homeController.initialize();
      await homeController.selectDemo();

      final updateService = _CountingAppUpdateService();
      final updateController = AppUpdateController(service: updateService);
      await updateController.initialize();

      await tester.pumpWidget(
        MaterialApp(
          locale: const Locale('pl'),
          supportedLocales: const <Locale>[Locale('pl'), Locale('en')],
          localizationsDelegates: const <LocalizationsDelegate<Object>>[
            HomeControlStrings.delegate,
            GlobalMaterialLocalizations.delegate,
            GlobalWidgetsLocalizations.delegate,
            GlobalCupertinoLocalizations.delegate,
          ],
          home: AppUpdateScope(
            controller: updateController,
            child: Scaffold(body: UpdatesPage(controller: homeController)),
          ),
        ),
      );
      await tester.pumpAndSettle();

      final otaButton = find.widgetWithIcon(
        FilledButton,
        Icons.download_rounded,
      );
      expect(otaButton, findsOneWidget);

      await tester.tap(otaButton);
      await tester.pumpAndSettle();

      expect(authenticator.authenticationCalls, 1);
      expect(updateService.downloadCalls, 0);
      expect(updateService.installCalls, 0);
      expect(updateController.phase, AppUpdatePhase.available);

      await tester.tap(otaButton);
      await tester.pumpAndSettle();

      expect(authenticator.authenticationCalls, 2);
      expect(updateService.downloadCalls, 1);
      expect(updateService.installCalls, 1);
      expect(updateController.phase, AppUpdatePhase.upToDate);

      await tester.pumpWidget(const SizedBox.shrink());
      homeController.dispose();
      updateController.dispose();
      await tester.pump();
    },
  );
}

final class _QueuedBiometricAuthenticator implements BiometricAuthenticator {
  _QueuedBiometricAuthenticator(List<BiometricAuthorization> results)
    : _results = List<BiometricAuthorization>.of(results);

  final List<BiometricAuthorization> _results;
  int authenticationCalls = 0;

  @override
  Future<BiometricAvailability> availability() async =>
      BiometricAvailability.available;

  @override
  Future<BiometricAuthorization> authenticate({
    required String localizedReason,
  }) async {
    authenticationCalls += 1;
    if (localizedReason.trim().isEmpty || _results.isEmpty) {
      return BiometricAuthorization.failed;
    }
    return _results.removeAt(0);
  }
}

final class _CountingAppUpdateService implements AppUpdateService {
  int downloadCalls = 0;
  int installCalls = 0;

  @override
  bool get supported => true;

  @override
  Future<InstalledAppVersion> installedVersion() async =>
      const InstalledAppVersion(version: '2.0.0', buildNumber: 5);

  @override
  Future<AppUpdateRelease?> findUpdate(InstalledAppVersion installed) async =>
      AppUpdateRelease(
        version: '2.0.1',
        buildNumber: 6,
        apkName: 'Home-Control-2.0.1.apk',
        apkUri: Uri.parse('https://github.com/example/Home-Control-2.0.1.apk'),
        bytes: 1024,
        sha256Digest: List<String>.filled(64, 'a').join(),
        notes: 'Regresyjna aktualizacja testowa.',
      );

  @override
  Future<String> download(
    AppUpdateRelease release, {
    required ValueChanged<double> onProgress,
  }) async {
    downloadCalls += 1;
    onProgress(1);
    return release.apkName;
  }

  @override
  Future<AppInstallerResult> install(String apkPath) async {
    installCalls += 1;
    return AppInstallerResult.launched;
  }

  @override
  void close() {}
}

final class _MemoryHubCredentialsStore implements HubCredentialsStore {
  HubCredentials? value;

  @override
  Future<void> clear() async => value = null;

  @override
  Future<HubCredentials?> load() async => value;

  @override
  Future<void> save(HubCredentials credentials) async => value = credentials;
}

final class _MemoryCredentialsStore implements CredentialsStore {
  HomeAssistantCredentials? value;

  @override
  Future<void> clear() async => value = null;

  @override
  Future<HomeAssistantCredentials?> load() async => value;

  @override
  Future<void> save(HomeAssistantCredentials credentials) async =>
      value = credentials;
}

final class _NoopSnapshotCache implements HomeSnapshotCache {
  const _NoopSnapshotCache();

  @override
  Future<void> clear(HomeSourceKind kind, {String? sourceId}) async {}

  @override
  Future<HomeSnapshot?> load(HomeSourceKind kind, String sourceId) async =>
      null;

  @override
  Future<void> save(HomeSnapshot snapshot) async {}
}
