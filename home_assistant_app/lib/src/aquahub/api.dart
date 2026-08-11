import 'dart:async';
import 'dart:convert';
import 'dart:developer' as developer;

import 'package:http/http.dart' as http;

import 'domain.dart';
import 'transport.dart';

enum HubFailureType {
  network,
  authentication,
  server,
  invalidResponse,
  security,
}

final class HubFailure implements Exception {
  const HubFailure(this.type, this.message, {this.statusCode});

  final HubFailureType type;
  final String message;
  final int? statusCode;

  @override
  String toString() => message;
}

final class HubPairResult {
  const HubPairResult({required this.token, required this.tlsFingerprint});

  final String token;
  final String tlsFingerprint;
}

final class HubApi {
  HubApi._({
    required this.baseUri,
    required http.Client client,
    this.accessToken,
    this.expectedFingerprint,
  }) : _client = client;

  factory HubApi.bootstrap(
    Uri baseUri, {
    void Function(String fingerprint)? onCertificate,
    http.Client? client,
  }) => HubApi._(
    baseUri: _normalizeBase(baseUri),
    client:
        client ??
        createHubHttpClient(bootstrap: true, onCertificate: onCertificate),
  );

  factory HubApi.authenticated(
    HubCredentials credentials, {
    http.Client? client,
  }) => HubApi._(
    baseUri: credentials.baseUri,
    accessToken: credentials.accessToken,
    expectedFingerprint: credentials.tlsFingerprint,
    client:
        client ??
        createHubHttpClient(
          bootstrap: false,
          expectedFingerprint: credentials.tlsFingerprint,
        ),
  );

  final Uri baseUri;
  final String? accessToken;
  final String? expectedFingerprint;
  final http.Client _client;

  static const _timeout = Duration(seconds: 10);
  static const _maximumResponseBytes = 1024 * 1024;

  Future<HubInfo> fetchInfo() async {
    final json = await _requestJson(
      'GET',
      '/api/v1/info',
      authenticated: false,
    );
    return HubInfo.fromJson(_object(json));
  }

  Future<HubPairResult> pair(int code) async {
    if (code < 100000 || code > 999999) {
      throw const HubFailure(
        HubFailureType.invalidResponse,
        'Kod parowania musi mieć sześć cyfr.',
      );
    }
    final json = await _requestJson(
      'POST',
      '/api/v1/pair',
      authenticated: false,
      body: <String, Object?>{'code': code},
    );
    final object = _object(json);
    final token = object['token'];
    final fingerprint = normalizeFingerprint(
      object['tls_fingerprint'] as String? ?? '',
    );
    if (token is! String ||
        token.length < 32 ||
        !RegExp(r'^[0-9A-F]{64}$').hasMatch(fingerprint)) {
      throw const HubFailure(
        HubFailureType.invalidResponse,
        'AquaHub zwrócił nieprawidłowe dane parowania.',
      );
    }
    return HubPairResult(token: token, tlsFingerprint: fingerprint);
  }

  Future<HubSystem> fetchSystem() async =>
      HubSystem.fromJson(_object(await _requestJson('GET', '/api/v1/system')));

  Future<List<HubDevice>> fetchDevices() async {
    final object = _object(await _requestJson('GET', '/api/v1/devices'));
    return _list(
      object['items'],
    ).map((item) => HubDevice.fromJson(_object(item))).toList(growable: false);
  }

  Future<List<HubEntity>> fetchEntities() async {
    final entities = <HubEntity>[];
    var offset = 0;
    while (true) {
      final object = _object(
        await _requestJson(
          'GET',
          '/api/v1/entities',
          query: <String, String>{'offset': '$offset', 'limit': '48'},
        ),
      );
      final total = _nonNegativeInteger(object['total'], 'total');
      final items = _list(object['items']);
      entities.addAll(items.map((item) => HubEntity.fromJson(_object(item))));
      offset += items.length;
      if (offset >= total) break;
      if (items.isEmpty || offset > 128) {
        throw const HubFailure(
          HubFailureType.invalidResponse,
          'AquaHub zwrócił niespójną paginację encji.',
        );
      }
    }
    return List<HubEntity>.unmodifiable(entities);
  }

  Future<List<HubHistoryPoint>> fetchHistory(String entityId) async {
    if (!_validEntityId(entityId)) {
      throw ArgumentError.value(entityId, 'entityId');
    }
    final object = _object(
      await _requestJson(
        'GET',
        '/api/v1/history',
        query: <String, String>{'entity_id': entityId, 'limit': '180'},
      ),
    );
    return _list(object['items'])
        .map((item) => HubHistoryPoint.fromJson(_object(item)))
        .toList(growable: false)
        .reversed
        .toList(growable: false);
  }

  Future<void> sendCommand(String entityId, Object? value) async {
    if (!_validEntityId(entityId) ||
        (value != null &&
            value is! bool &&
            value is! num &&
            value is! String)) {
      throw ArgumentError('Nieprawidłowa komenda encji.');
    }
    await _requestJson(
      'POST',
      '/api/v1/entities/$entityId/command',
      body: <String, Object?>{'value': value},
    );
  }

  Future<Object?> _requestJson(
    String method,
    String endpoint, {
    bool authenticated = true,
    Map<String, String>? query,
    Map<String, Object?>? body,
  }) async {
    final uri = _uri(endpoint, query);
    final headers = <String, String>{'accept': 'application/json'};
    if (authenticated) {
      if (accessToken == null) {
        throw const HubFailure(
          HubFailureType.authentication,
          'Brak tokenu dostępu AquaHub.',
        );
      }
      headers['authorization'] = 'Bearer $accessToken';
    }
    if (body != null) headers['content-type'] = 'application/json';
    try {
      final request = http.Request(method, uri)..headers.addAll(headers);
      if (body != null) request.body = jsonEncode(body);
      final streamed = await _client.send(request).timeout(_timeout);
      if (streamed.contentLength != null &&
          streamed.contentLength! > _maximumResponseBytes) {
        throw const HubFailure(
          HubFailureType.invalidResponse,
          'Odpowiedź AquaHub przekracza bezpieczny limit.',
        );
      }
      final bytes = await streamed.stream
          .fold<List<int>>(<int>[], (all, chunk) {
            if (all.length + chunk.length > _maximumResponseBytes) {
              throw const HubFailure(
                HubFailureType.invalidResponse,
                'Odpowiedź AquaHub przekracza bezpieczny limit.',
              );
            }
            all.addAll(chunk);
            return all;
          })
          .timeout(_timeout);
      final responseBody = utf8.decode(bytes, allowMalformed: false);
      if (streamed.statusCode == 401 || streamed.statusCode == 403) {
        throw HubFailure(
          HubFailureType.authentication,
          'AquaHub odrzucił parowanie lub token.',
          statusCode: streamed.statusCode,
        );
      }
      if (streamed.statusCode < 200 || streamed.statusCode >= 300) {
        final message =
            _serverMessage(responseBody) ??
            'AquaHub zwrócił błąd HTTP ${streamed.statusCode}.';
        throw HubFailure(
          HubFailureType.server,
          message,
          statusCode: streamed.statusCode,
        );
      }
      try {
        return jsonDecode(responseBody);
      } on FormatException {
        throw const HubFailure(
          HubFailureType.invalidResponse,
          'AquaHub zwrócił uszkodzony JSON.',
        );
      }
    } on HubFailure {
      rethrow;
    } on TimeoutException {
      throw const HubFailure(
        HubFailureType.network,
        'AquaHub nie odpowiedział w wymaganym czasie.',
      );
    } on Object catch (error, stackTrace) {
      developer.log(
        'AquaHub transport failure',
        name: 'aquahub.api',
        error: error,
        stackTrace: stackTrace,
      );
      throw const HubFailure(
        HubFailureType.network,
        'Nie udało się ustanowić bezpiecznego połączenia z AquaHub.',
      );
    }
  }

  Uri _uri(String endpoint, Map<String, String>? query) {
    final path = '${baseUri.path}$endpoint';
    return baseUri.replace(path: path, queryParameters: query);
  }

  void close() => _client.close();
}

Uri _normalizeBase(Uri uri) {
  if (uri.scheme != 'https' ||
      uri.host.isEmpty ||
      uri.userInfo.isNotEmpty ||
      uri.query.isNotEmpty ||
      uri.fragment.isNotEmpty) {
    throw const FormatException('AquaHub wymaga pełnego adresu HTTPS.');
  }
  return uri.replace(
    path: uri.path == '/' ? '' : uri.path.replaceFirst(RegExp(r'/+$'), ''),
  );
}

Map<String, Object?> _object(Object? value) {
  if (value is! Map<String, Object?>) {
    throw const HubFailure(
      HubFailureType.invalidResponse,
      'AquaHub zwrócił obiekt w nieznanym formacie.',
    );
  }
  return value;
}

List<Object?> _list(Object? value) {
  if (value is! List<Object?>) {
    throw const HubFailure(
      HubFailureType.invalidResponse,
      'AquaHub zwrócił listę w nieznanym formacie.',
    );
  }
  return value;
}

int _nonNegativeInteger(Object? value, String name) {
  if (value is! num ||
      !value.isFinite ||
      value < 0 ||
      value != value.roundToDouble()) {
    throw HubFailure(
      HubFailureType.invalidResponse,
      'Pole $name ma nieprawidłowy format.',
    );
  }
  return value.toInt();
}

bool _validEntityId(String value) =>
    value.isNotEmpty &&
    value.length < 64 &&
    RegExp(r'^[A-Za-z0-9_.:-]+$').hasMatch(value);

String? _serverMessage(String body) {
  try {
    final object = jsonDecode(body);
    if (object is Map<String, Object?> && object['message'] is String) {
      return object['message']! as String;
    }
  } on FormatException {
    return null;
  }
  return null;
}
