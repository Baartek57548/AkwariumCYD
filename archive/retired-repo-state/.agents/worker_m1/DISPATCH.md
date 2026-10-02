# DISPATCH — worker_m1

## Role
Implementation of Milestone 1: UART Protocol NDJSON Extensions & Shared Contracts.

## Exclusive Write Ownership
You exclusively own and may edit:
- `firmware/shared/uart_protocol/include/uart_protocol.h`
- `firmware/shared/uart_protocol/src/uart_protocol.cpp`
- `firmware/cyd_controller/include/uart_client.h`
- `firmware/cyd_controller/src/uart_client.cpp`
- `firmware/cyd_controller/test/test_native_domain/test_main.cpp` (or test files in that folder)

DO NOT modify any files in `firmware/wemos_controller/` or other modules in this milestone.

## Input Reference Files
Read before starting:
1. `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\ORIGINAL_REQUEST.md` (specifically header `## Follow-up — 2026-09-18T17:21:02Z`).
2. `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\orchestrator_3\SCOPE.md`.
3. `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_m1_1\report.md` (and handoff.md).
4. `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_m1_2\report.md` (and handoff.md).
5. `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_m1_3\report.md` (and handoff.md).

## Detailed Tasks
1. Update `firmware/shared/uart_protocol/include/uart_protocol.h`:
   - Add `EnterOta` to `enum class CommandType : uint8_t` (value 10).
   - Add `char ota_ssid[33];` and `char ota_pass[65];` to `struct AquariumCommand`.
   - Add `bool ota_mode;` and `char ota_ip[16];` to `struct AquariumTelemetry`.
   - Support native test compatibility if required.
2. Update `firmware/shared/uart_protocol/src/uart_protocol.cpp`:
   - `serializeCommand()`: add case for `CommandType::EnterOta`, serialize optional `ssid` and `pass`. Set `output.reserve(256)`.
   - `serializeTelemetry()`: serialize `ota_mode` and `ota_ip` (when non-empty). Set `output.reserve(320)`.
   - `parseMessage()`: parse `"enter_ota"` command into `out_msg.command` with safe bounds and string termination.
   - Support `"ota_mode"` with fallback `"ota"`, and ACK `"status":"ok"` / `"ok":true`.
3. Update `firmware/cyd_controller/include/uart_client.h` and `firmware/cyd_controller/src/uart_client.cpp`:
   - Handle `MessageType::Ack` in `processLine()` to capture `latest_ack_` and extract WeMos OTA confirmation/IP.
   - Implement `sendEnterOta(const char* ssid = nullptr, const char* pass = nullptr)`.
   - Implement OTA state tracking methods: `isWemosInOta()`, `hasWemosOtaAck()`, `isWemosOtaAckOk()`, `getWemosOtaIp()`, `getWemosOtaElapsedMs()`, `resetWemosOtaState()`.
   - In `sendCommand()`: suppress command TX while `wemos_in_ota_` is true (except `EnterOta`).
   - In `update()`: pause 3s offline timeout during `wemos_in_ota_` with 6-minute safety watchdog.
   - In `checkAlarmAudio()`: mute buzzer beeping during `wemos_in_ota_`.
4. Add unit test cases to `firmware/cyd_controller/test/test_native_domain/test_main.cpp`:
   - Test `EnterOta` command serialization and deserialization.
   - Test telemetry `ota_mode` and `ota_ip` serialization/deserialization.
   - Test ACK parsing.
5. Verification:
   - Run `pio test -e native` in `firmware/cyd_controller`.
   - Run `pio run -e wemos_d1_uno` from workspace root.
   - Run `pio run -e cyd_2_8` from workspace root.
   - Ensure all builds and tests pass cleanly with exit code 0.

## Mandatory Integrity Warning
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A teamwork_preview_auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

## Output
Document your changes, build/test results, and verification in `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\worker_m1\handoff.md` and send a completion message to parent.
