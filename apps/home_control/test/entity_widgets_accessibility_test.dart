import 'dart:async';

import 'package:aquacyd_home/src/aquahub/credentials_store.dart';
import 'package:aquacyd_home/src/aquahub/domain.dart';
import 'package:aquacyd_home/src/data/credentials_store.dart';
import 'package:aquacyd_home/src/domain/models.dart';
import 'package:aquacyd_home/src/home_control/biometric_gate.dart';
import 'package:aquacyd_home/src/home_control/controller.dart';
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

  testWidgets(
    'EntityCard exposes separate details and toggle semantics with 48px targets',
    (tester) async {
      final semantics = tester.ensureSemantics();
      final harness = await _Harness.online();
      addTearDown(harness.dispose);

      await tester.pumpWidget(harness.app());
      await tester.pumpAndSettle();

      final details = find.byKey(
        ValueKey<String>('entity-details-${harness.entity.id.value}'),
      );
      final toggle = find.byKey(
        ValueKey<String>('entity-toggle-${harness.entity.id.value}'),
      );

      expect(details, findsOneWidget);
      expect(toggle, findsOneWidget);
      expect(tester.getSize(toggle).width, greaterThanOrEqualTo(48));
      expect(tester.getSize(toggle).height, greaterThanOrEqualTo(48));
      expect(
        tester.getSemantics(details),
        matchesSemantics(
          label: harness.entity.name,
          value: 'Wyłączone',
          hint: 'Szczegóły',
          isButton: true,
          hasTapAction: true,
          isEnabled: true,
          hasEnabledState: true,
        ),
      );
      expect(
        tester.getSemantics(toggle),
        matchesSemantics(
          label: 'Włącz: ${harness.entity.name}',
          value: 'Wyłączone',
          hasTapAction: true,
          hasToggledState: true,
          isToggled: false,
          isEnabled: true,
          hasEnabledState: true,
          isFocusable: true,
          hasFocusAction: true,
        ),
      );

      await tester.tap(toggle);
      await tester.pump();

      expect(harness.source.sendCommandCount, 1);
      expect(
        find.byKey(
          ValueKey<String>('entity-pending-${harness.entity.id.value}'),
        ),
        findsOneWidget,
      );
      expect(
        harness.controller.snapshot?.entity(harness.entity.id)?.booleanValue,
        isFalse,
        reason: 'Stan nie może zmienić się przed potwierdzeniem źródła.',
      );
      expect(find.byType(BottomSheet), findsNothing);
      harness.source.completeCommand();
      await tester.pumpAndSettle();

      await tester.tap(details);
      await tester.pumpAndSettle();
      expect(find.byType(BottomSheet), findsOneWidget);
      semantics.dispose();
    },
  );

  testWidgets('EntityCard is read-only and announces offline state', (
    tester,
  ) async {
    final semantics = tester.ensureSemantics();
    final harness = await _Harness.offline();
    addTearDown(harness.dispose);

    await tester.pumpWidget(harness.app());
    await tester.pumpAndSettle();

    final details = find.byKey(
      ValueKey<String>('entity-details-${harness.entity.id.value}'),
    );
    final toggle = find.byKey(
      ValueKey<String>('entity-toggle-${harness.entity.id.value}'),
    );
    expect(
      tester.getSemantics(details),
      matchesSemantics(
        label: harness.entity.name,
        value: 'Offline',
        hint: 'Szczegóły',
        isButton: true,
        hasTapAction: true,
        isEnabled: true,
        hasEnabledState: true,
      ),
    );
    expect(
      tester.getSemantics(toggle),
      matchesSemantics(
        label: 'Włącz: ${harness.entity.name}',
        value: 'Wyłączone',
        hasToggledState: true,
        isToggled: false,
        isEnabled: false,
        hasEnabledState: true,
      ),
    );
    expect(tester.getSize(toggle), const Size(48, 48));
    await tester.tap(toggle);
    await tester.pump();
    expect(harness.source.sendCommandCount, 0);
    semantics.dispose();
  });

  testWidgets('SourceStatusBanner has one live message and independent dismiss', (
    tester,
  ) async {
    final semantics = tester.ensureSemantics();
    var dismissed = false;
    await tester.pumpWidget(
      _localizedApp(
        Scaffold(
          body: SourceStatusBanner(
            snapshot: _snapshot(_entity(), offline: true),
            failureKey: 'errorNetwork',
            onDismiss: () => dismissed = true,
          ),
        ),
      ),
    );

    final message = find.byKey(const ValueKey<String>('source-status-message'));
    final dismiss = find.byKey(const ValueKey<String>('source-status-dismiss'));
    expect(
      tester.getSemantics(message),
      matchesSemantics(
        label:
            'Źródło jest nieosiągalne. Sprawdź sieć lokalną, VPN lub Internet.',
        isLiveRegion: true,
      ),
    );
    expect(tester.getSize(dismiss), const Size(48, 48));
    expect(find.byTooltip('Zamknij'), findsOneWidget);

    await tester.tap(dismiss);
    expect(dismissed, isTrue);
    semantics.dispose();
  });
}

final class _Harness {
  _Harness({
    required this.controller,
    required this.source,
    required this.entity,
  });

  final HomeControlController controller;
  final _ControlledSource source;
  final HomeEntity entity;

  static Future<_Harness> online() => _create(offline: false);

  static Future<_Harness> offline() => _create(offline: true);

  static Future<_Harness> _create({required bool offline}) async {
    final preferences = HomeControlPreferences(
      storage: SharedPreferencesAsync(),
      fallbackLocale: const Locale('pl'),
    );
    final entity = _entity();
    final snapshot = _snapshot(entity, offline: offline);
    final source = _ControlledSource(snapshot);
    final credentialsStore = _MemoryCredentialsStore();
    final controller = HomeControlController(
      preferences: preferences,
      hubCredentialsStore: _MemoryHubCredentialsStore(),
      homeAssistantCredentialsStore: credentialsStore,
      snapshotCache: _MemorySnapshotCache(),
      biometricAuthenticator: const _UnavailableBiometricAuthenticator(),
      homeAssistantSourceFactory: (_, _) => source,
      enablePolling: false,
    );
    await preferences.saveActiveSource(HomeSourceKind.homeAssistant);
    await credentialsStore.save(
      HomeAssistantCredentials.parse(
        baseUrl: 'https://ha.example.test',
        accessToken: 'abcdefghijklmnopqrstuvwxyz123456',
      ),
    );
    await controller.initialize();
    return _Harness(controller: controller, source: source, entity: entity);
  }

  Widget app() => _localizedApp(
    AnimatedBuilder(
      animation: controller,
      builder: (context, child) => Scaffold(
        body: Center(
          child: SizedBox(
            width: 320,
            child: EntityCard(
              entity: controller.snapshot!.entity(entity.id)!,
              controller: controller,
            ),
          ),
        ),
      ),
    ),
  );

  Future<void> dispose() async {
    controller.dispose();
    await source.dispose();
  }
}

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

HomeEntity _entity() => HomeEntity(
  id: SourceScopedId(sourceId: 'ha-test', localId: 'light.living_room'),
  deviceId: SourceScopedId(sourceId: 'ha-test', localId: 'device.lamp'),
  areaId: SourceScopedId(sourceId: 'ha-test', localId: 'area.living_room'),
  name: 'Bardzo długa nazwa lampy w salonie',
  type: HomeEntityType.light,
  state: false,
  attributes: const <String, Object?>{},
  unit: '',
  availability: EntityAvailability.available,
  writable: true,
  risk: HomeCommandRisk.routine,
  changedAt: DateTime.utc(2026, 8, 13, 10),
  updatedAt: DateTime.utc(2026, 8, 13, 10),
  constraints: const EntityConstraints(),
);

HomeSnapshot _snapshot(HomeEntity entity, {required bool offline}) =>
    HomeSnapshot(
      schemaVersion: HomeSnapshot.currentSchemaVersion,
      sourceId: 'ha-test',
      sourceName: 'Home Assistant',
      sourceKind: HomeSourceKind.homeAssistant,
      areas: const <HomeArea>[],
      devices: const <HomeDevice>[],
      entities: <HomeEntity>[entity],
      automations: const <HomeAutomation>[],
      updates: const <HomeUpdate>[],
      synchronizedAt: DateTime.utc(2026, 8, 13, 10),
      isPartial: false,
      isOffline: offline,
    );

final class _ControlledSource implements HomeDataSource {
  _ControlledSource(this.snapshot);

  HomeSnapshot snapshot;
  final StreamController<HomeEntity> _changes =
      StreamController<HomeEntity>.broadcast();
  Completer<void>? _command;
  int sendCommandCount = 0;

  @override
  String get sourceId => snapshot.sourceId;

  @override
  String get displayName => snapshot.sourceName;

  @override
  HomeSourceKind get kind => snapshot.sourceKind;

  @override
  Stream<HomeEntity> get stateChanges => _changes.stream;

  @override
  Future<HomeSnapshot> connect(CancellationToken cancellation) async =>
      snapshot;

  @override
  Future<HomeSnapshot> refresh(CancellationToken cancellation) async =>
      snapshot;

  @override
  Future<void> sendCommand(
    HomeEntity entity,
    Object? value,
    CancellationToken cancellation,
  ) {
    sendCommandCount++;
    _command = Completer<void>();
    return _command!.future;
  }

  void completeCommand() {
    final command = _command;
    if (command == null || command.isCompleted) return;
    final current = snapshot.entities.single;
    final updated = current.copyWith(state: !current.booleanValue!);
    snapshot = snapshot.replaceEntity(updated);
    command.complete();
  }

  @override
  Future<List<HistoryPoint>> loadHistory(
    HomeEntity entity,
    Duration period,
    CancellationToken cancellation,
  ) async => const <HistoryPoint>[];

  @override
  Future<void> installUpdate(
    HomeUpdate update,
    CancellationToken cancellation,
  ) async {}

  @override
  Future<void> close() async {}

  Future<void> dispose() async {
    if (!_changes.isClosed) await _changes.close();
  }
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

final class _MemoryHubCredentialsStore implements HubCredentialsStore {
  @override
  Future<void> clear() async {}

  @override
  Future<HubCredentials?> load() async => null;

  @override
  Future<void> save(HubCredentials credentials) async {}
}

final class _MemorySnapshotCache implements HomeSnapshotCache {
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
  Future<BiometricAvailability> availability() async =>
      BiometricAvailability.unavailable;

  @override
  Future<BiometricAuthorization> authenticate({
    required String localizedReason,
  }) async => BiometricAuthorization.unavailable;
}
