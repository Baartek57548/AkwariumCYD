import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:nsd/nsd.dart' as nsd;

final class DiscoveredHub {
  const DiscoveredHub({
    required this.name,
    required this.host,
    required this.port,
  });

  final String name;
  final String host;
  final int port;

  Uri get baseUri => Uri(scheme: 'https', host: host, port: port);

  String get addressLabel => '$host:$port';
}

abstract interface class HubDiscoveryService {
  Future<List<DiscoveredHub>> scan({
    Duration duration = const Duration(seconds: 4),
  });
}

final class HubDiscoveryException implements Exception {
  const HubDiscoveryException(this.message);

  final String message;

  @override
  String toString() => message;
}

final class NativeHubDiscoveryService implements HubDiscoveryService {
  static const _serviceType = '_aquahub._tcp';
  static final RegExp _hostLabelPattern = RegExp(r'^[A-Za-z0-9-]+$');

  @override
  Future<List<DiscoveredHub>> scan({
    Duration duration = const Duration(seconds: 4),
  }) async {
    if (kIsWeb) return const <DiscoveredHub>[];
    nsd.Discovery? discovery;
    try {
      discovery = await nsd.startDiscovery(
        _serviceType,
        ipLookupType: nsd.IpLookupType.any,
      );
      await Future<void>.delayed(duration);
      final hubs = <String, DiscoveredHub>{};
      for (final service in discovery.services) {
        final hub = _fromService(service);
        if (hub != null) hubs['${hub.host}:${hub.port}'] = hub;
      }
      final result = hubs.values.toList(growable: false)
        ..sort((left, right) => left.name.compareTo(right.name));
      return result;
    } on Object {
      throw const HubDiscoveryException(
        'System nie udostępnił wykrywania urządzeń w sieci lokalnej.',
      );
    } finally {
      if (discovery != null) {
        try {
          await nsd.stopDiscovery(discovery);
        } on Object {
          // Zatrzymanie discovery jest sprzątaniem best-effort; wynik skanu
          // pozostaje bezpieczny także po błędzie warstwy systemowej.
        }
      }
    }
  }

  DiscoveredHub? _fromService(nsd.Service service) {
    final port = service.port;
    if (port == null || port < 1 || port > 65535) return null;
    var host = (service.host ?? '').trim();
    while (host.endsWith('.')) {
      host = host.substring(0, host.length - 1);
    }
    if (host.isEmpty) {
      final addresses = service.addresses ?? const <InternetAddress>[];
      final usable = addresses.where((address) => !address.isLinkLocal);
      final preferred = usable.where(
        (address) => address.type == InternetAddressType.IPv4,
      );
      host = preferred.isNotEmpty
          ? preferred.first.address
          : usable.isNotEmpty
          ? usable.first.address
          : '';
    }
    if (!_validHost(host)) return null;
    final rawName = (service.name ?? '').trim();
    final name = rawName.isEmpty || rawName.length > 80 ? 'AquaHub' : rawName;
    return DiscoveredHub(name: name, host: host, port: port);
  }

  bool _validHost(String host) {
    if (host.isEmpty || host.length > 253) return false;
    if (InternetAddress.tryParse(host) != null) return true;
    final labels = host.split('.');
    if (labels.any(
      (label) =>
          label.isEmpty ||
          label.length > 63 ||
          label.startsWith('-') ||
          label.endsWith('-') ||
          !_hostLabelPattern.hasMatch(label),
    )) {
      return false;
    }
    return true;
  }
}
