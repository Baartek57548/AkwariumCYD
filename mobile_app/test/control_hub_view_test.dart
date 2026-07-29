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

    await Scrollable.ensureVisible(tester.element(light), alignment: 0.2);
    await tester.pumpAndSettle();
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

  testWidgets('sets the front Aquael lamp directly to the night profile', (
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

    final frontLamp = find.byKey(
      const PageStorageKey<String>('output-card-light1'),
    );
    await Scrollable.ensureVisible(tester.element(frontLamp), alignment: 0.2);
    await tester.pumpAndSettle();
    await tester.tap(frontLamp);
    await tester.pumpAndSettle();

    final profileSelector = find.byKey(
      const ValueKey<String>('aquael-profile-front'),
    );
    expect(profileSelector, findsOneWidget);
    await Scrollable.ensureVisible(
      tester.element(profileSelector),
      alignment: 0.5,
    );
    await tester.pumpAndSettle();
    await tester.tap(profileSelector);
    await tester.pumpAndSettle();
    await tester.tap(find.text('NIGHT — światło nocne').last);
    await tester.pumpAndSettle();

    expect(actions.lastName, 'set_light_profile');
    expect(actions.lastPayload, <String, Object?>{
      'target': 'front',
      'profile': 'night',
    });
  });

  testWidgets('creates a 15 minute timed override for the filter', (
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

    final filterCard = find.byKey(
      const PageStorageKey<String>('output-card-filter'),
    );
    await Scrollable.ensureVisible(tester.element(filterCard), alignment: 0.2);
    await tester.pumpAndSettle();
    await tester.tap(filterCard);
    await tester.pumpAndSettle();

    final timedOverrideButton = find.byKey(
      const ValueKey<String>('timed-override-button-filter'),
    );
    expect(timedOverrideButton, findsOneWidget);
    await Scrollable.ensureVisible(
      tester.element(timedOverrideButton),
      alignment: 0.7,
    );
    await tester.pumpAndSettle();
    await tester.tap(timedOverrideButton);
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const Key('confirm-timed-override-button')));
    await tester.pumpAndSettle();

    expect(actions.lastName, 'set_timed_override');
    expect(actions.lastPayload, <String, Object?>{
      'target': 'filter',
      'state': false,
      'durationSec': 900,
    });
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
