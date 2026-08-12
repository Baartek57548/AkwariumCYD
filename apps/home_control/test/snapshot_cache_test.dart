import 'package:aquacyd_home/src/home_control/demo_data_source.dart';
import 'package:aquacyd_home/src/home_control/snapshot_cache.dart';
import 'package:flutter_secure_storage/flutter_secure_storage.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:home_entities/home_entities.dart';
import 'package:secure_connectivity/secure_connectivity.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  setUp(() => FlutterSecureStorage.setMockInitialValues(<String, String>{}));

  test('encrypted cache restores a safe offline snapshot', () async {
    final source = DemoDataSource(clock: () => DateTime.utc(2026, 8, 12, 12));
    final original = await source.connect(CancellationToken());
    final cache = SecureHomeSnapshotCache();

    await cache.save(original);
    final restored = await cache.load(HomeSourceKind.demo);

    expect(restored, isNotNull);
    expect(restored!.isOffline, isTrue);
    expect(restored.sourceKind, HomeSourceKind.demo);
    expect(restored.entities.length, original.entities.length);
    expect(restored.entities.first.id, original.entities.first.id);
    expect(
      restored.entities.first.attributes,
      original.entities.first.attributes,
    );
    await source.close();
  });

  test('unknown cache schema is deleted instead of crashing', () async {
    const storage = FlutterSecureStorage();
    await storage.write(
      key: 'home_control_snapshot_v1_homeAssistant',
      value: '{"cache_schema":999}',
    );
    final cache = SecureHomeSnapshotCache(storage: storage);

    expect(await cache.load(HomeSourceKind.homeAssistant), isNull);
    expect(
      await storage.read(key: 'home_control_snapshot_v1_homeAssistant'),
      isNull,
    );
  });
}
