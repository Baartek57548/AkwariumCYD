import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import '../controller_address.dart';

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
      statusCode == HttpStatus.forbidden ||
      code == 'invalid_pin' ||
      code == 'pin_required';

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

class ControllerApi {
  ControllerApi(Uri baseUri)
    : baseUri = ControllerAddress.parse(baseUri.toString());

  static const int maximumResponseBytes = 8 * 1024 * 1024;
  static const int maximumFirmwareBytes = 8 * 1024 * 1024;

  final Uri baseUri;

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

  Future<Map<String, dynamic>> status({bool includeHistory = false}) {
    return getJson(
      '/api/status',
      queryParameters: includeHistory ? const {'history': '1'} : null,
    );
  }

  Future<Map<String, dynamic>> logs(String pin) {
    return getJson('/api/logs', queryParameters: {'pin': pin});
  }

  Future<Map<String, dynamic>> busDiagnostics(String pin) {
    return getJson('/api/bus-diagnostics', queryParameters: {'pin': pin});
  }

  Future<void> webSession(String sessionId, String state) async {
    await getJson(
      '/api/web-session',
      queryParameters: {'sid': sessionId, 'state': state},
    );
  }

  Future<List<dynamic>> historyFiles() async {
    final value = await getJsonValue(
      '/api/files',
      queryParameters: const {'dir': '/aq/data/history'},
    );
    if (value is List<dynamic>) {
      return value;
    }
    if (value is Map<String, dynamic> && value['files'] is List<dynamic>) {
      return value['files'] as List<dynamic>;
    }
    throw const ControllerApiException(
      code: 'invalid_files_response',
      message: 'Sterownik zwrócił nieprawidłową listę archiwów.',
    );
  }

  Future<ControllerActionResult> authenticate(String pin) {
    return action('auth_check', pin: pin, includePin: true);
  }

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

  Future<Uint8List> download(
    String path, {
    Map<String, String>? queryParameters,
    int maximumBytes = 64 * 1024 * 1024,
  }) async {
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

    final boundary = '----cydAkwarium${DateTime.now().microsecondsSinceEpoch}';
    final safeName = fileName.replaceAll(RegExp(r'[^a-zA-Z0-9._-]'), '_');
    final prefix = utf8.encode(
      '--$boundary\r\n'
      'Content-Disposition: form-data; name="firmware"; filename="$safeName"\r\n'
      'Content-Type: application/octet-stream\r\n\r\n',
    );
    final suffix = utf8.encode('\r\n--$boundary--\r\n');
    final totalLength = prefix.length + firmware.length + suffix.length;
    final client = _newClient();
    try {
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
      final response = await request.close().timeout(
        const Duration(minutes: 3),
      );
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
    } on TimeoutException {
      throw const ControllerApiException(
        code: 'ota_timeout',
        message: 'Sterownik nie zakończył aktualizacji OTA w ciągu 3 minut.',
      );
    } on SocketException catch (error) {
      throw ControllerApiException(
        code: 'network_error',
        message: 'Błąd sieci podczas OTA: ${error.message}',
      );
    } finally {
      client.close(force: true);
    }
  }

  Future<Map<String, dynamic>> getJson(
    String path, {
    Map<String, String>? queryParameters,
  }) async {
    final value = await getJsonValue(path, queryParameters: queryParameters);
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
  }) async {
    final response = await _request('GET', resolve(path, queryParameters));
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
  }) async {
    final client = _newClient();
    try {
      final request = await client.openUrl(method, uri);
      request.headers.set(HttpHeaders.acceptHeader, 'application/json, */*');
      headers.forEach(request.headers.set);
      if (body != null) {
        request.contentLength = body.length;
        request.add(body);
      }
      final response = await request.close().timeout(
        const Duration(seconds: 12),
      );
      final bytes = await _readLimited(response, maximumBytes);
      return _HttpResponse(response.statusCode, bytes);
    } on TimeoutException {
      throw ControllerApiException(
        code: 'timeout',
        message: 'Sterownik ${uri.host} nie odpowiedział w wymaganym czasie.',
      );
    } on SocketException catch (error) {
      throw ControllerApiException(
        code: 'network_error',
        message: 'Brak połączenia ze sterownikiem: ${error.message}',
      );
    } finally {
      client.close(force: true);
    }
  }

  HttpClient _newClient() => HttpClient()
    ..connectionTimeout = const Duration(seconds: 8)
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
