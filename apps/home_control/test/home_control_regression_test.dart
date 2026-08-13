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
        on×]õ¶‰žËkºwµç}±Õµ¹Ì½¸…¸€àÀÁàÐàÀÝ…±°Á…¹•°œ°€ (€€€Ñ•ÍÑ•È°(€€¤…Íå¹Œì(€€€Ñ•ÍÑ•È¹Ù¥•Ü¹Á¡åÍ¥…±M¥é”€ô½¹ÍÐM¥é” àÀÀ°€ÐàÀ¤ì(€€€Ñ•ÍÑ•È¹Ù¥•Ü¹‘•Ù¥•A¥á•±I…Ñ¥¼€ô€Äì(€€€…‘‘Q•…É½Ý¸¡Ñ•ÍÑ•È¹Ù¥•Ü¹É•Í•ÑA¡åÍ¥…±M¥é”¤ì(€€€…‘‘Q•…É½Ý¸¡Ñ•ÍÑ•È¹Ù¥•Ü¹É•Í•Ñ•Ù¥•A¥á•±I…Ñ¥¼¤ì(€€€™¥¹…°ÁÉ•™•É•¹•Ì€ô}ÁÉ•™•É•¹•Ì ¤ì(€€€…Ý…¥ÐÁÉ•™•É•¹•Ì¹Í…Ù•Ñ¥Ù•M½ÕÉ”¡!½µ•M½ÕÉ•-¥¹¹¡½µ•ÍÍ¥ÍÑ…¹Ð¤ì(€€€™¥¹…°ÁÉ½™¥±•Ì€ô}5•µ½Éå!½µ•ÍÍ¥ÍÑ…¹ÑMÑ½É” ¤(€€€€€€¸¹…‘‘AÉ½™¥±”¡¥è}ÁÉ½™¥±•°¹…µ”è€½´œ°¡½ÍÐè€¡½µ”¹•á…µÁ±”¹¹•Ðœ¤(€€€€€€¸¹Í•±•Ñ•‘%€ô}ÁÉ½™¥±•ì(€€€™¥¹…°Í½ÕÉ”€ô}…­•!½µ•…Ñ…M½ÕÉ”¹¥µµ•‘¥…Ñ”¡}Í¹…ÁÍ¡½Ð¡}ÁÉ½™¥±•¤¤ì(€€€™¥¹…°½¹ÑÉ½±±•È€ô}½¹ÑÉ½±±•È (€€€€€ÁÉ•™•É•¹•ÌèÁÉ•™•É•¹•Ì°(€€€€€É•‘•¹Ñ¥…±ÍMÑ½É”èÁÉ½™¥±•Ì°(€€€€€Í½ÕÉ•…Ñ½Éäè€¡|°|¤€ôøÍ½ÕÉ”°(€€€€¤ì(€€€…‘‘Q•…É½Ý¸  ¤…Íå¹Œì(€€€€€½¹ÑÉ½±±•È¹‘¥ÍÁ½Í” ¤ì(€€€€€…Ý…¥ÐÍ½ÕÉ”¹‘¥ÍÁ½Í•MÑÉ•…´ ¤ì(€€€ô¤ì(€€€…Ý…¥Ð½¹ÑÉ½±±•È¹¥¹¥Ñ¥…±¥é” ¤ì(€€€…Ý…¥Ð½¹ÑÉ½±±•È¹Í…Ù•…Í¡‰½…É (€€€€€½¹ÑÉ½±±•È¹‘…Í¡‰½…É¹½Áå]¥Ñ ¡±…É•…É‘Ìè½¹ÍÐ€ñMÑÉ¥¹œùì™…Ù½É¥Ñ•Ìô¤°(€€€€¤ì((€€€…Ý…¥ÐÑ•ÍÑ•È¹ÁÕµÁ]¥‘•Ð (€€€€€}±½…±¥é•‘ÁÀ (€€€€€€€¹¥µ…Ñ•‘	Õ¥±‘•È (€€€€€€€€€…¹¥µ…Ñ¥½¸è½¹ÑÉ½±±•È°(€€€€€€€€€‰Õ¥±‘•Èè€¡½¹Ñ•áÐ°¡¥±¤€ôø!½µ•½¹ÑÉ½±M¡•±°¡½¹ÑÉ½±±•Èè½¹ÑÉ½±±•È¤°(€€€€€€€€¤°(€€€€€€¤°(€€€€¤ì(€€€…Ý…¥ÐÑ•ÍÑ•È¹ÁÕµÁ¹‘M•ÑÑ±” ¤ì(€€€…Ý…¥ÐÑ•ÍÑ•È¹‘É…œ (€€€€€™¥¹¹‰åQåÁ”¡ÕÍÑ½µMÉ½±±Y¥•Ü¤¹™¥ÉÍÐ°(€€€€€½¹ÍÐ=™™Í•Ð À°€´ÐÈÀ¤°(€€€€¤ì(€€€…Ý…¥ÐÑ•ÍÑ•È¹ÁÕµÁ¹‘M•ÑÑ±” ¤ì((€€€½¹ÍÐ™…Ù½É¥Ñ•Í1…É•-•ä€ôY…±Õ•-•äñMÑÉ¥¹œø (€€€€€€‘…Í¡‰½…ÉµÍ•Ñ¥½¸µ™…Ù½É¥Ñ•Ìµ±…É”œ°(€€€€¤ì(€€€½¹ÍÐ…É•…Í½µÁ…Ñ-•ä€ôY…±Õ•-•äñMÑÉ¥¹œø ‘…Í¡‰½…ÉµÍ•Ñ¥½¸µ…É•…Ìµ½µÁ…Ðœ¤ì(€€€•áÁ•Ð¡½¹ÑÉ½±±•È¹‘…Í¡‰½…É¹±…É•…É‘Ì°½¹Ñ…¥¹Ì ™…Ù½É¥Ñ•Ìœ¤¤ì(€€€•áÁ•Ð¡™¥¹¹‰å-•ä¡™…Ù½É¥Ñ•Í1…É•-•ä¤°™¥¹‘Í=¹•]¥‘•Ð¤ì(€€€•áÁ•Ð¡™¥¹¹‰å-•ä¡…É•…Í½µÁ…Ñ-•ä¤°™¥¹‘Í=¹•]¥‘•Ð¤ì(€€€™¥¹…°±…É•]¥‘Ñ €ôÑ•ÍÑ•È¹•ÑM¥é”¡™¥¹¹‰å-•ä¡™…Ù½É¥Ñ•Í1…É•-•ä¤¤¹Ý¥‘Ñ ì(€€€™¥¹…°½µÁ…Ñ]¥‘Ñ €ôÑ•ÍÑ•È¹•ÑM¥é”¡™¥¹¹‰å-•ä¡…É•…Í½µÁ…Ñ-•ä¤¤¹Ý¥‘Ñ ì(€€€•áÁ•Ð¡±…É•]¥‘Ñ °É•…Ñ•ÉQ¡…¸¡½µÁ…Ñ]¥‘Ñ €¨€Ä¸à¤¤ì((€€€…Ý…¥Ð½¹ÑÉ½±±•È¹Í…Ù•…Í¡‰½…É (€€€€€½¹ÑÉ½±±•È¹‘…Í¡‰½…É¹½Áå]¥Ñ  (€€€€€€€±…É•…É‘Ìè½¹ÍÐ€ñMÑÉ¥¹œùì™…Ù½É¥Ñ•Ìœ°€…É•…Ìô°(€€€€€€¤°(€€€€¤ì(€€€…Ý…¥ÐÑ•ÍÑ•È¹ÁÕµÁ¹‘M•ÑÑ±” ¤ì((€€€½¹ÍÐ…É•…Í1…É•-•ä€ôY…±Õ•-•äñMÑÉ¥¹œø ‘…Í¡‰½…ÉµÍ•Ñ¥½¸µ…É•…Ìµ±…É”œ¤ì(€€€•áÁ•Ð¡™¥¹¹‰å-•ä¡…É•…Í½µÁ…Ñ-•ä¤°™¥¹‘Í9½Ñ¡¥¹œ¤ì(€€€•áÁ•Ð (€€€€€Ñ•ÍÑ•È¹•ÑM¥é”¡™¥¹¹‰å-•ä¡…É•…Í1…É•-•ä¤¤¹Ý¥‘Ñ °(€€€€€µ½É•=É1•ÍÍÅÕ…±Ì¡±…É•]¥‘Ñ °•ÁÍ¥±½¸è€À¸Ä¤°(€€€€¤ì(€€€•áÁ•Ð¡Ñ•ÍÑ•È¹Ñ…­•á•ÁÑ¥½¸ ¤°¥Í9Õ±°¤ì(€€€…Ý…¥ÐÑ•ÍÑ•È¹ÁÕµÁ]¥‘•Ð¡½¹ÍÐM¥é•‘	½à¹Í¡É¥¹¬ ¤¤ì(€€€…Ý…¥ÐÑ•ÍÑ•È¹ÁÕµÀ ¤ì(€ô¤ì((€Ñ•ÍÑ]¥‘•ÑÌ „™…¥±•!½¹¹•Ñ¥½¸ÁÉ•Í•ÉÙ•Ì•Ù•Éä™½É´™¥•±œ°€ (€€€Ñ•ÍÑ•È°(€€¤…Íå¹Œì(€€€™¥¹…°Í½ÕÉ”€ô}…­•!½µ•…Ñ…M½ÕÉ” (€€€€€Í½ÕÉ•%è€¡„µ™…¥±•µÍ½ÕÉ”œ°(€€€€€½¹½¹¹•Ðè€¡|¤€ôøÕÑÕÉ”ñ!½µ•M¹…ÁÍ¡½Ðø¹•ÉÉ½È (€€€€€€€½¹ÍÐÁÁ…¥±ÕÉ” (€€€€€€€€€½‘”èÁÁ…¥±ÕÉ•½‘”¹½™™±¥¹”°(€€€€€€€€€µ•ÍÍ…•-•äè€•ÉÉ½É9•ÑÝ½É¬œ°(€€€€€€€€¤°(€€€€€€¤°(€€€€¤ì(€€€…‘‘Q•…É½Ý¸¡Í½ÕÉ”¹‘¥ÍÁ½Í•MÑÉ•…´¤ì((€€€…Ý…¥ÐÑ•ÍÑ•È¹ÁÕµÁ]¥‘•Ð (€€€€€!½µ•½¹ÑÉ½±ÁÀ (€€€€€€€ÁÉ•™•É•¹•Ìè}ÁÉ•™•É•¹•Ì ¤°(€€€€€€€¡Õ‰É•‘•¹Ñ¥…±ÍMÑ½É”è}5•µ½Éå!Õ‰É•‘•¹Ñ¥…±ÍMÑ½É” ¤°(€€€€€€€¡½µ•ÍÍ¥ÍÑ…¹ÑÉ•‘•¹Ñ¥…±ÍMÑ½É”è}5•µ½Éå!½µ•ÍÍ¥ÍÑ…¹ÑMÑ½É” ¤°(€€€€€€€Í¹…ÁÍ¡½Ñ…¡”è}5•µ½ÉåM¹…ÁÍ¡½Ñ…¡” ¤°(€€€€€€€…ÁÁUÁ‘…Ñ•M•ÉÙ¥”è½¹ÍÐU¹ÍÕÁÁ½ÉÑ•‘ÁÁUÁ‘…Ñ•M•ÉÙ¥” ¤°(€€€€€€€‰¥½µ•ÑÉ¥ÕÑ¡•¹Ñ¥…Ñ½Èè½¹ÍÐ}U¹…Ù…¥±…‰±•	¥½µ•ÑÉ¥ÕÑ¡•¹Ñ¥…Ñ½È ¤°(€€€€€€€¡½µ•ÍÍ¥ÍÑ…¹ÑM½ÕÉ•…Ñ½Éäè€¡|°|¤€ôøÍ½ÕÉ”°(€€€€€€€•¹…‰±•A½±±¥¹œè™…±Í”°(€€€€€€¤°(€€€€¤ì(€€€…Ý…¥ÐÑ•ÍÑ•È¹ÁÕµÁ¹‘M•ÑÑ±” ¤ì(€€€…Ý…¥ÐÑ•ÍÑ•È¹•¹ÍÕÉ•Y¥Í¥‰±”¡™¥¹¹Ñ•áÐ !½µ”ÍÍ¥ÍÑ…¹Ðœ¤¤ì(€€€…Ý…¥ÐÑ•ÍÑ•È¹ÁÕµÁ¹‘M•ÑÑ±” ¤ì(€€€…Ý…¥ÐÑ•ÍÑ•È¹Ñ…À¡™¥¹¹Ñ•áÐ !½µ”ÍÍ¥ÍÑ…¹Ðœ¤¤ì(€€€…Ý…¥ÐÑ•ÍÑ•È¹ÁÕµÁ¹‘M•ÑÑ±” ¤ì((€€€™¥¹…°™¥•±‘Ì€ô™¥¹¹‰åQåÁ”¡Q•áÑ½Éµ¥•±¤ì(€€€•áÁ•Ð¡™¥•±‘Ì°™¥¹‘Í9]¥‘•ÑÌ Ì¤¤ì(€€€…Ý…¥ÐÑ•ÍÑ•È¹•¹Ñ•ÉQ•áÐ¡™¥•±‘Ì¹…Ð À¤°€5¥•Íé­…¹¥”œ¤ì(€€€…Ý…¥ÐÑ•ÍÑ•È¹•¹Ñ•ÉQ•áÐ¡™¥•±‘Ì¹…Ð Ä¤°€¡ÑÑÁÌè¼½¡„¹•á…µÁ±”¹¹•Ðœ¤ì(€€€…Ý…¥ÐÑ•ÍÑ•È¹•¹Ñ•ÉQ•áÐ¡™¥•±‘Ì¹…Ð È¤°€…‰‘•™¡¥©­±µ¹½ÁÅÉÍÑÕÙÝáåèÄÈÌÐÔØœ¤ì(€€€…Ý…¥ÐÑ•ÍÑ•È¹•¹ÍÕÉ•Y¥Í¥‰±”¡™¥¹¹‰åQåÁ”¡¥±±•‘	ÕÑÑ½¸¤¤ì(€€€…Ý…¥ÐÑ•ÍÑ•È¹ÁÕµÁ¹‘M•ÑÑ±” ¤ì(€€€…Ý…¥ÐÑ•ÍÑ•È¹Ñ…À¡™¥¹¹‰åQåÁ”¡¥±±•‘	ÕÑÑ½¸¤¤ì(€€€…Ý…¥ÐÑ•ÍÑ•È¹ÁÕµÁ¹‘M•ÑÑ±” ¤ì((€€€•áÁ•Ð¡™¥¹¹‰åQåÁ”¡Q•áÑ½Éµ¥•±¤°™¥¹‘Í9]¥‘•ÑÌ Ì¤¤ì(€€€•áÁ•Ð¡}™¥•±‘Q•áÐ¡Ñ•ÍÑ•È°€À¤°€5¥•Íé­…¹¥”œ¤ì(€€€•áÁ•Ð¡}™¥•±‘Q•áÐ¡Ñ•ÍÑ•È°€Ä¤°€¡ÑÑÁÌè¼½¡„¹•á…µÁ±”¹¹•Ðœ¤ì(€€€•áÁ•Ð¡}™¥•±‘Q•áÐ¡Ñ•ÍÑ•È°€È¤°€…‰‘•™¡¥©­±µ¹½ÁÅÉÍÑÕÙÝáåèÄÈÌÐÔØœ¤ì(€€€•áÁ•Ð¡™¥¹¹‰åQåÁ”¡¥±±•‘	ÕÑÑ½¸¤°™¥¹‘Í=¹•]¥‘•Ð¤ì(€€€•áÁ•Ð¡™¥¹¹‰å%½¸¡%½¹Ì¹•ÉÉ½É}½ÕÑ±¥¹•}É½Õ¹‘•¤°™¥¹‘Í=¹•]¥‘•Ð¤ì(€€€•áÁ•Ð¡Ñ•ÍÑ•È¹Ñ…­•á•ÁÑ¥½¸ ¤°¥Í9Õ±°¤ì(€ô¤ì)ô()½¹ÍÐMÑÉ¥¹œ}ÁÉ½™¥±•€ô€¡„µ…………………„œì)½¹ÍÐMÑÉ¥¹œ}ÁÉ½™¥±•€ô€¡„µ‰‰‰‰‰‰‰ˆœì)½¹ÍÐMÑÉ¥¹œ}ÁÉ½™¥±•€ô€¡„µŒœì()!½µ•½¹ÑÉ½±AÉ•™•É•¹•Ì}ÁÉ•™•É•¹•Ì ¤€ôø!½µ•½¹ÑÉ½±AÉ•™•É•¹•Ì (€ÍÑ½É…”èM¡…É•‘AÉ•™•É•¹•ÍÍå¹Œ ¤°(€™…±±‰…­1½…±”è½¹ÍÐ1½…±” Á°œ¤°(¤ì()!½µ•½¹ÑÉ½±½¹ÑÉ½±±•È}½¹ÑÉ½±±•È¡ì(€É•ÅÕ¥É•!½µ•½¹ÑÉ½±AÉ•™•É•¹•ÌÁÉ•™•É•¹•Ì°(€É•ÅÕ¥É•É•‘•¹Ñ¥…±ÍMÑ½É”É•‘•¹Ñ¥…±ÍMÑ½É”°(€!½µ•M¹…ÁÍ¡½Ñ…¡”üÍ¹…ÁÍ¡½Ñ…¡”°(€!½µ•ÍÍ¥ÍÑ…¹ÑM½ÕÉ•…Ñ½ÉäüÍ½ÕÉ•…Ñ½Éä°(€	¥½µ•ÑÉ¥ÕÑ¡•¹Ñ¥…Ñ½Èü‰¥½µ•ÑÉ¥ÕÑ¡•¹Ñ¥…Ñ½È°)ô¤€ôø!½µ•½¹ÑÉ½±½¹ÑÉ½±±•È (€ÁÉ•™•É•¹•ÌèÁÉ•™•É•¹•Ì°(€¡Õ‰É•‘•¹Ñ¥…±ÍMÑ½É”è}5•µ½Éå!Õ‰É•‘•¹Ñ¥…±ÍMÑ½É” ¤°(€¡½µ•ÍÍ¥ÍÑ…¹ÑÉ•‘•¹Ñ¥…±ÍMÑ½É”èÉ•‘•¹Ñ¥…±ÍMÑ½É”°(€Í¹…ÁÍ¡½Ñ…¡”èÍ¹…ÁÍ¡½Ñ…¡”€üü}5•µ½ÉåM¹…ÁÍ¡½Ñ…¡” ¤°(€‰¥½µ•ÑÉ¥ÕÑ¡•¹Ñ¥…Ñ½Èè(€€€€€‰¥½µ•ÑÉ¥ÕÑ¡•¹Ñ¥…Ñ½È€üü½¹ÍÐ}U¹…Ù…¥±…‰±•	¥½µ•ÑÉ¥ÕÑ¡•¹Ñ¥…Ñ½È ¤°(€¡½µ•ÍÍ¥ÍÑ…¹ÑM½ÕÉ•…Ñ½ÉäèÍ½ÕÉ•…Ñ½Éä°(€•¹…‰±•A½±±¥¹œè™…±Í”°(¤ì()]¥‘•Ð}±½…±¥é•‘ÁÀ¡]¥‘•Ð¡½µ”¤€ôø5…Ñ•É¥…±ÁÀ (€±½…±”è½¹ÍÐ1½…±” Á°œ¤°(€ÍÕÁÁ½ÉÑ•‘1½…±•Ìè½¹ÍÐ€ñ1½…±”ùm1½…±” Á°œ¤°1½…±” •¸œ¥t°(€±½…±¥é…Ñ¥½¹Í•±•…Ñ•Ìè½¹ÍÐ€ñ1½…±¥é…Ñ¥½¹Í•±•…Ñ”ñ=‰©•Ðøùl(€€€!½µ•½¹ÑÉ½±MÑÉ¥¹Ì¹‘•±•…Ñ”°(€€€±½‰…±5…Ñ•É¥…±1½…±¥é…Ñ¥½¹Ì¹‘•±•…Ñ”°(€€€±½‰…±]¥‘•ÑÍ1½…±¥é…Ñ¥½¹Ì¹‘•±•…Ñ”°(€€€±½‰…±ÕÁ•ÉÑ¥¹½1½…±¥é…Ñ¥½¹Ì¹‘•±•…Ñ”°(€t°(€¡½µ”è¡½µ”°(¤ì()MÑÉ¥¹œ}™¥•±‘Q•áÐ¡]¥‘•ÑQ•ÍÑ•ÈÑ•ÍÑ•È°¥¹Ð¥¹‘•à¤€ôøÑ•ÍÑ•È(€€€€¹Ý¥‘•ÐñQ•áÑ½Éµ¥•±ø¡™¥¹¹‰åQåÁ”¡Q•áÑ½Éµ¥•±¤¹…Ð¡¥¹‘•à¤¤(€€€€¹½¹ÑÉ½±±•È„(€€€€¹Ñ•áÐì()!½µ•¹Ñ¥Ñä}Í•±•Ñ¹Ñ¥Ñä (€MÑÉ¥¹œÍ½ÕÉ•%°ì(€É•ÅÕ¥É•MÑÉ¥¹œÍÑ…Ñ”°(€1¥ÍÐñMÑÉ¥¹œø½ÁÑ¥½¹Ì€ô½¹ÍÐ€ñMÑÉ¥¹œùldœ°€9%!Pt°)ô¤€ôø!½µ•¹Ñ¥Ñä (€¥èM½ÕÉ•M½Á•‘%¡Í½ÕÉ•%èÍ½ÕÉ•%°±½…±%è€Í•±•Ð¹µ½‘”œ¤°(€‘•Ù¥•%èM½ÕÉ•M½Á•‘%¡Í½ÕÉ•%èÍ½ÕÉ•%°±½…±%è€‘•Ù¥”¹½¹ÑÉ½±±•Èœ¤°(€…É•…%èM½ÕÉ•M½Á•‘%¡Í½ÕÉ•%èÍ½ÕÉ•%°±½…±%è€…É•„¹ÕÑ¥±¥Ñäœ¤°(€¹…µ”è€QÉåˆÁÉ…äœ°(€ÑåÁ”è!½µ•¹Ñ¥ÑåQåÁ”¹Í•±•Ð°(€ÍÑ…Ñ”èÍÑ…Ñ”°(€…ÑÑÉ¥‰ÕÑ•Ìè½¹ÍÐ€ñMÑÉ¥¹œ°=‰©•Ðüùíô°(€Õ¹¥Ðè€œœ°(€…Ù…¥±…‰¥±¥Ñäè¹Ñ¥ÑåÙ…¥±…‰¥±¥Ñä¹…Ù…¥±…‰±”°(€ÝÉ¥Ñ…‰±”èÑÉÕ”°(€É¥Í¬è!½µ•½µµ…¹‘I¥Í¬¹É½ÕÑ¥¹”°(€¡…¹•‘Ðè…Ñ•Q¥µ”¹ÕÑŒ ÈÀÈØ°€à°€ÄÌ°€ÄÀ¤°(€ÕÁ‘…Ñ•‘Ðè…Ñ•Q¥µ”¹ÕÑŒ ÈÀÈØ°€à°€ÄÌ°€ÄÀ¤°(€½¹ÍÑÉ…¥¹ÑÌè¹Ñ¥Ñå½¹ÍÑÉ…¥¹ÑÌ¡½ÁÑ¥½¹Ìè½ÁÑ¥½¹Ì¤°(¤ì()!½µ•¹Ñ¥Ñä}É¥Ñ¥…±¹Ñ¥Ñä¡MÑÉ¥¹œÍ½ÕÉ•%¤€ôø!½µ•¹Ñ¥Ñä (€¥èM½ÕÉ•M½Á•‘%¡Í½ÕÉ•%èÍ½ÕÉ•%°±½…±%è€¹Õµ‰•È¹Ñ…É•Ñ}Ñ•µÁ•É…ÑÕÉ”œ¤°(€‘•Ù¥•%èM½ÕÉ•M½Á•‘%¡Í½ÕÉ•%èÍ½ÕÉ•%°±½…±%è€‘•Ù¥”¹½¹ÑÉ½±±•Èœ¤°(€…É•…%èM½ÕÉ•M½Á•‘%¡Í½ÕÉ•%èÍ½ÕÉ•%°±½…±%è€…É•„¹ÕÑ¥±¥Ñäœ¤°(€¹…µ”è€Q•µÁ•É…ÑÕÉ„é…‘…¹„œ°(€ÑåÁ”è!½µ•¹Ñ¥ÑåQåÁ”¹¹Õµ‰•È°(€ÍÑ…Ñ”è€ÈÐ¸À°(€…ÑÑÉ¥‰ÕÑ•Ìè½¹ÍÐ€ñMÑÉ¥¹œ°=‰©•Ðüùíô°(€Õ¹¥Ðè€Ÿ
Áœ°(€…Ù…¥±…‰¥±¥Ñäè¹Ñ¥ÑåÙ…¥±…‰¥±¥Ñä¹…Ù…¥±…‰±”°(€ÝÉ¥Ñ…‰±”èÑÉÕ”°(€É¥Í¬è!½µ•½µµ…¹‘I¥Í¬¹É¥Ñ¥…°°(€¡…¹•‘Ðè…Ñ•Q¥µ”¹ÕÑŒ ÈÀÈØ°€à°€ÄÌ°€ÄÀ¤°(€ÕÁ‘…Ñ•‘Ðè…Ñ•Q¥µ”¹ÕÑŒ ÈÀÈØ°€à°€ÄÌ°€ÄÀ¤°(€½¹ÍÑÉ…¥¹ÑÌè½¹ÍÐ¹Ñ¥Ñå½¹ÍÑÉ…¥¹ÑÌ¡µ¥¹¥µÕ´è€Äà°µ…á¥µÕ´è€ÌÀ°ÍÑ•Àè€À¸Ô¤°(¤ì()!½µ•M¹…ÁÍ¡½Ð}Í¹…ÁÍ¡½Ð (€MÑÉ¥¹œÍ½ÕÉ•%°ì(€!½µ•¹Ñ¥Ñäü•¹Ñ¥Ñä°(€‰½½°½™™±¥¹”€ô™…±Í”°)ô¤€ôø!½µ•M¹…ÁÍ¡½Ð (€Í¡•µ…Y•ÉÍ¥½¸è!½µ•M¹…ÁÍ¡½Ð¹ÕÉÉ•¹ÑM¡•µ…Y•ÉÍ¥½¸°(€Í½ÕÉ•%èÍ½ÕÉ•%°(€Í½ÕÉ•9…µ”è€!½µ”ÍÍ¥ÍÑ…¹Ð€‘Í½ÕÉ•%œ°(€Í½ÕÉ•-¥¹è!½µ•M½ÕÉ•-¥¹¹¡½µ•ÍÍ¥ÍÑ…¹Ð°(€…É•…Ìè½¹ÍÐ€ñ!½µ•É•„ùmt°(€‘•Ù¥•Ìè½¹ÍÐ€ñ!½µ••Ù¥”ùmt°(€•¹Ñ¥Ñ¥•Ìè•¹Ñ¥Ñä€ôô¹Õ±°€ü½¹ÍÐ€ñ!½µ•¹Ñ¥Ñäùmt€è€ñ!½µ•¹Ñ¥Ñäùm•¹Ñ¥Ñåt°(€…ÕÑ½µ…Ñ¥½¹Ìè½¹ÍÐ€ñ!½µ•ÕÑ½µ…Ñ¥½¸ùmt°(€ÕÁ‘…Ñ•Ìè½¹ÍÐ€ñ!½µ•UÁ‘…Ñ”ùmt°(€Íå¹¡É½¹¥é•‘Ðè…Ñ•Q¥µ”¹ÕÑŒ ÈÀÈØ°€à°€ÄÌ°€ÄÀ¤°(€¥ÍA…ÉÑ¥…°è™…±Í”°(€¥Í=™™±¥¹”è½™™±¥¹”°(¤ì()™¥¹…°±…ÍÌ}…­•!½µ•…Ñ…M½ÕÉ”¥µÁ±•µ•¹ÑÌ!½µ•…Ñ…M½ÕÉ”ì(€}…­•!½µ•…Ñ…M½ÕÉ”¡ì(€€€É•ÅÕ¥É•Ñ¡¥Ì¹Í½ÕÉ•%°(€€€É•ÅÕ¥É•Ñ¡¥Ì¹½¹½¹¹•Ð°(€€€Ñ¡¥Ì¹½¹±½Í”°(€€€Ñ¡¥Ì¹½¹M•¹‘½µµ…¹°(€ô¤ì((€™…Ñ½Éä}…­•!½µ•…Ñ…M½ÕÉ”¹¥µµ•‘¥…Ñ”¡!½µ•M¹…ÁÍ¡½ÐÍ¹…ÁÍ¡½Ð¤€ôø(€€€€€}…­•!½µ•…Ñ…M½ÕÉ” (€€€€€€€Í½ÕÉ•%èÍ¹…ÁÍ¡½Ð¹Í½ÕÉ•%°(€€€€€€€½¹½¹¹•Ðè€¡|¤…Íå¹Œ€ôøÍ¹…ÁÍ¡½Ð°(€€€€€€¤ì((€½Ù•ÉÉ¥‘”(€™¥¹…°MÑÉ¥¹œÍ½ÕÉ•%ì(€™¥¹…°ÕÑÕÉ”ñ!½µ•M¹…ÁÍ¡½ÐøÕ¹Ñ¥½¸¡…¹•±±…Ñ¥½¹Q½­•¸…¹•±±…Ñ¥½¸¤½¹½¹¹•Ðì(€™¥¹…°ÕÑÕÉ”ñÙ½¥øÕ¹Ñ¥½¸ ¤ü½¹±½Í”ì(€™¥¹…°ÕÑÕÉ”ñÙ½¥øÕ¹Ñ¥½¸ (€€€!½µ•¹Ñ¥Ñä•¹Ñ¥Ñä°(€€€=‰©•ÐüÙ…±Õ”°(€€€…¹•±±…Ñ¥½¹Q½­•¸…¹•±±…Ñ¥½¸°(€€¤ü(€½¹M•¹‘½µµ…¹ì(€™¥¹…°MÑÉ•…µ½¹ÑÉ½±±•Èñ!½µ•¹Ñ¥Ñäø}•Ù•¹ÑÌ€ô(€€€€€MÑÉ•…µ½¹ÑÉ½±±•Èñ!½µ•¹Ñ¥Ñäø¹‰É½…‘…ÍÐ¡Íå¹ŒèÑÉÕ”¤ì(€¥¹Ð½¹¹•Ñ½Õ¹Ð€ô€Àì(€¥¹Ð±½Í•½Õ¹Ð€ô€Àì(€¥¹ÐÍ•¹‘½µµ…¹‘½Õ¹Ð€ô€Àì(€!½µ•M¹…ÁÍ¡½Ðü}±…ÍÑM¹…ÁÍ¡½Ðì((€½Ù•ÉÉ¥‘”(€MÑÉ¥¹œ•Ð‘¥ÍÁ±…å9…µ”€ôø€!½µ”ÍÍ¥ÍÑ…¹Ð€‘Í½ÕÉ•%œì((€½Ù•ÉÉ¥‘”(€!½µ•M½ÕÉ•-¥¹•Ð­¥¹€ôø!½µ•M½ÕÉ•-¥¹¹¡½µ•ÍÍ¥ÍÑ…¹Ðì((€½Ù•ÉÉ¥‘”(€MÑÉ•…´ñ!½µ•¹Ñ¥Ñäø•ÐÍÑ…Ñ•¡…¹•Ì€ôø}•Ù•¹ÑÌ¹ÍÑÉ•…´ì((€‰½½°•Ð¡…ÍI•…±Ñ¥µ•1¥ÍÑ•¹•È€ôø}•Ù•¹ÑÌ¹¡…Í1¥ÍÑ•¹•Èì((€½Ù•ÉÉ¥‘”(€ÕÑÕÉ”ñ!½µ•M¹…ÁÍ¡½Ðø½¹¹•Ð¡…¹•±±…Ñ¥½¹Q½­•¸…¹•±±…Ñ¥½¸¤…Íå¹Œì(€€€½¹¹•Ñ½Õ¹Ð¬¬ì(€€€™¥¹…°Í¹…ÁÍ¡½Ð€ô…Ý…¥Ð½¹½¹¹•Ð¡…¹•±±…Ñ¥½¸¤ì(€€€}±…ÍÑM¹…ÁÍ¡½Ð€ôÍ¹…ÁÍ¡½Ðì(€€€É•ÑÕÉ¸Í¹…ÁÍ¡½Ðì(€ô((€½Ù•ÉÉ¥‘”(€ÕÑÕÉ”ñ!½µ•M¹…ÁÍ¡½ÐøÉ•™É•Í ¡…¹•±±…Ñ¥½¹Q½­•¸…¹•±±…Ñ¥½¸¤…Íå¹Œì(€€€…¹•±±…Ñ¥½¸¹Ñ¡É½Ý%™…¹•±±• ¤ì(€€€™¥¹…°Í¹…ÁÍ¡½Ð€ô}±…ÍÑM¹…ÁÍ¡½Ðì(€€€¥˜€¡Í¹…ÁÍ¡½Ð€ôô¹Õ±°¤ì(€€€€€Ñ¡É½Ü½¹ÍÐÁÁ…¥±ÕÉ” (€€€€€€€½‘”èÁÁ…¥±ÕÉ•½‘”¹½™™±¥¹”°(€€€€€€€µ•ÍÍ…•-•äè€•ÉÉ½É9•ÑÝ½É¬œ°(€€€€€€¤ì(€€€ô(€€€É•ÑÕÉ¸Í¹…ÁÍ¡½Ðì(€ô((€Ù½¥•µ¥Ð¡!½µ•¹Ñ¥Ñä•¹Ñ¥Ñä¤€ôø}•Ù•¹ÑÌ¹…‘¡•¹Ñ¥Ñä¤ì((€½Ù•ÉÉ¥‘”(€ÕÑÕÉ”ñÙ½¥øÍ•¹‘½µµ…¹ (€€€!½µ•¹Ñ¥Ñä•¹Ñ¥Ñä°(€€€=‰©•ÐüÙ…±Õ”°(€€€…¹•±±…Ñ¥½¹Q½­•¸…¹•±±…Ñ¥½¸°(€€¤…Íå¹Œì(€€€…¹•±±…Ñ¥½¸¹Ñ¡É½Ý%™…¹•±±• ¤ì(€€€Í•¹‘½µµ…¹‘½Õ¹Ð¬¬ì(€€€…Ý…¥Ð½¹M•¹‘½µµ…¹ü¹…±°¡•¹Ñ¥Ñä°Ù…±Õ”°…¹•±±…Ñ¥½¸¤ì(€ô((€½Ù•ÉÉ¥‘”(€ÕÑÕÉ”ñ1¥ÍÐñ!¥ÍÑ½ÉåA½¥¹Ðøø±½…‘!¥ÍÑ½Éä (€€€!½µ•¹Ñ¥Ñä•¹Ñ¥Ñä°(€€€ÕÉ…Ñ¥½¸Á•É¥½°(€€€…¹•±±…Ñ¥½¹Q½­•¸…¹•±±…Ñ¥½¸°(€€¤…Íå¹Œì(€€€…¹•±±…Ñ¥½¸¹Ñ¡É½Ý%™…¹•±±• ¤ì(€€€É•ÑÕÉ¸½¹ÍÐ€ñ!¥ÍÑ½ÉåA½¥¹Ðùmtì(€ô((€½Ù•ÉÉ¥‘”(€ÕÑÕÉ”ñÙ½¥ø¥¹ÍÑ…±±UÁ‘…Ñ” (€€€!½µ•UÁ‘…Ñ”ÕÁ‘…Ñ”°(€€€…¹•±±…Ñ¥½¹Q½­•¸…¹•±±…Ñ¥½¸°(€€¤…Íå¹Œì(€€€…¹•±±…Ñ¥½¸¹Ñ¡É½Ý%™…¹•±±• ¤ì(€ô((€½Ù•ÉÉ¥‘”(€ÕÑÕÉ”ñÙ½¥ø±½Í” ¤…Íå¹Œì(€€€±½Í•½Õ¹Ð¬¬ì(€€€…Ý…¥Ð½¹±½Í”ü¹…±° ¤ì(€ô((€ÕÑÕÉ”ñÙ½¥ø‘¥ÍÁ½Í•MÑÉ•…´ ¤…Íå¹Œì(€€€¥˜€ …}•Ù•¹ÑÌ¹¥Í±½Í•¤…Ý…¥Ð}•Ù•¹ÑÌ¹±½Í” ¤ì(€ô)ô()™¥¹…°±…ÍÌ}5•µ½Éå!½µ•ÍÍ¥ÍÑ…¹ÑMÑ½É”(€€€¥µÁ±•µ•¹ÑÌÉ•‘•¹Ñ¥…±ÍMÑ½É”°!½µ•ÍÍ¥ÍÑ…¹ÑAÉ½™¥±•MÑ½É”ì(€™¥¹…°5…ÀñMÑÉ¥¹œ°€¡íMÑÉ¥¹œ¹…µ”°!½µ•ÍÍ¥ÍÑ…¹ÑÉ•‘•¹Ñ¥…±ÌÉ•‘•¹Ñ¥…±Íô¤ø(€}ÁÉ½™¥±•Ì€ô€ñMÑÉ¥¹œ°€¡íMÑÉ¥¹œ¹…µ”°!½µ•ÍÍ¥ÍÑ…¹ÑÉ•‘•¹Ñ¥…±ÌÉ•‘•¹Ñ¥…±Íô¤ùíôì(€!½µ•ÍÍ¥ÍÑ…¹ÑÉ•‘•¹Ñ¥…±Ìü}±•…äì(€MÑÉ¥¹œüÍ•±•Ñ•‘%ì(€¥¹Ð}¹•áÑ%€ô€Àì(€ÕÑÕÉ”ñÙ½¥øÕ¹Ñ¥½¸¡MÑÉ¥¹œ¥¤ü‰•™½É•M•±•Ðì(€ÕÑÕÉ”ñÙ½¥øÕ¹Ñ¥½¸¡MÑÉ¥¹œ¥¤ü‰•™½É•M…Ù”ì((€Ù½¥…‘‘AÉ½™¥±”¡ì(€€€É•ÅÕ¥É•MÑÉ¥¹œ¥°(€€€É•ÅÕ¥É•MÑÉ¥¹œ¹…µ”°(€€€É•ÅÕ¥É•MÑÉ¥¹œ¡½ÍÐ°(€ô¤ì(€€€}ÁÉ½™¥±•Ím¥‘t€ô€ (€€€€€¹…µ”è¹…µ”°(€€€€€É•‘•¹Ñ¥…±Ìè!½µ•ÍÍ¥ÍÑ…¹ÑÉ•‘•¹Ñ¥…±Ì¹Á…ÉÍ” (€€€€€€€‰…Í•UÉ°è€¡ÑÑÁÌè¼¼‘¡½ÍÐœ°(€€€€€€€…•ÍÍQ½­•¸è€œ‘í¥‘õ…‰‘•™¡¥©­±µ¹½ÁÅÉÍÑÕÙÝáåèœ°(€€€€€€¤°(€€€€¤ì(€ô((€‰½½°½¹Ñ…¥¹Ì¡MÑÉ¥¹œ¥¤€ôø}ÁÉ½™¥±•Ì¹½¹Ñ…¥¹Í-•ä¡¥¤ì((€½Ù•ÉÉ¥‘”(€ÕÑÕÉ”ñÙ½¥ø±•…È ¤…Íå¹Œì(€€€}±•…ä€ô¹Õ±°ì(€€€}ÁÉ½™¥±•Ì¹±•…È ¤ì(€€€Í•±•Ñ•‘%€ô¹Õ±°ì(€ô((€½Ù•ÉÉ¥‘”(€ÕÑÕÉ”ñ!½µ•ÍÍ¥ÍÑ…¹ÑÉ•‘•¹Ñ¥…±Ìüø±½… ¤…Íå¹Œì(€€€™¥¹…°¥€ôÍ•±•Ñ•‘%ì(€€€É•ÑÕÉ¸¥€ôô¹Õ±°€ü}±•…ä€è}ÁÉ½™¥±•Ím¥‘tü¹É•‘•¹Ñ¥…±Ìì(€ô((€½Ù•ÉÉ¥‘”(€ÕÑÕÉ”ñÙ½¥øÍ…Ù”¡!½µ•ÍÍ¥ÍÑ…¹ÑÉ•‘•¹Ñ¥…±ÌÉ•‘•¹Ñ¥…±Ì¤…Íå¹Œì(€€€}±•…ä€ôÉ•‘•¹Ñ¥…±Ìì(€ô((€½Ù•ÉÉ¥‘”(€ÕÑÕÉ”ñ1¥ÍÐñ!½µ•ÍÍ¥ÍÑ…¹ÑAÉ½™¥±”øø±¥ÍÑAÉ½™¥±•Ì ¤…Íå¹Œ€ôø(€€€€€€ñ!½µ•ÍÍ¥ÍÑ…¹ÑAÉ½™¥±”ùl(€€€€€€€™½È€¡™¥¹…°•¹ÑÉä¥¸}ÁÉ½™¥±•Ì¹•¹ÑÉ¥•Ì¤(€€€€€€€€€!½µ•ÍÍ¥ÍÑ…¹ÑAÉ½™¥±” (€€€€€€€€€€€¥è•¹ÑÉä¹­•ä°(€€€€€€€€€€€¹…µ”è•¹ÑÉä¹Ù…±Õ”¹¹…µ”°(€€€€€€€€€€€‰…Í•UÉ¤è•¹ÑÉä¹Ù…±Õ”¹É•‘•¹Ñ¥…±Ì¹‰…Í•UÉ¤°(€€€€€€€€€€¤°(€€€€€tì((€½Ù•ÉÉ¥‘”(€ÕÑÕÉ”ñ!½µ•ÍÍ¥ÍÑ…¹ÑÉ•‘•¹Ñ¥…±Ìüø±½…‘AÉ½™¥±”¡MÑÉ¥¹œ¥¤…Íå¹Œ€ôø(€€€€€}ÁÉ½™¥±•Ím¥‘tü¹É•‘•¹Ñ¥…±Ìì((€½Ù•ÉÉ¥‘”(€ÕÑÕÉ”ñMÑÉ¥¹œüøÍ•±•Ñ•‘AÉ½™¥±•% ¤…Íå¹Œ€ôøÍ•±•Ñ•‘%ì((€½Ù•ÉÉ¥‘”(€ÕÑÕÉ”ñMÑÉ¥¹œøÍ…Ù•AÉ½™¥±”¡ì(€€€É•ÅÕ¥É•!½µ•ÍÍ¥ÍÑ…¹ÑÉ•‘•¹Ñ¥…±ÌÉ•‘•¹Ñ¥…±Ì°(€€€É•ÅÕ¥É•MÑÉ¥¹œ¹…µ”°(€€€MÑÉ¥¹œüÁÉ½™¥±•%°(€ô¤…Íå¹Œì(€€€™¥¹…°¥€ô(€€€€€€€ÁÉ½™¥±•%€üü€¡„µµ•µ½Éä‘ì¡}¹•áÑ%¬¬¤¹Ñ½MÑÉ¥¹œ ¤¹Á…‘1•™Ð Ø°€œÀœ¥ôœì(€€€…Ý…¥Ð‰•™½É•M…Ù”ü¹…±°¡¥¤ì(€€€}ÁÉ½™¥±•Ím¥‘t€ô€¡¹…µ”è¹…µ”°É•‘•¹Ñ¥…±ÌèÉ•‘•¹Ñ¥…±Ì¤ì(€€€Í•±•Ñ•‘%€ô¥ì(€€€É•ÑÕÉ¸¥ì(€ô((€½Ù•ÉÉ¥‘”(€ÕÑÕÉ”ñÙ½¥øÍ•±•ÑAÉ½™¥±”¡MÑÉ¥¹œ¥¤…Íå¹Œì(€€€¥˜€ …}ÁÉ½™¥±•Ì¹½¹Ñ…¥¹Í-•ä¡¥¤¤ì(€€€€€Ñ¡É½ÜÉÕµ•¹ÑÉÉ½È¹Ù…±Õ”¡¥°€¥œ°€AÉ½™¥±”‘½•Ì¹½Ð•á¥ÍÐ¸œ¤ì(€€€ô(€€€…Ý…¥Ð‰•™½É•M•±•Ðü¹…±°¡¥¤ì(€€€Í•±•Ñ•‘%€ô¥ì(€ô((€½Ù•ÉÉ¥‘”(€ÕÑÕÉ”ñÙ½¥ø‘•±•Ñ•AÉ½™¥±”¡MÑÉ¥¹œ¥¤…Íå¹Œì(€€€}ÁÉ½™¥±•Ì¹É•µ½Ù”¡¥¤ì(€€€¥˜€¡Í•±•Ñ•‘%€ôô¥¤Í•±•Ñ•‘%€ô}ÁÉ½™¥±•Ì¹­•åÌ¹™¥ÉÍÑ=É9Õ±°ì(€ô)ô()™¥¹…°±…ÍÌ}5•µ½ÉåM¹…ÁÍ¡½Ñ…¡”¥µÁ±•µ•¹ÑÌ!½µ•M¹…ÁÍ¡½Ñ…¡”ì(€™¥¹…°5…Àñ!½µ•M½ÕÉ•-¥¹°!½µ•M¹…ÁÍ¡½ÐøÙ…±Õ•Ì€ô(€€€€€€ñ!½µ•M½ÕÉ•-¥¹°!½µ•M¹…ÁÍ¡½Ðùíôì((€½Ù•ÉÉ¥‘”(€ÕÑÕÉ”ñÙ½¥ø±•…È¡!½µ•M½ÕÉ•-¥¹­¥¹°íMÑÉ¥¹œüÍ½ÕÉ•%‘ô¤…Íå¹Œì(€€€™¥¹…°ÕÉÉ•¹Ð€ôÙ…±Õ•Ím­¥¹‘tì(€€€¥˜€¡Í½ÕÉ•%€ôô¹Õ±°ñðÕÉÉ•¹Ðü¹Í½ÕÉ•%€ôôÍ½ÕÉ•%¤Ù…±Õ•Ì¹É•µ½Ù”¡­¥¹¤ì(€ô((€½Ù•ÉÉ¥‘”(€ÕÑÕÉ”ñ!½µ•M¹…ÁÍ¡½Ðüø±½…¡!½µ•M½ÕÉ•-¥¹­¥¹°MÑÉ¥¹œÍ½ÕÉ•%¤…Íå¹Œì(€€€™¥¹…°Ù…±Õ”€ôÙ…±Õ•Ím­¥¹‘tì(€€€É•ÑÕÉ¸Ù…±Õ”ü¹Í½ÕÉ•%€ôôÍ½ÕÉ•%€üÙ…±Õ”€è¹Õ±°ì(€ô((€½Ù•ÉÉ¥‘”(€ÕÑÕÉ”ñÙ½¥øÍ…Ù”¡!½µ•M¹…ÁÍ¡½ÐÍ¹…ÁÍ¡½Ð¤…Íå¹Œì(€€€Ù…±Õ•ÍmÍ¹…ÁÍ¡½Ð¹Í½ÕÉ•-¥¹‘t€ôÍ¹…ÁÍ¡½Ðì(€ô)ô()™¥¹…°±…ÍÌ}5•µ½Éå!Õ‰É•‘•¹Ñ¥…±ÍMÑ½É”¥µÁ±•µ•¹ÑÌ!Õ‰É•‘•¹Ñ¥…±ÍMÑ½É”ì(€!Õ‰É•‘•¹Ñ¥…±Ìü}É•‘•¹Ñ¥…±Ìì((€½Ù•ÉÉ¥‘”(€ÕÑÕÉ”ñÙ½¥ø±•…È ¤…Íå¹Œ€ôø}É•‘•¹Ñ¥…±Ì€ô¹Õ±°ì((€½Ù•ÉÉ¥‘”(€ÕÑÕÉ”ñ!Õ‰É•‘•¹Ñ¥…±Ìüø±½… ¤…Íå¹Œ€ôø}É•‘•¹Ñ¥…±Ìì((€½Ù•ÉÉ¥‘”(€ÕÑÕÉ”ñÙ½¥øÍ…Ù”¡!Õ‰É•‘•¹Ñ¥…±ÌÉ•‘•¹Ñ¥…±Ì¤…Íå¹Œ€ôø(€€€€€}É•‘•¹Ñ¥…±Ì€ôÉ•‘•¹Ñ¥…±Ìì)ô()™¥¹…°±…ÍÌ}U¹…Ù…¥±…‰±•	¥½µ•ÑÉ¥ÕÑ¡•¹Ñ¥…Ñ½È(€€€¥µÁ±•µ•¹ÑÌ	¥½µ•ÑÉ¥ÕÑ¡•¹Ñ¥…Ñ½Èì(€½¹ÍÐ}U¹…Ù…¥±…‰±•	¥½µ•ÑÉ¥ÕÑ¡•¹Ñ¥…Ñ½È ¤ì((€½Ù•ÉÉ¥‘”(€ÕÑÕÉ”ñ	¥½µ•ÑÉ¥Ù…¥±…‰¥±¥Ñäø…Ù…¥±…‰¥±¥Ñä ¤…Íå¹Œ€ôø(€€€€€	¥½µ•ÑÉ¥Ù…¥±…‰¥±¥Ñä¹Õ¹…Ù…¥±…‰±”ì((€½Ù•ÉÉ¥‘”(€ÕÑÕÉ”ñ	¥½µ•ÑÉ¥ÕÑ¡½É¥é…Ñ¥½¸ø…ÕÑ¡•¹Ñ¥…Ñ”¡ì(€€€É•ÅÕ¥É•MÑÉ¥¹œ±½…±¥é•‘I•…Í½¸°(€ô¤…Íå¹Œ€ôø	¥½µ•ÑÉ¥ÕÑ¡½É¥é…Ñ¥½¸¹Õ¹…Ù…¥±…‰±”ì)ô()™¥¹…°±…ÍÌ}•±…å•‘	¥½µ•ÑÉ¥ÕÑ¡•¹Ñ¥…Ñ½È¥µÁ±•µ•¹ÑÌ	¥½µ•ÑÉ¥ÕÑ¡•¹Ñ¥…Ñ½Èì(€™¥¹…°½µÁ±•Ñ•ÈñÙ½¥øÍÑ…ÉÑ•€ô½µÁ±•Ñ•ÈñÙ½¥ø ¤ì(€™¥¹…°½µÁ±•Ñ•Èñ	¥½µ•ÑÉ¥ÕÑ¡½É¥é…Ñ¥½¸ø}É•ÍÕ±Ð€ô(€€€€€½µÁ±•Ñ•Èñ	¥½µ•ÑÉ¥ÕÑ¡½É¥é…Ñ¥½¸ø ¤ì((€½Ù•ÉÉ¥‘”(€ÕÑÕÉ”ñ	¥½µ•ÑÉ¥Ù…¥±…‰¥±¥Ñäø…Ù…¥±…‰¥±¥Ñä ¤…Íå¹Œ€ôø(€€€€€	¥½µ•ÑÉ¥Ù…¥±…‰¥±¥Ñä¹…Ù…¥±…‰±”ì((€½Ù•ÉÉ¥‘”(€ÕÑÕÉ”ñ	¥½µ•ÑÉ¥ÕÑ¡½É¥é…Ñ¥½¸ø…ÕÑ¡•¹Ñ¥…Ñ”¡ì(€€€É•ÅÕ¥É•MÑÉ¥¹œ±½…±¥é•‘I•…Í½¸°(€ô¤ì(€€€¥˜€ …ÍÑ…ÉÑ•¹¥Í½µÁ±•Ñ•¤ÍÑ…ÉÑ•¹½µÁ±•Ñ” ¤ì(€€€É•ÑÕÉ¸}É•ÍÕ±Ð¹™ÕÑÕÉ”ì(€ô((€Ù½¥½µÁ±•Ñ”¡	¥½µ•ÑÉ¥ÕÑ¡½É¥é…Ñ¥½¸Ù…±Õ”¤ì(€€€¥˜€ …}É•ÍÕ±Ð¹¥Í½µÁ±•Ñ•¤}É•ÍÕ±Ð¹½µÁ±•Ñ”¡Ù…±Õ”¤ì(€ô)ô