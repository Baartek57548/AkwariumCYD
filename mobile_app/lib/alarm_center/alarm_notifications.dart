import 'package:flutter/foundation.dart';
import 'package:flutter_local_notifications/flutter_local_notifications.dart';

import 'alarm_models.dart';

abstract interface class AlarmNotificationSink {
  Future<void> initialize();
  Future<bool> requestPermission();
  Future<void> showTestNotification();
  Future<void> showAlarm(AlarmRecord alarm);
  Future<void> showResolved(AlarmRecord alarm);
  Future<void> showServiceReminder({
    required String id,
    required String title,
    required String body,
  });
  Future<void> cancelAlarm(String alarmKey);
}

final class LocalAlarmNotificationSink implements AlarmNotificationSink {
  LocalAlarmNotificationSink({FlutterLocalNotificationsPlugin? plugin})
    : _plugin = plugin ?? FlutterLocalNotificationsPlugin();

  static const criticalChannelId = 'aquacyd_critical_alarms_v1';
  static const warningChannelId = 'aquacyd_warning_alarms_v1';
  static const maintenanceChannelId = 'aquacyd_maintenance_v1';

  final FlutterLocalNotificationsPlugin _plugin;
  Future<void>? _initialization;

  @override
  Future<void> initialize() => _initialization ??= _initializeSafely();

  Future<void> _initializeSafely() async {
    try {
      await _initialize();
    } on Object {
      _initialization = null;
      rethrow;
    }
  }

  Future<void> _initialize() async {
    const android = AndroidInitializationSettings('ic_notification');
    const darwin = DarwinInitializationSettings(
      requestAlertPermission: false,
      requestBadgePermission: false,
      requestSoundPermission: false,
    );
    await _plugin.initialize(
      settings: const InitializationSettings(
        android: android,
        iOS: darwin,
        macOS: darwin,
      ),
    );
    final androidPlugin = _plugin
        .resolvePlatformSpecificImplementation<
          AndroidFlutterLocalNotificationsPlugin
        >();
    if (androidPlugin != null) {
      await androidPlugin.createNotificationChannel(
        const AndroidNotificationChannel(
          criticalChannelId,
          'Alarmy krytyczne',
          description: 'Pilne alarmy bezpieczeństwa akwarium.',
          importance: Importance.max,
          enableVibration: true,
          playSound: true,
        ),
      );
      await androidPlugin.createNotificationChannel(
        const AndroidNotificationChannel(
          warningChannelId,
          'Ostrzeżenia',
          description: 'Ostrzeżenia wymagające sprawdzenia akwarium.',
          importance: Importance.high,
        ),
      );
      await androidPlugin.createNotificationChannel(
        const AndroidNotificationChannel(
          maintenanceChannelId,
          'Serwis akwarium',
          description: 'Przypomnienia o konserwacji i pielęgnacji.',
          importance: Importance.defaultImportance,
        ),
      );
    }
  }

  @override
  Future<bool> requestPermission() async {
    await initialize();
    final android = _plugin
        .resolvePlatformSpecificImplementation<
          AndroidFlutterLocalNotificationsPlugin
        >();
    if (android != null) {
      final granted = await android.requestNotificationsPermission() ?? false;
      if (!granted) return false;
      return await android.areNotificationsEnabled() ?? true;
    }
    final ios = _plugin
        .resolvePlatformSpecificImplementation<
          IOSFlutterLocalNotificationsPlugin
        >();
    if (ios != null) {
      return await ios.requestPermissions(
            alert: true,
            badge: true,
            sound: true,
          ) ??
          false;
    }
    return !kIsWeb;
  }

  @override
  Future<void> showTestNotification() async {
    await initialize();
    await _ensureAndroidChannelEnabled(warningChannelId);
    await _plugin.show(
      id: _notificationId('diagnostic:system-notifications'),
      title: 'AquaCYD Control',
      body: 'Powiadomienia systemowe działają poprawnie.',
      payload: 'diagnostic:notifications',
      notificationDetails: const NotificationDetails(
        android: AndroidNotificationDetails(
          warningChannelId,
          'Ostrzeżenia',
          channelDescription: 'Ostrzeżenia wymagające sprawdzenia akwarium.',
          importance: Importance.high,
          priority: Priority.high,
          visibility: NotificationVisibility.public,
        ),
        iOS: DarwinNotificationDetails(
          presentAlert: true,
          presentBadge: true,
          presentSound: true,
        ),
      ),
    );
  }

  @override
  Future<void> showAlarm(AlarmRecord alarm) async {
    await initialize();
    final critical = alarm.severity == AlarmSeverity.critical;
    await _ensureAndroidChannelEnabled(
      critical ? criticalChannelId : warningChannelId,
    );
    await _plugin.show(
      id: _notificationId('alarm:${alarm.key}'),
      title: critical ? 'Pilny alarm: ${alarm.title}' : alarm.title,
      body: alarm.message,
      payload: 'alarm:${alarm.key}',
      notificationDetails: NotificationDetails(
        android: AndroidNotificationDetails(
          critical ? criticalChannelId : warningChannelId,
          critical ? 'Alarmy krytyczne' : 'Ostrzeżenia',
          channelDescription: critical
              ? 'Pilne alarmy bezpieczeństwa akwarium.'
              : 'Ostrzeżenia wymagające sprawdzenia akwarium.',
          importance: critical ? Importance.max : Importance.high,
          priority: critical ? Priority.max : Priority.high,
          category: AndroidNotificationCategory.alarm,
          visibility: NotificationVisibility.public,
        ),
        iOS: const DarwinNotificationDetails(
          presentAlert: true,
          presentBadge: true,
          presentSound: true,
        ),
      ),
    );
  }

  @override
  Future<void> showResolved(AlarmRecord alarm) async {
    await initialize();
    await _ensureAndroidChannelEnabled(warningChannelId);
    await _plugin.show(
      id: _notificationId('resolved:${alarm.key}'),
      title: 'Alarm rozwiązany',
      body: alarm.title,
      payload: 'alarm:${alarm.key}',
      notificationDetails: const NotificationDetails(
        android: AndroidNotificationDetails(
          warningChannelId,
          'Ostrzeżenia',
          channelDescription: 'Ostrzeżenia wymagające sprawdzenia akwarium.',
          importance: Importance.defaultImportance,
          priority: Priority.defaultPriority,
        ),
        iOS: DarwinNotificationDetails(),
      ),
    );
  }

  @override
  Future<void> showServiceReminder({
    required String id,
    required String title,
    required String body,
  }) async {
    await initialize();
    await _ensureAndroidChannelEnabled(maintenanceChannelId);
    await _plugin.show(
      id: _notificationId('service:$id'),
      title: title,
      body: body,
      payload: 'service:$id',
      notificationDetails: const NotificationDetails(
        android: AndroidNotificationDetails(
          maintenanceChannelId,
          'Serwis akwarium',
          channelDescription: 'Przypomnienia o konserwacji i pielęgnacji.',
          importance: Importance.defaultImportance,
          priority: Priority.defaultPriority,
        ),
        iOS: DarwinNotificationDetails(),
      ),
    );
  }

  @override
  Future<void> cancelAlarm(String alarmKey) async {
    await initialize();
    await _plugin.cancel(id: _notificationId('alarm:$alarmKey'));
  }

  Future<void> _ensureAndroidChannelEnabled(String channelId) async {
    final android = _plugin
        .resolvePlatformSpecificImplementation<
          AndroidFlutterLocalNotificationsPlugin
        >();
    if (android == null) return;
    if (await android.areNotificationsEnabled() == false) {
      throw StateError('Powiadomienia aplikacji są wyłączone w systemie.');
    }
    final channels = await android.getNotificationChannels();
    if (channels == null || channels.isEmpty) return;
    final channel = channels.where((item) => item.id == channelId).firstOrNull;
    if (channel == null || channel.importance == Importance.none) {
      throw StateError('Kanał powiadomień $channelId jest wyłączony.');
    }
  }

  static int _notificationId(String value) {
    var hash = 0x811c9dc5;
    for (final unit in value.codeUnits) {
      hash ^= unit;
      hash = (hash * 0x01000193) & 0x7fffffff;
    }
    return hash;
  }
}
