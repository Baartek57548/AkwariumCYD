import 'dart:async';
import 'dart:convert';

import 'package:flutter_local_notifications/flutter_local_notifications.dart';
import 'package:shared_preferences/shared_preferences.dart';

enum AquaNotificationTarget { alarm, service, update }

enum AquaNotificationAction {
  open,
  acknowledgeAlarm,
  completeService,
  remindUpdateLater,
}

final class AquaNotificationIntent {
  const AquaNotificationIntent({
    required this.target,
    required this.identifier,
    this.action = AquaNotificationAction.open,
  });

  static const String alarmAcknowledgeActionId = 'alarm_acknowledge';
  static const String serviceCompleteActionId = 'service_complete';
  static const String updateRemindActionId = 'update_remind_later';

  final AquaNotificationTarget target;
  final String identifier;
  final AquaNotificationAction action;

  String get payload {
    final encoded = Uri.encodeComponent(identifier);
    return 'aquacyd://${target.name}/$encoded';
  }

  AquaNotificationIntent withAction(AquaNotificationAction value) {
    return AquaNotificationIntent(
      target: target,
      identifier: identifier,
      action: value,
    );
  }

  Map<String, Object?> toJson() => <String, Object?>{
    'target': target.name,
    'identifier': identifier,
    'action': action.name,
  };

  static AquaNotificationIntent? tryParse(String? payload, {String? actionId}) {
    if (payload == null || payload.length > 512) return null;
    final uri = Uri.tryParse(payload);
    if (uri == null ||
        uri.scheme != 'aquacyd' ||
        uri.userInfo.isNotEmpty ||
        uri.hasQuery ||
        uri.hasFragment ||
        uri.pathSegments.length != 1) {
      return null;
    }
    final target = AquaNotificationTarget.values
        .where((candidate) => candidate.name == uri.host)
        .firstOrNull;
    if (target == null) return null;
    final identifier = Uri.decodeComponent(uri.pathSegments.single).trim();
    if (!_isValidIdentifier(identifier, target)) return null;

    final action = switch (actionId) {
      alarmAcknowledgeActionId when target == AquaNotificationTarget.alarm =>
        AquaNotificationAction.acknowledgeAlarm,
      serviceCompleteActionId when target == AquaNotificationTarget.service =>
        AquaNotificationAction.completeService,
      updateRemindActionId when target == AquaNotificationTarget.update =>
        AquaNotificationAction.remindUpdateLater,
      _ => AquaNotificationAction.open,
    };
    return AquaNotificationIntent(
      target: target,
      identifier: identifier,
      action: action,
    );
  }

  static AquaNotificationIntent? tryFromJson(Object? value) {
    if (value is! Map) return null;
    final targetName = value['target'];
    final identifier = value['identifier'];
    final actionName = value['action'];
    if (targetName is! String ||
        identifier is! String ||
        actionName is! String) {
      return null;
    }
    final target = AquaNotificationTarget.values
        .where((candidate) => candidate.name == targetName)
        .firstOrNull;
    final action = AquaNotificationAction.values
        .where((candidate) => candidate.name == actionName)
        .firstOrNull;
    if (target == null ||
        action == null ||
        !_isValidIdentifier(identifier, target)) {
      return null;
    }
    return AquaNotificationIntent(
      target: target,
      identifier: identifier,
      action: action,
    );
  }

  static bool _isValidIdentifier(String value, AquaNotificationTarget target) {
    if (value.isEmpty || value.length > 128) return false;
    return switch (target) {
      AquaNotificationTarget.update => RegExp(
        r'^mobile-v\d+\.\d+\.\d+$',
      ).hasMatch(value),
      AquaNotificationTarget.alarm || AquaNotificationTarget.service => RegExp(
        r'^[a-zA-Z0-9_.:-]{1,128}$',
      ).hasMatch(value),
    };
  }
}

final class AquaNotificationIntentBus {
  AquaNotificationIntentBus._();

  static final AquaNotificationIntentBus instance =
      AquaNotificationIntentBus._();

  final StreamController<AquaNotificationIntent> _controller =
      StreamController<AquaNotificationIntent>.broadcast();
  final List<AquaNotificationIntent> _pending = <AquaNotificationIntent>[];

  Stream<AquaNotificationIntent> get stream => _controller.stream;

  void acceptResponse(NotificationResponse response) {
    accept(
      AquaNotificationIntent.tryParse(
        response.payload,
        actionId: response.actionId,
      ),
    );
  }

  void accept(AquaNotificationIntent? intent) {
    if (intent == null) return;
    if (_controller.hasListener) {
      _controller.add(intent);
      return;
    }
    if (_pending.length == 8) _pending.removeAt(0);
    _pending.add(intent);
  }

  List<AquaNotificationIntent> takePending() {
    final result = List<AquaNotificationIntent>.unmodifiable(_pending);
    _pending.clear();
    return result;
  }
}

abstract final class NotificationIntentInbox {
  static const String _pendingResponseKey =
      'aquacyd.notification.pending_response.v1';

  static Future<void> persistResponse(NotificationResponse response) async {
    final intent = AquaNotificationIntent.tryParse(
      response.payload,
      actionId: response.actionId,
    );
    if (intent == null) return;
    final preferences = SharedPreferencesAsync();
    await preferences.setString(
      _pendingResponseKey,
      jsonEncode(intent.toJson()),
    );
  }

  static Future<AquaNotificationIntent?> takePersisted() async {
    final preferences = SharedPreferencesAsync();
    try {
      final encoded = await preferences.getString(_pendingResponseKey);
      if (encoded == null) return null;
      await preferences.remove(_pendingResponseKey);
      return AquaNotificationIntent.tryFromJson(jsonDecode(encoded));
    } on Object {
      await preferences.remove(_pendingResponseKey);
      return null;
    }
  }
}

@pragma('vm:entry-point')
void aquariumNotificationBackgroundResponse(
  NotificationResponse response,
) async {
  await NotificationIntentInbox.persistResponse(response);
}
