# BRIEFING — 2026-09-18T17:31:00Z

## Mission
Investigate the WeMos D1 Uno ESP8266 (AquaBox) codebase for OTA implementation (STA+AP fallback, Web OTA, ArduinoOTA, relay fail-safe, spare GPIO fallback button, 5-min timeout, build status).

## 🔒 My Identity
- Archetype: explorer
- Roles: survey, code analysis, system architecture investigation
- Working directory: c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_survey_1
- Original parent: 1e431c65-65c5-4543-abb7-e6a8950aeb32
- Milestone: AquaBox OTA Survey & Architecture

## 🔒 Key Constraints
- Read-only investigation — do NOT implement
- Deliver findings to report.md
- Produce 5-component handoff.md and send_message to parent

## Current Parent
- Conversation ID: 1e431c65-65c5-4543-abb7-e6a8950aeb32
- Updated: 2026-09-18T17:23:19Z

## Investigation State
- **Explored paths**:
  - `platformio.ini` (env:wemos_d1_uno, flags, ldscript, libs)
  - `firmware/wemos_controller/include/wemos_config.h`
  - `firmware/wemos_controller/src/main.cpp`
  - `firmware/wemos_controller/src/hardware_i2c.cpp`, `.h`
  - `firmware/wemos_controller/src/storage_manager.cpp`, `.h`
  - `firmware/wemos_controller/src/temp_control.cpp`, `automation_engine.cpp`
  - `firmware/shared/uart_protocol/include/uart_protocol.h`, `uart_protocol.cpp`
  - `obsidian/Hardware - WeMos D1 Uno ESP8266.md`, `Hardware - Zworki UART i Izolacja.md`, `Procedura - Programowanie i Wgrywanie Firmware.md`
- **Key findings**:
  - `pio run -e wemos_d1_uno` builds with 100% SUCCESS (RAM: 36.5%, Flash: 27.9%).
  - Normal mode runs with `WiFi.mode(WIFI_OFF)` and `WiFi.forceSleepBegin()`.
  - Relays on MCP23017 Port A are Active-Low. Fail-safe shutdown requires `g_hardware_i2c.setAllRelays(0x00)` (shadow 0xFF) and pausing thermostat/schedules.
  - GPIO12 (D6) is unassigned, safe for boot (no boot strapping conflict), supports `INPUT_PULLUP`, ideal for hardware OTA fallback button.
  - Core has built-in `ESP8266WebServer`, `ESP8266HTTPUpdateServer`, and `ArduinoOTA`.
  - EEPROM emulation is located outside the 1MB sketch partition and is not touched during OTA.
- **Unexplored areas**: None, all aspects investigated and verified.

## Key Decisions Made
- Selected GPIO12 (D6) with `INPUT_PULLUP` as physical OTA fallback button.
- Recommended architecture: dedicated `OtaManager` module in `firmware/wemos_controller/src/`.
- Proposed `enter_ota` command in `uart_protocol` and `ota` flag in telemetry.

## Artifact Index
- report.md — detailed findings and implementation roadmap
- handoff.md — 5-component handoff report
- progress.md — progress heartbeat
