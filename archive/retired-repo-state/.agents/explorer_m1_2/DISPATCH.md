# DISPATCH — explorer_m1_2

## Milestone 1: UART Protocol Extensions
Focus: `firmware/cyd_controller/src/uart_client.h` and `firmware/cyd_controller/src/uart_client.cpp`.

## Reference Request
Read `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\ORIGINAL_REQUEST.md` (section Follow-up — 2026-09-18T17:21:02Z).
Read `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\orchestrator_3\SCOPE.md`.
Read `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_survey_2\report.md` and `explorer_survey_3\report.md`.

## Tasks
1. Investigate how `UartClient` in CYD processes incoming lines and where `MessageType::Ack` should be handled.
2. Design `sendEnterOta(const char* ssid = nullptr, const char* pass = nullptr)` method in `UartClient`.
3. Design state tracking in `UartClient`: e.g. `isWemosInOta()`, `getWemosOtaIp()`, `hasWemosOtaAck()`.
4. Provide exact code modifications and diff recommendations in `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_m1_2\report.md`.
5. Deliver handoff and notify parent.

## 2026-09-18T17:32:28Z
You are explorer_m1_2, working in c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_m1_2.
Read your task in c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_m1_2\DISPATCH.md.
Also read c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\ORIGINAL_REQUEST.md under header "## Follow-up — 2026-09-18T17:21:02Z",
c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\orchestrator_3\SCOPE.md, and
c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_survey_3\report.md.

Analyze the exact changes needed in:
- firmware/cyd_controller/src/uart_client.h
- firmware/cyd_controller/src/uart_client.cpp
to handle ACK for EnterOta, implement sendEnterOta(ssid, pass), and track WeMos OTA status.

Write your report to c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_m1_2\report.md and handoff to c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_m1_2\handoff.md.
When finished, send a message to parent (conversation ID: 1e431c65-65c5-4543-abb7-e6a8950aeb32).

