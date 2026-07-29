import 'package:cyd_aquarium_mobile/alarm_center/alarm_models.dart';
import 'package:cyd_aquarium_mobile/alarm_center/alarm_notifications.dart';
import 'package:cyd_aquarium_mobile/alarm_center/alarm_preferences.dart';
import 'package:cyd_aquarium_mobile/notifications/notification_intents.dart';
import 'package:cyd_aquarium_mobile/remote_gateway/remote_push.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test(
    'maps validated push payloads to dedicated notification types',
    () async {
      final notifications = _RecordingNotificationSink();
      final handler = RemotePushPayloadHandler(
        notifications,
        preferencesLoader: () async =>
            const AlarmNotificationPreferences(enabled: true),
      );

      await handler.handle(<String, dynamic>{
        'type': 'alarm',
        'id': 'boot123-42',
        'eventType': 'temperature.high',
        'deviceId': 'aquacyd-salon',
        'title': 'Za wysoka temperatura',
        'body': 'Temperatura wody przekroczyła 29°C.',
        'severity': 'critical',
        'state': 'raised',
        'occurredAt': '2026-07-29T14:00:00Z',
      });
      await handler.handle(<String, dynamic>{
        'type': 'service',
        'id': 'filter_change',
        'title': 'Serwis filtra',
        'body': 'Czas wyczyścić filtr.',
      });
      await handler.handle(<String, dynamic>{
        'type': 'update',
        'id': 'mobile_release_6_0_0',
        'title': 'Nowa wersja',
        'body': 'AquaCYD 6.0.0 jest gotowa.',
        'version': '6.0.0',
        'tag': 'mobile-v6.0.0',
      });

      expect(notifications.alarms, hasLength(1));
      expect(
        notifications.alarms.single.key,
        'remote:aquacyd-salon:temperature.high',
      );
      expect(notifications.alarms.single.severity, AlarmSeverity.critical);
      expect(notifications.services, <String>['filter_change']);
      expect(notifications.updates, <String>['mobile-v6.0.0']);
    },
  );

  test('rejects malformed or unknown remote payloads', () async {
    final handler = RemotePushPayloadHandler(_RecordingNotificationSink());

    await expectLater(
      handler.handle(<String, dynamic>{
        'type': 'alarm',
        'id': '../unsafe',
        'title': 'Alarm',
        'body': 'Treść',
      }),
      throwsFormatException,
    );
    await expectLater(
      handler.handle(<String, dynamic>{
        'type': 'alarm',
        'id': 'event-12345678',
        'eventType': 'temperature.high',
        'deviceId': 'aquacyd-salon',
        'title': 'Alarm',
        'body': 'Treść',
        'severity': 'emergency',
        'state': 'raised',
      }),
      throwsFormatException,
    );
    await expectLater(
      handler.handle(<String, dynamic>{
        'type': 'update',
        'id': 'release',
        'title': 'Aktualizacja',
        'body': 'Treść',
        'version': '5.2',
        'tag': 'mobile-v5.2',
      }),
      throwsFormatException,
    );
    await expectLater(
      handler.handle(<String, dynamic>{
        'type': 'unsupported',
        'id': 'event',
        'title': 'Zdarzenie',
        'body': 'Treść',
      }),
      throwsFormatException,
    );
  });

  test(
    'compacts a valid long remote alarm identity to the local key limit',
    () async {
      final notifications = _RecordingNotificationSink();
      final handler = RemotePushPayloadHandler(
        notifications,
        preferencesLoader: () async =>
            const AlarmNotificationPreferences(enabled: true),
      );
      final deviceId = 'device-${List<String>.filled(57, 'a').join()}';
      final eventType = 'sensor.${List<String>.filled(41, 'b').join()}';
      final payload = <String, dynamic>{
        'type': 'alarm',
        'id': 'event-12345678',
        'eventType': eventType,
        'deviceId': deviceId,
        'title': 'Alarm zdalny',
        'body': 'Zweryfikowana treść alarmu.',
        'severity': 'warning',
        'state': 'raised',
      };

      await handler.handle(payload);

      expect(notifications.alarms.single.key, startsWith('remote:'));
      expect(notifications.alarms.single.key.length, lessThanOrEqualTo(64));
      expect(
        handler.intentFor(payload)?.identifier,
        notifications.alarms.single.key,
      );
    },
  );

  test(
    'resolved alarm cancels active notification without creating a new alarm',
    () async {
      final notifications = _RecordingNotificationSink();
      final handler = RemotePushPayloadHandler(
        notifications,
        preferencesLoader: () async => const AlarmNotificationPreferences(
          enabled: true,
          resolvedEnabled: true,
        ),
      );
      final payload = <String, dynamic>{
        'type': 'alarm',
        'id': 'boot123-43',
        'eventType': 'leak.detected',
        'deviceId': 'aquacyd-salon',
        'title': 'Wyciek usunięty',
        'body': 'Czujnik zalania wrócił do stanu bezpiecznego.',
        'severity': 'critical',
        'state': 'resolved',
        'occurredAt': '2026-07-29T14:05:00Z',
      };

      await handler.handle(payload);

      expect(notifications.alarms, isEmpty);
      expect(notifications.canceledAlarms, <String>[
        'remote:aquacyd-salon:leak.detected',
      ]);
      expect(notifications.resolved, hasLength(1));
      expect(notifications.resolved.single.lifecycle, AlarmLifecycle.resolved);
      expect(
        handler.intentFor(payload),
        isA<AquaNotificationIntent>()
            .having(
              (intent) => intent.target,
              'target',
              AquaNotificationTarget.alarm,
            )
            .having(
              (intent) => intent.identifier,
              'identifier',
              'remote:aquacyd-salon:leak.detected',
            ),
      );
    },
  );

  test('runtime Firebase options require all dart defines', () {
    const complete = RemotePushRuntimeConfiguration(
      apiKey: 'api-key',
      appId: '1:123456789:android:abcdef',
      messagingSenderId: '123456789',
      projectId: 'aquacyd-production',
    );
    const incomplete = RemotePushRuntimeConfiguration(
      apiKey: '',
      appId: '',
      messagingSenderId: '',
      projectId: '',
    );

    expect(complete.isComplete, isTrue);
    expect(complete.firebaseOptions.projectId, 'aquacyd-production');
    expect(complete.firebaseOptions.appId, '1:123456789:android:abcdef');
    expect(incomplete.isComplete, isFalse);
    expect(() => incomplete.firebaseOptions, throwsStateError);
  });

  test('unavailable adapter remains a no-op local fallback', () async {
    const adapter = UnavailableRemotePushAdapter();
    var delivered = false;

    await adapter.initialize(
      onMessage: (_) async => delivered = true,
      onMessageOpened: (_) async => delivered = true,
    );

    expect(adapter.isSupported, isFalse);
    expect(adapter.isInitialized, isFalse);
    expect(await adapter.getToken(), isNull);
    expect(delivered, isFalse);
    await adapter.dispose();
  });
}

final class _RecordingNotificationSink implements AlarmNotificationSink {
  final List<AlarmRecord> alarms = <AlarmRecord>[];
  final List<AlarmRecord> resolved = <AlarmRecord>[];
  final List<String> canceledAlarms = <String>[];
  final List<String> services = <String>[];
  final List<String> updates = <String>[];

  @override
  Future<void> cancelAlarm(String alarmKey) async {
    canceledAlarms.add(alarmKey);
  }

  @override
  Future<void> initialize() async {}

  @override
  Future<bool> requestPermission() async => true;

  @override
  Future<void> showAlarm(AlarmRecord alarm) async {
    alarms.add(alarm);
  }

  @override
  Future<void> showAppUpdate({
    required String tagName,
    required String version,
    required bool downloaded,
  }) async {
    updates.add(tagName);
  }

  @override
  Future<void> showResolved(AlarmRecord alarm) async {
    resolved.add(alarm);
  }

  @override
  Future<void> showServiceReminder({
    required String id,
    required String title,
    required String body,
  }) async {
    services.add(id);
  }

  @override
  Future<void> showTestNotification() async {}
}
