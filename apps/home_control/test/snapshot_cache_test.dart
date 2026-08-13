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
    final restored = await cache.load(HomeSourceKind.demo, original.sourceId);

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

    expect(await cache.load(HomeSourceKind.homeAssistant, 'ha-main'), isNull);
    expect(
      await storage.read(key: 'home_control_snapshot_v1_homeAssistant'),
      isNull,
    );
  });

  test('oversized attributes degrade to a bounded partial cache', () async {
    final source = DemoDataSource(clock: () => DateTime.utc(2026, 8, 12, 12));
    final original = await source.connect(CancellationToken());
    addTearDown(source.close);
    final first = original.entities.first.copyWith(
      attributes: <String, Object?>{
        'oversized_payload': List<String>.filled(600 * 1024, 'x').join(),
      },
    );
    final oversized = HomeSnapshot(
      schemaVersion: original.schemaVersion,
      sourceId: original.sourceId,
      sourceName: original.sourceName,
      sourceKind: HomeSourceKind.homeAssistant,
      areas: original.areas,
      devices: original.devices,
      entities: <HomeEntity>[first, ...original.entities.skip(1)],
      automations: original.automations,
      updates: original.updates,
      synchronizedAt: original.synchronizedAt,
      isPartial: false,
      isOffline: false,
    );
    final cache = SecureHomeSnapshotCache();

    await cache.save(oversized);
    final restored = await cache.load(
      HomeSourceKind.homeAssistant,
      oversized.sourceId,
    );

    expect(restored, isNotNull);
    expect(restored!.isOffline, isTrue);
    expect(restored.isPartial, isTrue);
    expect(restored.entities, hasLength(oversized.entities.length));
    expect(restored.entities.first.attributes, isEmpty);
  });

  test('Home Assistant profiles keep independent cache entries', () async {
    final cache = SecureHomeSnapshotCache();
    final home = _emptySnapshot('ha-aaaaaaaa', 'Dom');
    final office = _emptySnapshot('ha-bbbbbbbb', 'Biuro');

    await cache.save(home);
    await cache.save(office);

    expect(
      (await cache.load(
        HomeSourceKind.homeAssistant,
        home.sourceId,
      ))?.sourceName,
      'Dom',
    );
    expect(
      (await cache.load(
        HomeSourceKind.homeAssistant,
        office.sourceId,
      ))?.sourceName,
      'Biuro',
    );

    await cache.clear(HomeSourceKind.homeAssistant, sourceId: office.sourceId);
    expect(
      await cache.load(HomeSourceKind.homeAssistant, office.sourceId),
      isNull,
    );
    expect(
      await cache.load(HomeSourceKind.homeAssistant, home.sourceId),
      isNotNull,
    );
  });
}

HomeSnapshot _emptySnapshot(String sourceId, String sourceName) => HomeSnapshot(
  schemaVersion: HomeSnapshot.currentSchemaVersion,
  sourceId: sourceId,
  sourceName: sourceName,
  sourceKind: HomeSourceKind.homeAssistant,
  areas: const <HomeArea>[],
  devices: const <HomeDevice>[],
  entities: const <HomeEntity>[],
  automations: const <HomeAutomation>[],
  updates: const <HomeUpdate>[],
  synchronizedAt: DateTime.utc(2026, 8, 13, 10),
  isPartial: false,
  isOffline: false,
);
