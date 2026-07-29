import 'data_access.dart';

const int maximumStatusHistoryEntries = 1440;
const int maximumStatusListEntries = 2048;
const List<String> requiredControllerStatusSections = [
  'sensors',
  'alarms',
  'config',
  'display',
  'water',
  'leak',
  'modules',
  'schedules',
  'eco',
  'clock',
  'temperature',
  'battery',
  'firmware',
  'network',
  'web',
  'system',
  'relays',
  'schedule',
  'feeding',
];

/// Validates the controller status before it becomes application state.
///
/// The firmware may close a connection in the middle of a streamed JSON
/// response. A syntactically valid but partial object must not replace the last
/// known-good state, because that could present missing safety data as `false`.
JsonMap decodeControllerStatus(Object? value, {bool requireHistory = false}) {
  final status = _requireStringMap(value, r'$');
  _validateNode(status, r'$', 0);

  final device = status['device'];
  if (device is! String || device.trim().isEmpty || device.length > 64) {
    throw const FormatException(
      r'Pole $.device musi być niepustym tekstem o długości do 64 znaków.',
    );
  }

  final sections = <String, JsonMap>{};
  for (final key in requiredControllerStatusSections) {
    final section = _requireSection(status, key);
    if (section.isEmpty) {
      throw FormatException('Sekcja \$.$key nie może być pusta.');
    }
    sections[key] = section;
  }

  final sensors = sections['sensors']!;
  final modules = sections['modules']!;
  final temperature = sections['temperature']!;
  final network = sections['network']!;
  final system = sections['system']!;

  _requireBool(sensors, 'temp_valid', r'$.sensors');
  _requireNullableFiniteNumber(sensors, 'temp_c', r'$.sensors');

  _requireBool(modules, 'heater_on', r'$.modules');

  _requireNullableFiniteNumber(temperature, 'current', r'$.temperature');
  _requireFiniteNumber(temperature, 'target', r'$.temperature');
  _requireFiniteNumber(temperature, 'hysteresis', r'$.temperature');
  final historyCapacity = _requireInteger(
    temperature,
    'historyCapacity',
    r'$.temperature',
  );
  if (historyCapacity < 0 || historyCapacity > maximumStatusHistoryEntries) {
    throw FormatException(
      r'Pole $.temperature.historyCapacity wykracza poza zakres '
      '0..$maximumStatusHistoryEntries.',
    );
  }

  _requireFiniteNumber(network, 'rssi', r'$.network');
  _requireFiniteNumber(system, 'uptime', r'$.system');
  _requireFiniteNumber(system, 'freeHeap', r'$.system');

  final historyValue = temperature['history'];
  if (requireHistory && historyValue is! List<dynamic>) {
    throw const FormatException(
      'Odpowiedź statusu nie zawiera wymaganej historii temperatury.',
    );
  }
  if (historyValue != null) {
    if (historyValue is! List<dynamic>) {
      throw const FormatException(
        r'Pole $.temperature.history musi być listą.',
      );
    }
    _validateHistory(historyValue, historyCapacity);
  }

  return status;
}

JsonMap _requireSection(JsonMap status, String key) {
  final value = status[key];
  if (value is! Map<String, dynamic>) {
    throw FormatException('Brak wymaganej sekcji \$.$key.');
  }
  return value;
}

JsonMap _requireStringMap(Object? value, String path) {
  if (value is! Map) {
    throw FormatException('$path musi być obiektem JSON.');
  }
  final result = <String, dynamic>{};
  for (final entry in value.entries) {
    if (entry.key is! String) {
      throw FormatException('$path zawiera klucz, który nie jest tekstem.');
    }
    result[entry.key as String] = entry.value;
  }
  return result;
}

void _validateNode(Object? value, String path, int depth) {
  const maximumDepth = 16;
  const maximumMapEntries = 256;
  const maximumStringLength = 128 * 1024;

  if (depth > maximumDepth) {
    throw FormatException('$path przekracza maksymalną głębokość JSON.');
  }
  if (value is double && !value.isFinite) {
    throw FormatException('$path zawiera niefinitywną wartość liczbową.');
  }
  if (value is String && value.length > maximumStringLength) {
    throw FormatException('$path przekracza limit długości tekstu.');
  }
  if (value is List) {
    if (value.length > maximumStatusListEntries) {
      throw FormatException(
        '$path przekracza limit $maximumStatusListEntries elementów.',
      );
    }
    for (var index = 0; index < value.length; index++) {
      _validateNode(value[index], '$path[$index]', depth + 1);
    }
    return;
  }
  if (value is Map) {
    if (value.length > maximumMapEntries) {
      throw FormatException('$path przekracza limit $maximumMapEntries pól.');
    }
    for (final entry in value.entries) {
      final key = entry.key;
      if (key is! String || key.length > 128) {
        throw FormatException('$path zawiera nieprawidłowy klucz.');
      }
      _validateNode(entry.value, '$path.$key', depth + 1);
    }
  }
}

void _validateHistory(List<dynamic> history, int historyCapacity) {
  if (history.length > maximumStatusHistoryEntries) {
    throw FormatException(
      'Historia temperatury przekracza limit '
      '$maximumStatusHistoryEntries próbek.',
    );
  }
  if (history.length > historyCapacity) {
    throw const FormatException(
      'Historia temperatury jest większa niż pojemność zgłoszona '
      'przez sterownik.',
    );
  }
  for (var index = 0; index < history.length; index++) {
    final sample = history[index];
    if (sample is! Map<String, dynamic>) {
      throw FormatException(
        'Próbka \$.temperature.history[$index] nie jest obiektem.',
      );
    }
    _requireNullableFiniteNumber(
      sample,
      'value',
      '\$.temperature.history[$index]',
    );
    final epoch = _requireInteger(
      sample,
      'epoch',
      '\$.temperature.history[$index]',
    );
    if (epoch < 0 || epoch > 4102444800) {
      throw FormatException(
        'Próbka \$.temperature.history[$index] ma nieprawidłowy czas.',
      );
    }
  }
}

void _requireBool(JsonMap map, String key, String path) {
  if (map[key] is! bool) {
    throw FormatException('Pole $path.$key musi być wartością logiczną.');
  }
}

num _requireFiniteNumber(JsonMap map, String key, String path) {
  final value = map[key];
  if (value is! num || (value is double && !value.isFinite)) {
    throw FormatException('Pole $path.$key musi być skończoną liczbą.');
  }
  return value;
}

void _requireNullableFiniteNumber(JsonMap map, String key, String path) {
  if (!map.containsKey(key)) {
    throw FormatException('Brak wymaganego pola $path.$key.');
  }
  final value = map[key];
  if (value != null &&
      (value is! num || (value is double && !value.isFinite))) {
    throw FormatException(
      'Pole $path.$key musi być null albo skończoną liczbą.',
    );
  }
}

int _requireInteger(JsonMap map, String key, String path) {
  final value = _requireFiniteNumber(map, key, path);
  if (value is int) {
    return value;
  }
  final asDouble = value.toDouble();
  if (asDouble != asDouble.truncateToDouble()) {
    throw FormatException('Pole $path.$key musi być liczbą całkowitą.');
  }
  return asDouble.toInt();
}
