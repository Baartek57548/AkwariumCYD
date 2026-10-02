# Raport Techniczny: Analiza Rozszerzeń Protokołu UART NDJSON (Milestone 1)

**Autor:** explorer_m1_1  
**Data:** 2026-09-18  
**Projekt:** AquaCYD / AquaBox Dual-MCU Controller  
**Dotyczy:** Wymagane zmiany w `firmware/shared/uart_protocol/include/uart_protocol.h` oraz `firmware/shared/uart_protocol/src/uart_protocol.cpp` dla obsługi bezprzewodowej aktualizacji OTA (`EnterOta`, `ota_ssid`, `ota_pass`, `ota_mode`, `ota_ip`).

---

## 1. Podsumowanie Wykonawcze (Executive Summary)

W ramach Milestone 1 (M1) przeprowadzono szczegółową analizę implementacyjną biblioteki współdzielonej `uart_protocol` pod kątem obsługi bezprzewodowej aktualizacji oprogramowania (OTA) dla stacji wykonawczej WeMos D1 Uno (ESP8266) oraz panelu dotykowego ESP32 CYD (2.8").

### Główne Wnioski:
1. **`CommandType::EnterOta`**: Dodanie nowej wartości na końcu enumu `CommandType` (jawna wartość `10` / `0x0A`, bazowy typ `uint8_t`), co zachowuje 100% kompatybilności wstecznej dla wartości istniejących (0..9).
2. **`AquariumCommand`**: Rozszerzenie o pola `char ota_ssid[33]` i `char ota_pass[65]`. Rozmiary te są w 100% zgodne ze standardem IEEE 802.11 (SSID max 32 znaki + `\0`) oraz WPA2-PSK (hasło max 63/64 znaki + `\0`), a także z istniejącymi stałymi `WIFI_CREDENTIAL_SSID_BYTES` (33) i `WIFI_CREDENTIAL_PASSWORD_BYTES` (65) z `firmware/cyd_controller/include/wifi_credential_store.h`.
3. **`AquariumTelemetry`**: Rozszerzenie o `bool ota_mode` (flaga stanu serwisowego WeMos) oraz `char ota_ip[16]` (adres IPv4 w notacji kropkowej, max 15 znaków + `\0`).
4. **Pojemność Dokumentów ArduinoJson (Memory Footprint)**:
   - `StaticJsonDocument<384>` w `serializeCommand()`: Zużycie w najgorszym scenariuszu wynosi **168 bajtów**, co daje **216 bajtów marginesu (56% wolnej przestrzeni)**.
   - `StaticJsonDocument<512>` w `serializeTelemetry()`: Zużycie w najgorszym scenariuszu wynosi **272 bajty**, co daje **240 bajtów marginesu (47% wolnej przestrzeni)**.
   - `StaticJsonDocument<768>` w `parseMessage()`: Zużycie przy parsowaniu telemetrii wynosi **379 bajtów** (389 bajtów marginesu, >50%), a przy parsowaniu komendy `enter_ota` wynosi **191 bajtów** (577 bajtów marginesu, >75%).
   - **Wniosek:** Żaden bufor `StaticJsonDocument` nie wymaga powiększenia.
5. **Optymalizacja Alokacji Buforów Ciągów Znaków (`String::reserve`)**:
   - W `serializeCommand()` zaleca się zwiększenie `output.reserve(160)` do `output.reserve(256)` (lub `192`), aby zagwarantować zero reallokacji na stercie w przypadku długich haseł z ucieczkami JSON.
   - W `serializeTelemetry()` zaleca się zwiększenie `output.reserve(256)` do `output.reserve(320)`, gdyż pełna ramka telemetrii z polami OTA osiąga 231 znaków, co przy marginesie zapobiega reallokacjom sterty.
6. **Strategia Kluczy JSON i Kompatybilność**:
   - Parser `parseMessage()` zaimplementuje odporne parsowanie dwukluczowe: `out_msg.telemetry.ota_mode = doc["ota_mode"] | doc["ota"] | false;` oraz obsługę statusu ACK zarówno dla `"ok":true`, jak i `"status":"ok"`.

---

## 2. Analiza Zmian w `uart_protocol.h`

Plik: `firmware/shared/uart_protocol/include/uart_protocol.h`

### 2.1 Enum `CommandType`
Obecna definicja (linie 40–51):
```cpp
enum class CommandType : uint8_t {
    Unknown = 0,
    SetRelay,
    SetTempTarget,
    SetSchedule,
    FeedTrigger,
    SyncLights,
    SyncTime,
    ClearAlarm,
    GetStatus,
    SetSafeMode
};
```

#### Proponowana Modyfikacja:
```cpp
enum class CommandType : uint8_t {
    Unknown = 0,
    SetRelay,
    SetTempTarget,
    SetSchedule,
    FeedTrigger,
    SyncLights,
    SyncTime,
    ClearAlarm,
    GetStatus,
    SetSafeMode,
    EnterOta          // <-- Wartość 10 (0x0A), inicjalizacja trybu serwisowego OTA na WeMos
};
```

#### Uzasadnienie Inżynieryjne:
- Zachowano bazowy typ `uint8_t` (rozmiar 1 bajt).
- Wartość `EnterOta` umieszczona została na końcu listy (indeks 10), co gwarantuje, że dotychczasowe wartości `Unknown` (0) do `SetSafeMode` (9) pozostają niezmienne.
- Wszystkie instrukcje `switch (cmd.type)` w projekcie posiadają gałąź `default:`, co zapobiega ostrzeżeniom kompilatora `-Wswitch` w nieskończonych jeszcze modułach konsumenckich.

---

### 2.2 Struktura `AquariumCommand`
Obecna definicja (linie 80–93):
```cpp
struct AquariumCommand {
    CommandType type;
    uint8_t channel;           // Dla SetRelay (0..7)
    bool state;                // Dla SetRelay (0 lub 1)
    float temp_target;         // Dla SetTempTarget
    float temp_hysteresis;     // Dla SetTempTarget
    uint8_t schedule_dev;      // 0=Light, 1=Filter, 2=Aerator, 3=CO2
    uint8_t on_hour;
    uint8_t on_minute;
    uint8_t off_hour;
    uint8_t off_minute;
    bool schedule_enabled;
    uint32_t epoch_time;       // Dla SyncTime
};
```

#### Proponowana Modyfikacja:
```cpp
struct AquariumCommand {
    CommandType type;
    uint8_t channel;           // Dla SetRelay (0..7)
    bool state;                // Dla SetRelay (0 lub 1)
    float temp_target;         // Dla SetTempTarget
    float temp_hysteresis;     // Dla SetTempTarget
    uint8_t schedule_dev;      // 0=Light, 1=Filter, 2=Aerator, 3=CO2
    uint8_t on_hour;
    uint8_t on_minute;
    uint8_t off_hour;
    uint8_t off_minute;
    bool schedule_enabled;
    uint32_t epoch_time;       // Dla SyncTime
    char ota_ssid[33];         // Dla EnterOta: opcjonalny SSID sieci Wi-Fi (max 32 znaki + null)
    char ota_pass[65];         // Dla EnterOta: opcjonalne hasło WPA2-PSK (max 64 znaki + null)
};
```

#### Analiza Rozmiaru i Wyrównania Pamięci (Alignment):
- `ota_ssid`: bufor 33-bajtowy. Maksymalna długość identyfikatora SSID wg standardu 802.11 to 32 oktety. Dodatkowy 1 bajt rezerwowany jest na terminator `\0`.
- `ota_pass`: bufor 65-bajtowy. Maksymalna długość hasła WPA2-PSK to 63 znaki ASCII (lub 64 znaki w formacie hex PSK). 65 bajtów gwarantuje bezpieczne przechowywanie 64 znaków + `\0`.
- Spójność z kodem CYD: Stałe te idealnie odpowiadają definicjom z `firmware/cyd_controller/include/wifi_credential_store.h`:
  - `constexpr size_t WIFI_CREDENTIAL_SSID_BYTES = 33U;`
  - `constexpr size_t WIFI_CREDENTIAL_PASSWORD_BYTES = 65U;`
- Rozmiar struktury:
  - Dotychczas: 24 bajty (z dopełnieniem do wielokrotności 4 bajtów na architekturze 32-bitowej Xtensa).
  - Po zmianie: 24 + 33 + 65 = 122 bajty -> po dopełnieniu (padding): **124 bajty**.
  - Wpływ na stos: Struktura `AquariumCommand` jest polem składowym unii/struktury `AquariumMessage` alokowanej na stosie w funkcjach odbiorczych. Zwiększenie o 100 bajtów jest całkowicie bezpieczne zarówno dla stosu ESP32 (standardowo 4 KB–16 KB), jak i ESP8266 (stos systemowy 4 KB).

---

### 2.3 Struktura `AquariumTelemetry`
Obecna definicja (linie 63–77):
```cpp
struct AquariumTelemetry {
    uint32_t timestamp;        // Czas unix epoch z DS3231
    float temperature;         // Temp wody DS18B20 (°C)
    float temp_target;         // Temp zadana (°C)
    float temp_hysteresis;     // Histereza (°C)
    float ph;                  // Odczyn pH z ADS1115
    float ec;                  // Przewodność EC
    uint8_t relay_states;      // Bity 0..7 stanów załączenia przekaźników
    uint16_t alarms;           // Maska AlarmFlag
    bool water_level_low;      // Pływak poziomu wody
    bool leak_detected;        // Czujnik wycieku
    uint32_t uptime_seconds;   // Uptime sterownika WeMos
    bool safe_mode;            // Czy sterownik jest w trybie awaryjnym
    uint8_t light_mode;        // Tryb oświetlenia Aquael (1, 2, 3)
};
```

#### Proponowana Modyfikacja:
```cpp
struct AquariumTelemetry {
    uint32_t timestamp;        // Czas unix epoch z DS3231
    float temperature;         // Temp wody DS18B20 (°C)
    float temp_target;         // Temp zadana (°C)
    float temp_hysteresis;     // Histereza (°C)
    float ph;                  // Odczyn pH z ADS1115
    float ec;                  // Przewodność EC
    uint8_t relay_states;      // Bity 0..7 stanów załączenia przekaźników
    uint16_t alarms;           // Maska AlarmFlag
    bool water_level_low;      // Pływak poziomu wody
    bool leak_detected;        // Czujnik wycieku
    uint32_t uptime_seconds;   // Uptime sterownika WeMos
    bool safe_mode;            // Czy sterownik jest w trybie awaryjnym
    uint8_t light_mode;        // Tryb oświetlenia Aquael (1, 2, 3)
    bool ota_mode;             // true jeśli WeMos jest w trybie serwisowym OTA
    char ota_ip[16];           // Adres IP WeMos w trybie OTA (np. "192.168.1.150" lub "192.168.4.1")
};
```

#### Analiza Rozmiaru i Wyrównania Pamięci:
- `ota_mode`: 1 bajt typu `bool`. Wskazuje aktywność trybu OTA.
- `ota_ip`: bufor 16-bajtowy. Maksymalny adres IPv4 w zapisie kropkowym (np. `255.255.255.255`) zajmuje 15 znaków + 1 bajt `\0`.
- Rozmiar struktury:
  - Dotychczas: 36 bajtów.
  - Po zmianie: 36 + 1 (`ota_mode`) + 16 (`ota_ip`) = 53 bajty -> po wyrównaniu do granicy 4-bajtowej: **56 bajtów**.
  - Wzrost zaledwie o 20 bajtów.

---

## 3. Analiza Zmian w `uart_protocol.cpp`

Plik: `firmware/shared/uart_protocol/src/uart_protocol.cpp`

### 3.1 Funkcja `serializeCommand()`
Obecna implementacja (linie 30–83):
```cpp
String serializeCommand(const AquariumCommand &cmd)
{
    StaticJsonDocument<384> doc;
    doc["type"] = "cmd";

    switch (cmd.type) {
    // ...
    case CommandType::SetSafeMode:
        doc["cmd"] = "safe_mode";
        doc["val"] = cmd.state ? 1 : 0;
        break;
    default:
        return String();
    }

    String output;
    output.reserve(160);
    serializeJson(doc, output);
    return output;
}
```

#### Wymagane Zmiany:
1. Dodanie gałęzi dla `CommandType::EnterOta`:
```cpp
    case CommandType::EnterOta:
        doc["cmd"] = "enter_ota";
        if (cmd.ota_ssid[0] != '\0') {
            doc["ssid"] = cmd.ota_ssid;
        }
        if (cmd.ota_pass[0] != '\0') {
            doc["pass"] = cmd.ota_pass;
        }
        break;
```
2. Zwiększenie rezerwacji pamięci dla bufora wyjściowego `output`:
```cpp
    String output;
    output.reserve(256); // Zwiększone z 160 do 256
    serializeJson(doc, output);
    return output;
```

#### Weryfikacja Pojemności `StaticJsonDocument<384>`:
- Liczba kluczy w dokumencie dla `EnterOta`: 4 (`"type"`, `"cmd"`, `"ssid"`, `"pass"`).
- Pamięć na sloty obiektów: `JSON_OBJECT_SIZE(4) = 4 * 16 = 64 bajty`.
- Kopiowanie ciągów znaków (worst-case, gdyby biblioteka kopiowała `char[]`):
  - Klucze `"type"`, `"cmd"`, `"ssid"`, `"pass"` są literałami `const char*` -> alokacja: 0 bajtów (zero-copy pointer storage).
  - Wartości `"cmd"`: `"enter_ota"` jest literałem -> 0 bajtów.
  - Wartość `"ssid"`: bufor do 33 bajtów.
  - Wartość `"pass"`: bufor do 65 bajtów.
  - Sumaryczna pamięć na wartości: 33 + 65 = 98 bajtów (z wyrównaniem do 4 bajtów: 104 bajty).
- **Maksymalne wymagane zużycie:** 64 + 104 = **168 bajtów**.
- **Dostępna pojemność:** **384 bajty**.
- **Pozostały margines:** 384 - 168 = **216 bajtów (56.2% wolnej przestrzeni)**.
- `StaticJsonDocument<384>` jest w 100% wystarczający i nie wymaga modyfikacji.

#### Weryfikacja Długości Ciągu Wyjściowego:
- Najdłuższa możliwa ramka JSON:
  `{"type":"cmd","cmd":"enter_ota","ssid":"12345678901234567890123456789012","pass":"1234567890123456789012345678901234567890123456789012345678901234"}`
- Długość bazowa: 147 znaków.
- Jeśli hasło zawiera znaki specjalne wymagające ucieczki JSON (np. `\"` lub `\\`), każdy taki znak powiększa długość o 1 bajt.
- Przy `output.reserve(160)` hasło ze znakami ucieczki mogłoby wymusić pojedynczą reallokację sterty.
- Zwiększenie do `output.reserve(256)` definitywnie eliminuje jakiekolwiek ryzyko fragmentacji sterty.

---

### 3.2 Funkcja `serializeTelemetry()`
Obecna implementacja (linie 6–28):
```cpp
String serializeTelemetry(const AquariumTelemetry &t)
{
    StaticJsonDocument<512> doc;
    doc["type"] = "telemetry";
    doc["ts"] = t.timestamp;
    doc["temp"] = serialized(String(t.temperature, 2));
    doc["temp_tgt"] = serialized(String(t.temp_target, 2));
    doc["temp_hyst"] = serialized(String(t.temp_hysteresis, 2));
    doc["ph"] = serialized(String(t.ph, 2));
    doc["ec"] = serialized(String(t.ec, 1));
    doc["relays"] = t.relay_states;
    doc["alarms"] = t.alarms;
    doc["w_low"] = t.water_level_low;
    doc["leak"] = t.leak_detected;
    doc["uptime"] = t.uptime_seconds;
    doc["safe"] = t.safe_mode;
    doc["lmode"] = t.light_mode;

    String output;
    output.reserve(256);
    serializeJson(doc, output);
    return output;
}
```

#### Wymagane Zmiany:
1. Zapis pól OTA do dokumentu JSON:
```cpp
    doc["ota_mode"] = t.ota_mode;
    if (t.ota_ip[0] != '\0') {
        doc["ota_ip"] = t.ota_ip;
    }
```
2. Zwiększenie rezerwacji pamięci dla bufora wyjściowego `output`:
```cpp
    String output;
    output.reserve(320); // Zwiększone z 256 do 320
    serializeJson(doc, output);
    return output;
```

#### Uwaga Architektoniczna Dotycząca Klucza `"ota_mode"` vs `"ota"`:
- W istniejącej telemetrii stosowane są krótkie klucze (np. `safe` dla `safe_mode`, `lmode` dla `light_mode`, `w_low` dla `water_level_low`).
- W dokumencie `SCOPE.md` (linia 53) oraz DISPATCH podano `"ota_mode"`, z kolei w badaniu technicznym `explorer_survey_3` pojawił się również wariant `"ota"`.
- W serializacji przyjmujemy kanoniczny klucz `"ota_mode"` (lub `"ota"`).
- W parserze `parseMessage()` obsługujemy oba warianty bezwarunkowo, co gwarantuje 100% niewrażliwości na niespójności między wersjami modułów.
- Warunkowa serializacja `ota_ip` (tylko gdy bufor nie jest pusty): W normalnym trybie pracy (gdy `ota_mode == false` i `ota_ip == ""`) nie zaśmiecamy magistrali zbędnym kluczem `"ota_ip":""`. Odpowiada to dokładnie konwencji przyjętej w `serializeAck()` (gdzie pole `"msg"` jest pomijane, jeśli jest puste) oraz w `serializeCommand()`.

#### Weryfikacja Pojemności `StaticJsonDocument<512>`:
- Liczba kluczy w dokumencie telemetrii: 16 (14 dotychczasowych + `"ota_mode"` + `"ota_ip"`).
- Pamięć na sloty obiektów: `JSON_OBJECT_SIZE(16) = 16 * 16 = 256 bajtów`.
- Kopiowanie ciągów znaków:
  - Klucze są literałami `const char*` -> 0 bajtów.
  - Wartości float formatowane jako `serialized(...)` przechowują referencję do tymczasowego ciągu.
  - Wartość `t.ota_ip`: ciąg do 16 bajtów.
- **Maksymalne wymagane zużycie:** 256 + 16 = **272 bajty**.
- **Dostępna pojemność:** **512 bajtów**.
- **Pozostały margines:** 512 - 272 = **240 bajtów (46.9% wolnej przestrzeni)**.
- `StaticJsonDocument<512>` w zupełności wystarcza.

#### Weryfikacja Długości Ciągu Wyjściowego:
- Dotychczasowa długość ramki telemetrii: ~188 znaków.
- Dodatek dla trybu OTA:
  `,"ota_mode":true,"ota_ip":"192.168.1.150"` -> 43 znaki.
- Maksymalna długość ramki: 188 + 43 = **231 znaków**.
- Ponieważ 231 jest bardzo bliskie dotychczasowej rezerwacji `output.reserve(256)` (margines zaledwie 25 znaków), zaleca się zwiększenie rezerwacji do `output.reserve(320)`. Eliminuje to wszelkie reallokacje `String` na stercie ESP8266 i ESP32.

---

### 3.3 Funkcja `parseMessage()`
Plik: `firmware/shared/uart_protocol/src/uart_protocol.cpp` (linie 113–203).

#### Wymagane Zmiany w Sekcji Parsowania Komend (`type == "cmd"`):
Dodanie gałęzi `else if (strcmp(cmd_name, "enter_ota") == 0)`:
```cpp
        } else if (strcmp(cmd_name, "enter_ota") == 0) {
            out_msg.command.type = CommandType::EnterOta;
            const char *s = doc["ssid"] | "";
            strncpy(out_msg.command.ota_ssid, s, sizeof(out_msg.command.ota_ssid) - 1);
            out_msg.command.ota_ssid[sizeof(out_msg.command.ota_ssid) - 1] = '\0';
            const char *p = doc["pass"] | "";
            strncpy(out_msg.command.ota_pass, p, sizeof(out_msg.command.ota_pass) - 1);
            out_msg.command.ota_pass[sizeof(out_msg.command.ota_pass) - 1] = '\0';
        }
```

Dodatkowo, przed sekwencją porównań `cmd_name`, zaleca się wyczyszczenie struktury komendy:
```cpp
        memset(&out_msg.command, 0, sizeof(out_msg.command));
```
Zapewnia to, że nawet jeśli zmienna `out_msg` na stosie w funkcji wywołującej była niezainicjalizowana, wszystkie pola struktury `AquariumCommand` będą miały przewidywalne wartości zerowe.

#### Wymagane Zmiany w Sekcji Parsowania Telemetrii (`type == "telemetry"`):
Uzupełnienie o odczyt pól OTA z podwójnym fallbackiem:
```cpp
        out_msg.telemetry.ota_mode = doc.containsKey("ota_mode") ? (doc["ota_mode"] | false) : (doc["ota"] | false);
        const char *ip_str = doc["ota_ip"] | "";
        strncpy(out_msg.telemetry.ota_ip, ip_str, sizeof(out_msg.telemetry.ota_ip) - 1);
        out_msg.telemetry.ota_ip[sizeof(out_msg.telemetry.ota_ip) - 1] = '\0';
```

#### Defensywne Usprawnienie w Sekcji Parsowania ACK (`type == "ack"`):
W dokumencie `SCOPE.md` (linia 52) podano przykładową ramkę: `{"type":"ack","cmd":"enter_ota","status":"ok"}`. Z kolei w dotychczasowym kodzie `uart_protocol.cpp` oraz `explorer_survey_3` używane jest pole logiczne `"ok":true`.
Aby zabezpieczyć komunikację przed jakąkolwiek rozbieżnością, w sekcji `type == "ack"` zaleca się:
```cpp
        out_msg.ack.ok = doc.containsKey("ok") ? (doc["ok"] | false) : (strcmp(doc["status"] | "", "ok") == 0);
```

#### Weryfikacja Pojemności `StaticJsonDocument<768>` w `parseMessage()`:
- Ponieważ wejściowy ciąg znaków przekazywany jest jako `const char *json_line`, parser ArduinoJson w trybie `deserializeJson(doc, json_line)` kopiuje wszystkie klucze i wartości tekstowe do wewnętrznego poola pamięci.
- **Scenariusz A: Parsowanie Telemetrii (najbardziej obciążający)**:
  - 16 par klucz-wartość: `JSON_OBJECT_SIZE(16) = 16 * 16 = 256 bajtów`.
  - Pamięć na klucze (16 kluczy z terminatorami `\0`): 97 bajtów.
  - Pamięć na wartości tekstowe (`"telemetry"` + `ota_ip` max 16 znaków): 10 + 16 = 26 bajtów.
  - **Maksymalne wymagane zużycie:** 256 + 97 + 26 = **379 bajtów**.
  - **Dostępna pojemność:** **768 bajtów**.
  - **Pozostały margines:** 768 - 379 = **389 bajtów (50.6% wolnej przestrzeni)**.
- **Scenariusz B: Parsowanie Komendy `enter_ota`**:
  - 4 pary klucz-wartość: `JSON_OBJECT_SIZE(4) = 4 * 16 = 64 bajty`.
  - Pamięć na klucze: 19 bajtów.
  - Pamięć na wartości (`"cmd"`, `"enter_ota"`, `ssid` 33 B, `pass` 65 B): 10 + 33 + 65 = 108 bajtów.
  - **Maksymalne wymagane zużycie:** 64 + 19 + 108 = **191 bajtów**.
  - **Dostępna pojemność:** **768 bajtów**.
  - **Pozostały margines:** 768 - 191 = **577 bajtów (75.1% wolnej przestrzeni)**.
- **Wniosek:** `StaticJsonDocument<768>` posiada potężny margines bezpieczeństwa i nie wymaga powiększania.

---

## 4. Rekomendacja Difa Implementacyjnego (Exact Code Diffs)

### 4.1 Diff dla `firmware/shared/uart_protocol/include/uart_protocol.h`

```diff
--- a/firmware/shared/uart_protocol/include/uart_protocol.h
+++ b/firmware/shared/uart_protocol/include/uart_protocol.h
@@ -48,7 +48,8 @@ enum class CommandType : uint8_t {
     ClearAlarm,
     GetStatus,
-    SetSafeMode
+    SetSafeMode,
+    EnterOta
 };
 
 // Typy wiadomości w protokole
@@ -74,6 +75,8 @@ struct AquariumTelemetry {
     uint32_t uptime_seconds;   // Uptime sterownika WeMos
     bool safe_mode;            // Czy sterownik jest w trybie awaryjnym
     uint8_t light_mode;        // Tryb oświetlenia Aquael (1, 2, 3)
+    bool ota_mode;             // true jeśli aktywny jest tryb serwisowy OTA
+    char ota_ip[16];           // Adres IP WeMos w trybie OTA
 };
 
 // Struktura pojedynczej komendy (CYD -> WeMos)
@@ -90,6 +93,8 @@ struct AquariumCommand {
     uint8_t off_minute;
     bool schedule_enabled;
     uint32_t epoch_time;       // Dla SyncTime
+    char ota_ssid[33];         // Dla EnterOta: opcjonalny SSID sieci Wi-Fi
+    char ota_pass[65];         // Dla EnterOta: opcjonalne hasło WPA2
 };
 
 // Struktura potwierdzenia ACK
```

---

### 4.2 Diff dla `firmware/shared/uart_protocol/src/uart_protocol.cpp`

```diff
--- a/firmware/shared/uart_protocol/src/uart_protocol.cpp
+++ b/firmware/shared/uart_protocol/src/uart_protocol.cpp
@@ -21,8 +21,12 @@ String serializeTelemetry(const AquariumTelemetry &t)
     doc["uptime"] = t.uptime_seconds;
     doc["safe"] = t.safe_mode;
     doc["lmode"] = t.light_mode;
+    doc["ota_mode"] = t.ota_mode;
+    if (t.ota_ip[0] != '\0') {
+        doc["ota_ip"] = t.ota_ip;
+    }
 
     String output;
-    output.reserve(256);
+    output.reserve(320);
     serializeJson(doc, output);
     return output;
 }
@@ -73,11 +77,19 @@ String serializeCommand(const AquariumCommand &cmd)
     case CommandType::SetSafeMode:
         doc["cmd"] = "safe_mode";
         doc["val"] = cmd.state ? 1 : 0;
         break;
+    case CommandType::EnterOta:
+        doc["cmd"] = "enter_ota";
+        if (cmd.ota_ssid[0] != '\0') {
+            doc["ssid"] = cmd.ota_ssid;
+        }
+        if (cmd.ota_pass[0] != '\0') {
+            doc["pass"] = cmd.ota_pass;
+        }
+        break;
     default:
         return String();
     }
 
     String output;
-    output.reserve(160);
+    output.reserve(256);
     serializeJson(doc, output);
     return output;
 }
@@ -140,6 +152,10 @@ bool parseMessage(const char *json_line, AquariumMessage &out_msg)
         out_msg.telemetry.uptime_seconds = doc["uptime"] | 0UL;
         out_msg.telemetry.safe_mode = doc["safe"] | false;
         out_msg.telemetry.light_mode = doc["lmode"] | 1U;
+        out_msg.telemetry.ota_mode = doc.containsKey("ota_mode") ? (doc["ota_mode"] | false) : (doc["ota"] | false);
+        const char *ip_str = doc["ota_ip"] | "";
+        strncpy(out_msg.telemetry.ota_ip, ip_str, sizeof(out_msg.telemetry.ota_ip) - 1);
+        out_msg.telemetry.ota_ip[sizeof(out_msg.telemetry.ota_ip) - 1] = '\0';
         return true;
     }
 
     if (strcmp(type_str, "cmd") == 0) {
         out_msg.type = MessageType::Command;
+        memset(&out_msg.command, 0, sizeof(out_msg.command));
         const char *cmd_name = doc["cmd"] | "";
         if (strcmp(cmd_name, "set_relay") == 0) {
@@ -176,6 +192,14 @@ bool parseMessage(const char *json_line, AquariumMessage &out_msg)
         } else if (strcmp(cmd_name, "safe_mode") == 0) {
             out_msg.command.type = CommandType::SetSafeMode;
             out_msg.command.state = (doc["val"] | 0) != 0;
+        } else if (strcmp(cmd_name, "enter_ota") == 0) {
+            out_msg.command.type = CommandType::EnterOta;
+            const char *s = doc["ssid"] | "";
+            strncpy(out_msg.command.ota_ssid, s, sizeof(out_msg.command.ota_ssid) - 1);
+            out_msg.command.ota_ssid[sizeof(out_msg.command.ota_ssid) - 1] = '\0';
+            const char *p = doc["pass"] | "";
+            strncpy(out_msg.command.ota_pass, p, sizeof(out_msg.command.ota_pass) - 1);
+            out_msg.command.ota_pass[sizeof(out_msg.command.ota_pass) - 1] = '\0';
         } else {
             out_msg.command.type = CommandType::Unknown;
         }
@@ -186,7 +210,7 @@ bool parseMessage(const char *json_line, AquariumMessage &out_msg)
         out_msg.type = MessageType::Ack;
         const char *cmd_name = doc["cmd"] | "";
         strncpy(out_msg.ack.cmd, cmd_name, sizeof(out_msg.ack.cmd) - 1);
         out_msg.ack.cmd[sizeof(out_msg.ack.cmd) - 1] = '\0';
-        out_msg.ack.ok = doc["ok"] | false;
+        out_msg.ack.ok = doc.containsKey("ok") ? (doc["ok"] | false) : (strcmp(doc["status"] | "", "ok") == 0);
         const char *msg_text = doc["msg"] | "";
         strncpy(out_msg.ack.message, msg_text, sizeof(out_msg.ack.message) - 1);
         out_msg.ack.message[sizeof(out_msg.ack.message) - 1] = '\0';
```

---

## 5. Analiza Wpływu na Konsumentów Biblioteki (Consumer Impact)

### 5.1 WeMos D1 Uno (`firmware/wemos_controller`)
- **`main.cpp` - `process_serial_line()`**:
  Obecnie nieobsługiwane komendy trafiają do `default: Serial.println(AquaProtocol::serializeAck("unknown", false));`.
  Po rozszerzeniu protokołu o `CommandType::EnterOta`, WeMos w M2 zaimplementuje obsługę:
  1. Natychmiastowe odcięcie przekaźników: `g_hardware_i2c.setAllRelays(0x00);`.
  2. Ustawienie `safe_mode_active = true;`.
  3. Uruchomienie podsystemu OTA (Wi-Fi STA z fallbackiem do SoftAP `AquaBox-OTA`).
  4. Odesłanie ramki ACK: `Serial.println(AquaProtocol::serializeAck("enter_ota", true, ip_str)); Serial.flush();`.
  5. Wyciszenie telemetrii UART (`telemetry_uart_enabled = false;`).
- **`main.cpp` - `send_telemetry()`**:
  Obecnie tworzy instancję `AquaProtocol::AquariumTelemetry t;` na stosie i przypisuje wszystkie pola.
  W M2 WeMos uzupełni pola:
  `t.ota_mode = ota_service_active;`
  `strncpy(t.ota_ip, ota_current_ip, sizeof(t.ota_ip));`

### 5.2 ESP32 CYD Touch Panel (`firmware/cyd_controller`)
- **`uart_client.h` / `uart_client.cpp`**:
  W M1 należy dodać metodę pomocniczą:
  ```cpp
  bool sendEnterOta(const char *ssid = nullptr, const char *pass = nullptr);
  ```
  Implementacja:
  ```cpp
  bool UartClient::sendEnterOta(const char *ssid, const char *pass)
  {
      AquaProtocol::AquariumCommand cmd;
      memset(&cmd, 0, sizeof(cmd));
      cmd.type = AquaProtocol::CommandType::EnterOta;
      if (ssid != nullptr) {
          strncpy(cmd.ota_ssid, ssid, sizeof(cmd.ota_ssid) - 1);
          cmd.ota_ssid[sizeof(cmd.ota_ssid) - 1] = '\0';
      }
      if (pass != nullptr) {
          strncpy(cmd.ota_pass, pass, sizeof(cmd.ota_pass) - 1);
          cmd.ota_pass[sizeof(cmd.ota_pass) - 1] = '\0';
      }
      return sendCommand(cmd);
  }
  ```
- **Obsługa ACK i Gotowości**:
  W `UartClient::processLine()` należy buforować odebrane ramki `MessageType::Ack` (np. `latest_ack_`), co pozwoli interfejsowi LVGL na natychmiastowe wyświetlenie modalu z adresem IP przysłanym przez WeMos.
- **Wyciszenie Alarmu Braku Telemetrii**:
  W `UartClient::update()`: Gdy `latest_telemetry_.ota_mode == true`, panel CYD powinien zawiesić sprawdzanie `OFFLINE_TIMEOUT_MS` (3s) i nie załączać sygnału dźwiękowego buzzera, ponieważ WeMos celowo wygasza UART na czas wgrywania nowego firmware.

---

## 6. Weryfikacja i Metoda Testowania

1. **Weryfikacja Kompilacji Środowisk:**
   - Kompilacja kontrolera WeMos D1 Uno:
     `pio run -e wemos_d1_uno`
   - Kompilacja panelu dotykowego CYD:
     `pio run -e cyd_2_8`
   - Uruchomienie natywnych testów jednostkowych:
     `pio test -e native`
2. **Weryfikacja Wektorów Testowych NDJSON:**
   - Komenda bazowa:
     `{"type":"cmd","cmd":"enter_ota"}` -> Parsuje do `CommandType::EnterOta`, `ota_ssid == ""`, `ota_pass == ""`.
   - Komenda z parametrami Wi-Fi:
     `{"type":"cmd","cmd":"enter_ota","ssid":"DomoweWiFi","pass":"SuperHaslo"}` -> Parsuje poprawnie SSID i hasło.
   - Telemetria trybu serwisowego:
     `{"type":"telemetry","ts":1773852100,"temp":24.80,"temp_tgt":25.00,"temp_hyst":0.30,"ph":6.85,"ec":415.0,"relays":0,"alarms":0,"w_low":false,"leak":false,"uptime":120,"safe":true,"lmode":1,"ota_mode":true,"ota_ip":"192.168.1.150"}` -> Parsuje `ota_mode == true`, `ota_ip == "192.168.1.150"`.
   - Kompatybilność wsteczna:
     Starsza ramka telemetrii bez pól `"ota_mode"` / `"ota_ip"` -> Parsuje do `ota_mode == false`, `ota_ip == ""`.
