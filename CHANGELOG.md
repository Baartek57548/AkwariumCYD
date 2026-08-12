# Changelog

## Unreleased — Home Control 2.0 i monorepo produktowe

### What is included

- Natywna aplikacja Flutter **Home Control** dla Androida, iOS i web z własnym
  Material 3 UI; Home Assistant jest adapterem danych, a nie stroną w WebView.
- Trzy źródła: AquaHub, wiele instancji Home Assistant i pełne Demo offline,
  wspólny model pomieszczeń, urządzeń, 28 typów encji, historii i aktualizacji.
- Bezpieczne profile HA, oficjalne REST/WebSocket oraz rejestry areas, devices,
  entities i services, reconnect, snapshot cache i selektywne usuwanie danych.
- Poprawne natywne sterowanie m.in. klimatem, roletami, zamkami, alarmem,
  odkurzaczem, scenami, skryptami, liczbami, listami i tekstem z ACK/rollbackiem.
- Autoaktualizacja Home Control przy wejściu i wznowieniu, osobny asset
  `Home-Control-X.Y.Z.apk`, hash, certyfikat, manifest, SBOM i provenance.
- AquaCYD Service zachowany jako osobny produkt serwisowy bez regresji funkcji.
- Monorepo podzielone na `apps`, `firmware`, `packages`, `services` i
  `integrations` z kanoniczną dokumentacją architektury, bezpieczeństwa, OTA,
  QA, HIL i release.
- Bramka ESP32-C6 publikuje dodatkowo tryb grzałki i szesnaście pól czterech
  harmonogramów potrzebnych aplikacji i panelom operatorskim.
- Osobny, chroniony pipeline `home-vX.Y.Z` podpisuje Home Control, waliduje
  package i certyfikat oraz publikuje APK, SHA-256, manifest, SBOM i atestację
  pochodzenia bez zastępowania wydania AquaCYD Control.

- Profesjonalny, natywny panel LVGL 9 dla Waveshare 7B z sześcioma ekranami,
  animowanym startem, stanami offline/stale, alarmami, modalami i pełnym ACK.
- Edycja harmonogramów obu lamp, filtra i napowietrzania, profili Aquael oraz
  trybu, nastawy i histerezy termostatu.
- Telemetria ESP-NOW v2 z kompatybilnym odczytem v1, atomowy zapis w CYD,
  kontrola rewizji, walidacja zakresów i idempotencja.
- Trzynaście edytowalnych ramek SVG do importu w Figma, manifest prototypu,
  tokeny i specyfikacja animacji.
- Plan instalacji Home Assistant OS na Raspberry Pi 5 4 GB, lokalny Mosquitto,
  ACL, dashboard oraz skrypty HA używające tego samego kontraktu konfiguracji.
- Odświeżony interfejs 320×240 na ESP32-CYD: wspólna paleta z P4, płaskie karty,
  czytelne statusy urządzeń, alarm w pasku kondycji oraz sześć edytowalnych
  ramek SVG przedstawiających wszystkie główne strony i stan alarmowy.

### Verification

- Aktualne wyniki pełnej macierzy i jawne blokery właściciela są utrzymywane w
  `docs/QA_REPORT.md`.
- Produkcyjny tag pozostaje zablokowany do czasu fizycznego HIL bez skipów i
  użycia kluczy podpisu właściciela.

## mobile-v6.0.0 / firmware-v6.0.0 - 2026-07-29

Produkcyjne wydanie centrum sterowania AquaCYD `6.0.0`.

### What is included

- Aplikacja działa także bez połączenia ze sterownikiem: pokazuje ostatni
  zsynchronizowany stan, przechowuje zaszyfrowane szkice zmian i bezpiecznie
  rozwiązuje konflikty po ponownym połączeniu.
- Automatyczne wykrywanie urządzenia w sieci lokalnej, ręczne połączenie BLE oraz
  zdalne alarmy przez opcjonalną bramę HTTPS z podpisem HMAC i integracją FCM.
- Produkcyjne powiadomienia Android z akcjami, przekierowaniem do właściwego
  alarmu, diagnostyką uprawnień i kanałów oraz bezpiecznym sprawdzaniem aktualizacji
  w tle. Instalacja APK nadal wymaga jawnej zgody użytkownika.
- Firmware CYD otrzymał watchdog, trwałą historię resetów, alarmy braku i
  niestabilności czujników, fail-safe przekaźników, filtrację alarmów oraz kolejkę
  zdarzeń odporną na restarty.
- Kalibracja pH/EC jest atomowo zapisywana i dostępna przez kreator na ekranie,
  BLE i chronione API. Dane Wi-Fi zostały przeniesione z karty SD do NVS.
- Panel WWW otrzymał opcjonalny, instalowalny tryb PWA wyłącznie przez zaufaną
  bramę HTTPS, testy dostępności i zgodności przeglądarek oraz atomowy pakiet
  instalacyjny z manifestem, podpisem, rollbackiem i SBOM.
- Pipeline wydania wymusza testy API, Fluttera, Androida, obu wariantów firmware,
  panelu WWW, podpisów i pochodzenia artefaktów. Wydanie firmware wymaga zielonego
  testu HIL na fizycznym stanowisku.

### Verification

- Pełna macierz testów i kompilacji jest wykonywana przed utworzeniem tagów
  `mobile-v6.0.0` i `firmware-v6.0.0`.
- APK jest publikowany jako zasób GitHub Release wraz z sumą SHA-256; plik
  wykonywalny nie jest przechowywany bezpośrednio w historii Git.

## mobile-v5.1.1 - 2026-07-29

Poprawka produkcyjnych powiadomień systemowych aplikacji AquaCYD `5.1.1`.

### What is included

- Ikona `ic_notification` jest jawnie zachowywana podczas optymalizacji zasobów,
  dzięki czemu inicjalizacja kanałów Androida nie kończy się już błędem
  `invalid_icon`.
- Centrum alarmów potrafi wysłać powiadomienie testowe, wykrywa cofniętą zgodę
  lub wyłączony kanał i prowadzi bezpośrednio do ustawień aplikacji.
- Nieudana dostawa jest widoczna w interfejsie zamiast zostać ukryta przez
  warstwę synchronizacji.
- CI i pipeline wydania sprawdzają gotowy APK przez `aapt`, aby brak wymaganej
  ikony nie mógł ponownie trafić do GitHub Releases.

### Verification

- `flutter analyze` zakończone bez problemów.
- Testy centrum alarmów, zgód i powiadomienia diagnostycznego zakończone
  powodzeniem.
- Produkcyjny asset Androida: `AquaCYD-Control-5.1.1-current.apk`.

## mobile-v5.1.0 / firmware-v5.1.0 - 2026-07-28

Produkcyjne wydanie bezpieczeństwa aplikacji i firmware AquaCYD `5.1.0`.

### What is included

- BLE LE Secure Connections z bondingiem, MITM, 128-bitowym kluczem i kodem
  parowania wyświetlanym na CYD.
- Podpisane pakiety OTA `.aqfw` z RSA-3072/PSS, Secure Boot v2, walidacją targetu,
  integralności i ochroną anti-downgrade.
- Krótkotrwała sesja administracyjna przekazywana w nagłówku zamiast PIN-u w URL.
- Automatyczny rollback, jeśli po aktualizacji nie zostaną potwierdzone zdrowe
  zadania sterownika i gotowy interfejs.
- APK `5.1.0+18` z walidacją pakietu przed wysłaniem i natywnym bondingiem
  Android.
- Automatyczne wykrywanie właściwego wariantu firmware na GitHubie, komunikat
  w aplikacji oraz instalacja dopiero po świadomej zgodzie użytkownika.
- Unikalny PIN i hasło serwisowego AP generowane osobno na każdym produkcyjnym
  CYD; webowe komendy przesyłają wyłącznie krótkotrwały token sesji.

### Notes

- Pipeline nie przepala eFuse. Secure Boot v2 i Flash Encryption wymagają
  kontrolowanego provisioningu fizycznego urządzenia ESP32 ECO3+.

## mobile-v3.7.0 - 2026-07-25

Produkcyjne wydanie aplikacji AquaCYD Control `3.7.0+13`.

### What is included

- Material 3 UI przystosowane do ekranów 320 px i systemowej skali tekstu 300%.
- Pasek kondycji sterownika: Online/Offline/Connecting, RSSI, ping i wiek danych.
- Automatyczny reconnect z backoffem oraz zatrzymywanie odpytywania w tle.
- Serializacja komend REST/BLE, odporne parsowanie statusu i limity odpowiedzi.
- Bezpieczny cykl życia BLE, batchowanie skanowania i obsługa uszkodzonych ramek.
- Przewijalny, dostępny interfejs aktualizacji APK po zgodzie użytkownika.

### Verification

- `flutter analyze` zakończone bez problemów.
- 86 testów Flutter zakończonych powodzeniem.
- Podpisany wariant `current` zweryfikowany jako `3.7.0` (`versionCode 13`).

## v20260626release - 2026-06-26

Release panelu web, OTA i firmware po reorganizacji sekcji oraz stabilizacji
lokalnego srodowiska PlatformIO.

### What is included

- Reorganized web panel sections for relays, module automation and system settings.
- Updated OTA SD package with cache-busted assets `20260626release`.
- Factory schedule profile and Aquael DAY / DAYBREAK / NIGHT UI support.
- Improved web status, settings layout and OTA upload flow.
- PlatformIO pinned to the official `platformio/espressif32@7.0.1` platform.
- VS Code terminal configured for UTF-8 Python output to avoid Windows `cp1250` crashes.

### Verification

- Firmware build passed with `pio run --environment esp32dev`.
- JavaScript syntax checks passed for active web and OTA scripts.
- VS Code IntelliSense include paths regenerated with no missing paths.

## v1.0.0 - 2026-06-10

Stable UI/UX release for the CYD Aquarium firmware.

### What is included

- Responsive LVGL interface with Polish language assets.
- Event-driven separation between UI and control logic.
- Modular hardware, sensor, controller, and display layers.
- Lighting synchronization for Aquael lamps.
- Heater regulation with hysteresis and fail-safe handling.
- OTA and diagnostics panels.
- Circular history buffers for charts and telemetry.
- PIN protection for critical actions.
- Service mode with automatic timeout.
- Graceful degradation when optional hardware is absent.

### Verification

- Firmware build passed with `py -3.13 -m platformio run -e esp32dev`.

### Notes

- This release is marked as stable based on the current repository state and the successful firmware build.
- No changes were made to the existing UI flow beyond the warning cleanup in `events.cpp`.
