import 'package:cyd_aquarium_mobile/design_system.dart';
import 'package:cyd_aquarium_mobile/full_controller/controller_api.dart';
import 'package:cyd_aquarium_mobile/full_controller/controller_session.dart';
import 'package:cyd_aquarium_mobile/full_controller/views/control_hub_view.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  testWidgets('shows the feeder first and reveals output controls on demand', (
    tester,
  ) async {
    tester.view.devicePixelRatio = 1;
    tester.view.physicalSize = const Size(412, 915);
    addTearDown(tester.view.resetDevicePixelRatio);
    addTearDown(tester.view.resetPhysicalSize);

    final session = ControllerSession.development();
    addTearDown(session.dispose);
    final actions = _ActionRecorder();

    await tester.pumpWidget(
      MaterialApp(
        theme: AquaTheme.dark(),
        home: Scaffold(
          body: ControlHubView(
            session: session,
            runAction: actions.run,
            ensureAdmin: _allowAdmin,
          ),
        ),
      ),
    );
    await tester.pump();

    final feeder = find.byKey(const Key('manual-feed-card'));
    final light = find.byKey(
      const PageStorageKey<String>('output-card-light1'),
    );
    final lightSelector = find.byKey(
      const ValueKey<String>('output-mode-selector-light1'),
    );

    expect(feeder, findsOneWidget);
    expect(light, findsOneWidget);
    expect(tester.getTopLeft(feeder).dy, lessThan(tester.getTopLeft(light).dy));
    expect(tester.getSize(light).height, greaterThanOrEqualTo(72));
    expect(lightSelector, findsNothing);
    expect(find.text('Sterowane przez automatykę'), findsNothing);

    await tester.ensureVisible(light);
    await tester.tap(light);
    await tester.pumpAndSettle();

    expect(lightSelector, findsOneWidget);
    expect(
      find.descendant(of: lightSelector, matching: find.text('AUTO')),
      findsOneWidget,
    );
    expect(
      find.descendant(of: lightSelector, matching: find.text('ON')),
      findsOneWidget,
    );
    expect(
      find.descendant(of: lightSelector, matching: find.text('OFF')),
      findsOneWidget,
    );

    await tester.tap(
      find.descendant(of: lightSelector, matching: find.text('OFF')),
    );
    await tester.pumpAndSettle();

    expect(actions.lastName, 'set_light1');
    expect(actions.lastPayload, <String, Object?>{'state': false});
  });
}

Future<bool> _allowAdmin() async => true;

final class _ActionRecorder {
  String? lastName;
  Map<String, Object?>? lastPayload;

  Future<ControllerActionResult> run(
    String name, {
    Map<String, Object?> payload = const {},
    String? confirmation,
    bool refreshAfter = true,
  }) async {
    lastName = name;
    lastPayload = Map<String, Object?>.of(payload);
    return const ControllerActionResult(
      success: true,
      code: 'ok',
      message: 'Wykonano.',
    );
  }
}
