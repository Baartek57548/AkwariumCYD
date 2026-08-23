import 'package:cyd_aquarium_mobile/full_controller/controller_api.dart';
import 'package:cyd_aquarium_mobile/full_controller/controller_session.dart';
import 'package:cyd_aquarium_mobile/full_controller/views/charts_view.dart';
import 'package:cyd_aquarium_mobile/full_controller/views/insights_center_view.dart';
import 'package:cyd_aquarium_mobile/full_controller/views/logs_view.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  testWidgets('history center uses clear measurement and event labels', (
    tester,
  ) async {
    tester.view.devicePixelRatio = 1;
    tester.view.physicalSize = const Size(800, 900);
    addTearDown(tester.view.resetDevicePixelRatio);
    addTearDown(tester.view.resetPhysicalSize);
    final session = ControllerSession.development();
    addTearDown(session.dispose);

    await tester.pumpWidget(
      MaterialApp(
        theme: ThemeData(useMaterial3: true),
        home: Scaffold(
          body: InsightsCenterView(
            session: session,
            runAction: _successfulAction,
            ensureAdmin: () async => true,
          ),
        ),
      ),
    );
    await tester.pumpAndSettle();

    expect(find.text('Pomiary'), findsOneWidget);
    expect(find.text('Zdarzenia'), findsOneWidget);
    expect(find.text('Pomiary w czasie'), findsOneWidget);
    expect(tester.takeException(), isNull);
  });

  testWidgets('raw history data and export stay collapsed until requested', (
    tester,
  ) async {
    tester.view.devicePixelRatio = 1;
    tester.view.physicalSize = const Size(412, 915);
    addTearDown(tester.view.resetDevicePixelRatio);
    addTearDown(tester.view.resetPhysicalSize);
    final session = ControllerSession.development();
    addTearDown(session.dispose);

    await tester.pumpWidget(
      MaterialApp(
        theme: ThemeData(useMaterial3: true),
        home: Scaffold(body: ChartsView(session: session)),
      ),
    );
    await tester.pumpAndSettle();

    final details = find.text('Dane źródłowe i eksport');
    expect(find.text('Pomiary w czasie'), findsOneWidget);
    expect(details, findsOneWidget);
    expect(find.text('Odchylenie'), findsNothing);
    expect(find.text('Eksportuj CSV'), findsNothing);

    await tester.ensureVisible(details);
    await tester.tap(details);
    await tester.pumpAndSettle();

    expect(find.text('Odchylenie'), findsOneWidget);
    expect(find.text('Eksportuj CSV'), findsOneWidget);
    expect(tester.takeException(), isNull);
  });

  testWidgets('logs never request admin access automatically and hide tools', (
    tester,
  ) async {
    tester.view.devicePixelRatio = 1;
    tester.view.physicalSize = const Size(412, 915);
    addTearDown(tester.view.resetDevicePixelRatio);
    addTearDown(tester.view.resetPhysicalSize);
    final session = ControllerSession.development();
    addTearDown(session.dispose);
    var adminRequests = 0;

    Future<bool> ensureAdmin() async {
      adminRequests += 1;
      if (!session.isAdmin) {
        await session.login('1234');
      }
      return true;
    }

    await tester.pumpWidget(
      MaterialApp(
        theme: ThemeData(useMaterial3: true),
        home: Scaffold(
          body: LogsView(
            session: session,
            runAction: _successfulAction,
            ensureAdmin: ensureAdmin,
          ),
        ),
      ),
    );
    await tester.pumpAndSettle();

    expect(adminRequests, 0);
    expect(find.text('Informacje (2)'), findsOneWidget);
    expect(find.text('Narzędzia dziennika'), findsOneWidget);
    expect(find.text('Eksportuj TXT'), findsNothing);

    await tester.tap(find.byKey(const Key('load-controller-logs')));
    await tester.pumpAndSettle();

    expect(adminRequests, 1);
    expect(find.text('Logi zostały zsynchronizowane.'), findsOneWidget);

    final tools = find.text('Narzędzia dziennika');
    await tester.ensureVisible(tools);
    await tester.tap(tools);
    await tester.pumpAndSettle();

    expect(find.text('Eksportuj TXT'), findsOneWidget);
    expect(tester.takeException(), isNull);
    session.logout();
  });
}

Future<ControllerActionResult> _successfulAction(
  String name, {
  Map<String, Object?> payload = const {},
  String? confirmation,
  bool refreshAfter = true,
}) async {
  return const ControllerActionResult(
    success: true,
    code: 'ok',
    message: 'Wykonano.',
  );
}
