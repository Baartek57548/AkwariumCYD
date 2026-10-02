# Forensic Integrity Audit Report — Milestone 1

**Auditor:** `auditor_m1_1`  
**Date:** 2026-09-18  
**Working Directory:** `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\auditor_m1_1`  
**Target:** Milestone 1 (UART Protocol NDJSON Extensions & Shared Contracts)  
**Profile:** General Project (Integrity Mode: `development` per `ORIGINAL_REQUEST.md`)  
**Verdict:** **CLEAN**

---

## Forensic Audit Summary

| Forensic Check | Status | Details |
|---|---|---|
| **1. Hardcoded Output Detection** | **PASS** | No hardcoded test results, expected outputs, or test-specific branches in production source code. |
| **2. Facade Implementation Detection** | **PASS** | No stubbed functions or empty constant returns. Dynamic serialization and real state latching verified. |
| **3. Pre-populated Artifact Detection** | **PASS** | Workspace clean. No pre-existing test results, fake logs, or attestation artifacts. |
| **4. Self-Certifying Test Detection** | **PASS** | Tests in `test_main.cpp` genuinely exercise production code (`serializeCommand`, `parseMessage`, `serializeAck`, `serializeTelemetry`) across round-trips and adversarial edge cases. |
| **5. Build & Behavioral Verification** | **PASS** | All 3 targets independently built and tested: native tests (57/57 passed), `wemos_d1_uno` (SUCCESS), `cyd_2_8` (SUCCESS). |
| **6. Dependency Audit** | **PASS** | No prohibited delegation. Standard libraries (ArduinoJson v6, Unity) properly configured. |

---

## 1. Observation

### 1.1 Source Code Inspection
The following files were inspected for integrity violations, facades, and hardcoding:
1. `firmware/shared/uart_protocol/include/uart_protocol.h`
   - Lines 14–37: Provides a genuine C++11 desktop compatibility shim for `String` wrapping `std::string` with `std::snprintf` and `std::to_string` to enable dual compilation without modifying production code logic.
   - Lines 74–86: Extends `CommandType` with `EnterOta = 10`.
   - Lines 98–114: Extends `AquariumTelemetry` with `bool ota_mode` and `char ota_ip[16]`.
   - Lines 117–132: Extends `AquariumCommand` with `char ota_ssid[33]` and `char ota_pass[65]`.

2. `firmware/shared/uart_protocol/src/uart_protocol.cpp`
   - Lines 6–32 (`serializeTelemetry`): Dynamically formats JSON document using ArduinoJson from telemetry fields. `ota_mode` is serialized; `ota_ip` is included only when non-empty (`t.ota_ip[0] != '\0'`).
   - Lines 34–96 (`serializeCommand`): Under `CommandType::EnterOta`, serializes `"cmd": "enter_ota"`, adding `"ssid"` and `"pass"` only if non-empty.
   - Lines 98–112 (`serializeAck`): Dynamically serializes ACK frames with optional message strings.
   - Lines 126–229 (`parseMessage`): Parses incoming NDJSON.
     - Telemetry: Extracts `ota_mode` (with fallback to key `"ota"`), safely copies `ota_ip` with bounded `strncpy` and explicit null termination (`sizeof(out_msg.telemetry.ota_ip) - 1`).
     - Command: Extracts `enter_ota`, copying `ssid` (bounded to 32 chars) and `pass` (bounded to 64 chars) with explicit null termination.
     - Ack: Supports both `"ok": true/false` and `"status": "ok"/"error"`, bounded string copy into `out_msg.ack.message`.

3. `firmware/cyd_controller/include/uart_client.h` & `firmware/cyd_controller/src/uart_client.cpp`
   - Genuine state tracking implementation:
     - `wemos_in_ota_`, `wemos_ota_ack_received_`, `wemos_ota_ack_ok_`, `wemos_ota_ip_`, `wemos_ota_start_ms_`.
   - `sendEnterOta(ssid, pass)` (lines 239–262): Safely populates `AquariumCommand`, resets ACK tracking state, records timestamp, and transmits via `sendCommand()`.
   - `update()` (lines 94–130): Pauses 3-second offline timeout while `wemos_in_ota_` is active, enforcing a 6-minute (360,000 ms) safety watchdog.
   - `checkAlarmAudio()` (lines 132–153): Mutes alarm buzzer during `wemos_in_ota_`.
   - `sendCommand()` (lines 155–169): Mutes regular UART command transmission while `wemos_in_ota_` is active to prevent ESP8266 flash bus contention.
   - `extractOtaIp()` (lines 316–339): Dynamically parses both `"STA:192.168.1.150"` and `"AP:192.168.4.1"` prefixes or raw IP strings into destination buffer with strict bounds checking.

4. `firmware/cyd_controller/test/test_native_domain/test_main.cpp`
   - Lines 1630–1856: Adds 12 behavior tests:
     - `test_uart_protocol_enter_ota_serialization_round_trip_with_credentials`
     - `test_uart_protocol_enter_ota_serialization_default_credentials_omitted`
     - `test_uart_protocol_enter_ota_deserialization_buffer_boundary_safety`
     - `test_uart_protocol_ack_enter_ota_sta_success`
     - `test_uart_protocol_ack_enter_ota_ap_fallback`
     - `test_uart_protocol_ack_status_string_compatibility`
     - `test_uart_protocol_ack_missing_optional_message`
     - `test_uart_protocol_telemetry_ota_active_serialization_round_trip`
     - `test_uart_protocol_telemetry_ota_normal_mode_inactive`
     - `test_uart_protocol_telemetry_backward_compatibility_legacy_frames`
     - `test_uart_protocol_telemetry_ota_fallback_key_support`
     - `test_uart_protocol_telemetry_ip_buffer_bounds_check`
   - Lines 1907–1918: All 12 tests are registered and executed via `RUN_TEST`.

---

### 1.2 Empirical Tool Executions & Verbatim Output

#### Check 1: Native Test Suite Execution
Command: `pio test -e native` (in `firmware/cyd_controller`)
```
Verbosity level can be increased via `-v, -vv, or -vvv` option
Collected 1 tests

Processing test_native_domain in native environment
--------------------------------------------------------------------------------
Building...
Testing...
test\test_native_domain\test_main.cpp:1860: test_regular_and_wrapped_windows	[PASSED]
... [44 pre-existing domain tests PASSED] ...
test\test_native_domain\test_main.cpp:1904: test_feeding_schedule_day_latch_persists_across_minutes	[PASSED]
test\test_native_domain\test_main.cpp:1907: test_uart_protocol_enter_ota_serialization_round_trip_with_credentials	[PASSED]
test\test_native_domain\test_main.cpp:1908: test_uart_protocol_enter_ota_serialization_default_credentials_omitted	[PASSED]
test\test_native_domain\test_main.cpp:1909: test_uart_protocol_enter_ota_deserialization_buffer_boundary_safety	[PASSED]
test\test_native_domain\test_main.cpp:1910: test_uart_protocol_ack_enter_ota_sta_success	[PASSED]
test\test_native_domain\test_main.cpp:1911: test_uart_protocol_ack_enter_ota_ap_fallback	[PASSED]
test\test_native_domain\test_main.cpp:1912: test_uart_protocol_ack_status_string_compatibility	[PASSED]
test\test_native_domain\test_main.cpp:1913: test_uart_protocol_ack_missing_optional_message	[PASSED]
test\test_native_domain\test_main.cpp:1914: test_uart_protocol_telemetry_ota_active_serialization_round_trip	[PASSED]
test\test_native_domain\test_main.cpp:1915: test_uart_protocol_telemetry_ota_normal_mode_inactive	[PASSED]
test\test_native_domain\test_main.cpp:1916: test_uart_protocol_telemetry_backward_compatibility_legacy_frames	[PASSED]
test\test_native_domain\test_main.cpp:1917: test_uart_protocol_telemetry_ota_fallback_key_support	[PASSED]
test\test_native_domain\test_main.cpp:1918: test_uart_protocol_telemetry_ip_buffer_bounds_check	[PASSED]
------------- native:test_native_domain [PASSED] Took 3.39 seconds -------------

=================================== SUMMARY ===================================
Environment    Test                Status    Duration
-------------  ------------------  --------  ------------
native         test_native_domain  PASSED    00:00:03.391
================= 57 test cases: 57 succeeded in 00:00:03.391 =================
```
*Result: PASS. 57 succeeded, 0 failed.*

#### Check 2: WeMos D1 Uno Build Execution
Command: `pio run -e wemos_d1_uno` (in project root)
```
Processing wemos_d1_uno (platform: espressif8266; board: d1_mini; framework: arduino)
--------------------------------------------------------------------------------
Verbose mode can be enabled via `-v, --verbose` option
CONFIGURATION: https://docs.platformio.org/page/boards/espressif8266/d1_mini.html
PLATFORM: Espressif 8266 (4.2.1) > WeMos D1 R2 and mini
HARDWARE: ESP8266 80MHz, 80KB RAM, 4MB Flash
...
Retrieving maximum program size .pio\build\wemos_d1_uno\firmware.elf
Checking size .pio\build\wemos_d1_uno\firmware.elf
Advanced Memory Usage is available via "PlatformIO Home > Project Inspect"
RAM:   [====      ]  36.6% (used 29956 bytes from 81920 bytes)
Flash: [===       ]  28.0% (used 292183 bytes from 1044464 bytes)
========================= [SUCCESS] Took 9.13 seconds =========================

Environment    Status    Duration
-------------  --------  ------------
wemos_d1_uno   SUCCESS   00:00:09.130
========================= 1 succeeded in 00:00:09.130 =========================
```
*Result: PASS. Exit code 0.*

#### Check 3: ESP32 CYD 2.8 Build Execution
Command: `pio run -e cyd_2_8` (in project root)
```
Processing cyd_2_8 (platform: platformio/espressif32@7.0.1; board: esp32dev; framework: arduino)
--------------------------------------------------------------------------------
Verbose mode can be enabled via `-v, --verbose` option
CONFIGURATION: https://docs.platformio.org/page/boards/espressif32/esp32dev.html
PLATFORM: Espressif 32 (7.0.1) > Espressif ESP32 Dev Module
HARDWARE: ESP32 240MHz, 320KB RAM, 4MB Flash
...
Retrieving maximum program size .pio\build\cyd_2_8\firmware.elf
Checking size .pio\build\cyd_2_8\firmware.elf
Advanced Memory Usage is available via "PlatformIO Home > Project Inspect"
RAM:   [===       ]  27.1% (used 88744 bytes from 327680 bytes)
Flash: [========  ]  83.7% (used 1645345 bytes from 1966080 bytes)
========================= [SUCCESS] Took 49.42 seconds =========================

Environment    Status    Duration
-------------  --------  ------------
cyd_2_8        SUCCESS   00:00:49.421
========================= 1 succeeded in 00:00:49.421 =========================
```
*Result: PASS. Exit code 0.*

---

## 2. Logic Chain

1. **Absence of Hardcoding & Cheating (Obs 1.1.1, 1.1.2)**:
   - In `uart_protocol.cpp`, serialization produces dynamic JSON strings using `ArduinoJson::serializeJson` from values in `AquariumCommand`, `AquariumTelemetry`, and `AquariumAck`. There are no hardcoded string literals matching test outputs.
   - Parsing uses `ArduinoJson::deserializeJson` followed by field extraction and bounded `strncpy`. It handles arbitrary valid JSON inputs, not matching specific fixed buffers.
2. **Authentic Implementation vs. Facades (Obs 1.1.3)**:
   - `UartClient` methods are not empty stubs returning constants. State getters (`isWemosInOta()`, `hasWemosOtaAck()`, `getWemosOtaIp()`, `getWemosOtaElapsedMs()`) read authentic member variables populated by `processLine()` and `sendEnterOta()`.
   - Protections against flash contention (`sendCommand` suppression) and false alarms (`checkAlarmAudio` muting and offline timeout pause) are fully wired and functional.
3. **Genuine Test Verification (Obs 1.1.4, 1.2)**:
   - The 12 unit tests in `test_main.cpp` perform complete end-to-end round-trips: creating command/telemetry structs -> serializing to NDJSON -> feeding back to `parseMessage()` -> asserting equality of reconstructed struct fields.
   - Adversarial test cases verify boundary safety (oversized SSID and IPv6 strings exceeding buffer limits), backward compatibility with legacy frames lacking OTA keys, and alternative ACK field formats (`"status":"ok"`).
4. **Build & Integration Integrity (Obs 1.2)**:
   - All three compilation targets (`native`, `wemos_d1_uno`, `cyd_2_8`) compile cleanly with zero errors and zero warnings.

---

## 3. Caveats

1. **Hardware Transceiver Testing**:
   - Verification was performed through unit tests (`pio test -e native`) and PlatformIO cross-compilation builds (`wemos_d1_uno` and `cyd_2_8`). Live hardware flashing over physical UART and Wi-Fi OTA will be validated during Milestones 2 and 4.
2. **UI Integration**:
   - Visual buttons and touch handlers for OTA on the CYD screen (`gui_app.cpp`) are allocated to Milestone 3; the backend client API surface in `UartClient` is verified ready.

---

## 4. Conclusion

The Milestone 1 work product passes all forensic integrity checks without reservation.
- **No hardcoded test outputs or fake returns**.
- **No facade implementations or stubbed methods**.
- **Genuine parsing, serialization, and boundary protection**.
- **Clean independent verification across test and build targets**.

**Final Verdict:** **CLEAN**

---

## 5. Verification Method

To independently reproduce and verify this audit:

1. **Run Native Unit Tests**:
   ```powershell
   cd c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\firmware\cyd_controller
   pio test -e native
   ```
   *Expectation:* 57 succeeded, 0 failed.

2. **Build WeMos D1 Uno Firmware**:
   ```powershell
   cd c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium
   pio run -e wemos_d1_uno
   ```
   *Expectation:* Exit code 0, SUCCESS.

3. **Build ESP32 CYD Firmware**:
   ```powershell
   cd c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium
   pio run -e cyd_2_8
   ```
   *Expectation:* Exit code 0, SUCCESS.
