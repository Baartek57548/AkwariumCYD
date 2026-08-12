import 'dart:convert';

import 'package:aquacyd_home/src/app.dart';
import 'package:aquacyd_home/src/data/credentials_store.dart';
import 'package:aquacyd_home/src/data/home_assistant_api.dart';
import 'package:aquacyd_home/src/domain/entity_ids.dart';
import 'package:aquacyd_home/src/domain/models.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:http/http.dart' as http;
import 'package:http/testing.dart';

void main() {
  testWidgets('pierwsze uruchomienie pokazuje bezpieczną konfigurację HA', (
    tester,
  ) async {
    await tester.pumpWidget(
      AquaCydHomeApp(credentialsStore: _MemoryCredentialsStore()),
    );
    await tester.pumpAndSettle();

    expect(find.text('Połącz swój serwer'), findsOneWidget);
    expect(find.text('Adres Home Assistanta'), findsOneWidget);
    expect(find.text('Długoterminowy token dostępu'), findsOneWidget);
    expect(find.byIcon(Icons.shield_outlined), findsOneWidget);
  });

  testWidgets('formularz odrzuca niepełny adres i krótki token', (
    tester,
  ) async {
    await tester.pumpWidget(
      AquaCydHomeApp(credentialsStore: _MemoryCredentialsStore()),
    );
    await tester.pumpAndSettle();

    final fields = find.byType(TextFormField);
    await tester.enterText(fields.at(0), 'homeassistant.local');
    await tester.enterText(fields.at(1), 'short');
    await tester.tap(find.widgetWithText(FilledButton, 'Połącz'));
    await tester.pump();

    expect(find.text('Podaj pełny adres HTTP lub HTTPS.'), findsOneWidget);
    expect(
      find.text('Wklej pełny token z profilu Home Assistant.'),
      findsOneWidget,
    );
  });

  testWidgets('błąd zapisanej sesji prowadzi do ekranu ponowienia', (
    tester,
  ) async {
    final store = _MemoryCredentialsStore(
      HomeAssistantCredentials.parse(
        baseUrl: 'https://ha.example.net',
        accessToken: '12345678901234567890123456789012',
      ),
    );
    await tester.pumpWidget(
      AquaCydHomeApp(
        credentialsStore: store,
        enableRealtime: false,
        apiFactory: (credentials) => HomeAssistantApi(
          credentials,
          client: MockClient((_) async => http.Response('Unavailable', 503)),
        ),
      ),
    );
    await tester.pumpAndSettle();

    expect(find.text('Home Assistant jest nieosiągalny'), findsOneWidget);
    expect(find.text('Home Assistant zwrócił błąd HTTP 503.'), findsOneWidget);
    expect(find.text('Spróbuj ponownie'), findsOneWidget);
  });

  testWidgets('nawigacja otwiera wszystkie ekrany po połączeniu', (
    tester,
  ) async {
    const token = '12345678901234567890123456789012';
    final store = _MemoryCredentialsStore(
      HomeAssistantCredentials.parse(
        baseUrl: 'https://ha.example.net',
        accessToken: token,
      ),
    );
    await tester.pumpWidget(
      AquaCydHomeApp(
        credentialsStore: store,
        enableRealtime: false,
        apiFactory: (credentials) => HomeAssistantApi(
          credentials,
          client: MockClient((request) async {
            if (request.url.path == '/api/config') {
              return http.Response(
                jsonEncode(<String, Object?>{
                  'location_name': 'Dom',
                  'version': '2026.7.4',
                  'time_zone': 'Europe/Warsaw',
                }),
                200,
              );
            }
            if (request.url.path == '/api/states') {
              return http.Response(
                jsonEncode(<Object?>[
                  _entityJson(AquaEntityIds.temperature, '24.2'),
                  _entityJson(AquaEntityIds.ph, '7.1'),
                  _entityJson(AquaEntityIds.ec, '550'),
                  _entityJson(AquaEntityIds.ldr, '820'),
                  _entityJson(AquaEntityIds.controllerSafe, 'on'),
                  _entityJson(AquaEntityIds.leak, 'off'),
                  _entityJson(AquaEntityIds.waterLow, 'off'),
                  _entityJson(AquaEntityIds.alarms, '0'),
                ]),
                200,
              );
            }
            if (request.url.path.startsWith('/api/history/period/')) {
              return http.Response('[]', 200);
            }
            return http.Response('[]', 200);
          }),
        ),
      ),
    );
    await tester.pumpAndSettle();

    expect(find.text('Parametry wody'), findsOneWidget);
    await _openPage(tester, Icons.tune_outlined, 'Sterowanie ręczne');
    await _openPage(tester, Icons.show_chart_outlined, 'Historia pomiarów');
    await _openPage(tester, Icons.shield_outlined, 'Bezpieczeństwo');
    await _openPage(tester, Icons.schedule_outlined, 'Automatyka lokalna');
    await _openPage(tester, Icons.settings_outlined, 'System');
  });
}

final class _MemoryCredentialsStore implements CredentialsStore {
  _MemoryCredentialsStore([this.value]);

  HomeAssistantCredentials? value;

  @override
  Future<void> clear() async {
    value = null;
  }

  @override
  Future<HomeAssistantCredentials?> load() async => value;

  @override
  Future<void> save(HomeAssistantCredentials credentials) async {
    value = credentials;
  }
}

Future<void> _openPage(
  WidgetTester tester,
  IconData icon,
  String expectedTitle,
) async {
  await tester.tap(find.byIcon(icon).last);
  await tester.pumpAndSettle();
  expect(find.text(expectedTitle), findsWidgets);
}

Map<String, Object?> _entityJson(String entityId, String state) {
  return <String, Object?>{
    'entity_id': entityId,
    'state': state,
    'attributes': <String, Object?>{},
    'last_changed': '2026-07-30T08:00:00Z',
    'last_updated': '2026-07-30T08:00:00Z',
  };
}
