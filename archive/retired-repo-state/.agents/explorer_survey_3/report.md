# Raport Techniczny: Protokół UART NDJSON, Bezpieczeństwo Sprzętowe, Pamięć EEPROM i Dokumentacja OTA

**Autor:** explorer_survey_3  
**Data:** 2026-09-18  
**Projekt:** AquaCYD / AquaBox Dual-MCU Controller  
**Dotyczy:** Wdrożenie kompleksowego systemu bezprzewodowej aktualizacji OTA (ESP8266 WeMos D1 Uno w AquaBox + ESP32 CYD 2.8")

---

## 1. Wstęp i Cel Badania

W dwuprocesorowym sterowniku akwarium (stacja wykonawcza WeMos D1 Uno ESP8266 w obudowie AquaBox oraz panel dotykowy ESP32-2432S028R CYD) dotychczasowe wgrywanie firmware wymagało fizycznego rozkręcania obudowy, wyciągania zworek izolacyjnych UART (`J_TX`, `J_RX`) i osobnego podłączania kabli USB do każdego z mikrokontrolerów.

Celem niniejszego audytu jest opracowanie spójnego i bezpiecznego mechanizmu aktualizacji **Over-The-Air (OTA)** dla obu mikrokontrolerów z zachowaniem:
1. Całkowicie wyłączonego Wi-Fi w WeMos podczas normalnej pracy (tryb 100% autonomiczny, brak poboru prądu RF i zakłóceń ADC).
2. Dwóch dróg wejścia WeMos w tryb serwisowy: zdalnej (komenda UART `enter_ota`) i sprzętowej (przycisk monostabilny przy włączeniu zasilania).
3. Bezwzględnego bezpieczeństwa biologicznego i pożarowego (natychmiastowe odcięcie przekaźników grzałki i CO2 przed jakimkolwiek zapisem do Flash).
4. Pełnej integralności magistrali UART podczas operacji zapisu flash (wygaszenie telemetrii, by nie zakłócać transmisji TCP/OTA i zapisu pamięci).
5. Trwałości kalibracji czujników (pH, termostat) i harmonogramów w pamięci EEPROM/NVS przez automatyczną migrację struktur.
6. Kompletnej aktualizacji bazy wiedzy w Obsidian.

---

## 2. Architektura Współdzielonej Biblioteki `uart_protocol`

### 2.1. Lokalizacja i Zależności
- **Ścieżka:** `firmware/shared/uart_protocol/`
  - Plik nagłówkowy: `firmware/shared/uart_protocol/include/uart_protocol.h`
  - Implementacja: `firmware/shared/uart_protocol/src/uart_protocol.cpp`
  - Konfiguracja PlatformIO: współdzielona przez dyrektywę `lib_extra_dirs = firmware/shared` w `platformio.ini` dla środowisk `cyd_2_8` i `wemos_d1_uno`.
- **Zależności:** `bblanchon/ArduinoJson@^6.21.4` (używana wersja 6.21.6).
- **Parametry fizyczne transmisji:** 115200 baud, 8 bitów danych, brak parzystości, 1 bit stopu (8N1). Transmisja w standardzie NDJSON (Newline Delimited JSON, linie zakończone `\r\n` lub `\n`).

### 2.2. Aktualne Struktury i Przepływ Danych
Obecny protokół obsługuje 4 typy komunikatów identyfikowane polem `"type"`:
- `"telemetry"`: przesyłany cyklicznie co 1000 ms z WeMos do CYD (`serializeTelemetry`).
- `"cmd"`: przesyłany z CYD do WeMos w reakcji na akcje użytkownika (`serializeCommand`).
- `"ack"`: odsyłany przez WeMos po przetworzeniu komendy (`serializeAck`).
- `"time_sync"`: synchronizacja czasu Unix Epoch z CYD do RTC DS3231 na WeMos (`serializeTimeSync`).

### 2.3. Zidentyfikowane Ograniczenia i Luki
1. **Brak komendy wejścia w tryb OTA:** Enum `CommandType` zawiera komendy: `SetRelay`, `SetTempTarget`, `SetSchedule`, `FeedTrigger`, `SyncLights`, `SyncTime`, `ClearAlarm`, `GetStatus`, `SetSafeMode`. Brak komendy dedykowanej dla OTA (`EnterOta`).
2. **Brak odbioru ramek ACK w `UartClient` na CYD:** W pliku `firmware/cyd_controller/src/uart_client.cpp`, funkcja `processLine()` przetwarza wyłącznie `MessageType::Telemetry`. Ramki typu `MessageType::Ack` są całkowicie ignorowane, co uniemożliwia panelowi CYD uzyskanie asynchronicznego potwierdzenia wejścia WeMos w tryb serwisowy.
3. **Brak flagi stanu serwisowego/OTA w telemetrii:** Struktura `AquariumTelemetry` posiada flagę `safe_mode`, lecz nie informuje panelu CYD, czy WeMos przygotowuje się do odbioru firmware, jaki adres IP uzyskał i w jakim trybie sieci pracuje.

---

## 3. Specyfikacja Nowej Komendy `enter_ota` i Potwierdzenia ACK

### 3.1. Komenda `enter_ota` (CYD -> WeMos)

Komenda jest wysyłana z panelu dotykowego CYD (kafelek serwisowy LVGL) lub interfejsu REST API panelu WWW.

#### Format NDJSON:
```json
{"type":"cmd","cmd":"enter_ota","ssid":"HomeWiFi_2.4G","pass":"TajneHasloWiFi"}
```
*Uwaga:* Pola `"ssid"` oraz `"pass"` są opcjonalne. Jeśli zostaną pominięte lub pozostaną puste (`""`), WeMos próbuje połączyć się za pomocą poświadczeń zapisanych uprzednio w EEPROM, a w razie ich braku/niepowodzenia natychmiast uruchamia własny AP `AquaBox-OTA`.

#### Rozszerzenie Struktur C++ w `uart_protocol.h`:
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
    EnterOta          // <-- Nowa komenda
};

struct AquariumCommand {
    CommandType type;
    uint8_t channel;
    bool state;
    float temp_target;
    float temp_hysteresis;
    uint8_t schedule_dev;
    uint8_t on_hour;
    uint8_t on_minute;
    uint8_t off_hour;
    uint8_t off_minute;
    bool schedule_enabled;
    uint32_t epoch_time;
    char ota_ssid[33]; // <-- Opcjonalny SSID przekazany z CYD
    char ota_pass[65]; // <-- Opcjonalne hasło WPA2
};
```

#### Serializacja i Deserializacja w `uart_protocol.cpp`:
```cpp
// serializeCommand:
case CommandType::EnterOta:
    doc["cmd"] = "enter_ota";
    if (cmd.ota_ssid[0] != '\0') {
        doc["ssid"] = cmd.ota_ssid;
    }
    if (cmd.ota_pass[0] != '\0') {
        doc["pass"] = cmd.ota_pass;
    }
    break;

// parseMessage (sekcja cmd):
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

### 3.2. Potwierdzenie ACK (WeMos -> CYD)

Po odebraniu komendy `enter_ota`, WeMos:
1. Błyskawicznie wyłącza wszystkie przekaźniki.
2. Inicjalizuje moduł Wi-Fi i łączy się ze stacją STA lub stawia Access Point.
3. Odsyła potwierdzenie ACK zawierające przydzielony adres IP i tryb pracy:
   ```json
   {"type":"ack","cmd":"enter_ota","ok":true,"msg":"STA:192.168.1.150"}
   ```
   W przypadku braku zasięgu sieci domowej:
   ```json
   {"type":"ack","cmd":"enter_ota","ok":true,"msg":"AP:192.168.4.1"}
   ```
   W przypadku błędu sprzętowego (np. awaria MCP23017):
   ```json
   {"type":"ack","cmd":"enter_ota","ok":false,"msg":"Relay cutoff failed"}
   ```

### 3.3. Obsługa ACK w `UartClient` na ESP32 CYD
Plik `firmware/cyd_controller/src/uart_client.cpp` musi zostać rozszerzony o buforowanie ostatniego odebranego ACK oraz funkcję oczekiwania na potwierdzenie:
```cpp
void UartClient::processLine(const char *line) {
    AquaProtocol::AquariumMessage msg;
    if (!AquaProtocol::parseMessage(line, msg)) return;

    if (msg.type == AquaProtocol::MessageType::Telemetry) {
        latest_telemetry_ = msg.telemetry;
        last_rx_ms_ = millis();
        online_ = true;
    } else if (msg.type == AquaProtocol::MessageType::Ack) {
        latest_ack_ = msg.ack;
        last_ack_ms_ = millis();
        has_new_ack_ = true;
    }
}
```
Umożliwi to panelowi CYD natychmiastowe wyświetlenie na ekranie LVGL:
*"WeMos gotowy do aktualizacji! Połącz się z http://192.168.1.150 lub AP AquaBox-OTA (192.168.4.1)"*.

---

## 4. Rozszerzenie Schematu Telemetrii o Flagi Trybu OTA

Aby zapewnić pełną widoczność stanu sterownika bez względu na odbiór pojedynczej ramki ACK, struktura telemetrii zostaje wzbogacona o pola statusu serwisowego.

### 4.1. Definicja w `uart_protocol.h`
```cpp
enum class OtaState : uint8_t {
    Idle = 0,          // Normalna praca (Wi-Fi wyłączone)
    Connecting = 1,    // Łączenie z domową siecią Wi-Fi
    Ready = 2,         // Serwer Web OTA i ArduinoOTA aktywne, oczekiwanie na plik .bin
    Flashing = 3,      // Trwa fizyczny zapis do pamięci Flash
    Error = 4          // Błąd inicjalizacji lub przekroczenie limitu czasu
};

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
    bool ota_mode;             // true jeśli aktywny jest tryb serwisowy OTA
    char ota_ip[16];           // Adres IP np. "192.168.1.150" lub "192.168.4.1"
};
```

### 4.2. Reprezentacja NDJSON i Kompatybilność Wsteczna
Przykładowa ramka telemetrii podczas trybu serwisowego WeMos:
```json
{"type":"telemetry","ts":1773852100,"temp":24.80,"temp_tgt":25.00,"temp_hyst":0.30,"ph":6.85,"ec":415.0,"relays":0,"alarms":0,"w_low":false,"leak":false,"uptime":120,"safe":true,"lmode":1,"ota":true,"ota_ip":"192.168.1.150"}
```
- W parserze `parseMessage`:
  `out_msg.telemetry.ota_mode = doc["ota"] | false;`
  `strncpy(out_msg.telemetry.ota_ip, doc["ota_ip"] | "", sizeof(out_msg.telemetry.ota_ip) - 1);`
- Jeśli starsza wersja firmware nie wyśle pola `"ota"`, domyślną wartością jest `false`, co gwarantuje 100% kompatybilności wstecznej.
- Rozmiar bufora `StaticJsonDocument<512>` w `serializeTelemetry` oraz `StaticJsonDocument<768>` w `parseMessage` posiada ponad 150 bajtów marginesu, więc dodanie tych pól nie wymaga powiększania alokacji buforów JSON.

---

## 5. Mechanizm Wygaszania Transmisji UART w Czasie Flashowania

### 5.1. Uzasadnienie Inżynieryjne (Dlaczego UART musi milczeć?)
1. **Blokada Pamięci Podręcznej (Cache Lockout) w ESP8266:**
   Układ ESP8266 wykonuje kod programu z zewnętrznej pamięci Flash SPI przez 32 KB pamięci podręcznej ICACHE. W trakcie wykonywania operacji `spi_flash_erase_sector` oraz `spi_flash_write` dostęp do pamięci flash jest całkowicie zablokowany. Jeżeli w trakcie zapisu sektora nadejdzie przerwanie UART lub funkcja `Serial.println()` spróbuje wysłać dane z kodu umieszczonego we Flash (a nie w IRAM), następuje natychmiastowy reset procesora (`Fatal exception 0 / 28` lub `Hardware Watchdog Reset`), co prowadzi do uszkodzenia wgrywanego obrazu (bricked firmware).
2. **Stabilność Stosu Sieciowego TCP/IP:**
   Podczas wgrywania pliku firmware przez przeglądarkę WWW lub ArduinoOTA, stos lwIP wymaga ciągłej obsługi pakietów TCP w buforach odbiorczych. Ciągłe formatowanie i wysyłanie ramek JSON (które w `Serial.print` blokuje procesor przy zapełnieniu bufora TX) generuje jitter czasowy i ryzyko utraty pakietów TCP.
3. **Zapobieganie Kolizjom na Liniach:**
   Gdyby WeMos nadal nadawał telemetrię co 1 sekundę, panel CYD mógłby na nią odpowiadać lub rejestrować fałszywe stany.

### 5.2. Architektura Blokady Transmisji
Wyciszenie magistrali następuje dwuetapowo:
1. **Etap Przygotowawczy (Po wejściu w tryb OTA):**
   - Po wysłaniu ramki ACK z adresem IP WeMos wykonuje `Serial.flush()`.
   - Flaga `telemetry_uart_enabled` zostaje ustawiona na `false`.
   - Okresowe wysyłanie telemetrii w `loop()` zostaje wstrzymane.
2. **Etap Krytyczny (Start Flashowania):**
   - W callbacku `ArduinoOTA.onStart()` oraz w obsłudze WebServer `upload.status == UPLOAD_FILE_START`:
     ```cpp
     ArduinoOTA.onStart([]() {
         telemetry_uart_enabled = false;
         Serial.flush();
         // Wyłączenie odbioru UART, aby żadne śmieci z linii nie budziły przerwań
         // Serial.end() lub wyczyszczenie buforów RX
     });
     ```
3. **Zachowanie Panelu CYD:**
   - Panel CYD po odebraniu potwierdzenia przejścia WeMos w tryb OTA **wstrzymuje sprawdzanie timeoutu offline (3000 ms)** i wycisza buzzer alarmowy.
   - CYD nie wysyła żadnych zapytań ani komend na linię TX w stronę WeMos aż do momentu ponownego wykrycia prawidłowej ramki telemetrii po restarcie WeMos z nowym kodem.

---

## 6. Bezpieczeństwo Sprzętowe: Natychmiastowe Odcięcie Obciążeń

### 6.1. Identyfikacja Zagrożeń Biologicznych i Pożarowych
Sterownik AquaBox zarządza obciążeniami o dużej mocy i potencjale destrukcyjnym:
- **Przekaźnik CH4 (`RELAY_CH_HEATER`):** Grzałka akwariowa 100–300W. W przypadku zawieszenia przekaźnika w stanie załączonym w małym zbiorniku, temperatura wody po 2 godzinach przekroczy 34°C, co skutkuje śmiercią wszystkich ryb i roślin.
- **Przekaźnik CH5 (`RELAY_CH_CO2`):** Elektrozawór dozowania dwutlenku węgla. Ciągłe podawanie CO2 przy wyłączonym oświetleniu doprowadza do gwałtownego spadku pH (kwasica) i uduszenia obsady.
- **Przekaźnik CH0/CH1 (`RELAY_CH_LIGHT`):** Oświetlenie akwarium.

### 6.2. Logika Sprzętowa Przekaźników
Zgodnie z plikiem `firmware/wemos_controller/include/wemos_config.h`:
```cpp
constexpr bool RELAY_ACTIVE_LOW = true;
```
Przekaźniki sterowane są przez 16-bitowy ekspander I2C MCP23017 (Port A, piny 0..7).
- Stan **NISKI (0)** na wyjściu MCP23017 załącza optoizolator i przekaźnik.
- Stan **WYSOKI (1 / 0xFF)** na wyjściu MCP23017 rozwiera styki przekaźnika (stan bezpieczny, wyłączony).

### 6.3. Procedura Natychmiastowego Odcięcia (Pre-OTA Fail-Safe)
W momencie wyzwolenia trybu OTA (zarówno przez komendę UART `enter_ota`, jak i przycisk sprzętowy):
1. **Krok 1 (Hardware Cutoff):**
   Bezzwłoczne wywołanie metody ekspandera I2C:
   ```cpp
   g_hardware_i2c.setAllRelays(0x00); // 0 logiczne = 0xFF na rejestrze OLATA (wszystkie 8 przekaźników OFF)
   ```
   Czas wykonania po magistrali I2C (100 kHz): ok. **1.2 ms**.
2. **Krok 2 (Software Interlock):**
   Ustawienie flagi `safe_mode_active = true;`. Termostat (`g_temp_control.update()`) oraz silnik harmonogramów (`g_automation_engine.update()`) zostają natychmiast ominięte w pętli `loop()`, uniemożliwiając ponowne załączenie grzałki.
3. **Krok 3 (Safety Watchdog 5 minut):**
   Jeśli użytkownik zainicjuje tryb OTA, ale z jakiegokolwiek powodu nie wgra oprogramowania (np. zamknie przeglądarkę, utraci zasięg Wi-Fi):
   ```cpp
   if (now_ms - ota_enter_timestamp_ms >= 300000UL) { // 5 minut
       ESP.restart(); // Automatyczny powrót do normalnej pracy
   }
   ```
   Po restarcie WeMos wstaje w normalnym trybie autonomicznym z wyłączonym Wi-Fi, przywraca harmonogramy i termostat z pamięci EEPROM.

### 6.4. Sprzętowy Przycisk Awaryjny na WeMos (Hardware Fallback)
Gdyby panel CYD uległ uszkodzeniu mechanicznemu lub awarii zasilania, użytkownik musi mieć możliwość wgrania firmware do WeMos bez rozkręcania obudowy.
- **Wybór Pinu:** Analiza pinoutu ESP8266 na WeMos D1 Uno:
  - `GPIO5` (D1) & `GPIO4` (D2): zajęte przez I2C (MCP23017, ADS1115, DS3231).
  - `GPIO14` (D5): zajęte przez 1-Wire DS18B20.
  - `GPIO2` (D4): Status LED (wymaga HIGH przy bootowaniu).
  - `GPIO0` (D3): pin trybu FLASH bootloadera ROM (zwarcie do GND przy starcie wymusza tryb UART download zamiast uruchomienia programu).
  - `GPIO15` (D8): strapping pin (musi być LOW przy starcie).
  - **`GPIO12` (D6): WOLNY!** Posiada wewnętrzny rezystor podciągający (`INPUT_PULLUP`), nie bierze udziału w sekwencji bootowania ESP8266, w pełni bezpieczny.
- **Implementacja:**
  Zewnętrzny mikroprzełącznik podłączony między **D6 (GPIO12)** a **GND**.
  W `setup()`:
  ```cpp
  pinMode(WemosConfig::PIN_BTN_OTA, INPUT_PULLUP);
  delay(10);
  if (digitalRead(WemosConfig::PIN_BTN_OTA) == LOW) {
      // Przycisk przytrzymany przy starcie -> wejście w tryb serwisowy OTA
      enter_ota_service_mode();
  }
  ```

---

## 7. Analiza Pamięci EEPROM i Gwarancje Trwałości Danych

### 7.1. Układ Pamięci Flash i Emulacja EEPROM w ESP8266
W konfiguracji `platformio.ini` dla środowiska `wemos_d1_uno`:
```ini
board_build.ldscript = eagle.flash.4m2m.ld
```
- Całkowity rozmiar pamięci Flash SPI: **4 MB (0x400000 bajtów)**.
- Układ partycji:
  - `0x000000 - ~0x0FE000` (ok. 1019 KB): Aktywny szkic programu (Sketch Slot 1).
  - `~0x100000 - ~0x1FE000` (ok. 1019 KB): Bufor pobieranego szkicu OTA (Sketch Slot 2).
  - `0x200000 - 0x3FB000` (2012 KB): System plików SPIFFS.
  - `0x3FB000 - 0x3FC000` (4 KB): **Dedykowany sektor emulacji pamięci EEPROM**.
  - `0x3FC000 - 0x400000` (16 KB): Parametry systemowe SDK Espressif (kalibracja RF, konfiguracja Wi-Fi).

#### Gwarancja Fizyczna:
Zarówno `ArduinoOTA`, jak i biblioteka `Update` (używana przez Web OTA) dokonują zapisu wyłącznie w obszarze drugiego slotu aplikacji (`ESP.getFreeSketchSpace()`). Sektor pamięci EEPROM (`0x3FB000`) leży daleko poza obszarem binariów aplikacji i **nie jest kasowany ani modyfikowany podczas bezprzewodowego flashowania**.

### 7.2. Struktura `ControllerConfig` i Problem Integralności Logicznej
W pliku `firmware/wemos_controller/src/storage_manager.h`:
```cpp
struct ControllerConfig {
    uint32_t magic;               // 0x41515541 ("AQUA")
    uint16_t version;             // 1
    float target_temp;            // np. 25.0 °C
    float hysteresis;             // np. 0.3 °C
    float ph_cal_offset;          // offset kalibracyjny elektrody pH
    float ph_cal_slope;           // nachylenie charakterystyki elektrody pH
    DeviceSchedule schedules[4];  // 4 harmonogramy (światło, filtr, napowietrzacz, CO2)
    uint32_t checksum;            // CRC32
};
```
Całkowity rozmiar struktury: **48 bajtów**.

#### Zidentyfikowane Ryzyko Utraty Danych Użytkownika:
W obecnej implementacji `StorageManager::loadConfig()`:
```cpp
EEPROM.get(0, cfg);
if (cfg.magic != CONFIG_MAGIC || cfg.version != CONFIG_VERSION) {
    resetToDefaults(cfg);
    saveConfig(cfg);
    return false;
}
```
Jeśli programista rozszerzy strukturę `ControllerConfig` (np. o zapisane poświadczenia Wi-Fi lub flagi konfiguracyjne) i podbije `CONFIG_VERSION` z `1` na `2`, wówczas po wgraniu nowego firmware warunek `version != CONFIG_VERSION` spowoduje natychmiastowe wywołanie `resetToDefaults()`.
**Skutek:** Skasowanie wielopunktowej kalibracji sondy pH (która wymaga buforów pH 4.01 i 7.00 oraz czasu) oraz reset wszystkich harmonogramów czasowych!

### 7.3. Rekomendacja Architektoniczna: Automatyczna Migracja Schematu (V1 -> V2)
Aby zagwarantować 100% trwałości konfiguracji użytkownika, `StorageManager` musi posiadać ścieżkę migracji wstecznej:

```cpp
// Definicja historycznej wersji struktury V1:
struct ControllerConfigV1 {
    uint32_t magic;
    uint16_t version;
    float target_temp;
    float hysteresis;
    float ph_cal_offset;
    float ph_cal_slope;
    DeviceSchedule schedules[4];
    uint32_t checksum;
};

// Nowa wersja struktury V2:
struct ControllerConfigV2 {
    uint32_t magic;
    uint16_t version;             // 2
    float target_temp;
    float hysteresis;
    float ph_cal_offset;
    float ph_cal_slope;
    DeviceSchedule schedules[4];
    char wifi_ssid[33];           // Nowe pole: zapamiętane SSID dla OTA
    char wifi_pass[65];           // Nowe pole: zapamiętane hasło
    uint32_t checksum;
};
```

#### Algorytm Ładowania i Migracji:
1. Odczytaj nagłówek (pierwsze 6 bajtów: `magic` i `version`).
2. Jeśli `magic == CONFIG_MAGIC`:
   - Jeśli `version == 1`:
     - Odczytaj `ControllerConfigV1`.
     - Zweryfikuj sumę CRC32 dla V1.
     - Przepisz parametry: `target_temp`, `hysteresis`, `ph_cal_offset`, `ph_cal_slope`, `schedules` do nowej struktury V2.
     - Zainicjalizuj nowe pola domyślnymi wartościami (puste łańcuchy znaków dla Wi-Fi).
     - Ustaw `version = 2`, oblicz nowy CRC32 i zapisz do EEPROM (`saveConfig`).
   - Jeśli `version == 2`:
     - Odczytaj `ControllerConfigV2` i zweryfikuj CRC32.
3. Tylko w przypadku uszkodzenia sumy kontrolnej lub braku sygnatury "AQUA" załaduj fabryczne `resetToDefaults()`.

---

## 8. Analiza i Plan Aktualizacji Dokumentacji Obsidian

### 8.1. Stan Obecny Pliku `obsidian/Procedura - Programowanie i Wgrywanie Firmware.md`
Obecny dokument (63 linie) opisuje wyłącznie pierwotną procedurę wgrywania kablami USB z koniecznością rozwierania zworek:
- Krok 1: Zdjęcie zworek `J_TX`, `J_RX`.
- Krok 2: Wgranie do WeMos przez kabel microUSB (`pio run -e wemos_d1_uno -t upload`).
- Krok 3: Wgranie do CYD przez USB (`pio run -e cyd_2_8 -t upload`).
- Krok 4: Założenie zworek z powrotem i restart 12V.
- Sekcja ostrzegawcza o kolizji konwerterów USB-UART CH340 / CP2102.

### 8.2. Wymagane Zmiany w Bazie Wiedzy Obsidian
Dokument `Procedura - Programowanie i Wgrywanie Firmware.md` musi zostać zreorganizowany w hierarchiczny przewodnik, w którym:
1. **Główną i domyślną procedurą staje się Bezprzewodowa Aktualizacja OTA**:
   - Brak konieczności otwierania pokrywy AquaBox.
   - Zworki UART pozostają **na stałe zwarte (założone)**.
   - Wgrywanie firmware przez przeglądarkę WWW (Web OTA) oraz z poziomu PlatformIO (ArduinoOTA).
2. **Procedura USB zostaje zachowana jako Procedura Awaryjna (Disaster Recovery)**:
   - Dedykowana wyłącznie w sytuacji uszkodzenia bootloadera, wgrania wadliwego obrazu blokującego sieć lub pierwszego uruchomienia czystej płytki ("fabryczny bootstrap").
3. **Aktualizacja Powiązanych Notatek:**
   - `obsidian/Hardware - Zworki UART i Izolacja.md`: zmiana opisu – zworki są demontowane wyłącznie w trybie Disaster Recovery, a podczas normalnej eksploatacji i bezprzewodowego programowania pozostają zwarte.
   - `obsidian/Protokół - Katalog Komend i Sterowanie.md`: dodanie wpisu dla komendy `enter_ota`.
   - `obsidian/Protokół - Potwierdzenia ACK i Błędy.md`: dodanie formatu ramki potwierdzenia wejścia w tryb OTA.
   - `obsidian/Protokół - Struktura Telemetrii.md`: opis nowych pól `"ota"` oraz `"ota_ip"`.
   - `obsidian/Firmware - WeMos Pamięć EEPROM i Storage.md`: opis gwarancji trwałości konfiguracji i mechanizmu migracji V1 -> V2.
   - `obsidian/MOC - Bezpieczeństwo i Fail-Safe.md`: dodanie procedury pre-OTA relay cutoff i 5-minutowego watchdog'a powrotnego.

---

## 9. Mapa Drogowa Wdrożenia (Roadmap)

### Faza 1: Warstwa Protokołu Współdzielonego (`firmware/shared/uart_protocol`)
1. W pliku `uart_protocol.h`:
   - Dodać wartość `EnterOta` do enumu `CommandType`.
   - Dodać pola `ota_ssid` i `ota_pass` do struktury `AquariumCommand`.
   - Dodać pola `bool ota_mode` i `char ota_ip[16]` do struktury `AquariumTelemetry`.
2. W pliku `uart_protocol.cpp`:
   - Zaimplementować serializację i deserializację komendy `enter_ota`.
   - Zaktualizować `serializeTelemetry()` i `parseMessage()` o obsługę nowych pól telemetrii.

### Faza 2: Oprogramowanie WeMos D1 Uno (`firmware/wemos_controller`)
1. W pliku `wemos_config.h`:
   - Zdefiniować pin przycisku awaryjnego: `constexpr uint8_t PIN_BTN_OTA = 12; // D6`
2. W plikach `storage_manager.h` / `.cpp`:
   - Wdrożyć strukturę `ControllerConfigV2` oraz automatyczną migrację z wersji V1 w metodzie `loadConfig()`.
3. W pliku `main.cpp`:
   - Zaimplementować funkcję `enter_ota_service_mode()`:
     - Wywołanie `g_hardware_i2c.setAllRelays(0x00)` (natychmiastowe odcięcie obciążeń).
     - Ustawienie `safe_mode_active = true;` i `telemetry_uart_enabled = false;`.
     - Wybudzenie radia Wi-Fi (`WiFi.forceSleepWake()`).
     - Próba połączenia ze stacją STA (z timeoutem 10 s); przy niepowodzeniu podniesienie AP `AquaBox-OTA` (hasło: brak lub standardowe).
     - Wysłanie ramki ACK z uzyskanym adresem IP i `Serial.flush()`.
     - Inicjalizacja `ESP8266WebServer` na porcie 80 z endpointem `/update`.
     - Inicjalizacja `ArduinoOTA.begin()`.
     - Aktywacja watchdoga 5-minutowego.
   - W funkcji `setup()`: sprawdzanie pinu D6 przy starcie.
   - W pętli `loop()`: obsługa komendy `EnterOta`, obsługa klientów HTTP i ArduinoOTA w trybie serwisowym.

### Faza 3: Oprogramowanie Panelu ESP32 CYD (`firmware/cyd_controller`)
1. W plikach `uart_client.h` / `.cpp`:
   - Dodać metodę `sendEnterOta(const char *ssid = nullptr, const char *pass = nullptr)`.
   - Dodać buforowanie i obsługę komunikatów ACK w `processLine()`.
   - Zaimplementować wstrzymywanie timeoutu braku telemetrii, gdy WeMos zgłasza tryb OTA.
2. W interfejsie graficznym LVGL (`gui_app.cpp`):
   - Dodać kafelek serwisowy w menu ustawień z przyciskami:
     - *"Aktualizuj Panel (CYD)"* -> przełączenie w portal OTA panelu.
     - *"Aktualizuj AquaBox (WeMos)"* -> wysłanie `enter_ota` i wyświetlenie instrukcji z adresem IP.

### Faza 4: Aktualizacja Dokumentacji Obsidian
1. Zaktualizować plik `obsidian/Procedura - Programowanie i Wgrywanie Firmware.md` o kompletne instrukcje bezprzewodowe Web OTA i ArduinoOTA.
2. Wprowadzić powiązane korekty w `Hardware - Zworki UART i Izolacja.md`, `Protokół - Katalog Komend i Sterowanie.md`, `Protokół - Struktura Telemetrii.md`, `Firmware - WeMos Pamięć EEPROM i Storage.md` oraz `MOC - Bezpieczeństwo i Fail-Safe.md`.
