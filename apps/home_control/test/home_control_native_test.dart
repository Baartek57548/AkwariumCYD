import 'package:aquacyd_home/src/aquahub/app_update.dart';
import 'package:aquacyd_home/src/aquahub/credentials_store.dart';
import 'package:aquacyd_home/src/aquahub/domain.dart';
import 'package:aquacyd_home/src/aquahub/hub_discovery.dart';
import 'package:aquacyd_home/src/data/credentials_store.dart';
import 'package:aquacyd_home/src/domain/models.dart';
import 'package:aquacyd_home/src/home_control/app.dart';
import 'package:aquacyd_home/src/home_control/biometric_gate.dart';
import 'package:aquacyd_home/src/home_control/controller.dart';
import 'package:aquacyd_home/src/home_control/dashboard.dart';
import 'package:aquacyd_home/src/home_control/demo_data_source.dart';
import 'package:aquacyd_home/src/home_control/operations_pages.dart';
import 'package:aquacyd_home/src/home_control/preferences.dart';
import 'package:aquacyd_home/src/home_control/snapshot_cache.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:home_entities/home_entities.dart';
import 'package:secure_connectivity/secure_connectivity.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'package:shared_preferences_platform_interface/in_memory_shared_preferences_async.dart';
import 'package:shared_preferences_platform_interface/shared_preferences_async_platform_interface.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  setUp(() {
    SharedPreferencesAsyncPlatform.instance =
        InMemorySharedPreferencesAsync.empty();
  });

  testWidgets('native onboarding offers AquaHub, Home Assistant and Demo', (
    tester,
  ) async {
    await tester.pumpWidget(_testApp());
    await tester.pumpAndSettle();

    expect(find.text('Wybierz źródło'), findsOneWidget);
    expect(find.text('AquaHub'), findsOneWidget);
    expect(find.text('Home Assistant'), findsOneWidget);
    expect(find.text('Demo offline'), findsOneWidget);
  });

  testWidgets('AquaHub setup follows the selected English locale', (
    tester,
  ) async {
    await tester.pumpWidget(
      _testApp(
        fallbackLocale: const Locale('en'),
        discoveryService: const _EmptyHubDiscoveryService(),
      ),
    );
    await tester.pumpAndSettle();
    await tester.tap(find.text('AquaHub'));
    await tester.pumpAndSettle();

    expect(find.text('Welcome to AquaHub'), findsOneWidget);
    expect(find.text('Automatic discovery'), findsOneWidget);
    expect(find.text('Open the complete app in demo mode'), findsOneWidget);
    expect(find.text('Witaj w AquaHub'), findsNothing);
    expect(tester.takeException(), isNull);
  });

  testWidgets('Demo opens complete native dashboard and changes state', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(393, 852);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);

    await tester.pumpWidget(_testApp());
    await tester.pumpAndSettle();
    await tester.ensureVisible(find.text('Demo offline'));
    await tester.tap(find.text('Demo offline'));
    await tester.pumpAndSettle();

    expect(find.text('Akwarium'), findsWidgets);
    expect(find.text('Pulpit'), findsWidgets);
    expect(find.text('Pokoje'), findsOneWidget);
    expect(find.text('Sprzęt'), findsOneWidget);

    await tester.tap(find.text('Pokoje'));
    await tester.pumpAndSettle();
    expect(find.byType(TextField), findsOneWidget);

    await tester.enterText(find.byType(TextField), 'Światło główne');
    await tester.pumpAndSettle();
    final toggle = find.byType(Switch);
    expect(tester.widget<Switch>(toggle).value, isTrue);
    await tester.tap(toggle);
    await tester.pumpAndSettle();
    expect(tester.widget<Switch>(toggle).value, isFalse);
  });

  testWidgets('tablet uses navigation rail and supports enlarged text', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(1280, 900);
    tester.view.devicePixelRatio = 1;
    tester.platformDispatcher.textScaleFactorTestValue = 1.4;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    addTearDown(tester.platformDispatcher.clearTextScaleFactorTestValue);

    await tester.pumpWidget(_testApp());
    await tester.pumpAndSettle();
    await tester.tap(find.text('Demo offline'));
    await tester.pumpAndSettle();

    expect(find.byType(NavigationRail), findsOneWidget);
    expect(tester.takeException(), isNull);
  });

  testWidgets('mobile navigation keeps secondary destinations in More', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(393, 852);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);

    await tester.pumpWidget(_testApp());
    await tester.pumpAndSettle();
    await tester.ensureVisible(find.text('Demo offline'));
    await tester.tap(find.text('Demo offline'));
    await tester.pumpAndSettle();

    expect(find.byType(NavigationBar), findsOneWidget);
    expect(find.text('Więcej'), findsOneWidget);
    await tester.tap(find.text('Więcej'));
    await tester.pumpAndSettle();

    expect(find.text('Automatyzacje'), findsOneWidget);
    expect(find.text('Aktualizacje'), findsOneWidget);
    expect(find.text('Ustawienia'), findsOneWidget);
    await tester.tap(find.text('Ustawienia'));
    await tester.pumpAndSettle();

    expect(find.byType(SettingsPage), findsOneWidget);
    expect(tester.takeException(), isNull);
  });

  testWidgets('aquarium summary cannot stack duplicate detail routes', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(393, 852);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);

    await tester.pumpWidget(_testApp());
    await tester.pumpAndSettle();
    await tester.ensureVisible(find.text('Demo offline'));
    await tester.tap(find.text('Demo offline'));
    await tester.pumpAndSettle();

    final summary = find.byType(AquariumDashboardCard);
    expect(summary, findsOneWidget);
    await tester.tap(summary);
    await tester.pumpAndSettle();
    expect(find.byType(AquariumDetailsPage), findsOneWidget);

    await tester.tap(find.byType(AquariumDashboardCard));
    await tester.pumpAndSettle();
    expect(find.byType(AquariumDetailsPage), findsOneWidget);
    expect(tester.takeException(), isNull);
  });

  testWidgets('compact phone and wall-panel layouts remain overflow-free', (
    tester,
  ) async {
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    addTearDown(tester.platformDispatcher.clearTextScaleFactorTestValue);
    const configurations = <(Size, double)>[
      (Size(320, 568), 1),
      (Size(800, 480), 1.3),
      (Size(1024, 600), 2),
    ];

    for (final configuration in configurations) {
      SharedPreferencesAsyncPlatform.instance =
          InMemorySharedPreferencesAsync.empty();
      tester.view.physicalSize = configuration.$1;
      tester.view.devicePixelRatio = 1;
      tester.platformDispatcher.textScaleFactorTestValue = configuration.$2;

      await tester.pumpWidget(_testApp());
      await tester.pumpAndSettle();
      expect(
        tester.takeException(),
        isNull,
        reason: 'Onboarding at ${configuration.$1}, scale ${configuration.$2}',
      );
      await tester.ensureVisible(find.text('Demo offline'));
      await tester.tap(find.text('Demo offline'));
      await tester.pumpAndSettle();
      expect(
        tester.takeException(),
        isNull,
        reason: 'Dashboard at ${configuration.$1}, scale ${configuration.$2}',
      );
      await tester.pumpWidget(const SizedBox.shrink());
      await tester.pumpAndSettle();
    }
  });

  test(
    'Demo covers controlled entity families and failure scenarios',
    () async {
      final source = DemoDataSource(clock: () => DateTime.utc(2026, 8, 12, 12));
      final token = CancellationToken();
      final snapshot = await source.connect(token);

      expect(
        snapshot.entities.map((entity) => entity.type).toSet(),
        containsAll(<HomeEntityType>{
          HomeEntityType.light,
          HomeEntityType.switchEntity,
          HomeEntityType.sensor,
          HomeEntityType.binarySensor,
          HomeEntityType.climate,
          HomeEntityType.cover,
          HomeEntityType.lock,
          HomeEntityType.alarmControlPanel,
          HomeEntityType.camera,
          HomeEntityType.mediaPlayer,
          HomeEntityType.fan,
          HomeEntityType.vacuum,
          HomeEntityType.weather,
          HomeEntityType.person,
          HomeEntityType.scene,
          HomeEntityType.automation,
          HomeEntityType.button,
          HomeEntityType.number,
          HomeEntityType.select,
          HomeEntityType.unknown,
        }),
      );
      expect(snapshot.entities.any((entity) => !entity.available), isTrue);
      expect(
        snapshot.entities.any(
          (entity) => entity.type == HomeEntityType.unknown,
        ),
        isTrue,
      );
      expect(snapshot.updates, isNotEmpty);

      source.setOffline(true);
      expect(() => source.refresh(token), throwsA(isA<AppFailure>()));
      await source.close();
    },
  );

  test(
    'secure snapshot cache round-trips typed data and rejects corruption',
    () async {
      final source = DemoDataSource(clock: () => DateTime.utc(2026, 8, 12, 12));
      final snapshot = await source.connect(CancellationToken());
      final cache = _MemorySnapshotCache();

      await cache.save(snapshot);
      final restored = await cache.load(HomeSourceKind.demo, 'demo');

      expect(restored?.sourceId, snapshot.sourceId);
      expect(restored?.entities.length, snapshot.entities.length);
      expect(restored?.isOffline, isTrue);
      await cache.clear(HomeSourceKind.demo, sourceId: 'demo');
      expect(await cache.load(HomeSourceKind.demo, 'demo'), isNull);
      await source.close();
    },
  );

  test(
    'biometric protection blocks and authorizes critical commands',
    () async {
      final preferences = HomeControlPreferences(
        storage: SharedPreferencesAsync(),
      );
      final authenticator = _FakeBiometricAuthenticator();
      final controller = HomeControlController(
        preferences: preferences,
        hubCredentialsStore: _MemoryHubCredentialsStore(),
        homeAssistantCredentialsStore: _MemoryHaCredentialsStore(),
        snapshotCache: _MemorySnapshotCache(),
        biometricAuthenticator: authenticator,
        enablePolling: false,
      );
      addTearDown(controller.dispose);

      await controller.initialize();
      await controller.selectDemo();
      authenticator.results.add(BiometricAuthorization.authorized);

      expect(await controller.setBiometricProtection(true), isTrue);
      expect(await preferences.loadBiometricProtection(), isTrue);
      expect(controller.biometricProtectionEnabled, isTrue);

      final critical = controller.snapshot!.entities.singleWhere(
        (entity) => entity.id.localId == 'number.aquacyd_target_temperature',
      );
      authenticator.results.add(BiometricAuthorization.cancelled);
      expect(await controller.sendCommand(critical, 25.5), isFalse);
      expect(controller.failure?.messageKey, 'errorBiometricCancelled');
      expect(
        controller.snapshot!.entity(critical.id)?.numericValue,
        critical.numericValue,
      );

      authenticator.results.add(BiometricAuthorization.authorized);
      expect(await controller.sendCommand(critical, 25.5), isTrue);
      expect(controller.snapshot!.entity(critical.id)?.numericValue, 25.5);

      authenticator.results.add(BiometricAuthorization.authorized);
      expect(await controller.setBiometricProtection(false), isTrue);
      expect(await preferences.loadBiometricProtection(), isFalse);
      expect(authenticator.localizedReasons, isNotEmpty);
    },
  );
}

Widget _testApp({
  Locale fallbackLocale = const Locale('pl'),
  HubDiscoveryService? discoveryService,
}) => HomeControlApp(
  preferences: HomeControlPreferences(
    storage: SharedPreferencesAsync(),
    fallbackLocale: fallbackLocale,
  ),
  hubCredentialsStore: _MemoryHubCredentialsStore(),
  homeAssistantCredentialsStore: _MemoryHaCredentialsStore(),
  snapshotCache: _MemorySnapshotCache(),
  appUpdateService: const UnsupportedAppUpdateService(),
  biometricAuthenticator: _FakeBiometricAuthenticator()
    ..availabilityResult = BiometricAvailability.unavailable,
  discoveryService: discoveryService,
  enablePolling: false,
);

final class _EmptyHubDiscoveryService implements HubDiscoveryService {
  const _EmptyHubDiscoveryService();

  @override
  Future<List<DiscoveredHub>> scan({
    Duration duration = const Duration(seconds: 4),
  }) async => const <DiscoveredHub>[];
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

final class _MemoryHaCredentialsStore implements CredentialsStore {
  HomeAssistantCredentials? value;

  @override
  Future<void> clear() async => value = null;

  @override
  Future<HomeAssistantCredentials?> load() async => value;

  @override
  Future<void> save(HomeAssistantCredentials credentials) async =>
      value = credentials;
}

final class _MemorySnapshotCache implements HomeSnapshotCache {
  final Map<HomeSourceKind, HomeSnapshot> values =
      <HomeSourceKind, HomeSnapshot>{};

  @override
  Future<void> clear(HomeSourceKind kind, {String? sourceId}) async {
    final current = values[kind];
    if (sourceId == null || current?.sourceId == sourceId) values.remove(kind);
  }

  @override
  Future<HomeSnapshot?> load(HomeSourceKind kind, String sourceId) async {
    final value = values[kind];
    if (value == null || value.sourceId != sourceId) return null;
    return HomeSnapshot(
      schemaVersion: value.schemaVersion,
      sourceId: value.sourceId,
      sourceName: value.sourceName,
      sourceKind: value.sourceKind,
      areas: value.areas,
      devices: value.devices,
      entities: value.entities,
      automations: value.automations,
      updates: value.updates,
      synchronizedAt: value.synchronizedAt,
      isPartial: value.isPartial,
      isOffline: true,
    );
  }

  @override
  Future<void> save(HomeSnapshot snapshot) async {
    values[snapshot.sourceKind] = snapshot;
  }
}

final class _FakeBiometricAuthenticator implements BiometricAuthenticator {
  BiometricAvailability availabilityResult = BiometricAvailability.available;
  final List<BiometricAuthorization> results = <BiometricAuthorization>[];
  final List<String> localizedReasons = <String>[];

  @override
  Future<BiometricAvailability> availability() async => availabilityResult;

  @override
  Future<BiometricAuthorization> authenticate({
    required String localizedReason,
  }) async {
    localizedReasons.add(localizedReason);
    return results.isEmpty
        ? BiometricAuthorization.failed
        : results.removeAt(0);
  }
}
