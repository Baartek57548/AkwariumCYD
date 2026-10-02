# BRIEFING — 2026-09-18T17:31:31Z

## Mission
Investigate UART NDJSON protocol, safety cutoff, EEPROM layout/persistence, and Obsidian documentation for dual-chip OTA update system.

## 🔒 My Identity
- Archetype: explorer
- Roles: investigation, synthesis
- Working directory: c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_survey_3
- Original parent: 1e431c65-65c5-4543-abb7-e6a8950aeb32
- Milestone: OTA Investigation Survey

## 🔒 Key Constraints
- Read-only investigation — do NOT implement
- Analyze UART NDJSON protocol, enter_ota command & ACK, telemetry schema extensions, UART telemetry suppression during OTA, hardware safety cutoff, EEPROM layout & persistence, Obsidian documentation.

## Current Parent
- Conversation ID: 1e431c65-65c5-4543-abb7-e6a8950aeb32
- Updated: 2026-09-18T17:31:31Z

## Investigation State
- **Explored paths**: `firmware/shared/uart_protocol`, `firmware/wemos_controller`, `firmware/cyd_controller`, `platformio.ini`, `obsidian/*.md`
- **Key findings**:
  - `uart_protocol` located at `firmware/shared/uart_protocol`, uses ArduinoJson 6.21.6; lacks `enter_ota` command, `ota_mode`/`ota_ip` fields, and ACK processing in `UartClient`.
  - Hardware safety requires immediate `g_hardware_i2c.setAllRelays(0x00)` (<2 ms via MCP23017 active-low) to prevent boiling or CO2 poisoning.
  - Free pin for WeMos hardware button is D6 (`GPIO12`), safe at boot with internal pullup.
  - Flash map `eagle.flash.4m2m.ld` places EEPROM sector at `0x3FB000` (untouched by sketch OTA flash).
  - EEPROM logical integrity requires V1->V2 struct migration in `StorageManager::loadConfig()` to avoid resetting pH calibration and schedules.
  - Obsidian documentation `obsidian/Procedura - Programowanie i Wgrywanie Firmware.md` needs revamp making wireless OTA primary and USB disaster recovery.
- **Unexplored areas**: None for survey scope.

## Key Decisions Made
- Formulated 4-phase implementation roadmap documented in `report.md`.
- Produced 5-component handoff in `handoff.md`.

## Artifact Index
- `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_survey_3\report.md` — Detailed technical analysis report
- `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_survey_3\handoff.md` — 5-component handoff report
- `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_survey_3\progress.md` — Progress tracker
