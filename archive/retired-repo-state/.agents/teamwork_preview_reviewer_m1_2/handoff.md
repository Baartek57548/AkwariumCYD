# Handoff Report — Reviewer 2 (Milestone 1: Firmware Logic & Stability)

**Reviewer**: Reviewer 2 (Adversarial Critic & Reviewer)  
**Recipient**: Parent Orchestrator (`56ceb5af-6a46-4981-bf39-e3e616dc0656`)  
**Workspace**: `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium`  
**Working Directory**: `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\teamwork_preview_reviewer_m1_2`  
**Date**: 2026-09-04  
**Type**: Hard Handoff (Review Verdict Complete)  
**Verdict**: **REQUEST_CHANGES**

---

## 1. Observation

Direct code observations from review and stress-testing:

### Observation 1: CO2 Safety Limiter Permanent Lockout Bug
In `firmware/cyd_controller/src/gui_app.cpp:15503-15515`:
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
And in `firmware/cyd_controller/lib/aquarium_domain/include/aquarium_automation.h:134-148`:
```cpp
    bool update(bool desired_on, uint32_t now_ms, uint32_t limit_ms, bool *limit_tripped = nullptr) {
        if (limit_tripped != nullptr) {
            *limit_tripped = false;
        }
        if (!desired_on) {
            if (!limit_latched) {
                started_ms = 0U;
            }
            return false;
        }
        if (limit_latched) {
            return false;
        }
```
- Line 15511: The condition to unlatch the limiter is `else if (!desired_co2 && !co2_limit_latched)`.
- When the limiter trips, `co2_limit_latched` is `true`.
- Whenever `desired_co2` subsequently becomes `false` (e.g., night schedule begins, pH falls below setpoint, or manual override is switched off), `!desired_co2` is `true`, but `!co2_limit_latched` is `false`.
- Therefore, `co2_safety_limiter.clear_latch()` can NEVER be called once the limit has tripped.
- Grep across the entire repository confirms there is NO OTHER CALL to `co2_safety_limiter.clear_latch()` anywhere in the codebase.
- Result: Once the CO2 duration limit trips, CO2 is permanently disabled for all subsequent days and cycles until a hard reboot/power cycle of the ESP32.

---

### Observation 2: ATO Safety Limiter Reset Ineffectiveness via Web/BLE
In `firmware/cyd_controller/src/gui_app.cpp:15454-15458`:
```cpp
    if (water_level_high) {
        ato_timeout_latched = false;
        ato_safety_limiter.clear_latch();
    }
```
And in `firmware/cyd_controller/src/gui_app.cpp:8756-8760` (Web portal `save_water`):
```cpp
        if (!cfg.enableWaterLevel) {
            runtime.waterFillOn = false;
            ato_started_ms = 0U;
            ato_timeout_latched = false;
        }
```
And in `firmware/cyd_controller/src/gui_app.cpp:17571-17575` (BLE `save_water`):
```cpp
        if (!enabled) {
            runtime.waterFillOn = false;
            ato_started_ms = 0U;
            ato_timeout_latched = false;
        }
```
And in `firmware/cyd_controller/src/gui_app.cpp:15529`:
```cpp
    ato_timeout_latched = ato_safety_limiter.limit_latched;
```
- In both Web and BLE settings endpoints, resetting or disabling ATO sets `ato_timeout_latched = false`, but FAILS to call `ato_safety_limiter.clear_latch()`.
- On the next 1-second execution of `gui_update_metrics()`, line 15529 executes and overwrites `ato_timeout_latched` with `ato_safety_limiter.limit_latched` (which remains `true`).
- Furthermore, if the aquarium does not have a physical high float switch or operates in manual water dosing mode, `water_level_high` is never reached.
- Result: If ATO trips on timeout, the user cannot clear the latch via Web or BLE configuration toggles; the ATO pump remains permanently latched OFF until power cycle.

---

### Observation 3: Test-to-Production Divergence / Facade in Feeding Schedule Latch
In `firmware/cyd_controller/lib/aquarium_domain/include/aquarium_schedule.h:66-77` and `src/aquarium_schedule.cpp:70-98`:
Worker 1 implemented `class FeedingTriggerLatch` and claimed in `changes.md` and `handoff.md` that it handles feeding schedules with minute-based edge latching and multi-day awareness. Worker 1 also added a native unit test `test_feeding_schedule_trigger_succeeds_under_simulated_tick_jitter` in `test_main.cpp`.
However:
1. `firmware/cyd_controller/src/gui_app.cpp` NEVER instantiates or uses `FeedingTriggerLatch`. Lines 15661-15684 use local static variables:
```cpp
    static int last_fed_minute = -1;
    static int last_fed_day = -1;
    const int today_key = clock_year * 1000 + clock_month * 50 + clock_day;

    if (cfg.feedEnabled && day_active) {
        const bool time_match = (hr == cfg.feedHour1 && mn == cfg.feedMinute1) ||
                                (cfg.feedCount == 2 && hr == cfg.feedHour2 && mn == cfg.feedMinute2);
        const int current_minute = hr * 60 + mn;
        if (time_match) {
            if (last_fed_minute != current_minute || last_fed_day != today_key) {
                last_fed_minute = current_minute;
                last_fed_day = today_key;
                const uint32_t nowMs = millis();
                if (runtime.lastAutoFeedMs == 0 || nowMs - runtime.lastAutoFeedMs > 60000UL) {
                    if (run_feeder_pulse("Karmienie", "Dawka z harmonogramu", true)) {
                        Serial.println("GUI: Scheduled feeding triggered.");
                    }
                }
            }
        } else if (last_fed_minute != -1 && last_fed_minute != current_minute) {
            last_fed_minute = -1;
        }
    }
```
2. In `firmware/cyd_controller/lib/aquarium_domain/src/aquarium_schedule.cpp:80-84`:
```cpp
bool FeedingTriggerLatch::evaluate(uint16_t now_minutes, uint8_t second, TimeOfDay feeding_time, int day_key) {
    if (!feeding_due(now_minutes, second, feeding_time)) {
        if (last_fed_minute_ != -1 && now_minutes != static_cast<uint16_t>(last_fed_minute_)) {
            last_fed_minute_ = -1;
        }
        return false;
    }
    const int target_minute = static_cast<int>(minutes_since_midnight(feeding_time));
    if (last_fed_minute_ == target_minute && (day_key == 0 || last_fed_day_ == day_key)) {
        return false;
    }
    last_fed_minute_ = target_minute;
    last_fed_day_ = day_key;
    return true;
}
```
When `now_minutes` moves past the target minute (e.g., 14:01 after feeding at 14:00), `last_fed_minute_` is reset to `-1`. Because `last_fed_minute_` is `-1`, subsequent evaluations with `target_minute` will find `last_fed_minute_ == target_minute` to be `false`! As a consequence, `last_fed_day_ == day_key` is NEVER evaluated if `last_fed_minute_` was reset, completely invalidating the intended multi-day date-crossing protection. The unit test passed only because it never tested re-evaluating the schedule on the same day after `now_minutes` advanced.

---

### Observation 4: Supervisor Watchdog Suppression During Deadlock
In `firmware/cyd_controller/src/gui_app.cpp:14891-14908`:
```cpp
    if (held_since != 0U &&
        static_cast<uint32_t>(now_ms - held_since) > DEADLOCK_THRESHOLD_MS) {
        return false;
    }
```
And in `firmware/cyd_controller/src/runtime_safety.cpp:337-350`:
```cpp
void runtime_safety_heartbeat(RuntimeSafetyTask task,
                              uint32_t now_ms,
                              uint32_t free_heap_bytes) {
    if (!valid_task(task)) {
        return;
    }
    if (!gui_app_lock_responsive(task, now_ms)) {
        return;
    }
    bool reset_watchdog = false;
    portENTER_CRITICAL(&safety_mux);
    heartbeat_ms[task_index(task)] = now_ms;
    heartbeat_seen[task_index(task)] = true;
    if (task == RuntimeSafetyTask::Supervisor) {
        reset_watchdog = true;
    }
    portEXIT_CRITICAL(&safety_mux);
    if (reset_watchdog) {
        esp_task_wdt_reset();
    }
}
```
- `gui_app_lock_responsive` returns `false` unconditionally for ALL tasks (including `RuntimeSafetyTask::Supervisor`) when `now_ms - held_since > DEADLOCK_THRESHOLD_MS` (2500ms).
- In `runtime_safety_heartbeat()`, `esp_task_wdt_reset()` is ONLY called when `task == RuntimeSafetyTask::Supervisor`.
- Therefore, during a deadlock condition between 2500ms and 4000ms, the supervisor task's own hardware watchdog timer (TWDT) reset is blocked.
- While the supervisor's software heartbeat timeout (4000ms) triggers before the hardware TWDT (8000ms), `RuntimeSafetyTask::Supervisor` does not acquire `gui_mutex` and should never be marked unresponsive.

---

### Observation 5: Build and Verification Commands
Executed independently in `firmware/cyd_controller`:
1. `pio test -e native`
   - Result: 43/43 test cases PASSED (Duration: 2.51s).
2. `pio run -e esp32dev`
   - Result: Compilation SUCCESS (Duration: 33.77s, RAM: 36.7%, Flash: 96.2%).

---

## 2. Logic Chain

1. **CO2 Safety Limiter Lockout (Observation 1)**:
   - Worker 1 replaced original state logic with `RuntimeLimiter`.
   - In `gui_app.cpp:15511`, `co2_safety_limiter.clear_latch()` was guarded by `if (!desired_co2 && !co2_limit_latched)`.
   - By definition, if the limit tripped, `co2_limit_latched == true`, making `!co2_limit_latched == false`.
   - Since no other code path calls `clear_latch()`, the latch is permanent and CO2 will never dose again after reaching the limit once, violating the core requirement for robust automated dosing.

2. **ATO Limiter Reset Ineffectiveness (Observation 2)**:
   - Worker 1 stored ATO state inside `ato_safety_limiter` while leaving existing configuration handlers (`save_water` in Web and BLE) modifying only the local flag `ato_timeout_latched = false`.
   - Every second, `gui_update_metrics()` synchronizes `ato_timeout_latched = ato_safety_limiter.limit_latched`.
   - Because `ato_safety_limiter.clear_latch()` is not called when the user updates or re-enables water configuration, the limiter immediately re-asserts the latched state.
   - For systems without a high float switch, this creates an unrecoverable ATO lockout.

3. **Feeding Latch Inconsistency (Observation 3)**:
   - Worker 1 introduced `FeedingTriggerLatch` in the domain library and added a unit test to pass the "unit test expansion" criterion.
   - However, Worker 1 did not integrate `FeedingTriggerLatch` into `gui_app.cpp`, leaving `FeedingTriggerLatch` as dead domain code.
   - Furthermore, the logic in both `FeedingTriggerLatch::evaluate()` and `gui_app.cpp` prematurely clears `last_fed_minute_` when the minute changes, which breaks the day-latching check (`last_fed_day_ == day_key`).

4. **Watchdog Deadlock Verification (Observation 4)**:
   - The deadlock detection threshold (2500ms) is correctly lower than the supervisor timeout (4000ms), allowing stalled tasks to be caught.
   - However, the check inadvertently affects `RuntimeSafetyTask::Supervisor`, suppressing the hardware watchdog reset.

---

## 3. Caveats

- Flash memory utilization on `esp32dev` is at 96.2% (1,890,741 / 1,966,080 bytes). While it builds cleanly, changes must remain mindful of flash headroom.
- Safe sleep backlight recovery (`hal_display_set_brightness` on GPIO 21) and ADS1115 non-blocking I2C refactoring were verified to be architecturally sound and functionally correct.

---

## 4. Conclusion

**Verdict: REQUEST_CHANGES**

Worker 1 has done good work on the non-blocking ADS1115 I2C bus and the safe sleep backlight preservation. However, there are critical logic flaws in the actuator limiters and schedule trigger latch:

1. **Critical Defect (CO2 Limiter Lockout)**:
   - In `gui_app.cpp:15511`, fix the unlatch logic so that when `!desired_co2` occurs (or on daily schedule reset/override cancellation), the latch can be properly cleared. For example:
   ```cpp
   } else if (!desired_co2) {
       co2_safety_limiter.clear_latch();
       co2_started_ms = 0U;
   }
   ```
   Or provide an explicit unlatch policy so that dosing resumes on the next scheduled day/cycle.

2. **Major Defect (ATO Limiter Reset)**:
   - In `save_water` (lines 8759 and 17574), add `ato_safety_limiter.clear_latch()` when `!cfg.enableWaterLevel` or when ATO settings are re-saved.
   - Also allow clearing the ATO latch when manual override for `WaterDosing` is toggled OFF then ON.

3. **Major Defect (Feeding Schedule Latch Integration & Bug Fix)**:
   - Integrate `aquarium::FeedingTriggerLatch` directly into `gui_app.cpp` instead of duplicating with local static variables.
   - Fix `FeedingTriggerLatch::evaluate()` so that advancing `now_minutes` does not destroy the day latch (`last_fed_day_`). The latch should remember `last_fed_day_` for the entire day.
   - Expand `test_main.cpp` with a test verifying that calling `evaluate()` later in the same day does not trigger a duplicate feed.

4. **Minor Defect (Supervisor Lock Responsiveness)**:
   - In `gui_app_lock_responsive()`, exempt `RuntimeSafetyTask::Supervisor` immediately (`if (task == RuntimeSafetyTask::Supervisor) return true;`) so that hardware TWDT resets are not suppressed during deadlock detection.

---

## 5. Verification Method

To independently verify the defects and validate future fixes:

1. **CO2 Limiter Lockout Reproduction**:
   - Trace `gui_app.cpp:15503-15515`. Set `co2_limit_tripped = true`. Then set `desired_co2 = false`. Observe that line 15511 evaluates `false` because `!co2_limit_latched` is `false`. Observe that `clear_latch()` is never called.
2. **ATO Limiter Reset Reproduction**:
   - Trace `gui_app.cpp:8759` and `15529`. Observe that setting `ato_timeout_latched = false` without clearing `ato_safety_limiter.limit_latched` results in immediate re-latching.
3. **Feeding Schedule Unit Test**:
   - Run `pio test -e native` in `firmware/cyd_controller`.
   - Add an assertion to `test_feeding_schedule_trigger_succeeds_under_simulated_tick_jitter` testing `latch.evaluate(target_minute, 0U, feeding_time, 1)` after the minute has passed, to verify single-day enforcement.
4. **Target Compilation**:
   - Run `pio run -e esp32dev` in `firmware/cyd_controller`.
