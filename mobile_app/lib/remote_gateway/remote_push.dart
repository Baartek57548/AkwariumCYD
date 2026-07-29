import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:firebase_core/firebase_core.dart';
import 'package:firebase_messaging/firebase_messaging.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter/widgets.dart';
import 'package:crypto/crypto.dart';

import '../alarm_center/alarm_center.dart';
import '../alarm_center/alarm_models.dart';
import '../alarm_center/alarm_notifications.dart';
import '../alarm_center/alarm_preferences.dart';
import '../notifications/notification_intents.dart';
import 'remote_alarm_gateway.dart';

final class RemotePushRuntimeConfiguration {
  const RemotePushRuntimeConfiguration({
    required this.apiKey,
    required this.appId,
    required this.messagingSenderId,
    required this.projectId,
  });

  factory RemotePushRuntimeConfiguration.fromEnvironment() {
    return const RemotePushRuntimeConfiguration(
      apiKey: String.fromEnvironment('AQUACYD_FIREBASE_API_KEY'),
      appId: String.fromEnvironment('AQUACYD_FIREBASE_APP_ID'),
      messagingSenderId: String.fromEnvironment(
        'AQUACYD_FIREBASE_MESSAGING_SENDER_ID',
      ),
      projectId: String.fromEnvironment('AQUACYD_FIREBASE_PROJECT_ID'),
    );
  }

  final String apiKey;
  final String appId;
  final String messagingSenderId;
  final String projectId;

  bool get isComplete =>
      apiKey.trim().isNotEmpty &&
      appId.trim().isNotEmpty &&
      messagingSenderId.trim().isNotEmpty &&
      projectId.trim().isNotEmpty;

  bool get supportsCurrentPlatform =>
      isComplete &&
      !kIsWeb &&
      (Platform.isAndroid || Platform.isIOS || Platform.isMacOS);

  FirebaseOptions get firebaseOptions {
    if (!isComplete) {
      throw StateError('Brakuje konfiguracji runtime Firebase.');
    }
    return FirebaseOptions(
      apiKey: apiKey.trim(),
      appId: appId.trim(),
      messagingSenderId: messagingSenderId.trim(),
      projectId: projectId.trim(),
    );
  }
}

abstract interface class RemotePushAdapter {
  bool get isSupported;
  bool get isInitialized;
  Stream<String> get tokenChanges;

  Future<void> initialize({
    required Future<void> Function(Map<String, dynamic> data) onMessage,
    required Future<void> Function(Map<String, dynamic> data) onMessageOpened,
  });
  Future<String?> getToken();
  Future<void> deleteToken();
  Future<void> dispose();
}

final class UnavailableRemotePushAdapter implements RemotePushAdapter {
  const UnavailableRemotePushAdapter();

  @override
  bool get isSupported => false;

  @override
  bool get isInitialized => false;

  @override
  Stream<String> get tokenChanges => const Stream<String>.empty();

  @override
  Future<void> initialize({
    required Future<void> Function(Map<String, dynamic> data) onMessage,
    required Future<void> Function(Map<String, dynamic> data) onMessageOpened,
  }) async {}

  @override
  Future<String?> getToken() async => null;

  @override
  Future<void> deleteToken() async {}

  @override
  Future<void> dispose() async {}
}

final class FirebaseRemotePushAdapter implements RemotePushAdapter {
  FirebaseRemotePushAdapter({RemotePushRuntimeConfiguration? configuration})
    : configuration =
          configuration ?? RemotePushRuntimeConfiguration.fromEnvironment();

  final RemotePushRuntimeConfiguration configuration;
  final StreamController<String> _tokenController =
      StreamController<String>.broadcast();

  Future<void>? _initialization;
  StreamSubscription<RemoteMessage>? _messageSubscription;
  StreamSubscription<RemoteMessage>? _openedSubscription;
  StreamSubscription<String>? _tokenSubscription;
  Future<void> Function(Map<String, dynamic> data)? _onMessage;
  Future<void> Function(Map<String, dynamic> data)? _onMessageOpened;
  bool _disposed = false;

  @override
  bool get isSupported => configuration.supportsCurrentPlatform;

  @override
  bool get isInitialized => _initialization != null;

  @override
  Stream<String> get tokenChanges => _tokenController.stream;

  @override
  Future<void> initialize({
    required Future<void> Function(Map<String, dynamic> data) onMessage,
    required Future<void> Function(Map<String, dynamic> data) onMessageOpened,
  }) {
    if (_disposed) {
      throw StateError('Adapter FCM został już zamknięty.');
    }
    _onMessage = onMessage;
    _onMessageOpened = onMessageOpened;
    if (!isSupported) return Future<void>.value();
    return _initialization ??= _initializeSafely();
  }

  Future<void> _initializeSafely() async {
    try {
      await RemotePushBootstrap.ensureFirebaseInitialized(configuration);
      final messaging = FirebaseMessaging.instance;
      await messaging.setAutoInitEnabled(true);
      final permission = await messaging.requestPermission(
        alert: true,
        announcement: false,
        badge: true,
        carPlay: false,
        criticalAlert: false,
        provisional: false,
        sound: true,
      );
      if (permission.authorizationStatus == AuthorizationStatus.denied) {
        throw StateError(
          'Systemowa zgoda na zdalne powiadomienia została odrzucona.',
        );
      }
      await messaging.setForegroundNotificationPresentationOptions(
        alert: false,
        badge: false,
        sound: false,
      );

      _messageSubscription = FirebaseMessaging.onMessage.listen((message) {
        unawaited(_dispatchMessage(message, opened: false));
      });
      _openedSubscription = FirebaseMessaging.onMessageOpenedApp.listen((
        message,
      ) {
        unawaited(_dispatchMessage(message, opened: true));
      });
      _tokenSubscription = messaging.onTokenRefresh.listen((token) {
        if (!_tokenController.isClosed && token.isNotEmpty) {
          _tokenController.add(token);
        }
      });
      final initialMessage = await messaging.getInitialMessage();
      if (initialMessage != null) {
        await _dispatchMessage(initialMessage, opened: true);
      }
    } on Object {
      _initialization = null;
      await _cancelSubscriptions();
      rethrow;
    }
  }

  Future<void> _dispatchMessage(
    RemoteMessage message, {
    required bool opened,
  }) async {
    if (message.data.isEmpty) return;
    final callback = opened ? _onMessageOpened : _onMessage;
    if (callback == null) return;
    try {
      await callback(Map<String, dynamic>.from(message.data));
    } on Object {
      // Nieprawidłowy zdalny payload nie może przerwać strumienia FCM.
    }
  }

  @override
  Future<String?> getToken() async {
    if (!isSupported || _initialization == null) return null;
    await _initialization;
    return FirebaseMessaging.instance.getToken();
  }

  @override
  Future<void> deleteToken() async {
    if (!isSupported || _initialization == null) return;
    await _initialization;
    await FirebaseMessaging.instance.deleteToken();
  }

  @override
  Future<void> dispose() async {
    if (_disposed) return;
    _disposed = true;
    await _cancelSubscriptions();
    await _tokenController.close();
  }

  Future<void> _cancelSubscriptions() async {
    await _messageSubscription?.cancel();
    await _openedSubscription?.cancel();
    await _tokenSubscription?.cancel();
    _messageSubscription = null;
    _openedSubscription = null;
    _tokenSubscription = null;
  }
}

abstract final class RemotePushBootstrap {
  static void configureBackgroundHandling() {
    final configuration = RemotePushRuntimeConfiguration.fromEnvironment();
    if (!configuration.supportsCurrentPlatform) return;
    FirebaseMessaging.onBackgroundMessage(
      aquariumFirebaseMessagingBackgroundHandler,
    );
  }

  static Future<FirebaseApp> ensureFirebaseInitialized(
    RemotePushRuntimeConfiguration configuration,
  ) async {
    if (!configuration.isComplete) {
      throw StateError('Brakuje konfiguracji runtime Firebase.');
    }
    if (Firebase.apps.isNotEmpty) return Firebase.apps.first;
    return Firebase.initializeApp(options: configuration.firebaseOptions);
  }
}

@pragma('vm:entry-point')
Future<void> aquariumFirebaseMessagingBackgroundHandler(
  RemoteMessage message,
) async {
  final configuration = RemotePushRuntimeConfiguration.fromEnvironment();
  if (!configuration.supportsCurrentPlatform || message.data.isEmpty) return;
  try {
    await RemotePushBootstrap.ensureFirebaseInitialized(configuration);
    await RemotePushPayloadHandler(
      LocalAlarmNotificationSink(),
      alarmCenterFactory: AlarmCenter.standard,
    ).handle(Map<String, dynamic>.from(message.data));
  } on Object {
    // Android może zakończyć isolate; błędny payload jest bezpiecznie pomijany.
  }
}

typedef AlarmPreferencesLoader =
    Future<AlarmNotificationPreferences> Function();

final class RemotePushPayloadHandler {
  RemotePushPayloadHandler(
    this.notifications, {
    AlarmPreferencesLoader? preferencesLoader,
    this.alarmCenter,
    AlarmCenter Function()? alarmCenterFactory,
  }) : _preferencesLoader =
           preferencesLoader ?? (() => AlarmPreferencesStore().load()),
       _alarmCenterFactory = alarmCenterFactory;

  final AlarmNotificationSink notifications;
  final AlarmCenter? alarmCenter;
  final AlarmPreferencesLoader _preferencesLoader;
  final AlarmCenter Function()? _alarmCenterFactory;
  AlarmCenter? _lazyAlarmCenter;

  Future<void> handle(Map<String, dynamic> data) async {
    final type = _requiredText(
      data,
      'type',
      maximumLength: 24,
      pattern: RegExp(r'^[a-z]+$'),
    );
    switch (type) {
      case 'alarm':
        await _handleAlarm(data);
      case 'service':
        await _handleService(data);
      case 'update':
        await _handleUpdate(data);
      default:
        throw const FormatException('Nieznany typ zdalnego powiadomienia.');
    }
  }

  AquaNotificationIntent? intentFor(Map<String, dynamic> data) {
    try {
      final type = _requiredText(
        data,
        'type',
        maximumLength: 24,
        pattern: RegExp(r'^[a-z]+$'),
      );
      return switch (type) {
        'alarm' => AquaNotificationIntent(
          target: AquaNotificationTarget.alarm,
          identifier: _alarmKey(data),
        ),
        'service' => AquaNotificationIntent(
          target: AquaNotificationTarget.service,
          identifier: _eventId(data),
        ),
        'update' => AquaNotificationIntent(
          target: AquaNotificationTarget.update,
          identifier: _updateTag(data),
        ),
        _ => null,
      };
    } on FormatException {
      return null;
    }
  }

  Future<void> _handleAlarm(Map<String, dynamic> data) async {
    final eventId = _eventId(data);
    final title = _title(data);
    final body = _body(data);
    final key = _alarmKey(data);
    final rawState = _requiredText(
      data,
      'state',
      maximumLength: 16,
      pattern: RegExp(r'^(raised|resolved)$'),
    );
    final rawSeverity = _requiredText(
      data,
      'severity',
      maximumLength: 16,
      pattern: RegExp(r'^(info|warning|critical)$'),
    );
    final severity = rawSeverity == 'critical'
        ? AlarmSeverity.critical
        : AlarmSeverity.warning;
    final occurredAt = _occurredAt(data);
    final settings = await _preferencesLoader();
    if (rawState == 'resolved') {
      var resolved = AlarmRecord(
        key: key,
        severity: severity,
        lifecycle: AlarmLifecycle.resolved,
        title: title,
        message: body,
        firstTriggeredAt: occurredAt,
        lastTriggeredAt: occurredAt,
        resolvedAt: occurredAt,
        occurrences: 1,
      );
      resolved = await _persistAlarm(resolved, eventId);
      if (resolved.lifecycle != AlarmLifecycle.resolved) return;
      await notifications.cancelAlarm(key);
      if (settings.enabled && settings.resolvedEnabled) {
        await notifications.showResolved(resolved);
      }
      return;
    }
    var active = AlarmRecord(
      key: key,
      severity: severity,
      lifecycle: AlarmLifecycle.newAlarm,
      title: title,
      message: body,
      firstTriggeredAt: occurredAt,
      lastTriggeredAt: occurredAt,
      occurrences: 1,
    );
    active = await _persistAlarm(active, eventId);
    if (!active.isActive) return;
    final severityEnabled = severity == AlarmSeverity.critical
        ? settings.criticalEnabled
        : settings.warningEnabled;
    if (!settings.enabled || !severityEnabled) return;
    await notifications.showAlarm(active);
  }

  Future<AlarmRecord> _persistAlarm(AlarmRecord record, String eventId) async {
    final center =
        alarmCenter ?? (_lazyAlarmCenter ??= _alarmCenterFactory?.call());
    if (center == null) return record;
    try {
      return await center.ingestExternalRecord(record, eventId: eventId);
    } on Object {
      // Awaria lokalnej historii nie może zablokować alarmu systemowego.
      return record;
    }
  }

  Future<void> _handleService(Map<String, dynamic> data) {
    return notifications.showServiceReminder(
      id: _eventId(data),
      title: _title(data),
      body: _body(data),
    );
  }

  Future<void> _handleUpdate(Map<String, dynamic> data) {
    final version = _requiredText(
      data,
      'version',
      maximumLength: 32,
      pattern: RegExp(r'^\d+\.\d+\.\d+$'),
    );
    final tagName = _updateTag(data);
    _eventId(data);
    _title(data);
    _body(data);
    return notifications.showAppUpdate(
      tagName: tagName,
      version: version,
      downloaded: false,
    );
  }

  static String _eventId(Map<String, dynamic> data) {
    return _requiredText(
      data,
      'id',
      maximumLength: 128,
      pattern: RegExp(r'^[A-Za-z0-9_.:-]+$'),
    );
  }

  static String _title(Map<String, dynamic> data) {
    return _requiredText(data, 'title', maximumLength: 160);
  }

  static String _body(Map<String, dynamic> data) {
    return _requiredText(data, 'body', maximumLength: 800);
  }

  static String _alarmKey(Map<String, dynamic> data) {
    final eventType = _requiredText(
      data,
      'eventType',
      maximumLength: 48,
      pattern: RegExp(r'^[A-Za-z0-9_.:-]+$'),
    );
    final deviceId = _requiredText(
      data,
      'deviceId',
      maximumLength: 64,
      pattern: RegExp(r'^[A-Za-z0-9_-]{4,64}$'),
    );
    final readable = 'remote:$deviceId:$eventType'.toLowerCase();
    if (readable.length <= 64) return readable;
    final digest = sha256
        .convert(utf8.encode('$deviceId\u0000$eventType'))
        .toString();
    return 'remote:${digest.substring(0, 48)}';
  }

  static String _updateTag(Map<String, dynamic> data) {
    final version = _requiredText(
      data,
      'version',
      maximumLength: 32,
      pattern: RegExp(r'^\d+\.\d+\.\d+$'),
    );
    final tag = _requiredText(
      data,
      'tag',
      maximumLength: 48,
      pattern: RegExp(r'^mobile-v\d+\.\d+\.\d+$'),
    );
    if (tag != 'mobile-v$version') {
      throw const FormatException('Nieprawidłowa wersja aktualizacji.');
    }
    return tag;
  }

  static DateTime _occurredAt(Map<String, dynamic> data) {
    final raw = data['occurredAt'];
    if (raw == null) return DateTime.now().toUtc();
    if (raw is! String || raw.length > 40) {
      throw const FormatException('Nieprawidłowy czas zdalnego alarmu.');
    }
    final parsed = DateTime.tryParse(raw);
    if (parsed == null ||
        parsed.toUtc().year < 2020 ||
        parsed.toUtc().year > 2200) {
      throw const FormatException('Nieprawidłowy czas zdalnego alarmu.');
    }
    return parsed.toUtc();
  }

  static String _requiredText(
    Map<String, dynamic> data,
    String key, {
    required int maximumLength,
    RegExp? pattern,
  }) {
    final raw = data[key];
    if (raw is! String) {
      throw FormatException('Brak wymaganego pola $key.');
    }
    final value = raw.trim();
    if (value.isEmpty ||
        value.length > maximumLength ||
        (pattern != null && !pattern.hasMatch(value))) {
      throw FormatException('Nieprawidłowe pole $key.');
    }
    return value;
  }
}

final class RemotePushRegistrar {
  RemotePushRegistrar({
    required this.gatewayStore,
    required this.adapter,
    AlarmNotificationSink? notifications,
    AlarmPreferencesLoader? preferencesLoader,
    HttpClient Function()? clientFactory,
    this.timeout = const Duration(seconds: 8),
  }) : _handler = _createHandler(
         notifications: notifications,
         preferencesLoader: preferencesLoader,
       ),
       _clientFactory = clientFactory ?? HttpClient.new;

  static const int maximumResponseBytes = 16 * 1024;

  final RemoteAlarmGatewayStore gatewayStore;
  final RemotePushAdapter adapter;
  final RemotePushPayloadHandler _handler;
  final HttpClient Function() _clientFactory;
  final Duration timeout;
  StreamSubscription<String>? _tokenSubscription;

  static RemotePushPayloadHandler _createHandler({
    AlarmNotificationSink? notifications,
    AlarmPreferencesLoader? preferencesLoader,
  }) {
    final sink = notifications ?? LocalAlarmNotificationSink();
    return RemotePushPayloadHandler(
      sink,
      preferencesLoader: preferencesLoader,
      alarmCenterFactory: () => AlarmCenter.standard(notifications: sink),
    );
  }

  Future<bool> initialize() async {
    if (!adapter.isSupported) return false;
    final credentials = await gatewayStore.loadCredentials();
    if (credentials == null) return false;
    await adapter.initialize(
      onMessage: _handler.handle,
      onMessageOpened: _handleOpenedMessage,
    );
    final token = await adapter.getToken();
    if (token != null && token.isNotEmpty) {
      await _sendToken(credentials, token, register: true);
    }
    await _tokenSubscription?.cancel();
    _tokenSubscription = adapter.tokenChanges.listen((nextToken) {
      if (nextToken.isNotEmpty) {
        unawaited(_sendLatestTokenSafely(nextToken));
      }
    });
    return true;
  }

  Future<void> _handleOpenedMessage(Map<String, dynamic> data) async {
    AquaNotificationIntentBus.instance.accept(_handler.intentFor(data));
  }

  Future<void> unregister() async {
    final credentials = await gatewayStore.loadCredentials(
      includeDisabled: true,
    );
    final token = await adapter.getToken();
    if (credentials != null && token != null && token.isNotEmpty) {
      await _sendToken(credentials, token, register: false);
    }
    await adapter.deleteToken();
    await _tokenSubscription?.cancel();
    _tokenSubscription = null;
  }

  Future<void> dispose() async {
    await _tokenSubscription?.cancel();
    _tokenSubscription = null;
    await adapter.dispose();
  }

  Future<void> _sendLatestTokenSafely(String token) async {
    try {
      final credentials = await gatewayStore.loadCredentials();
      if (credentials == null) return;
      await _sendToken(credentials, token, register: true);
    } on Object {
      // Następne uruchomienie lub odświeżenie tokenu ponowi rejestrację.
    }
  }

  Future<void> _sendToken(
    RemoteAlarmGatewayCredentials credentials,
    String pushToken, {
    required bool register,
  }) async {
    if (pushToken.length < 16 || pushToken.length > 4096) {
      throw const FormatException('Token push ma nieprawidłowy rozmiar.');
    }
    final base = credentials.configuration.baseUrl;
    final root = base.path.endsWith('/')
        ? base.path.substring(0, base.path.length - 1)
        : base.path;
    final endpoint = base.replace(
      path:
          '$root/api/v1/devices/'
          '${Uri.encodeComponent(credentials.configuration.deviceId)}'
          '/push-tokens',
    );
    final client = _clientFactory()
      ..connectionTimeout = timeout
      ..idleTimeout = timeout;
    try {
      final request = register
          ? await client.postUrl(endpoint).timeout(timeout)
          : await client.deleteUrl(endpoint).timeout(timeout);
      request
        ..followRedirects = false
        ..headers.set(
          HttpHeaders.authorizationHeader,
          'Bearer ${credentials.viewerToken}',
        )
        ..headers.contentType = ContentType.json
        ..add(
          utf8.encode(
            jsonEncode(<String, Object?>{
              'token': pushToken,
              'platform': Platform.operatingSystem,
            }),
          ),
        );
      final response = await request.close().timeout(timeout);
      var received = 0;
      await for (final chunk in response.timeout(timeout)) {
        received += chunk.length;
        if (received > maximumResponseBytes) {
          throw const HttpException('Gateway response too large.');
        }
      }
      if (response.isRedirect ||
          response.statusCode < 200 ||
          response.statusCode >= 300) {
        throw HttpException(
          'Gateway rejected push token: HTTP ${response.statusCode}.',
        );
      }
    } finally {
      client.close(force: true);
    }
  }
}

final class RemotePushCoordinator {
  RemotePushCoordinator({required this.gatewayStore, required this.registrar});

  factory RemotePushCoordinator.standard() {
    final store = RemoteAlarmGatewayStore();
    return RemotePushCoordinator(
      gatewayStore: store,
      registrar: RemotePushRegistrar(
        gatewayStore: store,
        adapter: FirebaseRemotePushAdapter(),
      ),
    );
  }

  final RemoteAlarmGatewayStore gatewayStore;
  final RemotePushRegistrar registrar;
  Future<bool>? _operation;
  bool _disposed = false;

  Future<bool> initialize() => reconcile();

  Future<bool> reconcile() {
    if (_disposed) return Future<bool>.value(false);
    final running = _operation;
    if (running != null) return running;
    final operation = _reconcile();
    _operation = operation;
    unawaited(
      operation.then<void>(
        (_) {
          if (identical(_operation, operation)) _operation = null;
        },
        onError: (Object _, StackTrace _) {
          if (identical(_operation, operation)) _operation = null;
        },
      ),
    );
    return operation;
  }

  Future<bool> _reconcile() async {
    if (!registrar.adapter.isSupported) return false;
    final credentials = await gatewayStore.loadCredentials();
    if (credentials == null) {
      await registrar.unregister();
      return false;
    }
    return registrar.initialize();
  }

  Future<void> unregister() async {
    final running = _operation;
    if (running != null) await running;
    if (_disposed || !registrar.adapter.isSupported) return;
    await registrar.unregister();
  }

  Future<void> dispose() async {
    if (_disposed) return;
    _disposed = true;
    final running = _operation;
    if (running != null) {
      try {
        await running;
      } on Object {
        // Zamknięcie zasobów jest wymagane także po błędzie rejestracji.
      }
    }
    await registrar.dispose();
  }
}

class RemotePushScope extends InheritedWidget {
  const RemotePushScope({
    super.key,
    required this.coordinator,
    required super.child,
  });

  final RemotePushCoordinator coordinator;

  static RemotePushCoordinator? maybeOf(
    BuildContext context, {
    bool listen = false,
  }) {
    if (listen) {
      return context
          .dependOnInheritedWidgetOfExactType<RemotePushScope>()
          ?.coordinator;
    }
    return context
        .getInheritedWidgetOfExactType<RemotePushScope>()
        ?.coordinator;
  }

  @override
  bool updateShouldNotify(RemotePushScope oldWidget) {
    return !identical(coordinator, oldWidget.coordinator);
  }
}
