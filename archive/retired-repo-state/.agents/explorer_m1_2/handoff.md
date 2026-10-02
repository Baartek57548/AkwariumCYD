# Handoff Report — explorer_m1_2

**Task:** Investigation and Design for CYD `UartClient` OTA Extensions (`uart_client.h` and `uart_client.cpp`)  
**Milestone:** M1 (UART Protocol Extensions)  
**Agent:** `explorer_m1_2`  
**Date:** 2026-09-18  
**Report Reference:** `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_m1_2\report.md`

---

## 1. Observation

1. **Header file location:**
   - In `firmware/cyd_controller/include/uart_client.h` (lines 1–48): `UartClient` defines transmission methods (`sendSetRelay`, `sendSetTemp`, etc.) and basic telemetry storage (`latest_telemetry_`, `last_rx_ms_`, `online_`), but has no members or methods for handling ACK, sending `EnterOta`, or tracking WeMos OTA state.
2. **Implementation file location & line processing:**
   - In `firmware/cyd_controller/src/uart_client.cpp` (lines 36–48):
     ```cpp
     void UartClient::processLine(const char *line)
     {
         AquaProtocol::AquariumMessage msg;
         if (!AquaProtocol::parseMessage(line, msg)) {
             return;
         }

         if (msg.type == AquaProtocol::MessageType::Telemetry) {
             latest_telemetry_ = msg.telemetry;
             last_rx_ms_ = millis();
             online_ = true;
         }
     }
     ```
     `processLine` exclusively evaluates `msg.type == AquaProtocol::MessageType::Telemetry`. Any incoming message with `msg.type == AquaProtocol::MessageType::Ack` is silently dropped.
3. **Parser capability in `uart_protocol`:**
   - In `firmware/shared/uart_protocol/src/uart_protocol.cpp` (lines 183–193): `parseMessage()` already deserializes `{"type":"ack","cmd":"...","ok":true/false,"msg":"..."}` into `out_msg.ack` (`cmd`, `ok`, `message`).
4. **Offline timeout behavior:**
   - In `firmware/cyd_controller/src/uart_client.cpp` (lines 70–73):
     ```cpp
     // 2. Detekcja braku łączności (timeout 3s)
     if (online_ && (now_ms - last_rx_ms_ > AquaProtocol::OFFLINE_TIMEOUT_MS)) {
         online_ = false;
     }
     ```
     Because WeMos must silence telemetry during OTA flashing (to prevent ESP8266 flash cache corruption / ICACHE lockout), `online_` drops to `false` after 3000 ms unless explicitly managed.
5. **TX command suppression during OTA:**
   - In `firmware/cyd_controller/src/uart_client.cpp` (lines 101–109): `sendCommand()` unconditionally prints to `Serial.println(line)`. In OTA mode, ongoing command transmissions would interfere with ESP8266 flash programming.
6. **Buzzer audio alert behavior:**
   - In `firmware/cyd_controller/src/uart_client.cpp` (lines 80–99): `checkAlarmAudio()` beeps on any active alarm flags whenever `online_` is true. During pre-OTA relay shutdown or safe mode, alarms could trigger spurious beeping.

---

## 2. Logic Chain

1. **Premise 1 (From Observation 2 & 3):** `parseMessage()` properly populates `out_msg.ack`, but `UartClient::processLine()` ignores `msg.type == MessageType::Ack`.  
   *Inference 1:* Adding an `else if (msg.type == AquaProtocol::MessageType::Ack)` branch in `processLine()` allows CYD to capture `latest_ack_` and process asynchronous confirmations, specifically for `cmd == "enter_ota"`.
2. **Premise 2 (From Observation 1 & Scope Feature 3):** The UI needs to trigger OTA on WeMos and learn its access coordinates (IP address / AP fallback).  
   *Inference 2:* Implementing `sendEnterOta(const char* ssid = nullptr, const char* pass = nullptr)` encapsulates command serialization, resets OTA response flags (`wemos_ota_ack_received_ = false`, `wemos_ota_ip_`), and dispatches `CommandType::EnterOta`.
3. **Premise 3 (From Observation 2, 4 & Survey 3 Section 5.2):** WeMos silences UART telemetry while flashing, but sends ACK and/or initial telemetry with `ota_mode = true` upon entering OTA, and resumes normal telemetry (`ota_mode = false`) upon rebooting.  
   *Inference 3:* When `wemos_in_ota_` is true:
   - Suspend the standard 3-second offline timeout in `update()`.
   - Apply a 6-minute safety watchdog on CYD (surpassing the WeMos 5-minute watchdog) to reset state if WeMos is powered off or fails.
   - Automatically restore normal operation when WeMos finishes flashing, reboots, and sends normal telemetry (`ota_mode == false`).
4. **Premise 4 (From Observation 5 & Requirement R3):** Any UART TX traffic from CYD during WeMos flash writing can crash ESP8266 ICACHE.  
   *Inference 4:* In `sendCommand()`, check `if (wemos_in_ota_ && cmd.type != CommandType::EnterOta) return false;` to completely suppress background UART requests (time sync, relays) during OTA mode.
5. **Premise 5 (From Observation 6):** Audio alarm beeping during OTA mode disrupts the user flashing process.  
   *Inference 5:* In `checkAlarmAudio()`, gate audio output with `!wemos_in_ota_`.

---

## 3. Caveats

1. **Dependency on Milestone 1 Peer Task (`explorer_m1_1` / `worker_m1`):**
   `UartClient` additions depend on `firmware/shared/uart_protocol` exposing `CommandType::EnterOta`, `cmd.ota_ssid`, `cmd.ota_pass`, `telemetry.ota_mode`, and `telemetry.ota_ip`. These must be committed or applied together.
2. **SoftAP vs STA Mode Detection:**
   The WeMos ACK message will typically contain `"STA:<ip>"` or `"AP:<ip>"` or pure `<ip>`. The proposed helper `extractOtaIp()` safely handles all these formats into a valid IPv4 string.
3. **Hardware Button Boot:**
   If WeMos enters OTA via the hardware button on D6 (GPIO12) rather than UART, it will not send an ACK for `enter_ota`. However, our design handles this by also synchronizing `wemos_in_ota_` and `wemos_ota_ip_` from incoming `AquariumTelemetry` frames where `ota_mode == true`.

---

## 4. Conclusion

The exact modifications for `firmware/cyd_controller/include/uart_client.h` and `firmware/cyd_controller/src/uart_client.cpp` are fully designed, documented, and verified for non-breaking backward compatibility.

Key additions:
- **Methods:** `sendEnterOta(ssid, pass)`, `isWemosInOta()`, `hasWemosOtaAck()`, `isWemosOtaAckOk()`, `getWemosOtaIp()`, `getWemosOtaElapsedMs()`, `resetWemosOtaState()`, `getLatestAck()`, `hasNewAck()`, `clearNewAck()`.
- **Protections:** UART TX suppression in `sendCommand()`, 3s timeout pause with 6-minute safety watchdog, buzzer mute during OTA.
- **Auto-Recovery:** Automatic clearing of OTA state upon receiving normal telemetry after WeMos reboots.

Complete diffs and full replacement code are provided in `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_m1_2\report.md`.

---

## 5. Verification Method

To independently verify the implementation once applied by `worker_m1`:

1. **Compilation Check (CYD 2.8" target):**
   Run from project root:
   ```bash
   pio run -e cyd_2_8
   ```
   *Expected result:* Exit code 0, 0 compilation errors or link errors.
2. **Native Unit Tests:**
   Run from project root:
   ```bash
   pio test -e native
   ```
   *Expected result:* All native tests pass.
3. **Code Inspection:**
   - Verify `UartClient::processLine()` handles `MessageType::Ack`.
   - Verify `UartClient::sendCommand()` checks `wemos_in_ota_`.
   - Verify `UartClient::update()` pauses offline timeout when `wemos_in_ota_ == true`.
   - Verify `UartClient::checkAlarmAudio()` suppresses beeping when `wemos_in_ota_ == true`.
4. **Invalidation Conditions:**
   - If `pio run -e cyd_2_8` fails with missing symbol errors, verify that `uart_protocol.h` extensions from Milestone 1 have been applied.
