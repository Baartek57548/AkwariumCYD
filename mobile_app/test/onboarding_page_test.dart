import 'package:cyd_aquarium_mobile/onboarding/onboarding_page.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  testWidgets('onboarding explains safety and returns an offline choice', (
    tester,
  ) async {
    tester.view.devicePixelRatio = 1;
    tester.view.physicalSize = const Size(360, 800);
    addTearDown(tester.view.resetDevicePixelRatio);
    addTearDown(tester.view.resetPhysicalSize);

    await tester.pumpWidget(const MaterialApp(home: _OnboardingHost()));
    await tester.tap(find.text('Otwórz przewodnik'));
    await tester.pumpAndSettle();

    expect(find.text('Twoje akwarium pod kontrolą'), findsOneWidget);
    expect(
      find.textContaining('ostatni zapisany stan również bez urządzenia'),
      findsOneWidget,
    );

    await tester.tap(find.text('Dalej'));
    await tester.pumpAndSettle();
    expect(find.text('Alarmy bez szumu'), findsOneWidget);

    await tester.tap(find.text('Dalej'));
    await tester.pumpAndSettle();
    expect(find.text('Wybierz pierwsze połączenie'), findsOneWidget);
    expect(find.text('Skonfiguruj Wi‑Fi'), findsOneWidget);
    expect(find.text('Połącz przez Bluetooth'), findsOneWidget);

    await tester.tap(find.text('Zacznij offline'));
    await tester.pumpAndSettle();

    expect(find.text('Wynik: offline'), findsOneWidget);
    expect(tester.takeException(), isNull);
  });
}

class _OnboardingHost extends StatefulWidget {
  const _OnboardingHost();

  @override
  State<_OnboardingHost> createState() => _OnboardingHostState();
}

class _OnboardingHostState extends State<_OnboardingHost> {
  OnboardingConnectionChoice? _choice;

  Future<void> _open() async {
    final result = await Navigator.of(context).push<OnboardingConnectionChoice>(
      MaterialPageRoute<OnboardingConnectionChoice>(
        builder: (_) => const OnboardingPage(),
      ),
    );
    if (mounted) setState(() => _choice = result);
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Center(
        child: _choice == null
            ? FilledButton(
                onPressed: _open,
                child: const Text('Otwórz przewodnik'),
              )
            : Text('Wynik: ${_choice!.name}'),
      ),
    );
  }
}
