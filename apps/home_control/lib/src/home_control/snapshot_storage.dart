import 'package:flutter_secure_storage/flutter_secure_storage.dart';

abstract interface class SnapshotStorage {
  Future<String?> read({required String key});

  Future<void> write({required String key, required String value});

  Future<void> delete({required String key});

  Future<Map<String, String>> readAll();
}

final class SecureSnapshotStorage implements SnapshotStorage {
  const SecureSnapshotStorage(this._storage);

  final FlutterSecureStorage _storage;

  @override
  Future<String?> read({required String key}) => _storage.read(key: key);

  @override
  Future<void> write({required String key, required String value}) =>
      _storage.write(key: key, value: value);

  @override
  Future<void> delete({required String key}) => _storage.delete(key: key);

  @override
  Future<Map<String, String>> readAll() => _storage.readAll();
}
