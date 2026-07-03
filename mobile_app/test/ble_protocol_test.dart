import 'dart:convert';

import 'package:cyd_aquarium_mobile/connectivity/ble_protocol.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('reassembles framed UTF-8 message received out of order', () {
    final assembler = BleFrameAssembler();
    final payload = utf8.encode('{"type":"status","temp":25.4}');
    final first = <int>[42, 0, 0, 2, ...payload.sublist(0, 14)];
    final second = <int>[42, 0, 1, 2, ...payload.sublist(14)];

    expect(assembler.add(second), isNull);
    expect(assembler.add(first), '{"type":"status","temp":25.4}');
  });

  test('rejects invalid frame metadata', () {
    final assembler = BleFrameAssembler();
    expect(() => assembler.add([1, 0, 0]), throwsFormatException);
    expect(() => assembler.add([1, 0, 1, 1, 65]), throwsFormatException);
    expect(() => assembler.add([1, 0, 0, 0, 65]), throwsFormatException);
  });
}
