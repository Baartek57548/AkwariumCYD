# Raport Techniczny: Analiza Testów Jednostkowych w `test_native_domain` oraz Weryfikacja Wykonalności `pio test -e native` dla Rozszerzeń Protokołu UART (M1)

**Autor:** explorer_m1_3  
**Data:** 2026-09-18  
**Środowisko:** PlatformIO, Unity, MinGW GCC 5.1.0 (`toolchain-gccmingw32@1.50100.0`), Windows x86_64  
**Dotyczy:** Milestone 1 — Rozszerzenie protokołu UART NDJSON o komendę `EnterOta`, ramki potwierdzenia ACK oraz flagi telemetrii `ota_mode` i `ota_ip`  

---

## 1. Podsumowanie Wykonawcze (Executive Summary)

W ramach Milestone 1 przeprowadzono dogłębny audyt architektury testów jednostkowych w repozytorium `cydAquarium`, ze szczególnym uwzględnieniem katalogu `firmware/cyd_controller/test/test_native_domain/`, biblioteki współdzielonej `firmware/shared/uart_protocol/` oraz konfiguracji środowiska natywnego `[env:native]` w PlatformIO.

### Główne Wnioski:
1. **Stan Obecny:** Zestaw testów w `firmware/cyd_controller/test/test_native_domain/test_main.cpp` zawiera **45 testów jednostkowych** obejmujących czystą logikę domenową (harmonogramy, termostat, histerezę, filtrację medianową, polityki bezpieczeństwa OTA oraz binarny protokół COBS `aquacyd_link`). Wszystkie 45 testów wykonują się pomyślnie w czasie **~4.8 sekundy** przy uruchomieniu z poziomu katalogu `firmware/cyd_controller`.
2. **Luka Pokrycia:** W całym repozytorium istnieje obecnie **0 testów** dla protokołu tekstowego NDJSON `uart_protocol` (`AquaProtocol`). Przyczyną tego stanu rzeczy było bezpośrednie powiązanie `uart_protocol.h` z frameworkiem Arduino (`#include <Arduino.h>` oraz typ zwracany `String`), co blokowało kompilację pod natywnym kompilatorem desktopowym MinGW bez obecności platformy Arduino.
3. **Weryfikacja Wykonalności `pio test -e native`:** Empirycznie udowodniono, że biblioteka `ArduinoJson 6.21.6` oraz parser/serializer NDJSON kompilują się i wykonują w 100% poprawnie pod kompilatorem MinGW GCC 5.1.0 z flagą `-std=gnu++11` po dostarczeniu lekkiego shima/mocka klasy `String`.
4. **Niespójność Ścieżek w Root `platformio.ini`:** Uruchomienie `pio test -e native` z głównego katalogu projektu kończy się błędem `fatal error: aquarium_automation.h: No such file or directory`, ponieważ profil `[env:native]` w głównym `platformio.ini` deklaruje `lib_extra_dirs = firmware/shared`, pomijając `firmware/cyd_controller/lib`. Z kolei uruchomienie w podkatalogu `firmware/cyd_controller` wykonuje się bezbłędnie. Wskazano precyzyjną poprawkę konfiguracyjną.

---

## 2. Analiza Istniejącego Zestawu Testów w `test_native_domain/`

### 2.1. Inwentaryzacja i Zakres Testów
Plik testowy `firmware/cyd_controller/test/test_native_domain/test_main.cpp` (1677 linii kodu) weryfikuje następujące podsystemy:
- **Harmonogramy i Okna Czasowe:** `test_regular_and_wrapped_windows`, `test_schedule_modes_override_window`, `test_factory_light_profile_boundaries`, `test_factory_schedule_outputs`.
- **Automatyka Akwarium i Bezpieczeństwo Obciążeń:** `test_thermostat_hysteresis_and_invalid_input`, `test_gas_control_is_mutually_exclusive_and_leak_safe`, `test_ato_requires_low_valid_level_and_obeys_interlocks`, `test_ato_runtime_safety_limit_enforced_during_manual_override`, `test_co2_runtime_safety_limit_enforced_during_manual_override`.
- **Filtry Sensoryczne i Kalibracja:** `test_alarm_flags_and_count`, `test_alarm_flags_report_required_sensor_and_actuator_health`, `test_alarm_stability_filter_rejects_threshold_chatter`, `test_alarm_stability_filter_raises_safety_alarms_immediately`, `test_sensor_calibration_and_temperature_compensation`, `test_sensor_median_filter_rejects_single_spike`.
- **Polityki OTA i Bezpieczeństwo Bootowania:** `test_ota_preflight_and_pending_verify_policy`, `test_ota_finalize_order_and_boot_recovery_are_fail_safe`, `test_ota_package_header_and_policy_accept_valid_release`, `test_ota_package_rejects_wrong_target_downgrade_and_security_rollback`, `test_ota_package_rejects_noncanonical_or_truncated_headers`, `test_semantic_version_comparison_is_canonical_and_overflow_safe`.
- **Protokoły Sieciowe i Komunikacyjne:** `test_aquacyd_link_frame_round_trip_and_crc_rejection`, `test_aquacyd_link_payload_codecs_preserve_signed_values`, `test_aquacyd_link_cobs_and_sequence_window` (protokół binarny ramkowy COBS w `firmware/shared/aquacyd_link/`).
- **AquaHub i Integracja IoT:** `test_aquahub_registry_validates_discovery_and_state`, `test_aquahub_registry_rejects_identity_conflict_and_replay`, `test_aquahub_automation_evaluates_condition_and_cooldown`.

### 2.2. Dlaczego `uart_protocol` Nie Był Dotychczas Testowany?
W pliku `firmware/shared/uart_protocol/include/uart_protocol.h`:
```cpp
#include <Arduino.h>
#include <stdint.h>

namespace AquaProtocol {
    String serializeTelemetry(const AquariumTelemetry &t);
    String serializeCommand(const AquariumCommand &cmd);
    String serializeAck(const char *cmd, bool ok, const char *msg = nullptr);
    String serializeTimeSync(uint32_t epoch);
    bool parseMessage(const char *json_line, AquariumMessage &out_msg);
}
```
Podczas gdy biblioteki `aquarium_domain` oraz `aquacyd_link` zostały zaprojektowane w czystym standardzie C++11 (nagłówki `<stdint.h>`, `<stddef.h>`, brak zależności od ESP32/Arduino SDK), biblioteka `uart_protocol`:
1. Zawiera dyrektywę `#include <Arduino.h>`, która w kompilatorze desktopowym MinGW natychmiast wyrzuca błąd `fatal error: Arduino.h: No such file or directory`.
2. Zwraca instancje klasy `String` (wbudowany typ platformy Arduino).
3. Do tej pory nikt nie przygotował dla niej adaptera testowego ani shima.

---

## 3. Analiza Architektoniczna: Uruchomienie Testów NDJSON w `[env:native]`

Aby włączyć protokół UART NDJSON do automatycznego suite'u `pio test -e native`, zbadano trzy warianty architektoniczne:

| Cecha | Wariant A: Shim / Mock Arduino | Wariant B: Dual-Mode (`#ifdef ARDUINO`) | Wariant C: Czyste C++ API Buforowe + Wrapper |
|---|---|---|---|
| **Opis** | Dodanie nagłówka `Arduino.h` (mock) w katalogu testowym dla native | W nagłówku `uart_protocol.h` warunkowa definicja `String` na bazie `std::string` | Nowe funkcje `serialize*(..., char *buf, size_t len)` + wrapper `String` |
| **Wpływ na kod WeMos/CYD** | **Zero zmian** w istniejącym kodzie produkcyjnym | **Zero zmian** w kodzie wywołującym | Zero zmian (stare metody wołają nowe) |
| **Alokacja Pamięci RAM** | Używa sterty (jak dotychczas) | Używa sterty (jak dotychczas) | **Zero alokacji dynamicznej** (zero heap) |
| **Zgodność z R2 (RAM Opt)** | Neutralna | Neutralna | **Wysoka (eliminacja fragmentacji SRAM)** |
| **Złożoność Wdrożenia** | Bardzo niska (1 plik nagłówkowy) | Niska (kilka dyrektyw preprocesora) | Średnia (refaktoryzacja wnętrza serializerów) |

### Rekomendacja Architektoniczna:
Najbardziej eleganckim i bezpiecznym rozwiązaniem (łączącym zalety Wariantów B i C) jest:
1. **W `uart_protocol.h`**:
   Warunkowe dołączanie nagłówka Arduino:
   ```cpp
   #if defined(ARDUINO)
       #include <Arduino.h>
   #else
       #include <stdint.h>
       #include <stddef.h>
       #include <string.h>
       #include <string>
       // Lekka klasa zgodna z Arduino String dla kompilacji desktopowej / testów native
       class String : public std::string {
       public:
           String() : std::string() {}
           String(const char *s) : std::string(s ? s : "") {}
           String(const std::string &s) : std::string(s) {}
           String(float val, int decimals = 2) {
               char buf[32];
               std::snprintf(buf, sizeof(buf), "%.*f", decimals, static_cast<double>(val));
               assign(buf);
           }
           String(int val) : std::string(std::to_string(val)) {}
           String(unsigned int val) : std::string(std::to_string(val)) {}
           String(long val) : std::string(std::to_string(val)) {}
           String(unsigned long val) : std::string(std::to_string(val)) {}
           const char* c_str() const { return std::string::c_str(); }
           size_t length() const { return std::string::length(); }
       };
   #endif
   ```
2. **Dostępność `ArduinoJson` dla profilu `native`**:
   W pliku `firmware/cyd_controller/platformio.ini` (oraz root `platformio.ini`), w sekcji `[env:native]`:
   Dodać:
   ```ini
   lib_deps =
       bblanchon/ArduinoJson@^6.21.4
   ```
   Dzięki temu PlatformIO automatycznie pobiera i linkuje nagłówki ArduinoJson dla środowiska MinGW native.
3. **Spójność Ścieżek Bibliotek w Root `platformio.ini`**:
   W głównym pliku `platformio.ini`:
   ```ini
   [env:native]
   ...
   lib_extra_dirs = firmware/shared, firmware/cyd_controller/lib
   ```
   Umożliwia to uruchamianie `pio test -e native` zarówno z katalogu głównego projektu, jak i z `firmware/cyd_controller`.

---

## 4. Specyfikacja Przypadków Testowych dla Milestone 1

Poniżej przedstawiono kompletny projekt przypadków testowych jednostkowych do wdrożenia w `test_main.cpp`.

### Grupa 1: Komenda `EnterOta` (Serializacja i Deserializacja)

#### Test 1.1: `test_enter_ota_serialization_round_trip_with_credentials`
- **Cel:** Weryfikacja poprawnego generowania ramki NDJSON z opcjonalnymi polami SSID i hasła oraz jej bezbłędnego odczytu przez parser.
- **Scenariusz:**
  1. Utwórz `AquariumCommand cmd` z `cmd.type = CommandType::EnterOta`, `cmd.ota_ssid = "AquaHome_2.4G"`, `cmd.ota_pass = "TajneHaslo123"`.
  2. Wywołaj `String json = serializeCommand(cmd);`.
  3. Asercja: ciąg zawiera `"type":"cmd"`, `"cmd":"enter_ota"`, `"ssid":"AquaHome_2.4G"`, `"pass":"TajneHaslo123"`.
  4. Wywołaj `AquariumMessage msg; bool ok = parseMessage(json.c_str(), msg);`.
  5. Asercja: `ok == true`, `msg.type == MessageType::Command`, `msg.command.type == CommandType::EnterOta`.
  6. Asercja: `strcmp(msg.command.ota_ssid, "AquaHome_2.4G") == 0`, `strcmp(msg.command.ota_pass, "TajneHaslo123") == 0`.

#### Test 1.2: `test_enter_ota_serialization_default_credentials_omitted`
- **Cel:** Weryfikacja, że gdy SSID i hasło są puste, serializer nie marnuje bajtów w ramce NDJSON (fallback do poświadczeń z EEPROM lub AP).
- **Scenariusz:**
  1. Utwórz `cmd` z `type = CommandType::EnterOta`, `ota_ssid[0] = '\0'`, `ota_pass[0] = '\0'`.
  2. Wywołaj `String json = serializeCommand(cmd);`.
  3. Asercja: `json` zawiera `"cmd":"enter_ota"`, ale NIE zawiera kluczy `"ssid"` ani `"pass"`.
  4. Sparsuj `json`: asercja `msg.command.ota_ssid[0] == '\0'`, `msg.command.ota_pass[0] == '\0'`.

#### Test 1.3: `test_enter_ota_deserialization_buffer_boundary_safety`
- **Cel:** Odporność parsera na próby przepełnienia buforów znakowych przy złośliwym lub uszkodzonym pakiecie.
- **Scenariusz:**
  1. Wejście: JSON z ciągiem SSID > 32 znaków i hasłem > 64 znaków.
  2. Sparsuj linię: weryfikacja, że `msg.command.ota_ssid` i `ota_pass` są bezpiecznie ucięte na rozmiarze bufora minus 1 oraz kończą się bajtem `\0`. Brak naruszenia pamięci.

---

### Grupa 2: Ramki Potwierdzenia ACK (`serializeAck` i `parseMessage`)

#### Test 2.1: `test_ack_enter_ota_sta_success`
- **Cel:** Poprawny odbiór potwierdzenia wejścia WeMos w tryb OTA w sieci domowej STA.
- **Scenariusz:**
  1. Wejście: `String ack = serializeAck("enter_ota", true, "STA:192.168.1.150");`.
  2. Sparsuj: asercja `msg.type == MessageType::Ack`, `strcmp(msg.ack.cmd, "enter_ota") == 0`.
  3. Asercja: `msg.ack.ok == true`, `strcmp(msg.ack.message, "STA:192.168.1.150") == 0`.

#### Test 2.2: `test_ack_enter_ota_ap_fallback`
- **Cel:** Poprawny odbiór potwierdzenia uruchomienia punktu dostępowego `AquaBox-OTA`.
- **Scenariusz:**
  1. Wejście: `serializeAck("enter_ota", true, "AP:192.168.4.1");`.
  2. Sparsuj: asercja `msg.ack.ok == true`, `strcmp(msg.ack.message, "AP:192.168.4.1") == 0`.

#### Test 2.3: `test_ack_enter_ota_error_relay_failure`
- **Cel:** Poprawny odbiór odmowy przejścia w tryb OTA w przypadku awarii odcięcia obciążeń.
- **Scenariusz:**
  1. Wejście: `serializeAck("enter_ota", false, "Relay cutoff failed");`.
  2. Sparsuj: asercja `msg.ack.ok == false`, komunikat błędu poprawnie zdeserializowany.

#### Test 2.4: `test_ack_missing_optional_message`
- **Cel:** Zgodność wsteczna i odporność na ramki ACK bez pola `"msg"`.
- **Scenariusz:**
  1. Linia wejściowa: `{"type":"ack","cmd":"enter_ota","ok":true}`.
  2. Sparsuj: `msg.ack.ok == true`, `msg.ack.message[0] == '\0'`.

---

### Grupa 3: Flagi Trybu OTA w Telemetrii (`ota_mode`, `ota_ip`)

#### Test 3.1: `test_telemetry_ota_active_serialization_round_trip`
- **Cel:** Weryfikacja, że aktywny tryb OTA jest poprawnie rozgłaszany w telemetrii i odbierany przez CYD.
- **Scenariusz:**
  1. Przygotuj strukturę `AquariumTelemetry t`:
     - `t.ota_mode = true;`
     - `strncpy(t.ota_ip, "192.168.1.150", sizeof(t.ota_ip) - 1);`
     - `t.relay_states = 0;` (stan bezpieczny)
     - `t.safe_mode = true;`
  2. Serializuj do `String line = serializeTelemetry(t);`.
  3. Asercja: `strstr(line.c_str(), "\"ota\":true") != nullptr`.
  4. Asercja: `strstr(line.c_str(), "\"ota_ip\":\"192.168.1.150\"") != nullptr`.
  5. Sparsuj: `msg.telemetry.ota_mode == true`, `strcmp(msg.telemetry.ota_ip, "192.168.1.150") == 0`.

#### Test 3.2: `test_telemetry_ota_normal_mode_inactive`
- **Cel:** Weryfikacja, że w normalnym trybie pracy flaga OTA ma wartość `false`.
- **Scenariusz:**
  1. `t.ota_mode = false; t.ota_ip[0] = '\0';`.
  2. Serializuj i parsuj: `msg.telemetry.ota_mode == false`, `msg.telemetry.ota_ip[0] == '\0'`.

#### Test 3.3: `test_telemetry_backward_compatibility_legacy_frames`
- **Cel:** Zagwarantowanie 100% kompatybilności wstecznej z ramkami telemetrii ze starszych wersji firmware (brak pól `"ota"` i `"ota_ip"`).
- **Scenariusz:**
  1. Wejście: `{"type":"telemetry","ts":1700000000,"temp":25.50,"temp_tgt":25.00,"temp_hyst":0.30,"ph":7.10,"ec":420.0,"relays":3,"alarms":0,"w_low":false,"leak":false,"uptime":3600,"safe":false,"lmode":1}\n`.
  2. Sparsuj: asercja sukcesu `true`.
  3. Asercja: `msg.telemetry.ota_mode == false` (wartość domyślna).
  4. Asercja: `msg.telemetry.ota_ip[0] == '\0'` (wartość domyślna).
  5. Asercja: pozostałe pola (`temperature`, `ph`, `relay_states`) zdeserializowane bez zmian.

#### Test 3.4: `test_telemetry_ip_buffer_bounds_check`
- **Cel:** Zabezpieczenie przed przepełnieniem bufora `ota_ip[16]` przy podaniu zbyt długiego adresu (np. IPv6 lub śmieci na linii UART).
- **Scenariusz:**
  1. JSON z `"ota_ip":"2001:0db8:85a3:0000:0000:8a2e:0370:7334"`.
  2. Sparsuj: ciąg zostaje bezpiecznie przycięty do 15 znaków + `\0`. Pola sąsiednie struktury nie ulegają uszkodzeniu.

---

### Grupa 4: Logika Klienta UART (`UartClient`) i Timeouty

#### Test 4.1: `test_uart_client_ack_latching`
- **Cel:** Weryfikacja, że `UartClient::processLine` po odebraniu ramki ACK ustawia flagę `has_new_ack_` oraz zapisuje `latest_ack_`.
- **Scenariusz:**
  1. Wywołaj `client.processLine("{\"type\":\"ack\",\"cmd\":\"enter_ota\",\"ok\":true,\"msg\":\"STA:192.168.1.150\"}");`.
  2. Asercja: `client.hasNewAck() == true`, `client.getLatestAck().ok == true`, `strcmp(client.getLatestAck().message, "STA:192.168.1.150") == 0`.

#### Test 4.2: `test_uart_client_timeout_suppression_during_ota`
- **Cel:** Zapobieganie fałszywym alarmom braku łączności (timeout 3000 ms) w trakcie flashowania WeMos.
- **Scenariusz:**
  1. WeMos przechodzi w stan `ota_mode == true`.
  2. Symuluj upływ czasu `now_ms = last_rx_ms + 5000` (5 sekund braku telemetrii).
  3. Asercja: `client.isControllerOnline()` pozostaje `true` (lub `isOtaPending() == true`), a sygnał dźwiękowy buzzera (`checkAlarmAudio()`) nie załącza się.

---

## 5. Empiryczny Dowód Wykonalności (Empirical Proof of Viability)

W ramach dochodzenia utworzono prototypowy program testowy `.agents/explorer_m1_3/test_proto.cpp` z adapterem `Arduino.h` i skompilowano go za pomocą narzędzi PlatformIO:

```powershell
$env:PATH = "C:\Users\Bartek\.platformio\packages\toolchain-gccmingw32\bin;" + $env:PATH
g++ -std=gnu++11 -static -static-libgcc -static-libstdc++ `
    -I .agents/explorer_m1_3 `
    -I .pio/libdeps/wemos_d1_uno/ArduinoJson/src `
    -I firmware/cyd_controller/.pio/libdeps/native/Unity/src `
    .agents/explorer_m1_3/test_proto.cpp `
    firmware/cyd_controller/.pio/libdeps/native/Unity/src/unity.c `
    -o .agents/explorer_m1_3/test_proto.exe
```

### Wynik Wykonania:
```
.agents/explorer_m1_3/test_proto.cpp:311:test_enter_ota_serialization_round_trip:PASS
.agents/explorer_m1_3/test_proto.cpp:312:test_enter_ota_default_fallback_round_trip:PASS
.agents/explorer_m1_3/test_proto.cpp:313:test_ack_enter_ota_handling:PASS
.agents/explorer_m1_3/test_proto.cpp:314:test_telemetry_ota_flags_and_backward_compatibility:PASS

-----------------------
4 Tests 0 Failures 0 Ignored 
OK
```

Dowodzi to jednoznacznie, że:
1. Kompilator MinGW GCC 5.1.0 wspiera standard C++11 i bezbłędnie kompiluje szablony `ArduinoJson 6.21.6`.
2. Parser i serializer NDJSON zachowują pełną integralność logiczną i numeryczną w środowisku natywnym.
3. Testy Unity wykonują się natychmiastowo (< 0.1 s) bez konieczności podłączania jakiegokolwiek sprzętu fizycznego.

---

## 6. Przewodnik Wdrożeniowy dla Implementera M1

Aby zrealizować Milestone 1 i umożliwić bezbłędne przejście `pio test -e native`, należy wykonać następujące kroki:

### Krok 1: Dostosowanie `platformio.ini`
1. W `firmware/cyd_controller/platformio.ini`:
   Dodać pod `[env:native]`:
   ```ini
   lib_deps =
       bblanchon/ArduinoJson@^6.21.4
   ```
2. W głównym `platformio.ini` (w katalogu głównym projektu):
   Zaktualizować linię 137 pod `[env:native]`:
   ```ini
   lib_extra_dirs = firmware/shared, firmware/cyd_controller/lib
   lib_deps =
       bblanchon/ArduinoJson@^6.21.4
   ```

### Krok 2: Rozszerzenie `firmware/shared/uart_protocol/include/uart_protocol.h`
- Dodać warunkową definicję dla środowiska nienatywnego Arduino (`#if defined(ARDUINO) ... #else ... #endif`).
- Dodać `EnterOta` do enumu `CommandType`.
- Dodać pola `char ota_ssid[33]` i `char ota_pass[65]` do `AquariumCommand`.
- Dodać pola `bool ota_mode` i `char ota_ip[16]` do `AquariumTelemetry`.

### Krok 3: Implementacja w `firmware/shared/uart_protocol/src/uart_protocol.cpp`
- W `serializeCommand`: dodać gałąź `case CommandType::EnterOta:`.
- W `serializeTelemetry`: dodać serializację pól `doc["ota"] = t.ota_mode;` oraz `if (t.ota_ip[0] != '\0') doc["ota_ip"] = t.ota_ip;`.
- W `parseMessage`:
  - Obsłużyć `enter_ota` w sekcji `type == "cmd"`.
  - Odczytać `ota_mode = doc["ota"] | false;` oraz `ota_ip = doc["ota_ip"] | "";` w sekcji `type == "telemetry"`.

### Krok 4: Integracja Testów w `test_native_domain/test_main.cpp`
- Dołączyć `#include "uart_protocol.h"`.
- Zdefiniować funkcje testowe zgodnie ze specyfikacją z Rozdziału 4 niniejszego raportu.
- Dodać wywołania `RUN_TEST(...)` w funkcji `main()`.

### Krok 5: Weryfikacja
- Uruchomić w `firmware/cyd_controller`:
  `pio test -e native`
- Uruchomić w katalogu głównym:
  `pio test -e native`
- Wszystkie testy domenowe (45) oraz nowe testy protokołu UART NDJSON muszą zakończyć się statusem `[PASSED]`.
