import 'package:cyd_aquarium_mobile/app_update/app_update_models.dart';
import 'package:cyd_aquarium_mobile/app_update/app_update_ui.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  testWidgets('update prompt exposes install, later and skip actions', (
    tester,
  ) async {
    AppUpdatePromptAction? selected;
    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(
          body: Builder(
            builder: (context) => FilledButton(
              onPressed: () async {
                selected = await showDialog<AppUpdatePromptAction>(
                  context: context,
                  builder: (_) => AppUpdatePromptDialog(release: _release()),
                );
              },
              child: const Text('Otwórz'),
            ),
          ),
        ),
      ),
    );

    await tester.tap(find.text('Otwórz'));
    await tester.pumpAndSettle();

    expect(find.text('AquaCYD 3.6.0 jest dostępna'), findsOneWidget);
    expect(find.text('Pobierz i zainstaluj'), findsOneWidget);
    expect(find.text('Później'), findsOneWidget);
    expect(find.text('Pomiń tę wersję'), findsOneWidget);

    await tester.tap(find.text('Później'));
    await tester.pumpAndSettle();
    expect(selected, AppUpdatePromptAction.remindLater);
  });

  testWidgets('update prompt remains usable at 320 px with 300 percent text', (
    tester,
  ) async {
    tester.view.devicePixelRatio = 1;
    tester.view.physicalSize = const Size(320, 640);
    addTearDown(tester.view.resetDevicePixelRatio);
    addTearDown(tester.view.resetPhysicalSize);

    await tester.pumpWidget(
      MaterialApp(
        builder: (context, child) => MediaQuery(
          data: MediaQuery.of(
            context,
          ).copyWith(textScaler: const TextScaler.linear(3)),
          child: child!,
        ),
        home: Scaffold(body: AppUpdatePromptDialog(release: _release())),
      ),
    );
    await tester.pump();

    expect(find.text('Pobierz i zainstaluj'), findsOneWidget);
    expect(tester.takeException(), isNull);
  });
}

AppRelease _release() {
  return AppRelease(
    version: const SemanticVersion(3, 6, 0),
    tagName: 'mobile-v3.6.0',
    title: 'AquaCYD Mobile 3.6.0',
    notes: 'Automatyczne sprawdzanie i bezpieczna instalacja aktualizacji.',
    publishedAt: DateTime.utc(2026, 7, 24),
    releasePageUri: Uri.parse(
      'https://github.com/Baartek57548/AkwariumCYD/releases/tag/mobile-v3.6.0',
    ),
    asset: ReleaseAsset(
      name: 'AquaCYD-Control-3.6.0-current.apk',
      downloadUri: Uri.parse(
        'https://github.com/Baartek57548/AkwariumCYD/releases/download/mobile-v3.6.0/AquaCYD-Control-3.6.0-current.apk',
      ),
      size: 57 * 1024 * 1024,
      sha256: 'd' * 64,
      contentType: 'application/vnd.android.package-archive',
    ),
  );
}
