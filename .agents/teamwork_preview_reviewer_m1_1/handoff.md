# Handoff Report — Reviewer 1 (Milestone 1: Firmware Logic & Stability)

**Reviewer**: Reviewer 1 (Reviewer & Adversarial Critic)  
**Recipient**: Parent Orchestrator (`56ceb5af-6a46-4981-bf39-e3e616dc0656`)  
**Workspace**: `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium`  
**Working Directory**: `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\teamwork_preview_reviewer_m1_1`  
**Date**: 2026-09-04  
**Type**: Hard Handoff (Milestone 1 Review Complete)  

---

## Review Summary

**Verdict**: **REQUEST_CHANGES**

Worker 1 made substantial progress by introducing clean domain structures (`RuntimeLimiter`, `FeedingTriggerLatch`), fixing the light sleep PWM/watchdog conflict, refactoring ADS1115 I2C delay handling, and expanding the native test suite from 40 to 43 tests. Target compilation (`pio run -e esp32dev`) and native unit tests (`pio test -e native`) pass cleanly.

However, adversarial code review and logic tracing revealed **two Critical defects** and **one Major design gap** in the firmware runtime:
1. **Critical Defect 1 (Permanent CO2 Lockout)**: In `src/gui_app.cpp:15511`, clearing the CO2 latch is conditioned on `!desired_co2 && !co2_limit_latched`. Once CO2 trips the safety limit, `co2_limit_latched` is true, which makes `!co2_limit_latched` false forever. As there is no other latch-clearing site for CO2, the CO2 dosing system is permanently disabled until an ESP32 hardware reboot.
2. **Critical Defect 2 (`ato_safety_limiter` Desynchronization)**: In `src/gui_app.cpp:15529`, `ato_timeout_latched` is unconditionally overwritten each second from `ato_safety_limiter.limit_latched`. However, when users reset settings or disable/re-enable ATO via Web Portal (`save_water`, factory reset) or BLE commands, only `ato_timeout_latched = false;` is modified—`ato_safety_limiter.clear_latch()` is never called. Consequently, `ato_safety_limiter.limit_latched` remains true and immediately re-latches `ato_timeout_latched = true` on the next second, rendering user resets completely ineffective.
3. **Major Gap 3 (`GuiMutexGuard` Bypasses Lock & Deadlock Tracking)**: In `src/gui_app.cpp:79-90`, `GuiMutexGuard` takes and gives `gui_mutex` via raw FreeRTOS calls (`xSemaphoreTakeRecursive` / `xSemaphoreGiveRecursive`) instead of `gui_app_lock()` and `gui_app_unlock()`. All 18 call sites using `GuiMutexGuard`—including `gui_app_service_background()` in `io_task`—bypass lock metrics, failure timestamps, and held-time tracking.

---

## 1. Observation

Direct observations from independent verification commands and code inspection:

1. **Independent Verification Commands**:
   - `pio test -e native` (in `firmware/cyd_controller`):
     - **Result**: `PASSED` (43 test cases: 43 succeeded in 2.98s).
   - `pio run -e esp32dev` (in `firmware/cyd_controller`):
     - **Result**: `SUCCESS` in 36.15s.
     - **Memory Footprint**: RAM: 36.7% (used 120,244 bytes from 327,680 bytes); Flash: 96.2% (used 1,890,741 bytes from 1,966,080 bytes).

2. **CO2 Latch Logic in `firmware/cyd_controller/src/gui_app.cpp:15501-15515`**:
   ```cpp
   const uint32_t co2_max_limit_ms = static_cast<uint32_t>(co2_max_time_minutes) * 60000UL;
   bool co2_limit_tripped = false;
   runtime.co2On = co2_safety_limiter.update(desired_co2, control_now_ms, co2_max_limit_ms, &co2_limit_tripped);
   co2_started_ms = co2_safety_limiter.started_ms;
   co2_limit_latched = co2_safety_limiter.limit_latched;
   if (co2_limit_tripped) {
       add_gui_log("CO2: przekroczono limit czasu dozowania", true);
       if (cfg.enableAerator && aerator_window_active && !leak_valve_interlock) {
           runtime.airOn = true;
       }
   } else if (!desired_co2 && !co2_limit_latched) {
       co2_safety_limiter.clear_latch();
       co2_started_ms = 0U;
   }
   ```
   - Only 3 lines in the entire firmware reference `co2_limit_latched`: declaration at line 377, assignment at line 15505, and condition at line 15511.
   - Line 15512 is the only place in the entire firmware that invokes `co2_safety_limiter.clear_latch()`.

3. **ATO Latch Synchronization in `firmware/cyd_controller/src/gui_app.cpp`**:
   - `ato_timeout_latched` is cleared at:
     - Line 8703 (Web factory reset)
     - Line 8759 (Web `save_water`: `if (!cfg.enableWaterLevel) ato_timeout_latched = false;`)
     - Line 17574 (BLE `save_water`: `if (!enabled) ato_timeout_latched = false;`)
     - Line 17726 (BLE factory reset)
   - In NONE of these lines is `ato_safety_limiter.clear_latch()` called.
   - At line 15529:
     ```cpp
     ato_timeout_latched = ato_safety_limiter.limit_latched;
     ```
   - The only line that invokes `ato_safety_limiter.clear_latch()` is line 15457:
     ```cpp
     if (water_level_high) {
         ato_timeout_latched = false;
         ato_safety_limiter.clear_latch();
     }
     ```

4. **`GuiMutexGuard` Implementation in `firmware/cyd_controller/src/gui_app.cpp:79-90`**:
   ```cpp
   class GuiMutexGuard {
   public:
       explicit GuiMutexGuard(uint32_t timeout_ms)
           : locked_(gui_mutex != nullptr &&
                     xSemaphoreTakeRecursive(gui_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
       }

       ~GuiMutexGuard() {
           if (locked_) {
               xSemaphoreGiveRecursive(gui_mutex);
           }
       }
   ...
   ```
   - `GuiMutexGuard` bypasses `gui_app_lock()` and `gui_app_unlock()`.
   - Used at 18 call sites across `gui_app.cpp`, including `gui_app_service_background()` (line 14995).

5. **Millis Comparison in `firmware/cyd_controller/src/gui_app.cpp:14896-14906`**:
   ```cpp
   if (task == RuntimeSafetyTask::Ui) {
       if (ui_success != 0U &&
           static_cast<uint32_t>(now_ms - ui_success) > DEADLOCK_THRESHOLD_MS &&
           ui_fail > ui_success) {
           return false;
       }
   } else if (task == RuntimeSafetyTask::Io) {
       if (io_success != 0U &&
           static_cast<uint32_t>(now_ms - io_success) > DEADLOCK_THRESHOLD_MS &&
           io_fail > io_success) {
           return false;
       }
   }
   ```
   - `ui_fail > ui_success` and `io_fail > io_success` are direct unsigned 32-bit comparisons.

---

## 2. Logic Chain

1. **CO2 Permanent Lockout**:
   - When CO2 runs for `co2_max_time_minutes`, `co2_safety_limiter.update()` trips, setting `co2_safety_limiter.limit_latched = true`.
   - At line 15505, `co2_limit_latched` is set to `true`.
   - When the dosing period finishes, or schedule window closes, or user deactivates manual override, `desired_co2` becomes `false`.
   - The code enters the branch evaluation at line 15511: `else if (!desired_co2 && !co2_limit_latched)`.
   - Because `co2_limit_latched == true`, `!co2_limit_latched` evaluates to `false`.
   - Thus, `co2_safety_limiter.clear_latch()` is **never called**.
   - On the next day, or when manual override is requested again, `co2_safety_limiter.limit_latched` is still `true`. `update()` immediately returns `false`.
   - **Conclusion**: The CO2 solenoid will never open again until the ESP32 is power-cycled.

2. **ATO Reset Inefficacy**:
   - When ATO times out due to an empty reservoir, `ato_safety_limiter.limit_latched = true` and `ato_timeout_latched = true`.
   - The user refills the reservoir and uses the Web Portal or BLE app to toggle or reset the ATO settings.
   - The API/BLE handler sets `ato_timeout_latched = false;`.
   - Within 1000ms, `gui_update_metrics()` executes. Line 15527 evaluates `ato_safety_limiter.update()`. Since `ato_safety_limiter.limit_latched` was not cleared, it returns `false`.
   - At line 15529, `ato_timeout_latched = ato_safety_limiter.limit_latched;` sets `ato_timeout_latched` back to `true`.
   - The only way to clear `ato_safety_limiter` is `if (water_level_high)`. But because the aquarium water is low and ATO is disabled, water cannot rise to the high float switch automatically.
   - **Conclusion**: The user is trapped in an un-resettable ATO lockout without a hardware reboot or manually pouring buckets of water to trigger the high sensor.

3. **`GuiMutexGuard` Tracking Bypass**:
   - Worker 1 reordered `io_task` to run `gui_app_service_background()` before `runtime_safety_heartbeat()`, claiming lock acquisition failures would update lock responsiveness metrics.
   - However, `gui_app_service_background()` acquires the lock using `GuiMutexGuard guard(50U);`.
   - Because `GuiMutexGuard` calls FreeRTOS `xSemaphoreTakeRecursive` directly, neither `last_io_lock_success_ms` nor `last_io_lock_fail_ms` nor `lock_acquired_ms` are modified.
   - Furthermore, prolonged stalls or deadlocks in any of the 18 `GuiMutexGuard` scopes will not set `lock_acquired_ms`, leaving `held_since == 0U` in `gui_app_lock_responsive()`.
   - **Conclusion**: Lock monitoring and deadlock detection do not cover the background service or web/BLE handlers as claimed.

4. **Millis Rollover Comparison**:
   - When uptime passes 49.7 days, `millis()` rolls over to 0.
   - An unsigned comparison `ui_fail > ui_success` where `ui_fail` was logged at `0xFFFFFFFE` and `ui_success` at `0x00000010` evaluates to `true` (`0xFFFFFFFE > 0x10`), leading to false positive deadlock detection.
   - **Conclusion**: Signed circular distance `static_cast<int32_t>(ui_fail - ui_success) > 0` must be used.

---

## 3. Caveats

- Unit test coverage does not currently include full end-to-end simulation of `gui_update_metrics()` because `gui_app.cpp` depends heavily on Arduino and LVGL symbols not present in native test runner.
- The 43 native domain unit tests pass because they test `RuntimeLimiter` and `FeedingTriggerLatch` in isolation; the unit tests did not cover the clearing logic in `gui_app.cpp`.
- Target compilation on `esp32dev` succeeded with 0 warnings/errors, but Flash memory utilization is at 96.2%. Any additional code must be memory-conscious.

---

## 4. Conclusion & Required Changes

The changes for Milestone 1 are sound in concept and address the architectural root causes, but contain critical implementation bugs in state unlatching and lock wrapping.

### Required Changes:

1. **Fix CO2 Latch Reset in `firmware/cyd_controller/src/gui_app.cpp:15511`**:
   Change:
   ```cpp
   } else if (!desired_co2 && !co2_limit_latched) {
       co2_safety_limiter.clear_latch();
       co2_started_ms = 0U;
   }
   ```
   To:
   ```cpp
   } else if (!desired_co2) {
       co2_safety_limiter.clear_latch();
       co2_started_ms = 0U;
   }
   ```
   Ensure that when CO2 demand ends (schedule window closes, target pH reached, or manual override disabled), the safety limiter latch is cleared for subsequent cycles.

2. **Synchronize ATO Latch Reset across Web and BLE in `src/gui_app.cpp`**:
   - In `save_water` (lines 8759 and 17574) and factory reset handlers (lines 8703 and 17726):
     Add `ato_safety_limiter.clear_latch();` alongside `ato_timeout_latched = false;`.
   - In `gui_update_metrics()`: If `!ato_timeout_latched && ato_safety_limiter.limit_latched`, invoke `ato_safety_limiter.clear_latch();` so that any external clearing of `ato_timeout_latched` propagates to the limiter object.

3. **Refactor `GuiMutexGuard` in `src/gui_app.cpp:79-90`**:
   Refactor `GuiMutexGuard` constructor and destructor to route through `gui_app_lock(timeout_ms)` and `gui_app_unlock()`:
   ```cpp
   class GuiMutexGuard {
   public:
       explicit GuiMutexGuard(uint32_t timeout_ms)
           : locked_(gui_app_lock(timeout_ms)) {
       }

       ~GuiMutexGuard() {
           if (locked_) {
               gui_app_unlock();
           }
       }
       ...
   ```
   This ensures that all 18 lock scopes correctly populate lock metrics, deadlock timers, and nesting counts.

4. **Use Circular Difference for Rollover in `src/gui_app.cpp:14896-14906`**:
   Replace `ui_fail > ui_success` with `static_cast<int32_t>(ui_fail - ui_success) > 0`, and `io_fail > io_success` with `static_cast<int32_t>(io_fail - io_success) > 0`.

5. **Add Unit Tests for State Unlatching in `test_main.cpp`**:
   Add test assertions verifying that turning off desired demand unlatches the limiter, allowing subsequent activations to run up to the limit again.

---

## 5. Verification Method

To re-verify after Worker 1 applies fixes:

1. **Native Unit Tests**:
   ```powershell
   cd firmware\cyd_controller
   pio test -e native
   ```
   *Expected*: All tests pass (including new unlatching assertions).

2. **Target Compilation**:
   ```powershell
   cd firmware\cyd_controller
   pio run -e esp32dev
   ```
   *Expected*: Clean compilation with exit code 0, Flash size strictly under 1,966,080 bytes.

3. **Code Inspection**:
   - Confirm `gui_app.cpp` clears `co2_safety_limiter` when `!desired_co2`.
   - Confirm all reset paths call `ato_safety_limiter.clear_latch()`.
   - Confirm `GuiMutexGuard` calls `gui_app_lock()` and `gui_app_unlock()`.
