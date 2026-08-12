import 'package:flutter_secure_storage/flutter_secure_storage.dart';

import '../domain/models.dart';

abstract interface class CredentialsStore {
  Future<HomeAssistantCredentials?> load();

  Future<void> save(HomeAssistantCredentials credentials);

  Future<void> clear();
}

final class SecureCredentialsStore implements CredentialsStore {
  SecureCredentialsStore({FlutterSecureStorage? storage})
    : _storage = storage ?? const FlutterSecureStorage();

  static const _baseUrlKey = 'home_assistant_base_url';
  static const _accessTokenKey = 'home_assistant_access_token';

  final FlutterSecureStorage _storage;

  @override
  Future<HomeAssistantCredentials?> load() async {
    final values = await _storage.readAll();
    final baseUrl = values[_baseUrlKey];
    final accessToken = values[_accessTokenKey];
    if (baseUrl == null || accessToken == null) {
      if (baseUrl != null || accessToken != null) {
        await clear();
      }
      return null;
    }

    try {
      return HomeAssistantCredentials.parse(
        baseUrl: baseUrl,
        accessToken: accessToken,
      );
    } on FormatException {
      await clear();
      return null;
    }
  }

  @override
  Future<void> save(HomeAssistantCredentials credentials) async {
    try {
      await _storage.write(
        key: _baseUrlKey,
        value: credentials.baseUri.toString(),
      );
      await _storage.write(
        key: _accessTokenKey,
        value: credentials.accessToken,
      );
    } on Object {
      await clear();
      rethrow;
    }
  }

  @override
  Future<void> clear() async {
    await Future.wait<void>([
      _storage.delete(key: _baseUrlKey),
      _storage.delete(key: _accessTokenKey),
    ]);
  }
}
