import 'package:aquacyd_home/src/aquahub/app.dart';
import 'package:aquacyd_home/src/aquahub/credentials_store.dart';
import 'package:aquacyd_home/src/aquahub/demo.dart';
import 'package:aquacyd_home/src/aquahub/domain.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  testWidgets('pierwsze uruchomienie prowadzi do parowania AquaHub', (
    tester,
  ) async {
    await tester.pumpWidget(
      AquaHubApp(
        credentialsStore: _MemoryHubCredentialsStore(),
        enablePolling: false,
      ),
    );
    await tester.pumpAndSettle();

    expect(find.text('Połącz centrum AquaHub'), findsOneWidget);
    expect(find.text('Adres panelu'), findsOneWidget);
    expect(find.text('Sprawdź AquaHub'), findsOneWidget);
    expect(find.byIcon(Icons.router_outlined), findsOneWidget);
  });

  testWidgets('kompletna sesja pokazuje pulpit, automatyzacje i OTA', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(1280, 900);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);

    await tester.pumpWidget(
      AquaHubApp(
        credentialsStore: DemoHubCredentialsStore(),
        apiFactory: createDemoHubApi,
        enablePolling: false,
      ),
    );
    await tester.pumpAndSettle();

    expect(find.text('System działa prawidłowo'), findsOneWidget);
    expect(find.text('Temperatura wody'), findsOneWidget);

    tester.binding.handleAppLifecycleStateChanged(
      AppLifecycleState.paused,
    );
    tester.binding.handleAppLifecycleStateChanged(
      AppLifecycleState.resumed,
    );
    await tester.pumpAndSettle();
    expect(find.text('System działa prawidłowo'), findsOneWidget);

    await tester.tap(find.text('Automatyzacje').last);
    await tester.pumpAndSettle();
    expect(find.text('Automatyzacje lokalne'), findsOneWidget);
    expect(find.text('Chłodzenie awaryjne'), findsOneWidget);

    await tester.tap(find.text('Więcej').last);
    await tester.pumpAndSettle();
    expect(find.text('Centrum aktualizacji'), findsOneWidget);
    expect(find.text('Wersja 1.0.0 · security 1'), findsOneWidget);
  });
}

final class _MemoryHubCredentialsStore implements HubCredentialsStore {
  HubCredentials? credentials;

  @override
  Future<void> clear() async => credentials = null;

  @override
  Future<HubCredentials?> load() async => credentials;

  @override
  Future<void> save(HubCredentials value) async => credentials = value;
}
