import 'package:cyd_aquarium_mobile/alarm_center/alarm_notifications.dart';
import 'package:flutter/material.dart';
import 'package:flutter_local_notifications/flutter_local_notifications.dart';
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

      final plugin = FlutterLocalNotificationsPlugin();
      var activeNotifications = <ActiveNotification>[];
      for (var attempt = 0; attempt < 20; attempt++) {
        activeNotifications = await plugin.getActiveNotifications();
        if (activeNotifications.any(
          (notification) =>
              notification.channelId ==
                  LocalAlarmNotificationSink.warningChannelId &&
              notification.title == 'AquaCYD Control' &&
              notification.body ==
                  'Powiadomienia systemowe działają poprawnie.',
        )) {
          break;
        }
        await Future<void>.delayed(const Duration(milliseconds: 250));
      }

      expect(
        activeNotifications,
        contains(
          isA<ActiveNotification>()
              .having(
                (notification) => notification.channelId,
                'channelId',
                LocalAlarmNotificationSink.warningChannelId,
              )
              .having(
                (notification) => notification.title,
                'title',
                'AquaCYD Control',
              )
              .having(
                (notification) => notification.body,
                'body',
                'Powiadomienia systemowe działają poprawnie.',
              ),
        ),
        reason:
            'Android musi raportować aktywne powiadomienie utworzone przez '
            'produkcyjny sink, a nie tylko poprawne zakończenie wywołania.',
      );
    },
  );
}
