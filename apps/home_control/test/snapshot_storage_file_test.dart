import 'dart:io';

import 'package:aquacyd_home/src/home_control/snapshot_storage_factory_io.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  late Directory temporaryDirectory;
  late FileSnapshotStorage storage;

  setUp(() async {
    temporaryDirectory = await Directory.systemTemp.createTemp(
      'home-control-snapshot-storage-',
    );
    storage = FileSnapshotStorage(
      directoryProvider: () async => temporaryDirectory,
    );
  });

  tearDown(() async {
    if (await temporaryDirectory.exists()) {
      await temporaryDirectory.delete(recursive: true);
    }
  });

  test(
    'stores independent cache entries without exposing keys in paths',
    () async {
      await storage.write(
        key: 'home_control_snapshot_v2_demo_first',
        value: '{"v":1}',
      );
      await storage.write(
        key: 'home_control_snapshot_v2_demo_second',
        value: '{"v":2}',
      );

      expect(
        await storage.read(key: 'home_control_snapshot_v2_demo_first'),
        '{"v":1}',
      );
      expect(await storage.readAll(), <String, String>{
        'home_control_snapshot_v2_demo_first': '{"v":1}',
        'home_control_snapshot_v2_demo_second': '{"v":2}',
      });
      final names = await temporaryDirectory
          .list()
          .map((entity) => entity.uri.pathSegments.last)
          .toList();
      expect(
        names,
        everyElement(
          matches(RegExp(r'^[a-f0-9]{64}-[0-9]{20}-[a-f0-9]{8}\.json$')),
        ),
      );
    },
  );

  test(
    'removes corrupt cache entries without affecting valid entries',
    () async {
      await storage.write(key: 'valid', value: 'snapshot');
      final corrupt = File(
        '${temporaryDirectory.path}${Platform.pathSeparator}bad.json',
      );
      await corrupt.writeAsString('{broken', flush: true);

      expect(await storage.readAll(), <String, String>{'valid': 'snapshot'});
      expect(await corrupt.exists(), isFalse);
      expect(await storage.read(key: 'valid'), 'snapshot');
    },
  );

  test('deletes only the requested cache entry', () async {
    await storage.write(key: 'first', value: 'one');
    await storage.write(key: 'second', value: 'two');

    await storage.delete(key: 'first');

    expect(await storage.read(key: 'first'), isNull);
    expect(await storage.read(key: 'second'), 'two');
  });
}
