# BRIEFING — 2026-09-18T17:28:30Z

## Mission
Investigate ESP32 CYD codebase (env cyd_2_8, OTA mechanisms, Wi-Fi STA/AP fallback, LVGL UI architecture, OTA UI controls, build status) and produce detailed report and roadmap.

## 🔒 My Identity
- Archetype: explorer
- Roles: explorer, survey, analysis
- Working directory: c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_survey_2
- Original parent: 1e431c65-65c5-4543-abb7-e6a8950aeb32
- Milestone: ESP32 CYD OTA & UI Survey

## 🔒 Key Constraints
- Read-only investigation — do NOT implement
- Write only to your folder (`.agents/explorer_survey_2/`)
- Final output in `report.md` and `handoff.md`
- Use send_message back to parent when done

## Current Parent
- Conversation ID: 1e431c65-65c5-4543-abb7-e6a8950aeb32
- Updated: 2026-09-18T17:28:30Z

## Investigation State
- **Explored paths**: `platformio.ini`, `firmware/cyd_controller/include/config.h`, `gui_app.h`, `gui_app.cpp`, `main.cpp`, `runtime_controller.cpp`, `secure_ota.cpp`, `ota_guard.cpp`, `uart_client.cpp`, `firmware/shared/uart_protocol/`, `firmware/wemos_controller/`, `obsidian/Procedura - Programowanie i Wgrywanie Firmware.md`
- **Key findings**:
  1. `env:cyd_2_8` uses `min_spiffs.csv` (1.88MB dual OTA slots), `AQUARIUM_ALLOW_UNSIGNED_ARDUINO_OTA=0` (disables raw ArduinoOTA in production).
  2. Web OTA endpoint `/update` exists in `gui_app.cpp`, currently requires `.aqfw` RSA-3072 signed packages.
  3. Wi-Fi AP fallback SSID currently `"cydAkwarium-OTA"` in `config.h`, needs update to `"AquaCYD-OTA"`.
  4. LVGL UI has 5 main pages (Start, Plan, Moduly, Wykres, System) and dynamic subpages (`ActiveSubpage::Wifi`, `ActiveSubpage::Service`).
  5. UI controls: "Aktualizuj Panel (CYD)" and "Aktualizuj AquaBox (WeMos)" recommended for integration in `subpage_wifi` / dedicated OTA modal.
  6. UART protocol NDJSON needs `CommandType::EnterOta`, ACK, and telemetry suppression during flash.
  7. Compilation of `cyd_2_8` verified with `pio run -e cyd_2_8`.
- **Unexplored areas**: None. Survey complete.

## Key Decisions Made
- Mapped entire architecture and produced comprehensive survey and roadmap in `report.md`.

## Artifact Index
- report.md — comprehensive findings and implementation roadmap
- handoff.md — 5-component handoff report
- progress.md — liveness heartbeat
