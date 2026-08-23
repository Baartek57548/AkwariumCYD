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
        'http://example.com',
      ]) {
        expect(() => ControllerAddress.parse(value), throwsFormatException);
      }
    });

    test('allows an explicit loopback IP for local development', () {
      expect(
        ControllerAddress.parse('http://127.0.0.1:8080').toString(),
        'http://127.0.0.1:8080',
      );
    });

    test('allows a public host only over HTTPS', () {
      expect(
        ControllerAddress.parse('https://controller.example.com').toString(),
        'https://controller.example.com',
      );
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
