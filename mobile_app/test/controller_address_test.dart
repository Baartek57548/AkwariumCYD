import 'package:cyd_aquarium_mobile/controller_address.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  group('ControllerAddress.parse', () {
    test('adds HTTP scheme to a local host', () {
      expect(
        ControllerAddress.parse('akwarium.local').toString(),
        'http://akwarium.local',
      );
    });

    test('preserves an explicit port and removes trailing slashes', () {
      expect(
        ControllerAddress.parse(' http://192.168.1.20:8000/// ').toString(),
        'http://192.168.1.20:8000',
      );
    });

    test('rejects unsafe and ambiguous addresses', () {
      for (final value in [
        '',
        'file:///tmp/panel.html',
        'http://user:secret@akwarium.local',
        'http://akwarium.local?pin=1234',
        'http://akwarium.local/#panel',
      ]) {
        expect(() => ControllerAddress.parse(value), throwsFormatException);
      }
    });
  });

  group('ControllerAddress.isSameController', () {
    test('accepts paths on the configured origin', () {
      final controller = Uri.parse('http://akwarium.local');
      expect(
        ControllerAddress.isSameController(
          Uri.parse('http://akwarium.local/api/status'),
          controller,
        ),
        isTrue,
      );
    });

    test('rejects a different scheme, host or port', () {
      final controller = Uri.parse('http://akwarium.local');
      expect(
        ControllerAddress.isSameController(
          Uri.parse('https://akwarium.local'),
          controller,
        ),
        isFalse,
      );
      expect(
        ControllerAddress.isSameController(
          Uri.parse('http://example.com'),
          controller,
        ),
        isFalse,
      );
      expect(
        ControllerAddress.isSameController(
          Uri.parse('http://akwarium.local:8000'),
          controller,
        ),
        isFalse,
      );
    });
  });
}
