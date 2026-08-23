import 'dart:async';
import 'dart:io';

import 'package:flutter/foundation.dart';

typedef HomeAssistantHostResolver =
    Future<List<InternetAddress>> Function(String host);

final class HomeAssistantNetworkPolicyException implements Exception {
  const HomeAssistantNetworkPolicyException(this.message);

  final String message;

  @override
  String toString() => message;
}

final class HomeAssistantDestination {
  const HomeAssistantDestination({required this.uri, this.hostHeader});

  final Uri uri;
  final String? hostHeader;
}

abstract final class HomeAssistantNetworkPolicy {
  static Future<HomeAssistantDestination> pinCleartextDestination(
    Uri uri, {
    required HomeAssistantHostResolver resolver,
    required Duration timeout,
  }) async {
    final cleartext = uri.scheme == 'http' || uri.scheme == 'ws';
    if (!cleartext) return HomeAssistantDestination(uri: uri);
    if (kIsWeb) {
      throw const HomeAssistantNetworkPolicyException(
        'Nieszyfrowane połączenie Home Assistant nie jest dozwolone w wersji web.',
      );
    }

    final literal = InternetAddress.tryParse(uri.host);
    final List<InternetAddress> addresses;
    try {
      addresses = literal == null
          ? await resolver(uri.host).timeout(timeout)
          : <InternetAddress>[literal];
    } on Object {
      throw const HomeAssistantNetworkPolicyException(
        'Nie udało się bezpiecznie rozwiązać lokalnego adresu Home Assistant.',
      );
    }
    if (addresses.isEmpty || addresses.any((address) => !isLocal(address))) {
      throw const HomeAssistantNetworkPolicyException(
        'Adres HTTP Home Assistanta nie wskazuje wyłącznie sieci lokalnej.',
      );
    }

    final selected = addresses.firstWhere(
      (address) => address.type == InternetAddressType.IPv4,
      orElse: () => addresses.first,
    );
    return HomeAssistantDestination(
      uri: uri.replace(host: selected.address),
      hostHeader: uri.authority,
    );
  }

  static bool isLocal(InternetAddress address) {
    final bytes = address.rawAddress;
    if (address.type == InternetAddressType.IPv4 && bytes.length == 4) {
      return _isLocalIpv4(bytes);
    }
    if (address.type != InternetAddressType.IPv6 || bytes.length != 16) {
      return false;
    }
    final loopback =
        bytes.take(15).every((value) => value == 0) && bytes[15] == 1;
    final uniqueLocal = (bytes[0] & 0xFE) == 0xFC;
    final linkLocal = bytes[0] == 0xFE && (bytes[1] & 0xC0) == 0x80;
    final mappedIpv4 =
        bytes.take(10).every((value) => value == 0) &&
        bytes[10] == 0xFF &&
        bytes[11] == 0xFF &&
        _isLocalIpv4(bytes.sublist(12));
    return loopback || uniqueLocal || linkLocal || mappedIpv4;
  }

  static bool _isLocalIpv4(List<int> bytes) {
    final first = bytes[0];
    final second = bytes[1];
    return first == 10 ||
        first == 127 ||
        (first == 169 && second == 254) ||
        (first == 172 && second >= 16 && second <= 31) ||
        (first == 192 && second == 168);
  }
}
