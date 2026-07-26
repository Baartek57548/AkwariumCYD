import 'package:cyd_aquarium_mobile/aquarium_app.dart';
import 'package:cyd_aquarium_mobile/full_controller/controller_api.dart';
import 'package:cyd_aquarium_mobile/full_controller/controller_session.dart';
import 'package:cyd_aquarium_mobile/full_controller/controller_shell.dart';
import 'package:cyd_aquarium_mobile/full_controller/data_access.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test(
    'offline session preserves the cached snapshot and rejects commands',
    () async {
      final cachedAt = DateTime.utc(2026, 7, 26, 12, 30);
      final session = ControllerSession.offline(
        cachedAt: cachedAt,
        cachedStatus: <String, dynamic>{
          'device': 'AquaCYD Salon',
          'sensors': <String, dynamic>{
            'temp_c': 25.4,
            'temp_valid': true,
            'ph': 7.1,
            'ph_valid': true,
          },
          'network': <String, dynamic>{'rssi': -58},
        },
      );
      addTearDown(session.dispose);

      await session.connect();

      expect(session.isOfflineMode, isTrue);
      expect(session.hasCachedSnapshot, isTrue);
      expect(session.lastUpdate, cachedAt);
      expect(session.status.section('sensors').number('temp_c'), 25.4);
      expect(session.connectionHealth.isOnline, isFalse);
      expect(session.canIssueCommands, isFalse);
      expect(
        session.commandBlockReason,
        contains('ostatnio zapisanych danych'),
      );
      await expectLater(
        session.action('set_light', payload: const {'state': true}),
        throwsA(
          isA<ControllerApiException>()
              .having((error) => error.code, 'code', 'controller_unavailable')
              .having(
                (error) => error.message,
                'message',
                contains('ostatnio zapisanych danych'),
              ),
        ),
      );
      await expectLater(
        session.login('1234'),
        throwsA(
          isA<ControllerApiException>().having(
            (error) => error.code,
            'code',
            'controller_unavailable',
          ),
        ),
      );
    },
  );

  testWidgets(
    'cached offline snapshot opens the complete five-section command center',
    (tester) async {
      tester.view.devicePixelRatio = 1;
      tester.view.physicalSize = const Size(412, 915);
      addTearDown(tester.view.resetDevicePixelRatio);
      addTearDown(tester.view.resetPhysicalSize);

      final seed = ControllerSession.development();
      final cachedStatus = Map<String, dynamic>.from(seed.status);
      seed.dispose();
      final session = ControllerSession.offline(
        cachedStatus: cachedStatus,
        cachedAt: DateTime.now().subtract(const Duration(minutes: 7)),
      );

      await tester.pumpWidget(
        AquariumApp(home: ControllerShell(session: session)),
      );
      await tester.pumpAndSettle(const Duration(milliseconds: 500));

      expect(find.text('Podgląd ostatnio zapisanych danych'), findsOneWidget);
      expect(find.text('Sterownik offline'), findsWidgets);
      expect(find.byKey(const Key('connection-center-button')), findsOneWidget);
      for (final label in const [
        'Centrum',
        'Steruj',
        'Auto',
        'Historia',
        'System',
      ]) {
        expect(find.text(label), findsOneWidget);
      }

      await tester.tap(find.text('Steruj'));
      await tester.pumpAndSettle();
      expect(find.text('Sterowanie operacyjne'), findsOneWidget);
      final feedButton = find.widgetWithText(FilledButton, 'Podaj jedną dawkę');
      await tester.ensureVisible(feedButton);
      expect(tester.widget<FilledButton>(feedButton).onPressed, isNull);

      await tester.tap(find.text('Auto'));
      await tester.pumpAndSettle();
      expect(find.text('Plan dobowy'), findsOneWidget);

      await tester.tap(find.text('Historia'));
      await tester.pumpAndSettle();
      expect(find.text('Trendy i historia'), findsOneWidget);

      await tester.tap(find.text('System'));
      await tester.pumpAndSettle();
      expect(find.text('System i administracja'), findsOneWidget);
      expect(find.text('Sterownik offline'), findsWidgets);
      expect(tester.takeException(), isNull);
    },
  );

  testWidgets('top-right connection button remains available in offline mode', (
    tester,
  ) async {
    var connectionChooserOpenCount = 0;
    final session = ControllerSession.offline(
      cachedStatus: const <String, dynamic>{
        'device': 'AquaCYD',
        'sensors': <String, dynamic>{'temp_c': 24.8, 'temp_valid': true},
      },
      cachedAt: DateTime.utc(2026, 7, 26, 12),
    );

    await tester.pumpWidget(
      AquariumApp(
        home: ControllerShell(
          session: session,
          onOpenConnection: () => connectionChooserOpenCount++,
        ),
      ),
    );
    await tester.pump();

    final connectionButton = find.byKey(const Key('connection-center-button'));
    expect(connectionButton, findsOneWidget);
    expect(
      tester.widget<IconButton>(connectionButton).tooltip,
      'Połączenia Wi‑Fi i Bluetooth',
    );

    await tester.tap(connectionButton);
    await tester.pump();

    expect(connectionChooserOpenCount, 1);
  });

  testWidgets('first offline launch never presents simulated controller data', (
    tester,
  ) async {
    tester.view.devicePixelRatio = 1;
    tester.view.physicalSize = const Size(412, 915);
    addTearDown(tester.view.resetDevicePixelRatio);
    addTearDown(tester.view.resetPhysicalSize);

    await tester.pumpWidget(
      AquariumApp(home: ControllerShell(session: ControllerSession.offline())),
    );
    await tester.pumpAndSettle(const Duration(milliseconds: 300));

    expect(find.text('Brak zapisanych nastaw temperatury'), findsOneWidget);
    expect(find.text('Aplikacja działa bez urządzenia'), findsOneWidget);

    await tester.tap(find.text('Steruj'));
    await tester.pumpAndSettle();
    expect(find.text('BRAK ZAPISANEGO STANU'), findsWidgets);
    expect(find.text('WYJŚCIE FIZYCZNE OFF'), findsNothing);

    await tester.tap(find.text('Auto'));
    await tester.pumpAndSettle();
    expect(find.text('Brak zapisanej osi czasu'), findsOneWidget);
    expect(find.text('Start —'), findsWidgets);
    expect(find.text('Start 10:00'), findsNothing);
    expect(find.text('14:00'), findsNothing);

    await tester.tap(find.text('System'));
    await tester.pumpAndSettle();
    expect(find.text('Brak zapisanej konfiguracji'), findsOneWidget);
    expect(find.textContaining('100%'), findsNothing);
    expect(tester.takeException(), isNull);
  });

  testWidgets(
    'first offline launch keeps automation and display settings neutral',
    (tester) async {
      tester.view.devicePixelRatio = 1;
      tester.view.physicalSize = const Size(412, 1000);
      addTearDown(tester.view.resetDevicePixelRatio);
      addTearDown(tester.view.resetPhysicalSize);

      await tester.pumpWidget(
        AquariumApp(
          home: ControllerShell(session: ControllerSession.offline()),
        ),
      );
      await tester.pumpAndSettle(const Duration(milliseconds: 300));

      await tester.tap(find.text('Auto'));
      await tester.pumpAndSettle();
      await tester.tap(find.text('Plan dobowy'));
      await tester.pumpAndSettle();
      await tester.tap(find.text('Reguły i bezpieczeństwo').last);
      await tester.pumpAndSettle();

      expect(find.text('Brak zapisanych reguł automatyki'), findsOneWidget);
      expect(find.text('Brak zapisanej konfiguracji'), findsWidgets);
      expect(find.textContaining('25.0'), findsNothing);
      expect(find.textContaining('6.8'), findsNothing);

      await tester.tap(find.text('System'));
      await tester.pumpAndSettle();
      final controllerSettings = find.text('Sterownik, sieć i ekran');
      await tester.ensureVisible(controllerSettings);
      await tester.tap(controllerSettings);
      await tester.pumpAndSettle();

      final displaySettings = find.text('Wyświetlacz CYD');
      await tester.drag(find.byType(ListView).last, const Offset(0, -300));
      await tester.pumpAndSettle();
      await tester.tap(displaySettings);
      await tester.pumpAndSettle();

      expect(find.text('Brak zapisanej konfiguracji ekranu'), findsOneWidget);
      expect(find.textContaining('100%'), findsNothing);
      expect(find.text('Zawsze włączony'), findsNothing);
      expect(tester.takeException(), isNull);
    },
  );
}
