import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:math' as math;

import 'package:crypto/crypto.dart';
import 'package:web_socket_channel/io.dart';
import 'package:web_socket_channel/web_socket_channel.dart';

import 'domain.dart';
import 'event_socket.dart';

HubEventSocket createHubEventSocket(HubCredentials credentials) =>
    _NativeHubEventSocket(credentials);

final class _NativeHubEventSocket implements HubEventSocket {
  _NativeHubEventSocket(this.credentials);

  final HubCredentials credentials;
  final StreamController<void> _changes = StreamController<void>.broadcast();
  WebSocketChannel? _channel;
  StreamSubscription<Object?>? _subscription;
  Timer? _reconnectTimer;
  int _generation = 0;
  int _reconnectAttempt = 0;
  bool _disposed = false;

  @override
  Stream<void> get registryChanges => _changes.stream;

  @override
  Future<void> connect() async {
    if (_disposed || _channel != null) return;
    final generation = ++_generation;
    _reconnectTimer?.cancel();
    _reconnectTimer = null;
    final expected = normalizeFingerprint(credentials.tlsFingerprint);
    final context = SecurityContext(withTrustedRoots: false);
    final client = HttpClient(context: context)
      ..connectionTimeout = const Duration(seconds: 8)
      ..badCertificateCallback = (certificate, host, port) =>
          _fingerprint(certificate) == expected;
    final basePath = credentials.baseUri.path.replaceFirst(RegExp(r'/+$'), '');
    final uri = credentials.baseUri.replace(
      scheme: 'wss',
      path: '$basePath/api/v1/events',
      queryParameters: const <String, String>{},
      fragment: '',
    );
    try {
      final channel = IOWebSocketChannel.connect(
        uri,
        headers: <String, String>{
          'authorization': 'Bearer ${credentials.accessToken}',
        },
        pingInterval: const Duration(seconds: 20),
        connectTimeout: const Duration(seconds: 10),
        customClient: client,
      );
      await channel.ready;
      if (_disposed || generation != _generation) {
        await channel.sink.close();
        client.close(force: true);
        return;
      }
      _channel = channel;
      _reconnectAttempt = 0;
      _subscription = channel.stream.cast<Object?>().listen(
        _handleMessage,
        onError: (Object error, StackTrace stackTrace) =>
            _disconnect(generation),
        onDone: () => _disconnect(generation),
        cancelOnError: true,
      );
    } on Object {
      client.close(force: true);
      if (!_disposed && generation == _generation) {
        _scheduleReconnect(generation);
      }
    }
  }

  void _handleMessage(Object? raw) {
    if (_disposed || raw is! String) return;
    try {
      final decoded = jsonDecode(raw);
      if (decoded is Map<String, Object?> &&
          decoded['type'] == 'registry_changed' &&
          !_changes.isClosed) {
        _changes.add(null);
      }
    } on FormatException {
      return;
    }
  }

  void _disconnect(int generation) {
    if (_disposed || generation != _generation) return;
    unawaited(_closeChannel());
    _scheduleReconnect(generation);
  }

  void _scheduleReconnect(int generation) {
    if (_disposed || generation != _generation || _reconnectTimer != null) {
      return;
    }
    final exponent = math.min(_reconnectAttempt, 5);
    final seconds = math.min(30, math.pow(2, exponent).toInt());
    _reconnectAttempt++;
    _reconnectTimer = Timer(Duration(seconds: seconds), () {
      _reconnectTimer = null;
      if (!_disposed && generation == _generation) unawaited(connect());
    });
  }

  Future<void> _closeChannel() async {
    await _subscription?.cancel();
    _subscription = null;
    final channel = _channel;
    _channel = null;
    await channel?.sink.close();
  }

  @override
  Future<void> close() async {
    if (_disposed) return;
    _disposed = true;
    _generation++;
    _reconnectTimer?.cancel();
    _reconnectTimer = null;
    await _closeChannel();
    await _changes.close();
  }

  static String _fingerprint(X509Certificate certificate) {
    final normalizedPem = certificate.pem
        .replaceAll('-----BEGIN CERTIFICATE-----', '')
        .replaceAll('-----END CERTIFICATE-----', '')
        .replaceAll(RegExp(r'\s'), '');
    return sha256.convert(base64Decode(normalizedPem)).toString().toUpperCase();
  }
}
