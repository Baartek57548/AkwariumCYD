# DISPATCH — explorer_m1_1

## Milestone 1: UART Protocol Extensions
Focus: `firmware/shared/uart_protocol/include/uart_protocol.h` and `firmware/shared/uart_protocol/src/uart_protocol.cpp`.

## Reference Request
Read `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\ORIGINAL_REQUEST.md` (section Follow-up — 2026-09-18T17:21:02Z).
Read `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\orchestrator_3\SCOPE.md`.
Read `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_survey_3\report.md`.

## Tasks
1. Verify exact enum values and struct fields to add for `CommandType::EnterOta`, `AquariumCommand` (`ota_ssid`, `ota_pass`), and `AquariumTelemetry` (`bool ota_mode`, `char ota_ip[16]`).
2. Verify ArduinoJson document capacity requirements for serialization and deserialization of the new fields in `uart_protocol.cpp`.
3. Provide exact code modifications and diff recommendations in `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_m1_1\report.md`.
4. Deliver handoff and notify parent.

## 2026-09-18T17:32:28Z
You are explorer_m1_1, working in c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_m1_1.
Read your task in c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_m1_1\DISPATCH.md.
Also read c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\ORIGINAL_REQUEST.md under header "## Follow-up — 2026-09-18T17:21:02Z",
c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\orchestrator_3\SCOPE.md, and
c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_survey_3\report.md.

Analyze the exact changes needed in:
- firmware/shared/uart_protocol/include/uart_protocol.h
- firmware/shared/uart_protocol/src/uart_protocol.cpp
for CommandType::EnterOta, AquariumCommand (ota_ssid, ota_pass), and AquariumTelemetry (ota_mode, ota_ip), plus serialization and deserialization.

Write your report to c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_m1_1\report.md and handoff to c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_m1_1\handoff.md.
When finished, send a message to parent (conversation ID: 1e431c65-65c5-4543-abb7-e6a8950aeb32).
