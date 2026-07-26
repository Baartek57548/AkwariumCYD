# AquaCYD Control 4

Profesjonalne, mobilne centrum dowodzenia dla sterownika akwarium AquaCYD. Aplikacja łączy się bezpośrednio ze sterownikiem przez Wi‑Fi lub BLE, prezentuje stan instalacji, chroni operacje administracyjne kodem PIN i prowadzi użytkownika od alarmu do właściwej akcji.

## Centrum dowodzenia

- pięć obszarów roboczych: **Centrum**, **Steruj**, **Auto**, **Historia** i **System**,
- nagłówek kondycji z trybem połączenia, RSSI, pingiem i czasem ostatniej synchronizacji,
- priorytetyzacja alarmów, wiarygodności temperatury i stanu urządzeń wykonawczych,
- sterowanie oświetleniem, filtrem, napowietrzaniem, termostatem i karmnikiem,
- czytelne rozróżnienie fizycznego stanu wyjścia od trybu automatyki,
- harmonogramy i reguły temperatury, CO₂, ATO oraz zabezpieczenia przed wyciekiem,
- wykresy temperatury, próbki historyczne, logi i eksport danych,
- diagnostyka magistral oraz czujników, ustawienia urządzenia i OTA; edytor profilu przekaźników pozostaje bezpiecznie zablokowany do czasu obsługi profilu przez firmware,
- responsywny interfejs Material 3 w jasnym i ciemnym motywie.

Komendy są blokowane, gdy sterownik jest offline albo telemetria jest nieaktualna. Operacje sieciowe są serializowane, szybkie akcje nie zasypują mikrokontrolera żądaniami, a odpytywanie po błędzie wraca z opóźnieniem wykładniczym. Ostatni prawidłowy odczyt pozostaje widoczny jako dane archiwalne.

## Warianty aplikacji

| Wariant | Punkt wejścia | Zastosowanie | Identyfikator Androida |
| --- | --- | --- | --- |
| `current` | `lib/main.dart` | produkcyjne centrum dowodzenia; automatyczne sprawdzanie aktualizacji w buildzie release | `pl.cydakwarium.cyd_aquarium_mobile` |
| `full` | `lib/main_full.dart` | równoległa instalacja pełnego interfejsu bez kanału aktualizacji `current` | `pl.cydakwarium.cyd_aquarium_mobile.full` |
| `dev` | `lib/main_dev.dart` | symulator sterownika w pamięci RAM, przeznaczony do rozwoju i testów UI | `pl.cydakwarium.cyd_aquarium_mobile.dev` |

Kod zawiera techniczny moduł zgodności ze starszym panelem WWW oparty na WebView, ale wydanie 4.0.0 nie pokazuje go w produkcyjnym ekranie połączenia. Nie istnieje osobny flavor ani publikowany APK `legacy`; podstawowym interfejsem jest natywne centrum dowodzenia.

## Wymagania

- Flutter 3.41.5 lub nowszy i Dart 3.11,
- Android 7.0 / API 24 lub nowszy; projekt zachowuje również konfigurację iOS 13+,
- Bluetooth i wymagane uprawnienia systemowe dla połączenia BLE,
- wspólna sieć Wi‑Fi telefonu i sterownika albo połączenie telefonu z punktem dostępowym sterownika,
- prywatny klucz właściciela aplikacji do podpisania produkcyjnego APK.

## Konfiguracja sterownika

1. Połącz telefon z siecią sterownika.
2. Wybierz połączenie Wi‑Fi i użyj `http://akwarium.local`. Dla punktu dostępowego sterownika użyj `http://192.168.4.1`.
3. Jeśli mDNS nie działa w danej sieci, wpisz bezpośredni adres IPv4 sterownika wraz ze schematem `http://`.
4. Zaloguj operacje administracyjne kodem PIN skonfigurowanym w firmware. Fabryczny PIN bieżącego firmware i symulatora DEV to `1234`; przed wdrożeniem należy go zmienić.

Adres nie może zawierać loginu, hasła, parametrów zapytania ani fragmentu. Pełny zakres funkcji jest dostępny przez natywne REST API. BLE zapewnia sterowanie bez sieci, lecz zakres ekranów zależy od wersji protokołu zaimplementowanej w firmware. Specyfikacja znajduje się w [docs/ble-protocol.md](docs/ble-protocol.md).

Nie należy wystawiać HTTP sterownika bezpośrednio do Internetu. Bieżący firmware przesyła PIN w lokalnym protokole HTTP, dlatego sieć musi być zaufana i odizolowana. Aplikacja usuwa PIN po przejściu w tło oraz po pięciu minutach bezczynności, ale nie zastępuje to TLS ani krótkotrwałego tokenu sesyjnego po stronie firmware.

## Architektura

- `full_controller/views/` — ekrany centrum dowodzenia i formularze,
- `command_center_models.dart` — typowane modele bezpieczeństwa, czujników, wyjść, alarmów i następnych zdarzeń,
- `ControllerSession` — stan połączenia, cykl odpytywania, kolejka komend, wygasająca autoryzacja i ponawianie,
- `ControllerApi`, `BleRemoteApi` i `connectivity/` — izolacja transportu REST, BLE oraz symulatora,
- `status_decoder.dart` — bezpieczne parsowanie, walidacja typów i limitów odpowiedzi,
- `design_system.dart` — tokeny kolorów, odstępów, promieni i motywy Material 3,
- `app_update/` — wykrywanie, pobieranie, weryfikacja i przekazanie APK do instalatora Androida.

Widoki nie wykonują bezpośrednich żądań HTTP. Aktualizują wyłącznie potrzebny stan, a zasoby sesji, timery i transporty są zamykane wraz z cyklem życia ekranu.

## Uruchomienie i build

```powershell
cd mobile_app
flutter pub get

flutter run --flavor current --target lib/main.dart
flutter run --flavor full --target lib/main_full.dart
flutter run --flavor dev --target lib/main_dev.dart
```

Weryfikacja i produkcyjny APK:

```powershell
flutter analyze
flutter test
flutter build apk --release --flavor current --target lib/main.dart
```

Wynik powstaje w `build/app/outputs/flutter-apk/app-current-release.apk`. Wydanie 4.0.0 jest publikowane jako `AquaCYD-Control-4.0.0-current.apk`.

Build release wymaga pliku `android/key.properties` z niepustymi polami:

```properties
storePassword=haslo_magazynu
keyPassword=haslo_klucza
keyAlias=aquacyd
storeFile=C:/bezpieczna/sciezka/aquacyd-release.jks
```

Plik JKS, hasła i `key.properties` muszą pozostać poza repozytorium. Gradle przerywa build release, jeżeli konfiguracja podpisu jest niekompletna.

## Aktualizacje aplikacji

Kanał aktualizacji działa wyłącznie w produkcyjnym buildzie release wariantu `current`. Aplikacja sprawdza stabilne GitHub Releases po uruchomieniu, po powrocie na pierwszy plan oraz nie częściej niż raz na 12 godzin. Użytkownik może także uruchomić sprawdzenie ręcznie, odłożyć aktualizację o 24 godziny albo pominąć wskazaną wersję.

Kontrakt publikacji:

- tag `mobile-vX.Y.Z`,
- dokładnie jeden asset `AquaCYD-Control-X.Y.Z-current.apk`,
- wydanie nie może być draftem ani prerelease,
- GitHub musi udostępnić digest `sha256`,
- `versionCode` musi być wyższy od zainstalowanego,
- APK musi używać identyfikatora `pl.cydakwarium.cyd_aquarium_mobile` i tego samego certyfikatu podpisu.

Przed uruchomieniem instalatora Androida aplikacja sprawdza rozmiar, SHA‑256, nazwę pakietu, wersję i certyfikat. Pierwsza instalacja wymaga zgody „Zezwalaj z tego źródła”, a każda aktualizacja kończy się systemowym potwierdzeniem użytkownika. Aplikacja nie omija zabezpieczeń Androida i nie aktualizuje się bez zgody.

## Ograniczenia zależne od firmware

- telemetria jest odpytywana okresowo; bieżący firmware nie zapewnia WebSocket, SSE ani powiadomień push działających w tle,
- wymuszenie `WŁ.` lub `WYŁ.` dla wyjścia zapisuje odpowiedni tryb harmonogramu; nie jest to czasowy override z automatycznym wygaśnięciem,
- fizyczny stan grzałki nie oznacza włączenia lub wyłączenia termostatu — aplikacja pokazuje te informacje oddzielnie,
- zapis konfiguracji sieci wymaga ponownego podania hasła, ponieważ bieżący endpoint firmware zastępuje komplet ustawień Wi‑Fi,
- bieżący firmware zapisuje profil przekaźników, ale go nie odczytuje ani nie stosuje; aplikacja blokuje edytor do czasu dodania odczytu, walidacji, atomowego zastosowania i rollbacku po stronie sterownika,
- historia, archiwa SD, OTA i zaawansowana diagnostyka nie są dostępne przez starszy protokół BLE v1.

Rozszerzenie tych funkcji wymaga jednoczesnej zmiany firmware i kontraktu komunikacyjnego aplikacji.
