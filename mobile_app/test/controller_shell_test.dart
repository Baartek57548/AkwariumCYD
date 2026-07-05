import 'package:cyd_aquarium_mobile/full_controller/controller_session.dart';
import 'package:cyd_aquarium_mobile/full_controller/controller_shell.dart';
import 'package:cyd_aquarium_mobile/full_controller/data_access.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  testWidgets('mobile shell exposes four task-oriented destinations', (
    tester,
  ) async {
    tester.view.devicePixelRatio = 1;
    tester.view.physicalSize = const Size(360, 800);
    addTearDown(tester.view.resetDevicePixelRatio);
    addTearDown(tester.view.resetPhysicalSize);

    await tester.pumpWidget(
      MaterialApp(
        theme: ThemeData(useMaterial3: true),
        home: ControllerShell(session: ControllerSession.development()),
      ),
    );
    await tester.pumpAndSettle(const Duration(milliseconds: 500));

    expect(find.byType(NavigationBar), findsOneWidget);
    expect(find.text('Pulpit'), findsOneWidget);
    expect(find.text('Sterowanie'), findsOneWidget);
    expect(find.text('Harmonogram'), findsOneWidget);
    expect(find.text('Ustawienia'), findsOneWidget);
    expect(find.text('Temperatura wody'), findsOneWidget);
    expect(find.text('Jasność względna'), findsOneWidget);
    expect(tester.takeException(), isNull);
  });

  testWidgets('tablet shell uses a four-item navigation rail', (tester) async {
    tester.view.devicePixelRatio = 1;
    tester.view.physicalSize = const Size(1200, 900);
    addTearDown(tester.view.resetDevicePixelRatio);
    addTearDown(tester.view.resetPhysicalSize);

    await tester.pumpWidget(
      MaterialApp(
        home: ControllerShell(session: ControllerSession.development()),
      ),
    );
    await tester.pumpAndSettle(const Duration(milliseconds: 500));

    expect(find.byType(NavigationRail), findsOneWidget);
    expect(find.byType(NavigationBar), findsNothing);
    expect(find.text('Pulpit'), findsOneWidget);
    expect(find.text('Sterowanie'), findsOneWidget);
    expect(find.text('Harmonogram'), findsOneWidget);
    expect(find.text('Ustawienia'), findsOneWidget);
    expect(tester.takeException(), isNull);
  });

  testWidgets('dashboard renders unavailable and alarm sensor states safely', (
    tester,
  ) async {
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

    await tester.pumpWidget(
      MaterialApp(
        theme: ThemeData.dark(useMaterial3: true),
        builder: (context, child) => MediaQuery(
          data: MediaQuery.of(
            context,
          ).copyWith(textScaler: const TextScaler.linear(1.3)),
          child: child!,
        ),
        home: ControllerShell(session: session),
      ),
    );
    await tester.pumpAndSettle(const Duration(milliseconds: 500));

    expect(find.text('System wymaga uwagi'), findsOneWidget);
    expect(find.text('Błąd czujnika'), findsOneWidget);
    expect(find.text('ADC 4095 / 4095'), findsOneWidget);
    expect(find.textContaining('wyciek'), findsWidgets);
    expect(tester.takeException(), isNull);
  });

  testWidgets('all legacy tools remain reachable from the four sections', (
    tester,
  ) async {
    tester.view.devicePixelRatio = 1;
    tester.view.physicalSize = const Size(412, 915);
    addTearDown(tester.view.resetDevicePixelRatio);
    addTearDown(tester.view.resetPhysicalSize);

    await tester.pumpWidget(
      MaterialApp(
        home: ControllerShell(session: ControllerSession.development()),
      ),
    );
    await tester.pumpAndSettle(const Duration(milliseconds: 500));

    await tester.tap(find.text('Sterowanie'));
    await tester.pumpAndSettle();
    expect(find.text('Szybkie sterowanie'), findsOneWidget);
    expect(find.text('Automatyka'), findsOneWidget);
    expect(find.text('Przekaźniki'), findsOneWidget);

    await tester.tap(find.text('Ustawienia'));
    await tester.pumpAndSettle();
    expect(find.text('Zasilanie i OTA'), findsOneWidget);
    expect(find.text('Logi'), findsOneWidget);
    expect(find.text('Diagnostyka sprzętu'), findsOneWidget);
    expect(find.text('Obsługiwane'), findsOneWidget);
    expect(tester.takeException(), isNull);
  });
}
