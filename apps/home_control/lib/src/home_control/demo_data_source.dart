import 'dart:async';

import 'package:home_entities/home_entities.dart';
import 'package:secure_connectivity/secure_connectivity.dart';

import 'data_source.dart';

final class DemoDataSource implements HomeDataSource {
  DemoDataSource({DateTime Function()? clock}) : _clock = clock ?? DateTime.now;

  final DateTime Function() _clock;
  final StreamController<HomeEntity> _changes =
      StreamController<HomeEntity>.broadcast();
  HomeSnapshot? _snapshot;
  Timer? _telemetryTimer;
  bool _offline = false;
  bool _alarm = false;
  bool _disposed = false;
  int _tick = 0;

  @override
  String get sourceId => 'demo';

  @override
  String get displayName => 'Home Control Demo';

  @override
  HomeSourceKind get kind => HomeSourceKind.demo;

  @override
  Stream<HomeEntity> get stateChanges => _changes.stream;

  bool get offline => _offline;
  bool get alarm => _alarm;

  @override
  Future<HomeSnapshot> connect(CancellationToken cancellation) async {
    cancellation.throwIfCancelled();
    _snapshot = _buildSnapshot();
    _startTelemetry();
    return _snapshot!;
  }

  @override
  Future<HomeSnapshot> refresh(CancellationToken cancellation) async {
    cancellation.throwIfCancelled();
    if (_offline) {
      throw const AppFailure(
        code: AppFailureCode.offline,
        messageKey: 'errorOffline',
      );
    }
    await cancellation.bind(
      Future<void>.delayed(const Duration(milliseconds: 180)),
    );
    _snapshot = _buildSnapshot(previous: _snapshot);
    return _snapshot!;
  }

  void setOffline(bool value) {
    if (_offline == value || _disposed) return;
    _offline = value;
    if (!value) {
      _snapshot = _buildSnapshot(previous: _snapshot);
      for (final entity in _snapshot!.entities.take(1)) {
        _changes.add(entity);
      }
    }
  }

  void setAlarm(bool value) {
    if (_alarm == value || _disposed) return;
    _alarm = value;
    final snapshot = _snapshot;
    if (snapshot == null) return;
    for (final localId in <String>[
      'binary_sensor.aquacyd_leak',
      'binary_sensor.aquacyd_alarm',
    ]) {
      final id = SourceScopedId(sourceId: sourceId, localId: localId);
      final entity = snapshot.entity(id);
      if (entity != null) _apply(entity.copyWith(state: value));
    }
  }

  @override
  Future<void> sendCommand(
    HomeEntity entity,
    Object? value,
    CancellationToken cancellation,
  ) async {
    cancellation.throwIfCancelled();
    if (_offline) {
      throw const AppFailure(
        code: AppFailureCode.offline,
        messageKey: 'errorOffline',
      );
    }
    if (entity.id.sourceId != sourceId || !entity.writable) {
      throw const AppFailure(
        code: AppFailureCode.permission,
        messageKey: 'errorPermission',
      );
    }
    await cancellation.bind(
      Future<void>.delayed(const Duration(milliseconds: 320)),
    );
    final normalized = switch (entity.type) {
      HomeEntityType.number ||
      HomeEntityType.inputNumber => _validatedNumber(entity, value),
      HomeEntityType.select ||
      HomeEntityType.inputSelect => _validatedOption(entity, value),
      HomeEntityType.button ||
      HomeEntityType.inputButton ||
      HomeEntityType.scene ||
      HomeEntityType.script => _clock().toIso8601String(),
      _ => value,
    };
    _apply(
      entity.copyWith(
        state: normalized,
        changedAt: _clock(),
        updatedAt: _clock(),
      ),
    );
  }

  @override
  Future<List<HistoryPoint>> loadHistory(
    HomeEntity entity,
    Duration period,
    CancellationToken cancellation,
  ) async {
    cancellation.throwIfCancelled();
    if (period <= Duration.zero || period > const Duration(days: 31)) {
      throw ArgumentError.value(period, 'period');
    }
    await cancellation.bind(
      Future<void>.delayed(const Duration(milliseconds: 220)),
    );
    final numeric =
        entity.numericValue ?? (entity.booleanValue == true ? 1 : 0);
    final now = _clock();
    return List<HistoryPoint>.generate(48, (index) {
      final phase = index % 12;
      final variation = (phase - 6) * 0.035;
      return HistoryPoint(
        time: now.subtract(period * ((47 - index) / 47)),
        value: double.parse((numeric + variation).toStringAsFixed(2)),
      );
    }, growable: false);
  }

  @override
  Future<void> installUpdate(
    HomeUpdate update,
    CancellationToken cancellation,
  ) async {
    cancellation.throwIfCancelled();
    if (update.id.sourceId != sourceId || !update.canInstall) {
      throw const AppFailure(
        code: AppFailureCode.unsupported,
        messageKey: 'errorUnsupported',
      );
    }
    await cancellation.bind(Future<void>.delayed(const Duration(seconds: 1)));
    final snapshot = _snapshot;
    if (snapshot == null) return;
    _snapshot = HomeSnapshot(
      schemaVersion: snapshot.schemaVersion,
      sourceId: snapshot.sourceId,
      sourceName: snapshot.sourceName,
      sourceKind: snapshot.sourceKind,
      areas: snapshot.areas,
      devices: snapshot.devices,
      entities: snapshot.entities,
      automations: snapshot.automations,
      updates: <HomeUpdate>[
        for (final item in snapshot.updates)
          if (item.id == update.id)
            HomeUpdate(
              id: item.id,
              name: item.name,
              currentVersion: item.latestVersion,
              latestVersion: item.latestVersion,
              phase: HomeUpdatePhase.complete,
              progress: 1,
              mandatory: item.mandatory,
              releaseNotes: item.releaseNotes,
              canInstall: false,
            )
          else
            item,
      ],
      synchronizedAt: _clock(),
      isPartial: false,
      isOffline: false,
    );
  }

  @override
  Future<void> close() async {
    if (_disposed) return;
    _disposed = true;
    _telemetryTimer?.cancel();
    await _changes.close();
  }

  void _startTelemetry() {
    _telemetryTimer?.cancel();
    _telemetryTimer = Timer.periodic(const Duration(seconds: 6), (_) {
      if (_disposed || _offline) return;
      _tick++;
      final id = SourceScopedId(
        sourceId: sourceId,
        localId: 'sensor.aquacyd_temperature',
      );
      final entity = _snapshot?.entity(id);
      if (entity == null) return;
      final value = 24.6 + ((_tick % 7) - 3) * 0.04;
      _apply(
        entity.copyWith(
          state: double.parse(value.toStringAsFixed(2)),
          updatedAt: _clock(),
        ),
      );
    });
  }

  void _apply(HomeEntity entity) {
    final snapshot = _snapshot;
    if (snapshot == null || _disposed) return;
    _snapshot = snapshot.replaceEntity(entity);
    if (!_changes.isClosed) _changes.add(entity);
  }

  double _validatedNumber(HomeEntity entity, Object? value) {
    final number = value is num
        ? value.toDouble()
        : double.tryParse(value?.toString() ?? '');
    if (number == null || !number.isFinite) {
      throw const AppFailure(
        code: AppFailureCode.invalidResponse,
        messageKey: 'errorInvalidValue',
      );
    }
    final min = entity.constraints.minimum;
    final max = entity.constraints.maximum;
    if ((min != null && number < min) || (max != null && number > max)) {
      throw const AppFailure(
        code: AppFailureCode.invalidResponse,
        messageKey: 'errorInvalidValue',
      );
    }
    return number;
  }

  String _validatedOption(HomeEntity entity, Object? value) {
    final option = value?.toString() ?? '';
    if (!entity.constraints.options.contains(option)) {
      throw const AppFailure(
        code: AppFailureCode.invalidResponse,
        messageKey: 'errorInvalidValue',
      );
    }
    return option;
  }

  HomeSnapshot _buildSnapshot({HomeSnapshot? previous}) {
    final now = _clock();
    SourceScopedId id(String localId) =>
        SourceScopedId(sourceId: sourceId, localId: localId);

    const roomNames = <String, String>{
      'living': 'Salon',
      'kitchen': 'Kuchnia',
      'bedroom': 'Sypialnia',
      'garden': 'Ogród',
      'technical': 'Pomieszczenie techniczne',
    };
    final areas = <HomeArea>[
      for (final entry in roomNames.entries)
        HomeArea(id: id('area.${entry.key}'), name: entry.value),
    ];
    final devices = <HomeDevice>[
      HomeDevice(
        id: id('device.aquacyd'),
        name: 'Akwarium salon',
        areaId: id('area.living'),
        manufacturer: 'AquaCYD',
        model: 'CYD Controller',
        softwareVersion: '5.1.0',
        available: !_offline,
        lastSeen: now,
        isAquariumController: true,
      ),
      HomeDevice(
        id: id('device.living_lights'),
        name: 'Oświetlenie salonu',
        areaId: id('area.living'),
        manufacturer: 'Demo',
        model: 'Matter Bridge',
        softwareVersion: '1.4.2',
        available: true,
        lastSeen: now,
      ),
      HomeDevice(
        id: id('device.kitchen'),
        name: 'Kuchnia',
        areaId: id('area.kitchen'),
        manufacturer: 'Demo',
        model: 'Multi Sensor',
        softwareVersion: '2.0.1',
        available: true,
        lastSeen: now,
      ),
      HomeDevice(
        id: id('device.security'),
        name: 'Bezpieczeństwo',
        areaId: id('area.technical'),
        manufacturer: 'Demo',
        model: 'Secure Hub',
        softwareVersion: '3.2.0',
        available: true,
        lastSeen: now,
      ),
    ];

    HomeEntity entity({
      required String localId,
      required String device,
      required String area,
      required String name,
      required HomeEntityType type,
      required Object? state,
      String unit = '',
      bool writable = false,
      HomeCommandRisk risk = HomeCommandRisk.routine,
      EntityAvailability availability = EntityAvailability.available,
      EntityConstraints constraints = const EntityConstraints(),
      Map<String, Object?> attributes = const <String, Object?>{},
    }) => HomeEntity(
      id: id(localId),
      deviceId: id('device.$device'),
      areaId: id('area.$area'),
      name: name,
      type: type,
      state: state,
      attributes: attributes,
      unit: unit,
      availability: _offline ? EntityAvailability.unavailable : availability,
      writable: writable,
      risk: risk,
      changedAt: now.subtract(const Duration(minutes: 3)),
      updatedAt: now,
      constraints: constraints,
    );

    final entities = <HomeEntity>[
      entity(
        localId: 'sensor.aquacyd_temperature',
        device: 'aquacyd',
        area: 'living',
        name: 'Temperatura wody',
        type: HomeEntityType.sensor,
        state: 24.7,
        unit: '°C',
      ),
      entity(
        localId: 'sensor.aquacyd_ph',
        device: 'aquacyd',
        area: 'living',
        name: 'pH wody',
        type: HomeEntityType.sensor,
        state: 6.82,
        unit: 'pH',
      ),
      entity(
        localId: 'sensor.aquacyd_ec',
        device: 'aquacyd',
        area: 'living',
        name: 'Przewodność EC',
        type: HomeEntityType.sensor,
        state: 410,
        unit: 'µS/cm',
      ),
      entity(
        localId: 'binary_sensor.aquacyd_leak',
        device: 'aquacyd',
        area: 'living',
        name: 'Wyciek',
        type: HomeEntityType.binarySensor,
        state: _alarm,
      ),
      entity(
        localId: 'binary_sensor.aquacyd_alarm',
        device: 'aquacyd',
        area: 'living',
        name: 'Alarm sterownika',
        type: HomeEntityType.binarySensor,
        state: _alarm,
      ),
      entity(
        localId: 'switch.aquacyd_front_light',
        device: 'aquacyd',
        area: 'living',
        name: 'Lampa przednia',
        type: HomeEntityType.light,
        state: true,
        writable: true,
      ),
      entity(
        localId: 'select.aquacyd_front_profile',
        device: 'aquacyd',
        area: 'living',
        name: 'Profil lampy przedniej',
        type: HomeEntityType.select,
        state: 'DAY',
        writable: true,
        constraints: const EntityConstraints(
          options: <String>['DAY', 'DAYBREAK', 'NIGHT'],
        ),
      ),
      entity(
        localId: 'switch.aquacyd_rear_light',
        device: 'aquacyd',
        area: 'living',
        name: 'Lampa tylna',
        type: HomeEntityType.light,
        state: true,
        writable: true,
      ),
      entity(
        localId: 'select.aquacyd_rear_profile',
        device: 'aquacyd',
        area: 'living',
        name: 'Profil lampy tylnej',
        type: HomeEntityType.select,
        state: 'DAYBREAK',
        writable: true,
        constraints: const EntityConstraints(
          options: <String>['DAY', 'DAYBREAK', 'NIGHT'],
        ),
      ),
      entity(
        localId: 'switch.aquacyd_filter',
        device: 'aquacyd',
        area: 'living',
        name: 'Filtr',
        type: HomeEntityType.switchEntity,
        state: true,
        writable: true,
        risk: HomeCommandRisk.consequential,
      ),
      entity(
        localId: 'switch.aquacyd_aeration',
        device: 'aquacyd',
        area: 'living',
        name: 'Napowietrzanie',
        type: HomeEntityType.switchEntity,
        state: false,
        writable: true,
        risk: HomeCommandRisk.consequential,
      ),
      entity(
        localId: 'switch.aquacyd_heater',
        device: 'aquacyd',
        area: 'living',
        name: 'Termostat',
        type: HomeEntityType.climate,
        state: 'heat',
        writable: true,
        risk: HomeCommandRisk.critical,
        attributes: const <String, Object?>{
          'temperature': 25.0,
          'current_temperature': 24.7,
        },
      ),
      entity(
        localId: 'number.aquacyd_target_temperature',
        device: 'aquacyd',
        area: 'living',
        name: 'Temperatura zadana',
        type: HomeEntityType.number,
        state: 25.0,
        unit: '°C',
        writable: true,
        risk: HomeCommandRisk.critical,
        constraints: const EntityConstraints(
          minimum: 18,
          maximum: 30,
          step: 0.1,
        ),
      ),
      entity(
        localId: 'button.aquacyd_feeder',
        device: 'aquacyd',
        area: 'living',
        name: 'Karmienie teraz',
        type: HomeEntityType.button,
        state: 'idle',
        writable: true,
        risk: HomeCommandRisk.consequential,
      ),
      entity(
        localId: 'light.living_ceiling',
        device: 'living_lights',
        area: 'living',
        name: 'Światło główne',
        type: HomeEntityType.light,
        state: true,
        writable: true,
        attributes: const <String, Object?>{'brightness': 183},
      ),
      entity(
        localId: 'media_player.living_tv',
        device: 'living_lights',
        area: 'living',
        name: 'Telewizor',
        type: HomeEntityType.mediaPlayer,
        state: 'playing',
        writable: true,
      ),
      entity(
        localId: 'sensor.kitchen_temperature',
        device: 'kitchen',
        area: 'kitchen',
        name: 'Temperatura kuchni',
        type: HomeEntityType.sensor,
        state: 21.6,
        unit: '°C',
      ),
      entity(
        localId: 'binary_sensor.kitchen_window',
        device: 'kitchen',
        area: 'kitchen',
        name: 'Okno',
        type: HomeEntityType.binarySensor,
        state: false,
      ),
      entity(
        localId: 'fan.kitchen_hood',
        device: 'kitchen',
        area: 'kitchen',
        name: 'Okap',
        type: HomeEntityType.fan,
        state: false,
        writable: true,
      ),
      entity(
        localId: 'cover.garden_awning',
        device: 'kitchen',
        area: 'garden',
        name: 'Markiza',
        type: HomeEntityType.cover,
        state: 'closed',
        writable: true,
        risk: HomeCommandRisk.consequential,
      ),
      entity(
        localId: 'lock.front_door',
        device: 'security',
        area: 'technical',
        name: 'Drzwi wejściowe',
        type: HomeEntityType.lock,
        state: 'locked',
        writable: true,
        risk: HomeCommandRisk.critical,
      ),
      entity(
        localId: 'alarm_control_panel.home',
        device: 'security',
        area: 'technical',
        name: 'Alarm domu',
        type: HomeEntityType.alarmControlPanel,
        state: 'disarmed',
        writable: true,
        risk: HomeCommandRisk.critical,
      ),
      entity(
        localId: 'camera.garden',
        device: 'security',
        area: 'garden',
        name: 'Kamera ogród',
        type: HomeEntityType.camera,
        state: 'streaming',
      ),
      entity(
        localId: 'weather.home',
        device: 'kitchen',
        area: 'garden',
        name: 'Pogoda',
        type: HomeEntityType.weather,
        state: 'partlycloudy',
        attributes: const <String, Object?>{'temperature': 19.0},
      ),
      entity(
        localId: 'person.alex',
        device: 'security',
        area: 'technical',
        name: 'Aleks',
        type: HomeEntityType.person,
        state: 'home',
      ),
      entity(
        localId: 'vacuum.downstairs',
        device: 'security',
        area: 'living',
        name: 'Odkurzacz',
        type: HomeEntityType.vacuum,
        state: 'docked',
        writable: true,
      ),
      entity(
        localId: 'scene.evening',
        device: 'living_lights',
        area: 'living',
        name: 'Wieczór',
        type: HomeEntityType.scene,
        state: 'idle',
        writable: true,
      ),
      entity(
        localId: 'automation.away',
        device: 'security',
        area: 'technical',
        name: 'Tryb poza domem',
        type: HomeEntityType.automation,
        state: true,
        writable: true,
      ),
      entity(
        localId: 'future.energy_orchestrator',
        device: 'security',
        area: 'technical',
        name: 'Przyszły typ energii',
        type: HomeEntityType.unknown,
        state: 'balanced',
        availability: EntityAvailability.unknown,
        attributes: const <String, Object?>{'future_schema': 4},
      ),
    ];
    final previousStates = <String, HomeEntity>{
      for (final item in previous?.entities ?? const <HomeEntity>[])
        item.id.value: item,
    };
    final merged = <HomeEntity>[
      for (final item in entities)
        if (previousStates[item.id.value] case final old?)
          item.copyWith(state: old.state, changedAt: old.changedAt)
        else
          item,
    ];
    return HomeSnapshot(
      schemaVersion: HomeSnapshot.currentSchemaVersion,
      sourceId: sourceId,
      sourceName: displayName,
      sourceKind: kind,
      areas: areas,
      devices: devices,
      entities: merged,
      automations: <HomeAutomation>[
        HomeAutomation(
          id: id('automation.away'),
          name: 'Tryb poza domem',
          enabled: true,
          description:
              'Wyłącza światła i uzbraja alarm po wyjściu ostatniej osoby.',
          lastTriggered: now.subtract(const Duration(days: 1)),
        ),
        HomeAutomation(
          id: id('automation.aquarium_night'),
          name: 'Noc akwarium',
          enabled: true,
          description: 'Przełącza obie lampy na profil NIGHT po zachodzie.',
          lastTriggered: now.subtract(const Duration(hours: 11)),
        ),
      ],
      updates: <HomeUpdate>[
        HomeUpdate(
          id: id('update.aquahub'),
          name: 'AquaHub ESP32-P4',
          currentVersion: '1.1.0',
          latestVersion: '1.2.0',
          phase: HomeUpdatePhase.available,
          progress: 0,
          mandatory: false,
          releaseNotes: 'Stabilniejsze WebSocket i nowe encje.',
          canInstall: true,
        ),
        HomeUpdate(
          id: id('update.aquacyd'),
          name: 'Sterownik CYD',
          currentVersion: '5.1.0',
          latestVersion: '5.1.0',
          phase: HomeUpdatePhase.idle,
          progress: 0,
          mandatory: false,
          releaseNotes: '',
          canInstall: false,
        ),
        HomeUpdate(
          id: id('update.esp32c6'),
          name: 'Bramka ESP32-C6',
          currentVersion: '0.9.0',
          latestVersion: '0.9.0',
          phase: HomeUpdatePhase.unsupported,
          progress: 0,
          mandatory: false,
          releaseNotes: 'OTA C6 nie jest jeszcze udostępnione.',
          canInstall: false,
        ),
      ],
      synchronizedAt: now,
      isPartial: false,
      isOffline: _offline,
    );
  }
}
