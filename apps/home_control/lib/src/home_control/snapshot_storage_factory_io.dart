import 'dart:convert';
import 'dart:io';
import 'dart:math';

import 'package:crypto/crypto.dart';
import 'package:path_provider/path_provider.dart';

import 'snapshot_storage.dart';

typedef SnapshotDirectoryProvider = Future<Directory> Function();

SnapshotStorage createPlatformSnapshotStorage() => FileSnapshotStorage();

final class FileSnapshotStorage implements SnapshotStorage {
  FileSnapshotStorage({SnapshotDirectoryProvider? directoryProvider})
    : _directoryProvider = directoryProvider ?? _defaultDirectory;

  static const int _maximumEnvelopeBytes = 640 * 1024;

  final SnapshotDirectoryProvider _directoryProvider;
  final Random _random = Random.secure();

  static Future<Directory> _defaultDirectory() async {
    final applicationDirectory = await getApplicationCacheDirectory();
    return Directory(
      '${applicationDirectory.path}${Platform.pathSeparator}snapshots',
    );
  }

  @override
  Future<String?> read({required String key}) async {
    final files = await _filesFor(key);
    files.sort((left, right) => right.path.compareTo(left.path));
    for (final file in files) {
      try {
        if (await file.length() > _maximumEnvelopeBytes) {
          await file.delete();
          continue;
        }
        final envelope = _decodeEnvelope(await file.readAsString(), key: key);
        return envelope.value;
      } on Object {
        await _deleteIfPresent(file);
      }
    }
    return null;
  }

  @override
  Future<void> write({required String key, required String value}) async {
    final directory = await _ensureDirectory();
    final payload = jsonEncode(<String, String>{'key': key, 'value': value});
    if (utf8.encode(payload).length > _maximumEnvelopeBytes) {
      throw const FormatException('Snapshot storage envelope is too large.');
    }
    final uniqueSuffix =
        '${DateTime.now().toUtc().microsecondsSinceEpoch.toString().padLeft(20, '0')}-'
        '${_random.nextInt(1 << 32).toRadixString(16).padLeft(8, '0')}';
    final target = File(
      '${directory.path}${Platform.pathSeparator}${_keyDigest(key)}-'
      '$uniqueSuffix.json',
    );
    final temporary = File('${target.path}.tmp');
    try {
      await temporary.writeAsString(payload, encoding: utf8, flush: true);
      await temporary.rename(target.path);
      for (final previous in await _filesFor(key)) {
        if (previous.path != target.path) await _deleteIfPresent(previous);
      }
    } finally {
      await _deleteIfPresent(temporary);
    }
  }

  @override
  Future<void> delete({required String key}) async {
    for (final file in await _filesFor(key)) {
      await _deleteIfPresent(file);
    }
  }

  @override
  Future<Map<String, String>> readAll() async {
    final directory = await _directoryProvider();
    if (!await directory.exists()) return <String, String>{};
    final values = <String, _StoredSnapshot>{};
    await for (final entity in directory.list(followLinks: false)) {
      if (entity is! File || !entity.path.endsWith('.json')) continue;
      try {
        if (await entity.length() > _maximumEnvelopeBytes) {
          await entity.delete();
          continue;
        }
        final envelope = _decodeEnvelope(await entity.readAsString());
        final modified = await entity.lastModified();
        final previous = values[envelope.key];
        if (previous == null || modified.isAfter(previous.modified)) {
          values[envelope.key] = _StoredSnapshot(envelope.value, modified);
        }
      } on Object {
        await _deleteIfPresent(entity);
      }
    }
    return <String, String>{
      for (final entry in values.entries) entry.key: entry.value.value,
    };
  }

  Future<List<File>> _filesFor(String key) async {
    final directory = await _directoryProvider();
    if (!await directory.exists()) return <File>[];
    final prefix = '${_keyDigest(key)}-';
    return directory
        .list(followLinks: false)
        .where(
          (entity) =>
              entity is File &&
              _fileName(entity.path).startsWith(prefix) &&
              entity.path.endsWith('.json'),
        )
        .cast<File>()
        .toList();
  }

  Future<Directory> _ensureDirectory() async {
    final directory = await _directoryProvider();
    if (!await directory.exists()) {
      await directory.create(recursive: true);
    }
    return directory;
  }

  static String _keyDigest(String key) =>
      sha256.convert(utf8.encode(key)).toString();

  static String _fileName(String path) =>
      path.split(Platform.pathSeparator).last;

  static _SnapshotEnvelope _decodeEnvelope(String encoded, {String? key}) {
    final decoded = jsonDecode(encoded);
    if (decoded is! Map<String, Object?>) throw const FormatException();
    final storedKey = decoded['key'];
    final value = decoded['value'];
    if (storedKey is! String ||
        storedKey.isEmpty ||
        value is! String ||
        (key != null && storedKey != key)) {
      throw const FormatException();
    }
    return _SnapshotEnvelope(storedKey, value);
  }

  static Future<void> _deleteIfPresent(File file) async {
    try {
      if (await file.exists()) await file.delete();
    } on FileSystemException {
      if (await file.exists()) rethrow;
    }
  }
}

final class _SnapshotEnvelope {
  const _SnapshotEnvelope(this.key, this.value);

  final String key;
  final String value;
}

final class _StoredSnapshot {
  const _StoredSnapshot(this.value, this.modified);

  final String value;
  final DateTime modified;
}
