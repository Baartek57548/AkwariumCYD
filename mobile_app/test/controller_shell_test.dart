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
      'Centrum',
      'Steruj',
      'Auto',
      'Historia',
      'System',
    ]) {
      expect(find.text(label), findsOneWidget);
    }
    expect(find.text('Temperatura wody'), findsOneWidget);
    expect(find.text('Telemetria na żywo'), findsOneWidget);
    expect(find.text('Światło otoczenia'), findsOneWidget);
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
      'Centrum',
      'Steruj',
      'Auto',
      'Historia',
      'System',
    ]) {
      expect(find.text(label), findsOneWidget);
    }
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
    expect(find.text('ADC 4095 / 4095'), findsOneWidget);
    expect(
      find.bySemanticsLabel(
        RegExp(r'Wymagana natychmiastowa reakcja.*Transport'),
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
    expect(find.text('Sterowanie operacyjne'), findsOneWidget);
    expect(find.text('Procesy wykonawcze'), findsOneWidget);
    expect(tester.takeException(), isNull, reason: 'sekcja Steruj');

    await tester.tap(find.text('Auto'));
    await tester.pumpAndSettle();
    expect(find.text('Plan dobowy'), findsOneWidget);
    expect(find.text('Sekcja automatyki'), findsOneWidget);
    final automationSwitcher = find.byWidgetPredicate(
      (widget) =>
          widget is DropdownButtonFormField<int> &&
          widget.decoration.labelText == 'Sekcja automatyki',
    );
    await tester.tap(automationSwitcher);
    await tester.pumpAndSettle();
    expect(find.text('Reguły i bezpieczeństwo'), findsOneWidget);
    await tester.tap(find.text('Reguły i bezpieczeństwo').last);
    await tester.pumpAndSettle();
    expect(find.text('Automatyka'), findsOneWidget);
    expect(tester.takeException(), isNull, reason: 'sekcja Auto');

    await tester.tap(find.text('Historia'));
    await tester.pumpAndSettle();
    expect(find.text('Trendy i historia'), findsOneWidget);
    expect(find.text('Sekcja historii'), findsOneWidget);
    final historySwitcher = find.byWidgetPredicate(
      (widget) =>
          widget is DropdownButtonFormField<int> &&
          widget.decoration.labelText == 'Sekcja historii',
    );
    await tester.tap(historySwitcher);
    await tester.pumpAndSettle();
    expect(find.text('Dziennik zdarzeń'), findsOneWidget);
    await tester.tapAt(const Offset(8, 8));
    await tester.pumpAndSettle();
    expect(tester.takeException(), isNull, reason: 'sekcja Historia');

    await tester.tap(find.text('System'));
    await tester.pumpAndSettle();
    expect(find.text('System i administracja'), findsOneWidget);
    expect(find.text('Firmware, energia i OTA'), findsOneWidget);
    expect(find.text('Diagnostyka sprzętu'), findsOneWidget);
    expect(find.text('Kanały przekaźników'), findsOneWidget);
    expect(tester.takeException(), isNull, reason: 'sekcja System');
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

    await tester.tap(find.text('Centrum'));
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
