import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:http/http.dart' as http;

import '../domain/entity_ids.dart';
import '../domain/models.dart';

enum HomeAssistantFailureType {
  authentication,
  network,
  invalidResponse,
  server,
}

final class HomeAssistantFailure implements Exception {
  const HomeAssistantFailure(this.type, this.message, {this.statusCode});

  final HomeAssistantFailureType type;
  final String message;
  final int? statusCode;

  @override
  String toString() => message;
}

final class HomeAssistantApi {
  HomeAssistantApi(
    this.credentials, {
    http.Client? client,
    this.timeout = const Duration(seconds: 12),
  }) : _client = client ?? http.Client(),
       _ownsClient = client == null;

  final HomeAssistantCredentials credentials;
  final Duration timeout;
  final http.Client _client;
  final bool _ownsClient;

  Map<String, String> get _headers => <String, String>{
    HttpHeaders.authorizationHeader: 'Bearer ${credentials.accessToken}',
    HttpHeaders.contentTypeHeader: 'application/json',
  };

  Future<HomeAssistantConfig> fetchConfig() async {
    final response = await _get(_apiUri('config'));
    return _parseResponse(() {
      final json = _decodeMap(response.body);
      return HomeAssistantConfig.fromJson(json);
    });
  }

  Future<Map<String, HaEntityState>> fetchAquaStates() async {
    final states = await fetchAllStates();
    return <String, HaEntityState>{
      for (final entry in states.entries)
        if (AquaEntityIds.all.contains(entry.key)) entry.key: entry.value,
    };
  }

  Future<Map<String, HaEntityState>> fetchAllStates() async {
    final response = await _get(_apiUri('states'));
    return _parseResponse(() {
      final decoded = _decodeList(response.body);
      final result = <String, HaEntityState>{};
      for (final item in decoded) {
        final map = _objectMap(item);
        if (map == null) {
          continue;
        }
        try {
          final state = HaEntityState.fromJson(map);
          result[state.entityId] = state;
        } on FormatException {
          continue;
        }
      }
      return result;
    });
  }

  Future<void> callScript(
    String script, [
    Map<String, Object?> data = const <String, Object?>{},
  ]) {
    if (!RegExp(r'^[a-z0-9_]+$').hasMatch(script)) {
      throw ArgumentError.value(
        script,
        'script',
        'Nieprawidłowa nazwa skryptu',
      );
    }
    return callService('script', script, data);
  }

  Future<void> callService(
    String domain,
    String service, [
    Map<String, Object?> data = const <String, Object?>{},
  ]) async {
    final servicePart = RegExp(r'^[a-z0-9_]+$');
    if (!servicePart.hasMatch(domain) || !servicePart.hasMatch(service)) {
      throw ArgumentError('Nieprawidłowa nazwa domeny lub usługi.');
    }
    await _post(_apiUri('services/$domain/$service'), body: jsonEncode(data));
  }

  Future<List<HistorySample>> fetchHistory(
    String entityId,
    Duration period,
  ) async {
    if (!AquaEntityIds.all.contains(entityId)) {
      throw ArgumentError.value(entityId, 'entityId', 'Nieznana encja AquaCYD');
    }
    return fetchEntityHistory(entityId, period);
  }

  Future<List<HistorySample>> fetchEntityHistory(
    String entityId,
    Duration period,
  ) async {
    if (!RegExp(r'^[a-z0-9_]+\.[a-z0-9_]+$').hasMatch(entityId)) {
      throw ArgumentError.value(entityId, 'entityId', 'Invalid entity ID');
    }
    if (period <= Duration.zero || period > const Duration(days: 31)) {
      throw ArgumentError.value(
        period,
        'period',
        'Zakres historii musi wynosić od 1 sekundy do 31 dni.',
      );
    }

    final start = DateTime.now().subtract(period).toUtc().toIso8601String();
    final response = await _get(
      _apiUri(
        'history/period/$start',
        queryParameters: <String, String>{
          'filter_entity_id': entityId,
          'minimal_response': 'true',
          'no_attributes': 'true',
        },
      ),
    );
    return _parseResponse(() {
      final outer = _decodeList(response.body);
      if (outer.isEmpty) {
        return const <HistorySample>[];
      }
      final rawSeries = outer.first;
      if (rawSeries is! List<Object?>) {
        throw const HomeAssistantFailure(
          HomeAssistantFailureType.invalidResponse,
          'Home Assistant zwrócił nieprawidłowy format historii.',
        );
      }

      final samples = <HistorySample>[];
      for (final rawPoint in rawSeries) {
        final point = _objectMap(rawPoint);
        if (point == null) {
          continue;
        }
        final rawState = point['state'];
        final rawTime = point['last_changed'] ?? point['last_updated'];
        final value = rawState is String
            ? double.tryParse(rawState.replaceAll(',', '.'))
            : null;
        final time = rawTime is String
            ? DateTime.tryParse(rawTime)?.toLocal()
            : null;
        if (value != null && value.isFinite && time != null) {
          samples.add(HistorySample(time: time, value: value));
        }
      }
      return AquariumSnapshot.normalizeHistory(samples);
    });
  }

  void close() {
    if (_ownsClient) {
      _client.close();
    }
  }

  Uri _apiUri(String endpoint, {Map<String, String>? queryParameters}) {
    final basePath = credentials.baseUri.path.replaceFirst(RegExp(r'/+$'), '');
    return credentials.baseUri.replace(
      path: '$basePath/api/$endpoint',
      queryParameters: queryParameters,
    );
  }

  Future<http.Response> _get(Uri uri) {
    return _request(() => _client.get(uri, headers: _headers));
  }

  Future<http.Response> _post(Uri uri, {required String body}) {
    return _request(() => _client.post(uri, headers: _headers, body: body));
  }

  Future<http.Response> _request(Future<http.Response> Function() send) async {
    try {
      final response = await send().timeout(timeout);
      if (response.statusCode == HttpStatus.unauthorized ||
          response.statusCode == HttpStatus.forbidden) {
        throw HomeAssistantFailure(
          HomeAssistantFailureType.authentication,
          'Home Assistant odrzucił token dostępu.',
          statusCode: response.statusCode,
        );
      }
      if (response.statusCode < 200 || response.statusCode >= 300) {
        throw HomeAssistantFailure(
          HomeAssistantFailureType.server,
          'Home Assistant zwrócił błąd HTTP ${response.statusCode}.',
          statusCode: response.statusCode,
        );
      }
      return response;
    } on HomeAssistantFailure {
      rethrow;
    } on TimeoutException {
      throw const HomeAssistantFailure(
        HomeAssistantFailureType.network,
        'Przekroczono czas oczekiwania na Home Assistanta.',
      );
    } on http.ClientException {
      throw const HomeAssistantFailure(
        HomeAssistantFailureType.network,
        'Nie można połączyć się z Home Assistantem.',
      );
    } on SocketException {
      throw const HomeAssistantFailure(
        HomeAssistantFailureType.network,
        'Home Assistant jest nieosiągalny w tej sieci.',
      );
    } on FormatException {
      throw const HomeAssistantFailure(
        HomeAssistantFailureType.invalidResponse,
        'Home Assistant zwrócił dane w nieprawidłowym formacie.',
      );
    }
  }

  static Map<String, Object?> _decodeMap(String body) {
    final Object? decoded = jsonDecode(body);
    final result = _objectMap(decoded);
    if (result == null) {
      throw const FormatException('Oczekiwano obiektu JSON.');
    }
    return result;
  }

  static List<Object?> _decodeList(String body) {
    final Object? decoded = jsonDecode(body);
    if (decoded is! List<Object?>) {
      throw const FormatException('Oczekiwano tablicy JSON.');
    }
    return decoded;
  }

  static Map<String, Object?>? _objectMap(Object? value) {
    if (value is! Map<Object?, Object?>) {
      return null;
    }
    final result = <String, Object?>{};
    for (final entry in value.entries) {
      final key = entry.key;
      if (key is String) {
        result[key] = entry.value;
      }
    }
    return result;
  }

  static T _parseResponse<T>(T Function() parse) {
    try {
      return parse();
    } on HomeAssistantFailure {
      rethrow;
    } on FormatException {
      throw const HomeAssistantFailure(
        HomeAssistantFailureType.invalidResponse,
        'Home Assistant zwrócił dane w nieprawidłowym formacie.',
      );
    }
  }
}
