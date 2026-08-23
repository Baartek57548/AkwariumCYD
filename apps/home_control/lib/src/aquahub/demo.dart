import 'dart:convert';

import 'package:http/http.dart' as http;

import 'api.dart';
import 'credentials_store.dart';
import 'domain.dart';

const _demoFingerprint =
    '0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF';

final class DemoHubCredentialsStore implements HubCredentialsStore {
  HubCredentials? _credentials = HubCredentials.parse(
    baseUrl: 'https://demo.aquahub.local:8443',
    accessToken: 'aquahub-demo-session-token-00000001',
    tlsFingerprint: _demoFingerprint,
  );

  @override
  Future<void> clear() async => _credentials = null;

  @override
  Future<HubCredentials?> load() async => _credentials;

  @override
  Future<void> save(HubCredentials credentials) async {
    _credentials = credentials;
  }
}

HubApi createDemoHubApi(HubCredentials credentials) =>
    HubApi.authenticated(credentials, client: _DemoHubClient());

final class _DemoHubClient extends http.BaseClient {
  final List<Map<String, Object?>> _automations = <Map<String, Object?>>[
    <String, Object?>{
      'id': 'auto_cooling',
      'name': 'Chłodzenie awaryjne',
      'enabled': true,
      'cooldown_ms': 60000,
      'trigger': <String, Object?>{
        'entity_id': 'water_temperature',
        'comparison': 'above',
        'value': 27.5,
      },
      'condition': null,
      'action': <String, Object?>{'entity_id': 'aerator', 'value': true},
    },
  ];
  final Map<String, Object?> _states = <String, Object?>{
    'water_temperature': 24.6,
    'water_ph': 7.12,
    'water_level': 82,
    'filter': true,
    'aerator': false,
    'main_light': true,
    'light_brightness': 68,
    'feeding_mode': 'normalny',
    'leak_alarm': false,
  };
  String _updatePhase = 'available';

  @override
  Future<http.StreamedResponse> send(http.BaseRequest request) async {
    final path = request.url.path;
    final body = request.method == 'GET'
        ? ''
        : await request.finalize().bytesToString();
    if (request.method == 'GET' && path == '/api/v1/system') {
      return _json(<String, Object?>{
        'uptime_ms': 91234000,
        'free_heap_bytes': 5242880,
        'devices': 3,
        'online_devices': 3,
        'entities': _states.length,
        'writable_entities': 5,
        'accepted_messages': 14823,
        'rejected_messages': 2,
        'registry_revision': 73,
      });
    }
    if (request.method == 'GET' && path == '/api/v1/devices') {
      return _json(<String, Object?>{'items': _devices});
    }
    if (request.method == 'GET' && path == '/api/v1/entities') {
      return _json(<String, Object?>{
        'total': _entities.length,
        'offset': 0,
        'limit': 48,
        'items': _entities,
      });
    }
    if (request.method == 'GET' && path == '/api/v1/history') {
      final id = request.url.queryParameters['entity_id'];
      final base = _states[id];
      if (base is! num) return _json(<String, Object?>{'items': <Object?>[]});
      return _json(<String, Object?>{
        'items': List<Map<String, Object?>>.generate(24, (index) {
          final variation =
              ((index % 7) - 3) * (id == 'water_ph' ? 0.02 : 0.12);
          return <String, Object?>{
            'changed_at_ms': 91234000 - (index * 300000),
            'state': double.parse((base + variation).toStringAsFixed(2)),
          };
        }),
      });
    }
    if (request.method == 'GET' && path == '/api/v1/updates') {
      return _json(<String, Object?>{
        'supported': true,
        'target': 'aquahub-p4',
        'current_version': '1.0.0',
        'current_security_version': 1,
        'phase': _updatePhase,
        'progress_percent': _updatePhase == 'downloading' ? 64 : 0,
        'bytes_received': _updatePhase == 'downloading' ? 734003 : 0,
        'total_bytes': 1146880,
        'error': '',
        'release': <String, Object?>{
          'release_id': 'stable-1.1.0',
          'version': '1.1.0',
          'size': 1146880,
          'security_version': 1,
          'mandatory': false,
          'notes':
              'Centrum automatyzacji, bezpieczne OTA A/B i poprawki stabilności połączenia.',
        },
      });
    }
    if (request.method == 'POST' && path == '/api/v1/updates/check') {
      _updatePhase = 'available';
      return _accepted();
    }
    if (request.method == 'POST' && path == '/api/v1/updates/install') {
      _updatePhase = 'downloading';
      return _accepted();
    }
    if (request.method == 'GET' && path == '/api/v1/automations') {
      return _json(<String, Object?>{
        'capacity': 8,
        'count': _automations.length,
        'items': _automations,
      });
    }
    if (request.method == 'POST' && path == '/api/v1/automations') {
      final decoded = jsonDecode(body);
      if (decoded is! Map<String, Object?> || decoded['id'] is! String) {
        return _error(400, 'Nieprawidłowa automatyzacja.');
      }
      final id = decoded['id']! as String;
      _automations.removeWhere((rule) => rule['id'] == id);
      _automations.add(Map<String, Object?>.from(decoded));
      return _accepted();
    }
    if (request.method == 'DELETE' && path.startsWith('/api/v1/automations/')) {
      final id = path.substring('/api/v1/automations/'.length);
      _automations.removeWhere((rule) => rule['id'] == id);
      return _accepted();
    }
    if (request.method == 'POST' &&
        path.startsWith('/api/v1/entities/') &&
        path.endsWith('/command')) {
      final id = path.substring(
        '/api/v1/entities/'.length,
        path.length - '/command'.length,
      );
      final decoded = jsonDecode(body);
      if (!_states.containsKey(id) || decoded is! Map<String, Object?>) {
        return _error(404, 'Nieznana encja.');
      }
      _states[id] = decoded['value'];
      return _accepted();
    }
    return _error(404, 'Nieznany endpoint demonstracyjny.');
  }

  List<Map<String, Object?>> get _devices => <Map<String, Object?>>[
    <String, Object?>{
      'id': 'aquacyd_main',
      'name': 'Sterownik akwarium',
      'model': 'ESP32 CYD',
      'manufacturer': 'AquaCYD',
      'firmware_version': '2.4.1',
      'area': 'Salon',
      'online': true,
      'last_seen_ms': 900,
    },
    <String, Object?>{
      'id': 'water_probe',
      'name': 'Sonda parametrów wody',
      'model': 'AquaSense C6',
      'manufacturer': 'AquaCYD',
      'firmware_version': '1.3.0',
      'area': 'Salon',
      'online': true,
      'last_seen_ms': 1300,
    },
    <String, Object?>{
      'id': 'aquahub_panel',
      'name': 'Panel ścienny AquaHub',
      'model': 'ESP32-P4 + ESP32-C6',
      'manufacturer': 'Waveshare',
      'firmware_version': '1.0.0',
      'area': 'Korytarz',
      'online': true,
      'last_seen_ms': 220,
    },
  ];

  List<Map<String, Object?>> get _entities => <Map<String, Object?>>[
    _entity(
      'water_temperature',
      'aquacyd_main',
      'Temperatura wody',
      'sensor',
      '°C',
    ),
    _entity('water_ph', 'water_probe', 'Odczyn pH', 'sensor', 'pH'),
    _entity('water_level', 'water_probe', 'Poziom wody', 'sensor', '%'),
    _entity('filter', 'aquacyd_main', 'Filtr', 'switch', '', writable: true),
    _entity(
      'aerator',
      'aquacyd_main',
      'Napowietrzanie',
      'switch',
      '',
      writable: true,
    ),
    _entity(
      'main_light',
      'aquacyd_main',
      'Oświetlenie',
      'light',
      '',
      writable: true,
    ),
    _entity(
      'light_brightness',
      'aquacyd_main',
      'Jasność światła',
      'number',
      '%',
      writable: true,
      minimum: 0,
      maximum: 100,
      step: 1,
    ),
    _entity(
      'feeding_mode',
      'aquacyd_main',
      'Tryb karmienia',
      'select',
      '',
      writable: true,
      options: const <String>['normalny', 'urlop', 'wstrzymany'],
    ),
    _entity(
      'leak_alarm',
      'water_probe',
      'Alarm zalania',
      'binary_sensor',
      '',
      critical: true,
    ),
  ];

  Map<String, Object?> _entity(
    String id,
    String deviceId,
    String name,
    String kind,
    String unit, {
    bool writable = false,
    bool critical = false,
    num? minimum,
    num? maximum,
    num? step,
    List<String> options = const <String>[],
  }) => <String, Object?>{
    'id': id,
    'device_id': deviceId,
    'name': name,
    'kind': kind,
    'unit': unit,
    'writable': writable,
    'critical': critical,
    'state': _states[id],
    'changed_at_ms': 14000,
    'updated_at_ms': 1200,
    'minimum': ?minimum,
    'maximum': ?maximum,
    'step': ?step,
    if (options.isNotEmpty) 'options': options,
  };

  http.StreamedResponse _accepted() =>
      _json(<String, Object?>{'accepted': true});

  http.StreamedResponse _error(int status, String message) =>
      _json(<String, Object?>{'error': message}, status: status);

  http.StreamedResponse _json(Map<String, Object?> value, {int status = 200}) {
    final bytes = utf8.encode(jsonEncode(value));
    return http.StreamedResponse(
      Stream<List<int>>.value(bytes),
      status,
      contentLength: bytes.length,
      headers: const <String, String>{
        'content-type': 'application/json; charset=utf-8',
      },
    );
  }
}
