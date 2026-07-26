import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import '../controller_address.dart';
import 'status_decoder.dart';

class ControllerApiException implements Exception {
  const ControllerApiException({
    required this.message,
    this.code = 'request_failed',
    this.statusCode,
  });

  final String message;
  final String code;
  final int? statusCode;

  bool get isAuthenticationError =>
      statusCode == HttpStatus.unauthorized ||
      statusCode == HttpStatus.forbidden ||
      code == 'invalid_pin' ||
      code == 'pin_invalid' ||
      code == 'pin_required' ||
      code == 'session_required' ||
      code == 'session_expired';

  @override
  String toString() => message;
}

class ControllerActionResult {
  const ControllerActionResult({
    required this.success,
    required this.code,
    required this.message,
  });

  factory ControllerActionResult.fromJson(
    Map<String, dynamic> json, {
    required bool fallbackSuccess,
  }) {
    final success = json['success'] == true || json['ok'] == true;
    return ControllerActionResult(
      success: json.containsKey('success') || json.containsKey('ok')
          ? success
          : fallbackSuccess,
      code: json['code']?.toString() ?? (fallbackSuccess ? 'ok' : 'error'),
      message:
          json['message']?.toString() ??
          (fallbackSuccess
              ? 'Operacja zakończona.'
              : 'Operacja nie powiodła się.'),
    );
  }

  final bool success;
  final String code;
  final String message;
}

class ControllerAdminSession {
  const ControllerAdminSession({required this.token, required this.expiresAt});

  factory ControllerAdminSession.fromJson(
    Map<String, dynamic> json, {
    DateTime? authenticatedAt,
  }) {
    final token = json['sessionToken']?.toString() ?? '';
    if (!RegExp(r'^[a-fA-F0-9]{32}$').hasMatch(token)) {
      throw const ControllerApiException(
        code: 'invalid_auth_response',
        message: 'Sterownik zwrócił nieprawidłowy token sesji.',
      );
    }
    final rawExpires = json['expiresInSec'];
    final expiresInSeconds = rawExpires is num
        ? rawExpires.toInt()
        : int.tryParse(rawExpires?.toString() ?? '');
    if (expiresInSeconds == null ||
        expiresInSeconds < 30 ||
        expiresInSeconds > 3600) {
      throw const ControllerApiException(
        code: 'invalid_auth_response',
        message: 'Sterownik zwrócił nieprawidłowy czas sesji.',
      );
    }
    return ControllerAdminSession(
      token: token.toLowerCase(),
      expiresAt: (authenticatedAt ?? DateTime.now()).add(
        Duration(seconds: expiresInSeconds),
      ),
    );
  }

  final String token;
  final DateTime expiresAt;

  bool get isValid => DateTime.now().isBefore(expiresAt);
}

abstract interface class ControllerProtocolV2Api {
  Future<Map<String, dynamic>> capabilities();
  Future<ControllerAdminSession> authenticateSession(String pin);
  Future<ControllerActionResult> actionV2(
    String action, {
    required String commandId,
    required String token,
    Map<String, Object?> payload = const {},
  });
}

abstract interface class ControllerRemoteApi {
  Uri? get baseUri;
  bool get supportsFirmwareUpload;
  bool get supportsFileDownload;
  bool get supportsWebSession;

  Future<void> connect();
  Future<void> disconnect();
  Future<Map<String, dynamic>> status({bool includeHistory = false});
  Future<Map<String, dynamic>> logs(String pin);
  Future<Map<String, dynamic>> busDiagnostics(String pin);
  Future<List<dynamic>> historyFiles();
  Future<ControllerActionResult> authenticate(String pin);
  Future<ControllerActionResult> action(
    String action, {
    Map<String, Object?> payload = const {},
    String? pin,
    bool includePin = true,
  });
  Future<void> setBrowserTime(int epochSeconds, String pin);
  Future<Uint8List> download(
    String path, {
    Map<String, String>? queryParameters,
    int maximumBytes = 16 * 1024 * 1024,
  });
  Future<ControllerActionResult> uploadFirmware(
    Uint8List firmware,
    String fileName,
    String pin, {
    void Function(int sent, int total)? onProgress,
  });
  Future<void> webSession(String sessionId, String state);
}

class ControllerApi implements ControllerRemoteApi, ControllerProtocolV2Api {
  ControllerApi(
    Uri baseUri, {
    Duration requestDeadline = const Duration(seconds: 12),
    Duration readRetryDelay = const Duration(milliseconds: 250),
    int maximumReadAttempts = 2,
  }) : baseUri = ControllerAddress.parse(baseUri.toString()),
       _requestDeadline = _requirePositiveDuration(
         requestDeadline,
         'requestDeadline',
       ),
       _readRetryDelay = _requireNonNegativeDuration(
         readRetryDelay,
         'readRetryDelay',
       ),
       _maximumReadAttempts = _requireReadAttempts(maximumReadAttempts);

  static const int maximumResponseBytes = 2 * 1024 * 1024;
  static const int maximumDownloadBytes = 16 * 1024 * 1024;
  static const int maximumFirmwareBytes = 8 * 1024 * 1024;
  static const int maximumHistoryFileEntries = 1024;
  static const Duration firmwareUploadDeadline = Duration(minutes: 3);

  @override
  final Uri baseUri;
  final Duration _requestDeadline;
  final Duration _readRetryDelay;
  final int _maximumReadAttempts;

  @override
  bool get supportsFirmwareUpload => true;

  @override
  bool get supportsFileDownload => true;

  @override
  bool get supportsWebSession => true;

  @override
  Future<void> connect() async {}

  @override
  Future<void> disconnect() async {}

  Uri resolve(String path, [Map<String, String>? queryParameters]) {
    final basePath = baseUri.path.endsWith('/')
        ? baseUri.path.substring(0, baseUri.path.length - 1)
        : baseUri.path;
    final normalizedPath = path.startsWith('/') ? path : '/$path';
    return baseUri.replace(
      path: '$basePath$normalizedPath',
      queryParameters: queryParameters,
    );
  }

  @override
  Future<Map<String, dynamic>> status({bool includeHistory = false}) async {
    final value = await getJsonValue(
      '/api/status',
      queryParameters: includeHistory ? const {'history': '1'} : null,
    );
    try {
      return decodeControllerStatus(value, requireHistory: includeHistory);
    } on FormatException catch (error) {
      throw ControllerApiException(
        code: 'invalid_status',
        message: 'Sterownik zwrócił niepełny status: ${error.message}',
      );
    }
  }

  @override
  Future<Map<String, dynamic>> capabilities() async {
    final response = await getJson('/api/v2/capabilities');
    final data = response['data'];
    if (data is Map<String, dynamic>) {
      return Map<String, dynamic>.unmodifiable(data);
    }
    if (data is Map) {
      return Map<String, dynamic>.unmodifiable(
        data.map((key, value) => MapEntry(key.toString(), value)),
      );
    }
    return Map<String, dynamic>.unmodifiable(response);
  }

  @override
  Future<Map<String, dynamic>> logs(String pin) {
    return getJson('/api/logs', queryParameters: {'pin': pin});
  }

  @override
  Future<Map<String, dynamic>> busDiagnostics(String pin) {
    return getJson('/api/bus-diagnostics', queryParameters: {'pin': pin});
  }

  @override
  Future<void> webSession(String sessionId, String state) async {
    await getJson(
      '/api/web-session',
      queryParameters: {'sid': sessionId, 'state': state},
      allowRetry: false,
    );
  }

  @override
  Future<List<dynamic>> historyFiles() async {
    final value = await getJsonValue(
      '/api/files',
      queryParameters: const {'dir': '/aq/data/history'},
    );
    final files = switch (value) {
      List<dynamic>() => value,
      Map<String, dynamic>() when value['files'] is List<dynamic> =>
        value['files'] as List<dynamic>,
      _ => null,
    };
    if (files == null) {
      throw const ControllerApiException(
        code: 'invalid_files_response',
        message: 'Sterownik zwrócił nieprawidłową listę archiwów.',
      );
    }
    if (files.length > maximumHistoryFileEntries) {
      throw const ControllerApiException(
        code: 'history_files_too_large',
        message: 'Lista archiwów przekracza bezpieczny limit 1024 wpisów.',
      );
    }
    return List<dynamic>.unmodifiable(files);
  }

  @override
  Future<ControllerActionResult> authenticate(String pin) {
    return action('auth_check', pin: pin, includePin: true);
  }

  @override
  Future<ControllerAdminSession> authenticateSession(String pin) async {
    final response = await _request(
      'POST',
      resolve('/api/v2/auth'),
      headers: const {HttpHeaders.contentTypeHeader: 'application/json'},
      body: utf8.encode(jsonEncode({'v': 2, 'pin': pin})),
      allowRetry: false,
    );
    final decoded = _decodeJsonObject(response.body);
    if (!response.isSuccess || decoded['ok'] != true) {
      throw ControllerApiException(
        message:
            decoded['message']?.toString() ??
            'Nie udało się rozpocząć bezpiecznej sesji administratora.',
        code: decoded['code']?.toString() ?? 'authentication_failed',
        statusCode: response.statusCode,
      );
    }
    final rawData = decoded['data'];
    final data = rawData is Map<String, dynamic>
        ? rawData
        : rawData is Map
        ? rawData.map((key, value) => MapEntry(key.toString(), value))
        : <String, dynamic>{};
    return ControllerAdminSession.fromJson(data);
  }

  @override
  Future<ControllerActionResult> actionV2(
    String actionName, {
    required String commandId,
    required String token,
    Map<String, Object?> payload = const {},
  }) {
    if (!RegExp(r'^[a-zA-Z0-9_-]{8,48}$').hasMatch(commandId)) {
      throw const ControllerApiException(
        code: 'invalid_command_id',
        message: 'Identyfikator polecenia ma nieprawidłowy format.',
      );
    }
    if (!RegExp(r'^[a-fA-F0-9]{32}$').hasMatch(token)) {
      throw const ControllerApiException(
        code: 'invalid_session_token',
        message: 'Token sesji administratora ma nieprawidłowy format.',
      );
    }
    return action(
      actionName,
      payload: {
        'v': 2,
        'commandId': commandId,
        'token': token.toLowerCase(),
        ...payload,
      },
      includePin: false,
    );
  }

  @override
  Future<ControllerActionResult> action(
    String action, {
    Map<String, Object?> payload = const {},
    String? pin,
    bool includePin = true,
  }) async {
    final fields = <String, String>{'action': action};
    for (final entry in payload.entries) {
      final value = entry.value;
      if (value != null) {
        fields[entry.key] = _encodeField(value);
      }
    }
    if (includePin && pin != null && pin.isNotEmpty) {
      fields['pin'] = pin;
    }

    final response = await _request(
      'POST',
      resolve('/api/action'),
      headers: const {
        HttpHeaders.contentTypeHeader: 'application/x-www-form-urlencoded',
      },
      body: utf8.encode(Uri(queryParameters: fields).query),
    );
    final decoded = _decodeJsonObject(response.body);
    final result = ControllerActionResult.fromJson(
      decoded,
      fallbackSuccess: response.isSuccess,
    );
    if (!response.isSuccess || !result.success) {
      throw ControllerApiException(
        message: result.message,
        code: result.code,
        statusCode: response.statusCode,
      );
    }
    return result;
  }

  @override
  Future<void> setBrowserTime(int epochSeconds, String pin) async {
    final fields = {'epoch': '$epochSeconds', 'pin': pin};
    final response = await _request(
      'POST',
      resolve('/settime'),
      headers: const {
        HttpHeaders.contentTypeHeader: 'application/x-www-form-urlencoded',
      },
      body: utf8.encode(Uri(queryParameters: fields).query),
    );
    if (!response.isSuccess) {
      throw ControllerApiException(
        message: utf8.decode(response.body, allowMalformed: true).trim().isEmpty
            ? 'Nie udało się ustawić czasu sterownika.'
            : utf8.decode(response.body, allowMalformed: true).trim(),
        code: response.statusCode == HttpStatus.forbidden
            ? 'invalid_pin'
            : 'set_time_failed',
        statusCode: response.statusCode,
      );
    }
  }

  @override
  Future<Uint8List> download(
    String path, {
    Map<String, String>? queryParameters,
    int maximumBytes = maximumDownloadBytes,
  }) async {
    if (maximumBytes <= 0 || maximumBytes > maximumDownloadBytes) {
      throw const ControllerApiException(
        code: 'invalid_download_limit',
        message: 'Limit pobierania musi mieścić się w zakresie 1 B–16 MB.',
      );
    }
    final response = await _request(
      'GET',
      resolve(path, queryParameters),
      maximumBytes: maximumBytes,
    );
    if (!response.isSuccess) {
      throw ControllerApiException(
        message: 'Pobieranie nie powiodło się (HTTP ${response.statusCode}).',
        code: 'download_failed',
        statusCode: response.statusCode,
      );
    }
    return Uint8List.fromList(response.body);
  }

  @override
  Future<ControllerActionResult> uploadFirmware(
    Uint8List firmware,
    String fileName,
    String pin, {
    void Function(int sent, int total)? onProgress,
  }) async {
    if (firmware.isEmpty || firmware.length > maximumFirmwareBytes) {
      throw const ControllerApiException(
        code: 'invalid_firmware_size',
        message: 'Firmware musi mieć od 1 B do 8 MB.',
      );
    }
    if (!fileName.toLowerCase().endsWith('.bin')) {
      throw const ControllerApiException(
        code: 'invalid_firmware_extension',
        message: 'Firmware musi być plikiem .bin.',
      );
    }

    final client = _newClient();
    try {
      return await _performFirmwareUpload(
        client,
        firmware,
        fileName,
        pin,
        onProgress,
      ).timeout(firmwareUploadDeadline);
    } on ControllerApiException {
      rethrow;
    } on TimeoutException {
      throw const ControllerApiException(
        code: 'ota_timeout',
        message: 'Sterownik nie zakończył aktualizacji OTA w ciągu 3 minut.',
      );
    } on HandshakeException catch (error) {
      throw ControllerApiException(
        code: 'tls_error',
        message: 'Nie udało się zabezpieczyć połączenia OTA: $error',
      );
    } on SocketException catch (error) {
      throw ControllerApiException(
        code: 'network_error',
        message: 'Błąd sieci podczas OTA: ${error.message}',
      );
    } on HttpException catch (error) {
      throw ControllerApiException(
        code: 'http_protocol_error',
        message: 'Błąd protokołu HTTP podczas OTA: ${error.message}',
      );
    } on IOException catch (error) {
      throw ControllerApiException(
        code: 'io_error',
        message: 'Błąd wejścia/wyjścia podczas OTA: $error',
      );
    } finally {
      client.close(force: true);
    }
  }

  Future<ControllerActionResult> _performFirmwareUpload(
    HttpClient client,
    Uint8List firmware,
    String fileName,
    String pin,
    void Function(int sent, int total)? onProgress,
  ) async {
    final boundary = '----cydAkwarium${DateTime.now().microsecondsSinceEpoch}';
    final safeName = fileName.replaceAll(RegExp(r'[^a-zA-Z0-9._-]'), '_');
    final prefix = utf8.encode(
      '--$boundary\r\n'
      'Content-Disposition: form-data; name="firmware"; filename="$safeName"\r\n'
      'Content-Type: application/octet-stream\r\n\r\n',
    );
    final suffix = utf8.encode('\r\n--$boundary--\r\n');
    final totalLength = prefix.length + firmware.length + suffix.length;
    final request = await client.postUrl(resolve('/update', {'pin': pin}));
    request.headers.contentType = ContentType(
      'multipart',
      'form-data',
      parameters: {'boundary': boundary},
    );
    request.contentLength = totalLength;
    var sent = 0;
    request.add(prefix);
    sent += prefix.length;
    onProgress?.call(sent, totalLength);
    const chunkSize = 32 * 1024;
    for (var offset = 0; offset < firmware.length; offset += chunkSize) {
      final end = (offset + chunkSize).clamp(0, firmware.length);
      request.add(firmware.sublist(offset, end));
      sent += end - offset;
      onProgress?.call(sent, totalLength);
      await Future<void>.delayed(Duration.zero);
    }
    request.add(suffix);
    sent += suffix.length;
    onProgress?.call(sent, totalLength);
    final response = await request.close();
    final responseBytes = await _readLimited(response, maximumResponseBytes);
    final responseText = utf8
        .decode(responseBytes, allowMalformed: true)
        .trim();
    if (response.statusCode < 200 || response.statusCode >= 300) {
      throw ControllerApiException(
        message: responseText.isEmpty
            ? 'Aktualizacja OTA nie powiodła się.'
            : responseText,
        code: response.statusCode == HttpStatus.forbidden
            ? 'invalid_pin'
            : 'ota_failed',
        statusCode: response.statusCode,
      );
    }
    return ControllerActionResult(
      success: true,
      code: 'ok',
      message: responseText.isEmpty
          ? 'Firmware został przyjęty. Sterownik uruchomi się ponownie.'
          : responseText,
    );
  }

  Future<Map<String, dynamic>> getJson(
    String path, {
    Map<String, String>? queryParameters,
    bool allowRetry = true,
  }) async {
    final value = await getJsonValue(
      path,
      queryParameters: queryParameters,
      allowRetry: allowRetry,
    );
    if (value is! Map<String, dynamic>) {
      throw const ControllerApiException(
        code: 'invalid_json',
        message: 'Sterownik zwrócił nieprawidłowy obiekt JSON.',
      );
    }
    return value;
  }

  Future<dynamic> getJsonValue(
    String path, {
    Map<String, String>? queryParameters,
    bool allowRetry = true,
  }) async {
    final response = await _request(
      'GET',
      resolve(path, queryParameters),
      allowRetry: allowRetry,
    );
    if (!response.isSuccess) {
      final payload = _tryDecodeJsonObject(response.body);
      throw ControllerApiException(
        message:
            payload?['message']?.toString() ??
            'Sterownik zwrócił HTTP ${response.statusCode}.',
        code: payload?['code']?.toString() ?? 'http_error',
        statusCode: response.statusCode,
      );
    }
    try {
      return jsonDecode(utf8.decode(response.body, allowMalformed: false));
    } on FormatException catch (error) {
      throw ControllerApiException(
        code: 'invalid_json',
        message: 'Nieprawidłowa odpowiedź JSON: ${error.message}',
      );
    }
  }

  Future<_HttpResponse> _request(
    String method,
    Uri uri, {
    Map<String, String> headers = const {},
    List<int>? body,
    int maximumBytes = maximumResponseBytes,
    bool allowRetry = true,
  }) async {
    if (maximumBytes <= 0) {
      throw const ControllerApiException(
        code: 'invalid_response_limit',
        message: 'Limit odpowiedzi musi być większy od zera.',
      );
    }
    final normalizedMethod = method.toUpperCase();
    final attempts = allowRetry && normalizedMethod == 'GET'
        ? _maximumReadAttempts
        : 1;
    for (var attempt = 1; attempt <= attempts; attempt++) {
      try {
        return await _requestOnce(
          normalizedMethod,
          uri,
          headers: headers,
          body: body,
          maximumBytes: maximumBytes,
        );
      } on ControllerApiException catch (error) {
        if (attempt >= attempts || !_isTransientReadError(error)) {
          rethrow;
        }
        if (_readRetryDelay > Duration.zero) {
          await Future<void>.delayed(_readRetryDelay);
        }
      }
    }
    throw StateError('Nieosiągalny stan ponawiania żądania HTTP.');
  }

  Future<_HttpResponse> _requestOnce(
    String method,
    Uri uri, {
    required Map<String, String> headers,
    required List<int>? body,
    required int maximumBytes,
  }) async {
    final client = _newClient();
    try {
      return await _performRequest(
        client,
        method,
        uri,
        headers,
        body,
        maximumBytes,
      ).timeout(_requestDeadline);
    } on ControllerApiException {
      rethrow;
    } on TimeoutException {
      throw ControllerApiException(
        code: 'timeout',
        message: 'Sterownik ${uri.host} nie odpowiedział w wymaganym czasie.',
      );
    } on HandshakeException catch (error) {
      throw ControllerApiException(
        code: 'tls_error',
        message: 'Nie udało się zabezpieczyć połączenia: $error',
      );
    } on SocketException catch (error) {
      throw ControllerApiException(
        code: 'network_error',
        message: 'Brak połączenia ze sterownikiem: ${error.message}',
      );
    } on HttpException catch (error) {
      throw ControllerApiException(
        code: 'http_protocol_error',
        message: 'Sterownik zwrócił błąd protokołu HTTP: ${error.message}',
      );
    } on IOException catch (error) {
      throw ControllerApiException(
        code: 'io_error',
        message: 'Błąd wejścia/wyjścia podczas komunikacji: $error',
      );
    } finally {
      client.close(force: true);
    }
  }

  Future<_HttpResponse> _performRequest(
    HttpClient client,
    String method,
    Uri uri,
    Map<String, String> headers,
    List<int>? body,
    int maximumBytes,
  ) async {
    final request = await client.openUrl(method, uri);
    request.headers.set(HttpHeaders.acceptHeader, 'application/json, */*');
    headers.forEach(request.headers.set);
    if (body != null) {
      request.contentLength = body.length;
      request.add(body);
    }
    final response = await request.close();
    final bytes = await _readLimited(response, maximumBytes);
    return _HttpResponse(response.statusCode, bytes);
  }

  static bool _isTransientReadError(ControllerApiException error) => const {
    'timeout',
    'network_error',
    'tls_error',
    'http_protocol_error',
    'io_error',
  }.contains(error.code);

  HttpClient _newClient() => HttpClient()
    ..connectionTimeout = _requestDeadline
    ..idleTimeout = const Duration(seconds: 15);

  static Future<List<int>> _readLimited(
    HttpClientResponse response,
    int maximumBytes,
  ) async {
    final builder = BytesBuilder(copy: false);
    var received = 0;
    await for (final chunk in response) {
      received += chunk.length;
      if (received > maximumBytes) {
        throw ControllerApiException(
          code: 'response_too_large',
          message: 'Odpowiedź sterownika przekracza limit $maximumBytes B.',
        );
      }
      builder.add(chunk);
    }
    return builder.takeBytes();
  }

  static String _encodeField(Object value) {
    if (value is bool) {
      return value ? '1' : '0';
    }
    if (value is Map || value is List) {
      return jsonEncode(value);
    }
    return value.toString();
  }

  static Map<String, dynamic> _decodeJsonObject(List<int> bytes) {
    final result = _tryDecodeJsonObject(bytes);
    if (result == null) {
      throw const ControllerApiException(
        code: 'invalid_json',
        message: 'Sterownik zwrócił nieprawidłową odpowiedź.',
      );
    }
    return result;
  }

  static Map<String, dynamic>? _tryDecodeJsonObject(List<int> bytes) {
    try {
      final decoded = jsonDecode(utf8.decode(bytes, allowMalformed: true));
      return decoded is Map<String, dynamic> ? decoded : null;
    } on FormatException {
      return null;
    }
  }
}

class _HttpResponse {
  const _HttpResponse(this.statusCode, this.body);

  final int statusCode;
  final List<int> body;

  bool get isSuccess => statusCode >= 200 && statusCode < 300;
}

Duration _requirePositiveDuration(Duration value, String name) {
  if (value <= Duration.zero) {
    throw ArgumentError.value(value, name, 'Czas musi być większy od zera.');
  }
  return value;
}

Duration _requireNonNegativeDuration(Duration value, String name) {
  if (value.isNegative) {
    throw ArgumentError.value(value, name, 'Czas nie może być ujemny.');
  }
  return value;
}

int _requireReadAttempts(int value) {
  if (value < 1 || value > 3) {
    throw ArgumentError.value(
      value,
      'maximumReadAttempts',
      'Liczba prób odczytu musi mieścić się w zakresie 1..3.',
    );
  }
  return value;
}
