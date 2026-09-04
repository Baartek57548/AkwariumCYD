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
