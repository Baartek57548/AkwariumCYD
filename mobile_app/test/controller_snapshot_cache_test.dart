import 'dart:convert';

import 'package:cyd_aquarium_mobile/controller_snapshot_cache.dart';
import 'package:cyd_aquarium_mobile/full_controller/controller_session.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';
// ignore: depend_on_referenced_packages
import 'package:shared_preferences_platform_interface/in_memory_shared_preferences_async.dart';
// ignore: depend_on_referenced_packages
import 'package:shared_preferences_platform_interface/shared_preferences_async_platform_interface.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  late SharedPreferencesAsync preferences;
  late ControllerSnapshotCache cache;

  setUp(() {
    SharedPreferencesAsyncPlatform.instance =
        InMemorySharedPreferencesAsync.empty();
    preferences = SharedPreferencesAsync();
    cache = ControllerSnapshotCache(preferences: preferences);
  });

  test('zapisuje i odczytuje wersjonowany snapshot', () async {
    final savedAt = DateTime.utc(2026, 7, 26, 12, 30);
    final stored = await cache.save(<String, dynamic>{
      'temperature': 25.4,
      'online': true,
      'network': <String, dynamic>{'rssi': -55},
    }, savedAt: savedAt);

    final snapshot = await cache.load();

    expect(stored, isTrue);
    expect(snapshot, isNotNull);
    expect(snapshot!.savedAt, savedAt);
    expect(snapshot.status['temperature'], 25.4);
    expect(snapshot.status['online'], isTrue);
    expect((snapshot.status['network'] as Map<String, dynamic>)['rssi'], -55);
    expect(() => snapshot.status['temperature'] = 30, throwsUnsupportedError);
  });

  test('rekurencyjnie usuwa wszystkie klucze poufne', () async {
    await cache.save(<String, dynamic>{
      'adminPin': '1234',
      'wifi_password': 'tajne',
      'accessTokenValue': 'token',
      'secretKey': 'sekret',
      'dataPin': 17,
      'pinRequired': true,
      'safe': <String, dynamic>{
        'temperature': 24.8,
        'auth_token': 'ukryj',
        'items': <dynamic>[
          <String, dynamic>{'devicePassword': 'ukryj', 'value': 7},
        ],
      },
    });

    final status = (await cache.load())!.status;
    final safe = status['safe'] as Map<String, dynamic>;
    final item = (safe['items'] as List).single as Map<String, dynamic>;

    expect(status.keys, isNot(contains('adminPin')));
    expect(status.keys, isNot(contains('wifi_password')));
    expect(status.keys, isNot(contains('accessTokenValue')));
    expect(status.keys, isNot(contains('secretKey')));
    expect(status['dataPin'], 17);
    expect(status['pinRequired'], isTrue);
    expect(safe, isNot(contains('auth_token')));
    expect(item, isNot(contains('devicePassword')));
    expect(item['value'], 7);
  });

  test('ogranicza tekst, listy, historię i głębokość', () {
    const codec = ControllerSnapshotCodec();
    final encoded = codec.encode(<String, dynamic>{
      'description': List<String>.filled(3000, 'x').join(),
      'values': List<int>.generate(150, (index) => index),
      'history': List<int>.generate(80, (index) => index),
      'nested': <String, dynamic>{
        'level2': <String, dynamic>{
          'level3': <String, dynamic>{
            'level4': <String, dynamic>{
              'level5': <String, dynamic>{
                'level6': <String, dynamic>{
                  'level7': <String, dynamic>{
                    'level8': <String, dynamic>{'tooDeep': true},
                  },
                },
              },
            },
          },
        },
      },
    }, DateTime.utc(2026));

    final snapshot = codec.decode(encoded!);

    expect(
      (snapshot!.status['description'] as String).length,
      ControllerSnapshotCodec.maximumStringLength,
    );
    expect(
      (snapshot.status['values'] as List).length,
      ControllerSnapshotCodec.maximumListItems,
    );
    final history = snapshot.status['history'] as List;
    expect(history.length, ControllerSnapshotCodec.maximumHistoryItems);
    expect(history.first, 32);
    expect(history.last, 79);
    expect(jsonEncode(snapshot.status), isNot(contains('tooDeep')));
  });

  test('odrzuca nieobsługiwany schemat i uszkodzone dane', () {
    const codec = ControllerSnapshotCodec();

    expect(codec.decode('{uszkodzone'), isNull);
    expect(
      codec.decode(
        jsonEncode(<String, dynamic>{
          'schemaVersion': 99,
          'savedAt': DateTime.utc(2026).toIso8601String(),
          'status': <String, dynamic>{},
        }),
      ),
      isNull,
    );
    expect(
      codec.decode(
        jsonEncode(<String, dynamic>{
          'schemaVersion': ControllerSnapshotCodec.schemaVersion,
          'savedAt': 'nie-data',
          'status': <String, dynamic>{},
        }),
      ),
      isNull,
    );
  });

  test('usuwa uszkodzony wpis z pamięci preferencji', () async {
    await preferences.setString(
      ControllerSnapshotCache.storageKey,
      '{niepoprawny-json',
    );

    expect(await cache.load(), isNull);
    expect(
      await preferences.getString(ControllerSnapshotCache.storageKey),
      isNull,
    );
  });

  test('odrzuca wpis większy od limitu', () async {
    final status = <String, dynamic>{
      for (
        var index = 0;
        index < ControllerSnapshotCodec.maximumMapEntries;
        index++
      )
        'field$index': List<String>.filled(1500, '${index}_').join(),
    };

    expect(await cache.save(status), isFalse);
    expect(await cache.load(), isNull);
  });

  test('clear usuwa zapisany snapshot', () async {
    expect(await cache.save(<String, dynamic>{'temperature': 25}), isTrue);

    expect(await cache.clear(), isTrue);
    expect(await cache.load(), isNull);
  });

  test('mieści kompletny status centrum dowodzenia', () async {
    final session = ControllerSession.development();
    addTearDown(session.dispose);

    expect(await cache.save(session.status), isTrue);
    final snapshot = await cache.load();

    expect(snapshot, isNotNull);
    expect(snapshot!.status['device'], session.status['device']);
    expect(snapshot.status['sensors'], isA<Map<String, dynamic>>());
    expect(snapshot.status['schedules'], isA<Map<String, dynamic>>());
  });
}
