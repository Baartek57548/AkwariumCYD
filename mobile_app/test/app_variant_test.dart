import 'package:cyd_aquarium_mobile/aquarium_app.dart';
import 'package:cyd_aquarium_mobile/connection_home_page.dart';
import 'package:cyd_aquarium_mobile/full_controller/controller_session.dart';
import 'package:cyd_aquarium_mobile/full_controller/controller_shell.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  testWidgets('global rectangular components never exceed 5 px radius', (
    tester,
  ) async {
    ThemeData? capturedTheme;
    await tester.pumpWidget(
      AquariumApp(
        home: Builder(
          builder: (context) {
            capturedTheme = Theme.of(context);
            return const Scaffold(body: SizedBox.expand());
          },
        ),
      ),
    );
    await tester.pump();

    final theme = capturedTheme!;
    final shapes = <ShapeBorder?>[
      theme.cardTheme.shape,
      theme.filledButtonTheme.style?.shape?.resolve({}),
      theme.elevatedButtonTheme.style?.shape?.resolve({}),
      theme.outlinedButtonTheme.style?.shape?.resolve({}),
      theme.textButtonTheme.style?.shape?.resolve({}),
      theme.iconButtonTheme.style?.shape?.resolve({}),
      theme.dialogTheme.shape,
      theme.bottomSheetTheme.shape,
      theme.popupMenuTheme.shape,
      theme.chipTheme.shape,
      theme.navigationBarTheme.indicatorShape,
      theme.navigationRailTheme.indicatorShape,
    ];

    for (final shape in shapes) {
      expect(_maximumRadius(shape), lessThanOrEqualTo(5));
    }
  });

  testWidgets('current variant exposes all connection modes', (tester) async {
    await tester.pumpWidget(const MaterialApp(home: ConnectionHomePage()));

    expect(find.text('AquaCYD Control'), findsOneWidget);
    expect(find.text('Pełna aplikacja przez Wi‑Fi'), findsOneWidget);
    expect(find.text('Połącz przez Bluetooth BLE'), findsOneWidget);
    expect(find.text('Uruchom tryb DEV'), findsOneWidget);
    expect(find.text('Oryginalny panel WWW'), findsOneWidget);
  });

  testWidgets('full variant contains only production transports', (
    tester,
  ) async {
    await tester.pumpWidget(
      const MaterialApp(
        home: ConnectionHomePage(
          brandName: 'AquaCYD Full',
          showDevelopment: false,
          showLegacyWebView: false,
        ),
      ),
    );

    expect(find.text('AquaCYD Full'), findsOneWidget);
    expect(find.text('Pełna aplikacja przez Wi‑Fi'), findsOneWidget);
    expect(find.text('Połącz przez Bluetooth BLE'), findsOneWidget);
    expect(find.text('Uruchom tryb DEV'), findsNothing);
    expect(find.text('Oryginalny panel WWW'), findsNothing);
  });

  testWidgets('dev variant starts directly in the complete controller', (
    tester,
  ) async {
    final session = ControllerSession.development();
    await tester.pumpWidget(
      MaterialApp(home: ControllerShell(session: session)),
    );
    await tester.pumpAndSettle(const Duration(milliseconds: 500));

    expect(find.text('Pulpit'), findsOneWidget);
    expect(find.text('Tryb deweloperski'), findsOneWidget);
    expect(find.text('Pełna aplikacja przez Wi‑Fi'), findsNothing);
  });
}

double _maximumRadius(ShapeBorder? shape) {
  if (shape is! RoundedRectangleBorder) return double.infinity;
  final radius = shape.borderRadius.resolve(TextDirection.ltr);
  return [
    radius.topLeft.x,
    radius.topRight.x,
    radius.bottomLeft.x,
    radius.bottomRight.x,
  ].reduce((first, second) => first > second ? first : second);
}
