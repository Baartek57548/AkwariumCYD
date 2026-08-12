import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:web_socket_channel/web_socket_channel.dart';

import '../domain/entity_ids.dart';
import '../domain/models.dart';

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
  });

  final HomeAssistantCredentials credentials;
  final Duration connectionTimeout;

  final StreamController<HaEntityState> _states =
      StreamController<HaEntityState>.broadcast();
  final StreamController<HomeAssistantSocketStatus> _statuses =
      StreamController<HomeAssistantSocketStatus>.broadcast();

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
    final channel = WebSocketChannel.connect(_webSocketUri());
    _channel = channel;
    try {
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
    } on Object {
      if (!_disposed && generation == _generation) {
        await _closeChannel();
        _scheduleReconnect(generation);
      }
    }
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
      if (AquaEntityIds.all.contains(entity.entityId) && !_states.isClosed) {
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
    await channel?.sink.close();
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
