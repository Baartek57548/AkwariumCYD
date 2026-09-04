# Handoff Report — Step 0 Survey (Firmware Logic & Stability)

**Agent**: Explorer 1 (Firmware Logic & Stability Explorer)  
**Recipient**: Parent Orchestrator (`56ceb5af-6a46-4981-bf39-e3e616dc0656`)  
**Workspace**: `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium`  
**Analysis File**: `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\teamwork_preview_explorer_survey_1\analysis.md`  
**Date**: 2026-09-04  
**Type**: Hard Handoff (Step 0 Survey Complete)

---

## 1. Observation

Direct code observations from `firmware/cyd_controller`:

1. **Safety Runtime Limits Bypassed by Manual Overrides**:
   - In `firmware/cyd_controller/src/gui_app.cpp:15366-15415`:
     ```cpp
     if (automation_co2_demand) {
         if (co2_started_ms == 0) co2_started_ms = now_ms;
         if (now_ms - co2_started_ms > co2_max_limit_ms) {
             runtime.co2On = false;
         } else {
             runtime.co2On = true;
         }
     } else {
         co2_started_ms = 0;
         runtime.co2On = false;
     }
     ```
   - In `firmware/cyd_controller/src/gui_app.cpp:15442-15457`:
     ```cpp
     control_modes.resolve(&runtime);
     ```
   - Observation: If automatic CO2 or ATO demand is false, `co2_started_ms` and `ato_started_ms` are continuously reset to 0. Downstream `control_modes.resolve()` overrides `runtime.co2On` or `runtime.waterFillOn` to `true`.

2. **Fragile Second Matching on Scheduled Feeding**:
   - In `firmware/cyd_controller/src/gui_app.cpp:15583` and `lib/aquarium_schedule/aquarium_schedule.cpp:66`:
     ```cpp
     if (now.hour == schedule.feed_hour && now.minute == schedule.feed_minute && now.second == 0)
     ```
   - Observation: Scheduled feeding triggers strictly when `now.second == 0`. Telemetry dispatch runs on a 1000ms periodic timer subject to FreeRTOS tick jitter and `gui_mutex` contention.

3. **Deadlock Invisibility in `safety_supervisor`**:
   - In `firmware/cyd_controller/src/runtime_controller.cpp:234` (`io_task`):
     ```cpp
     runtime_safety_heartbeat(TASK_ID_IO);
     if (!gui_app_lock(pdMS_TO_TICKS(50))) {
         vTaskDelay(pdMS_TO_TICKS(10));
         continue;
     }
     ```
   - In `firmware/cyd_controller/src/main.cpp:281` (`loop()`):
     ```cpp
     runtime_safety_heartbeat(TASK_ID_UI);
     if (gui_app_lock(pdMS_TO_TICKS(20))) {
         // UI service
         gui_app_unlock();
     }
     ```
   - Observation: Heartbeats are fed before acquiring `gui_mutex`. If `gui_mutex` is deadlocked, both tasks loop, fail the lock, and continue feeding heartbeats.

4. **Light Sleep Freezes Tasks and Triggers Watchdog Reset Crash**:
   - In `firmware/cyd_controller/src/gui_app.cpp:5731-5743`:
     ```cpp
     void light_sleep_authorized() {
         digitalWrite(21, LOW);
         esp_sleep_enable_timer_wakeup(10000000ULL); // 10s
         esp_light_sleep_start();
     }
     ```
   - In `firmware/cyd_controller/src/runtime_safety.cpp:106`: Heartbeat timeout threshold is `4000ms`.
   - Observation: 10s light sleep advances `millis()` past the 4000ms heartbeat limit, while GPIO 21 is multiplexed to LEDC PWM backlight.

5. **Unprotected Shared SD Card Access**:
   - In `firmware/cyd_controller/src/hal_sd.cpp`, no synchronization mutex exists.
   - Core 1 appends telemetry samples via `history_archive_append_sample()` in `gui_app.cpp:16420`.
   - Core 0 reads SD files via `ota_http_server.streamFile()` and reads `RELAY_CA_PATH` in `remote_alarm_relay.cpp:344`.

6. **I2C Bus Lock Held Across Delay in ADS1115 ADC**:
   - In `firmware/cyd_controller/src/hal_adc.cpp:166-203`:
     ```cpp
     if (hal_i2c_bus_lock(portMAX_DELAY)) {
         // writes config
         while (!(read_register(ADS1115_REG_CONFIG) & 0x8000)) {
             vTaskDelay(pdMS_TO_TICKS(1));
         }
         hal_i2c_bus_unlock();
     }
     ```
   - Observation: `hal_i2c_bus_lock` is held continuously across FreeRTOS context switches while waiting 8-20ms for conversion.

7. **Sensor Inventory Verification**:
   - DS18B20 1-Wire temperature sensor implemented on GPIO 17 (`hal_onewire_bus.cpp`).
   - DHT sensor implementation does NOT exist anywhere in the firmware source code.

8. **Build & Test Baseline**:
   - `pio test -e native`: 40 passed, 0 failed, 4.80s.
   - `pio run -e esp32dev`: SUCCESS (RAM: 36.7%, Flash: 96.1%).

---

## 2. Logic Chain

1. **ATO & CO2 Safety Hazard**:
   - From Observation 1: When automatic demand is false, `co2_started_ms` and `ato_started_ms` reset to 0 every second.
   - Downstream, `control_modes.resolve()` forces `runtime.waterFillOn` or `runtime.co2On` to true upon manual override.
   - Because the start timer is reset to 0 each second, the condition `now_ms - co2_started_ms > co2_max_limit_ms` never evaluates to true.
   - Therefore, a manual override command will run unbounded (up to the 24-hour manual timeout), risking severe tank flood or fish asphyxiation.

2. **Missed Feeding Schedule**:
   - From Observation 2: The trigger requires `sc == 0`.
   - In a multi-tasking FreeRTOS environment with 1000ms periodic tasks and mutex contention, tick scheduling can drift by >50ms.
   - A single drifted tick skipping from second 59 to second 01 causes `sc == 0` to never be hit.
   - Therefore, scheduled feedings can be silently skipped for the entire day.

3. **Deadlock Invisibility**:
   - From Observation 3: Heartbeats are updated at the loop start before `gui_app_lock()`.
   - If `gui_mutex` deadlocks, both tasks continue to execute their loop bodies, fail lock acquisition, and ping `runtime_safety_heartbeat()`.
   - Therefore, `safety_supervisor` never detects the deadlock, and failsafes will not engage.

4. **Watchdog Crash on Sleep**:
   - From Observation 4: Light sleep halts execution for 10 seconds.
   - `safety_supervisor` enforces a 4-second timeout.
   - Immediately upon waking, `safety_supervisor` evaluates `millis() - last_heartbeat > 4000ms` as true, causing an unexpected crash and reboot.

5. **Filesystem Corruption Risk**:
   - From Observation 5: Multiple FreeRTOS tasks on separate CPU cores invoke FatFs driver functions concurrently without a mutex.
   - FatFs is non-reentrant on shared volumes.
   - Therefore, concurrent history logging and web server downloads risk filesystem corruption.

6. **Test Suite Coverage Gap**:
   - From Observation 8: Existing 40 native tests pass, but only test clean domain calculations.
   - None of the 40 tests evaluate HAL bitmasks, DS18B20 CRC8 validation, manual override vs. safety limiter interaction, or schedule tick jitter.
   - Therefore, critical failure modes are completely uncovered by automated regression testing.

---

## 3. Caveats

- All findings were identified via read-only static analysis and compiler tool executions; physical target hardware was not powered on.
- Flash utilization is currently high at 96.1% (1.89 MB / 1.97 MB); any future code expansion must monitor partition boundaries.
- No DHT sensor exists in the codebase; any requirement referencing DHT applies solely to DS18B20 1-Wire sensors.

---

## 4. Conclusion

The firmware architecture demonstrates sound domain decoupling and mathematical rigor in calibration, but contains critical safety and concurrency flaws:
1. Manual overrides bypass ATO and CO2 safety runtime limiters.
2. Feeding schedules can be dropped due to `sc == 0` exact second matching.
3. Heartbeat watchdog cannot detect `gui_mutex` deadlocks.
4. Unprotected SD card access risks FatFs corruption.
5. ADS1115 conversion polling monopolizes the I2C bus.

All findings are documented in detail with line numbers and recommended fixes in `analysis.md`. Native test expansion is required to cover these edge cases.

---

## 5. Verification Method

To independently verify all observations, results, and assertions:

1. **Verify Native Unit Test Baseline**:
   ```powershell
   pio test -e native
   ```
   *Expected Result*: 40/40 tests pass in `test_native_domain/test_main.cpp`.

2. **Verify Target Compilation Baseline**:
   ```powershell
   pio run -e esp32dev
   ```
   *Expected Result*: Clean build success with Flash ~96.1% and RAM ~36.7%.

3. **Verify File Existence & Code Locations**:
   - Check `gui_app.cpp:15366-15457` for ATO/CO2 reset and manual override.
   - Check `gui_app.cpp:15583` for `sc == 0`.
   - Check `runtime_controller.cpp:234` and `main.cpp:281` for unconditional heartbeat calls.
   - Check `hal_adc.cpp:166-203` for I2C lock hold during conversion wait.
   - View `analysis.md`:
     `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\teamwork_preview_explorer_survey_1\analysis.md`
