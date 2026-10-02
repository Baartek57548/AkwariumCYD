# Handoff Report — Milestone 1: UART Protocol NDJSON Extensions & Shared Contracts

**Worker:** `worker_m1`  
**Date:** 2026-09-18  
**Working Directory:** `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\worker_m1`  
**Status:** Complete  

---

## 1. Observation

### 1.1 Files Modified
1. `firmware/shared/uart_protocol/include/uart_protocol.h`
   - Added `#if defined(ARDUINO) ... #else ... #endif` defining a lightweight C++11 `String` shim inheriting from `std::string` for native desktop compilation.
   - Extended `enum class CommandType : uint8_t` with `EnterOta` (explicit integer value 10).
   - Extended `struct AquariumTelemetry` with `bool ota_mode;` and `char ota_ip[16];`.
   - Extended `struct AquariumCommand` with `char ota_ssid[33];` and `char ota_pass[65];`.

2. `firmware/shared/uart_protocol/src/uart_protocol.cpp`
   - `serializeCommand()`: added `case CommandType::EnterOta:` serializing `"cmd":"enter_ota"`, optional `"ssid"`, and optional `"pass"`; increased `output.reserve(256)`.
   - `serializeTelemetry()`: added `doc["ota_mode"] = t.ota_mode;` and conditional `doc["ota_ip"] = t.ota_ip;`; increased `output.reserve(320)`.
   - `parseMessage()`:
     - Telemetry: parsed `ota_mode` with dual-key fallback (`doc.containsKey("ota_mode") ? (doc["ota_mode"] | false) : (doc["ota"] | false)`) and bounded `ota_ip` with null termination.
     - Command: cleared `out_msg.command` with `memset`, added branch for `"enter_ota"` parsing `ota_ssid` (bounded to 32 chars + null) and `ota_pass` (bounded to 64 chars + null).
     - ACK: robust parsing accepting either `"ok":true` or `"status":"ok"` (`doc.containsKey("ok") ? (doc["ok"] | false) : (strcmp(doc["status"] | "", "ok") == 0)`).

3. `firmware/cyd_controller/include/uart_client.h`
   - Added method declarations: `sendEnterOta(const char *ssid = nullptr, const char *pass = nullptr)`.
   - Added WeMos OTA state tracking: `isWemosInOta()`, `hasWemosOtaAck()`, `isWemosOtaAckOk()`, `getWemosOtaIp()`, `getWemosOtaElapsedMs()`, `resetWemosOtaState()`.
   - Added ACK inspection: `getLatestAck()`, `hasNewAck()`, `clearNewAck()`.
   - Added member variables for OTA latching: `wemos_in_ota_`, `wemos_ota_ack_received_`, `wemos_ota_ack_ok_`, `wemos_ota_ip_[16]`, `wemos_ota_start_ms_`, `latest_ack_`, `last_ack_ms_`, `has_new_ack_`.

4. `firmware/cyd_controller/src/uart_client.cpp`
   - Initialized all new tracking variables and buffers in constructor.
   - `processLine()`: parsed `MessageType::Ack` into `latest_ack_`, detected `cmd == "enter_ota"`, latched `wemos_in_ota_ = true`, recorded start time, and extracted IP address.
   - `processLine()`: synchronized WeMos state from telemetry: if `msg.telemetry.ota_mode == true`, latched OTA state; if `msg.telemetry.ota_mode == false` and `wemos_in_ota_` was active, automatically called `resetWemosOtaState()`.
   - `update()`: paused the 3-second offline timeout (`AquaProtocol::OFFLINE_TIMEOUT_MS`) during active `wemos_in_ota_`, enforcing a 6-minute (360s) safety watchdog.
   - `checkAlarmAudio()`: muted the buzzer beeping during `wemos_in_ota_`.
   - `sendCommand()`: suppressed non-OTA command transmission while `wemos_in_ota_` is true to avoid flash memory bus contention on ESP8266.
   - Implemented helper `extractOtaIp()` to parse both `"STA:192.168.1.150"`, `"AP:192.168.4.1"`, or raw IP formats.

5. `firmware/cyd_controller/test/test_native_domain/test_main.cpp`
   - Included `"uart_protocol.h"`.
   - Added 12 new behavior-based test cases covering `EnterOta` round-trip serialization/deserialization, default credential omission, buffer boundary clipping safety, ACK parsing (`STA`, `AP`, `status:"ok"`, missing `"msg"`), telemetry OTA serialization/deserialization, legacy frame backward compatibility, fallback key `"ota"` parsing, and IP address buffer safety.
   - Registered all 12 test cases in `main()`.

6. `firmware/cyd_controller/platformio.ini` & `platformio.ini`
   - Added `bblanchon/ArduinoJson@^6.21.4` under `[env:native]` per explicit authorization from orchestrator.
   - Added `firmware/cyd_controller/lib` to `lib_extra_dirs` under `[env:native]` in root `platformio.ini`.

### 1.2 Tool Verification Commands & Verbatim Outputs

#### Verification 1: Native Test Suite
Command: `pio test -e native` (run in `firmware/cyd_controller`)
```
Verbosity level can be increased via `-v, -vv, or -vvv` option
Collected 1 tests

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
------------- native:test_native_domain [PASSED] Took 4.46 seconds -------------

=================================== SUMMARY ===================================
Environment    Test                Status    Duration
-------------  ------------------  --------  ------------
native         test_native_domain  PASSED    00:00:04.460
================= 57 test cases: 57 succeeded in 00:00:04.460 =================
```

#### Verification 2: WeMos D1 Uno Build
Command: `pio run -e wemos_d1_uno` (run from workspace root)
```
Processing wemos_d1_uno (platform: espressif8266; board: d1_mini; framework: arduino)
--------------------------------------------------------------------------------
...
Building in release mode
Linking .pio\build\wemos_d1_uno\firmware.elf
Checking size .pio\build\wemos_d1_uno\firmware.elf
RAM:   [====      ]  36.6% (used 29956 bytes from 81920 bytes)
Flash: [===       ]  28.0% (used 292183 bytes from 1044464 bytes)
Building .pio\build\wemos_d1_uno\firmware.bin
========================= [SUCCESS] Took 22.82 seconds =========================
```

#### Verification 3: ESP32 CYD 2.8 Build
Command: `pio run -e cyd_2_8` (run from workspace root)
```
Processing cyd_2_8 (platform: platformio/espressif32@7.0.1; board: esp32dev; framework: arduino)
--------------------------------------------------------------------------------
...
Building in release mode
Linking .pio\build\cyd_2_8\firmware.elf
Checking size .pio\build\cyd_2_8\firmware.elf
RAM:   [===       ]  27.1% (used 88744 bytes from 327680 bytes)
Flash: [========  ]  83.7% (used 1645345 bytes from 1966080 bytes)
Building .pio\build\cyd_2_8\firmware.bin
Merged 27 ELF sections
Successfully created esp32 image.
======================== [SUCCESS] Took 190.42 seconds ========================
```

---

## 2. Logic Chain

1. **Protocol Extension (Obs 1.1.1, 1.1.2)**:
   - To trigger OTA on WeMos from CYD via UART without hardware manipulation, a dedicated NDJSON command `enter_ota` (`CommandType::EnterOta`) is required.
   - `AquariumCommand` now contains `ota_ssid[33]` and `ota_pass[65]`. If credentials are provided, WeMos can connect directly to home Wi-Fi; if omitted, it falls back to AP `AquaBox-OTA`.
   - `AquariumTelemetry` now includes `ota_mode` and `ota_ip[16]`. In normal operation, `ota_mode == false` and `ota_ip` is omitted from output frames. During OTA service mode, WeMos advertises `ota_mode: true` and its assigned IP.
   - In `parseMessage()`, dual-key compatibility ensures interoperability between `ota_mode` and `ota`, and ACK statuses `ok: true` and `status: "ok"`.

2. **UartClient State Management & Protections (Obs 1.1.3, 1.1.4)**:
   - When WeMos enters OTA mode, it suppresses periodic UART telemetry to protect the ESP8266 flash cache from corruption during flash writes.
   - Previously, after 3000 ms without telemetry, CYD would mark WeMos offline (`online_ = false`) and could sound the buzzer alarm.
   - In the updated `UartClient::update()`, when `wemos_in_ota_` is active, the 3-second offline timeout is paused. A master 6-minute (360s) watchdog safety timeout ensures that if WeMos hangs or fails to complete flashing, the controller safely resets OTA state and restores offline detection.
   - `checkAlarmAudio()` mutes the buzzer whenever `wemos_in_ota_ == true`, preventing false audible alarms while user is flashing.
   - `sendCommand()` drops any normal command TX while `wemos_in_ota_ == true` (allowing only re-entry `EnterOta`), ensuring silent UART lines during OTA writes.

3. **Native Testing & PlatformIO Compatibility (Obs 1.1.1, 1.1.5, 1.1.6, 1.2)**:
   - `uart_protocol.h` conditionally defines a desktop-compatible `String` class inheriting from `std::string` when `ARDUINO` is not defined.
   - With `bblanchon/ArduinoJson@^6.21.4` configured in `[env:native]`, the Unity test suite compiles cleanly under desktop MinGW GCC.
   - All 45 pre-existing domain unit tests and all 12 newly added protocol tests pass with zero failures.
   - Both embedded targets (`wemos_d1_uno` and `cyd_2_8`) compile and link without errors or regressions.

---

## 3. Caveats

1. **Milestone Scope Boundary**:
   - `firmware/wemos_controller/` was strictly untouched in this milestone. Actual handling of `EnterOta`, fail-safe relay shutdown, and OTA server initialization on ESP8266 will be implemented in Milestone 2.
   - UI integration (touchscreen buttons and modal dialogs in `gui_app.cpp`) is planned for Milestone 3. The `UartClient` methods provided here (`sendEnterOta`, `isWemosInOta`, `getWemosOtaIp`, `hasWemosOtaAck`, `isWemosOtaAckOk`) form the complete integration surface for Milestone 3.

---

## 4. Conclusion

Milestone 1 is complete:
- Shared UART NDJSON protocol is fully extended with `CommandType::EnterOta`, `AquariumCommand` credential fields, `AquariumTelemetry` OTA fields, and dual-format ACK parsing.
- `UartClient` on ESP32 CYD correctly parses ACKs, sends `EnterOta`, tracks WeMos OTA state and IP, pauses the offline timeout with a 6-minute watchdog, mutes alarms, and suppresses UART commands during OTA.
- 12 comprehensive unit tests verify serialization, deserialization, bounds safety, and backward compatibility.
- All three PlatformIO environments (`native`, `wemos_d1_uno`, `cyd_2_8`) compile and pass verification with 100% success.

---

## 5. Verification Method

To independently verify this milestone:

1. **Run Native Unit Tests**:
   ```powershell
   cd c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\firmware\cyd_controller
   pio test -e native
   ```
   *Expected result:* 57 test cases succeeded, 0 failures.

2. **Run WeMos D1 Uno Build**:
   ```powershell
   cd c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium
   pio run -e wemos_d1_uno
   ```
   *Expected result:* SUCCESS (exit code 0).

3. **Run ESP32 CYD Build**:
   ```powershell
   cd c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium
   pio run -e cyd_2_8
   ```
   *Expected result:* SUCCESS (exit code 0).

4. **Inspect Code Changes**:
   - `firmware/shared/uart_protocol/include/uart_protocol.h`
   - `firmware/shared/uart_protocol/src/uart_protocol.cpp`
   - `firmware/cyd_controller/include/uart_client.h`
   - `firmware/cyd_controller/src/uart_client.cpp`
   - `firmware/cyd_controller/test/test_native_domain/test_main.cpp`
