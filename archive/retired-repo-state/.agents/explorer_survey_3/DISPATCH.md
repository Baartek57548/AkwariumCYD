# DISPATCH — explorer_survey_3

## Role
Investigate UART Protocol (NDJSON), Hardware Safety, EEPROM preservation, and Obsidian Documentation.

## Task
1. Read `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\ORIGINAL_REQUEST.md` (specifically header `## Follow-up — 2026-09-18T17:21:02Z`).
2. Search and analyze:
   - `uart_protocol` library (location, structures, serializers, parsers for NDJSON between CYD and WeMos).
   - Command `enter_ota` (CYD -> WeMos) and ACK mechanism.
   - Telemetry schema: how service/OTA mode flags are expressed.
   - Mechanism to suppress UART telemetry during flashing to prevent bus noise and flash corruption.
   - Hardware safety: immediate cutoff of heater relay, CO2 solenoid, and power loads before OTA flashing begins.
   - Configuration persistence in EEPROM across firmware flashing.
   - Obsidian documentation file: `obsidian/Procedura - Programowanie i Wgrywanie Firmware.md` (and any related doc files).
3. Write detailed findings, protocol specification changes, safety guarantees, EEPROM layout analysis, and documentation updates plan to `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_survey_3\report.md`.
4. Send completion message when done.
