import 'package:cyd_aquarium_mobile/full_controller/data_access.dart';
import 'package:cyd_aquarium_mobile/full_controller/status_decoder.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('accepts a complete firmware-compatible status', () {
    final status = decodeControllerStatus(
      _validStatus(
        history: const [
          {'value': 24.5, 'epoch': 1767225600},
          {'value': null, 'epoch': 1767225660},
        ],
      ),
      requireHistory: true,
    );

    expect(status.section('network').integer('rssi'), -52);
    expect(status.section('temperature').list('history'), hasLength(2));
  });

  test('rejects a syntactically valid but partial status', () {
    final status = _validStatus()..remove('network');

    expect(
      () => decodeControllerStatus(status),
      throwsA(isA<FormatException>()),
    );
  });

  test('rejects non-finite numbers anywhere in status', () {
    final status = _validStatus();
    status['diagnostic'] = {
      'nested': [double.nan],
    };

    expect(
      () => decodeControllerStatus(status),
      throwsA(isA<FormatException>()),
    );
  });

  test('rejects history above the bounded capacity', () {
    final status = _validStatus(
      history: List<dynamic>.generate(
        maximumStatusHistoryEntries + 1,
        (index) => {'value': 24.0, 'epoch': 1767225600 + index},
      ),
      historyCapacity: maximumStatusHistoryEntries,
    );

    expect(
      () => decodeControllerStatus(status, requireHistory: true),
      throwsA(isA<FormatException>()),
    );
  });

  test('requires history when it was explicitly requested', () {
    expect(
      () => decodeControllerStatus(_validStatus(), requireHistory: true),
      throwsA(isA<FormatException>()),
    );
  });

  test('numeric accessors safely reject NaN and infinity', () {
    final values = <String, dynamic>{
      'nan': double.nan,
      'infinity': double.infinity,
      'textNan': 'NaN',
    };

    expect(values.integer('nan', 7), 7);
    expect(values.number('infinity', 2.5), 2.5);
    expect(values.nullableNumber('textNan'), isNull);
  });
}

Map<String, dynamic> _validStatus({
  List<dynamic>? history,
  int historyCapacity = 144,
}) {
  final temperature = <String, dynamic>{
    'current': 24.6,
    'target': 25.0,
    'hysteresis': 0.5,
    'historyCapacity': historyCapacity,
  };
  if (history != null) {
    temperature['history'] = history;
  }
  final status = <String, dynamic>{
    'device': 'cydAkwarium',
    'sensors': {'temp_c': 24.6, 'temp_valid': true},
    'alarms': {'flags': 0},
    'config': {'target_temp': 25.0},
    'display': {'brightness': 80},
    'water': {'active': false},
    'leak': {'action': 'disable_all'},
    'modules': {'heater_on': false},
    'schedules': {'light': 'day'},
    'eco': {'safe_active': false},
    'clock': {'valid': true},
    'temperature': temperature,
    'battery': {'voltage': null},
    'firmware': {'version': 'test'},
    'network': {'rssi': -52},
    'web': {'focus': false},
    'system': {'uptime': 120, 'freeHeap': 180000},
    'relays': {'heater': false},
    'schedule': {'heaterMode': 0},
    'feeding': {'active': false},
  };
  return status;
}
