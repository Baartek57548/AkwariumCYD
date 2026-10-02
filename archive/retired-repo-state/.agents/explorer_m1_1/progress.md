# Progress — explorer_m1_1

Last visited: 2026-09-18T17:36:20Z

- [x] Read DISPATCH.md, ORIGINAL_REQUEST.md, SCOPE.md, explorer_survey_3/report.md
- [x] Created BRIEFING.md and initialized progress tracking
- [x] Baseline compilation check: `wemos_d1_uno` compiled successfully (9.6s, RAM: 36.5%, Flash: 27.9%)
- [x] Baseline compilation check: `cyd_2_8` compiled successfully (44.97s, RAM: 27.0%, Flash: 83.6%)
- [x] Baseline unit tests check: `native` in `firmware/cyd_controller` passed (45/45 tests passed in 2.49s)
- [x] Detailed analysis of `uart_protocol.h`:
  - `CommandType::EnterOta` enum value and placement (value 10, preserves 0..9)
  - `AquariumCommand` struct changes (`char ota_ssid[33]`, `char ota_pass[65]`)
  - `AquariumTelemetry` struct changes (`bool ota_mode`, `char ota_ip[16]`)
- [x] Detailed analysis of `uart_protocol.cpp`:
  - Serialization: `serializeCommand` changes (case `CommandType::EnterOta`), `output.reserve` analysis (160 -> 192/256 recommended)
  - Serialization: `serializeTelemetry` changes (`doc["ota_mode"]` / `doc["ota"]`, `doc["ota_ip"]`), `output.reserve` (256 -> 320 recommended)
  - Deserialization: `parseMessage` command handling (`doc["ssid"]`, `doc["pass"]`)
  - Deserialization: `parseMessage` telemetry handling (`ota_mode`, `ota_ip` with robust fallback)
  - ArduinoJson capacity math for all `StaticJsonDocument` sizes (384, 512, 768 all have ample headroom >46% to >75%)
- [x] Backward compatibility and failure mode analysis
- [x] Write `report.md`
- [x] Write `handoff.md`
- [x] Notify parent via `send_message`


