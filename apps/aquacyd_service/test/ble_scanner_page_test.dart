import 'dart:async';
import 'dart:typed_data';

import 'package:cyd_aquarium_mobile/ble_scanner_page.dart';
import 'package:flutter/material.dart';
import 'package:flutter_reactive_ble/flutter_reactive_ble.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  testWidgets('batches scan results at a 300 ms cadence', (tester) async {
    final scan = StreamController<DiscoveredDevice>.broadcast(sync: true);
    addTearDown(scan.close);
    await tester.pumpWidget(
      MaterialApp(
        home: BleScannerPage(
          permissionRequester: () async {},
          scanStarter: () => scan.stream,
        ),
      ),
    );
    await tester.pump();

    scan
      ..add(_device('device-a', 'Aqua A', -70))
      ..add(_device('device-b', 'Aqua B', -55))
      ..add(_device('device-a', 'Aqua A', -48));

    await tester.pump(const Duration(milliseconds: 299));
    expect(find.text('Aqua A'), findsNothing);
    expect(find.text('Aqua B'), findsNothing);

    await tester.pump(const Duration(milliseconds: 1));
    expect(find.text('Aqua A'), findsOneWidget);
    expect(find.text('Aqua B'), findsOneWidget);
    expect(find.textContaining('-48 dBm'), findsOneWidget);

    await tester.pumpWidget(const SizedBox.shrink());
  });

  testWidgets('opens only one route after repeated taps', (tester) async {
    final scan = StreamController<DiscoveredDevice>.broadcast(sync: true);
    addTearDown(scan.close);
    var openedRoutes = 0;
    await tester.pumpWidget(
      MaterialApp(
        home: BleScannerPage(
          permissionRequester: () async {},
          scanStarter: () => scan.stream,
          devicePageBuilder: (_) {
            openedRoutes += 1;
            return const Scaffold(body: Text('Połączony sterownik'));
          },
        ),
      ),
    );
    await tester.pump();
    scan.add(_device('device-a', 'Aqua Test', -50));
    await tester.pump(const Duration(milliseconds: 300));

    final deviceTile = tester.widget<ListTile>(
      find.widgetWithText(ListTile, 'Aqua Test'),
    );
    deviceTile.onTap!();
    deviceTile.onTap!();
    await tester.runAsync(() async {
      await Future<void>.delayed(const Duration(milliseconds: 10));
    });
    await tester.pump();
    await tester.pump(const Duration(seconds: 1));

    expect(openedRoutes, 1);
    expect(find.text('Połączony sterownik'), findsOneWidget);

    await tester.pumpWidget(const SizedBox.shrink());
  });
}

DiscoveredDevice _device(String id, String name, int rssi) {
  return DiscoveredDevice(
    id: id,
    name: name,
    serviceData: const {},
    manufacturerData: Uint8List(0),
    rssi: rssi,
    serviceUuids: const [],
    connectable: Connectable.available,
  );
}
