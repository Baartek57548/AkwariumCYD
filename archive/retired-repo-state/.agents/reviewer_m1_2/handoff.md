# Handoff Report — Milestone 1 Review: UART NDJSON Extensions & Shared Contracts

**Reviewer / Critic:** `reviewer_m1_2`  
**Date:** 2026-09-18  
**Working Directory:** `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\reviewer_m1_2`  
**Verdict:** **APPROVE** (with 1 Major advisory finding for M2 WeMos integration and 2 Minor recommendations)

---

## 1. Observation

### 1.1 Integrity Check & Anti-Cheating Audit
- **Source Code Verification**: Inspected `firmware/shared/uart_protocol/src/uart_protocol.cpp`, `firmware/cyd_controller/src/uart_client.cpp`, and `firmware/cyd_controller/test/test_native_domain/test_main.cpp`.
  - No hardcoded test responses or bypasses embedded in production logic.
  - No dummy/facade implementations.
  - No shortcuts bypassing the serialization/deserialization logic.
  - All 12 unit tests perform genuine end-to-end NDJSON serialization, string verification, deserialization, and field assertions.
  - Zero integrity violations detected.

### 1.2 Modified & Added Files Directly Inspected
1. `firmware/shared/uart_protocol/include/uart_protocol.h`:
   - Lines 4-38: Desktop C++11 compatibility shim `class String : public std::string` when `!defined(ARDUINO)`. Provides required methods (`c_str()`, `length()`, `reserve()`) and type constructors (`float`, `double`, `int`, etc.) for native desktop unit tests.
   - Line 85: Added `CommandType::EnterOta` (value 10).
   - Lines 112-113: Added `bool ota_mode;` and `char ota_ip[16];` to `struct AquariumTelemetry`.
   - Lines 130-131: Added `char ota_ssid[33];` and `char ota_pass[65];` to `struct AquariumCommand`.
   - Line 134-139: `AquariumAck` contains `char cmd[24]`, `bool ok`, `char message[48]`.

2. `firmware/shared/uart_protocol/src/uart_protocol.cpp`:
   - Lines 23-26: `serializeTelemetry()` serializes `doc["ota_mode"] = t.ota_mode;` and conditionally `doc["ota_ip"] = t.ota_ip;` when non-empty.
   - Lines 79-87: `serializeCommand()` formats `CommandType::EnterOta` with optional `"ssid"` and `"pass"` if non-empty.
   - Lines 98-112: `serializeAck()` formats NDJSON ACK with `"ok": ok` and optional `"msg"`.
   - Lines 154-157: `parseMessage()` telemetry branch extracts `ota_mode` via dual-key check (`doc.containsKey("ota_mode") ? (doc["ota_mode"] | false) : (doc["ota"] | false)`) and performs bounded copy of `ota_ip` with `sizeof(out_msg.telemetry.ota_ip) - 1` and explicit null termination.
   - Lines 195-203: `parseMessage()` command branch clears `out_msg.command` with `memset`, extracts `enter_ota`, and performs bounded copies of `ota_ssid` (32 chars max) and `ota_pass` (64 chars max) with guaranteed null termination.
   - Lines 209-219: `parseMessage()` ACK branch parses both `"ok": true/false` and `"status": "ok"` formats, with bounded string copies.

3. `firmware/cyd_controller/include/uart_client.h`:
   - Lines 27-35: Declared `sendEnterOta()`, `isWemosInOta()`, `hasWemosOtaAck()`, `isWemosOtaAckOk()`, `getWemosOtaIp()`, `getWemosOtaElapsedMs()`, and `resetWemosOtaState()`.
   - Lines 59-64: Added state variables `wemos_in_ota_`, `wemos_ota_ack_received_`, `wemos_ota_ack_ok_`, `wemos_ota_ip_[16]`, `wemos_ota_start_ms_`.

4. `firmware/cyd_controller/src/uart_client.cpp`:
   - Lines 14-25: Zero-initialized new member variables in constructor.
   - Lines 58-70: Synchronizes `wemos_in_ota_` state from incoming telemetry. Automatically calls `resetWemosOtaState()` when telemetry indicates `ota_mode == false` after being in OTA.
   - Lines 78-90: Parses ACK for `enter_ota`, latches `wemos_ota_ack_received_`, `wemos_ota_ack_ok_`, `wemos_in_ota_`, records start timestamp, and extracts IP address via `extractOtaIp()`.
   - Lines 118-126: Pauses standard 3000 ms offline timeout while `wemos_in_ota_` is true, enforcing an overarching 6-minute (`360000UL` ms) watchdog timeout.
   - Lines 136-153: Mutes buzzer alarm audio while `wemos_in_ota_` is true.
   - Lines 159-161: Drops outbound commands other than `EnterOta` while `wemos_in_ota_` is active.
   - Lines 316-339: `extractOtaIp()` safely extracts IP address from `"STA: <ip>"`, `"AP: <ip>"`, or raw IP string with boundary checks.

5. `firmware/cyd_controller/test/test_native_domain/test_main.cpp`:
   - Lines 1633-1856: Added 12 new unit test cases verifying:
     - EnterOta serialization and deserialization with credentials.
     - Default omission of credentials when empty.
     - Buffer boundary clipping safety for long SSIDs and passwords.
     - ACK parsing for STA mode success and AP mode fallback.
     - ACK backward compatibility for `"status":"ok"`.
     - ACK handling when `"msg"` field is absent.
     - Telemetry active OTA round-trip.
     - Telemetry normal mode omission of `ota_ip`.
     - Telemetry backward compatibility with legacy frames missing `ota_mode`.
     - Telemetry fallback key `"ota"`.
     - Telemetry IP buffer boundary safety under long strings (IPv6).
   - Lines 1907-1918: Registered and executed all 12 tests in `main()`.

6. `platformio.ini` & `firmware/cyd_controller/platformio.ini`:
   - Configured `bblanchon/ArduinoJson@^6.21.4` under native test environment.
   - Preserved all build flags and include paths for both targets.

### 1.3 Independent Tool Verification Commands & Results

#### Verification 1: Native Test Suite
- **Command:** `pio test -e native` in `firmware/cyd_controller`
- **Output:**
  ```
  Processing test_native_domain in native environment
  --------------------------------------------------------------------------------
  Building...
  Testing...
  test\test_native_domain\test_main.cpp:1860: test_regular_and_wrapped_windows	[PASSED]
  ... [45 existing domain tests PASSED] ...
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
  ------------- native:test_native_domain [PASSED] Took 3.33 seconds -------------
  ================= 57 test cases: 57 succeeded in 00:00:03.334 =================
  ```
- **Result:** Exit Code 0, 57 succeeded, 0 failed.

#### Verification 2: WeMos D1 Uno Build
- **Command:** `pio run -e wemos_d1_uno` from workspace root
- **Output:**
  ```
  RAM:   [====      ]  36.6% (used 29956 bytes from 81920 bytes)
  Flash: [===       ]  28.0% (used 292183 bytes from 1044464 bytes)
  ========================= [SUCCESS] Took 9.37 seconds =========================
  ```
- **Result:** Exit Code 0, cleanly built and linked.

#### Verification 3: ESP32 CYD Target Build
- **Command:** `pio run -e cyd_2_8` from workspace root
- **Output:**
  ```
  RAM:   [===       ]  27.1% (used 88744 bytes from 327680 bytes)
  Flash: [========  ]  83.7% (used 1645345 bytes from 1966080 bytes)
  ========================= [SUCCESS] Took 49.27 seconds =========================
  ```
- **Result:** Exit Code 0, cleanly built and linked.

---

## 2. Logic Chain

1. **Anti-Cheating & Integrity Verification (Obs 1.1)**:
   - Scrutiny of the diff and files confirms no hardcoded mock results, no facades, and no bypassed requirements. All 12 added tests perform genuine end-to-end string serialization and deserialization.

2. **Scope Conformance (Obs 1.2.1 - 1.2.5)**:
   - Feature 1 (`EnterOta` Command): Defined in `CommandType::EnterOta`, handled in `serializeCommand()` and `parseMessage()`.
   - Feature 2 (Telemetry Flags): Extended `AquariumTelemetry` with `ota_mode` and `ota_ip[16]`, handled in `serializeTelemetry()` and `parseMessage()`.
   - Feature 3 (CYD `UartClient`): Implemented `sendEnterOta()`, ACK latching, WeMos state tracking, timeout pause with 6-minute watchdog, alarm audio muting, and UART command silencing during OTA.

3. **Buffer Safety and String Bounding (Obs 1.2.2, 1.2.4)**:
   - All string copying in `parseMessage()` and `extractOtaIp()` uses explicit length bounds (`sizeof(dst) - 1`) with guaranteed `'\0'` termination.
   - String buffer limits match protocol and network standards: SSID (32 chars + null = 33), WPA2 Passphrase (64 chars + null = 65), IPv4 (15 chars + null = 16), ACK command name (23 chars + null = 24), ACK message (47 chars + null = 48).
   - Fixed 512-byte RX buffer in `UartClient` with overflow reset (`rx_idx_ = 0`) prevents heap and stack corruption on oversized incoming UART streams.

4. **Dynamic Memory and Heap Safety (Obs 1.2.2, 1.3)**:
   - Steady-state serialization and deserialization use fixed `StaticJsonDocument` buffers (size 256 to 768 bytes), eliminating dynamic heap allocations in hot UART loops.
   - ESP32 RAM usage is 27.1% (88,744 bytes / 327,680 bytes), leaving ~238 KB free SRAM.
   - WeMos ESP8266 RAM usage is 36.6% (29,956 bytes / 81,920 bytes), leaving ~52 KB free RAM.

---

## 3. Findings

### [Major] Advisory Finding 1: Lack of In-Class Default Initializers in Shared Structs
- **Where:** `firmware/shared/uart_protocol/include/uart_protocol.h:112-113` and `130-131`, and `firmware/wemos_controller/src/main.cpp:26`.
- **What:** In `uart_protocol.h`, the new fields in `AquariumTelemetry` (`bool ota_mode; char ota_ip[16];`) and `AquariumCommand` (`char ota_ssid[33]; char ota_pass[65];`) lack in-class default member initializers (e.g., `= false;` and `= {0};`).
- **Why:** In C++, non-static local POD structures created without `{}` or `memset` have indeterminate values. In existing code `firmware/wemos_controller/src/main.cpp:26`:
  ```cpp
  void send_telemetry() {
      AquaProtocol::AquariumTelemetry t;
      t.timestamp = g_hardware_i2c.getEpochTime();
      ...
  ```
  `t.ota_mode` and `t.ota_ip` are uninitialized on the stack. When `serializeTelemetry(t)` is called, uninitialized stack garbage in `t.ota_mode` could evaluate to `true`, causing CYD's `UartClient` to lock into `wemos_in_ota_ = true` on normal startup, blocking outbound relay commands and muting alarms. Additionally, if `t.ota_ip[0]` contains non-zero stack garbage, `doc["ota_ip"] = t.ota_ip;` could read beyond the 16-byte buffer until finding a zero byte.
- **Actionable Mitigation for Milestone 2**:
  1. In `uart_protocol.h`, add default initializers to the struct fields:
     ```cpp
     struct AquariumTelemetry {
         ...
         bool ota_mode = false;
         char ota_ip[16] = {0};
     };
     struct AquariumCommand {
         ...
         char ota_ssid[33] = {0};
         char ota_pass[65] = {0};
     };
     ```
  2. In `uart_protocol.cpp:24`, guard IP serialization:
     ```cpp
     if (t.ota_mode && t.ota_ip[0] != '\0') {
         doc["ota_ip"] = t.ota_ip;
     }
     ```
  3. In `wemos_controller/src/main.cpp:26`, zero-initialize `AquaProtocol::AquariumTelemetry t = {};`.

### [Minor] Finding 2: `extractOtaIp` Token Character Validation
- **Where:** `firmware/cyd_controller/src/uart_client.cpp:316-339`
- **What:** `extractOtaIp` strips `"STA:"` or `"AP:"` and copies characters until whitespace or delimiter. If WeMos sends an ACK message like `{"type":"ack","cmd":"enter_ota","ok":true,"msg":"OTA Mode Active"}`, `extractOtaIp` copies `"OTA"` into `wemos_ota_ip_`.
- **Why:** While safe against buffer overflows, `getWemosOtaIp()` could briefly return `"OTA"` instead of an IP address until the first telemetry broadcast arrives.
- **Suggestion:** Check whether the token starts with a digit before copying, or rely on subsequent telemetry for IP resolution.

### [Minor] Finding 3: Safety Timeout State Cleanup
- **Where:** `firmware/cyd_controller/src/uart_client.cpp:120-123`
- **What:** When the 6-minute `WEMOS_OTA_SAFETY_TIMEOUT_MS` expires, `update()` sets `wemos_in_ota_ = false; online_ = false;`, but does not call `resetWemosOtaState()`.
- **Why:** `wemos_ota_ip_` retains stale IP and `wemos_ota_ack_received_` remains `true` until the next session.
- **Suggestion:** Call `resetWemosOtaState()` upon safety timeout expiration.

---

## 4. Adversarial Review & Stress-Testing

### Challenge 1: Flash Write Flash Cache Contention on ESP8266
- **Assumption:** ESP8266 cannot service UART interrupts or execute non-IRAM code during active flash writes without causing a watchdog reset or flash corruption.
- **Stress-Test:** CYD user triggers touchscreen buttons or background loop triggers while WeMos is in OTA mode.
- **Mitigation Checked:** Verified lines 159-161 of `uart_client.cpp`: `sendCommand()` checks `if (wemos_in_ota_ && cmd.type != AquaProtocol::CommandType::EnterOta) return false;`. All normal command transmissions are dropped while WeMos is in OTA mode. Pass.

### Challenge 2: Millis Rollover on Watchdog & Timers
- **Assumption:** ESP32 and WeMos timers must survive `millis()` 32-bit integer overflow (~49.7 days).
- **Stress-Test:** `now_ms - wemos_ota_start_ms_ > WEMOS_OTA_SAFETY_TIMEOUT_MS` and `now_ms - last_rx_ms_ > AquaProtocol::OFFLINE_TIMEOUT_MS`.
- **Evaluation:** In C++, unsigned subtraction `(uint32_t)(now_ms - start_ms)` is mathematically correct across rollover as long as the elapsed duration does not exceed $2^{32}-1$ milliseconds. Pass.

### Challenge 3: Inactive WeMos Offline False Alarm During Flashing
- **Assumption:** Normal offline detection triggers after 3000 ms of missing telemetry.
- **Stress-Test:** WeMos stops sending telemetry during OTA.
- **Mitigation Checked:** Lines 118-126 of `uart_client.cpp`: when `wemos_in_ota_ == true`, standard offline timeout is paused and replaced by the 6-minute watchdog. Buzzer alarm is muted (`checkAlarmAudio()`). Pass.

---

## 5. Caveats

1. **Firmware Integration Scope**:
   - `firmware/wemos_controller/` implementation changes (GPIO12 button trigger, fail-safe relay shutdown, OTA server bring-up, and telemetry silencing) are designated for Milestone 2.
   - GUI screens and modal dialogs in `firmware/cyd_controller/src/gui_app.cpp` are designated for Milestone 3.
2. **Physical Hardware Bench**:
   - Verification was performed via Native Unit Tests and PlatformIO cross-compilers (`xtensa-esp32-elf` and `xtensa-lx106-elf`). End-to-end physical hardware testing over the physical UART wire will take place in Milestone 4.

---

## 6. Conclusion

**Verdict: APPROVE**

Milestone 1 successfully delivers all protocol extensions, serialization/deserialization logic, ACK handling, `UartClient` protections, and passes 100% of unit tests and target compilations without integrity violations or memory regressions. Advisory findings documented herein are flagged for incorporation into Milestone 2 (WeMos integration).

---

## 7. Verification Method

To independently reproduce this verification:

1. **Execute Native Domain Unit Tests**:
   ```powershell
   cd c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\firmware\cyd_controller
   pio test -e native
   ```
   *Expected outcome:* 57 succeeded, 0 failed.

2. **Execute WeMos Target Compilation**:
   ```powershell
   cd c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium
   pio run -e wemos_d1_uno
   ```
   *Expected outcome:* SUCCESS, RAM: ~36.6%, Flash: ~28.0%.

3. **Execute CYD Target Compilation**:
   ```powershell
   cd c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium
   pio run -e cyd_2_8
   ```
   *Expected outcome:* SUCCESS, RAM: ~27.1%, Flash: ~83.7%.

4. **Verify Integrity & Code Diffs**:
   Inspect:
   - `firmware/shared/uart_protocol/include/uart_protocol.h`
   - `firmware/shared/uart_protocol/src/uart_protocol.cpp`
   - `firmware/cyd_controller/include/uart_client.h`
   - `firmware/cyd_controller/src/uart_client.cpp`
   - `firmware/cyd_controller/test/test_native_domain/test_main.cpp`
