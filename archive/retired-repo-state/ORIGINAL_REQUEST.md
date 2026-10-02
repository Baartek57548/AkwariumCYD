# Original User Request

## Initial Request — 2026-09-04T09:52:17Z

Kompleksowy audyt logiki, stabilności oraz optymalizacji zużycia pamięci RAM dla kontrolera akwarium ESP32-CYD, eliminacja błędów i niespójności w firmware, obsłudze karty SD oraz interfejsie Web/API.

Working directory: c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium
Integrity mode: development

## Requirements

### R1. Firmware Logic & Stability Audit
Przeprowadzenie dogłębnego audytu logiki firmware ESP32 (odczyty czujników, sterowanie przekaźnikami/PWM, harmonogramy, tryby pracy, komunikacja BLE/ESP-NOW, obsługa błędów). Zidentyfikowanie i usunięcie wszelkich błędów logicznych, niespójności stanów, wyścigów (race conditions) oraz nieobsłużonych sytuacji wyjątkowych.

### R2. RAM & Heap Memory Optimization
Szczegółowa analiza alokacji pamięci dynamicznej, buforów ekranu LVGL, zadań FreeRTOS oraz buforów I/O karty SD pod kątem ograniczeń pamięci SRAM układu ESP32-CYD (brak zewnętrznej pamięci PSRAM). Wyeliminowanie wycieków pamięci (memory leaks), fragmentacji sterty (heap fragmentation) i ryzyka przepełnienia stosu (stack overflow/OOM).

### R3. SD Card & Web/API Subsystem Consistency
Audyt podsystemu karty SD i obsługi serwera WWW/API pod kątem spójności stanów, poprawnego serwowania zasobów ze struktury SD, obsługi błędów wejścia/wyjścia (brak karty, błąd odczytu, zajętość szyny SPI) oraz bezbłędnej synchronizacji danych z aplikacją webową.

## Verification Resources
- Środowiska budowania PlatformIO w `firmware/cyd_controller/platformio.ini` (`esp32dev`, `esp32dev-dev`, `esp32dev-espnow`, `esp32dev-st7789`, `native`).
- Zestaw natywnych testów jednostkowych Unity w `firmware/cyd_controller/test/test_native_domain` (`pio test -e native`).
- Skrypty weryfikacji i budowania assetów webowych w `package.json` (`npm run test:api`, `npm run build:web-assets`).

## Acceptance Criteria

### Compilation & Build Integrity
- [ ] Firmware kompiluje się bezbłędnie pod docelowe środowisko PlatformIO (`pio run -e esp32dev` w katalogu `firmware/cyd_controller`).
- [ ] Wszystkie natywne testy jednostkowe przechodzą pomyślnie (`pio test -e native` w `firmware/cyd_controller`).

### Memory & Stability Guardrails
- [ ] Zidentyfikowano i zlikwidowano niekontrolowane alokacje dynamiczne na stercie w pętlach głównych i zadaniach krytycznych.
- [ ] Rozmiary stosów zadań FreeRTOS oraz bufory graficzne LVGL mieszczą się w bezpiecznym limicie pamięci wewnętrznej ESP32 bez ryzyka kolizji i awarii OOM.

### Logic & Subsystem Correctness
- [ ] Wszystkie wykryte niespójności w maszynach stanów, logice automatyki, harmonogramach i obsłudze czujników/przekaźników zostały skorygowane i zachowują poprawną semantykę.
- [ ] Błędy odczytu/zapisu karty SD oraz brakujące pliki webowe są obsługiwane bezpiecznie bez restartu kontrolera (hang/crash).

## Follow-up — 2026-09-18T17:21:02Z

Wdrożenie kompleksowego i spójnego systemu bezprzewodowej aktualizacji oprogramowania (OTA) w trybach STA i fallback AP dla dwuprocesorowego sterownika akwarium (WeMos D1 Uno ESP8266 w AquaBox oraz panelu dotykowego ESP32 CYD), z obsługą wgrywania przez przeglądarkę WWW (Web OTA) oraz bezpośrednio z PlatformIO (ArduinoOTA), eliminującego konieczność rozkręcania obudowy i manipulowania zworkami UART.

Working directory: c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium
Integrity mode: development

## Requirements

### R1. Tryb Serwisowy OTA dla WeMos D1 Uno (ESP8266 w AquaBox)
- Normalny tryb pracy WeMos pozostaje w 100% autonomiczny z całkowicie wyłączonym Wi-Fi (WiFi.mode(WIFI_OFF)).
- Wejście w tryb serwisowy OTA wyzwalane jest na dwa sposoby:
  1. Zdalnie: Poprzez komendę z magistrali UART wysłaną z ekranu dotykowego CYD lub panelu WWW.
  2. Sprzętowo: Poprzez przytrzymanie fizycznego przycisku monostabilnego podłączonego do wolnego pinu WeMos podczas startu zasilania (jako awaryjny fallback, gdyby panel CYD uległ awarii).
- W trybie serwisowym WeMos:
  - Bezpiecznie wyłącza wszystkie przekaźniki grzałki, CO2 i zasilania (stan fail-safe).
  - Włącza Wi-Fi: najpierw próbuje połączyć się z domową siecią (STA), a w razie braku sieci lub błędu uruchamia własny punkt dostępowy AP AquaBox-OTA.
  - Uruchamia serwer Web OTA z prostym formularzem HTTP do wgrania pliku .bin w przeglądarce oraz usługę ArduinoOTA dla bezpośredniego wgrywania z PlatformIO.
  - Posiada automatyczny watchdog powrotu: jeśli przez 5 minut nikt nie wgra pliku, WeMos restartuje się do normalnego trybu pracy.

### R2. Aktualizacja OTA dla ESP32 CYD (Panel i Bramka)
- Obsługa wgrywania firmware z poziomu panelu Web OTA (formularz w przeglądarce) oraz przez ArduinoOTA.
- Dedykowany ekran / kafelek serwisowy w interfejsie LVGL na wyświetlaczu dotykowym:
  - Przycisk "Aktualizuj Panel (CYD)" — przełącza CYD w dedykowany tryb OTA / restartuje z flagą OTA.
  - Przycisk "Aktualizuj AquaBox (WeMos)" — wysyła komendę UART do WeMos nakazującą podniesienie sieci OTA i wyświetla na ekranie CYD instrukcję: nazwę sieci Wi-Fi i adres IP WeMos do wpisania w przeglądarce.
- Automatyczny fallback do trybu Access Point AquaCYD-OTA, jeśli sieć domowa jest niedostępna.

### R3. Rozszerzenie Protokołu UART NDJSON
- Dodanie do współdzielonej biblioteki uart_protocol:
  - Komendy enter_ota (CYD -> WeMos) z potwierdzeniem ACK.
  - Flagi w telemetrii informującej o wejściu w tryb serwisowy / gotowości do flashowania.
  - Zabezpieczenie przed jednoczesnym wysyłaniem telemetrii na UART w trakcie flashowania (aby nie generować zakłóceń).

### R4. Bezpieczeństwo Sprzętowe i Integralność
- Przed rozpoczęciem wgrywania firmware do któregokolwiek mikrokontrolera wszystkie obciążenia (przekaźnik grzałki, elektrozawór CO2) są bezzwłocznie odcinane.
- Zachowanie konfiguracji w pamięci EEPROM (nieuleganie skasowaniu przy aktualizacji programu).
- Automatyczny restart i weryfikacja poprawności po zakończeniu flashowania.

## Acceptance Criteria

### Weryfikacja programowa i kompilacji
- [ ] Środowisko wemos_d1_uno kompiluje się w 100% bez błędów (pio run -e wemos_d1_uno).
- [ ] Środowisko cyd_2_8 kompiluje się w 100% bez błędów (pio run -e cyd_2_8).
- [ ] WeMos w normalnym trybie pracy ma Wi-Fi wyłączone (zero zużycia pamięci i czasu CPU na stos RF).
- [ ] Po wyzwoleniu trybu OTA (przycisk lub komenda UART) WeMos poprawnie inicjalizuje ArduinoOTA i serwer Web OTA.
- [ ] Interfejs CYD zawiera kontrolki inicjalizacji OTA dla obu układów oraz wyświetla dane dostępowe IP.
- [ ] Procedura aktualizacji zostaje zaktualizowana w dokumentacji bazy wiedzy Obsidian w obsidian/Procedura - Programowanie i Wgrywanie Firmware.md.
