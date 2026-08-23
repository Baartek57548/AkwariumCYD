import 'dart:typed_data';

import 'package:cyd_aquarium_mobile/full_controller/controller_api.dart';
import 'package:cyd_aquarium_mobile/full_controller/controller_session.dart';
import 'package:cyd_aquarium_mobile/full_controller/firmware_package.dart';
import 'package:cyd_aquarium_mobile/full_controller/firmware_release_service.dart';
import 'package:cyd_aquarium_mobile/full_controller/views/system_view.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  testWidgets('system view presents only the signed .aqfw update flow', (
    tester,
  ) async {
    final session = ControllerSession.development();
    await session.connect();

    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(
          body: AnimatedBuilder(
            animation: session,
            builder: (context, _) => SystemView(
              session: session,
              ensureAdmin: () async => true,
              runAction:
                  (
                    String name, {
                    Map<String, Object?> payload = const {},
                    String? confirmation,
                    bool refreshAfter = true,
                  }) async => const ControllerActionResult(
                    success: true,
                    code: 'ok',
                    message: 'Wykonano.',
                  ),
            ),
          ),
        ),
      ),
    );

    expect(find.text('Bezpieczne OTA gotowe'), findsOneWidget);
    await tester.drag(find.byType(ListView).first, const Offset(0, -500));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Instalacja z pliku .aqfw'));
    await tester.pumpAndSettle();
    expect(find.text('Wybierz podpisany pakiet .aqfw'), findsOneWidget);
    expect(find.textContaining('firmware.bin'), findsNothing);
    expect(find.textContaining('kontrolą PIN'), findsNothing);

    session.dispose();
    await tester.pumpWidget(const SizedBox.shrink());
  });

  testWidgets('available GitHub firmware requires explicit user consent', (
    tester,
  ) async {
    final repository = _UiFirmwareReleaseRepository();
    final session = ControllerSession.development(
      firmwareReleaseRepository: repository,
    );
    await session.connect();

    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(
          body: AnimatedBuilder(
            animation: session,
            builder: (context, _) => SystemView(
              session: session,
              ensureAdmin: () async => true,
              runAction:
                  (
                    String name, {
                    Map<String, Object?> payload = const {},
                    String? confirmation,
                    bool refreshAfter = true,
                  }) async => const ControllerActionResult(
                    success: true,
                    code: 'ok',
                    message: 'Wykonano.',
                  ),
            ),
          ),
        ),
      ),
    );
    await tester.pumpAndSettle();

    expect(find.text('Dostępny firmware 5.1.0'), findsOneWidget);
    final installButton = find.text('Pobierz i zainstaluj');
    await tester.drag(find.byType(ListView).first, const Offset(0, -520));
    await tester.pumpAndSettle();
    await tester.tap(installButton);
    await tester.pumpAndSettle();

    expect(find.text('Firmware 5.1.0 jest dostępny'), findsOneWidget);
    expect(find.text('Nie teraz'), findsOneWidget);
    expect(find.text('Pobierz i zainstaluj'), findsWidgets);
    expect(repository.downloadCalls, 0);

    await tester.tap(find.text('Nie teraz'));
    await tester.pumpAndSettle();
    expect(repository.downloadCalls, 0);

    session.dispose();
    await tester.pumpWidget(const SizedBox.shrink());
  });
}

class _UiFirmwareReleaseRepository implements FirmwareReleaseRepository {
  int downloadCalls = 0;

  FirmwareRelease get release => FirmwareRelease(
    version: '5.1.0',
    tagName: 'firmware-v5.1.0',
    title: 'AquaCYD Firmware 5.1.0',
    notes: 'Stabilna aktualizacja sterownika.',
    publishedAt: DateTime.utc(2026, 7, 28),
    releasePageUri: Uri.parse(
      'https://github.com/Baartek57548/AkwariumCYD/'
      'releases/tag/firmware-v5.1.0',
    ),
    target: FirmwareTarget.ili9341,
    asset: FirmwareReleaseAsset(
      name: 'AquaCYD-Firmware-5.1.0-ili9341.aqfw',
      downloadUri: Uri.parse(
        'https://github.com/Baartek57548/AkwariumCYD/releases/'
        'download/firmware-v5.1.0/'
        'AquaCYD-Firmware-5.1.0-ili9341.aqfw',
      ),
      size: 1800000,
      sha256: List.filled(64, 'a').join(),
    ),
  );

  @override
  Future<FirmwareRelease?> fetchLatestFirmwareRelease(
    FirmwareTarget target,
  ) async {
    return release;
  }

  @override
  Future<Uint8List> downloadFirmwarePackage({
    required FirmwareRelease release,
    required void Function(double progress) onProgress,
    required FirmwareDownloadCancellationToken cancellationToken,
  }) async {
    downloadCalls++;
    return Uint8List(0);
  }

  @override
  void dispose() {}
}
