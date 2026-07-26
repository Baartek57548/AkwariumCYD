import 'dart:async';
import 'dart:typed_data';

import 'package:cyd_aquarium_mobile/aquarium_app.dart';
import 'package:cyd_aquarium_mobile/controller_bootstrap_page.dart';
import 'package:cyd_aquarium_mobile/controller_preferences.dart';
import 'package:cyd_aquarium_mobile/controller_snapshot_cache.dart';
import 'package:cyd_aquarium_mobile/full_controller/controller_api.dart';
import 'package:cyd_aquarium_mobile/full_controller/controller_session.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';
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
    'pierwsza klatka pokazuje pełne pięć sekcji przed końcem inicjalizacji',
    (tester) async {
      _configurePhoneViewport(tester);
      final initializationGate = Completer<void>();
      SharedPreferencesAsyncPlatform.instance = _GatedInMemoryPreferences(
        initializationGate.future,
      );
      final preferences = SharedPreferencesAsync();

      await tester.pumpWidget(
        AquariumApp(
          home: ControllerBootstrapPage(
            preferences: ControllerPreferences(preferences: preferences),
            snapshotCache: ControllerSnapshotCache(preferences: preferences),
          ),
        ),
      );

      expect(initializationGate.isCompleted, isFalse);
      expect(find.byType(NavigationBar), findsOneWidget);
      for (final label in const [
        'Centrum',
        'Steruj',
        'Auto',
        'Historia',
        'System',
      ]) {
        expect(find.text(label), findsOneWidget);
      }
      expect(find.text('Temperatura wody'), findsOneWidget);
      expect(find.text('Aplikacja działa bez urządzenia'), findsOneWidget);
      expect(tester.takeException(), isNull);

      initializationGate.complete();
      await tester.pump();
      await tester.pump();
      await tester.pumpWidget(const SizedBox.shrink());
    },
  );

  testWidgets(
    'pokazuje snapshot i uruchamia zapisane Wi-Fi w tle przez builder sesji',
    (tester) async {
      _configurePhoneViewport(tester);
      final preferences = SharedPreferencesAsync();
      final controllerPreferences = ControllerPreferences(
        preferences: preferences,
      );
      final snapshotCache = ControllerSnapshotCache(preferences: preferences);
      final savedAddress = Uri.parse('http://192.168.50.27');
      final savedAt = DateTime.utc(2026, 7, 26, 12, 30);
      const savedDeviceName = 'AquaCYD zapisany salon';

      await controllerPreferences.saveAddress(savedAddress);
      await controllerPreferences.saveAutoReconnect(true);
      expect(
        await snapshotCache.save(
          _cachedStatus(deviceName: savedDeviceName),
          savedAt: savedAt,
        ),
        isTrue,
      );

      final connectGate = Completer<void>();
      final api = _FakeControllerRemoteApi(
        baseUri: savedAddress,
        connectGate: connectGate,
      );
      Uri? builderAddress;
      Map<String, dynamic>? builderStatus;
      DateTime? builderCachedAt;
      var builderCalls = 0;

      await tester.pumpWidget(
        AquariumApp(
          home: ControllerBootstrapPage(
            preferences: controllerPreferences,
            snapshotCache: snapshotCache,
            wifiSessionBuilder: (address, initialStatus, cachedAt) {
              builderCalls += 1;
              builderAddress = address;
              builderStatus = initialStatus;
              builderCachedAt = cachedAt;
              return ControllerSession.wifi(
                api,
                initialStatus: initialStatus,
                cachedAt: cachedAt,
              );
            },
          ),
        ),
      );

      for (var attempt = 0; attempt < 8 && builderCalls == 0; attempt++) {
        await tester.pump();
      }
      await tester.pump();

      expect(builderCalls, 1);
      expect(builderAddress, savedAddress);
      expect(builderStatus?['device'], savedDeviceName);
      expect(builderCachedAt, savedAt);
      expect(api.connectCalls, 1);
      expect(connectGate.isCompleted, isFalse);
      expect(find.text(savedDeviceName), findsOneWidget);
      expect(find.text('Ostatni zapisany stan'), findsOneWidget);
      expect(find.text('Dane dostępne podczas łączenia'), findsOneWidget);
      expect(tester.takeException(), isNull);

      await tester.pumpWidget(const SizedBox.shrink());
      connectGate.complete();
      await tester.pump();
    },
  );

  testWidgets(
    'przycisk w prawym górnym rogu otwiera wybór Wi-Fi Bluetooth i offline',
    (tester) async {
      _configurePhoneViewport(tester);
      final preferences = SharedPreferencesAsync();

      await tester.pumpWidget(
        AquariumApp(
          home: ControllerBootstrapPage(
            preferences: ControllerPreferences(preferences: preferences),
            snapshotCache: ControllerSnapshotCache(preferences: preferences),
          ),
        ),
      );
      for (var frame = 0; frame < 4; frame++) {
        await tester.pump();
      }

      final connectionButton = find.byKey(
        const Key('connection-center-button'),
      );
      expect(connectionButton, findsOneWidget);

      await tester.tap(connectionButton);
      for (var frame = 0; frame < 5; frame++) {
        await tester.pump(const Duration(milliseconds: 100));
      }

      expect(find.text('Połączenia'), findsOneWidget);
      expect(find.text('Wi-Fi'), findsOneWidget);
      expect(find.text('Bluetooth'), findsOneWidget);
      expect(find.text('Pozostań offline'), findsOneWidget);
      expect(find.text('Łącz automatycznie przez Wi-Fi'), findsOneWidget);
      expect(tester.takeException(), isNull);
    },
  );

  testWidgets(
    'włączenie automatycznego Wi-Fi od razu uruchamia połączenie w tle',
    (tester) async {
      _configurePhoneViewport(tester);
      final preferences = SharedPreferencesAsync();
      final controllerPreferences = ControllerPreferences(
        preferences: preferences,
      );
      final snapshotCache = ControllerSnapshotCache(preferences: preferences);
      final savedAddress = Uri.parse('http://192.168.50.31');
      await controllerPreferences.saveAddress(savedAddress);
      await controllerPreferences.saveAutoReconnect(false);
      await snapshotCache.save(
        _cachedStatus(deviceName: 'AquaCYD offline'),
        savedAt: DateTime.utc(2026, 7, 26, 13),
      );

      final connectGate = Completer<void>();
      final api = _FakeControllerRemoteApi(
        baseUri: savedAddress,
        connectGate: connectGate,
      );
      var builderCalls = 0;
      await tester.pumpWidget(
        AquariumApp(
          home: ControllerBootstrapPage(
            preferences: controllerPreferences,
            snapshotCache: snapshotCache,
            wifiSessionBuilder: (address, initialStatus, cachedAt) {
              builderCalls += 1;
              return ControllerSession.wifi(
                api,
                initialStatus: initialStatus,
                cachedAt: cachedAt,
              );
            },
          ),
        ),
      );
      await tester.pumpAndSettle();
      expect(builderCalls, 0);

      await tester.tap(find.byKey(const Key('connection-center-button')));
      await tester.pumpAndSettle();
      await tester.tap(find.text('Łącz automatycznie przez Wi-Fi'));
      for (var attempt = 0; attempt < 8 && builderCalls == 0; attempt++) {
        await tester.pump();
      }

      expect(builderCalls, 1);
      expect(api.connectCalls, 1);
      expect(await controllerPreferences.loadAutoReconnect(), isTrue);

      await tester.pumpWidget(const SizedBox.shrink());
      connectGate.complete();
      await tester.pump();
    },
  );

  testWidgets(
    'host zwalnia automatyczną sesję dokładnie raz i anuluje jej timery',
    (tester) async {
      _configurePhoneViewport(tester);
      final preferences = SharedPreferencesAsync();
      final controllerPreferences = ControllerPreferences(
        preferences: preferences,
      );
      final savedAddress = Uri.parse('http://192.168.50.41');
      await controllerPreferences.saveAddress(savedAddress);
      await controllerPreferences.saveAutoReconnect(true);

      final connectGate = Completer<void>()..complete();
      final api = _FakeControllerRemoteApi(
        baseUri: savedAddress,
        connectGate: connectGate,
      );

      await tester.pumpWidget(
        AquariumApp(
          home: ControllerBootstrapPage(
            preferences: controllerPreferences,
            snapshotCache: ControllerSnapshotCache(preferences: preferences),
            wifiSessionBuilder: (address, initialStatus, cachedAt) {
              return ControllerSession.wifi(
                api,
                initialStatus: initialStatus,
                cachedAt: cachedAt,
              );
            },
          ),
        ),
      );

      for (var attempt = 0; attempt < 8 && api.statusCalls == 0; attempt++) {
        await tester.pump();
      }
      expect(api.connectCalls, 1);
      expect(api.statusCalls, 1);
      expect(api.disconnectCalls, 0);

      await tester.pumpWidget(const SizedBox.shrink());
      await tester.pump(const Duration(seconds: 5));

      expect(api.disconnectCalls, 1);
      expect(
        api.statusCalls,
        1,
        reason:
            'Anulowany timer odpytywania nie może wywołać kolejnego statusu.',
      );
      expect(tester.takeException(), isNull);
    },
  );
}

void _configurePhoneViewport(WidgetTester tester) {
  tester.view.devicePixelRatio = 1;
  tester.view.physicalSize = const Size(412, 915);
  addTearDown(tester.view.resetDevicePixelRatio);
  addTearDown(tester.view.resetPhysicalSize);
}

Map<String, dynamic> _cachedStatus({required String deviceName}) {
  return <String, dynamic>{
    'device': deviceName,
    'mode': 'STA',
    'sensors': <String, dynamic>{
      'temp_c': 25.37,
      'temp_valid': true,
      'ph': 6.95,
      'ph_valid': true,
      'ec': 438.0,
      'ec_valid': true,
      'ldr': 1150,
      'ldr_valid': true,
      'water_level_high': true,
      'water_level_valid': true,
      'leak_detected': false,
      'leak_valid': true,
      'flow_active': true,
      'flow_valid': true,
      'supply_voltage': 5.04,
      'supply_valid': true,
      'mcp_present': true,
      'mcp_valid': true,
      'mcp_ok': true,
    },
    'alarms': <String, dynamic>{'flags': 0, 'activeCount': 0},
    'config': <String, dynamic>{'target_temp': 25.0, 'temp_hysteresis': 0.5},
    'modules': <String, dynamic>{
      'light1_on': true,
      'light2_on': false,
      'filter_on': true,
      'heater_on': false,
      'co2_on': false,
      'air_on': true,
      'water_dosing_on': false,
      'heater_enabled': true,
      'ph_sensor_enabled': true,
      'co2_enabled': true,
      'ec_enabled': true,
      'water_level_enabled': true,
      'leak_enabled': true,
      'flow_enabled': true,
      'feeder_enabled': true,
    },
    'network': <String, dynamic>{
      'ip': '192.168.50.27',
      'rssi': -57,
      'staConnected': true,
    },
    'system': <String, dynamic>{'uptime': 7200, 'freeHeap': 176000},
    'temperature': <String, dynamic>{
      'current': 25.37,
      'target': 25.0,
      'history': <dynamic>[],
    },
    'relays': <String, dynamic>{
      'light1': true,
      'light2': false,
      'pump': true,
      'heater': false,
      'co2': false,
      'aeration': true,
      'waterDosing': false,
    },
    'schedules': <String, dynamic>{},
    'schedule': <String, dynamic>{},
    'feeding': <String, dynamic>{},
  };
}

base class _GatedInMemoryPreferences extends InMemorySharedPreferencesAsync {
  _GatedInMemoryPreferences(this.readGate)
    : super.withData(const <String, Object>{});

  final Future<void> readGate;

  @override
  Future<String?> getString(
    String key,
    SharedPreferencesOptions options,
  ) async {
    await readGate;
    return super.getString(key, options);
  }
}

final class _FakeControllerRemoteApi implements ControllerRemoteApi {
  _FakeControllerRemoteApi({required this.baseUri, required this.connectGate});

  @override
  final Uri baseUri;

  final Completer<void> connectGate;
  int connectCalls = 0;
  int disconnectCalls = 0;
  int statusCalls = 0;

  @override
  bool get supportsFileDownload => true;

  @override
  bool get supportsFirmwareUpload => true;

  @override
  bool get supportsWebSession => false;

  @override
  Future<void> connect() async {
    connectCalls += 1;
    await connectGate.future;
  }

  @override
  Future<void> disconnect() async {
    disconnectCalls += 1;
  }

  @override
  Future<Map<String, dynamic>> status({bool includeHistory = false}) async {
    statusCalls += 1;
    return _cachedStatus(deviceName: 'AquaCYD stan bieżący');
  }

  @override
  Future<ControllerActionResult> authenticate(String pin) async {
    return const ControllerActionResult(
      success: true,
      code: 'ok',
      message: 'Zalogowano.',
    );
  }

  @override
  Future<ControllerActionResult> action(
    String action, {
    Map<String, Object?> payload = const {},
    String? pin,
    bool includePin = true,
  }) async {
    return const ControllerActionResult(
      success: true,
      code: 'ok',
      message: 'Wykonano.',
    );
  }

  @override
  Future<Map<String, dynamic>> logs(String pin) async {
    return <String, dynamic>{};
  }

  @override
  Future<Map<String, dynamic>> busDiagnostics(String pin) async {
    return <String, dynamic>{};
  }

  @override
  Future<List<dynamic>> historyFiles() async => <dynamic>[];

  @override
  Future<void> setBrowserTime(int epochSeconds, String pin) async {}

  @override
  Future<Uint8List> download(
    String path, {
    Map<String, String>? queryParameters,
    int maximumBytes = 16 * 1024 * 1024,
  }) async {
    return Uint8List(0);
  }

  @override
  Future<ControllerActionResult> uploadFirmware(
    Uint8List firmware,
    String fileName,
    String pin, {
    void Function(int sent, int total)? onProgress,
  }) async {
    onProgress?.call(firmware.length, firmware.length);
    return const ControllerActionResult(
      success: true,
      code: 'ok',
      message: 'Wgrano.',
    );
  }

  @override
  Future<void> webSession(String sessionId, String state) async {}
}
