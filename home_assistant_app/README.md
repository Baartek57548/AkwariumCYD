# AquaCYD Home

Druga, niezależna aplikacja Flutter dla systemu AquaCYD. Łączy się bezpośrednio
z Home Assistantem przez oficjalne REST API i WebSocket API. Nie komunikuje się
bezpośrednio z CYD, dzięki czemu działa w domu, przez VPN lub przez bezpieczny
adres HTTPS Home Assistanta.

## Zakres aplikacji

- responsywny interfejs Android, iOS i web;
- konfiguracja adresu HA oraz długoterminowego tokenu dostępu;
- bezpieczne przechowywanie tokenu w magazynie systemowym;
- bieżące pomiary, stany urządzeń i diagnostyka ESP32-C6;
- ręczne sterowanie z limitem czasu;
- historia temperatury, pH, EC i światła;
- dekodowanie wszystkich dziesięciu lokalnych flag alarmowych CYD;
- edycja harmonogramów obu lamp, filtra i napowietrzania;
- edycja trybu, nastawy i histerezy termostatu;
- automatyczne ponawianie WebSocket z REST jako ścieżką awaryjną.

CYD pozostaje źródłem prawdy. Home Assistant i aplikacja wysyłają wyłącznie
polecenia przez skrypty z `home_assistant/packages/aquacyd.yaml`; ostateczna
walidacja, interlocki i zapis konfiguracji są wykonywane w sterowniku.

## Wymagania

1. Home Assistant z brokerem MQTT i pakietem
   `home_assistant/packages/aquacyd.yaml`.
2. Bramka `firmware/esp32c6_gateway` połączona z CYD przez szyfrowany ESP-NOW.
3. Użytkownik HA z długoterminowym tokenem dostępu.
4. Flutter 3.41.5 / Dart 3.11.3 do lokalnej kompilacji.

Po pierwszym uruchomieniu bramka publikuje MQTT Discovery również dla trybu
grzałki oraz szesnastu pól harmonogramów wymaganych przez ekran Automatyka.

## Uruchomienie

```powershell
cd home_assistant_app
flutter pub get
flutter analyze
flutter test
flutter run
```

W profilu użytkownika Home Assistanta otwórz sekcję „Długoterminowe tokeny
dostępu”, utwórz osobny token dla AquaCYD Home i wklej go na ekranie połączenia.
Adres musi zawierać schemat i port, np. `http://homeassistant.local:8123` albo
`https://ha.example.net`.

## Bezpieczeństwo

- nieszyfrowane HTTP jest akceptowane tylko dla `localhost`, domen `.local`,
  `.lan` i prywatnych adresów IPv4;
- do zdalnego dostępu należy użyć HTTPS lub VPN;
- token nie jest wyświetlany w diagnostyce ani zapisywany w logach;
- Android nie wykonuje kopii zapasowej danych aplikacji;
- aplikacja web wymaga HTTPS albo `localhost`, aby bezpieczny magazyn
  `flutter_secure_storage` mógł użyć WebCrypto;
- wdrożenie web na innym originie niż HA wymaga prawidłowego CORS w
  `configuration.yaml` Home Assistanta.

Przykład CORS dla lokalnego serwera deweloperskiego:

```yaml
http:
  cors_allowed_origins:
    - http://localhost:8080
```

Nie dodawaj szerokiego originu `*`. Produkcyjnie wpisz dokładny adres hostingu
aplikacji.

## Kompilacje

```powershell
flutter build apk --debug
flutter build web --release
```

Podpisany Android release używa opcjonalnego `android/key.properties` z polami
`storePassword`, `keyPassword`, `keyAlias` i `storeFile`. Klucz oraz plik
properties pozostają poza repozytorium. Bez tego pliku Gradle tworzy
niepodpisany wariant release przeznaczony do dalszego podpisania w CI.

Szczegółowa architektura, kontrakt danych i strategia wdrożenia są opisane w
[`docs/HOME_ASSISTANT_FLUTTER_APP.md`](../docs/HOME_ASSISTANT_FLUTTER_APP.md).
