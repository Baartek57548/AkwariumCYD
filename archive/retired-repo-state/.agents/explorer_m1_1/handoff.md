# Raport Przekazania (Handoff): Rozszerzenia Protokołu UART NDJSON (M1)

**Autor:** explorer_m1_1  
**Odbiorca:** parent / implementer (Milestone 1)  
**Ścieżka:** `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_m1_1\handoff.md`  
**Typ handoffu:** Hard (Zadanie analizy protokołu zakończone w 100%)

---

## 1. Observation (Obserwacje Bezpośrednie)

1. **Struktura Plików i Zależności Protokołu**:
   - `firmware/shared/uart_protocol/include/uart_protocol.h`: deklaruje `enum class CommandType : uint8_t` (linie 40–51), `struct AquariumTelemetry` (linie 63–77), `struct AquariumCommand` (linie 80–93), `struct AquariumAck` (linie 96–100).
   - `firmware/shared/uart_protocol/src/uart_protocol.cpp`: implementuje `serializeTelemetry` (StaticJsonDocument<512>), `serializeCommand` (StaticJsonDocument<384>), `serializeAck` (StaticJsonDocument<256>), `parseMessage` (StaticJsonDocument<768>).
   - `firmware/shared/uart_protocol/library.json`: deklaruje bibliotekę PlatformIO `"bblanchon/ArduinoJson": "^6.21.4"` (kompiluje się z wersją 6.21.6).

2. **Konsumenci i Użycie Struktur**:
   - `firmware/wemos_controller/src/main.cpp`:
     - Linia 26: `AquaProtocol::AquariumTelemetry t;` alokowana na stosie w `send_telemetry()`.
     - Linia 63: `AquaProtocol::AquariumMessage msg;` alokowana na stosie w `process_serial_line()`.
     - Linia 78: `switch (msg.command.type)` z gałęzią `default:` odsyłającą `{"type":"ack","cmd":"unknown","ok":false}`.
   - `firmware/cyd_controller/src/uart_client.cpp`:
     - Linia 13: `memset(&latest_telemetry_, 0, sizeof(latest_telemetry_));` w konstruktorze.
     - Linia 38: `AquaProtocol::AquariumMessage msg;` alokowana na stosie w `processLine()`.
     - Linia 101: `bool UartClient::sendCommand(const AquaProtocol::AquariumCommand &cmd)`.

3. **Wymiary Buforów Sieciowych Wi-Fi w Projekcie**:
   - `firmware/cyd_controller/include/wifi_credential_store.h`:
     - Linia 6: `constexpr size_t WIFI_CREDENTIAL_SSID_BYTES = 33U;`
     - Linia 7: `constexpr size_t WIFI_CREDENTIAL_PASSWORD_BYTES = 65U;`

4. **Weryfikacja Kompilacji Środowisk Bazowych**:
   - `pio run -e wemos_d1_uno`: SUCCESS (RAM: 36.5%, Flash: 27.9%, czas: 9.63 s).
   - `pio run -e cyd_2_8`: SUCCESS (RAM: 27.0%, Flash: 83.6%, czas: 44.97 s).
   - `pio test -e native` (w katalogu `firmware/cyd_controller`): SUCCESS (45 z 45 testów przeszło, czas: 2.49 s).

---

## 2. Logic Chain (Łańcuch Dedukcji i Logiki)

1. **Wartość i Pozycja `CommandType::EnterOta`**:
   - Dotychczasowy enum `CommandType` posiada 10 elementów: `Unknown = 0`, `SetRelay = 1`, ..., `SetSafeMode = 9`.
   - Umieszczenie `EnterOta` na końcu listy nadaje mu wartość `10` (`0x0A`), zachowując bazowy typ `uint8_t` (1 bajt).
   - Wartości numeryczne dla wszystkich istniejących komend pozostają niezmienne, eliminując ryzyko błędnej interpretacji przy asynchronicznej wymianie oprogramowania.

2. **Pola `ota_ssid` i `ota_pass` w `AquariumCommand`**:
   - Standard 802.11 definiuje maksymalną długość SSID na 32 bajty. Dodając `\0`, minimalny bezpieczny bufor to 33 bajty (`char ota_ssid[33]`).
   - Standard WPA2-PSK definiuje hasło do 63 znaków ASCII lub 64 znaków HEX. Dodając `\0`, minimalny bezpieczny bufor to 65 bajtów (`char ota_pass[65]`).
   - Te wymiary idealnie pokrywają się z istniejącym modułem `wifi_credential_store.h`.
   - Rozmiar `AquariumCommand` rośnie z 24 bajtów do 124 bajtów (po wyrównaniu do granicy 4 bajtów). W strukturze `AquariumMessage` na stosie mieści się to bez problemu na ESP8266 i ESP32.

3. **Pola `ota_mode` i `ota_ip` w `AquariumTelemetry`**:
   - Flaga `bool ota_mode` informuje panel CYD o gotowości WeMos do flashowania i nakazuje wyciszenie alarmu timeoutu offline.
   - Adres IPv4 (np. `192.168.1.150` lub `192.168.4.1`) ma maksymalnie 15 znaków. Bufor `char ota_ip[16]` gwarantuje pełną reprezentację z terminatorem `\0`.
   - Rozmiar `AquariumTelemetry` rośnie z 36 do 56 bajtów.

4. **Pojemność Dokumentów JSON w ArduinoJson 6**:
   - **`serializeCommand()`**: `StaticJsonDocument<384>`:
     - 4 elementy (`type`, `cmd`, `ssid`, `pass`) -> `JSON_OBJECT_SIZE(4)` = 64 B.
     - Ciągi znaków: literały = 0 B, `ssid` (33 B) + `pass` (65 B) = 98 B.
     - Zużycie max: 168 B na 384 B dostępne -> **216 B wolnego (56%)**.
     - Bufor wyjściowy `String`: maksymalna ramka to 147 znaków (lub do ~180 przy ucieczkach znaków). Zalecane podbicie `output.reserve(160)` do `output.reserve(256)` całkowicie eliminuje reallokacje na stercie.
   - **`serializeTelemetry()`**: `StaticJsonDocument<512>`:
     - 16 elementów -> `JSON_OBJECT_SIZE(16)` = 256 B.
     - Kopiowanie `ota_ip` -> 16 B.
     - Zużycie max: 272 B na 512 B dostępne -> **240 B wolnego (47%)**.
     - Bufor wyjściowy `String`: ramka z polami OTA ma 231 znaków. Podbicie `output.reserve(256)` do `output.reserve(320)` zabezpiecza przed reallokacją.
   - **`parseMessage()`**: `StaticJsonDocument<768>`:
     - Parsowanie telemetrii (16 par): 256 B sloty + 97 B klucze + 26 B wartości = 379 B. Margines: **389 B wolnego (>50%)**.
     - Parsowanie komendy `enter_ota`: 64 B sloty + 19 B klucze + 108 B wartości = 191 B. Margines: **577 B wolnego (>75%)**.
     - Żadne powiększanie `StaticJsonDocument` nie jest wymagane.

5. **Odporność Parsowania (Dual-Key Fallback)**:
   - W `parseMessage()` dla telemetrii:
     `out_msg.telemetry.ota_mode = doc.containsKey("ota_mode") ? (doc["ota_mode"] | false) : (doc["ota"] | false);`
     akceptuje zarówno klucz `"ota_mode"`, jak i `"ota"`.
   - W `parseMessage()` dla komendy:
     `memset(&out_msg.command, 0, sizeof(out_msg.command));` przed deserializacją oraz `strncpy(..., sizeof(...) - 1)` gwarantują, że bufory są zawsze poprawnie zakończone `\0`.
   - W `parseMessage()` dla ACK:
     `out_msg.ack.ok = doc.containsKey("ok") ? (doc["ok"] | false) : (strcmp(doc["status"] | "", "ok") == 0);`
     akceptuje zarówno `"ok":true`, jak i `"status":"ok"`.

---

## 3. Caveats (Zastrzeżenia i Założenia)

1. **Brak Modyfikacji Kodu Firmware w Tej Fazie**:
   Zgodnie z ograniczeniem roli (Read-only Explorer), niniejszy agent nie modyfikował plików w `firmware/`. Wszystkie zmiany zostały zawarte w raportach jako gotowe instrukcje i patche dla agenta implementującego (Coder M1).
2. **Kolejność Wywołania Serializacji w WeMos**:
   Po dodaniu pól `ota_mode` i `ota_ip` do `AquariumTelemetry`, w `wemos_controller/src/main.cpp` funkcja `send_telemetry()` w Milestone 2 musi jawnie ustawiać `t.ota_mode = ota_active;` i kopiować adres IP, aby uniknąć wysyłania przypadkowych danych ze stosu.
3. **Uruchamianie Testów Natywnych Unity**:
   Środowisko `[env:native]` wymaga uruchamiania z katalogu roboczego `firmware/cyd_controller` (`pio test -e native`), ze względu na konfigurację ścieżek `extra_scripts` i `lib_extra_dirs` w `firmware/cyd_controller/platformio.ini`.

---

## 4. Conclusion (Konkluzja i Zalecenia)

1. Modyfikacje w `firmware/shared/uart_protocol/include/uart_protocol.h`:
   - Dodać `EnterOta` do `enum class CommandType : uint8_t`.
   - Dodać `bool ota_mode;` i `char ota_ip[16];` do `struct AquariumTelemetry`.
   - Dodać `char ota_ssid[33];` i `char ota_pass[65];` do `struct AquariumCommand`.
2. Modyfikacje w `firmware/shared/uart_protocol/src/uart_protocol.cpp`:
   - W `serializeCommand()`: dodać `case CommandType::EnterOta`, zwiększyć `output.reserve(256)`.
   - W `serializeTelemetry()`: dodać serializację `ota_mode` oraz warunkową `ota_ip`, zwiększyć `output.reserve(320)`.
   - W `parseMessage()`: dodać parsowanie `"enter_ota"` z `strncpy`, dodać parsowanie `ota_mode`/`ota_ip` z fallbackiem, dodać tolerancję `"status":"ok"` dla ACK.
3. Kompletny raport ze szczegółowymi diffami został zapisany w:
   `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_m1_1\report.md`.

---

## 5. Verification Method (Metoda Niezależnej Weryfikacji)

1. **Weryfikacja Plików Raportu:**
   - Sprawdzić `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_m1_1\report.md` pod kątem kompletności kodu i diffów.
2. **Kompilacja i Testy (po zaaplikowaniu zmian przez Codera):**
   - WeMos D1 Uno:
     `pio run -e wemos_d1_uno` (musi zakończyć się kodem wyjścia 0)
   - CYD 2.8":
     `pio run -e cyd_2_8` (musi zakończyć się kodem wyjścia 0)
   - Natywne testy jednostkowe:
     W katalogu `firmware/cyd_controller`: `pio test -e native` (wszystkie 45 testów musi przejść pomyślnie)
3. **Warunki Unieważnienia (Invalidation Conditions):**
   - Zwiększenie rozmiaru buforów `StaticJsonDocument` bez uzasadnienia (jest zbędne i zwiększa zużycie stosu).
   - Zmiana dotychczasowych wartości numerycznych `CommandType` (0..9).
   - Użycie buforów o rozmiarach mniejszych niż 33 dla SSID i 65 dla hasła.
