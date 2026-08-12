import 'package:cyd_aquarium_mobile/notifications/notification_intents.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('parses alarm deep link and maps acknowledge action', () {
    const source = AquaNotificationIntent(
      target: AquaNotificationTarget.alarm,
      identifier: 'water_level.low',
    );

    final parsed = AquaNotificationIntent.tryParse(
      source.payload,
      actionId: AquaNotificationIntent.alarmAcknowledgeActionId,
    );

    expect(parsed, isNotNull);
    expect(parsed!.target, AquaNotificationTarget.alarm);
    expect(parsed.identifier, 'water_level.low');
    expect(parsed.action, AquaNotificationAction.acknowledgeAlarm);
  });

  test('parses update and service actions independently', () {
    final update = AquaNotificationIntent.tryParse(
      'aquacyd://update/mobile-v6.0.0',
      actionId: AquaNotificationIntent.updateRemindActionId,
    );
    final service = AquaNotificationIntent.tryParse(
      'aquacyd://service/filter_change',
      actionId: AquaNotificationIntent.serviceCompleteActionId,
    );

    expect(update?.action, AquaNotificationAction.remindUpdateLater);
    expect(update?.identifier, 'mobile-v6.0.0');
    expect(service?.action, AquaNotificationAction.completeService);
  });

  test('rejects malformed, foreign and oversized payloads', () {
    expect(
      AquaNotificationIntent.tryParse('https://example.com/alarm/leak'),
      isNull,
    );
    expect(
      AquaNotificationIntent.tryParse('aquacyd://update/not-a-version'),
      isNull,
    );
    expect(
      AquaNotificationIntent.tryParse('aquacyd://alarm/${'a' * 200}'),
      isNull,
    );
  });

  test('serializes persisted intent without losing action', () {
    const source = AquaNotificationIntent(
      target: AquaNotificationTarget.service,
      identifier: 'water_change',
      action: AquaNotificationAction.completeService,
    );

    final restored = AquaNotificationIntent.tryFromJson(source.toJson());

    expect(restored?.target, source.target);
    expect(restored?.identifier, source.identifier);
    expect(restored?.action, source.action);
  });
}
