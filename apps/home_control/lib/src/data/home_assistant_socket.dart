import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:web_socket_channel/web_socket_channel.dart';

import '../domain/entity_ids.dart';
import '../domain/models.dart';
import 'home_assistant_channel.dart';
import 'home_assistant_network_policy.dart';

typedef HomeAssistantChannelFactory =
    WebSocketChannel Function(Uri uri, String? hostHeader);

enum HomeAssistantSocketStatus {
  disconnected,
  connecting,
  connected,
  unauthorized,
}

final class HomeAssistantSocket {
  HomeAssistantSocket(
    this.credentials, {
    this.connectionTimeout = const Duration(seconds: 12),
    this.filterAquariumOnly = true,
    HomeAssistantHostResolver? hostResolver,
    HomeAssistantChannelFactory? channelFactory,
  }) : _hostResolver = hostResolver ?? InternetAddress.lookup,
       _channelFactory =
           channelFactory ??
           ((uri, hostHeader) =>
               connectHomeAssistantChannel(uri, hostHeader: hostHeader));

  final HomeAssistantCredentials credentials;
  final Duration connectionTimeout;
  final bool filterAquariumOnly;
  final HomeAssistantHostResolver _hostResolver;
  final HomeAssistantChannelFactory _channelFactory;

  final StreamController<HaEntityState> _states =
      StreamController<HaEntityState>.broadcast();
  final StreamController<HomeAssistantSocketStatus> _statuses =
      StreamController<HomeAssistantSocketStatus>.broadcast();
  final Map<int, Completer<Object?>> _pending = <int, Completer<Object?>>{};
  final Completer<void> _authenticated = Completer<void>();

  WebSocketChannel? _channel;
  StreamSubscription<Object?>? _subscription;
  Timer? _reconnectTimer;
  Timer? _pingTimer;
  HomeAssistantSocketStatus _status = HomeAssistantSocketStatus.disconnected;
  var _generation = 0;
  var _reconnectAttempt = 0;
  var _requestId = 10;
  var _disposed = false;

  Stream<HaEntityState> get states => _states.stream;
  Stream<HomeAssistantSocketStatus> get statuses => _statuses.stream;
  HomeAssistantSocketStatus get status => _status;

  Future<void> connect() async {
    if (_disposed || _status == HomeAssistantSocketStatus.connecting) {
      return;
    }
    final generation = ++_generation;
    _reconnectTimer?.cancel();
    _reconnectTimer = null;
    await _closeChannel();
    if (_disposed || generation != _generation) {
      return;
    }

    _setStatus(HomeAssistantSocketStatus.connecting);
    try {
      final destination =
          await HomeAssistantNetworkPolicy.pinCleartextDestination(
            _webSocketUri(),
            resolver: _hostResolver,
            timeout: connectionTimeout,
          );
      final channel = _channelFactory(destination.uri, destination.hostHeader);
      _channel = channel;
      await channel.ready.timeout(connectionTimeout);
      if (_disposed || generation != _generation) {
        await channel.sink.close();
        return;
      }
      _subscription = channel.stream.cast<Object?>().listen(
        (message) => _handleMessage(message, generation),
        onError: (Object error, StackTrace stackTrace) {
          _handleDisconnect(generation);
        },
        onDone: () => _handleDisconnect(generation),
        cancelOnError: true,
      );
    } on HomeAssistantNetworkPolicyException {
      if (!_disposed && generation == _generation) {
        await _closeChannel();
        _setStatus(HomeAssistantSocketStatus.disconnected);
      }
      rethrow;
    } on Object {
      if (!_disposed && generation == _generation) {
        await _closeChannel();
        _scheduleReconnect(generation);
      }
    }
  }

  Future<HaRegistryMetadata> fetchRegistryMetadata() async {
    await _authenticated.future.timeout(connectionTimeout);
    final responses = await Future.wait<Object?>(<Future<Object?>>[
      _command('config/area_registry/list'),
      _command('config/device_registry/list'),
      _command('config/entity_registry/list'),
      _command('get_services'),
    ]);
    return HaRegistryMetadata.fromResponses(
      areas: responses[0],
      devices: responses[1],
      entities: responses[2],
      services: responses[3],
    );
  }

  Future<List<HaStatisticSample>> fetchStatistics({
    required String statisticId,
    required DateTime start,
    required DateTime end,
    HaStatisticPeriod period = HaStatisticPeriod.hour,
  }) async {
    if (!RegExp(r'^[a-z0-9_]+\.[a-z0-9_]+$').hasMatch(statisticId)) {
      throw ArgumentError.value(
        statisticId,
        'statisticId',
        'Invalid Home Assistant statistic ID.',
      );
    }
    if (!start.isBefore(end)) {
      throw ArgumentError.value(
        start,
        'start',
        'Statistics start must be before end.',
      );
    }
    await _authenticated.future.timeout(connectionTimeout);
    if (_status != HomeAssistantSocketStatus.connected) {
      throw StateError('Home Assistant socket is reconnecting.');
    }
    final response = await _command(
      'recorder/statistics_during_period',
      <String, Object?>{
        'start_time': start.toUtc().toIso8601String(),
        'end_time': end.toUtc().toIso8601String(),
        'statistic_ids': <String>[statisticId],
        'period': period.wireValue,
        'types': const <String>['mean', 'state', 'sum', 'max', 'min'],
      },
    );
    return HaStatisticSample.fromResponse(response, statisticId);
  }

  Future<void> disconnect() async {
    if (_disposed) {
      return;
    }
    _generation++;
    _reconnectTimer?.cancel();
    _reconnectTimer = null;
    _pingTimer?.cancel();
    _pingTimer = null;
    await _closeChannel();
    _setStatus(HomeAssistantSocketStatus.disconnected);
  }

  Future<void> dispose() async {
    if (_disposed) {
      return;
    }
    _disposed = true;
    _generation++;
    _reconnectTimer?.cancel();
    _pingTimer?.cancel();
    for (final completer in _pending.values) {
      if (!completer.isCompleted) {
        completer.completeError(
          const FormatException('Home Assistant socket closed.'),
        );
      }
    }
    _pending.clear();
    await _closeChannel();
    await _states.close();
    await _statuses.close();
  }

  Uri _webSocketUri() {
    final scheme = credentials.baseUri.scheme == 'https' ? 'wss' : 'ws';
    final basePath = credentials.baseUri.path.replaceFirst(RegExp(r'/+$'), '');
    return credentials.baseUri.replace(
      scheme: scheme,
      path: '$basePath/api/websocket',
      queryParameters: const <String, String>{},
      fragment: '',
    );
  }

  void _handleMessage(Object? rawMessage, int generation) {
    if (_disposed || generation != _generation) {
      return;
    }
    final String message;
    if (rawMessage is String) {
      message = rawMessage;
    } else if (rawMessage is Uint8List) {
      message = utf8.decode(rawMessage, allowMalformed: false);
    } else {
      return;
    }

    try {
      final Object? decoded = jsonDecode(message);
      final payload = _objectMap(decoded);
      if (payload == null) {
        return;
      }
      switch (payload['type']) {
        case 'auth_required':
          _send(<String, Object?>{
            'type': 'auth',
            'access_token': credentials.accessToken,
          });
        case 'auth_ok':
          _reconnectAttempt = 0;
          if (!_authenticated.isCompleted) _authenticated.complete();
          _send(<String, Object?>{
            'id': 1,
            'type': 'subscribe_events',
            'event_type': 'state_changed',
          });
        case 'auth_invalid':
          _setStatus(HomeAssistantSocketStatus.unauthorized);
          _generation++;
          unawaited(_closeChannel());
        case 'result':
          if (payload['id'] == 1 && payload['success'] == true) {
            _setStatus(HomeAssistantSocketStatus.connected);
            _startPing(generation);
          } else if (payload['id'] is int) {
            final completer = _pending.remove(payload['id']);
            if (completer != null && !completer.isCompleted) {
              if (payload['success'] == true) {
                completer.complete(payload['result']);
              } else {
                final error = _objectMap(payload['error']);
                completer.completeError(
                  FormatException(
                    error?['message']?.toString() ??
                        'Home Assistant rejected a WebSocket command.',
                  ),
                );
              }
            }
          }
        case 'event':
          _handleEvent(payload);
      }
    } on FormatException {
      return;
    }
  }

  void _handleEvent(Map<String, Object?> payload) {
    final event = _objectMap(payload['event']);
    final data = event == null ? null : _objectMap(event['data']);
    final newState = data == null ? null : _objectMap(data['new_state']);
    if (newState == null) {
      return;
    }
    try {
      final entity = HaEntityState.fromJson(newState);
      if ((!filterAquariumOnly ||
              AquaEntityIds.all.contains(entity.entityId)) &&
          !_states.isClosed) {
        _states.add(entity);
      }
    } on FormatException {
      return;
    }
  }

  void _handleDisconnect(int generation) {
    if (_disposed || generation != _generation) {
      return;
    }
    _pingTimer?.cancel();
    _pingTimer = null;
    if (_status != HomeAssistantSocketStatus.unauthorized) {
      _setStatus(HomeAssistantSocketStatus.disconnected);
      _scheduleReconnect(generation);
    }
  }

  void _scheduleReconnect(int generation) {
    if (_disposed ||
        generation != _generation ||
        _status == HomeAssistantSocketStatus.unauthorized ||
        _reconnectTimer != null) {
      return;
    }
    const delays = <int>[1, 2, 4, 8, 16, 30];
    final delayIndex = _reconnectAttempt.clamp(0, delays.length - 1);
    final delay = Duration(seconds: delays[delayIndex]);
    _reconnectAttempt++;
    _setStatus(HomeAssistantSocketStatus.disconnected);
    _reconnectTimer = Timer(delay, () {
      _reconnectTimer = null;
      if (!_disposed && generation == _generation) {
        unawaited(connect());
      }
    });
  }

  void _startPing(int generation) {
    _pingTimer?.cancel();
    _pingTimer = Timer.periodic(const Duration(seconds: 25), (_) {
      if (!_disposed &&
          generation == _generation &&
          _status == HomeAssistantSocketStatus.connected) {
        _send(<String, Object?>{'id': _requestId++, 'type': 'ping'});
      }
    });
  }

  void _send(Map<String, Object?> payload) {
    _channel?.sink.add(jsonEncode(payload));
  }

  Future<Object?> _command(
    String type, [
    Map<String, Object?> fields = const <String, Object?>{},
  ]) {
    if (_disposed || _channel == null || type.isEmpty) {
      throw StateError('Home Assistant socket is not connected.');
    }
    if (fields.containsKey('id') || fields.containsKey('type')) {
      throw ArgumentError('Command fields cannot override id or type.');
    }
    final id = _requestId++;
    final completer = Completer<Object?>();
    _pending[id] = completer;
    _send(<String, Object?>{'id': id, 'type': type, ...fields});
    return completer.future.timeout(
      connectionTimeout,
      onTimeout: () {
        _pending.remove(id);
        throw TimeoutException('Home Assistant command timed out.');
      },
    );
  }

  void _setStatus(HomeAssistantSocketStatus value) {
    if (_status == value) {
      return;
    }
    _status = value;
    if (!_statuses.isClosed) {
      _statuses.add(value);
    }
  }

  Future<void> _closeChannel() async {
    final subscription = _subscription;
    _subscription = null;
    await subscription?.cancel();
    final channel = _channel;
    _channel = null;
    if (channel != null) {
      try {
        await channel.sink.close().timeout(connectionTimeout);
      } on Object {
        // A failed handshake may not complete its sink close notification.
      }
    }
  }

  static Map<String, Object?>? _objectMap(Object? value) {
    if (value is! Map<Object?, Object?>) {
      return null;
    }
    return <String, Object?>{
      for (final entry in value.entries)
        if (entry.key is String) entry.key! as String: entry.value,
    };
  }
}

enum HaStatisticPeriod {
  fiveMinutes('5minute'),
  hour('hour'),
  day('day'),
  week('week'),
  month('month');

  const HaStatisticPeriod(this.wireValue);

  final String wireValue;
}

final class HaStatisticSample {
  const HaStatisticSample({required this.time, required this.value});

  final DateTime time;
  final double value;

  static List<HaStatisticSample> fromResponse(
    Object? response,
    String statisticId,
  ) {
    final result = HomeAssistantSocket._objectMap(response);
    final rawSamples = result?[statisticId];
    if (rawSamples is! List<Object?>) {
      return const <HaStatisticSample>[];
    }

    final samplesByTimestamp = <int, HaStatisticSample>{};
    for (final rawSample in rawSamples) {
      final sample = HomeAssistantSocket._objectMap(rawSample);
      if (sample == null) continue;
      final timestamp = _timestamp(sample['start']);
      final value = _firstFiniteNumber(<Object?>[
        sample['mean'],
        sample['state'],
        sample['sum'],
        sample['max'],
        sample['min'],
      ]);
      if (timestamp == null || value == null) continue;
      samplesByTimestamp[timestamp.millisecondsSinceEpoch] = HaStatisticSample(
        time: timestamp,
        value: value,
      );
    }
    final samples = samplesByTimestamp.values.toList(growable: false)
      ..sort((first, second) => first.time.compareTo(second.time));
    return samples;
  }

  static DateTime? _timestamp(Object? value) {
    if (value is num && value.isFinite) {
      final milliseconds = value.toInt();
      if (milliseconds < 0 || milliseconds > 8640000000000000) {
        return null;
      }
      return DateTime.fromMillisecondsSinceEpoch(
        milliseconds,
        isUtc: true,
      ).toLocal();
    }
    if (value is String) {
      return DateTime.tryParse(value)?.toLocal();
    }
    return null;
  }

  static double? _firstFiniteNumber(List<Object?> values) {
    for (final value in values) {
      final parsed = value is num
          ? value.toDouble()
          : double.tryParse(value?.toString() ?? '');
      if (parsed != null && parsed.isFinite) return parsed;
    }
    return null;
  }
}

final class HaRegistryArea {
  const HaRegistryArea({required this.id, required this.name, this.icon});

  final String id;
  final String name;
  final String? icon;
}

final class HaRegistryDevice {
  const HaRegistryDevice({
    required this.id,
    required this.name,
    required this.areaId,
    required this.manufacturer,
    required this.model,
    required this.softwareVersion,
  });

  final String id;
  final String name;
  final String? areaId;
  final String manufacturer;
  final String model;
  final String softwareVersion;
}

final class HaRegistryEntity {
  const HaRegistryEntity({
    required this.entityId,
    required this.deviceId,
    required this.areaId,
    required this.name,
  });

  final String entityId;
  final String? deviceId;
  final String? areaId;
  final String? name;
}

final class HaRegistryMetadata {
  const HaRegistryMetadata({
    required this.areas,
    required this.devices,
    required this.entities,
    required this.serviceDomains,
  });

  const HaRegistryMetadata.empty()
    : areas = const <String, HaRegistryArea>{},
      devices = const <String, HaRegistryDevice>{},
      entities = const <String, HaRegistryEntity>{},
      serviceDomains = const <String>{};

  factory HaRegistryMetadata.fromResponses({
    required Object? areas,
    required Object? devices,
    required Object? entities,
    required Object? services,
  }) {
    Map<String, Object?>? map(Object? value) =>
        HomeAssistantSocket._objectMap(value);
    final areaResult = <String, HaRegistryArea>{};
    if (areas is List<Object?>) {
      for (final raw in areas) {
        final item = map(raw);
        final id = item?['area_id'];
        final name = item?['name'];
        if (id is String &&
            name is String &&
            id.isNotEmpty &&
            name.isNotEmpty) {
          areaResult[id] = HaRegistryArea(
            id: id,
            name: name,
            icon: item?['icon'] as String?,
          );
        }
      }
    }
    final deviceResult = <String, HaRegistryDevice>{};
    if (devices is List<Object?>) {
      for (final raw in devices) {
        final item = map(raw);
        final id = item?['id'];
        if (id is! String || id.isEmpty) continue;
        final name = item?['name_by_user'] ?? item?['name'];
        deviceResult[id] = HaRegistryDevice(
          id: id,
          name: name is String && name.isNotEmpty ? name : id,
          areaId: item?['area_id'] as String?,
          manufacturer: item?['manufacturer']?.toString() ?? '',
          model: item?['model']?.toString() ?? '',
          softwareVersion: item?['sw_version']?.toString() ?? '',
        );
      }
    }
    final entityResult = <String, HaRegistryEntity>{};
    if (entities is List<Object?>) {
      for (final raw in entities) {
        final item = map(raw);
        final id = item?['entity_id'];
        if (id is! String || id.isEmpty || item?['disabled_by'] != null) {
          continue;
        }
        final name = item?['name'] ?? item?['original_name'];
        entityResult[id] = HaRegistryEntity(
          entityId: id,
          deviceId: item?['device_id'] as String?,
          areaId: item?['area_id'] as String?,
          name: name is String && name.isNotEmpty ? name : null,
        );
      }
    }
    final serviceMap = map(services);
    return HaRegistryMetadata(
      areas: Map<String, HaRegistryArea>.unmodifiable(areaResult),
      devices: Map<String, HaRegistryDevice>.unmodifiable(deviceResult),
      entities: Map<String, HaRegistryEntity>.unmodifiable(entityResult),
      serviceDomains: Set<String>.unmodifiable(
        serviceMap?.keys ?? const <String>[],
      ),
    );
  }

  final Map<String, HaRegistryArea> areas;
  final Map<String, HaRegistryDevice> devices;
  final Map<String, HaRegistryEntity> entities;
  final Set<String> serviceDomains;
}
