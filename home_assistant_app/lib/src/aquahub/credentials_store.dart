import 'package:flutter_secure_storage/flutter_secure_storage.dart';

import 'domain.dart';

abstract interface class HubCredentialsStore {
  Future<HubCredentials?> load();

  Future<void> save(HubCredentials credentials);

  Future<void> clear();
}

final class SecureHubCredentialsStore implements HubCredentialsStore {
  SecureHubCredentialsStore({FlutterSecureStorage? storage})
    : _storage = storage ?? const FlutterSecureStorage();

  static const _baseUrlKey = 'aquahub_base_url';
  static const _accessTokenKey = 'aquahub_access_token';
  static const _fingerprintKey = 'aquahub_tls_sha256';

  final FlutterSecureStorage _storage;

  @override
  Future<HubCredentials?> load() async {
    final values = await _storage.readAll();
    final url = values[_baseUrlKey];
    final token = values[_accessTokenKey];
    final fingerprint = values[_fingerprintKey];
    if (url == null || token == null || fingerprint == null) {
      if (url != null || token != null || fingerprint != null) await clear();
      return null;
    }
    try {
      return HubCredentials.parse(
        baseUrl: url,
        accessToken: token,
        tlsFingerprint: fingerprint,
      );
    } on FormatException {
      await clear();
      return null;
    }
  }

  @override
  Future<void> save(HubCredentials credentials) async {
    try {
      await _storage.write(
        key: _baseUrlKey,
        value: credentials.baseUri.toString(),
      );
      await _storage.write(
        key: _accessTokenKey,
        value: credentials.accessToken,
      );
      await _storage.write(
        key: _fingerprintKey,
        value: credentials.tlsFingerprint,
      );
    } on Object {
      await clear();
      rethrow;
    }
  }

  @override
  Future<void> clear() async {
    await Future.wait<void>(<Future<void>>[
      _storage.delete(key: _baseUrlKey),
      _storage.delete(key: _accessTokenKey),
      _storage.delete(key: _fingerprintKey),
    ]);
  }
}
