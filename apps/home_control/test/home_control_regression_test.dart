import 'dart:async';

import 'package:aquacyd_home/src/aquahub/app_update.dart';
import 'package:aquacyd_home/src/aquahub/credentials_store.dart';
import 'package:aquacyd_home/src/aquahub/domain.dart';
import 'package:aquacyd_home/src/data/credentials_store.dart';
import 'package:aquacyd_home/src/domain/models.dart';
import 'package:aquacyd_home/src/home_control/app.dart';
import 'package:aquacyd_home/src/home_control/biometric_gate.dart';
import 'package:aquacyd_home/src/home_control/controller.dart';
import 'package:aquacyd_home/src/home_control/data_source.dart';
import 'package:aquacyd_home/src/home_control/entity_widgets.dart';
import 'package:aquacyd_home/src/home_control/preferences.dart';
import 'package:aquacyd_home/src/home_control/shell.dart';
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

  test('the latest Home Assistant profile activation wins a race', () async {
    final preferences = _preferences();
    await preferences.saveActiveSource(HomeSourceKind.homeAssistant);
    final profiles = _MemoryHomeAssistantStore()
      ..addProfile(id: _profileA, name: 'Dom', host: 'home.example.net')
      ..addProfile(id: _profileB, name: 'Biuro', host: 'office.example.net')
      ..addProfile(id: _profileC, name: 'Domek', host: 'cabin.example.net')
      ..selectedId = _profileA;
    final delayedStarted = Completer<void>();
    final delayedResult = Completer<HomeSnapshot>();
    final sources = <String, _FakeHomeDataSource>{
      _profileA: _FakeHomeDataSource.immediate(_snapshot(_profileA)),
      _profileB: _FakeHomeDataSource(
        sourceId: _profileB,
        onConnect: (_) {
          if (!delayedStarted.isCompleted) delayedStarted.complete();
          return delayedResult.future;
        },
      ),
      _profileC: _FakeHomeDataSource.immediate(_snapshot(_profileC)),
    };
    final controller = _controller(
      preferences: preferences,
      credentialsStore: profiles,
      sourceFactory: (_, profileId) => sources[profileId]!,
    );
    addTearDown(() async {
      controller.dispose();
      await Future.wait(sources.values.map((source) => source.disposeStream()));
    });

    await controller.initialize();
    expect(controller.snapshot?.sourceId, _profileA);

    final delayedActivation = controller.selectHomeAssistantProfile(_profileB);
    await delayedStarted.future;
    final latestActivation = controller.selectHomeAssistantProfile(_profileC);

    expect(await latestActivation, isTrue);
    delayedResult.complete(_snapshot(_profileB));
    expect(await delayedActivation, isFalse);

    expect(controller.phase, HomeControlPhase.ready);
    expect(controller.snapshot?.sourceId, _profileC);
    expect(controller.selectedHomeAssistantProfileId, _profileC);
    expect(profiles.selectedId, _profileC);
    expect(sources[_profileB]!.closeCount, greaterThanOrEqualTo(1));
  });

  test('a delayed close cannot clear the latest activated source', () async {
    final preferences = _preferences();
    await preferences.saveActiveSource(HomeSourceKind.homeAssistant);
    final profiles = _MemoryHomeAssistantStore()
      ..addProfile(id: _profileA, name: 'Dom', host: 'home.example.net')
      ..addProfile(id: _profileB, name: 'Biuro', host: 'office.example.net')
      ..addProfile(id: _profileC, name: 'Domek', host: 'cabin.example.net')
      ..selectedId = _profileA;
    final closeStarted = Completer<void>();
    final releaseClose = Completer<void>();
    final sources = <String, _FakeHomeDataSource>{
      _profileA: _FakeHomeDataSource(
        sourceId: _profileA,
        onConnect: (_) async => _snapshot(_profileA),
        onClose: () async {
          if (!closeStarted.isCompleted) closeStarted.complete();
          await releaseClose.future;
        },
      ),
      _profileB: _FakeHomeDataSource.immediate(_snapshot(_profileB)),
      _profileC: _FakeHomeDataSource.immediate(_snapshot(_profileC)),
    };
    final controller = _controller(
      preferences: preferences,
      credentialsStore: profiles,
      sourceFactory: (_, profileId) => sources[profileId]!,
    );
    addTearDown(() async {
      if (!releaseClose.isCompleted) releaseClose.complete();
      controller.dispose();
      await Future.wait(sources.values.map((source) => source.disposeStream()));
    });

    await controller.initialize();
    final delayed = controller.selectHomeAssistantProfile(_profileB);
    await closeStarted.future;
    final latest = controller.selectHomeAssistantProfile(_profileC);

    expect(await latest, isTrue);
    releaseClose.complete();
    expect(await delayed, isFalse);
    expect(controller.phase, HomeControlPhase.ready);
    expect(controller.snapshot?.sourceId, _profileC);
    expect(sources[_profileC]!.closeCount, 0);
  });

  test('serialized profile commits keep the newest selection', () async {
    final preferences = _preferences();
    await preferences.saveActiveSource(HomeSourceKind.homeAssistant);
    final selectStarted = Completer<void>();
    final releaseSelect = Completer<void>();
    final profiles = _MemoryHomeAssistantStore()
      ..addProfile(id: _profileA, name: 'Dom', host: 'home.example.net')
      ..addProfile(id: _profileB, name: 'Biuro', host: 'office.example.net')
      ..addProfile(id: _profileC, name: 'Domek', host: 'cabin.example.net')
      ..selectedId = _profileA
      ..beforeSelect = (id) async {
        if (id != _profileB) return;
        if (!selectStarted.isCompleted) selectStarted.complete();
        await releaseSelect.future;
      };
    final sources = <String, _FakeHomeDataSource>{
      _profileA: _FakeHomeDataSource.immediate(_snapshot(_profileA)),
      _profileB: _FakeHomeDataSource.immediate(_snapshot(_profileB)),
      _profileC: _FakeHomeDataSource.immediate(_snapshot(_profileC)),
    };
    final controller = _controller(
      preferences: preferences,
      credentialsStore: profiles,
      sourceFactory: (_, profileId) => sources[profileId]!,
    );
    addTearDown(() async {
      if (!releaseSelect.isCompleted) releaseSelect.complete();
      controller.dispose();
      await Future.wait(sources.values.map((source) => source.disposeStream()));
    });

    await controller.initialize();
    final delayed = controller.selectHomeAssistantProfile(_profileB);
    await selectStarted.future;
    final latest = controller.selectHomeAssistantProfile(_profileC);
    releaseSelect.complete();

    expect(await delayed, isFalse);
    expect(await latest, isTrue);
    expect(profiles.selectedId, _profileC);
    expect(controller.selectedHomeAssistantProfileId, _profileC);
    expect(controller.snapshot?.sourceId, _profileC);
  });

  test(
    'source switch during biometric authorization cancels old command',
    () async {
      final preferences = _preferences();
      await preferences.saveActiveSource(HomeSourceKind.homeAssistant);
      await preferences.saveBiometricProtection(true);
      final profiles = _MemoryHomeAssistantStore()
        ..addProfile(id: _profileA, name: 'Dom', host: 'home.example.net')
        ..addProfile(id: _profileB, name: 'Biuro', host: 'office.example.net')
        ..selectedId = _profileA;
      final critical = _criticalEntity(_profileA);
      final sources = <String, _FakeHomeDataSource>{
        _profileA: _FakeHomeDataSource.immediate(
          _snapshot(_profileA, entity: critical),
        ),
        _profileB: _FakeHomeDataSource.immediate(_snapshot(_profileB)),
      };
      final authenticator = _DelayedBiometricAuthenticator();
      final controller = _controller(
        preferences: preferences,
        credentialsStore: profiles,
        sourceFactory: (_, profileId) => sources[profileId]!,
        biometricAuthenticator: authenticator,
      );
      addTearDown(() async {
        authenticator.complete(BiometricAuthorization.cancelled);
        controller.dispose();
        await Future.wait(
          sources.values.map((source) => source.disposeStream()),
        );
      });

      await controller.initialize();
      final command = controller.sendCommand(critical, 26.0);
      await authenticator.started.future;
      expect(await controller.selectHomeAssistantProfile(_profileB), isTrue);
      authenticator.complete(BiometricAuthorization.authorized);

      expect(await command, isFalse);
      expect(sources[_profileA]!.sendCommandCount, 0);
      expect(controller.snapshot?.sourceId, _profileB);
    },
  );

  test(
    'cancelled setup rolls back a profile saved after cancellation',
    () async {
      final saveStarted = Completer<void>();
      final releaseSave = Completer<void>();
      final profiles = _MemoryHomeAssistantStore()
        ..beforeSave = (id) async {
          if (!saveStarted.isCompleted) saveStarted.complete();
          await releaseSave.future;
        };
      final sources = <_FakeHomeDataSource>[];
      final preferences = _preferences();
      final controller = _controller(
        preferences: preferences,
        credentialsStore: profiles,
        sourceFactory: (_, profileId) {
          final source = _FakeHomeDataSource.immediate(_snapshot(profileId));
          sources.add(source);
          return source;
        },
      );
      addTearDown(() async {
        if (!releaseSave.isCompleted) releaseSave.complete();
        controller.dispose();
        await Future.wait(sources.map((source) => source.disposeStream()));
      });

      await controller.initialize();
      controller.beginHomeAssistantSetup();
      final configuration = controller.configureHomeAssistant(
        baseUrl: 'https://ha.example.net',
        accessToken: 'abcdefghijklmnopqrstuvwxyz123456',
        profileName: 'Dom',
      );
      await saveStarted.future;
      await controller.cancelSetup();
      releaseSave.complete();

      expect(await configuration, isFalse);
      expect(await profiles.listProfiles(), isEmpty);
      expect(profiles.selectedId, isNull);
      expect(await preferences.loadActiveSource(), isNull);
      expect(controller.phase, HomeControlPhase.onboarding);
      expect(controller.setupStep, HomeSetupStep.sourceSelection);
    },
  );

  test('deleting the active profile activates the remaining source', () async {
    final preferences = _preferences();
    await preferences.saveActiveSource(HomeSourceKind.homeAssistant);
    final profiles = _MemoryHomeAssistantStore()
      ..addProfile(id: _profileA, name: 'Dom', host: 'home.example.net')
      ..addProfile(id: _profileB, name: 'Biuro', host: 'office.example.net')
      ..selectedId = _profileA;
    final sources = <String, _FakeHomeDataSource>{
      _profileA: _FakeHomeDataSource.immediate(_snapshot(_profileA)),
      _profileB: _FakeHomeDataSource.immediate(_snapshot(_profileB)),
    };
    final controller = _controller(
      preferences: preferences,
      credentialsStore: profiles,
      sourceFactory: (_, profileId) => sources[profileId]!,
    );
    addTearDown(() async {
      controller.dispose();
      await Future.wait(sources.values.map((source) => source.disposeStream()));
    });

    await controller.initialize();
    await controller.deleteHomeAssistantProfile(_profileA);

    expect(profiles.contains(_profileA), isFalse);
    expect(profiles.selectedId, _profileB);
    expect(controller.selectedHomeAssistantProfileId, _profileB);
    expect(controller.snapshot?.sourceId, _profileB);
    expect(sources[_profileB]!.connectCount, 1);
  });

  test(
    'cached startup performs a full reconnect and restores realtime events',
    () async {
      final preferences = _preferences();
      await preferences.saveActiveSource(HomeSourceKind.homeAssistant);
      final profiles = _MemoryHomeAssistantStore()
        ..addProfile(id: _profileA, name: 'Dom', host: 'home.example.net')
        ..selectedId = _profileA;
      final cachedEntity = _selectEntity(_profileA, state: 'unknown');
      final cachedSnapshot = _snapshot(
        _profileA,
        entity: cachedEntity,
        offline: true,
      );
      final onlineSnapshot = _snapshot(
        _profileA,
        entity: cachedEntity.copyWith(state: 'DAY'),
      );
      final cache = _MemorySnapshotCache()
        ..values[HomeSourceKind.homeAssistant] = cachedSnapshot;
      late final _FakeHomeDataSource source;
      source = _FakeHomeDataSource(
        sourceId: _profileA,
        onConnect: (_) async {
          if (source.connectCount == 1) {
            throw const AppFailure(
              code: AppFailureCode.offline,
              messageKey: 'errorNetwork',
            );
          }
          return onlineSnapshot;
        },
      );
      final controller = _controller(
        preferences: preferences,
        credentialsStore: profiles,
        snapshotCache: cache,
        sourceFactory: (_, _) => source,
      );
      addTearDown(() async {
        controller.dispose();
        await source.disposeStream();
      });

      await controller.initialize();
      expect(controller.phase, HomeControlPhase.ready);
      expect(controller.snapshot?.isOffline, isTrue);
      expect(source.connectCount, 1);

      expect(await controller.refresh(), isTrue);
      expect(source.connectCount, 2);
      expect(controller.snapshot?.isOffline, isFalse);

      source.emit(cachedEntity.copyWith(state: 'NIGHT'));
      await Future<void>.delayed(const Duration(milliseconds: 25));

      expect(controller.snapshot?.entity(cachedEntity.id)?.state, 'NIGHT');
      expect(source.hasRealtimeListener, isTrue);
    },
  );

  testWidgets(
    'an unknown select value renders safely and remains disabled offline',
    (tester) async {
      final preferences = _preferences();
      await preferences.saveActiveSource(HomeSourceKind.homeAssistant);
      final profiles = _MemoryHomeAssistantStore()
        ..addProfile(id: _profileA, name: 'Dom', host: 'home.example.net')
        ..selectedId = _profileA;
      final entity = _selectEntity(_profileA, state: 'unknown');
      final cache = _MemorySnapshotCache()
        ..values[HomeSourceKind.homeAssistant] = _snapshot(
          _profileA,
          entity: entity,
          offline: true,
        );
      final source = _FakeHomeDataSource(
        sourceId: _profileA,
        onConnect: (_) => Future<HomeSnapshot>.error(
          const AppFailure(
            code: AppFailureCode.offline,
            messageKey: 'errorNetwork',
          ),
        ),
      );
      final controller = _controller(
        preferences: preferences,
        credentialsStore: profiles,
        snapshotCache: cache,
        sourceFactory: (_, _) => source,
      );
      addTearDown(() async {
        controller.dispose();
        await source.disposeStream();
      });
      await controller.initialize();

      await tester.pumpWidget(
        _localizedApp(
          Scaffold(
            body: EntityCard(entity: entity, controller: controller),
          ),
        ),
      );
      await tester.tap(find.byType(EntityCard));
      await tester.pumpAndSettle();

      final dropdown = find.byType(DropdownButtonFormField<String>);
      expect(dropdown, findsOneWidget);
      expect(
        tester.widget<DropdownButtonFormField<String>>(dropdown).onChanged,
        isNull,
      );
      expect(tester.takeException(), isNull);
    },
  );

  testWidgets('failed select command rolls back and reports inside details', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(320, 568);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    final preferences = _preferences();
    await preferences.saveActiveSource(HomeSourceKind.homeAssistant);
    final profiles = _MemoryHomeAssistantStore()
      ..addProfile(id: _profileA, name: 'Dom', host: 'home.example.net')
      ..selectedId = _profileA;
    final dayOption = 'DAY ${List<String>.filled(90, 'x').join()}';
    final entity = _selectEntity(
      _profileA,
      state: dayOption,
      options: <String>[dayOption, 'NIGHT'],
    );
    final source = _FakeHomeDataSource(
      sourceId: _profileA,
      onConnect: (_) async => _snapshot(_profileA, entity: entity),
      onSendCommand: (_, _, _) async => throw const AppFailure(
        code: AppFailureCode.server,
        messageKey: 'errorServer',
      ),
    );
    final controller = _controller(
      preferences: preferences,
      credentialsStore: profiles,
      sourceFactory: (_, _) => source,
    );
    addTearDown(() async {
      controller.dispose();
      await source.disposeStream();
    });
    await controller.initialize();

    await tester.pumpWidget(
      _localizedApp(
        Scaffold(
          body: EntityCard(entity: entity, controller: controller),
        ),
      ),
    );
    await tester.tap(find.byType(EntityCard));
    await tester.pumpAndSettle();
    final dropdownFinder = find.byType(DropdownButtonFormField<String>);
    await tester.ensureVisible(dropdownFinder);
    await tester.pumpAndSettle();
    await tester.tap(dropdownFinder);
    await tester.pumpAndSettle();
    await tester.tap(find.text('NIGHT').last);
    await tester.pumpAndSettle();

    final dropdown = tester.widget<DropdownButtonFormField<String>>(
      find.byType(DropdownButtonFormField<String>),
    );
    expect(controller.snapshot?.entity(entity.id)?.state, dayOption);
    expect(dropdown.initialValue, dayOption);
    expect(
      find.text('Serwer zwrócił błąd. Spróbuj ponownie za chwilę.'),
      findsOneWidget,
    );
    expect(tester.takeException(), isNull);
  });

  testWidgets('large cards span both columns on an 800x480 wall panel', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(800, 480);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    final preferences = _preferences();
    await preferences.saveActiveSource(HomeSourceKind.homeAssistant);
    final profiles = _MemoryHomeAssistantStore()
      ..addProfile(id: _profileA, name: 'Dom', host: 'home.example.net')
      ..selectedId = _profileA;
    final source = _FakeHomeDataSource.immediate(_snapshot(_profileA));
    final controller = _controller(
      preferences: preferences,
      credentialsStore: profiles,
      sourceFactory: (_, _) => source,
    );
    addTearDown(() async {
      controller.dispose();
      await source.disposeStream();
    });
    await controller.initialize();
    await controller.saveDashboard(
      controller.dashboard.copyWith(largeCards: const <String>{'favorites'}),
    );

    await tester.pumpWidget(
      _localizedApp(
        AnimatedBuilder(
          animation: controller,
          builder: (context, child) => HomeControlShell(controller: controller),
        ),
      ),
    );
    await tester.pumpAndSettle();
    await tester.drag(
      find.byType(CustomScrollView).first,
      const Offset(0, -420),
    );
    await tester.pumpAndSettle();

    const favoritesLargeKey = ValueKey<String>(
      'dashboard-section-favorites-large',
    );
    const areasCompactKey = ValueKey<String>('dashboard-section-areas-compact');
    expect(controller.dashboard.largeCards, contains('favorites'));
    expect(find.byKey(favoritesLargeKey), findsOneWidget);
    expect(find.byKey(areasCompactKey), findsOneWidget);
    final largeWidth = tester.getSize(find.byKey(favoritesLargeKey)).width;
    final compactWidth = tester.getSize(find.byKey(areasCompactKey)).width;
    expect(largeWidth, greaterThan(compactWidth * 1.8));

    await controller.saveDashboard(
      controller.dashboard.copyWith(
        largeCards: const <String>{'favorites', 'areas'},
      ),
    );
    await tester.pumpAndSettle();

    const areasLargeKey = ValueKey<String>('dashboard-section-areas-large');
    expect(find.byKey(areasCompactKey), findsNothing);
    expect(
      tester.getSize(find.byKey(areasLargeKey)).width,
      moreOrLessEquals(largeWidth, epsilon: 0.1),
    );
    expect(tester.takeException(), isNull);
    await tester.pumpWidget(const SizedBox.shrink());
    await tester.pump();
  });

  testWidgets('a failed HA connection preserves every form field', (
    tester,
  ) async {
    final source = _FakeHomeDataSource(
      sourceId: 'ha-failed-source',
      onConnect: (_) => Future<HomeSnapshot>.error(
        const AppFailure(
          code: AppFailureCode.offline,
          messageKey: 'errorNetwork',
        ),
      ),
    );
    addTearDown(source.disposeStream);

    await tester.pumpWidget(
      HomeControlApp(
        preferences: _preferences(),
        hubCredentialsStore: _MemoryHubCredentialsStore(),
        homeAssistantCredentialsStore: _MemoryHomeAssistantStore(),
        snapshotCache: _MemorySnapshotCache(),
        appUpdateService: const UnsupportedAppUpdateService(),
        biometricAuthenticator: const _UnavailableBiometricAuthenticator(),
        homeAssistantSourceFactory: (_, _) => source,
        enablePolling: false,
      ),
    );
    await tester.pumpAndSettle();
    await tester.ensureVisible(find.text('Home Assistant'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Home Assistant'));
    await tester.pumpAndSettle();

    final fields = find.byType(TextFormField);
    expect(fields, findsNWidgets(3));
    await tester.enterText(fields.at(0), 'Mieszkanie');
    await tester.enterText(fields.at(1), 'https://ha.example.net');
    await tester.enterText(fields.at(2), 'abcdefghijklmnopqrstuvwxyz123456');
    await tester.ensureVisible(find.byType(FilledButton));
    await tester.pumpAndSettle();
    await tester.tap(find.byType(FilledButton));
    await tester.pumpAndSettle();

    expect(find.byType(TextFormField), findsNWidgets(3));
    expect(_fieldText(tester, 0), 'Mieszkanie');
    expect(_fieldText(tester, 1), 'https://ha.example.net');
    expect(_fieldText(tester, 2), 'abcdefghijklmnopqrstuvwxyz123456');
    expect(find.byType(FilledButton), findsOneWidget);
    expect(find.byIcon(Icons.error_outline_rounded), findsOneWidget);
    expect(tester.takeException(), isNull);
  });
}

const String _profileA = 'ha-aaaaaaaa';
const String _profileB = 'ha-bbbbbbbb';
const String _profileC = 'ha-cccccccc';

HomeControlPreferences _preferences() => HomeControlPreferences(
  storage: SharedPreferencesAsync(),
  fallbackLocale: const Locale('pl'),
);

HomeControlController _controller({
  required HomeControlPreferences preferences,
  required CredentialsStore credentialsStore,
  HomeSnapshotCache? snapshotCache,
  HomeAssistantSourceFactory? sourceFactory,
  BiometricAuthenticator? biometricAuthenticator,
}) => HomeControlController(
  preferences: preferences,
  hubCredentialsStore: _MemoryHubCredentialsStore(),
  homeAssistantCredentialsStore: credentialsStore,
  snapshotCache: snapshotCache ?? _MemorySnapshotCache(),
  biometricAuthenticator:
      biometricAuthenticator ?? const _UnavailableBiometricAuthenticator(),
  homeAssistantSourceFactory: sourceFactory,
  enablePolling: false,
);

Widget _localizedApp(Widget home) => MaterialApp(
  locale: const Locale('pl'),
  supportedLocales: const <Locale>[Locale('pl'), Locale('en')],
  localizationsDelegates: const <LocalizationsDelegate<Object>>[
    HomeControlStrings.delegate,
    GlobalMaterialLocalizations.delegate,
    GlobalWidgetsLocalizations.delegate,
    GlobalCupertinoLocalizations.delegate,
  ],
  home: home,
);

String _fieldText(WidgetTester tester, int index) => tester
    .widget<TextFormField>(find.byType(TextFormField).at(index))
    .controller!
    .text;

HomeEntity _selectEntity(
  String sourceId, {
  required String state,
  List<String> options = const <String>['DAY', 'NIGHT'],
}) => HomeEntity(
  id: SourceScopedId(sourceId: sourceId, localId: 'select.mode'),
  deviceId: SourceScopedId(sourceId: sourceId, localId: 'device.controller'),
  areaId: SourceScopedId(sourceId: sourceId, localId: 'area.utility'),
  name: 'Tryb pracy',
  type: HomeEntityType.select,
  state: state,
  attributes: const <String, Object?>{},
  unit: '',
  availability: EntityAvailability.available,
  writable: true,
  risk: HomeCommandRisk.routine,
  changedAt: DateTime.utc(2026, 8, 13, 10),
  updatedAt: DateTime.utc(2026, 8, 13, 10),
  constraints: EntityConstraints(options: options),
);

HomeEntity _criticalEntity(String sourceId) => HomeEntity(
  id: SourceScopedId(sourceId: sourceId, localId: 'number.target_temperature'),
  deviceId: SourceScopedId(sourceId: sourceId, localId: 'device.controller'),
  areaId: SourceScopedId(sourceId: sourceId, localId: 'area.utility'),
  name: 'Temperatura zadana',
  type: HomeEntityType.number,
  state: 24.0,
  attributes: const <String, Object?>{},
  unit: '°C',
  availability: EntityAvailability.available,
  writable: true,
  risk: HomeCommandRisk.critical,
  changedAt: DateTime.utc(2026, 8, 13, 10),
  updatedAt: DateTime.utc(2026, 8, 13, 10),
  constraints: const EntityConstraints(minimum: 18, maximum: 30, step: 0.5),
);

HomeSnapshot _snapshot(
  String sourceId, {
  HomeEntity? entity,
  bool offline = false,
}) => HomeSnapshot(
  schemaVersion: HomeSnapshot.currentSchemaVersion,
  sourceId: sourceId,
  sourceName: 'Home Assistant $sourceId',
  sourceKind: HomeSourceKind.homeAssistant,
  areas: const <HomeArea>[],
  devices: const <HomeDevice>[],
  entities: entity == null ? const <HomeEntity>[] : <HomeEntity>[entity],
  automations: const <HomeAutomation>[],
  updates: const <HomeUpdate>[],
  synchronizedAt: DateTime.utc(2026, 8, 13, 10),
  isPartial: false,
  isOffline: offline,
);

final class _FakeHomeDataSource implements HomeDataSource {
  _FakeHomeDataSource({
    required this.sourceId,
    required this.onConnect,
    this.onClose,
    this.onSendCommand,
  });

  factory _FakeHomeDataSource.immediate(HomeSnapshot snapshot) =>
      _FakeHomeDataSource(
        sourceId: snapshot.sourceId,
        onConnect: (_) async => snapshot,
      );

  @override
  final String sourceId;
  final Future<HomeSnapshot> Function(CancellationToken cancellation) onConnect;
  final Future<void> Function()? onClose;
  final Future<void> Function(
    HomeEntity entity,
    Object? value,
    CancellationToken cancellation,
  )?
  onSendCommand;
  final StreamController<HomeEntity> _events =
      StreamController<HomeEntity>.broadcast(sync: true);
  int connectCount = 0;
  int closeCount = 0;
  int sendCommandCount = 0;
  HomeSnapshot? _lastSnapshot;

  @override
  String get displayName => 'Home Assistant $sourceId';

  @override
  HomeSourceKind get kind => HomeSourceKind.homeAssistant;

  @override
  Stream<HomeEntity> get stateChanges => _events.stream;

  bool get hasRealtimeListener => _events.hasListener;

  @override
  Future<HomeSnapshot> connect(CancellationToken cancellation) async {
    connectCount++;
    final snapshot = await onConnect(cancellation);
    _lastSnapshot = snapshot;
    return snapshot;
  }

  @override
  Future<HomeSnapshot> refresh(CancellationToken cancellation) async {
    cancellation.throwIfCancelled();
    final snapshot = _lastSnapshot;
    if (snapshot == null) {
      throw const AppFailure(
        code: AppFailureCode.offline,
        messageKey: 'errorNetwork',
      );
    }
    return snapshot;
  }

  void emit(HomeEntity entity) => _events.add(entity);

  @override
  Future<void> sendCommand(
    HomeEntity entity,
    Object? value,
    CancellationToken cancellation,
  ) async {
    cancellation.throwIfCancelled();
    sendCommandCount++;
    await onSendCommand?.call(entity, value, cancellation);
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
  Future<void> close() async {
    closeCount++;
    await onClose?.call();
  }

  Future<void> disposeStream() async {
    if (!_events.isClosed) await _events.close();
  }
}

final class _MemoryHomeAssistantStore
    implements CredentialsStore, HomeAssistantProfileStore {
  final Map<String, ({String name, HomeAssistantCredentials credentials})>
  _profiles = <String, ({String name, HomeAssistantCredentials credentials})>{};
  HomeAssistantCredentials? _legacy;
  String? selectedId;
  int _nextId = 0;
  Future<void> Function(String id)? beforeSelect;
  Future<void> Function(String id)? beforeSave;

  void addProfile({
    required String id,
    required String name,
    required String host,
  }) {
    _profiles[id] = (
      name: name,
      credentials: HomeAssistantCredentials.parse(
        baseUrl: 'https://$host',
        accessToken: '${id}abcdefghijklmnopqrstuvwxyz',
      ),
    );
  }

  bool contains(String id) => _profiles.containsKey(id);

  @override
  Future<void> clear() async {
    _legacy = null;
    _profiles.clear();
    selectedId = null;
  }

  @override
  Future<HomeAssistantCredentials?> load() async {
    final id = selectedId;
    return id == null ? _legacy : _profiles[id]?.credentials;
  }

  @override
  Future<void> save(HomeAssistantCredentials credentials) async {
    _legacy = credentials;
  }

  @override
  Future<List<HomeAssistantProfile>> listProfiles() async =>
      <HomeAssistantProfile>[
        for (final entry in _profiles.entries)
          HomeAssistantProfile(
            id: entry.key,
            name: entry.value.name,
            baseUri: entry.value.credentials.baseUri,
          ),
      ];

  @override
  Future<HomeAssistantCredentials?> loadProfile(String id) async =>
      _profiles[id]?.credentials;

  @override
  Future<String?> selectedProfileId() async => selectedId;

  @override
  Future<String> saveProfile({
    required HomeAssistantCredentials credentials,
    required String name,
    String? profileId,
  }) async {
    final id =
        profileId ?? 'ha-memory${(_nextId++).toString().padLeft(6, '0')}';
    await beforeSave?.call(id);
    _profiles[id] = (name: name, credentials: credentials);
    selectedId = id;
    return id;
  }

  @override
  Future<void> selectProfile(String id) async {
    if (!_profiles.containsKey(id)) {
      throw ArgumentError.value(id, 'id', 'Profile does not exist.');
    }
    await beforeSelect?.call(id);
    selectedId = id;
  }

  @override
  Future<void> deleteProfile(String id) async {
    _profiles.remove(id);
    if (selectedId == id) selectedId = _profiles.keys.firstOrNull;
  }
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
    return value?.sourceId == sourceId ? value : null;
  }

  @override
  Future<void> save(HomeSnapshot snapshot) async {
    values[snapshot.sourceKind] = snapshot;
  }
}

final class _MemoryHubCredentialsStore implements HubCredentialsStore {
  HubCredentials? _credentials;

  @override
  Future<void> clear() async => _credentials = null;

  @override
  Future<HubCredentials?> load() async => _credentials;

  @override
  Future<void> save(HubCredentials credentials) async =>
      _credentials = credentials;
}

final class _UnavailableBiometricAuthenticator
    implements BiometricAuthenticator {
  const _UnavailableBiometricAuthenticator();

  @override
  Future<BiometricAvailability> availability() async =>
      BiometricAvailability.unavailable;

  @override
  Future<BiometricAuthorization> authenticate({
    required String localizedReason,
  }) async => BiometricAuthorization.unavailable;
}

final class _DelayedBiometricAuthenticator implements BiometricAuthenticator {
  final Completer<void> started = Completer<void>();
  final Completer<BiometricAuthorization> _result =
      Completer<BiometricAuthorization>();

  @override
  Future<BiometricAvailability> availability() async =>
      BiometricAvailability.available;

  @override
  Future<BiometricAuthorization> authenticate({
    required String localizedReason,
  }) {
    if (!started.isCompleted) started.complete();
    return _result.future;
  }

  void complete(BiometricAuthorization value) {
    if (!_result.isCompleted) _result.complete(value);
  }
}
