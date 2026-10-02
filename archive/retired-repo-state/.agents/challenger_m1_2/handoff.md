# Handoff Report — challenger_m1_2

**Verdict**: **FAIL**  
**Component Under Test**: `UartClient` OTA State Machine & Protections (`firmware/cyd_controller/include/uart_client.h`, `firmware/cyd_controller/src/uart_client.cpp`)  
**Target Milestone**: M1 (UART NDJSON Extensions & CYD State Machine)  
**Date**: 2026-09-18T18:05:00Z  

---

## 1. Observation

Direct examination of `firmware/cyd_controller/src/uart_client.cpp` and empirical execution of 17 test cases via `test/adversarial/uart_client_stress_test.cpp` yielded the following findings:

### 1.1 Watchdog State Lingering Flaw (Lines 118–123 & Lines 67–70)
In `firmware/cyd_controller/src/uart_client.cpp`:
```cpp
118:     if (wemos_in_ota_) {
119:         constexpr uint32_t WEMOS_OTA_SAFETY_TIMEOUT_MS = 360000UL; // 6 minut
120:         if (now_ms - wemos_ota_start_ms_ > WEMOS_OTA_SAFETY_TIMEOUT_MS) {
121:             wemos_in_ota_ = false;
122:             online_ = false;
123:         }
124:     } else if (online_ && (now_ms - last_rx_ms_ > AquaProtocol::OFFLINE_TIMEOUT_MS)) {
125:         online_ = false;
126:     }
```
And in `processLine()`:
```cpp
58:         if (msg.telemetry.ota_mode) {
59:             wemos_in_ota_ = true;
...
67:         } else if (wemos_in_ota_) {
68:             // WeMos powrócił z trybu OTA do normalnej pracy po resecie
69:             resetWemosOtaState();
70:         }
```
When the 6-minute watchdog triggers (lines 120–123), it executes only:
```cpp
wemos_in_ota_ = false;
online_ = false;
```
It does **NOT** invoke `resetWemosOtaState()`. As a consequence:
- `wemos_ota_ack_received_` remains `true`
- `wemos_ota_ack_ok_` remains `true`
- `wemos_ota_ip_` retains the stale IP address (e.g. `"192.168.4.1"`)
- `wemos_ota_start_ms_` retains the previous timestamp

Furthermore, when WeMos subsequently reboots into normal mode and sends regular telemetry (`ota_mode == false`):
- `if (msg.telemetry.ota_mode)` evaluates to `false`
- `else if (wemos_in_ota_)` evaluates to `false` (because `wemos_in_ota_` was already reset to `false` by the watchdog at line 121)
- `resetWemosOtaState()` is **never called**!
- Therefore, `hasWemosOtaAck()` remains `true`, `isWemosOtaAckOk()` remains `true`, and `getWemosOtaIp()` returns `"192.168.4.1"` permanently, even while the system is operating in normal autonomous mode.

### 1.2 Non-IP ACK Status Parsing into Bogus IP (Lines 316–339 & Line 87)
In `firmware/cyd_controller/src/uart_client.cpp`:
```cpp
316: void UartClient::extractOtaIp(const char *src, char *dst, size_t dst_size)
317: {
318:     if (src == nullptr || dst == nullptr || dst_size == 0) {
319:         return;
320:     }
321:     dst[0] = '\0';
322: 
323:     const char *p = src;
324:     if (strncmp(p, "STA:", 4) == 0) {
325:         p += 4;
326:     } else if (strncmp(p, "AP:", 3) == 0) {
327:         p += 3;
328:     }
329: 
330:     while (*p == ' ') {
331:         p++;
332:     }
333: 
334:     size_t i = 0;
335:     while (*p != '\0' && *p != ' ' && *p != '\r' && *p != '\n' && i < dst_size - 1) {
336:         dst[i++] = *p++;
337:     }
338:     dst[i] = '\0';
339: }
```
When an ACK frame arrives with `ok == true` and a text status message like `{"type":"ack","cmd":"enter_ota","ok":true,"msg":"OTA Ready"}` (as tested in `test_native_domain/test_main.cpp:1724`), `extractOtaIp` copies the first token `"OTA"` into `wemos_ota_ip_`.
`getWemosOtaIp()` returns `"OTA"`, which is an invalid IPv4 address and will corrupt CYD GUI display widgets.

### 1.3 Validated Robust Features
The following features were empirically verified and behave according to specification:
1. **Command TX Suppression**: Normal commands (`sendSetRelay`, `sendSetTemp`, `sendSetSchedule`, `sendFeedTrigger`, `sendSyncLights`, `sendSyncTime`, `sendClearAlarm`, `sendSafeMode`) return `false` and transmit 0 bytes while in OTA mode.
2. **EnterOta Command Exception**: `sendEnterOta()` is explicitly allowed during OTA mode (`cmd.type != CommandType::EnterOta`), permitting retries or re-provisioning.
3. **Standard 3s Timeout Pausing**: While `wemos_in_ota_ == true`, the 3s timeout is completely paused; controller remains online across 4s, 60s, 300s, and 359s of UART silence.
4. **Buzzer Muting**: When OTA mode begins, any active alarm beep on `SPEAKER_PIN` (GPIO26) is immediately cut to `LOW`. The buzzer remains 100% silent throughout OTA mode even with active alarm masks.
5. **Buzzer Unmuting on Exit**: Once WeMos resumes normal telemetry with `ota_mode == false`, intermittent alarm beeping resumes as expected.
6. **32-bit Millis Wrap-Around**: Unsigned 32-bit subtraction correctly handles timer rollover across `0xFFFFFFFF` without premature watchdog expiration.
7. **Buffer Overflow & Corrupted Streams**: Handling of oversized lines (>512 bytes) and corrupted JSON fragments does not crash or corrupt the client state.

---

## 2. Logic Chain

1. **Premise 1**: The specification requires that the 6-minute watchdog resets state if WeMos never reboots, and that WeMos returning to normal telemetry cleanly clears all OTA flags.
2. **Premise 2**: In `uart_client.cpp`, line 121 sets `wemos_in_ota_ = false` upon watchdog expiry but leaves `wemos_ota_ack_received_ = true`, `wemos_ota_ack_ok_ = true`, and `wemos_ota_ip_ = <stale_ip>`.
3. **Premise 3**: In `uart_client.cpp`, line 67 guards `resetWemosOtaState()` with `else if (wemos_in_ota_)`. If `wemos_in_ota_` was already reset to `false` by the watchdog, line 67 evaluates to `false` when normal telemetry resumes.
4. **Premise 4**: Empirical test `test_watchdog_state_lingering_vulnerability` was executed.
   - Result: `client.hasWemosOtaAck()` remained `true` and `client.getWemosOtaIp()` returned `"192.168.4.1"` after watchdog timeout and after resumption of normal telemetry frames.
5. **Premise 5**: Empirical test `test_non_ip_ack_message` was executed with `"OTA Ready"`.
   - Result: `client.getWemosOtaIp()` returned `"OTA"`, confirming unvalidated token copying.
6. **Conclusion**: The state machine has two logic flaws violating clean recovery and valid IP reporting requirements. Verdict: **FAIL**.

---

## 3. Caveats

- Tests were run using PlatformIO's native MinGW toolchain with a hardware mock shim (`test/adversarial/mocks/Arduino.h`), accurately reproducing the C++ execution semantics of ESP32 Arduino framework.
- Physical UART line noise and electrical bouncing were not simulated on real silicon, but stream robustness was verified against malformed/oversized packets.

---

## 4. Adversarial Review Challenge Report

### Overall Risk Assessment: **HIGH**

### Challenges

#### Challenge 1 [High]: Watchdog Omits State Reset, Causing Permanent Flag Lingering
- **Assumption challenged**: "The 6-minute watchdog timeout safely resets the state."
- **Attack scenario**: WeMos enters OTA mode. Flash upload fails, hangs, or WeMos crashes. After 6 minutes, the watchdog on CYD trips. CYD marks the controller offline and sets `wemos_in_ota_ = false`. Later, WeMos watchdog reboots WeMos back into normal autonomous mode. Normal telemetry resumes. Because `wemos_in_ota_` was already false, `resetWemosOtaState()` is bypassed.
- **Blast radius**: `hasWemosOtaAck()` and `isWemosOtaAckOk()` return `true` permanently. `getWemosOtaIp()` continues returning the stale IP. Any CYD UI panel inspecting these flags will permanently display OTA connection guidance while the system is actually running normal control loops.
- **Mitigation**:
  In `firmware/cyd_controller/src/uart_client.cpp`:
  At line 121:
  ```cpp
  if (now_ms - wemos_ota_start_ms_ > WEMOS_OTA_SAFETY_TIMEOUT_MS) {
      resetWemosOtaState();
      online_ = false;
  }
  ```
  And at line 67 in `processLine()`:
  ```cpp
  } else if (wemos_in_ota_ || wemos_ota_ack_received_ || wemos_ota_ip_[0] != '\0') {
      resetWemosOtaState();
  }
  ```

#### Challenge 2 [Medium]: Non-IP Status Message in ACK Injected into `wemos_ota_ip_`
- **Assumption challenged**: "ACK message payload always contains an IP address."
- **Attack scenario**: WeMos responds with `{"type":"ack","cmd":"enter_ota","ok":true,"msg":"OTA Ready"}` or `{"type":"ack","cmd":"enter_ota","ok":true,"msg":"OK"}`. `extractOtaIp` extracts `"OTA"` or `"OK"`.
- **Blast radius**: CYD screen displays `"Adres WeMos: OTA"` or `"Adres WeMos: OK"`, leading to user confusion and invalid connection attempts.
- **Mitigation**:
  Validate that the extracted string contains IPv4 formatting (e.g. contains at least one dot `.` or matches dotted-decimal syntax) before populating `wemos_ota_ip_`. If invalid, keep `wemos_ota_ip_[0] = '\0'`.

---

## 5. Stress Test Results Summary

| # | Scenario | Expected Behavior | Actual Behavior | Result |
|---|---|---|---|:---:|
| 1 | Normal command TX in non-OTA mode | All 9 command types transmit | Transmitted to Serial TX | **PASS** |
| 2 | Normal command TX during OTA mode | All 8 non-OTA commands return false | Suppressed, 0 bytes on TX | **PASS** |
| 3 | EnterOta command TX during OTA mode | Allowed through (retry capability) | Transmitted to Serial TX | **PASS** |
| 4 | Offline timeout in normal mode | Online at 2999ms, offline at 3001ms | Exactly 3000ms boundary | **PASS** |
| 5 | Offline timeout during OTA mode | Paused; remains online > 3s | Stays online at 4s, 60s, 300s, 359s | **PASS** |
| 6 | Watchdog expiration at 6 minutes | Trips at 360,001 ms | In OTA at 360s; trips at 360001ms | **PASS** |
| 7 | Watchdog cleanup of ACK and IP | All OTA state wiped | `hasWemosOtaAck()` & IP linger | **FAIL** |
| 8 | Unacknowledged EnterOta timeout | Falls back to 3s offline timeout | Offline at 3001ms | **PASS** |
| 9 | 32-bit millis wrap-around | Survives rollover across 0xFFFFFFFF | Stays online, trips at 360001ms | **PASS** |
| 10 | Buzzer muting upon entering OTA | Active beep immediately silenced | Speaker pin cut to LOW | **PASS** |
| 11 | Buzzer unmuting on normal exit | Resumes intermittent beeping | Speaker pin beeps at 1s period | **PASS** |
| 12 | ACK IP parsing (STA/AP/raw) | Extracts valid IP strings | Extracted all 3 formats | **PASS** |
| 13 | Non-IP ACK status string | Rejects non-IP text | Parsed "OTA Ready" into IP "OTA" | **FAIL** |
| 14 | Telemetry IP update & guard | Updates IP; ignores empty string | Preserved valid IP | **PASS** |
| 15 | Non-OTA ACK during OTA | Does not disturb OTA state | Preserved OTA state & IP | **PASS** |
| 16 | Rapid stream & 600-byte line | Resets on overflow without crash | Handled safely | **PASS** |
| 17 | Clean recovery when WeMos exits OTA | Complete reset of flags and timer | Cleared, commands unblocked | **PASS** |

**Total**: 17 tests (15 PASSED, 2 FAILED).

---

## 6. Verification Method

To independently reproduce these findings and execute the empirical test suite:

1. Open PowerShell in `firmware/cyd_controller`:
   ```powershell
   $bin = "$env:USERPROFILE\.platformio\packages\toolchain-gccmingw32\bin"
   $env:PATH = "$bin;$env:PATH"
   & "$bin\g++.exe" -std=gnu++11 -I test/adversarial/mocks -I include -I ../shared/uart_protocol/include -I .pio/libdeps/native/ArduinoJson/src test/adversarial/uart_client_stress_test.cpp src/uart_client.cpp ../shared/uart_protocol/src/uart_protocol.cpp -o test_uart_client_stress.exe
   .\test_uart_client_stress.exe
   ```
2. Verify test output displays:
   - `[FAIL] Adversarial: Watchdog leaves stale ACK and IP flags lingering`
   - `[FAIL] Adversarial: Non-IP ACK message extracted as bogus IP`
3. Invalidation condition: Fix `update()` to call `resetWemosOtaState()` on watchdog timeout, update `processLine()` to clean lingering flags on normal telemetry, and validate IPv4 format in `extractOtaIp()`. All 17 tests will then PASS.
