import 'dart:convert';

import 'package:aquacyd_home/src/aquahub/api.dart';
import 'package:aquacyd_home/src/aquahub/app.dart';
import 'package:aquacyd_home/src/aquahub/app_update.dart';
import 'package:aquacyd_home/src/aquahub/credentials_store.dart';
import 'package:aquacyd_home/src/aquahub/demo.dart';
import 'package:aquacyd_home/src/aquahub/domain.dart';
import 'package:aquacyd_home/src/aquahub/hub_discovery.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:http/http.dart' as http;
import 'package:http/testing.dart';

void main() {
  testWidgets('pierwsze uruchomienie prowadzi do parowania AquaHub', (
    tester,
  ) async {
    await tester.pumpWidget(
      AquaHubApp(
        credentialsStore: _MemoryHubCredentialsStore(),
        appUpdateService: const UnsupportedAppUpdateService(),
        discoveryService: const _FakeHubDiscoveryService(<DiscoveredHub>[]),
        enablePolling: false,
      ),
    );
    await tester.pumpAndSettle();

    expect(find.text('Witaj w AquaHub'), findsOneWidget);
    expect(find.text('Automatyczne wykrywanie'), findsOneWidget);
    expect(find.text('Połączenie zaawansowane'), findsOneWidget);
    expect(find.text('Zobacz pełną aplikację w trybie demo'), findsOneWidget);

    await tester.tap(find.text('Zobacz pełną aplikację w trybie demo'));
    await tester.pumpAndSettle();
    expect(
      find.text('Tryb demonstracyjny · dane i komendy są symulowane'),
      findsOneWidget,
    );
    expect(find.text('System działa prawidłowo'), findsOneWidget);
  });

  testWidgets('natywne discovery wybiera jedyny panel bez wpisywania URL', (
    tester,
  ) async {
    const fingerprint =
        '0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF';
    await tester.pumpWidget(
      AquaHubApp(
        credentialsStore: _MemoryHubCredentialsStore(),
        appUpdateService: const UnsupportedAppUpdateService(),
        discoveryService: const _FakeHubDiscoveryService(<DiscoveredHub>[
          DiscoveredHub(
            name: 'AquaHub Salon',
            host: 'aquahub.local',
            port: 8443,
          ),
        ]),
        bootstrapFactory: (uri, _) => HubApi.bootstrap(
          uri,
          client: MockClient(
            (_) async => http.Response(
              jsonEncode(<String, Object?>{
                'product': 'aquahub-p4',
                'api_version': 1,
                'hostname': 'aquahub.local',
                'tls_fingerprint': fingerprint,
                'pairing_available': true,
              }),
              200,
              headers: const <String, String>{
                'content-type': 'application/json; charset=utf-8',
              },
            ),
          ),
        ),
        enablePolling: false,
      ),
    );
    await tester.pumpAndSettle();

    expect(find.text('Potwierdź panel'), findsOneWidget);
    expect(find.text('aquahub.local'), findsOneWidget);
    expect(find.text('6‑cyfrowy kod z panelu'), findsOneWidget);
  });

  testWidgets('błąd systemowego discovery zachowuje połączenie ręczne', (
    tester,
  ) async {
    await tester.pumpWidget(
      AquaHubApp(
        credentialsStore: _MemoryHubCredentialsStore(),
        appUpdateService: const UnsupportedAppUpdateService(),
        discoveryService: const _FailingHubDiscoveryService(),
        enablePolling: false,
      ),
    );
    await tester.pumpAndSettle();

    expect(find.text('Brak uprawnienia do sieci lokalnej.'), findsOneWidget);
    expect(find.text('Połączenie zaawansowane'), findsOneWidget);
    expect(find.text('Zobacz pełną aplikację w trybie demo'), findsOneWidget);
  });

  testWidgets('dostępne OTA aplikacji pojawia się automatycznie po starcie', (
    tester,
  ) async {
    await tester.pumpWidget(
      AquaHubApp(
        credentialsStore: _MemoryHubCredentialsStore(),
        discoveryService: const _FakeHubDiscoveryService(<DiscoveredHub>[]),
        appUpdateService: _AvailableAppUpdateService(),
        enablePolling: false,
      ),
    );
    await tester.pumpAndSettle();

    expect(find.text('Dostępna wersja 1.1.2'), findsOneWidget);
    expect(find.text('Pobierz i zainstaluj'), findsOneWidget);
    expect(find.text('Automatyczna aktualizacja testowa.'), findsOneWidget);
  });

  testWidgets('kompletna sesja pokazuje pulpit, automatyzacje i OTA', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(1280, 900);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);

    await tester.pumpWidget(
      AquaHubApp(
        credentialsStore: DemoHubCredentialsStore(),
        appUpdateService: const UnsupportedAppUpdateService(),
        apiFactory: createDemoHubApi,
        enablePolling: false,
      ),
    );
    await tester.pumpAndSettle();

    expect(find.text('System działa prawidłowo'), findsOneWidget);
    expect(find.text('Temperatura wody'), findsOneWidget);

    tester.binding.handleAppLifecycleStateChanged(AppLifecycleState.paused);
    tester.binding.handleAppLifecycleStateChanged(AppLifecycleState.resumed);
    await tester.pumpAndSettle();
    expect(find.text('System działa prawidłowo'), findsOneWidget);

    await tester.tap(find.text('Automatyzacje').last);
    await tester.pumpAndSettle();
    expect(find.text('Automatyzacje lokalne'), findsOneWidget);
    expect(find.text('Chłodzenie awaryjne'), findsOneWidget);

    await tester.tap(find.text('Więcej').last);
    await tester.pumpAndSettle();
    expect(find.text('Centrum aktualizacji'), findsOneWidget);
    expect(find.text('Wersja 1.0.0 · security 1'), findsOneWidget);
  });
}

final class _FakeHubDiscoveryService implements HubDiscoveryService {
  const _FakeHubDiscoveryService(this.hubs);

  final List<DiscoveredHub> hubs;

  @override
  Future<List<DiscoveredHub>> scan({
    Duration duration = const Duration(seconds: 4),
  }) async => hubs;
}

final class _FailingHubDiscoveryService implements HubDiscoveryService {
  const _FailingHubDiscoveryService();

  @override
  Future<List<DiscoveredHub>> scan({
    Duration duration = const Duration(seconds: 4),
  }) async =>
      throw const HubDiscoveryException('Brak uprawnienia do sieci lokalnej.');
}

final class _MemoryHubCredentialsStore implements HubCredentialsStore {
  HubCredentials? credentials;

  @override
  Future<void> clear() async => credentials = null;

  @override
  Future<HubCredentials?> load() async => credentials;

  @override
  Future<void> save(HubCredentials value) async => credentials = value;
}

final class _AvailableAppUpdateService implements AppUpdateService {
  @override
  bool get supported => true;

  @override
  Future<InstalledAppVersion> installedVersion() async =>
      const InstalledAppVersion(version: '1.1.1', buildNumber: 3);

  @override
  Future<AppUpdateRelease?> findUpdate(InstalledAppVersion installed) async =>
      AppUpdateRelease(
        version: '1.1.2',
        buildNumber: 4,
        apkName: 'Home-Control-1.1.2.apk',
        apkUri: Uri.parse('https://github.com/example/update.apk'),
        bytes: 1024 * 1024,
        sha256Digest: List<String>.filled(64, '0').join(),
        notes: 'Automatyczna aktualizacja testowa.',
      );

  @override
  Future<String> download(
    AppUpdateRelease release, {
    required ValueChanged<double> onProgress,
  }) async {
    onProgress(1);
    return 'verified.apk';
  }

  @override
  Future<AppInstallerResult> install(String apkPath) async =>
      AppInstallerResult.launched;

  @override
  void close() {}
}
