import 'package:aquacyd_design_system/aquacyd_design_system.dart';
import 'package:aquacyd_home/src/design/app_theme.dart';
import 'package:aquacyd_home/src/design/components.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('shared design tokens preserve interaction and motion hierarchy', () {
    expect(ProductLayout.minimumTouchTarget, greaterThanOrEqualTo(48));
    expect(ProductMotion.fast, lessThan(ProductMotion.standard));
    expect(ProductMotion.standard, lessThan(ProductMotion.emphasized));
    expect(ProductRadius.control, lessThan(ProductRadius.card));
    expect(ProductRadius.card, lessThan(ProductRadius.hero));
  });

  testWidgets(
    'reusable states and controls remain accessible with enlarged text',
    (tester) async {
      tester.view.physicalSize = const Size(320, 568);
      tester.view.devicePixelRatio = 1;
      tester.platformDispatcher.textScaleFactorTestValue = 2;
      addTearDown(tester.view.resetPhysicalSize);
      addTearDown(tester.view.resetDevicePixelRatio);
      addTearDown(tester.platformDispatcher.clearTextScaleFactorTestValue);
      final semantics = tester.ensureSemantics();
      var retried = false;
      var toggled = false;
      var sliderValue = 22.0;

      await tester.pumpWidget(
        MaterialApp(
          theme: AquaTheme.light(),
          home: Scaffold(
            body: SingleChildScrollView(
              padding: const EdgeInsets.all(ProductSpacing.md),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: <Widget>[
                  const HomeStatusChip(
                    icon: Icons.cloud_off_rounded,
                    label: 'Urządzenie offline',
                    tone: HomeStatusTone.error,
                  ),
                  const SizedBox(height: ProductSpacing.md),
                  HomeToggle(
                    label: 'Oświetlenie',
                    description: 'Sterowanie główne',
                    value: toggled,
                    onChanged: (value) => toggled = value,
                  ),
                  HomeSlider(
                    label: 'Temperatura',
                    value: sliderValue,
                    minimum: 18,
                    maximum: 30,
                    valueLabel: '22,0 °C',
                    onChanged: (value) => sliderValue = value,
                  ),
                  HomeEmptyState(
                    icon: Icons.sensors_off_rounded,
                    title: 'Brak danych',
                    message: 'Sprawdź połączenie i spróbuj ponownie.',
                    actionLabel: 'Ponów',
                    onAction: () => retried = true,
                  ),
                ],
              ),
            ),
          ),
        ),
      );
      await tester.pumpAndSettle();

      expect(find.bySemanticsLabel('Urządzenie offline'), findsOneWidget);
      expect(find.text('Brak danych'), findsOneWidget);
      await tester.ensureVisible(find.widgetWithText(FilledButton, 'Ponów'));
      await tester.tap(find.widgetWithText(FilledButton, 'Ponów'));
      expect(retried, isTrue);
      expect(tester.takeException(), isNull);
      semantics.dispose();
    },
  );
}
