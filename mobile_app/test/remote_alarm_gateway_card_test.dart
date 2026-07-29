import 'dart:convert';

import 'package:cyd_aquarium_mobile/remote_gateway/remote_alarm_gateway.dart';
import 'package:cyd_aquarium_mobile/remote_gateway/remote_alarm_gateway_card.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
// ignore: depend_on_referenced_packages
import 'package:shared_preferences_platform_interface/in_memory_shared_preferences_async.dart';
// ignore: depend_on_referenced_packages
import 'package:shared_preferences_platform_interface/shared_preferences_async_platform_interface.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  setUp(() {
    SharedPreferencesAsyncPlatform.instance =
        InMemorySharedPreferencesAsync.empty();
  });

  testWidgets(
    'provisions only through callback and clears ephemeral HMAC field',
    (tester) async {
      _configureViewport(tester);
      final store = RemoteAlarmGatewayStore(secrets: _MemoryGatewaySecrets());
      RemoteGatewayProvisioningRequest? captured;
      var clearCalls = 0;
      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(
            body: SingleChildScrollView(
              child: RemoteAlarmGatewayCard(
                store: store,
                onProvisionController: (request) async {
                  captured = request;
                  return 'Sterownik zapisał bramkę.';
                },
                onClearControllerProvisioning: () async {
                  clearCalls += 1;
                  return 'Sterownik wyczyścił bramkę.';
                },
              ),
            ),
          ),
        ),
      );
      await tester.pump();
      await tester.pump();

      await tester.tap(find.text('Bezpieczna bramka zdalna'));
      await tester.pumpAndSettle();
      await tester.enterText(
        find.byKey(const Key('remote-gateway-base-url-field')),
        'https://gateway.example.com/aquacyd/',
      );
      await tester.enterText(
        find.byKey(const Key('remote-gateway-device-id-field')),
        'aquacyd-salon',
      );
      final secret = base64Encode(List<int>.generate(32, (index) => index));
      await tester.enterText(
        find.byKey(const Key('remote-gateway-hmac-secret-field')),
        secret,
      );
      await tester.tap(find.text('Włącz zdalne alarmy'));
      await tester.pump();
      final provisionButton = find.byKey(
        const Key('remote-gateway-provision-button'),
      );
      await tester.ensureVisible(provisionButton);
      await tester.tap(provisionButton);
      await tester.pumpAndSettle();

      expect(captured, isNotNull);
      expect(captured!.enabled, isTrue);
      expect(
        captured!.baseUrl.toString(),
        'https://gateway.example.com/aquacyd',
      );
      expect(captured!.deviceId, 'aquacyd-salon');
      expect(captured!.hmacSecret, secret);
      final hmacField = tester.widget<TextFormField>(
        find.byKey(const Key('remote-gateway-hmac-secret-field')),
      );
      expect(hmacField.controller?.text, isEmpty);
      expect(find.text('Sterownik zapisał bramkę.'), findsOneWidget);

      final clearButton = find.byKey(
        const Key('remote-gateway-clear-controller-button'),
      );
      await tester.ensureVisible(clearButton);
      await tester.tap(clearButton);
      await tester.pumpAndSettle();
      expect(clearCalls, 1);
    },
  );

  testWidgets('explains and blocks provisioning outside secure BLE callback', (
    tester,
  ) async {
    _configureViewport(tester);
    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(
          body: SingleChildScrollView(
            child: RemoteAlarmGatewayCard(
              store: RemoteAlarmGatewayStore(secrets: _MemoryGatewaySecrets()),
              provisioningUnavailableReason:
                  'Provisioning przez Wi‑Fi i HTTP jest zablokowany.',
            ),
          ),
        ),
      ),
    );
    await tester.pump();
    await tester.pump();
    await tester.tap(find.text('Bezpieczna bramka zdalna'));
    await tester.pumpAndSettle();

    expect(
      find.text('Provisioning przez Wi‑Fi i HTTP jest zablokowany.'),
      findsOneWidget,
    );
    final provision = tester.widget<FilledButton>(
      find.byKey(const Key('remote-gateway-provision-button')),
    );
    final clear = tester.widget<OutlinedButton>(
      find.byKey(const Key('remote-gateway-clear-controller-button')),
    );
    expect(provision.onPressed, isNull);
    expect(clear.onPressed, isNull);
  });
}

void _configureViewport(WidgetTester tester) {
  tester.view.devicePixelRatio = 1;
  tester.view.physicalSize = const Size(700, 1600);
  addTearDown(tester.view.resetDevicePixelRatio);
  addTearDown(tester.view.resetPhysicalSize);
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
