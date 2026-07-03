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
}
