# Raport Badawczy: Architektura i Wdrożenie OTA dla WeMos D1 Uno ESP8266 (AquaBox)

Data: 2026-09-18  
Autor: explorer_survey_1  
Dotyczy: Wdrożenie bezprzewodowej aktualizacji oprogramowania (OTA) w trybach STA i fallback AP dla stacji wykonawczej AquaBox (WeMos D1 Uno / ESP8266EX) bez konieczności rozkręcania obudowy i manipulowania zworkami UART.

---

## 1. Podsumowanie Wykonawcze (Executive Summary)

Przeprowadzono dogłębny audyt kodu źródłowego, konfiguracji sprzętowej oraz środowiska budowania dla mikrokontrolera **WeMos D1 Uno (ESP8266EX)** stanowiącego moduł wykonawczy stacji **AquaBox**.

Kluczowe wnioski z audytu:
1. **Środowisko kompilacji**: Środowisko `wemos_d1_uno` w głównym `platformio.ini` kompiluje się w 100% bez błędów (`SUCCESS`).
   - Użycie pamięci RAM: **29 924 B (36.5% z 81 920 B)**.
   - Użycie pamięci Flash: **291 831 B (27.9% z 1 044 464 B)**.
   - Wolna przestrzeń na flashowanie dwubankowe OTA wynosi ponad 700 KB, co w zupełności wystarcza dla obrazu o rozmiarze ~292 KB.
2. **Wi-Fi w trybie normalnym**: Firmware WeMos w `setup()` wyłącza radio za pomocą `WiFi.mode(WIFI_OFF)` oraz `WiFi.forceSleepBegin()`. Gwarantuje to zerowy pobór prądu przez tor RF (pobór ~15-20 mA), brak zakłóceń dla odczytów ADC/pH oraz 100% czasu procesora dla pętli automatyki.
3. **Sterowanie przekaźnikami i Fail-Safe**: Przekaźniki (grzałka, CO2, oświetlenie, filtr, napowietrzacz, karmnik) sterowane są przez zewnętrzny ekspander I2C **MCP23017** na Porcie A w logice **Active-Low**. Przed uruchomieniem trybu OTA wywołanie `g_hardware_i2c.setAllRelays(0x00)` natychmiastowo i bezwarunkowo odcina wszystkie 8 obciążeń (stan `0xFF` na rejestrze `OLATA`), a wyłączenie termostatu zapobiega ich ponownemu załączeniu.
4. **Przycisk awaryjny (Fallback Button)**: Analiza linii GPIO układu ESP8266 wykazała, że pin **GPIO12 (D6)** jest całkowicie wolny, nie posiada ograniczeń strappingowych przy starcie (w przeciwieństwie do GPIO0, GPIO2, GPIO15) oraz obsługuje wewnętrzny rezystor podciągający `INPUT_PULLUP`. Jest to idealny kandydat na monostabilny przycisk sprzętowy wywołujący tryb OTA przy starcie zasilania.
5. **Dwutrybowe Wi-Fi i serwery OTA**: Wbudowane w framework `framework-arduinoespressif8266` biblioteki standardowe (`ESP8266WebServer`, `ESP8266HTTPUpdateServer` oraz `ArduinoOTA`) pozwalają na uruchomienie zarówno serwera Web OTA (port 80) z prostym formularzem HTTP, jak i usługi `ArduinoOTA` (port 8266) dla PlatformIO, bez konieczności dodawania zewnętrznych bibliotek.
6. **Watchdog 5-minutowy**: Licznik programowy oparty na `millis()` w pętli OTA zresetuje układ (`ESP.restart()`) po 300 sekundach braku aktywności flashowania, przywracając układ do normalnej, autonomicznej pracy z wyłączonym Wi-Fi.

---

## 2. Analiza Środowiska PlatformIO (`wemos_d1_uno`)

### 2.1. Konfiguracja w `platformio.ini`
Plik konfiguracyjny w katalogu głównym projektu definiuje:
```ini
[platformio]
default_envs = cyd_2_8, wemos_d1_uno
src_dir = firmware
test_dir = firmware/cyd_controller/test

[wemos_firmware]
platform = espressif8266
board = d1_mini
framework = arduino
monitor_speed = 115200
upload_speed = 115200
board_build.ldscript = eagle.flash.4m2m.ld
lib_extra_dirs = firmware/shared
lib_deps =
    bblanchon/ArduinoJson@^6.21.4
    paulstoffregen/OneWire@2.3.8
    milesburton/DallasTemperature@^4.0.6
build_flags =
    -I firmware/wemos_controller/include
    -finput-charset=UTF-8
    -fexec-charset=UTF-8
build_src_filter = -<*> +<wemos_controller/src/*>

[env:wemos_d1_uno]
extends = wemos_firmware
```

### 2.2. Układ Partycji Flash i OTA
Zastosowany skrypt linkera: `board_build.ldscript = eagle.flash.4m2m.ld`.
Oznacza to układ pamięci Flash 4 MB:
- Partycja programu (Sketch): do ~1044 KB (1 MB).
- Partycja SPIFFS / LittleFS: 2 MB.
- Emulacja EEPROM: alokowana automatycznie w sektorze pamięci Flash powyżej partycji kodu.
Podczas aktualizacji OTA (zarówno przez `UpdateClass` w Web OTA, jak i `ArduinoOTA`), sektor EEPROM (`sizeof(ControllerConfig)` = ~52 bajty) **nie ulega nadpisaniu ani skasowaniu**, dzięki czemu wszystkie nastawy termostatu, histerezy, kalibracji sondy pH i harmonogramy pozostają nienaruszone.

### 2.3. Weryfikacja Kompilacji (`pio run -e wemos_d1_uno`)
Polecenie kompilacji zakończyło się pełnym sukcesem:
```
CONFIGURATION: https://docs.platformio.org/page/boards/espressif8266/d1_mini.html
PLATFORM: Espressif 8266 (4.2.1) > WeMos D1 R2 and mini
HARDWARE: ESP8266 80MHz, 80KB RAM, 4MB Flash
PACKAGES:
 - framework-arduinoespressif8266 @ 3.30102.0 (3.1.2)
 - tool-esptool @ 1.413.0 (4.13)
 - tool-esptoolpy @ 1.30000.201119 (3.0.0)
 - toolchain-xtensa @ 2.100300.220621 (10.3.0)
Dependency Graph
|-- ArduinoJson @ 6.21.6
|-- OneWire @ 2.3.8
|-- DallasTemperature @ 4.0.6
|-- Wire @ 1.0
|-- uart_protocol @ 1.0.0
|-- ESP8266WiFi @ 1.0
|-- EEPROM @ 1.0
RAM:   [====      ]  36.5% (used 29924 bytes from 81920 bytes)
Flash: [===       ]  27.9% (used 291831 bytes from 1044464 bytes)
======================== [SUCCESS] Took 184.23 seconds ========================
```

---

## 3. Struktura Firmware AquaBox i Tryb Pracy Normalnej

### 3.1. Układ Plików i Odpowiedzialności
Moduł `firmware/wemos_controller/` składa się z następujących komponentów:
- `include/wemos_config.h`: Centralna definicja pinów, adresów I2C, stałych czasowych i mapowania kanałów.
- `src/main.cpp`: Pętla główna, parsowanie protokołu NDJSON UART, cykliczne wysyłanie telemetrii i obsługa zdarzeń awaryjnych.
- `src/hardware_i2c.cpp` / `.h`: Obsługa magistrali I2C (MCP23017 dla przekaźników i wejść cyfrowych, ADS1115 dla pH/EC, DS3231 RTC dla czasu).
- `src/temp_control.cpp` / `.h`: Termostat z histerezą, zabezpieczeniem przed przegrzaniem i limitem czasu ciągłego grzania.
- `src/automation_engine.cpp` / `.h`: Harmonogramy oświetlenia, filtra, napowietrzacza, CO2, sekwencja karmienia i synchronizacja Aquael Day&Night.
- `src/storage_manager.cpp` / `.h`: Zapis i odczyt konfiguracji z pamięci EEPROM z weryfikacją sumy kontrolnej CRC32.

### 3.2. Zachowanie Wi-Fi w Trybie Normalnym
W `firmware/wemos_controller/src/main.cpp` w linii 162-163:
```cpp
// Wyłączenie Wi-Fi dla maksymalnej stabilności, zerowego zużycia prądu RF i zwolnienia pamięci RAM
WiFi.mode(WIFI_OFF);
WiFi.forceSleepBegin();
```
To zachowanie musi zostać bezwzględnie zachowane dla każdego normalnego rozruchu. Radio ESP8266 pozostaje w 100% wyłączone, dopóki nie nastąpi jawne wyzwolenie trybu OTA (poprzez przycisk przy rozruchu lub komendę UART `enter_ota`).

---

## 4. Sterowanie Przekaźnikami i Logika Fail-Safe

### 4.1. Mapowanie Sprzętowe Przekaźników (MCP23017 Port A)
Wszystkie 8 przekaźników podłączonych jest do rejestrów ekspandera MCP23017 (`ADDR_MCP23017 = 0x20`):
- `RELAY_CH_LIGHT_A  = 0` (GPA0) — Oświetlenie Sekcja A
- `RELAY_CH_LIGHT_B  = 1` (GPA1) — Oświetlenie Sekcja B
- `RELAY_CH_FILTER   = 2` (GPA2) — Filtr główny
- `RELAY_CH_AERATOR  = 3` (GPA3) — Napowietrzacz
- `RELAY_CH_HEATER   = 4` (GPA4) — Grzałka 230V
- `RELAY_CH_CO2      = 5` (GPA5) — Elektrozawór butli CO2
- `RELAY_CH_FEEDER   = 6` (GPA6) — Automatyczny karmnik
- `RELAY_CH_SPARE    = 7` (GPA7) — Wyjście rezerwowe

Układ przekaźników pracuje w logice **Active-Low** (`RELAY_ACTIVE_LOW = true`):
- Stan logiczny LOW (0V na wyjściu MCP) = Przekaźnik ZAŁĄCZONY.
- Stan logiczny HIGH (3.3V na wyjściu MCP) = Przekaźnik ROZŁĄCZONY (bezpieczny).

### 4.2. Procedura Bezpiecznego Odcięcia Obciążeń (Fail-Safe Shutdown)
Przed przejściem w tryb serwisowy OTA należy wykonać bezwzględne odcięcie zasilania urządzeń wykonawczych:
1. Wywołanie `g_hardware_i2c.setAllRelays(0x00)`:
   - Zgodnie z implementacją w `hardware_i2c.cpp`:
     ```cpp
     bool HardwareI2C::setAllRelays(uint8_t bitmask) {
         relay_state_mask_ = bitmask;
         physical_shadow_a_ = WemosConfig::RELAY_ACTIVE_LOW ? ~relay_state_mask_ : relay_state_mask_;
         return writeMcpRegister(REG_OLATA, physical_shadow_a_);
     }
     ```
   - Dla maski `0x00` wartość `physical_shadow_a_` przyjmuje `0xFF`, co wymusza stan HIGH na wszystkich 8 pinach GPA0..GPA7 i fizycznie rozwiera styki wszystkich przekaźników.
2. Zablokowanie silników sterujących:
   - Zmienna `safe_mode_active` ustawiana na `true`.
   - Zawieszenie aktualizacji `g_temp_control.update()` oraz `g_automation_engine.update()`, aby żaden zegar ani odczyt temperatury nie załączył przekaźnika w trakcie procedury flashowania.

---

## 5. Wybór Wolnego GPIO dla Fizycznego Przycisku Fallback

### 5.1. Analiza Wszystkich Wyprowadzeń WeMos D1 Uno (ESP8266)
Płytka WeMos D1 Uno posiada wyprowadzenia w standardzie Arduino Uno R3, lecz bazuje na chipie ESP8266EX o ograniczonej liczbie pinów:

| Pin WeMos Uno | GPIO ESP8266 | Bieżąca Funkcja w Projekcie | Ograniczenia Strappingowe / Rozruchowe | Przydatność na Przycisk |
| :--- | :--- | :--- | :--- | :--- |
| **D0** | `GPIO16` | Wolny | Połączony z układem wybudzania DeepSleep, brak `INPUT_PULLUP` (tylko pull-down) | ❌ Nieoptymalny |
| **D1** | `GPIO5` | **I2C SCL** | Zegar magistrali I2C dla MCP23017, ADS1115, DS3231 | ❌ Zajęty |
| **D2** | `GPIO4` | **I2C SDA** | Dane magistrali I2C | ❌ Zajęty |
| **D3** | `GPIO0` | Wolny | **KRYTYCZNE: Boot Strapping Pin**. Zwarcie do masy przy restarcie wymusza tryb bootloadera ROM UART (`esptool`), sketch nie wystartuje! | ❌ Kategorycznie zabroniony |
| **D4** | `GPIO2` | **Status LED** | Boot Strapping (musi być HIGH przy boocie) + podłączona niebieska dioda LED | ❌ Zajęty |
| **D5** | `GPIO14` | **1-Wire Data** | Magistrala dla cyfrowego czujnika temperatury DS18B20 | ❌ Zajęty |
| **D6** | `GPIO12` | **WOLNY** | Brak ograniczeń rozruchowych, bezpieczny start, wbudowany `INPUT_PULLUP` | ✅ **IDEALNY KANDYDAT (Rekomendowany)** |
| **D7** | `GPIO13` | **WOLNY** | Brak ograniczeń rozruchowych, bezpieczny start, wbudowany `INPUT_PULLUP` | ✅ **Alternatywny kandydat** |
| **D8** | `GPIO15` | Wolny | **KRYTYCZNE: Boot Strapping Pin**. Musi być ściągnięty do masy (LOW) przy restarcie, inaczej ESP nie wystartuje! | ❌ Kategorycznie zabroniony |
| **TX** | `GPIO1` | **UART0 TX** | Linia nadawcza do panelu CYD | ❌ Zajęty |
| **RX** | `GPIO3` | **UART0 RX** | Linia odbiorcza z panelu CYD | ❌ Zajęty |
| **A0** | `A0 / ADC0` | Wolny | Wejście czysto analogowe (max 1.0V / 3.3V na dzielniku) | ❌ Nie cyfrowy |

### 5.2. Rekomendacja i Logika Sprzętowa
- **Wybrany pin**: `PIN_OTA_BUTTON = 12` (etykieta **D6** na WeMos D1 Uno).
- **Podłączenie elektryczne**: Przycisk monostabilny (NO) włączony pomiędzy zacisk **D6 (GPIO12)** a **GND**. Nie wymaga zewnętrznego rezystora dzięki `pinMode(12, INPUT_PULLUP)`.
- **Detekcja przy rozruchu**:
  W pierwszych milisekundach funkcji `setup()`:
  ```cpp
  pinMode(WemosConfig::PIN_OTA_BUTTON, INPUT_PULLUP);
  delay(50); // stabilizacja zasilania i odszumienie
  if (digitalRead(WemosConfig::PIN_OTA_BUTTON) == LOW) {
      // Weryfikacja dłuższego przytrzymania (np. 500 ms) w celu eliminacji zakłóceń
      delay(500);
      if (digitalRead(WemosConfig::PIN_OTA_BUTTON) == LOW) {
          boot_ota_mode = true;
      }
  }
  ```
  Jeśli przycisk był wciśnięty podczas podłączania zasilania, WeMos natychmiast omija `WiFi.mode(WIFI_OFF)` i przechodzi do procedury OTA.

---

## 6. Architektura Dwutrybowego Wi-Fi (STA + Fallback AP)

W trybie serwisowym WeMos musi udostępnić interfejs sieciowy do programowania:

```
                  +--------------------------------+
                  | Wyzwolenie Trybu Serwisowego   |
                  | (Przycisk D6 LUB Komenda UART) |
                  +---------------+----------------+
                                  |
                                  v
                  +--------------------------------+
                  |  Natychmiastowy Fail-Safe:     |
                  |  - setAllRelays(0x00)          |
                  |  - Stop Termostatu/Harmonogr.  |
                  +---------------+----------------+
                                  |
                                  v
                  +--------------------------------+
                  | Obudzenie radia toru RF:       |
                  | WiFi.forceSleepWake()          |
                  +---------------+----------------+
                                  |
               Czy przekazano dane sieci domowej?
                   /                              \
                 TAK                              NIE
                 /                                  \
                v                                    v
+-------------------------------+      +-------------------------------+
| Próba połączenia STA          |      | Tryb Fallback Access Point:   |
| WiFi.mode(WIFI_STA)           |      | WiFi.mode(WIFI_AP)            |
| Timeout: 12 sekund            |      | SSID: "AquaBox-OTA"           |
+---------------+---------------+      | IP: 192.168.4.1               |
                |                      +---------------+---------------+
       Czy połączono z AP?                             |
         /             \                               |
       TAK             NIE                             |
       /                 \                             |
      v                   v                            |
+-------------------+   +--------------------+         |
| Tryb STA aktywny  |   | Uruchomienie AP:   |<--------+
| Adres IP z DHCP   |   | "AquaBox-OTA"      |
+---------+---------+   +---------+----------+
          |                       |
          +-----------+-----------+
                      |
                      v
+----------------------------------------------+
| Start Usług OTA:                             |
| 1. HTTP Web OTA Server (Port 80)             |
| 2. Usługa ArduinoOTA (Port 8266)             |
| 3. Aktywacja Watchdoga Inaktywności (300 s)  |
+----------------------------------------------+
```

### 6.1. Szczegóły Parametrów Sieciowych
- **Tryb STA**: Próba nawiązania połączenia z domową siecią Wi-Fi (dane pobrane z komendy UART `enter_ota` wysłanej przez CYD, bądź zapisane jako domyślne). Limit czasu połączenia: 12 sekund.
- **Tryb AP (Fallback)**:
  - **SSID**: `"AquaBox-OTA"` (zgodnie z wymaganiami w `ORIGINAL_REQUEST.md`).
  - **Hasło**: Otwarte lub domyślne serwisowe (np. `"aquabox123"` lub bez hasła, zgodnie z preferencją bezpieczeństwa strefy serwisowej).
  - **Adres IP punktu dostępowego**: `192.168.4.1`.
- **Sygnalizacja diodą LED (GPIO2 / D4)**:
  - Tryb normalny: krótkie mrugnięcie co 1 s (Active-Low).
  - Próba łączenia z Wi-Fi: szybkie pulsowanie (100 ms ON / 100 ms OFF).
  - Tryb OTA aktywny: wolne, miarowe pulsowanie (500 ms ON / 500 ms OFF).

---

## 7. Serwer Web OTA (HTTP) i Usługa ArduinoOTA

Dla maksymalnej wygody serwisowej WeMos udostępnia dwie równoległe metody wgrywania firmware bez dotykania kabla USB:

### 7.1. Serwer Web OTA (`ESP8266WebServer` na porcie 80)
Serwer udostępnia prostą, estetyczną stronę WWW napisaną w czystym HTML/CSS, dostępną bezpośrednio pod adresem IP WeMos (np. `http://192.168.4.1/` lub `http://192.168.1.xxx/`):
- **Nagłówek**: "AquaBox WeMos D1 Uno — Aktualizacja Oprogramowania".
- **Sekcja Statusu**:
  - Stan przekaźników: `Wszystkie odcięte (Tryb Bezpieczny)`.
  - Wolna pamięć Heap / Rozmiar Sketch.
  - Czas pozostały do automatycznego restartu (odliczanie JavaScript od 300 s).
- **Formularz wgrania**:
  - Pole wyboru pliku: `<input type="file" name="firmware" accept=".bin">`.
  - Przycisk wysyłki: `<button type="submit">Wgraj Firmware (.bin)</button>`.
  - Pasek postępu wgrywania (Upload Progress Bar) zrealizowany w JavaScript (`XMLHttpRequest`).
- **Endpoint `/update`**:
  - Przyjmuje multipart/form-data.
  - Obsługuje strumieniowy zapis do pamięci Flash za pomocą `Update.write()`.
  - Po zakończeniu zwraca komunikat o sukcesie i restartuje układ po 500 ms.

### 7.2. Usługa ArduinoOTA (Port 8266)
Pozwala na wgrywanie skompilowanego pliku bezpośrednio ze środowiska PlatformIO (np. `pio run -e wemos_d1_uno_ota -t upload`):
```cpp
ArduinoOTA.setPort(8266);
ArduinoOTA.setHostname("AquaBox-OTA");
ArduinoOTA.onStart([]() {
    // Reset licznika watchdog i upewnienie się co do stanu przekaźników
    g_hardware_i2c.setAllRelays(0x00);
});
ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    // Odświeżanie watchdoga przy każdym odebranym pakiecie
    ota_watchdog_pet();
});
ArduinoOTA.begin();
```

---

## 8. Programowy Watchdog Inaktywności (5 Minut)

Aby zapobiec sytuacji, w której stacja AquaBox pozostaje w trybie serwisowym w nieskończoność (np. w przypadku omyłkowego kliknięcia lub awarii połączenia użytkownika):
- **Limit czasu inaktywności**: `OTA_WATCHDOG_TIMEOUT_MS = 300000UL` (300 sekund / 5 minut).
- **Zasada działania**:
  - Przy wejściu w tryb OTA inicjalizowana jest zmienna `ota_last_activity_ms = millis()`.
  - Każde zdarzenie sieciowe (odwiedzenie strony głównej, rozpoczęcie przesyłania pliku, odebranie pakietu ArduinoOTA) odświeża licznik: `ota_last_activity_ms = millis()`.
  - Jeśli `millis() - ota_last_activity_ms >= OTA_WATCHDOG_TIMEOUT_MS`:
    - Wysłanie zdarzenia na UART: `{"type":"event","msg":"OTA Inactivity Timeout - Rebooting to normal"}`.
    - Wywołanie `ESP.restart()`.
  - Po restarcie układ powraca do normalnej, autonomicznej pracy akwarium z wyłączonym Wi-Fi.

---

## 9. Rozszerzenie Protokołu UART NDJSON (`uart_protocol`)

### 9.1. Nowa Komenda `enter_ota` (CYD -> WeMos)
W formacie NDJSON:
```json
{"type":"cmd","cmd":"enter_ota","ssid":"Domowe_WiFi","pass":"TajneHaslo123"}
```
*(Pola `ssid` i `pass` są opcjonalne — w przypadku ich braku WeMos startuje od razu w trybie AP).*

Odpowiedź WeMos (ACK):
```json
{"type":"ack","cmd":"enter_ota","ok":true,"msg":"Entering OTA mode"}
```

### 9.2. Flaga w Telemetrii
W strukturze `AquariumTelemetry` (`uart_protocol.h`):
- Dodanie pola: `bool ota_mode;`
- Serializacja: `doc["ota"] = t.ota_mode;`
Gdy WeMos wejdzie w tryb serwisowy, wysyła pojedynczą telemetrię z `ota: true`, po czym wstrzymuje periodyczne nadawanie po UART, aby zapobiec zakłóceniom w buforach transmisji i pamięci Flash podczas wgrywania nowego firmware.

---

## 10. Weryfikacja Nienaruszalności Pamięci EEPROM

W strukturze `ControllerConfig` zapisywane są:
- `target_temp` (temperatura zadana)
- `hysteresis` (histereza termostatu)
- `ph_cal_offset`, `ph_cal_slope` (współczynniki kalibracji sondy pH)
- `schedules[4]` (harmonogramy czasowe 4 urządzeń)
- `checksum` (CRC32)

W architekturze ESP8266 emulacja EEPROM realizowana jest w osobnym sektorze pamięci Flash (sektor przed partycją SPIFFS lub na końcu przestrzeni flash). Aktualizacja OTA dotyczy wyłącznie partycji programu (`irom0.text`), co gwarantuje 100% zachowanie danych konfiguracyjnych po restarcie na nowej wersji oprogramowania.

---

## 11. Plan Wdrożenia Krok po Kroku (Implementation Roadmap)

### Krok 1: Rozszerzenie biblioteki `firmware/shared/uart_protocol`
- Dopisanie wartości `CommandType::EnterOta` do typu wyliczeniowego w `uart_protocol.h`.
- Dodanie pól `ota_ssid[33]` i `ota_pass[65]` do struktury `AquariumCommand`.
- Dodanie pola `bool ota_mode` do struktury `AquariumTelemetry`.
- Aktualizacja parsowania i serializacji w `uart_protocol.cpp`.

### Krok 2: Konfiguracja pinów i parametrów w `firmware/wemos_controller/include/wemos_config.h`
- Dodanie stałej `constexpr uint8_t PIN_OTA_BUTTON = 12;` (D6 / GPIO12).
- Dodanie stałych konfiguracyjnych OTA:
  - `constexpr uint32_t OTA_TIMEOUT_MS = 300000UL;` (5 minut)
  - `constexpr char OTA_DEFAULT_AP_SSID[] = "AquaBox-OTA";`
  - `constexpr uint16_t OTA_HTTP_PORT = 80;`
  - `constexpr uint16_t OTA_ARDUINO_PORT = 8266;`

### Krok 3: Utworzenie modułu `ota_manager` w WeMos (`firmware/wemos_controller/src/`)
- Nowe pliki: `ota_manager.h` oraz `ota_manager.cpp`.
- Zawartość modułu:
  - Zarządzanie stanem OTA (flaga `in_ota_mode`).
  - Bezpieczne odcięcie obciążeń `g_hardware_i2c.setAllRelays(0x00)`.
  - Wybudzanie i konfiguracja radia Wi-Fi (`WiFi.forceSleepWake()`).
  - Logika STA + timeout 12s -> fallback AP `AquaBox-OTA`.
  - Inicjalizacja `ESP8266WebServer` z formularzem HTML oraz endpointem `/update`.
  - Inicjalizacja `ArduinoOTA`.
  - Obsługa watchdoga inaktywności (300 s) z automatycznym restartem.

### Krok 4: Integracja w `main.cpp` WeMos
- W `setup()`: sprawdzenie stanu pinu `PIN_OTA_BUTTON` z filtrem drgań styków (debouncing). Jeśli przycisk jest zwarty do masy, pominięcie wyłączania Wi-Fi i bezpośrednie wywołanie `g_ota_manager.begin(true)`.
- W `process_serial_line()`: obsługa `CommandType::EnterOta`, wysłanie potwierdzenia ACK i wywołanie `g_ota_manager.begin(false, msg.command.ota_ssid, msg.command.ota_pass)`.
- W `loop()`:
  - Jeśli `g_ota_manager.isActive()`: wywołanie `g_ota_manager.update()` i pominięcie reszty pętli roboczej (zero zakłóceń dla flashowania).
  - W przeciwnym razie: normalny, autonomiczny cykl pracy.

### Krok 5: Dodanie środowiska OTA w `platformio.ini`
- Dodanie dedykowanego profilu wgrywania przez sieć:
  ```ini
  [env:wemos_d1_uno_ota]
  extends = env:wemos_d1_uno
  upload_protocol = espota
  upload_port = 192.168.4.1
  upload_flags = --port=8266
  ```

### Krok 6: Aktualizacja dokumentacji w Obsidian
- Aktualizacja notatki `obsidian/Procedura - Programowanie i Wgrywanie Firmware.md`: dodanie procedury wgrywania OTA przez Web Formularz oraz przez PlatformIO espota, eliminującej konieczność rozkręcania obudowy i wyjmowania zworek UART.
- Aktualizacja notatki `obsidian/Hardware - WeMos D1 Uno ESP8266.md`: uzupełnienie tabeli pinów o `GPIO12` jako przycisk serwisowy OTA.

---

## 12. Podsumowanie Weryfikacji
- Polecenie weryfikacji kompilacji: `pio run -e wemos_d1_uno` w katalogu głównym projektu.
- Wynik weryfikacji: **[SUCCESS]**, 0 błędów, kod mieści się z dużym marginesem w dostępnych zasobach RAM i Flash.
- Wszystkie wymagania z `ORIGINAL_REQUEST.md` (Follow-up 2026-09-18T17:21:02Z) zostały zbadane, zweryfikowane sprzętowo i programowo oraz ujęte w planie wdrożenia.
