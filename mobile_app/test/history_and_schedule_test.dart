import 'dart:typed_data';

import 'package:cyd_aquarium_mobile/full_controller/controller_api.dart';
import 'package:cyd_aquarium_mobile/full_controller/controller_session.dart';
import 'package:cyd_aquarium_mobile/full_controller/history_data.dart';
import 'package:cyd_aquarium_mobile/full_controller/views/charts_view.dart';
import 'package:cyd_aquarium_mobile/full_controller/views/schedules_view.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('decodes packed AQH1 archive records', () {
    final bytes = Uint8List(
      HistoryArchiveCodec.headerSize + HistoryArchiveCodec.recordSize * 2,
    );
    final data = ByteData.sublistView(bytes);
    data
      ..setUint32(0, HistoryArchiveCodec.magic, Endian.little)
      ..setUint16(4, HistoryArchiveCodec.version, Endian.little)
      ..setUint16(6, HistoryArchiveCodec.headerSize, Endian.little)
      ..setUint16(8, HistoryArchiveCodec.recordSize, Endian.little)
      ..setUint32(32, 1_720_000_000, Endian.little)
      ..setInt16(36, 2467, Endian.little)
      ..setInt16(38, 6900, Endian.little)
      ..setInt16(40, 812, Endian.little)
      ..setUint32(42, 180000, Endian.little)
      ..setUint8(46, 0x0F)
      ..setUint32(50, 1_720_000_060, Endian.little)
      ..setUint32(60, 179500, Endian.little)
      ..setUint8(64, 0x00);

    final samples = HistoryArchiveCodec.decode(bytes);

    expect(samples, hasLength(2));
    expect(samples.first.temperature, 24.67);
    expect(samples.first.ph, 6.9);
    expect(samples.first.ldr, 812);
    expect(samples.first.heapBytes, 180000);
    expect(samples.first.heaterOn, isTrue);
    expect(samples.last.temperature, isNull);
  });

  test('rejects truncated AQH1 archive records', () {
    final bytes = Uint8List(HistoryArchiveCodec.headerSize + 1);
    final data = ByteData.sublistView(bytes);
    data
      ..setUint32(0, HistoryArchiveCodec.magic, Endian.little)
      ..setUint16(4, HistoryArchiveCodec.version, Endian.little)
      ..setUint16(6, HistoryArchiveCodec.headerSize, Endian.little)
      ..setUint16(8, HistoryArchiveCodec.recordSize, Endian.little);

    expect(
      () => HistoryArchiveCodec.decode(bytes),
      throwsA(isA<FormatException>()),
    );
  });

  test('DEV history provides a complete six-hour range', () async {
    final session = ControllerSession.development();
    addTearDown(session.dispose);

    final result = await session.loadHistory(const Duration(hours: 6));

    expect(result.samples.length, greaterThanOrEqualTo(360));
    expect(
      result.availableRange,
      greaterThanOrEqualTo(const Duration(hours: 6)),
    );
    expect(result.samples.every((sample) => sample.ph != null), isTrue);
  });

  testWidgets('history ranges and dense table fit a narrow phone', (
    tester,
  ) async {
    tester.view.devicePixelRatio = 1;
    tester.view.physicalSize = const Size(360, 800);
    addTearDown(tester.view.resetDevicePixelRatio);
    addTearDown(tester.view.resetPhysicalSize);
    final session = ControllerSession.development();
    addTearDown(session.dispose);

    await tester.pumpWidget(
      MaterialApp(
        theme: ThemeData(useMaterial3: true),
        home: Scaffold(body: ChartsView(session: session)),
      ),
    );
    await tester.pumpAndSettle();

    expect(find.text('Pomiary w czasie'), findsOneWidget);
    expect(find.text('1H'), findsOneWidget);
    expect(find.text('3H'), findsOneWidget);
    expect(find.text('6H'), findsOneWidget);
    expect(find.text('1D'), findsOneWidget);
    expect(find.text('7D'), findsOneWidget);
    expect(find.text('Dane źródłowe i eksport'), findsOneWidget);
    expect(find.text('Odchylenie'), findsNothing);
    expect(tester.takeException(), isNull);

    await tester.tap(find.text('6H'));
    await tester.pumpAndSettle();
    expect(find.textContaining('zakres 6H'), findsOneWidget);
    expect(tester.takeException(), isNull);
  });

  testWidgets('schedule timeline and editors fit a narrow phone', (
    tester,
  ) async {
    tester.view.devicePixelRatio = 1;
    tester.view.physicalSize = const Size(360, 800);
    addTearDown(tester.view.resetDevicePixelRatio);
    addTearDown(tester.view.resetPhysicalSize);
    final session = ControllerSession.development();
    addTearDown(session.dispose);

    await tester.pumpWidget(
      MaterialApp(
        theme: ThemeData(useMaterial3: true),
        home: Scaffold(
          body: SchedulesView(session: session, runAction: _successfulAction),
        ),
      ),
    );
    await tester.pumpAndSettle();

    expect(find.text('Harmonogram dobowy 24 h'), findsOneWidget);
    expect(find.text('00'), findsOneWidget);
    expect(find.text('06'), findsOneWidget);
    expect(find.text('12'), findsOneWidget);
    expect(find.text('18'), findsOneWidget);
    expect(find.text('24'), findsOneWidget);
    expect(find.text('Światło 1'), findsWidgets);
    expect(find.text('Światło 2'), findsWidgets);
    expect(find.text('DAYBREAK'), findsOneWidget);
    expect(find.text('DAY'), findsOneWidget);
    expect(find.text('NIGHT'), findsOneWidget);
    expect(find.text('AUTO — 3 tryby'), findsNWidgets(2));
    expect(find.text('Napowietrzanie'), findsWidgets);
    expect(tester.takeException(), isNull);
  });
}

Future<ControllerActionResult> _successfulAction(
  String name, {
  Map<String, Object?> payload = const {},
  String? confirmation,
  bool refreshAfter = true,
}) async {
  return const ControllerActionResult(
    success: true,
    code: 'ok',
    message: 'Zapisano.',
  );
}
