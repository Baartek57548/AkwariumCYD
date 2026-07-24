import 'package:cyd_aquarium_mobile/full_controller/widgets.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  testWidgets('responsive grid never squeezes a tile below its minimum width', (
    tester,
  ) async {
    const firstKey = Key('first-tile');
    const secondKey = Key('second-tile');

    await tester.pumpWidget(
      const MaterialApp(
        home: Scaffold(
          body: Align(
            alignment: Alignment.topLeft,
            child: SizedBox(
              width: 292,
              child: ResponsiveGrid(
                minimumChildWidth: 145,
                spacing: 10,
                children: [
                  SizedBox(key: firstKey, height: 40),
                  SizedBox(key: secondKey, height: 40),
                ],
              ),
            ),
          ),
        ),
      ),
    );

    expect(tester.getSize(find.byKey(firstKey)).width, 292);
    expect(tester.getSize(find.byKey(secondKey)).width, 292);
    expect(tester.getTopLeft(find.byKey(secondKey)).dy, greaterThan(0));
  });

  testWidgets('section header stacks its action when text is enlarged', (
    tester,
  ) async {
    await tester.pumpWidget(
      MaterialApp(
        builder: (context, child) => MediaQuery(
          data: MediaQuery.of(
            context,
          ).copyWith(textScaler: const TextScaler.linear(1.8)),
          child: child!,
        ),
        home: Scaffold(
          body: SizedBox(
            width: 360,
            child: SectionHeader(
              title: 'Długi nagłówek sekcji',
              description: 'Opis pozostaje czytelny przy dużym tekście.',
              trailing: OutlinedButton(
                onPressed: () {},
                child: const Text('Akcja'),
              ),
            ),
          ),
        ),
      ),
    );

    final descriptionBottom = tester.getBottomLeft(
      find.text('Opis pozostaje czytelny przy dużym tekście.'),
    );
    final actionTop = tester.getTopLeft(
      find.widgetWithText(OutlinedButton, 'Akcja'),
    );
    expect(actionTop.dy, greaterThan(descriptionBottom.dy));
    expect(tester.takeException(), isNull);
  });

  testWidgets('error panel announces the state and keeps retry accessible', (
    tester,
  ) async {
    var retried = false;
    final semantics = tester.ensureSemantics();

    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(
          body: StatePanel.error(
            title: 'Brak połączenia',
            message: 'Sterownik nie odpowiedział.',
            actionLabel: 'Spróbuj ponownie',
            onAction: () => retried = true,
          ),
        ),
      ),
    );

    expect(
      find.bySemanticsLabel('Brak połączenia. Sterownik nie odpowiedział.'),
      findsOneWidget,
    );
    final retryButton = find.widgetWithText(FilledButton, 'Spróbuj ponownie');
    expect(retryButton, findsOneWidget);
    await tester.tap(retryButton);
    expect(retried, isTrue);
    semantics.dispose();
  });
}
