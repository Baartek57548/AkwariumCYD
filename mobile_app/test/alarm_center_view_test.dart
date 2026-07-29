import 'package:cyd_aquarium_mobile/alarm_center/alarm_center.dart';
import 'package:cyd_aquarium_mobile/alarm_center/alarm_models.dart';
import 'package:cyd_aquarium_mobile/alarm_center/alarm_notifications.dart';
import 'package:cyd_aquarium_mobile/alarm_center/alarm_preferences.dart';
import 'package:cyd_aquarium_mobile/aquarium_app.dart';
import 'package:cyd_aquarium_mobile/controller_runtime_services.dart';
import 'package:cyd_aquarium_mobile/full_controller/command_center_models.dart';
import 'package:cyd_aquarium_mobile/full_controller/controller_session.dart';
import 'package:cyd_aquarium_mobile/full_controller/views/alarm_center_view.dart';
import 'package:cyd_aquarium_mobile/local_history/local_history_recorder.dart';
import 'package:cyd_aquarium_mobile/local_history/local_history_repository.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';
// ignore: depend_on_referenced_packages
import 'package:shared_preferences_platform_interface/in_memory_shared_preferences_async.dart';
// ignore: depend_on_referenced_packages
import 'package:shared_preferences_platform_interface/shared_preferences_async_platform_interface.dart';
import 'package:sqflite_common_ffi/sqflite_ffi.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  setUpAll(sqfliteFfiInit);

  setUp(() {
    SharedPreferencesAsyncPlatform.instance =
        InMemorySharedPreferencesAsync.empty();
  });

  test('lokalne usługi alarmów kończą inicjalizację', () async {
    final harness = await _RuntimeHarness.create().timeout(
      const Duration(seconds: 10),
    );
    addTearDown(harness.dispose);

    expect(harness.services.initialized, isTrue);
  });

  testWidgets('pusta baza nie jest prezentowana jako bezpieczne akwarium', (
    tester,
  ) async {
    final harness = await _RuntimeHarness.create();
    addTearDown(harness.dispose);

    await tester.pumpWidget(
      AquariumApp(home: AlarmCenterView(services: harness.services)),
    );
    await _pumpUi(tester);

    expect(find.text('Brak danych do oceny'), findsOneWidget);
    expect(find.text('Brak zweryfikowanych danych'), findsOneWidget);
    expect(find.text('Akwarium pod opieką'), findsNothing);
  });

  testWidgets('utrwalony kompletny pomiar odblokowuje bezpieczny pusty stan', (
    tester,
  ) async {
    final harness = await _RuntimeHarness.create();
    addTearDown(harness.dispose);
    final session = ControllerSession.development();
    addTearDown(session.dispose);
    final observedAt = DateTime.utc(2026, 7, 26, 12);
    final model = CommandCenterModel.fromStatus(
      session.status,
      ControllerSessionKind.development,
      connected: true,
      now: observedAt,
    );
    await LocalHistoryRecorder(
      harness.repository,
    ).recordStatus(model, observedAt: observedAt, source: 'test_complete');
    await harness.services.refresh();

    await tester.pumpWidget(
      AquariumApp(home: AlarmCenterView(services: harness.services)),
    );
    await _pumpUi(tester);

    expect(harness.services.hasEvaluatedCompleteSnapshot, isTrue);
    expect(find.text('Akwarium pod opieką'), findsOneWidget);
    expect(find.text('Brak aktywnych alarmów'), findsOneWidget);
    expect(find.text('Brak zweryfikowanych danych'), findsNothing);
  });

  testWidgets('switch zapisuje opt-in dopiero po zgodzie systemowej', (
    tester,
  ) async {
    final harness = await _RuntimeHarness.create(permissionGranted: false);
    addTearDown(harness.dispose);

    await tester.pumpWidget(
      AquariumApp(home: AlarmCenterView(services: harness.services)),
    );
    await _pumpUi(tester);
    await tester.ensureVisible(find.text('Powiadomienia'));
    await _pumpUi(tester);
    await tester.tap(find.text('Powiadomienia'));
    await _pumpUi(tester);
    await tester.ensureVisible(find.text('Powiadomienia o alarmach'));
    await _pumpUi(tester);

    await tester.tap(find.text('Powiadomienia o alarmach'));
    await _pumpUi(tester);

    expect(harness.notifications.permissionRequests, 1);
    expect(harness.services.preferences.enabled, isFalse);

    harness.notifications.permissionGranted = true;
    await tester.tap(find.text('Powiadomienia o alarmach'));
    await _pumpUi(tester);

    expect(harness.notifications.permissionRequests, 2);
    expect(harness.services.preferences.enabled, isTrue);
    expect((await harness.preferences.load()).enabled, isTrue);
    expect(harness.notifications.testNotifications, 1);
  });

  testWidgets('przycisk diagnostyczny wysyła powiadomienie systemowe', (
    tester,
  ) async {
    final harness = await _RuntimeHarness.create(permissionGranted: true);
    addTearDown(harness.dispose);

    await tester.pumpWidget(
      AquariumApp(home: AlarmCenterView(services: harness.services)),
    );
    await _pumpUi(tester);
    await tester.ensureVisible(find.text('Powiadomienia'));
    await _pumpUi(tester);
    await tester.tap(find.text('Powiadomienia'));
    await _pumpUi(tester);
    await tester.ensureVisible(find.text('Wyślij powiadomienie testowe'));
    await _pumpUi(tester);

    await tester.tap(find.text('Wyślij powiadomienie testowe'));
    await _pumpUi(tester);

    expect(harness.notifications.permissionRequests, 1);
    expect(harness.notifications.testNotifications, 1);
    expect(find.text('Wysłano powiadomienie testowe.'), findsOneWidget);
  });
}

Future<void> _pumpUi(WidgetTester tester) async {
  await tester.pump();
  await tester.pump(const Duration(milliseconds: 500));
  await tester.pump();
}

final class _RuntimeHarness {
  _RuntimeHarness({
    required this.repository,
    required this.preferences,
    required this.notifications,
    required this.services,
  });

  final LocalHistoryRepository repository;
  final AlarmPreferencesStore preferences;
  final _PermissionNotificationSink notifications;
  final ControllerRuntimeServices services;

  static Future<_RuntimeHarness> create({
    bool permissionGranted = false,
  }) async {
    final repository = LocalHistoryRepository(
      factory: databaseFactoryFfiNoIsolate,
      databasePath: inMemoryDatabasePath,
    );
    final preferences = AlarmPreferencesStore(
      preferences: SharedPreferencesAsync(),
    );
    final notifications = _PermissionNotificationSink(permissionGranted);
    final alarmCenter = AlarmCenter.standard(
      database: repository,
      notifications: notifications,
      preferences: preferences,
    );
    final services = ControllerRuntimeServices(
      repository: repository,
      alarmCenter: alarmCenter,
      alarmPreferences: preferences,
    );
    await services.initialize();
    return _RuntimeHarness(
      repository: repository,
      preferences: preferences,
      notifications: notifications,
      services: services,
    );
  }

  Future<void> dispose() async {
    services.dispose();
    await repository.close();
  }
}

final class _PermissionNotificationSink implements AlarmNotificationSink {
  _PermissionNotificationSink(this.permissionGranted);

  bool permissionGranted;
  int permissionRequests = 0;
  int testNotifications = 0;

  @override
  Future<void> cancelAlarm(String alarmKey) async {}

  @override
  Future<void> initialize() async {}

  @override
  Future<bool> requestPermission() async {
    permissionRequests++;
    return permissionGranted;
  }

  @override
  Future<void> showTestNotification() async {
    testNotifications++;
  }

  @override
  Future<void> showAppUpdate({
    required String tagName,
    required String version,
    required bool downloaded,
  }) async {}

  @override
  Future<void> showAlarm(AlarmRecord alarm) async {}

  @override
  Future<void> showResolved(AlarmRecord alarm) async {}

  @override
  Future<void> showServiceReminder({
    required String id,
    required String title,
    required String body,
  }) async {}
}
