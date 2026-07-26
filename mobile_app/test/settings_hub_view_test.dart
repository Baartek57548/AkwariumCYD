import 'package:cyd_aquarium_mobile/aquarium_app.dart';
import 'package:cyd_aquarium_mobile/app_settings.dart';
import 'package:cyd_aquarium_mobile/full_controller/controller_api.dart';
import 'package:cyd_aquarium_mobile/full_controller/controller_session.dart';
import 'package:cyd_aquarium_mobile/full_controller/views/settings_hub_view.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

void main() {
  setUp(() {
    SharedPreferences.setMockInitialValues(<String, Object>{});
    AppSettings.expertModeNotifier.value = false;
  });

  tearDown(() {
    AppSettings.expertModeNotifier.value = false;
  });

  testWidgets(
    'hub ukrywa serwis w trybie prostym i odblokowuje go po autoryzacji',
    (tester) async {
      _configurePhoneViewport(tester);
      final session = ControllerSession.development();
      addTearDown(session.dispose);

      await tester.pumpWidget(
        AquariumApp(
          home: Scaffold(
            body: SettingsHubView(
              session: session,
              runAction: _successfulAction,
              ensureAdmin: _authorized,
            ),
          ),
        ),
      );
      await tester.pump();

      expect(find.text('Ustawienia i diagnostyka'), findsOneWidget);
      expect(find.text('Urządzenie i połączenie'), findsOneWidget);
      expect(find.text('Ustawienia aplikacji'), findsOneWidget);
      expect(find.text('Informacje o sterowniku'), findsOneWidget);
      expect(find.text('Narzędzia serwisowe'), findsOneWidget);
      expect(find.text('Wolna pamięć'), findsNothing);
      expect(find.textContaining('Uptime:'), findsNothing);
      expect(find.text('Diagnostyka sprzętu'), findsNothing);
      expect(find.text('Kanały przekaźników'), findsNothing);
      expect(find.text('Aktualizacje i zasilanie'), findsNothing);

      await tester.tap(find.byKey(const Key('device-details-section')));
      await tester.pumpAndSettle();
      expect(find.textContaining('Firmware:'), findsOneWidget);
      expect(find.textContaining('Uptime:'), findsOneWidget);

      final serviceTools = find.text('Narzędzia serwisowe');
      await tester.ensureVisible(serviceTools);
      await tester.drag(find.byType(Scrollable).first, const Offset(0, -120));
      await tester.pumpAndSettle();
      await tester.tap(serviceTools);
      await tester.pumpAndSettle();

      expect(
        find.text('Tryb prosty chroni ustawienia serwisowe'),
        findsOneWidget,
      );
      expect(find.text('Diagnostyka sprzętu'), findsNothing);

      final enableExpert = find.byKey(const Key('enable-expert-mode-button'));
      await Scrollable.ensureVisible(
        tester.element(enableExpert),
        alignment: 0.5,
      );
      await tester.pumpAndSettle();
      await tester.tap(enableExpert);
      await tester.pumpAndSettle();

      expect(AppSettings.expertModeNotifier.value, isTrue);
      expect(find.text('Diagnostyka sprzętu'), findsOneWidget);
      expect(find.text('Kanały przekaźników'), findsOneWidget);
      expect(find.text('Aktualizacje i zasilanie'), findsOneWidget);
      expect(find.text('Niedostępne w tej wersji firmware.'), findsOneWidget);
      expect(tester.takeException(), isNull);
    },
  );

  testWidgets('widoczne i serwisowe karty zachowują routing', (tester) async {
    _configurePhoneViewport(tester);
    AppSettings.expertModeNotifier.value = true;
    final session = ControllerSession.development();
    addTearDown(session.dispose);

    await tester.pumpWidget(
      AquariumApp(
        home: Scaffold(
          body: SettingsHubView(
            session: session,
            runAction: _successfulAction,
            ensureAdmin: _authorized,
          ),
        ),
      ),
    );
    await tester.pump();

    await tester.tap(find.text('Urządzenie i połączenie'));
    await tester.pumpAndSettle();
    expect(find.text('Ustawienia urządzenia'), findsOneWidget);
    expect(find.byType(BackButton), findsOneWidget);

    await tester.tap(find.byType(BackButton));
    await tester.pumpAndSettle();
    final serviceTools = find.text('Narzędzia serwisowe');
    await tester.ensureVisible(serviceTools);
    await tester.drag(find.byType(Scrollable).first, const Offset(0, -120));
    await tester.pumpAndSettle();
    await tester.tap(serviceTools);
    await tester.pumpAndSettle();

    final updates = find.text('Aktualizacje i zasilanie');
    await tester.scrollUntilVisible(
      updates,
      300,
      scrollable: find.byType(Scrollable).first,
    );
    await tester.pumpAndSettle();
    await tester.tap(updates);
    await tester.pumpAndSettle();

    expect(find.text('Zasilanie i tryb ECO'), findsOneWidget);
    expect(find.byType(BackButton), findsOneWidget);
    expect(tester.takeException(), isNull);
  });
}

void _configurePhoneViewport(WidgetTester tester) {
  tester.view.devicePixelRatio = 1;
  tester.view.physicalSize = const Size(412, 915);
  addTearDown(tester.view.resetDevicePixelRatio);
  addTearDown(tester.view.resetPhysicalSize);
}

Future<bool> _authorized() async => true;

Future<ControllerActionResult> _successfulAction(
  String name, {
  Map<String, Object?> payload = const {},
  String? confirmation,
  bool refreshAfter = true,
}) async {
  return ControllerActionResult(
    success: true,
    code: 'ok',
    message: 'Wykonano $name.',
  );
}
