import 'package:cyd_aquarium_mobile/full_controller/controller_session.dart';
import 'package:cyd_aquarium_mobile/full_controller/controller_shell.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  testWidgets('complete DEV application renders dashboard and navigation', (
    tester,
  ) async {
    final session = ControllerSession.development();
    await tester.binding.setSurfaceSize(const Size(1200, 900));
    addTearDown(() => tester.binding.setSurfaceSize(null));

    await tester.pumpWidget(
      MaterialApp(home: ControllerShell(session: session)),
    );
    await tester.pumpAndSettle(const Duration(milliseconds: 300));

    expect(find.text('Pulpit'), findsWidgets);
    expect(find.text('Parametry akwarium'), findsOneWidget);
    expect(find.text('Centrum sterowania'), findsOneWidget);
    expect(find.text('Harmonogramy'), findsOneWidget);
    expect(find.text('Diagnostyka'), findsOneWidget);
  });
}
