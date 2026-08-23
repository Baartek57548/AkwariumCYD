import 'package:cyd_aquarium_mobile/full_controller/controller_api.dart';
import 'package:cyd_aquarium_mobile/full_controller/controller_session.dart';
import 'package:cyd_aquarium_mobile/full_controller/controller_shell.dart';
import 'package:cyd_aquarium_mobile/full_controller/data_access.dart';
import 'package:cyd_aquarium_mobile/full_controller/views/automation_center_view.dart';
import 'package:cyd_aquarium_mobile/full_controller/views/automation_view.dart';
import 'package:cyd_aquarium_mobile/full_controller/widgets.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  testWidgets('automation center uses concise Plan and Reguły labels', (
    tester,
  ) async {
    tester.view.devicePixelRatio = 1;
    tester.view.physicalSize = const Size(900, 900);
    addTearDown(tester.view.resetDevicePixelRatio);
    addTearDown(tester.view.resetPhysicalSize);
    final session = ControllerSession.development();
    addTearDown(session.dispose);

    await tester.pumpWidget(
      _automationCenterHarness(session, _successfulAction),
    );
    await tester.pumpAndSettle();

    expect(find.text('Plan'), findsOneWidget);
    expect(find.text('Reguły'), findsOneWidget);
    expect(find.text('Plan dobowy'), findsNothing);
    expect(find.text('Reguły i bezpieczeństwo'), findsNothing);

    await tester.tap(find.text('Reguły'));
    await tester.pumpAndSettle();

    expect(find.text('Automatyka'), findsOneWidget);
    expect(tester.takeException(), isNull);
  });

  testWidgets('automation cards start collapsed and preserve a local draft', (
    tester,
  ) async {
    tester.view.devicePixelRatio = 1;
    tester.view.physicalSize = const Size(480, 1000);
    addTearDown(tester.view.resetDevicePixelRatio);
    addTearDown(tester.view.resetPhysicalSize);
    final session = ControllerSession.development();
    addTearDown(session.dispose);
    var actionCalls = 0;

    Future<ControllerActionResult> runAction(
      String name, {
      Map<String, Object?> payload = const {},
      String? confirmation,
      bool refreshAfter = true,
    }) async {
      actionCalls++;
      return _successResult;
    }

    await tester.pumpWidget(_automationHarness(session, runAction));
    await tester.pumpAndSettle();

    expect(find.byType(ExpansionTile), findsNWidgets(4));
    expect(find.byType(MetricTile), findsNothing);
    expect(
      find.text('Włączony · cel 25.0 °C · histereza 0.5 °C'),
      findsOneWidget,
    );
    expect(find.text('Zapisz termostat'), findsNothing);

    await tester.tap(find.text('Termostat'));
    await tester.pumpAndSettle();

    final targetField = _textFieldWithLabel('Temperatura docelowa [°C]');
    expect(targetField, findsOneWidget);
    expect(find.text('Zapisz termostat'), findsOneWidget);

    await tester.enterText(targetField, '26.0');
    await tester.pump();
    expect(find.text('Niezapisane zmiany'), findsOneWidget);

    await tester.tap(find.text('Termostat'));
    await tester.pumpAndSettle();
    expect(targetField, findsNothing);

    await tester.tap(find.text('Termostat'));
    await tester.pumpAndSettle();
    expect(targetField, findsOneWidget);
    expect(tester.widget<TextField>(targetField).controller?.text, '26.0');

    await tester.enterText(targetField, '35');
    await tester.tap(find.text('Zapisz termostat'));
    await tester.pump();

    expect(
      find.text('Temperatura musi być w zakresie 18.0–30.0.'),
      findsOneWidget,
    );
    expect(actionCalls, 0);

    await tester.enterText(targetField, '26.0');
    await tester.tap(find.text('Zapisz termostat'));
    await tester.pumpAndSettle();

    expect(actionCalls, 1);
    expect(tester.takeException(), isNull);
  });

  testWidgets('dirty automation draft reports and confirms a remote conflict', (
    tester,
  ) async {
    tester.view.devicePixelRatio = 1;
    tester.view.physicalSize = const Size(480, 1000);
    addTearDown(tester.view.resetDevicePixelRatio);
    addTearDown(tester.view.resetPhysicalSize);
    final session = ControllerSession.development();
    addTearDown(session.dispose);
    String? receivedConfirmation;
    Map<String, Object?>? receivedPayload;

    Future<ControllerActionResult> runAction(
      String name, {
      Map<String, Object?> payload = const {},
      String? confirmation,
      bool refreshAfter = true,
    }) async {
      receivedConfirmation = confirmation;
      receivedPayload = payload;
      return _successResult;
    }

    await tester.pumpWidget(_automationHarness(session, runAction));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Termostat'));
    await tester.pumpAndSettle();

    final targetField = _textFieldWithLabel('Temperatura docelowa [°C]');
    await tester.enterText(targetField, '26.0');
    await tester.pump();
    expect(find.text('Niezapisane zmiany'), findsOneWidget);

    session.status.section('config')['target_temp'] = 24.0;
    await tester.pumpWidget(_automationHarness(session, runAction));
    await tester.pump();

    expect(find.text('Sterownik ma nowsze ustawienia'), findsOneWidget);
    expect(
      find.text('Konflikt · sterownik ma nowsze ustawienia'),
      findsOneWidget,
    );

    await tester.tap(find.text('Zapisz termostat'));
    await tester.pumpAndSettle();

    expect(receivedConfirmation, contains('nowszą konfigurację'));
    expect(receivedPayload?['target'], '26.0');
    expect(
      find.text('Konflikt · sterownik ma nowsze ustawienia'),
      findsNothing,
    );
    expect(tester.takeException(), isNull);
  });

  testWidgets('missing automation data is explained once globally', (
    tester,
  ) async {
    tester.view.devicePixelRatio = 1;
    tester.view.physicalSize = const Size(480, 1000);
    addTearDown(tester.view.resetDevicePixelRatio);
    addTearDown(tester.view.resetPhysicalSize);
    final session = ControllerSession.offline();
    addTearDown(session.dispose);

    await tester.pumpWidget(_automationHarness(session, _unexpectedAction));
    await tester.pumpAndSettle();

    expect(find.text('Brak zapisanych reguł automatyki'), findsOneWidget);
    expect(find.text('Brak zapisanej konfiguracji'), findsNothing);
    expect(find.byType(MetricTile), findsNothing);
    final cards = tester.widgetList<ExpansionTile>(find.byType(ExpansionTile));
    expect(cards, hasLength(4));
    expect(cards.every((card) => !card.enabled), isTrue);

    await tester.tap(find.text('Termostat'));
    await tester.pumpAndSettle();
    expect(_textFieldWithLabel('Temperatura docelowa [°C]'), findsNothing);
    expect(tester.takeException(), isNull);
  });
}

Finder _textFieldWithLabel(String label) {
  return find.byWidgetPredicate(
    (widget) => widget is TextField && widget.decoration?.labelText == label,
  );
}

Widget _automationHarness(
  ControllerSession session,
  RunControllerAction runAction,
) {
  return MaterialApp(
    theme: ThemeData(useMaterial3: true),
    home: Scaffold(
      body: AutomationView(
        key: const ValueKey('automation-view'),
        session: session,
        runAction: runAction,
      ),
    ),
  );
}

Widget _automationCenterHarness(
  ControllerSession session,
  RunControllerAction runAction,
) {
  return MaterialApp(
    theme: ThemeData(useMaterial3: true),
    home: Scaffold(
      body: AutomationCenterView(session: session, runAction: runAction),
    ),
  );
}

Future<ControllerActionResult> _successfulAction(
  String name, {
  Map<String, Object?> payload = const {},
  String? confirmation,
  bool refreshAfter = true,
}) async {
  return _successResult;
}

Future<ControllerActionResult> _unexpectedAction(
  String name, {
  Map<String, Object?> payload = const {},
  String? confirmation,
  bool refreshAfter = true,
}) {
  throw StateError('Offline automation must not issue $name.');
}

const _successResult = ControllerActionResult(
  success: true,
  code: 'ok',
  message: 'Zapisano.',
);
