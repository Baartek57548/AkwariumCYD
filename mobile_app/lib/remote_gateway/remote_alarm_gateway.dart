import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter_secure_storage/flutter_secure_storage.dart';
import 'package:shared_preferences/shared_preferences.dart';

final class RemoteAlarmGatewayConfiguration {
  RemoteAlarmGatewayConfiguration({
    required Uri baseUrl,
    required String deviceId,
    required this.enabled,
    required this.hasViewerToken,
  }) : baseUrl = _validateBaseUrl(baseUrl),
       deviceId = _validateDeviceId(deviceId);

  final Uri baseUrl;
  final String deviceId;
  final bool enabled;
  final bool hasViewerToken;

  Uri get healthUri {
    final root = baseUrl.path.endsWith('/')
        ? baseUrl.path.substring(0, baseUrl.path.length - 1)
        : baseUrl.path;
    return baseUrl.replace(
      path: '$root/api/v1/devices/${Uri.encodeComponent(deviceId)}/health',
    );
  }

  RemoteAlarmGatewayConfiguration copyWith({
    Uri? baseUrl,
    String? deviceId,
    bool? enabled,
    bool? hasViewerToken,
  }) {
    return RemoteAlarmGatewayConfiguration(
      baseUrl: baseUrl ?? this.baseUrl,
      deviceId: deviceId ?? this.deviceId,
      enabled: enabled ?? this.enabled,
      hasViewerToken: hasViewerToken ?? this.hasViewerToken,
    );
  }

  static Uri _validateBaseUrl(Uri value) {
    if (value.scheme != 'https' ||
        value.host.isEmpty ||
        value.userInfo.isNotEmpty ||
        value.hasQuery ||
        value.hasFragment) {
      throw const FormatException(
        'Bramka alarmowa wymaga bazowego adresu HTTPS bez parametrów.',
      );
    }
    final port = value.hasPort ? value.port : 443;
    if (port < 1 || port > 65535) {
      throw const FormatException('Port bramki alarmowej jest nieprawidłowy.');
    }
    final normalizedPath = value.path == '/'
        ? ''
        : value.path.replaceFirst(RegExp(r'/+$'), '');
    return value.replace(path: normalizedPath);
  }

  static String _validateDeviceId(String value) {
    final normalized = value.trim();
    if (!RegExp(r'^[A-Za-z0-9_-]{4,64}$').hasMatch(normalized)) {
      throw const FormatException(
        'Identyfikator urządzenia musi mieć 4–64 znaki: litery, cyfry, _ lub -.',
      );
    }
    return normalized;
  }

  static String validateViewerToken(String value) {
    final normalized = value.trim();
    if (normalized.length < 24 ||
        normalized.length > 512 ||
        normalized.contains(RegExp(r'[\s\u0000-\u001f]'))) {
      throw const FormatException(
        'Token viewer musi mieć 24–512 znaków bez spacji.',
      );
    }
    return normalized;
  }
}

final class RemoteGatewayProvisioningRequest {
  factory RemoteGatewayProvisioningRequest({
    required Uri baseUrl,
    required String deviceId,
    required String hmacSecret,
    required bool enabled,
  }) {
    final configuration = RemoteAlarmGatewayConfiguration(
      baseUrl: baseUrl,
      deviceId: deviceId,
      enabled: false,
      hasViewerToken: false,
    );
    return RemoteGatewayProvisioningRequest._(
      baseUrl: configuration.baseUrl,
      deviceId: configuration.deviceId,
      hmacSecret: validateHmacSecret(hmacSecret),
      enabled: enabled,
    );
  }

  const RemoteGatewayProvisioningRequest._({
    required this.baseUrl,
    required this.deviceId,
    required this.hmacSecret,
    required this.enabled,
  });

  final Uri baseUrl;
  final String deviceId;

  /// Sekret żyje tylko przez czas obsługi formularza i pojedynczego polecenia
  /// BLE. Ten model nie udostępnia serializacji ani trwałego magazynu.
  final String hmacSecret;
  final bool enabled;

  Map<String, Object?> get actionPayload => <String, Object?>{
    'baseUrl': baseUrl.toString(),
    'deviceId': deviceId,
    'hmacSecret': hmacSecret,
    'enabled': enabled,
  };

  static String validateHmacSecret(String value) {
    final normalized = value.trim();
    if (normalized.length < 44 ||
        normalized.length > 88 ||
        normalized.length % 4 != 0 ||
        !RegExp(r'^[A-Za-z0-9+/]+={0,2}$').hasMatch(normalized)) {
      throw const FormatException(
        'Sekret HMAC musi być poprawnym Base64 o długości 32–64 bajtów.',
      );
    }
    late final List<int> decoded;
    try {
      decoded = base64Decode(normalized);
    } on FormatException {
      throw const FormatException(
        'Sekret HMAC musi być poprawnym Base64 o długości 32–64 bajtów.',
      );
    }
    if (decoded.length < 32 ||
        decoded.length > 64 ||
        base64Encode(decoded) != normalized) {
      throw const FormatException(
        'Sekret HMAC musi być poprawnym Base64 o długości 32–64 bajtów.',
      );
    }
    return normalized;
  }
}

abstract interface class RemoteAlarmGatewaySecretStore {
  Future<String?> readViewerToken();
  Future<void> writeViewerToken(String value);
  Future<void> clearViewerToken();
}

final class SecureRemoteAlarmGatewaySecretStore
    implements RemoteAlarmGatewaySecretStore {
  SecureRemoteAlarmGatewaySecretStore({
    FlutterSecureStorage storage = const FlutterSecureStorage(),
  }) : _storage = storage;

  static const String _viewerTokenKey = 'remote_alarm_gateway.viewer_token.v1';

  final FlutterSecureStorage _storage;

  @override
  Future<String?> readViewerToken() async {
    final value = await _storage.read(key: _viewerTokenKey);
    if (value == null) return null;
    try {
      return RemoteAlarmGatewayConfiguration.validateViewerToken(value);
    } on FormatException {
      await clearViewerToken();
      return null;
    }
  }

  @override
  Future<void> writeViewerToken(String value) {
    return _storage.write(
      key: _viewerTokenKey,
      value: RemoteAlarmGatewayConfiguration.validateViewerToken(value),
    );
  }

  @override
  Future<void> clearViewerToken() => _storage.delete(key: _viewerTokenKey);
}

final class RemoteAlarmGatewayCredentials {
  const RemoteAlarmGatewayCredentials({
    required this.configuration,
    required this.viewerToken,
  });

  final RemoteAlarmGatewayConfiguration configuration;
  final String viewerToken;
}

final class RemoteAlarmGatewayStore {
  RemoteAlarmGatewayStore({
    SharedPreferencesAsync? preferences,
    RemoteAlarmGatewaySecretStore? secrets,
  }) : _injectedPreferences = preferences,
       _secrets = secrets ?? SecureRemoteAlarmGatewaySecretStore();

  static const String _baseUrlKey = 'remote_alarm_gateway.base_url.v1';
  static const String _deviceIdKey = 'remote_alarm_gateway.device_id.v1';
  static const String _enabledKey = 'remote_alarm_gateway.enabled.v1';

  final SharedPreferencesAsync? _injectedPreferences;
  SharedPreferencesAsync? _defaultPreferences;
  final RemoteAlarmGatewaySecretStore _secrets;

  SharedPreferencesAsync get _preferences =>
      _injectedPreferences ??
      (_defaultPreferences ??= SharedPreferencesAsync());

  Future<RemoteAlarmGatewayConfiguration?> load() async {
    final values = await Future.wait<Object?>(<Future<Object?>>[
      _preferences.getString(_baseUrlKey),
      _preferences.getString(_deviceIdKey),
      _preferences.getBool(_enabledKey),
      _secrets.readViewerToken(),
    ]);
    final rawUrl = values[0];
    final rawDeviceId = values[1];
    final enabled = values[2] == true;
    final viewerToken = values[3];
    if (rawUrl is! String || rawDeviceId is! String) return null;
    final uri = Uri.tryParse(rawUrl);
    if (uri == null) return null;
    try {
      return RemoteAlarmGatewayConfiguration(
        baseUrl: uri,
        deviceId: rawDeviceId,
        enabled: enabled && viewerToken is String,
        hasViewerToken: viewerToken is String,
      );
    } on FormatException {
      return null;
    }
  }

  Future<void> save(
    RemoteAlarmGatewayConfiguration configuration, {
    String? newViewerToken,
  }) async {
    final existingToken = await _secrets.readViewerToken();
    final normalizedNewToken = newViewerToken?.trim();
    if (normalizedNewToken != null && normalizedNewToken.isNotEmpty) {
      await _secrets.writeViewerToken(normalizedNewToken);
    }
    final tokenAvailable =
        (normalizedNewToken != null && normalizedNewToken.isNotEmpty) ||
        existingToken != null;
    if (configuration.enabled && !tokenAvailable) {
      throw StateError(
        'Nie można włączyć bramki bez bezpiecznie zapisanego tokenu viewer.',
      );
    }
    await Future.wait<void>(<Future<void>>[
      _preferences.setString(_baseUrlKey, configuration.baseUrl.toString()),
      _preferences.setString(_deviceIdKey, configuration.deviceId),
      _preferences.setBool(_enabledKey, configuration.enabled),
    ]);
  }

  Future<void> setEnabled(bool enabled) async {
    if (enabled && await _secrets.readViewerToken() == null) {
      throw StateError('Najpierw zapisz token viewer bramki.');
    }
    await _preferences.setBool(_enabledKey, enabled);
  }

  Future<void> clearViewerToken() async {
    await _secrets.clearViewerToken();
    await _preferences.setBool(_enabledKey, false);
  }

  Future<RemoteAlarmGatewayCredentials?> loadCredentials({
    bool includeDisabled = false,
  }) async {
    final configuration = await load();
    if (configuration == null || (!includeDisabled && !configuration.enabled)) {
      return null;
    }
    final token = await _secrets.readViewerToken();
    if (token == null) return null;
    return RemoteAlarmGatewayCredentials(
      configuration: configuration,
      viewerToken: token,
    );
  }
}

final class RemoteAlarmGatewayTestResult {
  const RemoteAlarmGatewayTestResult({
    required this.success,
    required this.message,
    this.roundTrip,
  });

  final bool success;
  final String message;
  final Duration? roundTrip;
}

final class RemoteAlarmGatewayClient {
  RemoteAlarmGatewayClient({
    HttpClient Function()? clientFactory,
    this.timeout = const Duration(seconds: 8),
  }) : _clientFactory = clientFactory ?? HttpClient.new;

  static const int maximumResponseBytes = 16 * 1024;

  final HttpClient Function() _clientFactory;
  final Duration timeout;

  Future<RemoteAlarmGatewayTestResult> test(
    RemoteAlarmGatewayCredentials credentials,
  ) async {
    final stopwatch = Stopwatch()..start();
    final client = _clientFactory()
      ..connectionTimeout = timeout
      ..idleTimeout = timeout;
    try {
      final request = await client
          .getUrl(credentials.configuration.healthUri)
          .timeout(timeout);
      request
        ..followRedirects = false
        ..headers.set(
          HttpHeaders.authorizationHeader,
          'Bearer ${credentials.viewerToken}',
        )
        ..headers.set(HttpHeaders.acceptHeader, 'application/json');
      final response = await request.close().timeout(timeout);
      final bytes = <int>[];
      await for (final chunk in response.timeout(timeout)) {
        bytes.addAll(chunk);
        if (bytes.length > maximumResponseBytes) {
          return RemoteAlarmGatewayTestResult(
            success: false,
            message: 'Odpowiedź bramki przekroczyła bezpieczny limit.',
            roundTrip: stopwatch.elapsed,
          );
        }
      }
      if (response.isRedirect) {
        return RemoteAlarmGatewayTestResult(
          success: false,
          message: 'Bramka nie może przekierowywać żądania z tajnym tokenem.',
          roundTrip: stopwatch.elapsed,
        );
      }
      if (response.statusCode < 200 || response.statusCode >= 300) {
        return RemoteAlarmGatewayTestResult(
          success: false,
          message:
              response.statusCode == HttpStatus.unauthorized ||
                  response.statusCode == HttpStatus.forbidden
              ? 'Bramka odrzuciła token viewer.'
              : 'Bramka zwróciła HTTP ${response.statusCode}.',
          roundTrip: stopwatch.elapsed,
        );
      }
      final decoded = bytes.isEmpty
          ? const <String, Object?>{}
          : jsonDecode(utf8.decode(bytes));
      if (!isExpectedHealthPayload(
        decoded,
        expectedDeviceId: credentials.configuration.deviceId,
      )) {
        return RemoteAlarmGatewayTestResult(
          success: false,
          message:
              'Bramka zwróciła odpowiedź dla innego lub '
              'niezweryfikowanego urządzenia.',
          roundTrip: stopwatch.elapsed,
        );
      }
      return RemoteAlarmGatewayTestResult(
        success: true,
        message: 'Bezpieczne połączenie z bramką działa.',
        roundTrip: stopwatch.elapsed,
      );
    } on TimeoutException {
      return const RemoteAlarmGatewayTestResult(
        success: false,
        message: 'Bramka nie odpowiedziała w wymaganym czasie.',
      );
    } on HandshakeException {
      return const RemoteAlarmGatewayTestResult(
        success: false,
        message: 'Certyfikat TLS bramki jest nieprawidłowy.',
      );
    } on SocketException {
      return const RemoteAlarmGatewayTestResult(
        success: false,
        message: 'Nie można połączyć się z bramką alarmową.',
      );
    } on FormatException {
      return const RemoteAlarmGatewayTestResult(
        success: false,
        message: 'Bramka zwróciła nieprawidłową odpowiedź.',
      );
    } finally {
      client.close(force: true);
    }
  }

  static bool isExpectedHealthPayload(
    Object? decoded, {
    required String expectedDeviceId,
  }) {
    return decoded is Map &&
        decoded['status'] == 'ok' &&
        decoded['deviceId'] == expectedDeviceId;
  }
}
