import 'dart:async';

import 'package:aquacyd_protocol/aquacyd_protocol.dart';
import 'package:home_entities/home_entities.dart';
import 'package:secure_connectivity/secure_connectivity.dart';

import '../aquahub/api.dart';
import '../aquahub/credentials_store.dart';
import '../aquahub/domain.dart';
import '../aquahub/event_socket.dart';
import 'data_source.dart';
import 'failure_mapping.dart';

final class AquaHubDataSource
    implements HomeDataSource, HomeCredentialsCleaner {
  AquaHubDataSource({
    required HubCredentials credentials,
    required HubCredentialsStore credentialsStore,
    HubApi Function(HubCredentials credentials)? apiFactory,
  }) : _credentials = credentials,
       _credentialsStore = credentialsStore,
       _apiFactory = apiFactory ?? HubApi.authenticated;

  final HubCredentials _credentials;
  final HubCredentialsStore _credentialsStore;
  final HubApi Function(HubCredentials credentials) _apiFactory;
  final StreamController<HomeEntity> _changes =
      StreamController<HomeEntity>.broadcast();
  HubApi? _api;
  HubEventSocket? _eventSocket;
  StreamSubscription<void>? _eventSubscription;
  Timer? _eventRefreshDebounce;
  bool _disposed = false;

  @override
  String get sourceId => 'aquahub';

  @override
  String get displayName => 'AquaHub';

  @override
  HomeSourceKind get kind => HomeSourceKind.aquaHub;

  @override
  Stream<HomeEntity> get stateChanges => _changes.stream;

  @override
  Future<HomeSnapshot> connect(CancellationToken cancellation) async {
    cancellation.throwIfCancelled();
    await _closeTransports();
    _api = _apiFactory(_credentials);
    final snapshot = await refresh(cancellation);
    final socket = createHubEventSocket(_credentials);
    _eventSocket = socket;
    _eventSubscription = socket.registryChanges.listen((_) {
      _eventRefreshDebounce?.cancel();
      _eventRefreshDebounce = Timer(
        const Duration(milliseconds: 250),
        () => unawaited(_refreshFromEvent()),
      );
    });
    unawaited(socket.connect());
    return snapshot;
  }

  @override
  Future<HomeSnapshot> refresh(CancellationToken cancellation) async {
    final api = _requireApi();
    try {
      final results = await cancellation.bind(
        Future.wait<Object>(<Future<Object>>[
          api.fetchSystem(),
          api.fetchDevices(),
          api.fetchEntities(),
          api.fetchAutomations(),
          api.fetchUpdateStatus(),
        ]),
      );
      final snapshot = _mapSnapshot(
        system: results[0] as HubSystem,
        devices: results[1] as List<HubDevice>,
        entities: results[2] as List<HubEntity>,
        automations: results[3] as HubAutomationCollection,
        update: results[4] as HubUpdateStatus,
      );
      return snapshot;
    } on HubFailure catch (failure) {
      throw mapAquaHubFailure(failure);
    }
  }

  @override
  Future<void> sendCommand(
    HomeEntity entity,
    Object? value,
    CancellationToken cancellation,
  ) async {
    if (entity.id.sourceId != sourceId || !entity.writable) {
      throw const AppFailure(
        code: AppFailureCode.permission,
        messageKey: 'errorPermission',
      );
    }
    try {
      await cancellation.bind(
        _requireApi().sendCommand(entity.id.localId, value),
      );
    } on HubFailure catch (failure) {
      throw mapAquaHubFailure(failure);
    }
  }

  @override
  Future<List<HistoryPoint>> loadHistory(
    HomeEntity entity,
    Duration period,
    CancellationToken cancellation,
  ) async {
    if (entity.id.sourceId != sourceId) {
      throw ArgumentError.value(entity.id, 'entity');
    }
    try {
      final points = await cancellation.bind(
        _requireApi().fetchHistory(entity.id.localId),
      );
      final now = DateTime.now();
      return points
          .map(
            (point) => HistoryPoint(
              time: now.subtract(point.changedAt),
              value: point.value,
            ),
          )
          .where((point) => now.difference(point.time) <= period)
          .toList(growable: false);
    } on HubFailure catch (failure) {
      throw mapAquaHubFailure(failure);
    }
  }

  @override
  Future<void> installUpdate(
    HomeUpdate update,
    CancellationToken cancellation,
  ) async {
    if (update.id.sourceId != sourceId || !update.canInstall) {
      throw const AppFailure(
        code: AppFailureCode.unsupported,
        messageKey: 'errorUnsupported',
      );
    }
    try {
      await cancellation.bind(_requireApi().installUpdate());
    } on HubFailure catch (failure) {
      throw mapAquaHubFailure(failure);
    }
  }

  @override
  Future<void> clearCredentials() => _credentialsStore.clear();

  @override
  Future<void> close() async {
    if (_disposed) return;
    _disposed = true;
    await _closeTransports();
    await _changes.close();
  }

  Future<void> _refreshFromEvent() async {
    if (_disposed) return;
    try {
      final snapshot = await refresh(CancellationToken());
      for (final entity in snapshot.entities) {
        if (!_changes.isClosed) _changes.add(entity);
      }
    } on Object {
      return;
    }
  }

  Future<void> _closeTransports() async {
    _eventRefreshDebounce?.cancel();
    _eventRefreshDebounce = null;
    await _eventSubscription?.cancel();
    _eventSubscription = null;
    final socket = _eventSocket;
    _eventSocket = null;
    await socket?.close();
    _api?.close();
    _api = null;
  }

  HubApi _requireApi() {
    if (_disposed || _api == null) {
      throw const AppFailure(
        code: AppFailureCode.offline,
        messageKey: 'errorOffline',
      );
    }
    return _api!;
  }

  HomeSnapshot _mapSnapshot({
    required HubSystem system,
    required List<HubDevice> devices,
    required List<HubEntity> entities,
    required HubAutomationCollection automations,
    required HubUpdateStatus update,
  }) {
    final now = DateTime.now();
    SourceScopedId id(String localId) =>
        SourceScopedId(sourceId: sourceId, localId: localId);
    final areaNames =
        devices
            .map((device) => device.area.trim())
            .where((area) => area.isNotEmpty)
            .toSet()
            .toList(growable: false)
          ..sort();
    String areaLocalId(String name) =>
        'area.${name.toLowerCase().replaceAll(RegExp(r'[^a-z0-9]+'), '_')}';
    final areaByName = <String, SourceScopedId>{
      for (final name in areaNames) name: id(areaLocalId(name)),
    };
    final mappedDevices = <HomeDevice>[
      for (final device in devices)
        HomeDevice(
          id: id(device.id),
          name: device.name,
          areaId: areaByName[device.area],
          manufacturer: device.manufacturer,
          model: device.model,
          softwareVersion: device.firmwareVersion,
          available: device.online,
          lastSeen: now.subtract(device.lastSeen),
          isAquariumController: _looksLikeAquarium(
            '${device.id} ${device.name} ${device.model}',
          ),
        ),
    ];
    final deviceById = <String, HomeDevice>{
      for (final device in mappedDevices) device.id.localId: device,
    };
    final mappedEntities = <HomeEntity>[
      for (final entity in entities)
        _mapEntity(entity, deviceById[entity.deviceId], now),
    ];
    final mappedAutomations = <HomeAutomation>[
      for (final rule in automations.rules)
        HomeAutomation(
          id: id(rule.id),
          name: rule.name,
          enabled: rule.enabled,
          description:
              '${rule.trigger.entityId} ${rule.trigger.comparison.wireName} → ${rule.action.entityId}',
          lastTriggered: null,
        ),
    ];
    return HomeSnapshot(
      schemaVersion: HomeSnapshot.currentSchemaVersion,
      sourceId: sourceId,
      sourceName: displayName,
      sourceKind: kind,
      areas: <HomeArea>[
        for (final entry in areaByName.entries)
          HomeArea(id: entry.value, name: entry.key),
      ],
      devices: mappedDevices,
      entities: mappedEntities,
      automations: mappedAutomations,
      updates: <HomeUpdate>[_mapUpdate(update)],
      synchronizedAt: now,
      isPartial:
          system.deviceCount != devices.length ||
          system.entityCount != entities.length,
      isOffline: false,
    );
  }

  HomeEntity _mapEntity(HubEntity entity, HomeDevice? device, DateTime now) {
    final type = switch (entity.kind) {
      HubEntityKind.sensor => HomeEntityType.sensor,
      HubEntityKind.binarySensor => HomeEntityType.binarySensor,
      HubEntityKind.switchEntity => HomeEntityType.switchEntity,
      HubEntityKind.number => HomeEntityType.number,
      HubEntityKind.select => HomeEntityType.select,
      HubEntityKind.button => HomeEntityType.button,
      HubEntityKind.light => HomeEntityType.light,
      HubEntityKind.unknown => HomeEntityType.unknown,
    };
    final provisional = HomeEntity(
      id: SourceScopedId(sourceId: sourceId, localId: entity.id),
      deviceId: SourceScopedId(sourceId: sourceId, localId: entity.deviceId),
      areaId: device?.areaId,
      name: entity.name,
      type: type,
      state: entity.state,
      attributes: const <String, Object?>{},
      unit: entity.unit,
      availability: device?.available == false
          ? EntityAvailability.unavailable
          : EntityAvailability.available,
      writable: entity.writable,
      risk: entity.critical
          ? HomeCommandRisk.critical
          : HomeCommandRisk.routine,
      changedAt: now.subtract(entity.changedAt),
      updatedAt: now.subtract(entity.updatedAt),
      constraints: EntityConstraints(
        minimum: entity.minimum,
        maximum: entity.maximum,
        step: entity.step,
        options: entity.options,
      ),
    );
    final semanticRisk = AquariumSemantics.riskFor(
      AquariumSemantics.classify(provisional),
    );
    final risk = provisional.risk.index >= semanticRisk.index
        ? provisional.risk
        : semanticRisk;
    if (risk == provisional.risk) return provisional;
    return HomeEntity(
      id: provisional.id,
      deviceId: provisional.deviceId,
      areaId: provisional.areaId,
      name: provisional.name,
      type: provisional.type,
      state: provisional.state,
      attributes: provisional.attributes,
      unit: provisional.unit,
      availability: provisional.availability,
      writable: provisional.writable,
      risk: risk,
      changedAt: provisional.changedAt,
      updatedAt: provisional.updatedAt,
      constraints: provisional.constraints,
    );
  }

  HomeUpdate _mapUpdate(HubUpdateStatus update) {
    final release = update.release;
    return HomeUpdate(
      id: SourceScopedId(sourceId: sourceId, localId: 'update.esp32p4'),
      name: 'AquaHub ESP32-P4',
      currentVersion: update.currentVersion,
      latestVersion: release?.version ?? update.currentVersion,
      phase: switch (update.phase) {
        HubUpdatePhase.disabled => HomeUpdatePhase.unsupported,
        HubUpdatePhase.idle || HubUpdatePhase.upToDate => HomeUpdatePhase.idle,
        HubUpdatePhase.checking => HomeUpdatePhase.checking,
        HubUpdatePhase.available => HomeUpdatePhase.available,
        HubUpdatePhase.downloading => HomeUpdatePhase.downloading,
        HubUpdatePhase.verifying => HomeUpdatePhase.verifying,
        HubUpdatePhase.rebooting => HomeUpdatePhase.rebooting,
        HubUpdatePhase.failed => HomeUpdatePhase.failed,
      },
      progress: update.progressPercent / 100,
      mandatory: release?.mandatory ?? false,
      releaseNotes: release?.notes ?? '',
      canInstall: update.phase == HubUpdatePhase.available,
      error: update.error.isEmpty ? null : update.error,
    );
  }

  static bool _looksLikeAquarium(String value) {
    final normalized = value.toLowerCase();
    return normalized.contains('aquacyd') ||
        normalized.contains('aquarium') ||
        normalized.contains('akwarium');
  }
}
