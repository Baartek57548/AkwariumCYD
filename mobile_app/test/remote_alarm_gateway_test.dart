import 'dart:convert';

import 'package:cyd_aquarium_mobile/remote_gateway/remote_alarm_gateway.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';
// ignore: depend_on_referenced_packages
import 'package:shared_preferences_platform_interface/in_memory_shared_preferences_async.dart';
// ignore: depend_on_referenced_packages
import 'package:shared_preferences_platform_interface/shared_preferences_async_platform_interface.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  setUp(() {
    SharedPreferences.setMockInitialValues(<String, Object>{});
    SharedPreferencesAsyncPlatform.instance =
        InMemorySharedPreferencesAsync.empty();
  });

  test('normalizes HTTPS base and produces the documented health URL', () {
    final configuration = RemoteAlarmGatewayConfiguration(
      baseUrl: Uri.parse('https://gateway.example.com/aquacyd/'),
      deviceId: 'tank-room_1',
      enabled: true,
      hasViewerToken: true,
    );

    expect(
      configuration.healthUri.toString(),
      'https://gateway.example.com/aquacyd/api/v1/devices/'
      'tank-room_1/health',
    );
  });

  test('rejects cleartext, URL credentials and malformed device IDs', () {
    expect(
      () => RemoteAlarmGatewayConfiguration(
        baseUrl: Uri.parse('http://gateway.example.com'),
        deviceId: 'tank-1',
        enabled: false,
        hasViewerToken: false,
      ),
      throwsFormatException,
    );
    expect(
      () => RemoteAlarmGatewayConfiguration(
        baseUrl: Uri.parse('https://user:pass@gateway.example.com'),
        deviceId: 'tank-1',
        enabled: false,
        hasViewerToken: false,
      ),
      throwsFormatException,
    );
    expect(
      () => RemoteAlarmGatewayConfiguration(
        baseUrl: Uri.parse('https://gateway.example.com'),
        deviceId: '../tank',
        enabled: false,
        hasViewerToken: false,
      ),
      throwsFormatException,
    );
  });

  test('stores viewer token only through secret storage', () async {
    final secrets = _MemoryGatewaySecrets();
    final store = RemoteAlarmGatewayStore(secrets: secrets);
    final configuration = RemoteAlarmGatewayConfiguration(
      baseUrl: Uri.parse('https://gateway.example.com'),
      deviceId: 'aquacyd-salon',
      enabled: true,
      hasViewerToken: true,
    );
    const token = 'viewer-token-with-enough-entropy-123456';

    await store.save(configuration, newViewerToken: token);
    final loaded = await store.loadCredentials();
    final preferences = await SharedPreferences.getInstance();

    expect(loaded?.viewerToken, token);
    expect(loaded?.configuration.enabled, isTrue);
    expect(
      preferences.getKeys().where((key) => key.toLowerCase().contains('token')),
      isEmpty,
    );

    await store.clearViewerToken();
    expect(await store.loadCredentials(), isNull);
    expect(secrets.value, isNull);
  });

  test('creates exact ephemeral BLE provisioning payload', () {
    final secret = base64Encode(List<int>.generate(32, (index) => index));
    final request = RemoteGatewayProvisioningRequest(
      baseUrl: Uri.parse('https://gateway.example.com/aquacyd/'),
      deviceId: 'aquacyd-salon',
      hmacSecret: secret,
      enabled: true,
    );

    expect(request.hmacSecret, secret);
    expect(request.actionPayload, <String, Object?>{
      'baseUrl': 'https://gateway.example.com/aquacyd',
      'deviceId': 'aquacyd-salon',
      'hmacSecret': secret,
      'enabled': true,
    });
    expect(
      () => RemoteGatewayProvisioningRequest(
        baseUrl: Uri.parse('https://gateway.example.com'),
        deviceId: 'aquacyd-salon',
        hmacSecret: base64Encode(List<int>.filled(16, 7)),
        enabled: true,
      ),
      throwsFormatException,
    );
  });

  test('rejects spoofed HTTP 200 health payload for another device', () {
    expect(
      RemoteAlarmGatewayClient.isExpectedHealthPayload(const <String, Object?>{
        'status': 'ok',
        'deviceId': 'attacker-device',
      }, expectedDeviceId: 'aquacyd-salon'),
      isFalse,
    );
    expect(
      RemoteAlarmGatewayClient.isExpectedHealthPayload(const <String, Object?>{
        'status': 'ok',
        'deviceId': 'aquacyd-salon',
      }, expectedDeviceId: 'aquacyd-salon'),
      isTrue,
    );
    expect(
      RemoteAlarmGatewayClient.isExpectedHealthPayload(const <String, Object?>{
        'ok': true,
        'deviceId': 'aquacyd-salon',
      }, expectedDeviceId: 'aquacyd-salon'),
      isFalse,
    );
  });
}

final class _MemoryGatewaySecrets implements RemoteAlarmGatewaySecretStore {
  String? value;

  @override
  Future<void> clearViewerToken() async {
    value = null;
  }

  @override
  Future<String?> readViewerToken() async => value;

  @override
  Future<void> writeViewerToken(String value) async {
    this.value = value;
  }
}
