import 'package:cyd_aquarium_mobile/alarm_center/alarm_notifications.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  testWidgets(
    'creates Android channels and delivers a real test notification',
    (tester) async {
      await tester.pumpWidget(
        const MaterialApp(
          home: Scaffold(
            body: Center(child: Text('AquaCYD notification smoke test')),
          ),
        ),
      );
      final notifications = LocalAlarmNotificationSink();

      await notifications.initialize();
      final granted = await notifications.requestPermission();

      expect(
        granted,
        isTrue,
        reason:
            'Grant POST_NOTIFICATIONS before the device smoke test. '
            'The release gate must exercise the real Android permission.',
      );
      await expectLater(notifications.showTestNotification(), completes);
    },
  );
}
