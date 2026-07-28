import 'dart:io';

class ControllerAddressException implements FormatException {
  const ControllerAddressException(this.message);

  @override
  final String message;

  @override
  int? get offset => null;

  @override
  String? get source => null;

  @override
  String toString() => message;
}

abstract final class ControllerAddress {
  static const String defaultValue = 'http://akwarium.local';

  static Uri parse(String input) {
    var candidate = input.trim();
    if (candidate.isEmpty) {
      throw const ControllerAddressException(
        'Podaj adres sterownika, np. http://akwarium.local.',
      );
    }

    if (!candidate.contains('://')) {
      candidate = 'http://$candidate';
    }

    final uri = Uri.tryParse(candidate);
    if (uri == null || uri.host.isEmpty) {
      throw const ControllerAddressException(
        'Adres sterownika jest nieprawidłowy.',
      );
    }
    if (uri.scheme != 'http' && uri.scheme != 'https') {
      throw const ControllerAddressException(
        'Dozwolone są wyłącznie adresy HTTP i HTTPS.',
      );
    }
    if (uri.userInfo.isNotEmpty) {
      throw const ControllerAddressException(
        'Adres nie może zawierać loginu ani hasła.',
      );
    }
    if (uri.hasQuery || uri.hasFragment) {
      throw const ControllerAddressException(
        'Podaj adres bazowy bez parametrów i fragmentu.',
      );
    }
    if (uri.scheme == 'http' && !isLocalNetworkUri(uri, allowLoopback: true)) {
      throw const ControllerAddressException(
        'Nieszyfrowany HTTP jest dozwolony wyłącznie dla sterownika w sieci lokalnej.',
      );
    }

    final normalizedPath = uri.path == '/' || uri.path.isEmpty
        ? ''
        : uri.path.replaceFirst(RegExp(r'/+$'), '');
    return uri.replace(path: normalizedPath);
  }

  static bool isSameController(Uri candidate, Uri controller) {
    return candidate.scheme == controller.scheme &&
        candidate.host.toLowerCase() == controller.host.toLowerCase() &&
        candidate.port == controller.port;
  }

  static bool isLocalNetworkUri(Uri uri, {bool allowLoopback = false}) {
    final host = uri.host.toLowerCase();
    if (host.isEmpty || host == 'localhost' || host.endsWith('.localhost')) {
      return false;
    }
    final ip = InternetAddress.tryParse(host);
    if (ip != null) {
      if (ip.isLoopback) {
        return allowLoopback;
      }
      final bytes = ip.rawAddress;
      if (ip.type == InternetAddressType.IPv4) {
        return bytes[0] == 10 ||
            (bytes[0] == 172 && bytes[1] >= 16 && bytes[1] <= 31) ||
            (bytes[0] == 192 && bytes[1] == 168) ||
            (bytes[0] == 169 && bytes[1] == 254);
      }
      return (bytes[0] & 0xfe) == 0xfc ||
          (bytes[0] == 0xfe && (bytes[1] & 0xc0) == 0x80);
    }
    if (host.endsWith('.local')) {
      return true;
    }
    return !host.contains('.') &&
        RegExp(r'^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$').hasMatch(host);
  }
}
