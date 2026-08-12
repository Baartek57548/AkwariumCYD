import 'dart:convert';
import 'dart:io';

import 'package:flutter_secure_storage/flutter_secure_storage.dart';

import 'alarm_models.dart';

abstract interface class AlarmRelay {
  Future<void> send(AlarmTransition transition);
}

final class DisabledAlarmRelay implements AlarmRelay {
  const DisabledAlarmRelay();

  @override
  Future<void> send(AlarmTransition transition) async {}
}

final class HomeAssistantWebhookConfiguration {
  HomeAssistantWebhookConfiguration(Uri endpoint)
    : endpoint = _validateEndpoint(endpoint);

  final Uri endpoint;

  static Uri _validateEndpoint(Uri value) {
    if (value.scheme != 'https' ||
        value.host.isEmpty ||
        value.userInfo.isNotEmpty ||
        value.hasQuery ||
        value.hasFragment ||
        value.toString().length > 2048) {
      throw const FormatException(
        'Webhook musi być adresem HTTPS bez loginu, parametrów i fragmentu.',
      );
    }
    final path = value.path;
    if (path.length < 12 || path == '/') {
      throw const FormatException('Ścieżka webhooka jest zbyt krótka.');
    }
    return value;
  }

  @override
  String toString() => '${endpoint.scheme}://${endpoint.host}/[ukryty-webhook]';
}

abstract interface class AlarmRelaySecretStore {
  Future<void> saveWebhook(HomeAssistantWebhookConfiguration configuration);
  Future<HomeAssistantWebhookConfiguration?> loadWebhook();
  Future<void> clearWebhook();
}

final class SecureAlarmRelaySecretStore implements AlarmRelaySecretStore {
  SecureAlarmRelaySecretStore({FlutterSecureStorage? storage})
    : _storage = storage ?? const FlutterSecureStorage();

  static const _webhookKey = 'alarm_center_home_assistant_webhook_v1';
  final FlutterSecureStorage _storage;

  @override
  Future<void> saveWebhook(HomeAssistantWebhookConfiguration configuration) {
    return _storage.write(
      key: _webhookKey,
      value: configuration.endpoint.toString(),
    );
  }

  @override
  Future<HomeAssistantWebhookConfiguration?> loadWebhook() async {
    try {
      final value = await _storage.read(key: _webhookKey);
      if (value == null || value.isEmpty) return null;
      return HomeAssistantWebhookConfiguration(Uri.parse(value));
    } on Object {
      return null;
    }
  }

  @override
  Future<void> clearWebhook() => _storage.delete(key: _webhookKey);
}

/// Opcjonalny relay HTTPS. Nie śledzi przekierowań, aby tajny identyfikator
/// webhooka nie wyciekł do innego hosta.
final class HomeAssistantWebhookRelay implements AlarmRelay {
  HomeAssistantWebhookRelay({
    required this.configuration,
    this.timeout = const Duration(seconds: 8),
    HttpClient Function()? clientFactory,
  }) : _clientFactory = clientFactory ?? HttpClient.new;

  final HomeAssistantWebhookConfiguration configuration;
  final Duration timeout;
  final HttpClient Function() _clientFactory;

  @override
  Future<void> send(AlarmTransition transition) async {
    final client = _clientFactory()
      ..connectionTimeout = timeout
      ..idleTimeout = timeout;
    try {
      final request = await client
          .postUrl(configuration.endpoint)
          .timeout(timeout);
      request
        ..followRedirects = false
        ..headers.contentType = ContentType.json
        ..headers.set(HttpHeaders.acceptHeader, 'application/json')
        ..write(
          jsonEncode(<String, Object?>{
            'schema': 1,
            'event': transition.type.name,
            'alarm': <String, Object?>{
              'key': transition.record.key,
              'severity': transition.record.severity.name,
              'state': transition.record.lifecycle.storageValue,
              'title': transition.record.title,
              'message': transition.record.message,
              'occurrences': transition.record.occurrences,
            },
            'occurredAt': transition.occurredAt.toUtc().toIso8601String(),
          }),
        );
      final response = await request.close().timeout(timeout);
      await response.take(4096).drain<void>().timeout(timeout);
      if (response.isRedirect ||
          response.statusCode < HttpStatus.ok ||
          response.statusCode >= HttpStatus.multipleChoices) {
        throw HttpException(
          'Webhook odrzucił zdarzenie (HTTP ${response.statusCode}).',
        );
      }
    } finally {
      client.close(force: true);
    }
  }
}

final class MqttAlarmMessage {
  const MqttAlarmMessage({
    required this.topic,
    required this.payload,
    required this.retain,
  });

  final String topic;
  final String payload;
  final bool retain;
}

/// Interfejs transportu pozwala podłączyć istniejącego klienta MQTT bez
/// narzucania biblioteki i sposobu przechowywania hasła w tym module.
abstract interface class MqttAlarmTransport {
  Future<void> publish(MqttAlarmMessage message);
}

final class MqttAlarmRelay implements AlarmRelay {
  MqttAlarmRelay({
    required this.transport,
    this.topicPrefix = 'aquacyd/alarm',
  }) {
    if (!RegExp(r'^[a-zA-Z0-9_/-]{1,120}$').hasMatch(topicPrefix) ||
        topicPrefix.contains('//') ||
        topicPrefix.startsWith('/') ||
        topicPrefix.endsWith('/')) {
      throw ArgumentError.value(topicPrefix, 'topicPrefix');
    }
  }

  final MqttAlarmTransport transport;
  final String topicPrefix;

  @override
  Future<void> send(AlarmTransition transition) {
    final record = transition.record;
    return transport.publish(
      MqttAlarmMessage(
        topic: '$topicPrefix/${record.key}',
        retain: true,
        payload: jsonEncode(<String, Object?>{
          'schema': 1,
          'event': transition.type.name,
          'key': record.key,
          'severity': record.severity.name,
          'state': record.lifecycle.storageValue,
          'title': record.title,
          'message': record.message,
          'occurredAt': transition.occurredAt.toUtc().toIso8601String(),
        }),
      ),
    );
  }
}
