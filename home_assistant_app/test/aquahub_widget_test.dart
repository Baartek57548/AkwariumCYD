import 'package:aquacyd_home/src/aquahub/app.dart';
import 'package:aquacyd_home/src/aquahub/credentials_store.dart';
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
