# Project Scope: Wireless OTA Update System (WeMos D1 Uno & ESP32 CYD)

## Architecture
- **AquaBox (WeMos D1 Uno ESP8266)**: Autonomous real-time aquarium controller. In normal mode: `WiFi.mode(WIFI_OFF)` and `WiFi.forceSleepBegin()`. Dual-trigger OTA: remote via UART NDJSON command `enter_ota` or hardware push button on GPIO12 (D6). In OTA mode: immediate fail-safe relay shutdown (`setAllRelays(0x00)`), Wi-Fi STA with AP fallback (`AquaBox-OTA`), Web OTA HTTP server (port 80) and ArduinoOTA (port 8266), 300s inactivity watchdog, and telemetry suppression.
- **CYD Panel (ESP32-2432S028R)**: Touch screen gateway. Provides LVGL UI service controls ("Aktualizuj Panel (CYD)" and "Aktualizuj AquaBox (WeMos)"), AP fallback (`AquaCYD-OTA`), Web OTA portal and ArduinoOTA support.
- **Shared UART Protocol (NDJSON)**: Bidirectional serial communication on Serial0 (115200 baud). Extended with `CommandType::EnterOta`, ACK response, and `AquariumTelemetry` flags (`ota_mode`, `ota_ip`).

## Feature Inventory
| # | Feature | Description | Milestone | Source |
|---|---------|-------------|-----------|--------|
| 1 | UART NDJSON `EnterOta` Command | Add `CommandType::EnterOta` serializer/deserializer to `uart_protocol` | M1 | Survey (Explorer 3) |
| 2 | UART Telemetry OTA Status Flags | Extend `AquariumTelemetry` with `bool ota_mode` and `char ota_ip[16]` | M1 | Survey (Explorer 3) |
| 3 | CYD `UartClient` OTA Handling | Add ACK handling for `EnterOta` and `sendEnterOta()` in `UartClient` | M1 | Survey (Explorer 2 & 3) |
| 4 | WeMos Normal Mode RF Sleep | Ensure `WiFi.mode(WIFI_OFF)` and `WiFi.forceSleepBegin()` in normal loop | M2 | Survey (Explorer 1) |
| 5 | WeMos Fail-Safe Relay Cutoff | Immediate `g_hardware_i2c.setAllRelays(0x00)` de-energizing all loads | M2 | Survey (Explorer 1) |
| 6 | WeMos Dual-Trigger Entry | Trigger OTA via UART command `enter_ota` or boot GPIO12 (D6) button | M2 | Survey (Explorer 1 & 3) |
| 7 | WeMos STA + AP Fallback Mode | Connect to STA or fallback to SoftAP `AquaBox-OTA` (192.168.4.1) | M2 | Survey (Explorer 1) |
| 8 | WeMos Web OTA & ArduinoOTA | ESP8266 HTTP update server on port 80 and ArduinoOTA on port 8266 | M2 | Survey (Explorer 1) |
| 9 | WeMos 300s Safety Watchdog | Automatically reboot to normal mode if no upload occurs in 5 minutes | M2 | Survey (Explorer 1) |
| 10 | WeMos EEPROM Persistence | Ensure EEPROM layout and config migration preserve user settings | M2 | Survey (Explorer 1 & 3) |
| 11 | WeMos UART Telemetry Silence | Silence periodic telemetry during OTA mode to prevent flash corruption | M2 | Survey (Explorer 1 & 3) |
| 12 | CYD AP Fallback Naming | Update fallback SSID to `AquaCYD-OTA` in `config.h` and UI | M3 | Survey (Explorer 2) |
| 13 | CYD Service Mode ArduinoOTA | Ensure `ArduinoOTA` is enabled/accessible for direct PlatformIO upload | M3 | Survey (Explorer 2) |
| 14 | CYD LVGL UI "Aktualizuj Panel" | Button to initiate CYD OTA mode and show Web/PlatformIO guidelines | M3 | Survey (Explorer 2) |
| 15 | CYD LVGL UI "Aktualizuj WeMos"| Button sending UART `enter_ota` and displaying AquaBox SSID/IP screen | M3 | Survey (Explorer 2) |
| 16 | Obsidian OTA Documentation | Update `obsidian/Procedura - Programowanie i Wgrywanie Firmware.md` | M4 | Survey (Explorer 3) |
| 17 | Full Clean Compilation & Tests | Verify `pio run -e wemos_d1_uno`, `pio run -e cyd_2_8`, and native tests | M4 | Survey (All Explorers) |

## Milestones
| # | Name | Scope | Dependencies | Status |
|---|------|-------|-------------|--------|
| 1 | M1: UART NDJSON Extensions | Add `EnterOta` command, ACK handling, `AquariumTelemetry` fields, and CYD `UartClient` methods | none | PLANNED |
| 2 | M2: WeMos AquaBox OTA Subsystem | Normal mode RF sleep, GPIO12 button, fail-safe relay cutoff, STA+AP fallback (`AquaBox-OTA`), Web OTA + ArduinoOTA, 300s watchdog, EEPROM migration, UART telemetry pause | M1 | PLANNED |
| 3 | M3: CYD OTA & LVGL UI Integration | SSID `AquaCYD-OTA`, ArduinoOTA enable in service mode, LVGL buttons for CYD & WeMos OTA with IP/SSID guidance | M1 | PLANNED |
| 4 | M4: Docs, Build Verification & Audit | Update Obsidian documentation, verify both PlatformIO builds (`wemos_d1_uno`, `cyd_2_8`), run native tests, challenger & forensic audit | M1, M2, M3 | PLANNED |

## Code Layout
- `firmware/shared/uart_protocol/include/uart_protocol.h`: Shared protocol definitions, `CommandType`, `AquariumTelemetry`.
- `firmware/shared/uart_protocol/src/uart_protocol.cpp`: JSON serialization and deserialization.
- `firmware/cyd_controller/src/uart_client.h` & `uart_client.cpp`: CYD UART transmission and ACK handling.
- `firmware/wemos_controller/include/wemos_config.h`: WeMos pin definitions, GPIO12 button, OTA settings.
- `firmware/wemos_controller/src/main.cpp`: Normal vs OTA mode dispatch, button check, loop handling.
- `firmware/wemos_controller/src/ota_manager.h` & `ota_manager.cpp`: WeMos OTA state machine, Wi-Fi, Web OTA, ArduinoOTA, watchdog.
- `firmware/wemos_controller/src/storage_manager.cpp`: EEPROM migration and safety.
- `firmware/cyd_controller/include/config.h`: CYD network and OTA configuration (`AquaCYD-OTA`).
- `firmware/cyd_controller/src/gui_app.cpp`: LVGL UI service pages, OTA buttons, instructions modal.
- `obsidian/Procedura - Programowanie i Wgrywanie Firmware.md`: Complete OTA procedure documentation.

## Interface Contracts
### UART `EnterOta` Command & ACK
- CYD sends: `{"type":"cmd","cmd":"enter_ota"}\n` (or with optional `ssid` and `pass`).
- WeMos replies: `{"type":"ack","cmd":"enter_ota","status":"ok"}\n`.
- Telemetry broadcast: `{"type":"telemetry", ... ,"ota_mode":true,"ota_ip":"192.168.4.1"}\n`.
- Telemetry is suppressed during active flashing to protect ESP8266 flash cache.
### Hardware Relay Fail-Safe
- Upon entering OTA mode (via UART or GPIO12 button), WeMos immediately calls `g_hardware_i2c.setAllRelays(0x00)`. All active-low relays are turned OFF.
