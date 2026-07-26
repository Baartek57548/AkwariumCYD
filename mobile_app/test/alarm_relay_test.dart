import 'dart:convert';

import 'package:cyd_aquarium_mobile/alarm_center/alarm_models.dart';
import 'package:cyd_aquarium_mobile/alarm_center/alarm_relay.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('webhook akceptuje wyłącznie bezpieczny HTTPS i maskuje sekret', () {
    final configuration = HomeAssistantWebhookConfiguration(
      Uri.parse('https://hooks.example.com/api/webhook/tajny-identyfikator'),
    );

    expect(configuration.toString(), isNot(contains('tajny-identyfikator')));
    expect(
      () => HomeAssistantWebhookConfiguration(
        Uri.parse('http://hooks.example.com/api/webhook/sekret'),
      ),
      throwsFormatException,
    );
    expect(
      () => HomeAssistantWebhookConfiguration(
        Uri.parse('https://user:pass@example.com/api/webhook/sekret'),
      ),
      throwsFormatException,
    );
  });

  test('adapter MQTT publikuje ograniczony payload bez sekretów', () async {
    final transport = _FakeMqttTransport();
    final relay = MqttAlarmRelay(transport: transport);
    final at = DateTime.utc(2026, 7, 26, 12);
    final record = AlarmRecord(
      key: 'leak',
      severity: AlarmSeverity.critical,
      lifecycle: AlarmLifecycle.newAlarm,
      title: 'Wyciek',
      message: 'Wykryto wodę.',
      firstTriggeredAt: at,
      lastTriggeredAt: at,
      occurrences: 1,
    );

    await relay.send(
      AlarmTransition(
        type: AlarmTransitionType.opened,
        record: record,
        occurredAt: at,
      ),
    );

    final message = transport.messages.single;
    final payload = jsonDecode(message.payload) as Map<String, dynamic>;
    expect(message.topic, 'aquacyd/alarm/leak');
    expect(message.retain, isTrue);
    expect(payload['state'], 'new');
    expect(payload, isNot(contains('token')));
    expect(payload, isNot(contains('controllerAddress')));
  });
}

final class _FakeMqttTransport implements MqttAlarmTransport {
  final List<MqttAlarmMessage> messages = <MqttAlarmMessage>[];

  @override
  Future<void> publish(MqttAlarmMessage message) async {
    messages.add(message);
  }
}
