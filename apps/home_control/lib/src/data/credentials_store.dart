import 'dart:convert';

import 'package:flutter_secure_storage/flutter_secure_storage.dart';

import '../domain/models.dart';

abstract interface class CredentialsStore {
  Future<HomeAssistantCredentials?> load();

  Future<void> save(HomeAssistantCredentials credentials);

  Future<void> clear();
}

final class HomeAssistantProfile {
  const HomeAssistantProfile({
    required this.id,
    required this.name,
    required this.baseUri,
  });

  final String id;
  final String name;
  final Uri baseUri;
}

abstract interface class HomeAssistantProfileStore {
  Future<List<HomeAssistantProfile>> listProfiles();

  Future<String?> selectedProfileId();

  Future<HomeAssistantCredentials?> loadProfile(String id);

  Future<String> saveProfile({
    required HomeAssistantCredentials credentials,
    required String name,
    String? profileId,
  });

  Future<void> selectProfile(String id);

  Future<void> deleteProfile(String id);
}

final class SecureCredentialsStore
    implements CredentialsStore, HomeAssistantProfileStore {
  SecureCredentialsStore({FlutterSecureStorage? storage})
    : _storage = storage ?? const FlutterSecureStorage();

  static const _baseUrlKey = 'home_assistant_base_url';
  static const _accessTokenKey = 'home_assistant_access_token';
  static const _profileIndexKey = 'home_assistant_profile_index_v1';
  static const _selectedProfileKey = 'home_assistant_selected_profile_v1';
  static const _profilePrefix = 'home_assistant_profile_v1_';

  final FlutterSecureStorage _storage;

  @override
  Future<HomeAssistantCredentials?> load() async {
    final selected = await selectedProfileId();
    if (selected != null) {
      final profile = await loadProfile(selected);
      if (profile != null) return profile;
    }
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
    final profiles = await listProfiles();
    await Future.wait<void>([
      _storage.delete(key: _baseUrlKey),
      _storage.delete(key: _accessTokenKey),
      _storage.delete(key: _profileIndexKey),
      _storage.delete(key: _selectedProfileKey),
      for (final profile in profiles)
        _storage.delete(key: '$_profilePrefix${profile.id}'),
    ]);
  }

  @override
  Future<List<HomeAssistantProfile>> listProfiles() async {
    final raw = await _storage.read(key: _profileIndexKey);
    if (raw == null || raw.isEmpty) return const <HomeAssistantProfile>[];
    try {
      final decoded = jsonDecode(raw);
      if (decoded is! List<Object?>) throw const FormatException();
      final result = <HomeAssistantProfile>[];
      for (final item in decoded) {
        if (item is! Map<Object?, Object?>) continue;
        final id = item['id'];
        final name = item['name'];
        final uri = Uri.tryParse(item['base_url']?.toString() ?? '');
        if (id is String &&
            _validProfileId.hasMatch(id) &&
            name is String &&
            name.trim().isNotEmpty &&
            uri != null &&
            uri.hasAuthority) {
          result.add(
            HomeAssistantProfile(id: id, name: name.trim(), baseUri: uri),
          );
        }
      }
      result.sort(
        (a, b) => a.name.toLowerCase().compareTo(b.name.toLowerCase()),
      );
      return List<HomeAssistantProfile>.unmodifiable(result);
    } on Object {
      await _storage.delete(key: _profileIndexKey);
      await _storage.delete(key: _selectedProfileKey);
      return const <HomeAssistantProfile>[];
    }
  }

  @override
  Future<String?> selectedProfileId() async {
    final value = await _storage.read(key: _selectedProfileKey);
    return value != null && _validProfileId.hasMatch(value) ? value : null;
  }

  @override
  Future<HomeAssistantCredentials?> loadProfile(String id) async {
    if (!_validProfileId.hasMatch(id)) return null;
    final raw = await _storage.read(key: '$_profilePrefix$id');
    if (raw == null) return null;
    try {
      final decoded = jsonDecode(raw);
      if (decoded is! Map<Object?, Object?>) throw const FormatException();
      return HomeAssistantCredentials.parse(
        baseUrl: decoded['base_url']?.toString() ?? '',
        accessToken: decoded['access_token']?.toString() ?? '',
      );
    } on Object {
      await deleteProfile(id);
      return null;
    }
  }

  @override
  Future<String> saveProfile({
    required HomeAssistantCredentials credentials,
    required String name,
    String? profileId,
  }) async {
    final normalizedName = name.trim();
    if (normalizedName.isEmpty || normalizedName.length > 80) {
      throw const FormatException('Invalid Home Assistant profile name.');
    }
    final id = profileId ?? _newProfileId();
    if (!_validProfileId.hasMatch(id)) {
      throw const FormatException('Invalid Home Assistant profile ID.');
    }
    final profiles = List<HomeAssistantProfile>.from(await listProfiles());
    profiles.removeWhere((profile) => profile.id == id);
    profiles.add(
      HomeAssistantProfile(
        id: id,
        name: normalizedName,
        baseUri: credentials.baseUri,
      ),
    );
    profiles.sort(
      (a, b) => a.name.toLowerCase().compareTo(b.name.toLowerCase()),
    );
    await _storage.write(
      key: '$_profilePrefix$id',
      value: jsonEncode(<String, String>{
        'base_url': credentials.baseUri.toString(),
        'access_token': credentials.accessToken,
      }),
    );
    await _storage.write(
      key: _profileIndexKey,
      value: jsonEncode(<Map<String, String>>[
        for (final profile in profiles)
          <String, String>{
            'id': profile.id,
            'name': profile.name,
            'base_url': profile.baseUri.toString(),
          },
      ]),
    );
    await selectProfile(id);
    return id;
  }

  @override
  Future<void> selectProfile(String id) async {
    if (!_validProfileId.hasMatch(id) || await loadProfile(id) == null) {
      throw const FormatException('Home Assistant profile does not exist.');
    }
    await _storage.write(key: _selectedProfileKey, value: id);
  }

  @override
  Future<void> deleteProfile(String id) async {
    if (!_validProfileId.hasMatch(id)) return;
    final profiles = List<HomeAssistantProfile>.from(await listProfiles())
      ..removeWhere((profile) => profile.id == id);
    await _storage.delete(key: '$_profilePrefix$id');
    await _storage.write(
      key: _profileIndexKey,
      value: jsonEncode(<Map<String, String>>[
        for (final profile in profiles)
          <String, String>{
            'id': profile.id,
            'name': profile.name,
            'base_url': profile.baseUri.toString(),
          },
      ]),
    );
    if (await selectedProfileId() == id) {
      if (profiles.isEmpty) {
        await _storage.delete(key: _selectedProfileKey);
      } else {
        await _storage.write(
          key: _selectedProfileKey,
          value: profiles.first.id,
        );
      }
    }
  }

  static final RegExp _validProfileId = RegExp(r'^ha-[a-z0-9]{8,32}$');

  static String _newProfileId() {
    final micros = DateTime.now().microsecondsSinceEpoch.toRadixString(36);
    return 'ha-${micros.padLeft(8, '0')}';
  }
}
