import 'package:cyd_aquarium_mobile/aquarium_app.dart';
import 'package:cyd_aquarium_mobile/connection_home_page.dart';
import 'package:cyd_aquarium_mobile/design_system.dart';
import 'package:cyd_aquarium_mobile/full_controller/controller_session.dart';
import 'package:cyd_aquarium_mobile/full_controller/controller_shell.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  testWidgets(
    'design system keeps coherent shapes and accessible tap targets',
    (tester) async {
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
      expect(_maximumRadius(theme.cardTheme.shape), AquaRadius.card);
      expect(
        _maximumRadius(theme.filledButtonTheme.style?.shape?.resolve({})),
        AquaRadius.control,
      );
      expect(
        _maximumRadius(theme.navigationBarTheme.indicatorShape),
        AquaRadius.control,
      );
      expect(_maximumRadius(theme.dialogTheme.shape), AquaRadius.card);
      expect(_maximumRadius(theme.bottomSheetTheme.shape), AquaRadius.hero);
      expect(theme.materialTapTargetSize, MaterialTapTargetSize.padded);

      final buttonStyles = [
        theme.filledButtonTheme.style,
        theme.elevatedButtonTheme.style,
        theme.outlinedButtonTheme.style,
        theme.textButtonTheme.style,
        theme.iconButtonTheme.style,
      ];
      for (final style in buttonStyles) {
        expect(
          style?.minimumSize?.resolve({})?.height,
          greaterThanOrEqualTo(48),
        );
      }
    },
  );

  testWidgets(
    'production chooser exposes only supported connection transports',
    (tester) async {
      await tester.pumpWidget(const AquariumApp(home: ConnectionHomePage()));

      expect(find.text('AquaCYD Control'), findsOneWidget);
      expect(find.text('Połącz centrum dowodzenia'), findsOneWidget);
      expect(find.text('Sterownik przez Wi‑Fi'), findsOneWidget);
      expect(find.text('Sterownik przez BLE'), findsOneWidget);
      expect(find.text('Symulator całego sterownika'), findsNothing);
      expect(find.text('Panel WWW zgodności'), findsNothing);
      expect(find.text('Narzędzia serwisowe'), findsNothing);
    },
  );

  testWidgets('service tools appear only after explicit variant opt-in', (
    tester,
  ) async {
    await tester.pumpWidget(
      const AquariumApp(
        home: ConnectionHomePage(
          showDevelopment: true,
          showLegacyWebView: true,
        ),
      ),
    );
    final serviceTools = find.text('Narzędzia serwisowe');
    await tester.ensureVisible(serviceTools);
    await tester.pump();
    await tester.tap(serviceTools);
    await tester.pumpAndSettle();

    expect(find.text('Symulator całego sterownika'), findsOneWidget);
    expect(find.text('Panel WWW zgodności'), findsOneWidget);
  });

  testWidgets('connection chooser remains usable on a narrow scaled display', (
    tester,
  ) async {
    tester.view.devicePixelRatio = 1;
    tester.view.physicalSize = const Size(320, 640);
    addTearDown(tester.view.resetDevicePixelRatio);
    addTearDown(tester.view.resetPhysicalSize);

    await tester.pumpWidget(
      AquariumApp(home: const ConnectionHomePage(), title: 'AquaCYD Control'),
    );
    tester.binding.platformDispatcher.textScaleFactorTestValue = 1.8;
    addTearDown(
      tester.binding.platformDispatcher.clearTextScaleFactorTestValue,
    );
    await tester.pump();

    expect(find.text('Połącz centrum dowodzenia'), findsOneWidget);
    expect(find.text('Sterownik przez Wi‑Fi'), findsOneWidget);
    expect(find.text('Sterownik przez BLE'), findsOneWidget);
    expect(tester.takeException(), isNull);
  });

  testWidgets(
    'full variant preserves brand while hiding development surfaces',
    (tester) async {
      await tester.pumpWidget(
        const AquariumApp(
          home: ConnectionHomePage(
            brandName: 'AquaCYD Full',
            showDevelopment: false,
            showLegacyWebView: false,
          ),
        ),
      );

      expect(find.text('AquaCYD Full'), findsOneWidget);
      expect(find.text('Sterownik przez Wi‑Fi'), findsOneWidget);
      expect(find.text('Sterownik przez BLE'), findsOneWidget);
      expect(find.text('Symulator całego sterownika'), findsNothing);
      expect(find.text('Panel WWW zgodności'), findsNothing);
    },
  );

  testWidgets('development session starts in the complete command center', (
    tester,
  ) async {
    await tester.pumpWidget(
      AquariumApp(
        home: ControllerShell(session: ControllerSession.development()),
      ),
    );
    await tester.pumpAndSettle(const Duration(milliseconds: 500));

    expect(find.text('Start'), findsOneWidget);
    expect(find.text('Temperatura wody'), findsOneWidget);
    expect(find.text('Sterownik przez Wi‑Fi'), findsNothing);
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
