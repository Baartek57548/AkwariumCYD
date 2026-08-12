import 'package:cyd_aquarium_mobile/offline_drafts/offline_configuration_draft.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('creates deterministic base version and a readable diff', () {
    final draft = OfflineConfigurationDraft.create(
      kind: OfflineDraftKind.schedule,
      controllerId: 'aquacyd:salon',
      baseData: const <String, Object?>{
        'light': <String, Object?>{'start': '10:00', 'enabled': true},
      },
      editedData: const <String, Object?>{
        'light': <String, Object?>{'start': '11:30', 'enabled': true},
      },
      now: DateTime.utc(2026, 7, 29, 12),
    );

    expect(draft.baseVersion, hasLength(64));
    expect(draft.diff, hasLength(1));
    expect(draft.diff.single.path, 'light.start');
    expect(draft.diff.single.beforeLabel, '10:00');
    expect(draft.diff.single.afterLabel, '11:30');
    expect(
      draft.conflictsWith(const <String, Object?>{
        'light': <String, Object?>{'enabled': true, 'start': '10:00'},
      }),
      isFalse,
    );
    expect(
      draft.conflictsWith(const <String, Object?>{
        'light': <String, Object?>{'enabled': false, 'start': '10:00'},
      }),
      isTrue,
    );
  });

  test('secure repository round-trips and deletes a draft', () async {
    final storage = _MemoryDraftStorage();
    final repository = OfflineDraftRepository(storage: storage);
    final draft = OfflineConfigurationDraft.create(
      kind: OfflineDraftKind.settings,
      controllerId: 'aquacyd:office',
      baseData: const <String, Object?>{'brightness': 70},
      editedData: const <String, Object?>{'brightness': 45},
      now: DateTime.utc(2026, 7, 29, 12),
    );

    await repository.save(draft);
    final loaded = await repository.load(
      kind: OfflineDraftKind.settings,
      controllerId: 'aquacyd:office',
    );

    expect(loaded?.baseVersion, draft.baseVersion);
    expect(loaded?.editedData['brightness'], 45);

    await repository.delete(
      kind: OfflineDraftKind.settings,
      controllerId: 'aquacyd:office',
    );
    expect(
      await repository.load(
        kind: OfflineDraftKind.settings,
        controllerId: 'aquacyd:office',
      ),
      isNull,
    );
  });

  test('sensitive values never enter serialized draft', () {
    final draft = OfflineConfigurationDraft.create(
      kind: OfflineDraftKind.settings,
      controllerId: 'aquacyd:secure',
      baseData: const <String, Object?>{'brightness': 50, 'adminPin': '1234'},
      editedData: const <String, Object?>{
        'brightness': 60,
        'viewer_token': 'must-not-be-stored',
        'nested': <String, Object?>{'password': 'secret', 'safe': true},
      },
    );

    expect(draft.baseData, isNot(contains('adminPin')));
    expect(draft.editedData, isNot(contains('viewer_token')));
    expect(draft.editedData['nested'], const <String, Object?>{'safe': true});
  });
}

final class _MemoryDraftStorage implements OfflineDraftStorage {
  final Map<String, String> values = <String, String>{};

  @override
  Future<void> delete(String key) async {
    values.remove(key);
  }

  @override
  Future<String?> read(String key) async => values[key];

  @override
  Future<void> write(String key, String value) async {
    values[key] = value;
  }
}
