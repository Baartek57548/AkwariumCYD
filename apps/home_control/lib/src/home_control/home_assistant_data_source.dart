import 'dart:async';

import 'package:aquacyd_protocol/aquacyd_protocol.dart';
import 'package:home_entities/home_entities.dart';
import 'package:secure_connectivity/secure_connectivity.dart';

import '../data/credentials_store.dart';
import '../data/home_assistant_api.dart';
import '../data/home_assistant_socket.dart';
import '../domain/models.dart';
import 'data_source.dart';
import 'failure_mapping.dart';

final class HomeAssistantDataSource
    implements HomeDataSource, HomeCredentialsCleaner {
  HomeAssistantDataSource({
    required HomeAssistantCredentials credentials,
    required CredentialsStore credentialsStore,
    this.instanceId = 'ha-main',
    HomeAssistantApi Function(HomeAssistantCredentials credentials)? apiFactory,
    HomeAssistantSocket Function(HomeAssistantCredentials credentials)?
    socketFactory,
  }) : _credentials = credentials,
       _credentialsStore = credentialsStore,
       _apiFactory = apiFactory ?? HomeAssistantApi.new,
       _socketFactory =
           socketFactory ??
           ((value) => HomeAssistantSocket(value, filterAquariumOnly: false));

  final HomeAssistantCredentials _credentials;
  final CredentialsStore _credentialsStore;
  final String instanceId;
  final HomeAssistantApi Function(HomeAssistantCredentials credentials)
  _apiFactory;
  final HomeAssistantSocket Function(HomeAssistantCredentials credentials)
  _socketFactory;
  final StreamController<HomeEntity> _changes =
      StreamController<HomeEntity>.broadcast();
  HomeAssistantApi? _api;
  HomeAssistantSocket? _socket;
  StreamSubscription<HaEntityState>? _stateSubscription;
  HomeAssistantConfig? _config;
  HaRegistryMetadata _registry = const HaRegistryMetadata.empty();
  HomeSnapshot? _snapshot;
  bool _disposed = false;

  @override
  String get sourceId => instanceId;

  @override
  String get displayName => _config?.locationName ?? 'Home Assistant';

  @override
  HomeSourceKind get kind => HomeSourceKind.homeAssistant;

  @override
  Stream<HomeEntity> get stateChanges => _changes.stream;

  @override
  Future<HomeSnapshot> connect(CancellationToken cancellation) async {
    cancellation.throwIfCancelled();
    await _closeTransports();
    final api = _apiFactory(_credentials);
    _api = api;
    try {
      final results = await cancellation.bind(
        Future.wait<Object>(<Future<Object>>[
          api.fetchConfig(),
          api.fetchAllStates(),
        ]),
      );
      _config = results[0] as HomeAssistantConfig;
      final socket = _socketFactory(_credentials);
      _socket = socket;
      _stateSubscription = socket.states.listen(_handleState);
      await cancellation.bind(socket.connect());
      try {
        _registry = await cancellation.bind(socket.fetchRegistryMetadata());
      } on Object {
        _registry = const HaRegistryMetadata.empty();
      }
      final snapshot = _mapSnapshot(results[1] as Map<String, HaEntityState>);
      _snapshot = snapshot;
      return snapshot;
    } on HomeAssistantFailure catch (failure) {
      api.close();
      _api = null;
      throw mapHomeAssistantFailure(failure);
    }
  }

  @override
  Future<HomeSnapshot> refresh(CancellationToken cancellation) async {
    try {
      final states = await cancellation.bind(_requireApi().fetchAllStates());
      final snapshot = _mapSnapshot(states);
      _snapshot = snapshot;
      return snapshot;
    } on HomeAssistantFailure catch (failure) {
      throw mapHomeAssistantFailure(failure);
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
    final domain = entity.id.localId.split('.').first;
    final data = <String, Object?>{'entity_id': entity.id.localId};
    final service = _serviceFor(entity, value, data);
    try {
      await cancellation.bind(_requireApi().callService(domain, service, data));
    } on HomeAssistantFailure catch (failure) {
      throw mapHomeAssistantFailure(failure);
    }
  }

  @override
  Future<List<HistoryPoint>> loadHistory(
    HomeEntity entity,
    Duration period,
    CancellationToken cancellation,
  ) async {
    if (period >= const Duration(days: 2)) {
      final socket = _socket;
      if (socket != null) {
        try {
          final end = DateTime.now();
          final statistics = await cancellation.bind(
            socket.fetchStatistics(
              statisticId: entity.id.localId,
              start: end.subtract(period),
              end: end,
              period: _statisticsPeriod(period),
            ),
          );
          if (statistics.isNotEmpty) {
            return statistics
                .map(
                  (sample) =>
                      HistoryPoint(time: sample.time, value: sample.value),
                )
                .toList(growable: false);
          }
        } on OperationCancelled {
          rethrow;
        } on Object {
          // Recorder statistics are optional in Home Assistant. Falling back
          // preserves history for entities without long-term statistics and
          // for server versions that reject this evolving WebSocket command.
        }
      }
    }
    try {
      final values = await cancellation.bind(
        _requireApi().fetchEntityHistory(entity.id.localId, period),
      );
      return values
          .map((sample) => HistoryPoint(time: sample.time, value: sample.value))
          .toList(growable: false);
    } on HomeAssistantFailure catch (failure) {
      throw mapHomeAssistantFailure(failure);
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
      await cancellation.bind(
        _requireApi().callService('update', 'install', <String, Object?>{
          'entity_id': update.id.localId,
          'backup': true,
        }),
      );
    } on HomeAssistantFailure catch (failure) {
      throw mapHomeAssistantFailure(failure);
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

  Future<void> _closeTransports() async {
    await _stateSubscription?.cancel();
    _stateSubscription = null;
    final socket = _socket;
    _socket = null;
    await socket?.dispose();
    _api?.close();
    _api = null;
  }

  HomeAssistantApi _requireApi() {
    final api = _api;
    if (_disposed || api == null) {
      throw const AppFailure(
        code: AppFailureCode.offline,
        messageKey: 'errorOffline',
      );
    }
    return api;
  }

  void _handleState(HaEntityState state) {
    if (_disposed) return;
    final current = _snapshot;
    final mapped = _mapEntity(state);
    if (current != null) _snapshot = current.replaceEntity(mapped);
    if (!_changes.isClosed) _changes.add(mapped);
  }

  HomeSnapshot _mapSnapshot(Map<String, HaEntityState> states) {
    final mappedEntities = states.values.map(_mapEntity).toList(growable: false)
      ..sort((a, b) => a.name.compareTo(b.name));
    final areaNames = <String>{
      for (final area in _registry.areas.values) area.name,
      for (final state in states.values) _areaName(state),
    }..remove('');
    final areaIds = <String, SourceScopedId>{
      for (final name in areaNames) name: _scoped('area.${_slug(name)}'),
    };
    final deviceDescriptors = <String, _DeviceDescriptor>{};
    for (final device in _registry.devices.values) {
      final areaName = _registry.areas[device.areaId]?.name ?? '';
      deviceDescriptors['device.${device.id}'] = _DeviceDescriptor(
        id: 'device.${device.id}',
        name: device.name,
        areaName: areaName,
        manufacturer: device.manufacturer,
        model: device.model,
        softwareVersion: device.softwareVersion,
      );
    }
    for (final state in states.values) {
      final deviceId = _deviceLocalId(state);
      deviceDescriptors.putIfAbsent(
        deviceId,
        () => _DeviceDescriptor(
          id: deviceId,
          name:
              state.attributes['device_name']?.toString() ??
              _domainName(state.entityId.split('.').first),
          areaName: _areaName(state),
          manufacturer: state.attributes['manufacturer']?.toString() ?? '',
          model: state.attributes['model']?.toString() ?? '',
          softwareVersion: state.attributes['sw_version']?.toString() ?? '',
        ),
      );
    }
    final updates = mappedEntities
        .where((entity) => entity.type == HomeEntityType.update)
        .map(_mapUpdate)
        .toList(growable: false);
    final automations = mappedEntities
        .where((entity) => entity.type == HomeEntityType.automation)
        .map(
          (entity) => HomeAutomation(
            id: entity.id,
            name: entity.name,
            enabled: entity.booleanValue == true,
            description: entity.attributes['description']?.toString() ?? '',
            lastTriggered: _date(entity.attributes['last_triggered']),
          ),
        )
        .toList(growable: false);
    return HomeSnapshot(
      schemaVersion: HomeSnapshot.currentSchemaVersion,
      sourceId: sourceId,
      sourceName: displayName,
      sourceKind: kind,
      areas: areaIds.entries
          .map((entry) => HomeArea(id: entry.value, name: entry.key))
          .toList(growable: false),
      devices: <HomeDevice>[
        for (final descriptor in deviceDescriptors.values)
          HomeDevice(
            id: _scoped(descriptor.id),
            name: descriptor.name,
            areaId: areaIds[descriptor.areaName],
            manufacturer: descriptor.manufacturer,
            model: descriptor.model,
            softwareVersion: descriptor.softwareVersion,
            available: mappedEntities.any(
              (entity) =>
                  entity.deviceId?.localId == descriptor.id && entity.available,
            ),
            lastSeen: mappedEntities
                .where((entity) => entity.deviceId?.localId == descriptor.id)
                .map((entity) => entity.updatedAt)
                .whereType<DateTime>()
                .fold<DateTime?>(
                  null,
                  (latest, value) =>
                      latest == null || value.isAfter(latest) ? value : latest,
                ),
            isAquariumController: _looksLikeAquarium(
              '${descriptor.id} ${descriptor.name} ${descriptor.model}',
            ),
          ),
      ],
      entities: mappedEntities,
      automations: automations,
      updates: updates,
      synchronizedAt: DateTime.now(),
      isPartial: false,
      isOffline: false,
    );
  }

  HomeEntity _mapEntity(HaEntityState state) {
    final domain = state.entityId.split('.').first;
    final type = HomeEntityType.fromDomain(domain);
    final registryEntity = _registry.entities[state.entityId];
    final availability = switch (state.state.toLowerCase()) {
      'unavailable' => EntityAvailability.unavailable,
      'unknown' => EntityAvailability.unknown,
      _ => EntityAvailability.available,
    };
    final constraints = EntityConstraints(
      minimum:
          _number(state.attributes['min']) ??
          _number(state.attributes['min_temp']),
      maximum:
          _number(state.attributes['max']) ??
          _number(state.attributes['max_temp']),
      step:
          _number(state.attributes['step']) ??
          _number(state.attributes['target_temp_step']),
      options: _stringList(state.attributes['options']),
      supportedFeatures: _supportedFeatures(state.attributes),
    );
    final area = _areaName(state);
    final registryName = registryEntity?.name;
    final provisional = HomeEntity(
      id: _scoped(state.entityId),
      deviceId: _scoped(_deviceLocalId(state)),
      areaId: area.isEmpty ? null : _scoped('area.${_slug(area)}'),
      name: registryName?.isNotEmpty == true
          ? registryName!
          : state.friendlyName,
      type: type,
      state: state.state,
      attributes: Map<String, Object?>.unmodifiable(state.attributes),
      unit: state.unit,
      availability: availability,
      writable:
          _isWritable(type) &&
          (_registry.serviceDomains.isEmpty ||
              _registry.serviceDomains.contains(domain)),
      risk: _baseRisk(type),
      changedAt: state.lastChanged,
      updatedAt: state.lastUpdated,
      constraints: constraints,
    );
    final semanticRisk = AquariumSemantics.riskFor(
      AquariumSemantics.classify(provisional),
    );
    final risk = _higherRisk(provisional.risk, semanticRisk);
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

  String _serviceFor(
    HomeEntity entity,
    Object? value,
    Map<String, Object?> data,
  ) {
    final enabled = value == true || value?.toString() == 'on';
    switch (entity.type) {
      case HomeEntityType.lock:
        return enabled ? 'unlock' : 'lock';
      case HomeEntityType.cover:
        return enabled ? 'open_cover' : 'close_cover';
      case HomeEntityType.alarmControlPanel:
        final mode = value?.toString() ?? '';
        return switch (mode) {
          'disarmed' => 'alarm_disarm',
          'armed_home' => 'alarm_arm_home',
          'armed_away' => 'alarm_arm_away',
          'armed_night' => 'alarm_arm_night',
          _ => throw const AppFailure(
            code: AppFailureCode.invalidResponse,
            messageKey: 'errorInvalidValue',
          ),
        };
      case HomeEntityType.button:
      case HomeEntityType.inputButton:
        return 'press';
      case HomeEntityType.scene:
      case HomeEntityType.script:
        return 'turn_on';
      case HomeEntityType.number:
      case HomeEntityType.inputNumber:
        final number = value is num
            ? value.toDouble()
            : double.tryParse(value?.toString() ?? '');
        if (number == null || !number.isFinite) {
          throw const AppFailure(
            code: AppFailureCode.invalidResponse,
            messageKey: 'errorInvalidValue',
          );
        }
        data['value'] = number;
        return 'set_value';
      case HomeEntityType.select:
      case HomeEntityType.inputSelect:
        final option = value?.toString() ?? '';
        if (!entity.constraints.options.contains(option)) {
          throw const AppFailure(
            code: AppFailureCode.invalidResponse,
            messageKey: 'errorInvalidValue',
          );
        }
        data['option'] = option;
        return 'select_option';
      case HomeEntityType.text:
      case HomeEntityType.inputText:
        data['value'] = value?.toString() ?? '';
        return 'set_value';
      case HomeEntityType.climate:
        final number = value is num ? value.toDouble() : null;
        if (number != null) {
          data['temperature'] = number;
          return 'set_temperature';
        }
        return enabled ? 'turn_on' : 'turn_off';
      case HomeEntityType.vacuum:
        return enabled ? 'start' : 'return_to_base';
      default:
        return enabled ? 'turn_on' : 'turn_off';
    }
  }

  HomeUpdate _mapUpdate(HomeEntity entity) {
    final installed =
        entity.attributes['installed_version']?.toString() ??
        entity.state.toString();
    final latest = entity.attributes['latest_version']?.toString() ?? installed;
    final inProgress = entity.attributes['in_progress'];
    final progress = inProgress is num
        ? (inProgress.toDouble() / 100).clamp(0.0, 1.0)
        : 0.0;
    final available = entity.booleanValue == true || latest != installed;
    return HomeUpdate(
      id: entity.id,
      name: entity.name,
      currentVersion: installed,
      latestVersion: latest,
      phase: progress > 0
          ? HomeUpdatePhase.installing
          : available
          ? HomeUpdatePhase.available
          : HomeUpdatePhase.idle,
      progress: progress,
      mandatory: false,
      releaseNotes: entity.attributes['release_summary']?.toString() ?? '',
      canInstall: available && progress == 0,
    );
  }

  SourceScopedId _scoped(String localId) =>
      SourceScopedId(sourceId: sourceId, localId: localId);

  static bool _isWritable(HomeEntityType type) => switch (type) {
    HomeEntityType.light ||
    HomeEntityType.switchEntity ||
    HomeEntityType.climate ||
    HomeEntityType.cover ||
    HomeEntityType.lock ||
    HomeEntityType.alarmControlPanel ||
    HomeEntityType.mediaPlayer ||
    HomeEntityType.fan ||
    HomeEntityType.vacuum ||
    HomeEntityType.scene ||
    HomeEntityType.script ||
    HomeEntityType.automation ||
    HomeEntityType.button ||
    HomeEntityType.inputButton ||
    HomeEntityType.number ||
    HomeEntityType.inputNumber ||
    HomeEntityType.select ||
    HomeEntityType.inputSelect ||
    HomeEntityType.text ||
    HomeEntityType.inputText ||
    HomeEntityType.update => true,
    _ => false,
  };

  static HomeCommandRisk _baseRisk(HomeEntityType type) => switch (type) {
    HomeEntityType.lock ||
    HomeEntityType.alarmControlPanel ||
    HomeEntityType.update => HomeCommandRisk.critical,
    HomeEntityType.cover ||
    HomeEntityType.vacuum => HomeCommandRisk.consequential,
    _ => HomeCommandRisk.routine,
  };

  static HomeCommandRisk _higherRisk(
    HomeCommandRisk first,
    HomeCommandRisk second,
  ) => first.index >= second.index ? first : second;

  String _deviceLocalId(HaEntityState state) {
    final registered = _registry.entities[state.entityId]?.deviceId;
    if (registered != null && registered.isNotEmpty) {
      return 'device.$registered';
    }
    final provided = state.attributes['device_id']?.toString();
    if (provided != null &&
        RegExp(r'^[A-Za-z0-9_.:-]{1,150}$').hasMatch(provided)) {
      return 'device.$provided';
    }
    return 'device.virtual.${state.entityId.split('.').first}';
  }

  String _areaName(HaEntityState state) {
    final entity = _registry.entities[state.entityId];
    final registeredAreaId =
        entity?.areaId ?? _registry.devices[entity?.deviceId]?.areaId;
    final registeredArea = _registry.areas[registeredAreaId];
    if (registeredArea != null) return registeredArea.name;
    final value = state.attributes['area_name'] ?? state.attributes['area_id'];
    return value?.toString().trim() ?? '';
  }

  static String _slug(String value) {
    final slug = value
        .toLowerCase()
        .replaceAll(RegExp(r'[^a-z0-9]+'), '_')
        .replaceAll(RegExp(r'^_+|_+$'), '');
    return slug.isEmpty ? 'unassigned' : slug;
  }

  static String _domainName(String domain) => domain
      .split('_')
      .map(
        (part) => part.isEmpty
            ? part
            : '${part[0].toUpperCase()}${part.substring(1)}',
      )
      .join(' ');

  static double? _number(Object? value) {
    final parsed = value is num
        ? value.toDouble()
        : double.tryParse(value?.toString() ?? '');
    return parsed?.isFinite == true ? parsed : null;
  }

  static List<String> _stringList(Object? value) => value is List
      ? value
            .whereType<String>()
            .where((item) => item.isNotEmpty)
            .toList(growable: false)
      : const <String>[];

  static Set<String> _supportedFeatures(Map<String, Object?> attributes) {
    final result = <String>{};
    final raw = attributes['supported_features'];
    if (raw is num) result.add('bitmask:${raw.toInt()}');
    final modes = attributes['hvac_modes'];
    if (modes is List) result.addAll(modes.whereType<String>());
    return Set<String>.unmodifiable(result);
  }

  static DateTime? _date(Object? value) =>
      value is String ? DateTime.tryParse(value)?.toLocal() : null;

  static HaStatisticPeriod _statisticsPeriod(Duration period) {
    if (period > const Duration(days: 365)) {
      return HaStatisticPeriod.month;
    }
    if (period > const Duration(days: 90)) {
      return HaStatisticPeriod.week;
    }
    if (period > const Duration(days: 14)) {
      return HaStatisticPeriod.day;
    }
    return HaStatisticPeriod.hour;
  }

  static bool _looksLikeAquarium(String value) {
    final normalized = value.toLowerCase();
    return normalized.contains('aquacyd') ||
        normalized.contains('aquarium') ||
        normalized.contains('akwarium');
  }
}

final class _DeviceDescriptor {
  const _DeviceDescriptor({
    required this.id,
    required this.name,
    required this.areaName,
    required this.manufacturer,
    required this.model,
    required this.softwareVersion,
  });

  final String id;
  final String name;
  final String areaName;
  final String manufacturer;
  final String model;
  final String softwareVersion;
}
