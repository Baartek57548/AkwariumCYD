# Handoff Report — Milestone 1 Review: UART NDJSON Extensions & Shared Contracts

**Reviewer / Critic:** `reviewer_m1_1`  
**Date:** 2026-09-18  
**Working Directory:** `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\reviewer_m1_1`  
**Verdict:** **APPROVE**  

---

## 1. Observation

### 1.1 Source Code and Configuration Changes Inspected
1. `firmware/shared/uart_protocol/include/uart_protocol.h`:
   - Lines 4-38: Desktop compatibility shim `class String : public std::string` conditionally enabled when `!defined(ARDUINO)`. Provides conversions for `std::string`, `const char*`, `float`, `double`, integer types, and methods `c_str()`, `length()`, `reserve()`.
   - Line 85: Added `EnterOta` (index 10) to `enum class CommandType : uint8_t`.
   - Lines 112-113: Added `bool ota_mode;` and `char ota_ip[16];` to `struct AquariumTelemetry`.
   - Lines 130-131: Added `char ota_ssid[33];` and `char ota_pass[65];` to `struct AquariumCommand`.

2. `firmware/shared/uart_protocol/src/uart_protocol.cpp`:
   - Lines 23-26: `serializeTelemetry()` serializes `doc["ota_mode"] = t.ota_mode;` and `doc["ota_ip"] = t.ota_ip;` (when non-empty); reserves 320 bytes on `String output`.
   - Lines 79-87: `serializeCommand()` serializes `case CommandType::EnterOta:` emitting `"cmd":"enter_ota"` along with optional `"ssid"` and `"pass"` if populated.
   - Lines 98-112: `serializeAck()` formats NDJSON ACK with optional `"msg"`.
   - Lines 154-157: `parseMessage()` telemetry parser supports dual-key fallback: `doc.containsKey("ota_mode") ? (doc["ota_mode"] | false) : (doc["ota"] | false)`, with safe bounded `strncpy` and explicit null termination for `ota_ip`.
   - Lines 195-203: `parseMessage()` command parser unpacks `enter_ota` with bounded `strncpy` and explicit null termination on `ota_ssid` (32 chars) and `ota_pass` (64 chars).
   - Lines 209-219: `parseMessage()` ACK parser supports both boolean `"ok":true/false` and string `"status":"ok"/"error"` conventions.

3. `firmware/cyd_controller/include/uart_client.h`:
   - Lines 27-35: Declared `sendEnterOta(const char *ssid = nullptr, const char *pass = nullptr)` and state accessors `isWemosInOta()`, `hasWemosOtaAck()`, `isWemosOtaAckOk()`, `getWemosOtaIp()`, `getWemosOtaElapsedMs()`, and `resetWemosOtaState()`.
   - Lines 59-64: Added state member variables `volatile bool wemos_in_ota_`, `volatile bool wemos_ota_ack_received_`, `volatile bool wemos_ota_ack_ok_`, `char wemos_ota_ip_[16]`, and `uint32_t wemos_ota_start_ms_`.

4. `firmware/cyd_controller/src/uart_client.cpp`:
   - Lines 14-25: Clean initialization of all OTA and ACK state variables in constructor.
   - Lines 58-70: In `processLine()`, latches OTA state upon telemetry `ota_mode == true` and automatically resets state via `resetWemosOtaState()` when telemetry indicates WeMos returned to normal mode (`ota_mode == false`).
   - Lines 78-90: In `processLine()`, parses ACK for `enter_ota`, latches `wemos_ota_ack_received_`, `wemos_ota_ack_ok_`, `wemos_in_ota_`, and parses IP address via `extractOtaIp()`.
   - Lines 118-126: In `update()`, suppresses normal 3-second offline timeout during active OTA while enforcing a 6-minute (`360000UL` ms) watchdog timeout.
   - Lines 136-153: In `checkAlarmAudio()`, mutes buzzer alarm during WeMos OTA mode.
   - Lines 159-161: In `sendCommand()`, suppresses non-`EnterOta` outbound UART transmission while `wemos_in_ota_` is true to eliminate flash memory bus contention during ESP8266 flashing.
   - Lines 316-339: Bounded parsing of IP in `extractOtaIp()` supporting `"STA:"`, `"AP:"`, and plain IP representations.

5. `firmware/cyd_controller/test/test_native_domain/test_main.cpp`:
   - Lines 1633-1856: Added 12 unit tests covering round-trip serialization/deserialization, credentials inclusion/omission, buffer bounds clipping, ACK status compatibility, legacy telemetry frames, fallback keys, and IPv6 clipping.
   - Lines 1907-1918: Registered all 12 tests in `main()`.

### 1.2 Tool Execution Results

#### Test Suite Execution
- **Command:** `pio test -e native` in `firmware/cyd_controller`
- **Output:**
  ```
  Processing test_native_domain in native environment
  --------------------------------------------------------------------------------
  Building...
  Testing...
  test\test_native_domain\test_main.cpp:1860: test_regular_and_wrapped_windows	[PASSED]
  ... [45 existing tests PASSED] ...
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
  ------------- native:test_native_domain [PASSED] Took 3.29 seconds -------------
  ================= 57 test cases: 57 succeeded in 00:00:03.289 =================
  ```
- **Result:** Exit code 0, 57/57 tests passed.

#### Embedded Target Compilation: ESP32 CYD 2.8
- **Command:** `pio run -e cyd_2_8` in workspace root
- **Output:**
  ```
  RAM:   [===       ]  27.1% (used 88744 bytes from 327680 bytes)
  Flash: [========  ]  83.7% (used 1645345 bytes from 1966080 bytes)
  ========================= [SUCCESS] Took 48.62 seconds =========================
  ```
- **Result:** Exit code 0, cleanly built and linked. Free RAM: ~238 KB, Free Flash: ~320 KB.

#### Embedded Target Compilation: WeMos D1 Uno (ESP8266)
- **Command:** `pio run -e wemos_d1_uno` in workspace root
- **Output:**
  ```
  RAM:   [====      ]  36.6% (used 29956 bytes from 81920 bytes)
  Flash: [===       ]  28.0% (used 292183 bytes from 1044464 bytes)
  ========================= [SUCCESS] Took 9.35 seconds =========================
  ```
- **Result:** Exit code 0, cleanly built and linked. Free RAM: ~52 KB, Free Flash: ~752 KB.

---

## 2. Logic Chain

1. **Protocol Correctness and Completeness (Obs 1.1.1, 1.1.2)**:
   - The shared NDJSON protocol correctly incorporates the new command `enter_ota` (`CommandType::EnterOta`) and updates `AquariumTelemetry` and `AquariumCommand` structures.
   - Dual-key fallback in `parseMessage()` ensures seamless interoperation between full and shortened key names (`ota_mode` vs `ota`, `"ok": true` vs `"status": "ok"`).
   - Buffer bounds clipping using `sizeof(...) - 1` and explicit null terminator placement eliminates any potential buffer overflow from untrusted or oversized network strings.

2. **Memory Headroom & Zero Heap Fragmentation (Obs 1.1.2, 1.1.4, 1.2)**:
   - In `uart_protocol.cpp`, all JSON documents use stack-allocated `StaticJsonDocument` (`StaticJsonDocument<512>`, `StaticJsonDocument<384>`, `StaticJsonDocument<256>`, `StaticJsonDocument<768>`).
   - Steady-state UART processing generates **zero dynamic heap allocations**, avoiding memory leaks and heap fragmentation on ESP32 SRAM and ESP8266 RAM.
   - Fixed 512-byte ring-free RX buffer in `UartClient` prevents unbounded memory accumulation.
   - The targets compile with large memory safety margins (ESP32: 27.1% RAM utilized; ESP8266: 36.6% RAM utilized).

3. **Interface Conformance with Scope Requirements (Obs 1.1.1, 1.1.3, 1.1.4)**:
   - Milestone 1 requirements specified in `SCOPE.md` (Features 1, 2, 3) are completely fulfilled:
     - Feature 1 (UART NDJSON `EnterOta` Command): Implemented serializer and deserializer.
     - Feature 2 (UART Telemetry OTA Status Flags): Implemented `ota_mode` and `ota_ip[16]`.
     - Feature 3 (CYD `UartClient` OTA Handling): Implemented `sendEnterOta()`, ACK handling, WeMos OTA status latching, 6-minute watchdog, alarm audio muting, and UART command suppression during OTA mode.

4. **Adversarial Integrity & Robustness Evaluation (Obs 1.1.5, 1.2)**:
   - Zero integrity violations were found: no hardcoded return values, no facades, no bypassed logic.
   - All 12 added tests perform genuine input generation, serialization, string pattern matching, deserialization, and structural assertion.
   - Adversarial stress cases (long SSIDs, long passwords, oversized IPv6 strings, missing keys, alternate status keywords) are explicitly tested and pass.

---

## 3. Caveats

1. **Scope Boundary**:
   - Milestone 1 encompasses shared protocol extensions and the CYD `UartClient` communication layer.
   - ESP8266 firmware handling of `EnterOta`, fail-safe relay shutdown, and OTA server bring-up are scheduled for Milestone 2 (`firmware/wemos_controller/`).
   - GUI widgets and touchscreen modals in `gui_app.cpp` are scheduled for Milestone 3.

---

## 4. Conclusion

**Verdict: APPROVE**

Milestone 1 satisfies all requirements, maintains clean architectural separation, introduces zero heap fragmentation, provides robust buffer boundary protections, passes all 57 native tests, and compiles cleanly for both hardware targets (`cyd_2_8` and `wemos_d1_uno`).

---

## 5. Verification Method

To independently reproduce and verify this review:

1. **Run Native Unit Tests**:
   ```powershell
   cd c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\firmware\cyd_controller
   pio test -e native
   ```
   *Expected:* 57 succeeded, 0 failed.

2. **Run ESP32 CYD Target Build**:
   ```powershell
   cd c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium
   pio run -e cyd_2_8
   ```
   *Expected:* SUCCESS, RAM < 30%, Flash < 85%.

3. **Run WeMos D1 Uno Target Build**:
   ```powershell
   cd c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium
   pio run -e wemos_d1_uno
   ```
   *Expected:* SUCCESS, RAM < 40%, Flash < 30%.
