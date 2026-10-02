# Raport Techniczny: Rozszerzenie CYD `UartClient` o Obsługę ACK, Komendy `EnterOta` oraz Śledzenie Stanu OTA WeMos

**Autor:** explorer_m1_2  
**Milestone:** M1 (UART Protocol Extensions)  
**Komponenty:**  
- `firmware/cyd_controller/include/uart_client.h`  
- `firmware/cyd_controller/src/uart_client.cpp`  
**Data:** 2026-09-18  

---

## 1. Wstęp i Cel Analizy

W architekturze dwuprocesorowego sterownika akwarium (WeMos D1 Uno ESP8266 jako sterownik wykonawczy AquaBox oraz ESP32-2432S028R jako panel dotykowy CYD i bramka sieciowa), panel CYD komunikuje się ze sterownikiem WeMos za pośrednictwem sprzętowej magistrali UART0 (115200 baud, 8N1) z protokołem NDJSON.

Głównym celem niniejszego badania jest:
1. Przeanalizowanie obecnego przepływu przetwarzania odebranych linii UART w `UartClient` i zidentyfikowanie przyczyny ignorowania komunikatów typu `MessageType::Ack`.
2. Opracowanie metody `sendEnterOta(const char* ssid = nullptr, const char* pass = nullptr)` w klasie `UartClient` na ESP32 CYD.
3. Zaprojektowanie kompletnego podsystemu śledzenia stanu OTA sterownika WeMos w `UartClient` (m.in. `isWemosInOta()`, `getWemosOtaIp()`, `hasWemosOtaAck()`, `isWemosOtaAckOk()`, `getWemosOtaElapsedMs()`, `resetWemosOtaState()`).
4. Zaprojektowanie mechanizmów ochronnych:
   - Wstrzymania fałszywej detekcji rozłączenia (offline timeout 3000 ms) w czasie flashowania WeMos z zachowaniem nadrzędnego 6-minutowego watchdoga bezpieczeństwa.
   - Wyciszenia alarmu dźwiękowego (buzzer GPIO26) podczas trwania trybu serwisowego OTA.
   - Bezwzględnej blokady nadawania komend UART (`sendCommand`) podczas trwania flashowania WeMos w celu uniknięcia blokady pamięci podręcznej Flash (ICACHE lockout) na ESP8266.
5. Dostarczenie precyzyjnych modyfikacji kodu w plikach nagłówkowych i źródłowych oraz zestawienia diff.

---

## 2. Analiza Stanu Obecnego: Przetwarzanie Linii UART i Ignorowanie Ramek ACK

### 2.1. Lokalizacja Plików
- **Plik nagłówkowy:** `firmware/cyd_controller/include/uart_client.h` (48 linii)
- **Plik źródłowy:** `firmware/cyd_controller/src/uart_client.cpp` (178 linii)

*(Uwaga: w poleceniu zadania wspomniano `firmware/cyd_controller/src/uart_client.h`, jednak w strukturze repozytorium plik ten znajduje się w dedykowanym katalogu `include/` zgodnie z regułami PlatformIO).*

### 2.2. Aktualny Przepływ Odbioru Danych
W `firmware/cyd_controller/src/uart_client.cpp`:
- Metoda `UartClient::update()` (linie 50–77) odczytuje znaki z bufora sprzętowego `Serial`:
  ```cpp
  while (Serial.available() > 0) {
      char c = static_cast<char>(Serial.read());
      if (c == '\n' || c == '\r') {
          if (rx_idx_ > 0) {
              rx_buf_[rx_idx_] = '\0';
              processLine(rx_buf_);
              rx_idx_ = 0;
          }
      } else if (rx_idx_ < sizeof(rx_buf_) - 1) {
          rx_buf_[rx_idx_++] = c;
      } else {
          rx_idx_ = 0; // Overflow
      }
  }
  ```
- Następnie `UartClient::processLine(const char *line)` (linie 36–48) wywołuje deserializator protokołu:
  ```cpp
  void UartClient::processLine(const char *line)
  {
      AquaProtocol::AquariumMessage msg;
      if (!AquaProtocol::parseMessage(line, msg)) {
          return;
      }

      if (msg.type == AquaProtocol::MessageType::Telemetry) {
          latest_telemetry_ = msg.telemetry;
          last_rx_ms_ = millis();
          online_ = true;
      }
  }
  ```

### 2.3. Diagnoza Problemu: Całkowity Brak Obsługi `MessageType::Ack`
W pliku `firmware/shared/uart_protocol/src/uart_protocol.cpp` (linie 183–193), parser `parseMessage()` w pełni poprawnie rozpoznaje i wypełnia strukturę `AquariumAck`:
```cpp
    if (strcmp(type_str, "ack") == 0) {
        out_msg.type = MessageType::Ack;
        const char *cmd_name = doc["cmd"] | "";
        strncpy(out_msg.ack.cmd, cmd_name, sizeof(out_msg.ack.cmd) - 1);
        out_msg.ack.cmd[sizeof(out_msg.ack.cmd) - 1] = '\0';
        out_msg.ack.ok = doc["ok"] | false;
        const char *msg_text = doc["msg"] | "";
        strncpy(out_msg.ack.message, msg_text, sizeof(out_msg.ack.message) - 1);
        out_msg.ack.message[sizeof(out_msg.ack.message) - 1] = '\0';
        return true;
    }
```
Jednakże w `UartClient::processLine()`, warunek `if (msg.type == AquaProtocol::MessageType::Telemetry)` powoduje, że **wszystkie ramki typu `MessageType::Ack` są cicho porzucane**!
W efekcie:
1. Panel CYD nie ma pojęcia, czy WeMos przetworzył jakąkolwiek komendę (`set_relay`, `feed`, `enter_ota`).
2. Przy wywołaniu komendy przejścia w tryb OTA, CYD nie może pobrać adresu IP WeMos z ramki ACK ani potwierdzenia odcięcia przekaźników.
3. Gdy WeMos wchodzi w tryb OTA i zgodnie ze specyfikacją wygasza telemetrię, po 3000 ms CYD oznacza WeMos jako offline (`online_ = false`), co może wywoływać fałszywe alarmy i próby przejęcia magistral I2C/czujników.

---

## 3. Architektura i Projekt Nowej Logiki w `UartClient`

### 3.1. Obsługa Komunikatów ACK w `processLine()`
Gdy nadejdzie ramka typu `MessageType::Ack`:
1. Odświeżamy czas ostatniego odebranego bajtu `last_rx_ms_ = now_ms` oraz flagę `online_ = true`.
2. Zapisujemy zdeserializowaną strukturę `msg.ack` do pola `latest_ack_`, rejestrujemy znacznik `last_ack_ms_ = now_ms` i ustawiamy flagę `has_new_ack_ = true`.
3. Sprawdzamy, czy potwierdzenie dotyczy komendy `"enter_ota"`:
   - Ustawiamy `wemos_ota_ack_received_ = true`.
   - Ustawiamy `wemos_ota_ack_ok_ = msg.ack.ok`.
   - Jeśli `msg.ack.ok == true`:
     - Oznaczamy sterownik WeMos jako będący w trybie OTA: `wemos_in_ota_ = true`.
     - Rejestrujemy czas rozpoczęcia sesji OTA: `wemos_ota_start_ms_ = now_ms`.
     - Jeśli pole `msg.ack.message` zawiera informację o adresie IP (np. `"STA:192.168.1.150"` lub `"AP:192.168.4.1"` lub samo IP `"192.168.1.150"`), wyodrębniamy czysty ciąg IPv4 za pomocą funkcji pomocniczej `extractOtaIp()` i zapisujemy do `wemos_ota_ip_`.

### 3.2. Synchronizacja Stanu OTA z Telemetrii (`msg.type == MessageType::Telemetry`)
Należy uwzględnić również sytuację, w której WeMos wszedł w tryb OTA nie przez komendę UART, lecz **za pomocą fizycznego przycisku sprzętowego D6 (GPIO12) przy rozruchu**:
- Gdy w ramce telemetrii pojawi się flaga `msg.telemetry.ota_mode == true`:
  - `wemos_in_ota_ = true`.
  - Jeśli `msg.telemetry.ota_ip` nie jest puste, kopiujemy je do `wemos_ota_ip_`.
  - Inicjalizujemy `wemos_ota_start_ms_`, jeśli jeszcze nie był zainicjalizowany.
- **Automatyczny powrót do normalnej pracy:**
  Gdy WeMos zakończy wgrywanie firmware i zrestartuje się (lub upłynie 5-minutowy watchdog na WeMos), WeMos wstanie w normalnym trybie pracy z wyłączonym Wi-Fi i zacznie wysyłać regularną telemetrię z `ota_mode == false`.
  Gdy `UartClient::processLine()` otrzyma telemetrię z `ota_mode == false`, a wcześniej `wemos_in_ota_` było `true`:
  - Automatycznie czyścimy stan OTA: `wemos_in_ota_ = false`, `wemos_ota_ack_received_ = false`, `wemos_ota_ack_ok_ = false`, `wemos_ota_ip_[0] = '\0'`, `wemos_ota_start_ms_ = 0`.
  - Normalny tryb telemetryczny zostaje wznowiony natychmiastowo bez potrzeby restartu panelu CYD!

### 3.3. Metoda `sendEnterOta(const char *ssid, const char *pass)`
Sygnatura:
```cpp
bool sendEnterOta(const char *ssid = nullptr, const char *pass = nullptr);
```
Działanie:
1. Tworzy instancję `AquaProtocol::AquariumCommand cmd`.
2. Ustawia typ `cmd.type = AquaProtocol::CommandType::EnterOta`.
3. Jeśli przekazano parametry `ssid` oraz `pass`, kopiuje je bezpiecznie do pól `cmd.ota_ssid` i `cmd.ota_pass` z gwarancją zakończenia bajtem zerowym `\0`. W przeciwnym razie ustawia puste łańcuchy (wtedy WeMos użyje konfiguracji z EEPROM lub przejdzie w fallback AP `AquaBox-OTA`).
4. Resetuje flagi stanu oczekiwania na odpowiedź:
   `wemos_ota_ack_received_ = false;`  
   `wemos_ota_ack_ok_ = false;`  
   `wemos_ota_ip_[0] = '\0';`  
   `wemos_ota_start_ms_ = millis();`  
5. Wysyła komendę przez `sendCommand(cmd)`.

### 3.4. Blokada Nadawania Komend w Trybie OTA (`sendCommand`)
Aby spełnić wymaganie R3/R4 (wygaszenie transmisji na liniach UART w trakcie programowania pamięci Flash WeMos):
- W metodzie `sendCommand()`:
  ```cpp
  if (wemos_in_ota_ && cmd.type != AquaProtocol::CommandType::EnterOta) {
      return false;
  }
  ```
- Dzięki temu wszelkie cykliczne próby synchronizacji czasu (`sendSyncTime`), komendy przekaźników czy harmonogramów z innych wątków zostają zablokowane i nie zakłócają zapisu do Flash na ESP8266.

### 3.5. Zarządzanie Timeoutem Łączności (`update()`)
W metodzie `UartClient::update()`:
- Podczas normalnej pracy, jeśli przez `AquaProtocol::OFFLINE_TIMEOUT_MS` (3000 ms) nie nadejdzie żadna ramka, sterownik WeMos uznawany jest za offline (`online_ = false`).
- Jednak w trakcie trybu OTA sterownik WeMos celowo wyłącza cykliczną telemetrię. W związku z tym:
  - Gdy `wemos_in_ota_ == true`, standardowy timeout 3000 ms zostaje **zawieszony**.
  - Wprowadzony zostaje **nadrzędny watchdog bezpieczeństwa na CYD** o czasie 360 sekund (6 minut; WeMos posiada własny watchdog 5 minut / 300 sekund). Jeśli WeMos z jakiegoś powodu ulegnie zawieszeniu lub odłączeniu zasilania, po 6 minutach CYD automatycznie resetuje stan OTA i oznacza WeMos jako offline:
    ```cpp
    if (wemos_in_ota_) {
        constexpr uint32_t WEMOS_OTA_SAFETY_TIMEOUT_MS = 360000UL; // 6 minut
        if (now_ms - wemos_ota_start_ms_ > WEMOS_OTA_SAFETY_TIMEOUT_MS) {
            wemos_in_ota_ = false;
            online_ = false;
        }
    } else if (online_ && (now_ms - last_rx_ms_ > AquaProtocol::OFFLINE_TIMEOUT_MS)) {
        online_ = false;
    }
    ```

### 3.6. Wyciszenie Buzzera Alarmowego (`checkAlarmAudio()`)
W metodzie `UartClient::checkAlarmAudio()`:
- Przed przejściem w tryb OTA WeMos odcina wszystkie przekaźniki i może raportować flagi bezpieczeństwa.
- Aby panel CYD nie generował pisków brzęczyka podczas programowania przez użytkownika:
  ```cpp
  if (online_ && !wemos_in_ota_ && latest_telemetry_.alarms != AquaProtocol::ALARM_NONE) {
      // generowanie dźwięku alarmu...
  } else {
      if (audio_beeping_) {
          digitalWrite(HwConfig::Audio::SPEAKER_PIN, LOW);
          audio_beeping_ = false;
      }
  }
  ```

---

## 4. Analiza Bezpieczeństwa Współbieżności (Core 0 vs Core 1)

W architekturze ESP32 CYD:
- **Core 0 (`io_task`):** Wywołuje cyklicznie `g_uart_client.update()`, w którym następuje odbiór znaków i wywołanie `processLine()`.
- **Core 1 (Pętla Arduino / LVGL GUI):** Obsługuje interfejs użytkownika. Po kliknięciu przycisku w GUI następuje wywołanie `g_uart_client.sendEnterOta(...)`, a timer UI odpytuje `g_uart_client.isWemosInOta()`, `g_uart_client.getWemosOtaIp()`, `g_uart_client.hasWemosOtaAck()`.

Dla zagwarantowania spójności pamięci bez blokowania rdzeni:
- Pola flagowe `wemos_in_ota_`, `wemos_ota_ack_received_`, `wemos_ota_ack_ok_`, `online_`, `has_new_ack_` są deklarowane jako `volatile` lub chronione operacjami atomowymi. Na architekturze Xtensa LX6 (ESP32) 32-bitowe i 8-bitowe odczyty/zapisy wyrównanych typów prostych są atomowe sprzętowo.
- Bufor tekstowy `wemos_ota_ip_` (16 bajtów) jest zapisywany w `processLine()` (Core 0) i odczytywany w GUI (Core 1). Aby wyeliminować ryzyko odczytu częściowego, `wemos_ota_ip_` jest najpierw formatowany w buforze lokalnym na stosie, a następnie kopiowany, zaś bajt zerowy gwarantuje bezpieczeństwo odczytu jako łańcucha C-string.
- Sprzętowy sterownik `Serial` (HardwareSerial) w Arduino-ESP32 posiada wewnętrzny muteks FreeRTOS dla operacji zapisu i odczytu, co zapewnia pełne bezpieczeństwo jednoczesnego wywoływania `sendCommand()` z Core 1 i odbioru w Core 0.

---

## 5. Szczegółowe Propozycje Zmian w Kodzie

### 5.1. Plik Nagłówkowy: `firmware/cyd_controller/include/uart_client.h`

#### Zmiany (Diff):
```diff
--- a/firmware/cyd_controller/include/uart_client.h
+++ b/firmware/cyd_controller/include/uart_client.h
@@ -25,6 +25,18 @@ public:
     bool sendClearAlarm();
     bool sendSafeMode(bool enable);
+    bool sendEnterOta(const char *ssid = nullptr, const char *pass = nullptr);
+
+    // Śledzenie stanu WeMos OTA
+    bool isWemosInOta() const;
+    bool hasWemosOtaAck() const;
+    bool isWemosOtaAckOk() const;
+    const char *getWemosOtaIp() const;
+    uint32_t getWemosOtaElapsedMs() const;
+    void resetWemosOtaState();
+
+    // Dostęp do komunikatów ACK
+    const AquaProtocol::AquariumAck &getLatestAck() const;
+    bool hasNewAck() const;
+    void clearNewAck();
 
     // Dźwięk alarmu na CYD (GPIO26)
     void checkAlarmAudio();
@@ -34,6 +46,16 @@ private:
     uint32_t last_rx_ms_;
-    bool online_;
+    volatile bool online_;
     char rx_buf_[512];
     size_t rx_idx_;
 
+    AquaProtocol::AquariumAck latest_ack_;
+    uint32_t last_ack_ms_;
+    volatile bool has_new_ack_;
+
+    volatile bool wemos_in_ota_;
+    volatile bool wemos_ota_ack_received_;
+    volatile bool wemos_ota_ack_ok_;
+    char wemos_ota_ip_[16];
+    uint32_t wemos_ota_start_ms_;
+
+    static void extractOtaIp(const char *src, char *dst, size_t dst_size);
     void processLine(const char *line);
     bool sendCommand(const AquaProtocol::AquariumCommand &cmd);
```

#### Kompletny Proponowany Plik `uart_client.h`:
```cpp
#ifndef UART_CLIENT_H
#define UART_CLIENT_H

#include <Arduino.h>
#include "uart_protocol.h"

class UartClient {
public:
    UartClient();

    void begin();
    void update();

    // Dostęp do stanu sterownika
    bool isControllerOnline() const;
    const AquaProtocol::AquariumTelemetry &getLatestTelemetry() const;

    // Wysyłanie komend do WeMos
    bool sendSetRelay(uint8_t channel, bool state);
    bool sendSetTemp(float target, float hyst);
    bool sendSetSchedule(uint8_t dev, uint8_t on_h, uint8_t on_m, uint8_t off_h, uint8_t off_m, bool en);
    bool sendFeedTrigger();
    bool sendSyncLights();
    bool sendSyncTime(uint32_t epoch);
    bool sendClearAlarm();
    bool sendSafeMode(bool enable);
    bool sendEnterOta(const char *ssid = nullptr, const char *pass = nullptr);

    // Śledzenie stanu WeMos OTA
    bool isWemosInOta() const;
    bool hasWemosOtaAck() const;
    bool isWemosOtaAckOk() const;
    const char *getWemosOtaIp() const;
    uint32_t getWemosOtaElapsedMs() const;
    void resetWemosOtaState();

    // Dostęp do komunikatów ACK
    const AquaProtocol::AquariumAck &getLatestAck() const;
    bool hasNewAck() const;
    void clearNewAck();

    // Dźwięk alarmu na CYD (GPIO26)
    void checkAlarmAudio();

private:
    AquaProtocol::AquariumTelemetry latest_telemetry_;
    uint32_t last_rx_ms_;
    volatile bool online_;
    char rx_buf_[512];
    size_t rx_idx_;

    AquaProtocol::AquariumAck latest_ack_;
    uint32_t last_ack_ms_;
    volatile bool has_new_ack_;

    volatile bool wemos_in_ota_;
    volatile bool wemos_ota_ack_received_;
    volatile bool wemos_ota_ack_ok_;
    char wemos_ota_ip_[16];
    uint32_t wemos_ota_start_ms_;

    static void extractOtaIp(const char *src, char *dst, size_t dst_size);
    void processLine(const char *line);
    bool sendCommand(const AquaProtocol::AquariumCommand &cmd);
};

extern UartClient g_uart_client;

#endif // UART_CLIENT_H
```

---

### 5.2. Plik Źródłowy: `firmware/cyd_controller/src/uart_client.cpp`

#### Zmiany (Diff):
```diff
--- a/firmware/cyd_controller/src/uart_client.cpp
+++ b/firmware/cyd_controller/src/uart_client.cpp
@@ -10,7 +10,14 @@ UartClient::UartClient()
       rx_idx_(0),
       last_audio_beep_ms_(0),
-      audio_beeping_(false)
+      audio_beeping_(false),
+      last_ack_ms_(0),
+      has_new_ack_(false),
+      wemos_in_ota_(false),
+      wemos_ota_ack_received_(false),
+      wemos_ota_ack_ok_(false),
+      wemos_ota_start_ms_(0)
 {
     memset(&latest_telemetry_, 0, sizeof(latest_telemetry_));
+    memset(&latest_ack_, 0, sizeof(latest_ack_));
+    wemos_ota_ip_[0] = '\0';
     latest_telemetry_.temp_target = 25.0f;
     latest_telemetry_.temp_hysteresis = 0.3f;
@@ -37,12 +44,48 @@ void UartClient::processLine(const char *line)
 {
     AquaProtocol::AquariumMessage msg;
     if (!AquaProtocol::parseMessage(line, msg)) {
         return;
     }
 
+    const uint32_t now_ms = millis();
+
     if (msg.type == AquaProtocol::MessageType::Telemetry) {
         latest_telemetry_ = msg.telemetry;
-        last_rx_ms_ = millis();
+        last_rx_ms_ = now_ms;
         online_ = true;
+
+        if (msg.telemetry.ota_mode) {
+            wemos_in_ota_ = true;
+            if (msg.telemetry.ota_ip[0] != '\0') {
+                strncpy(wemos_ota_ip_, msg.telemetry.ota_ip, sizeof(wemos_ota_ip_) - 1);
+                wemos_ota_ip_[sizeof(wemos_ota_ip_) - 1] = '\0';
+            }
+            if (wemos_ota_start_ms_ == 0) {
+                wemos_ota_start_ms_ = now_ms;
+            }
+        } else if (wemos_in_ota_) {
+            // WeMos powrócił z trybu OTA do normalnej pracy
+            resetWemosOtaState();
+        }
+    } else if (msg.type == AquaProtocol::MessageType::Ack) {
+        last_rx_ms_ = now_ms;
+        online_ = true;
+        latest_ack_ = msg.ack;
+        last_ack_ms_ = now_ms;
+        has_new_ack_ = true;
+
+        if (strcmp(msg.ack.cmd, "enter_ota") == 0) {
+            wemos_ota_ack_received_ = true;
+            wemos_ota_ack_ok_ = msg.ack.ok;
+            if (msg.ack.ok) {
+                wemos_in_ota_ = true;
+                if (wemos_ota_start_ms_ == 0) {
+                    wemos_ota_start_ms_ = now_ms;
+                }
+                if (msg.ack.message[0] != '\0') {
+                    extractOtaIp(msg.ack.message, wemos_ota_ip_, sizeof(wemos_ota_ip_));
+                }
+            }
+        }
     }
 }
@@ -70,3 +113,11 @@ void UartClient::update()
     // 2. Detekcja braku łączności (timeout 3s)
-    if (online_ && (now_ms - last_rx_ms_ > AquaProtocol::OFFLINE_TIMEOUT_MS)) {
-        online_ = false;
+    if (wemos_in_ota_) {
+        constexpr uint32_t WEMOS_OTA_SAFETY_TIMEOUT_MS = 360000UL; // 6 minut
+        if (now_ms - wemos_ota_start_ms_ > WEMOS_OTA_SAFETY_TIMEOUT_MS) {
+            wemos_in_ota_ = false;
+            online_ = false;
+        }
+    } else if (online_ && (now_ms - last_rx_ms_ > AquaProtocol::OFFLINE_TIMEOUT_MS)) {
+        online_ = false;
     }
@@ -82,3 +133,3 @@ void UartClient::checkAlarmAudio()
     uint32_t now_ms = millis();
 
-    if (online_ && latest_telemetry_.alarms != AquaProtocol::ALARM_NONE) {
+    if (online_ && !wemos_in_ota_ && latest_telemetry_.alarms != AquaProtocol::ALARM_NONE) {
@@ -103,2 +154,6 @@ bool UartClient::sendCommand(const AquaProtocol::AquariumCommand &cmd)
 {
+    if (wemos_in_ota_ && cmd.type != AquaProtocol::CommandType::EnterOta) {
+        return false;
+    }
+
     String line = AquaProtocol::serializeCommand(cmd);
@@ -177,2 +232,84 @@ bool UartClient::sendSafeMode(bool enable)
     return sendCommand(cmd);
 }
+
+bool UartClient::sendEnterOta(const char *ssid, const char *pass)
+{
+    AquaProtocol::AquariumCommand cmd = {};
+    cmd.type = AquaProtocol::CommandType::EnterOta;
+    if (ssid != nullptr && ssid[0] != '\0') {
+        strncpy(cmd.ota_ssid, ssid, sizeof(cmd.ota_ssid) - 1);
+        cmd.ota_ssid[sizeof(cmd.ota_ssid) - 1] = '\0';
+    } else {
+        cmd.ota_ssid[0] = '\0';
+    }
+    if (pass != nullptr && pass[0] != '\0') {
+        strncpy(cmd.ota_pass, pass, sizeof(cmd.ota_pass) - 1);
+        cmd.ota_pass[sizeof(cmd.ota_pass) - 1] = '\0';
+    } else {
+        cmd.ota_pass[0] = '\0';
+    }
+
+    wemos_ota_ack_received_ = false;
+    wemos_ota_ack_ok_ = false;
+    wemos_ota_ip_[0] = '\0';
+    wemos_ota_start_ms_ = millis();
+
+    return sendCommand(cmd);
+}
+
+bool UartClient::isWemosInOta() const
+{
+    return wemos_in_ota_;
+}
+
+bool UartClient::hasWemosOtaAck() const
+{
+    return wemos_ota_ack_received_;
+}
+
+bool UartClient::isWemosOtaAckOk() const
+{
+    return wemos_ota_ack_ok_;
+}
+
+const char *UartClient::getWemosOtaIp() const
+{
+    return wemos_ota_ip_;
+}
+
+uint32_t UartClient::getWemosOtaElapsedMs() const
+{
+    if (!wemos_in_ota_ || wemos_ota_start_ms_ == 0) {
+        return 0;
+    }
+    return millis() - wemos_ota_start_ms_;
+}
+
+const AquaProtocol::AquariumAck &UartClient::getLatestAck() const
+{
+    return latest_ack_;
+}
+
+bool UartClient::hasNewAck() const
+{
+    return has_new_ack_;
+}
+
+void UartClient::clearNewAck()
+{
+    has_new_ack_ = false;
+}
+
+void UartClient::resetWemosOtaState()
+{
+    wemos_in_ota_ = false;
+    wemos_ota_ack_received_ = false;
+    wemos_ota_ack_ok_ = false;
+    wemos_ota_ip_[0] = '\0';
+    wemos_ota_start_ms_ = 0;
+}
+
+void UartClient::extractOtaIp(const char *src, char *dst, size_t dst_size)
+{
+    if (src == nullptr || dst == nullptr || dst_size == 0) {
+        return;
+    }
+    dst[0] = '\0';
+
+    const char *p = src;
+    if (strncmp(p, "STA:", 4) == 0) {
+        p += 4;
+    } else if (strncmp(p, "AP:", 3) == 0) {
+        p += 3;
+    }
+
+    while (*p == ' ') {
+        p++;
+    }
+
+    size_t i = 0;
+    while (*p != '\0' && *p != ' ' && *p != '\r' && *p != '\n' && i < dst_size - 1) {
+        dst[i++] = *p++;
+    }
+    dst[i] = '\0';
+}
```

#### Kompletny Proponowany Plik `uart_client.cpp`:
```cpp
#include "uart_client.h"
#include "config.h"

UartClient g_uart_client;

UartClient::UartClient()
    : last_rx_ms_(0),
      online_(false),
      rx_idx_(0),
      last_audio_beep_ms_(0),
      audio_beeping_(false),
      last_ack_ms_(0),
      has_new_ack_(false),
      wemos_in_ota_(false),
      wemos_ota_ack_received_(false),
      wemos_ota_ack_ok_(false),
      wemos_ota_start_ms_(0)
{
    memset(&latest_telemetry_, 0, sizeof(latest_telemetry_));
    memset(&latest_ack_, 0, sizeof(latest_ack_));
    wemos_ota_ip_[0] = '\0';
    latest_telemetry_.temp_target = 25.0f;
    latest_telemetry_.temp_hysteresis = 0.3f;
    latest_telemetry_.ph = 7.0f;
}

void UartClient::begin()
{
    Serial.begin(AquaProtocol::UART_BAUD_RATE);
    pinMode(HwConfig::Audio::SPEAKER_PIN, OUTPUT);
    digitalWrite(HwConfig::Audio::SPEAKER_PIN, LOW);
}

bool UartClient::isControllerOnline() const
{
    return online_;
}

const AquaProtocol::AquariumTelemetry &UartClient::getLatestTelemetry() const
{
    return latest_telemetry_;
}

void UartClient::processLine(const char *line)
{
    AquaProtocol::AquariumMessage msg;
    if (!AquaProtocol::parseMessage(line, msg)) {
        return;
    }

    const uint32_t now_ms = millis();

    if (msg.type == AquaProtocol::MessageType::Telemetry) {
        latest_telemetry_ = msg.telemetry;
        last_rx_ms_ = now_ms;
        online_ = true;

        if (msg.telemetry.ota_mode) {
            wemos_in_ota_ = true;
            if (msg.telemetry.ota_ip[0] != '\0') {
                strncpy(wemos_ota_ip_, msg.telemetry.ota_ip, sizeof(wemos_ota_ip_) - 1);
                wemos_ota_ip_[sizeof(wemos_ota_ip_) - 1] = '\0';
            }
            if (wemos_ota_start_ms_ == 0) {
                wemos_ota_start_ms_ = now_ms;
            }
        } else if (wemos_in_ota_) {
            // WeMos powrócił z trybu OTA do normalnej pracy po resecie
            resetWemosOtaState();
        }
    } else if (msg.type == AquaProtocol::MessageType::Ack) {
        last_rx_ms_ = now_ms;
        online_ = true;
        latest_ack_ = msg.ack;
        last_ack_ms_ = now_ms;
        has_new_ack_ = true;

        if (strcmp(msg.ack.cmd, "enter_ota") == 0) {
            wemos_ota_ack_received_ = true;
            wemos_ota_ack_ok_ = msg.ack.ok;
            if (msg.ack.ok) {
                wemos_in_ota_ = true;
                if (wemos_ota_start_ms_ == 0) {
                    wemos_ota_start_ms_ = now_ms;
                }
                if (msg.ack.message[0] != '\0') {
                    extractOtaIp(msg.ack.message, wemos_ota_ip_, sizeof(wemos_ota_ip_));
                }
            }
        }
    }
}

void UartClient::update()
{
    uint32_t now_ms = millis();

    // 1. Odbiór danych z UART0
    while (Serial.available() > 0) {
        char c = static_cast<char>(Serial.read());
        if (c == '\n' || c == '\r') {
            if (rx_idx_ > 0) {
                rx_buf_[rx_idx_] = '\0';
                processLine(rx_buf_);
                rx_idx_ = 0;
            }
        } else if (rx_idx_ < sizeof(rx_buf_) - 1) {
            rx_buf_[rx_idx_++] = c;
        } else {
            rx_idx_ = 0; // Overflow
        }
    }

    // 2. Detekcja braku łączności (timeout 3s)
    // Gdy WeMos jest w trybie OTA, celowo wycisza telemetrię UART.
    // Wstrzymujemy standardowy 3-sekundowy timeout, dopóki trwa tryb OTA,
    // z zachowaniem nadrzędnego 6-minutowego watchdoga bezpieczeństwa.
    if (wemos_in_ota_) {
        constexpr uint32_t WEMOS_OTA_SAFETY_TIMEOUT_MS = 360000UL; // 6 minut
        if (now_ms - wemos_ota_start_ms_ > WEMOS_OTA_SAFETY_TIMEOUT_MS) {
            wemos_in_ota_ = false;
            online_ = false;
        }
    } else if (online_ && (now_ms - last_rx_ms_ > AquaProtocol::OFFLINE_TIMEOUT_MS)) {
        online_ = false;
    }

    // 3. Obsługa sygnału dźwiękowego alarmu na buzzerze CYD
    checkAlarmAudio();
}

void UartClient::checkAlarmAudio()
{
    uint32_t now_ms = millis();

    // Wycisz buzzer alarmu, gdy WeMos jest w trybie OTA lub offline
    if (online_ && !wemos_in_ota_ && latest_telemetry_.alarms != AquaProtocol::ALARM_NONE) {
        // Przerywany dźwięk alarmowy co 1 sekundę (100 ms pisk)
        if (now_ms - last_audio_beep_ms_ >= 1000UL) {
            last_audio_beep_ms_ = now_ms;
            audio_beeping_ = true;
            digitalWrite(HwConfig::Audio::SPEAKER_PIN, HIGH);
        } else if (audio_beeping_ && (now_ms - last_audio_beep_ms_ >= 150UL)) {
            digitalWrite(HwConfig::Audio::SPEAKER_PIN, LOW);
            audio_beeping_ = false;
        }
    } else {
        if (audio_beeping_) {
            digitalWrite(HwConfig::Audio::SPEAKER_PIN, LOW);
            audio_beeping_ = false;
        }
    }
}

bool UartClient::sendCommand(const AquaProtocol::AquariumCommand &cmd)
{
    // Gdy WeMos jest w trybie OTA, blokujemy wszelkie zwykłe komendy UART,
    // aby nie zakłócać procesu programowania pamięci Flash na ESP8266.
    if (wemos_in_ota_ && cmd.type != AquaProtocol::CommandType::EnterOta) {
        return false;
    }

    String line = AquaProtocol::serializeCommand(cmd);
    if (line.length() == 0) {
        return false;
    }
    Serial.println(line);
    return true;
}

bool UartClient::sendSetRelay(uint8_t channel, bool state)
{
    AquaProtocol::AquariumCommand cmd;
    cmd.type = AquaProtocol::CommandType::SetRelay;
    cmd.channel = channel;
    cmd.state = state;
    return sendCommand(cmd);
}

bool UartClient::sendSetTemp(float target, float hyst)
{
    AquaProtocol::AquariumCommand cmd;
    cmd.type = AquaProtocol::CommandType::SetTempTarget;
    cmd.temp_target = target;
    cmd.temp_hysteresis = hyst;
    return sendCommand(cmd);
}

bool UartClient::sendSetSchedule(uint8_t dev, uint8_t on_h, uint8_t on_m, uint8_t off_h, uint8_t off_m, bool en)
{
    AquaProtocol::AquariumCommand cmd;
    cmd.type = AquaProtocol::CommandType::SetSchedule;
    cmd.schedule_dev = dev;
    cmd.on_hour = on_h;
    cmd.on_minute = on_m;
    cmd.off_hour = off_h;
    cmd.off_minute = off_m;
    cmd.schedule_enabled = en;
    return sendCommand(cmd);
}

bool UartClient::sendFeedTrigger()
{
    AquaProtocol::AquariumCommand cmd;
    cmd.type = AquaProtocol::CommandType::FeedTrigger;
    return sendCommand(cmd);
}

bool UartClient::sendSyncLights()
{
    AquaProtocol::AquariumCommand cmd;
    cmd.type = AquaProtocol::CommandType::SyncLights;
    return sendCommand(cmd);
}

bool UartClient::sendSyncTime(uint32_t epoch)
{
    AquaProtocol::AquariumCommand cmd;
    cmd.type = AquaProtocol::CommandType::SyncTime;
    cmd.epoch_time = epoch;
    return sendCommand(cmd);
}

bool UartClient::sendClearAlarm()
{
    AquaProtocol::AquariumCommand cmd;
    cmd.type = AquaProtocol::CommandType::ClearAlarm;
    return sendCommand(cmd);
}

bool UartClient::sendSafeMode(bool enable)
{
    AquaProtocol::AquariumCommand cmd;
    cmd.type = AquaProtocol::CommandType::SetSafeMode;
    cmd.state = enable;
    return sendCommand(cmd);
}

bool UartClient::sendEnterOta(const char *ssid, const char *pass)
{
    AquaProtocol::AquariumCommand cmd = {};
    cmd.type = AquaProtocol::CommandType::EnterOta;
    if (ssid != nullptr && ssid[0] != '\0') {
        strncpy(cmd.ota_ssid, ssid, sizeof(cmd.ota_ssid) - 1);
        cmd.ota_ssid[sizeof(cmd.ota_ssid) - 1] = '\0';
    } else {
        cmd.ota_ssid[0] = '\0';
    }
    if (pass != nullptr && pass[0] != '\0') {
        strncpy(cmd.ota_pass, pass, sizeof(cmd.ota_pass) - 1);
        cmd.ota_pass[sizeof(cmd.ota_pass) - 1] = '\0';
    } else {
        cmd.ota_pass[0] = '\0';
    }

    wemos_ota_ack_received_ = false;
    wemos_ota_ack_ok_ = false;
    wemos_ota_ip_[0] = '\0';
    wemos_ota_start_ms_ = millis();

    return sendCommand(cmd);
}

bool UartClient::isWemosInOta() const
{
    return wemos_in_ota_;
}

bool UartClient::hasWemosOtaAck() const
{
    return wemos_ota_ack_received_;
}

bool UartClient::isWemosOtaAckOk() const
{
    return wemos_ota_ack_ok_;
}

const char *UartClient::getWemosOtaIp() const
{
    return wemos_ota_ip_;
}

uint32_t UartClient::getWemosOtaElapsedMs() const
{
    if (!wemos_in_ota_ || wemos_ota_start_ms_ == 0) {
        return 0;
    }
    return millis() - wemos_ota_start_ms_;
}

const AquaProtocol::AquariumAck &UartClient::getLatestAck() const
{
    return latest_ack_;
}

bool UartClient::hasNewAck() const
{
    return has_new_ack_;
}

void UartClient::clearNewAck()
{
    has_new_ack_ = false;
}

void UartClient::resetWemosOtaState()
{
    wemos_in_ota_ = false;
    wemos_ota_ack_received_ = false;
    wemos_ota_ack_ok_ = false;
    wemos_ota_ip_[0] = '\0';
    wemos_ota_start_ms_ = 0;
}

void UartClient::extractOtaIp(const char *src, char *dst, size_t dst_size)
{
    if (src == nullptr || dst == nullptr || dst_size == 0) {
        return;
    }
    dst[0] = '\0';

    const char *p = src;
    if (strncmp(p, "STA:", 4) == 0) {
        p += 4;
    } else if (strncmp(p, "AP:", 3) == 0) {
        p += 3;
    }

    while (*p == ' ') {
        p++;
    }

    size_t i = 0;
    while (*p != '\0' && *p != ' ' && *p != '\r' && *p != '\n' && i < dst_size - 1) {
        dst[i++] = *p++;
    }
    dst[i] = '\0';
}
```

---

## 6. Kontrakt Integracyjny z Interfejsem LVGL (Milestone 3)

W Milestone 3 (implementacja UI w `firmware/cyd_controller/src/gui_app.cpp`):
1. **Kliknięcie przycisku "Aktualizuj AquaBox (WeMos)":**
   ```cpp
   if (!g_uart_client.isControllerOnline()) {
       show_message_box("Błąd", "Brak połączenia UART z WeMos AquaBox!");
       return;
   }
   // Opcjonalne pobranie poświadczeń bieżącego Wi-Fi ze store'a:
   const char *ssid = wifi_current_ssid();
   const char *pass = wifi_current_pass();
   g_uart_client.sendEnterOta(ssid, pass);
   switch_to_wemos_ota_screen();
   ```
2. **Ekran/Modal Instrukcji OTA WeMos (`update_wemos_ota_timer_cb`):**
   - Sprawdza `g_uart_client.hasWemosOtaAck()`:
     - Jeśli `false` i minęło > 10 s: wyświetla ostrzeżenie "Oczekiwanie na odpowiedź WeMos...".
     - Jeśli `true` i `g_uart_client.isWemosOtaAckOk()`:
       - Pobiera `g_uart_client.getWemosOtaIp()`.
       - Wyświetla:
         *"WeMos gotowy do aktualizacji! Otwórz w przeglądarce: http://%s/update"*.
         *"Jeśli brak połączenia w sieci domowej, połącz się z AP AquaBox-OTA (192.168.4.1)"*.
     - Jeśli `true` i `!g_uart_client.isWemosOtaAckOk()`:
       - Wyświetla błąd z `g_uart_client.getLatestAck().message`.
   - Oblicza i wyświetla pozostały czas:
     `uint32_t remaining_s = 300 - (g_uart_client.getWemosOtaElapsedMs() / 1000);`
   - Gdy `!g_uart_client.isWemosInOta()` i WeMos powróci do nadawania normalnej telemetrii:
     - Wyświetla komunikat *"Aktualizacja zakończona sukcesem! WeMos zrestartowany."* i automatycznie zamyka ekran serwisowy.
3. **Przycisk "Anuluj / Wróć":**
   Wywołuje `g_uart_client.resetWemosOtaState()` i powraca do menu głównego.

---

## 7. Podsumowanie Wymagań dla Implementatora (Worker M1)

1. Przed wdrożeniem zmian w `uart_client.h` i `uart_client.cpp`, pliki `firmware/shared/uart_protocol/include/uart_protocol.h` i `src/uart_protocol.cpp` muszą zawierać:
   - `AquaProtocol::CommandType::EnterOta`
   - Pola `ota_ssid[33]` i `ota_pass[65]` w strukturze `AquaProtocol::AquariumCommand`
   - Pola `bool ota_mode` i `char ota_ip[16]` w strukturze `AquaProtocol::AquariumTelemetry`
2. Zastosować powyższe modyfikacje w `uart_client.h` i `uart_client.cpp`.
3. Wykonać kompilację weryfikacyjną:
   `pio run -e cyd_2_8`
4. Sprawdzić brak ostrzeżeń kompilatora i brak regresji rozmiaru sterty DRAM.
