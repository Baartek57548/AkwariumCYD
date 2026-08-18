import 'dart:async';

import 'package:aquacyd_home/src/aquahub/app_update.dart';
import 'package:aquacyd_home/src/data/credentials_store.dart';
import 'package:aquacyd_home/src/domain/models.dart';
import 'package:aquacyd_home/src/home_control/app.dart';
import 'package:aquacyd_home/src/home_control/biometric_gate.dart';
import 'package:aquacyd_home/src/home_control/controller.dart';
import 'package:aquacyd_home/src/home_control/dashboard.dart';
import 'package:aquacyd_home/src/home_control/data_source.dart';
import 'package:aquacyd_home/src/home_control/entity_widgets.dart';
import 'package:aquacyd_home/src/home_control/preferences.dart';
import 'package:aquacyd_home/src/home_control/snapshot_cache.dart';
import 'package:aquacyd_home/src/home_control/strings.dart';
import 'package:flutter/material.dart';
import 'package:flutter_localizations/flutter_localizations.dart';
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

  testWidgets('dashboard omits aquarium when the source has no aquarium data', (
    tester,
  ) async {
    _configureView(tester, size: const Size(800, 480));
    await _pumpHomeAssistant(tester, snapshot: _snapshot(sourceId: 'ha-main'));

    expect(find.byType(HomeDashboardPage), findsOneWidget);
    expect(find.byType(AquariumDashboardCard), findsNothing);
    expect(
      find.text('This source does not expose an aquarium module yet.'),
      findsNothing,
    );
    expect(tester.takeException(), isNull);
  });

  for (final configuration in <({String name, Size size})>[
    (name: 'phone-320x568', size: const Size(320, 568)),
    (name: 'wall-panel-800x480', size: const Size(800, 480)),
  ]) {
    testWidgets('${configuration.name} remains usable at 200% text scale', (
      tester,
    ) async {
      _configureView(tester, size: configuration.size, textScale: 2);
      await _pumpDemo(tester);

      expect(find.byType(HomeDashboardPage), findsOneWidget);
      expect(tester.takeException(), isNull);

      final footer = find.textContaining('Last sync:');
      final dashboardScroll = find
          .descendant(
            of: find.byType(CustomScrollView).first,
            matching: find.byType(Scrollable),
          )
          .first;
      await tester.scrollUntilVisible(
        footer,
        240,
        scrollable: dashboardScroll,
        maxScrolls: 30,
      );
      expect(footer, findsOneWidget);
      expect(tester.takeException(), isNull);
    });
  }

  testWidgets('wall panel exposes four scrollable primary rail destinations', (
    tester,
  ) async {
    _configureView(tester, size: const Size(800, 480));
    await _pumpDemo(tester);

    final railFinder = find.byType(NavigationRail);
    expect(railFinder, findsOneWidget);
    final rail = tester.widget<NavigationRail>(railFinder);
    expect(rail.destinations, hasLength(4));
    expect(rail.scrollable, isTrue);
    expect(tester.takeException(), isNull);
  });

  for (final configuration in <({ThemeMode mode, Brightness brightness})>[
    (mode: ThemeMode.light, brightness: Brightness.light),
    (mode: ThemeMode.dark, brightness: Brightness.dark),
  ]) {
    testWidgets('${configuration.mode.name} premium theme renders dashboard', (
      tester,
    ) async {
      _configureView(tester, size: const Size(393, 852));
      await _pumpDemo(tester, themeMode: configuration.mode);

      final dashboard = find.byType(HomeDashboardPage);
      expect(dashboard, findsOneWidget);
      expect(
        Theme.of(tester.element(dashboard)).brightness,
        configuration.brightness,
      );
      expect(tester.takeException(), isNull);
    });
  }

  testWidgets(
    'status is a live region and primary targets remain at least 48dp',
    (tester) async {
      _configureView(tester, size: const Size(320, 568));
      final semantics = tester.ensureSemantics();

      const statusMessage =
          'There is no connection. Last known data remains read-only.';
      await tester.pumpWidget(
        _localizedApp(
          SourceStatusBanner(
            snapshot: _snapshot(sourceId: 'ha-main', offline: true),
            failureKey: null,
            onDismiss: () {},
          ),
        ),
      );
      await tester.pump();

      expect(find.bySemanticsLabel(statusMessage), findsOneWidget);
      final liveRegion = find.byWidgetPredicate(
        (widget) => widget is Semantics && widget.properties.liveRegion == true,
        description: 'live status semantics',
      );
      expect(liveRegion, findsOneWidget);
      expect(
        tester
            .getSemantics(liveRegion)
            .getSemanticsData()
            .flagsCollection
            .isLiveRegion,
        isTrue,
      );
      semantics.dispose();

      await _pumpDemo(tester);
      _expectMinimumSize(tester, find.byType(IconButton), 48);
      _expectMinimumSize(tester, find.byType(NavigationDestination), 48);
      expect(tester.takeException(), isNull);
    },
  );
}

void _configureView(
  WidgetTester tester, {
  required Size size,
  double textScale = 1,
}) {
  tester.view.physicalSize = size;
  tester.view.devicePixelRatio = 1;
  tester.platformDispatcher.textScaleFactorTestValue = textScale;
  addTearDown(tester.view.resetPhysicalSize);
  addTearDown(tester.view.resetDevicePixelRatio);
  addTearDown(tester.platformDispatcher.clearTextScaleFactorTestValue);
}

Future<void> _pumpDemo(
  WidgetTester tester, {
  ThemeMode themeMode = ThemeMode.light,
}) async {
  final preferences = HomeControlPreferences(
    storage: SharedPreferencesAsync(),
    fallbackLocale: const Locale('en'),
  );
  await preferences.saveLocale(const Locale('en'));
  await preferences.saveThemeMode(themeMode);
  await tester.pumpWidget(_testApp(preferences: preferences));
  await tester.pumpAndSettle();

  final demo = find.text('Offline demo');
  expect(demo, findsOneWidget);
  await tester.ensureVisible(demo);
  await tester.tap(demo);
  await tester.pumpAndSettle();
}

Future<void> _pumpHomeAssistant(
  WidgetTester tester, {
  required HomeSnapshot snapshot,
}) async {
  final preferences = HomeControlPreferences(
    storage: SharedPreferencesAsync(),
    fallbackLocale: const Locale('en'),
  );
  await preferences.saveLocale(const Locale('en'));
  await preferences.saveThemeMode(ThemeMode.light);
  await preferences.saveActiveSource(HomeSourceKind.homeAssistant);
  final credentials = HomeAssistantCredentials.parse(
    baseUrl: 'https://home.test.local',
    accessToken: 'test-access-token-with-sufficient-length',
  );
  await tester.pumpWidget(
    _testApp(
      preferences: preferences,
      credentialsStore: _MemoryCredentialsStore(credentials),
      homeAssistantSourceFactory: (_, _) => _StaticHomeDataSource(snapshot),
    ),
  );
  await tester.pumpAndSettle();
}

Widget _testApp({
  required HomeControlPreferences preferences,
  CredentialsStore? credentialsStore,
  HomeAssistantSourceFactory? homeAssistantSourceFactory,
}) => HomeControlApp(
  preferences: preferences,
  homeAssistantCredentialsStore: credentialsStore ?? _MemoryCredentialsStore(),
  snapshotCache: const _NoOpSnapshotCache(),
  appUpdateService: const UnsupportedAppUpdateService(),
  biometricAuthenticator: const _UnavailableBiometricAuthenticator(),
  homeAssistantSourceFactory: homeAssistantSourceFactory,
  enablePolling: false,
);

Widget _localizedApp(Widget child) => MaterialApp(
  locale: const Locale('en'),
  supportedLocales: const <Locale>[Locale('pl'), Locale('en')],
  localizationsDelegates: const <LocalizationsDelegate<Object>>[
    HomeControlStrings.delegate,
    GlobalMaterialLocalizations.delegate,
    GlobalWidgetsLocalizations.delegate,
    GlobalCupertinoLocalizations.delegate,
  ],
  home: Scaffold(body: child),
);

void _expectMinimumSize(WidgetTester tester, Finder finder, double minimum) {
  expect(finder, findsWidgets);
  for (var index = 0; index < finder.evaluate().length; index++) {
    final size = tester.getSize(finder.at(index));
    expect(
      size.width,
      greaterThanOrEqualTo(minimum),
      reason: '${finder.at(index)} has width ${size.width}',
    );
    expect(
      size.height,
      greaterThanOrEqualTo(minimum),
      reason: '${finder.at(index)} has height ${size.height}',
    );
  }
}

HomeSnapshot _snapshot({required String sourceId, bool offline = false}) =>
    HomeSnapshot(
      schemaVersion: HomeSnapshot.currentSchemaVersion,
      sourceId: sourceId,
      sourceName: 'Test home',
      sourceKind: HomeSourceKind.homeAssistant,
      areas: const <HomeArea>[],
      devices: const <HomeDevice>[],
      entities: const <HomeEntity>[],
      automations: const <HomeAutomation>[],
      updates: const <HomeUpdate>[],
      synchronizedAt: DateTime.utc(2026, 8, 13, 10),
      isPartial: false,
      isOffline: offline,
    );

final class _StaticHomeDataSource implements HomeDataSource {
  const _StaticHomeDataSource(this.snapshot);

  final HomeSnapshot snapshot;

  @override
  String get sourceId => snapshot.sourceId;

  @override
  String get displayName => snapshot.sourceName;

  @override
  HomeSourceKind get kind => snapshot.sourceKind;

  @override
  Stream<HomeEntity> get stateChanges => const Stream<HomeEntity>.empty();

  @override
  Future<HomeSnapshot> connect(CancellationToken cancellation) async {
    cancellation.throwIfCancelled();
    return snapshot;
  }

  @override
  Future<HomeSnapshot> refresh(CancellationToken cancellation) =>
      connect(cancellation);

  @override
  Future<void> sendCommand(
    HomeEntity entity,
    Object? value,
    CancellationToken cancellation,
  ) async {
    cancellation.throwIfCancelled();
  }

  @override
  Future<List<HistoryPoint>> loadHistory(
    HomeEntity entity,
    Duration period,
    CancellationToken cancellation,
  ) async {
    cancellation.throwIfCancelled();
    return const <HistoryPoint>[];
  }

  @override
  Future<void> installUpdate(
    HomeUpdate update,
    CancellationToken cancellation,
  ) async {
    cancellation.throwIfCancelled();
  }

  @override
  Future<void> close() async {}
}

final class _MemoryCredentialsStore implements CredentialsStore {
  _MemoryCredentialsStore([this.value]);

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

final class _NoOpSnapshotCache implements HomeSnapshotCache {
  const _NoOpSnapshotCache();

  @override
  Future<void> clear(HomeSourceKind kind, {String? sourceId}) async {}

  @override
  Future<HomeSnapshot?> load(HomeSourceKind kind, String sourceId) async =>
      null;

  @override
  Future<void> save(HomeSnapshot snapshot) async {}
}

final class _UnavailableBiometricAuthenticator
    implements BiometricAuthenticator {
  const _UnavailableBiometricAuthenticator();

  @override
  Future<BiometricAuthorization> authenticate({
    required String localizedReason,
  }) async => BiometricAuthorization.unavailable;

  @override
  Future<BiometricAvailability> availability() async =>
      BiometricAvailability.unavailable;
}
