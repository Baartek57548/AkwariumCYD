# Handoff Report — explorer_survey_1

## 1. Observation
1. **Środowisko PlatformIO**:
   - `platformio.ini` (linie 85–105): zdefiniowano środowisko `[wemos_firmware]` rozszerzane przez `[env:wemos_d1_uno]`. Platforma: `espressif8266`, płytka: `d1_mini`, framework: `arduino`, ldscript: `eagle.flash.4m2m.ld`. Źródła: `build_src_filter = -<*> +<wemos_controller/src/*>`. Dołączana biblioteka współdzielona: `firmware/shared/uart_protocol`.
   - Wynik wykonania polecenia `pio run -e wemos_d1_uno`:
     ```
     Processing wemos_d1_uno (platform: espressif8266; board: d1_mini; framework: arduino)
     RAM:   [====      ]  36.5% (used 29924 bytes from 81920 bytes)
     Flash: [===       ]  27.9% (used 291831 bytes from 1044464 bytes)
     ======================== [SUCCESS] Took 184.23 seconds ========================
     ```
2. **Zachowanie Wi-Fi w trybie normalnym**:
   - `firmware/wemos_controller/src/main.cpp` (linie 162–163):
     ```cpp
     WiFi.mode(WIFI_OFF);
     WiFi.forceSleepBegin();
     ```
   - Moduł radiowy RF jest całkowicie usypiany, co potwierdza notatka `obsidian/Hardware - WeMos D1 Uno ESP8266.md` (linie 38–43): pobór prądu spada do 15–20 mA, brak zakłóceń dla przetwornika ADC/pH, 100% czasu procesora dla sterowania.
3. **Przekaźniki i Fail-Safe**:
   - `firmware/wemos_controller/include/wemos_config.h` (linie 18, 22–32): przekaźniki podłączone są do zewnętrznego ekspandera I2C MCP23017 (`ADDR_MCP23017 = 0x20`) Port A, `RELAY_ACTIVE_LOW = true`. Kanały: 0..1 oświetlenie, 2 filtr, 3 napowietrzacz, 4 grzałka, 5 CO2, 6 karmnik, 7 zapasowy.
   - `firmware/wemos_controller/src/hardware_i2c.cpp` (linie 143–155): metoda `setAllRelays(uint8_t bitmask)` przy przekazaniu `0x00` ustawia `physical_shadow_a_ = 0xFF`, co wymusza stan HIGH na wszystkich wyjściach MCP23017 i natychmiastowo wyłącza wszystkie przekaźniki.
4. **Wyprowadzenia WeMos D1 Uno i wolne piny**:
   - `firmware/wemos_controller/include/wemos_config.h` (linie 10–15): zajęte są SCL (GPIO5 / D1), SDA (GPIO4 / D2), 1-Wire (GPIO14 / D5), LED (GPIO2 / D4), TX (GPIO1), RX (GPIO3).
   - Analiza strappingu ESP8266: GPIO0 (D3) to pin wyboru trybu bootloadera ROM UART (zwarcie do GND blokuje start sketchu); GPIO15 (D8) musi być LOW przy starcie; GPIO2 (D4) musi być HIGH przy starcie i ma wbudowany LED.
   - Piny GPIO12 (D6) oraz GPIO13 (D7) są całkowicie wolne, nie wpływają na sekwencję rozruchu ESP8266 i posiadają wewnętrzne podciąganie `INPUT_PULLUP`.
5. **Biblioteki OTA w frameworku**:
   - Framework `framework-arduinoespressif8266` zawiera wbudowane biblioteki:
     - `ArduinoOTA/ArduinoOTA.h`
     - `ESP8266WebServer/src/ESP8266WebServer.h`
     - `ESP8266HTTPUpdateServer/src/ESP8266HTTPUpdateServer.h`
   - Nie są wymagane żadne zewnętrzne biblioteki w `lib_deps`.
6. **Emulacja EEPROM**:
   - `firmware/wemos_controller/src/storage_manager.cpp`: konfiguracja zapisywana jest przez `EEPROM.put(0, copy)` i `EEPROM.commit()`. W architekturze ESP8266 sektor EEPROM znajduje się poza 1 MB partycją programu i nie ulega wyczyszczeniu podczas flashowania OTA.

## 2. Logic Chain
1. Z obserwacji 1 wynika, że kompilacja `wemos_d1_uno` przebiega bezbłędnie, a rozmiar programu (291 KB) zajmuje poniżej 28% dostępnej przestrzeni 1044 KB, co gwarantuje pełną kompatybilność z dwubankowym mechanizmem aktualizacji OTA w ESP8266.
2. Z obserwacji 2 wynika, że wyłączenie Wi-Fi w pętli normalnej jest kluczowe dla stabilności i parametrów pomiarowych, dlatego radio RF powinno być wybudzane (`WiFi.forceSleepWake()`) wyłącznie po wykryciu żądania trybu serwisowego OTA.
3. Z obserwacji 3 wynika, że wywołanie `g_hardware_i2c.setAllRelays(0x00)` wraz z wstrzymaniem pętli termostatu i harmonogramów stanowi kompletny, sprzętowo poprawny mechanizm bezpiecznego odcięcia obciążeń (grzałka, CO2, zasilanie) przed rozpoczęciem procedury OTA.
4. Z obserwacji 4 wynika, że podłączenie monostabilnego przycisku do pinu GPIO12 (D6) z konfiguracją `INPUT_PULLUP` pozwala na bezpieczne wykrycie przytrzymania przycisku podczas podłączania zasilania bez zakłócania wejścia układu w tryb wykonywania kodu Flash (w odróżnieniu od GPIO0/GPIO15).
5. Z obserwacji 5 wynika, że wbudowany zestaw bibliotek ESP8266 pozwala na bezproblemową implementację serwera Web OTA (port 80) oraz ArduinoOTA (port 8266) bez powiększania zależności projektu.
6. Z obserwacji 6 wynika, że parametry kalibracji sondy pH, histerezy i harmonogramów przetrwają aktualizację OTA bez utraty danych.

## 3. Caveats
- Przycisk fallback na GPIO12 (D6) wymaga fizycznego wyprowadzenia na zewnątrz obudowy AquaBox lub podłączenia zworki na płytce WeMos. W kodzie detekcja wymaga programowego filtru drgań styków (debouncing min. 300–500 ms) przy rozruchu zasilania.
- Przy przejściu w tryb serwisowy WeMos musi wstrzymać regularne wysyłanie telemetrii na UART0, aby uniknąć kolizji przerwaniowych podczas zapisu bloków pamięci Flash przez serwery OTA.

## 4. Conclusion
Architektura i zasoby sprzętowe WeMos D1 Uno (AquaBox) w 100% wspierają zaplanowane wdrożenie dwutrybowego systemu OTA (STA + Fallback AP "AquaBox-OTA", Web OTA HTTP, ArduinoOTA, watchdog 300s, przycisk GPIO12 oraz odcięcie przekaźników). Środowisko kompiluje się bezbłędnie. Kompletny raport i plan wdrożenia został sporządzony w pliku:
`c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_survey_1\report.md`.

## 5. Verification Method
1. Weryfikacja czystej kompilacji PlatformIO:
   ```powershell
   pio run -e wemos_d1_uno
   ```
   Kryterium sukcesu: kod wyjścia 0, status `[SUCCESS]`, RAM < 50%, Flash < 50%.
2. Weryfikacja definicji i implementacji w kodzie:
   - Sprawdzenie braku konfliktów pinów w `firmware/wemos_controller/include/wemos_config.h`.
   - Sprawdzenie logiki `setAllRelays(0)` w `firmware/wemos_controller/src/hardware_i2c.cpp`.
   - Sprawdzenie nienaruszalności `WiFi.mode(WIFI_OFF)` w ścieżce normalnego startu `firmware/wemos_controller/src/main.cpp`.
