# DISPATCH — explorer_m1_3

## Milestone 1: UART Protocol Extensions
Focus: Unit testing and cross-platform verification for protocol modifications.

## Reference Request
Read `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\ORIGINAL_REQUEST.md` (section Follow-up — 2026-09-18T17:21:02Z).
Read `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\orchestrator_3\SCOPE.md`.
Read `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_survey_3\report.md`.

## Tasks
1. Check existing tests in `firmware/cyd_controller/test/test_native_domain/` (how commands, NDJSON, and telemetry are tested).
2. Design unit test cases for `EnterOta` command serialization/deserialization, ACK handling, and telemetry `ota_mode` / `ota_ip` fields.
3. Verify that `pio test -e native` can execute these tests without hardware dependencies.
4. Provide recommendations in `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_m1_3\report.md`.
5. Deliver handoff and notify parent.
