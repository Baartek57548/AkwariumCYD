# BRIEFING — 2026-09-18T17:34:00Z

## Mission
Analyze exact changes in uart_protocol.h and uart_protocol.cpp for EnterOta, AquariumCommand (ota_ssid, ota_pass), and AquariumTelemetry (ota_mode, ota_ip).

## 🔒 My Identity
- Archetype: explorer
- Roles: investigation, synthesis
- Working directory: c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_m1_1
- Original parent: 1e431c65-65c5-4543-abb7-e6a8950aeb32
- Milestone: M1 (UART NDJSON Extensions)

## 🔒 Key Constraints
- Read-only investigation — do NOT implement source changes directly in firmware/
- Analyze exact changes in firmware/shared/uart_protocol/include/uart_protocol.h and firmware/shared/uart_protocol/src/uart_protocol.cpp
- Calculate exact JSON capacities and memory impact

## Current Parent
- Conversation ID: 1e431c65-65c5-4543-abb7-e6a8950aeb32
- Updated: 2026-09-18T17:32:28Z

## Investigation State
- **Explored paths**: `firmware/shared/uart_protocol/include/uart_protocol.h`, `firmware/shared/uart_protocol/src/uart_protocol.cpp`, `firmware/wemos_controller/src/main.cpp`, `firmware/cyd_controller/src/uart_client.cpp`, `firmware/cyd_controller/include/wifi_credential_store.h`, `platformio.ini`
- **Key findings**:
  - `CommandType::EnterOta` added as enum value 10 (0x0A), 100% backward compatible with values 0..9.
  - `AquariumCommand` requires `char ota_ssid[33]` and `char ota_pass[65]`, matching IEEE 802.11 and WPA2-PSK limits and `wifi_credential_store.h`.
  - `AquariumTelemetry` requires `bool ota_mode` and `char ota_ip[16]`.
  - All `StaticJsonDocument` sizes (384, 512, 768) have ample free headroom (>46% to >75%). No buffer size increases required.
  - Recommended bumping `output.reserve(160)` -> `256` in `serializeCommand` and `output.reserve(256)` -> `320` in `serializeTelemetry` to prevent heap reallocation.
  - Baseline compilation verified: `wemos_d1_uno` SUCCESS, `cyd_2_8` SUCCESS, `native` SUCCESS (45/45 tests).
- **Unexplored areas**: None for M1 UART protocol scope.

## Key Decisions Made
- Use bounds-checked `strncpy` with explicit null terminator on last index for all char arrays.
- Dual-key fallback in `parseMessage()` for `ota_mode` (`doc["ota_mode"] | doc["ota"] | false`) and ACK status (`"ok":true` or `"status":"ok"`).
- Omitting `ota_ip` in `serializeTelemetry` when empty to avoid sending unnecessary bytes in normal operation.

## Artifact Index
- report.md — Detailed analysis report of UART protocol changes with exact diffs
- handoff.md — 5-component handoff report for implementer

