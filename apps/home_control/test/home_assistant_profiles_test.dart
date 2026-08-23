import 'package:aquacyd_home/src/data/credentials_store.dart';
import 'package:aquacyd_home/src/domain/models.dart';
import 'package:flutter_secure_storage/flutter_secure_storage.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  setUp(() => FlutterSecureStorage.setMockInitialValues(<String, String>{}));

  test(
    'secure store creates, selects and deletes independent HA profiles',
    () async {
      final store = SecureCredentialsStore();
      final home = HomeAssistantCredentials.parse(
        baseUrl: 'https://home.example.net',
        accessToken: 'a' * 32,
      );
      final office = HomeAssistantCredentials.parse(
        baseUrl: 'https://office.example.net',
        accessToken: 'b' * 32,
      );

      final homeId = await store.saveProfile(credentials: home, name: 'Dom');
      final officeId = await store.saveProfile(
        credentials: office,
        name: 'Biuro',
      );

      expect(await store.listProfiles(), hasLength(2));
      expect(await store.selectedProfileId(), officeId);
      expect((await store.load())?.baseUri.host, 'office.example.net');

      await store.selectProfile(homeId);
      expect((await store.load())?.accessToken, 'a' * 32);

      await store.deleteProfile(homeId);
      expect(await store.selectedProfileId(), officeId);
      expect(await store.loadProfile(homeId), isNull);
      expect((await store.load())?.baseUri.host, 'office.example.net');
    },
  );

  test(
    'corrupt profile index is cleared without exposing credentials',
    () async {
      FlutterSecureStorage.setMockInitialValues(<String, String>{
        'home_assistant_profile_index_v1': '{broken',
        'home_assistant_selected_profile_v1': 'ha-12345678',
      });
      final store = SecureCredentialsStore();

      expect(await store.listProfiles(), isEmpty);
      expect(await store.selectedProfileId(), isNull);
    },
  );
}
