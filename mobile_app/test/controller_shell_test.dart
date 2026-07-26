import 'package:cyd_aquarium_mobile/aquarium_app.dart';
import 'package:cyd_aquarium_mobile/full_controller/controller_session.dart';
import 'package:cyd_aquarium_mobile/full_controller/controller_shell.dart';
import 'package:cyd_aquarium_mobile/full_controller/data_access.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  testWidgets('mobile shell exposes five command-center destinations', (
    tester,
  ) async {
    tester.view.devicePixelRatio = 1;
    tester.view.physicalSize = const Size(360, 800);
    addTearDown(tester.view.resetDevicePixelRatio);
    addTearDown(tester.view.resetPhysicalSize);

    await tester.pumpWidget(
      AquariumApp(
        home: ControllerShell(session: ControllerSession.development()),
      ),
    );
    await tester.pumpAndSettle(const Duration(milliseconds: 500));

    expect(find.byType(NavigationBar), findsOneWidget);
    for (final label in const [
      'Start',
      'Steruj',
      'Auto',
      'Historia',
      'Więcej',
    ]) {
      expect(find.text(label), findsOneWidget);
    }
    expect(find.text('Temperatura wody'), findsOneWidget);
    expect(find.text('Najważniejsze pomiary'), findsOneWidget);
    expect(find.text('Pozostałe pomiary'), findsOneWidget);
    expect(find.text('Światło otoczenia'), findsNothing);
    expect(tester.takeException(), isNull);
  });

  testWidgets('tablet shell uses a five-item navigation rail', (tester) async {
    tester.view.devicePixelRatio = 1;
    tester.view.physicalSize = const Size(1200, 900);
    addTearDown(tester.view.resetDevicePixelRatio);
    addTearDown(tester.view.resetPhysicalSize);

    await tester.pumpWidget(
      AquariumApp(
        home: ControllerShell(session: ControllerSession.development()),
      ),
    );
    await tester.pumpAndSettle(const Duration(milliseconds: 500));

    expect(find.byType(NavigationRail), findsOneWidget);
    expect(find.byType(NavigationBar), findsNothing);
    for (final label in const [
      'Start',
      'Steruj',
      'Automatyka',
      'Historia',
      'Więcej',
    ]) {
      expect(find.text(label), findsOneWidget);
    }
    expect(tester.takeException(), isNull);
  });

  testWidgets('technical connection metrics stay hidden until requested', (
    tester,
  ) async {
    tester.view.devicePixelRatio = 1;
    tester.view.physicalSize = const Size(412, 915);
    addTearDown(tester.view.resetDevicePixelRatio);
    addTearDown(tester.view.resetPhysicalSize);
    final session = ControllerSession.development();
    addTearDown(session.dispose);

    await tester.pumpWidget(
      AquariumApp(home: ControllerShell(session: session)),
    );
    await tester.pumpAndSettle(const Duration(milliseconds: 500));

    expect(find.text('Sygnał'), findsNothing);
    expect(find.text('Ping'), findsNothing);
    expect(find.text('Synchronizacja'), findsNothing);

    await tester.tap(find.byKey(const Key('connection-details-button')));
    await tester.pumpAndSettle();

    expect(find.text('Sygnał'), findsOneWidget);
    expect(find.text('Ping'), findsOneWidget);
    expect(find.text('Synchronizacja'), findsOneWidget);
    expect(tester.takeException(), isNull);
  });

  testWidgets('critical alarm is prominent and sensor failures render safely', (
    tester,
  ) async {
    final semantics = tester.ensureSemantics();
    tester.view.devicePixelRatio = 1;
    tester.view.physicalSize = const Size(412, 915);
    addTearDown(tester.view.resetDevicePixelRatio);
    addTearDown(tester.view.resetPhysicalSize);
    final session = ControllerSession.development();
    final sensors = session.status.section('sensors');
    final alarms = session.status.section('alarms');
    sensors
      ..['temp_valid'] = false
      ..['ph_valid'] = false
      ..['ec_valid'] = false
      ..['ldr'] = 4095
      ..['ldr_valid'] = true
      ..['flow_valid'] = false
      ..['leak_valid'] = true
      ..['leak_detected'] = true;
    alarms
      ..['activeCount'] = 1
      ..['flags'] = 16
      ..['leak'] = true;
    tester.binding.platformDispatcher.textScaleFactorTestValue = 1.3;
    addTearDown(
      tester.binding.platformDispatcher.clearTextScaleFactorTestValue,
    );

    await tester.pumpWidget(
      AquariumApp(home: ControllerShell(session: session)),
    );
    await tester.pump(const Duration(milliseconds: 500));

    expect(find.text('Wymagana natychmiastowa reakcja'), findsOneWidget);
    expect(find.text('Wykryto wyciek'), findsWidgets);
    expect(find.text('Brak wiarygodnego pomiaru'), findsWidgets);
    expect(find.text('ADC 4095 / 4095'), findsNothing);
    final additionalSensors = find.text('Pozostałe pomiary');
    await tester.ensureVisible(additionalSensors);
    await tester.drag(find.byType(Scrollable).first, const Offset(0, -120));
    await tester.pumpAndSettle();
    await tester.tap(additionalSensors);
    await tester.pumpAndSettle();
    expect(find.text('ADC 4095 / 4095'), findsOneWidget);
    expect(
      find.bySemanticsLabel(
        RegExp(r'Wymagana natychmiastowa reakcja.*Wykryto wyciek'),
      ),
      findsOneWidget,
    );
    expect(tester.takeException(), isNull);
    semantics.dispose();
  });

  testWidgets('all operational areas are reachable from the five sections', (
    tester,
  ) async {
    tester.view.devicePixelRatio = 1;
    tester.view.physicalSize = const Size(412, 915);
    addTearDown(tester.view.resetDevicePixelRatio);
    addTearDown(tester.view.resetPhysicalSize);

    await tester.pumpWidget(
      AquariumApp(
        home: ControllerShell(session: ControllerSession.development()),
      ),
    );
    await tester.pumpAndSettle(const Duration(milliseconds: 500));

    await tester.tap(find.text('Steruj'));
    await tester.pumpAndSettle();
    expect(find.text('Sterowanie'), findsWidgets);
    expect(find.text('Automatyka w tle'), findsOneWidget);
    expect(tester.takeException(), isNull, reason: 'sekcja Steruj');

    await tester.tap(find.text('Auto'));
    await tester.pumpAndSettle();
    expect(find.text('Plan dobowy 24 h'), findsOneWidget);
    expect(find.text('Sekcja automatyki'), findsOneWidget);
    final automationSwitcher = find.byWidgetPredicate(
      (widget) =>
          widget is DropdownButtonFormField<int> &&
          widget.decoration.labelText == 'Sekcja automatyki',
    );
    await tester.tap(automationSwitcher);
    await tester.pumpAndSettle();
    expect(find.text('Reguły'), findsOneWidget);
    await tester.tap(find.text('Reguły').last);
    await tester.pumpAndSettle();
    expect(find.text('Automatyka'), findsOneWidget);
    expect(tester.takeException(), isNull, reason: 'sekcja Auto');

    await tester.tap(find.text('Historia'));
    await tester.pumpAndSettle();
    expect(find.text('Pomiary w czasie'), findsOneWidget);
    expect(find.text('Widok historii'), findsOneWidget);
    final historySwitcher = find.byWidgetPredicate(
      (widget) =>
          widget is DropdownButtonFormField<int> &&
          widget.decoration.labelText == 'Widok historii',
    );
    await tester.tap(historySwitcher);
    await tester.pumpAndSettle();
    expect(find.text('Zdarzenia'), findsOneWidget);
    await tester.tap(find.text('Zdarzenia').last);
    await tester.pumpAndSettle();
    expect(find.text('Dziennik zdarzeń'), findsOneWidget);
    expect(tester.takeException(), isNull, reason: 'sekcja Historia');

    await tester.tap(find.text('Więcej'));
    await tester.pumpAndSettle();
    expect(find.text('Ustawienia i diagnostyka'), findsOneWidget);
    expect(find.text('Urządzenie i połączenie'), findsOneWidget);
    expect(find.text('Ustawienia aplikacji'), findsOneWidget);
    expect(find.text('Diagnostyka sprzętu'), findsNothing);
    expect(find.text('Kanały przekaźników'), findsNothing);
    expect(find.text('Aktualizacje i zasilanie'), findsNothing);

    final serviceTools = find.text('Narzędzia serwisowe');
    await tester.ensureVisible(serviceTools);
    await tester.drag(find.byType(Scrollable).first, const Offset(0, -120));
    await tester.pumpAndSettle();
    await tester.tap(serviceTools);
    await tester.pumpAndSettle();

    expect(find.text('Aktualizacje i zasilanie'), findsOneWidget);
    expect(find.text('Diagnostyka sprzętu'), findsOneWidget);
    expect(find.text('Kanały przekaźników'), findsOneWidget);
    expect(tester.takeException(), isNull, reason: 'sekcja Więcej');
  });

  testWidgets('section navigation preserves unsaved schedule state', (
    tester,
  ) async {
    tester.view.devicePixelRatio = 1;
    tester.view.physicalSize = const Size(412, 915);
    addTearDown(tester.view.resetDevicePixelRatio);
    addTearDown(tester.view.resetPhysicalSize);

    await tester.pumpWidget(
      AquariumApp(
        home: ControllerShell(session: ControllerSession.development()),
      ),
    );
    await tester.pumpAndSettle(const Duration(milliseconds: 500));

    await tester.tap(find.text('Auto'));
    await tester.pumpAndSettle();
    final dropdownFinder = find
        .byWidgetPredicate(
          (widget) =>
              widget is DropdownButtonFormField<int> &&
              widget.decoration.labelText != 'Sekcja automatyki',
        )
        .first;
    await tester.ensureVisible(dropdownFinder);
    await tester.pumpAndSettle();
    final initialValue = tester
        .widget<DropdownButtonFormField<int>>(dropdownFinder)
        .initialValue;
    final targetValue = initialValue == 1 ? 2 : 1;
    final targetLabel = targetValue == 1 ? 'Zawsze ON' : 'Zawsze OFF';

    await tester.tap(dropdownFinder);
    await tester.pumpAndSettle();
    await tester.tap(find.text(targetLabel).last);
    await tester.pumpAndSettle();
    expect(
      tester.widget<DropdownButtonFormField<int>>(dropdownFinder).initialValue,
      targetValue,
    );

    await tester.tap(find.text('Start'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Auto'));
    await tester.pumpAndSettle();

    expect(
      tester.widget<DropdownButtonFormField<int>>(dropdownFinder).initialValue,
      targetValue,
    );
    expect(tester.takeException(), isNull);
  });
}
