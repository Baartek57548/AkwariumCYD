# DISPATCH — challenger_m1_2

## Role
Adversarial Verifier for CYD `UartClient` OTA State Machine & Protections.

## Task
1. Read `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\ORIGINAL_REQUEST.md` (header `## Follow-up — 2026-09-18T17:21:02Z`).
2. Read `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\orchestrator_3\SCOPE.md`.
3. Inspect `firmware/cyd_controller/include/uart_client.h` and `src/uart_client.cpp`.
4. Test and verify the `UartClient` state machine under extreme scenarios:
   - Rapid sequence of ACKs and telemetry frames.
   - Command TX suppression: verify `sendCommand` returns false during `wemos_in_ota_` (except `EnterOta`).
   - Offline timeout: verify standard 3s timeout is paused during OTA, but 6-minute watchdog resets state if WeMos never reboots.
   - Audio muting: verify buzzer is muted during OTA.
   - Recovery: verify that arrival of telemetry with `ota_mode == false` automatically and cleanly resets OTA state.
5. Document findings, test harness results, and verdict (PASS/FAIL) in `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\challenger_m1_2\handoff.md`.
6. Send completion message to parent.

## 2026-09-18T17:55:54Z
You are challenger_m1_2, working in c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\challenger_m1_2.
Your task is in c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\challenger_m1_2\DISPATCH.md.
Read c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\ORIGINAL_REQUEST.md and
c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\orchestrator_3\SCOPE.md.

Stress-test and verify the UartClient OTA state machine in firmware/cyd_controller/src/uart_client.cpp:
- Command TX suppression during OTA.
- 3s offline timeout pausing with 6-minute watchdog timeout.
- Buzzer alarm muting during OTA.
- State transitions on ACK and telemetry.
- Clean recovery when WeMos exits OTA.

Write your report and verdict (PASS/FAIL) to:
c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\challenger_m1_2\handoff.md
Send a completion message to parent (conversation ID: 1e431c65-65c5-4543-abb7-e6a8950aeb32).
