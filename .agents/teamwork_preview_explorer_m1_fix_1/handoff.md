# Handoff Report — Explorer 1 (Milestone 1 Gate Resolution: Actuator Safety Limiter Latches)

**Agent**: Explorer 1 (`teamwork_preview_explorer_m1_fix_1`)  
**Parent Orchestrator**: `d608c00a-48aa-4e84-ad45-bc28b06cef03`  
**Workspace**: `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium`  
**Date**: 2026-09-04  
**Type**: Hard Handoff (Investigation & Complete Remediation Plan)  

---

## 1. Observation

Direct code inspection and tool outputs reveal the following facts:

### 1.1 Critical Defect 1: Permanent CO2 Lockout in `gui_app.cpp:15511`
In `firmware/cyd_controller/src/gui_app.cpp:15501-15515`:
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
- Line 15511 conditions unlatching on `!desired_co2 && !co2_limit_latched`.
- When the limiter trips on runtime limit exceeded, `co2_limit_latched` is set to `true` (line 15505).
- Whenever `desired_co2` becomes `false` (night schedule, target pH reached, or manual override disabled), `!co2_limit_latched` evaluates to `false`.
- Therefore, line 15512 (`co2_safety_limiter.clear_latch()`) is **never executed**.
- A full repository grep confirms line 15512 is the ONLY call site for `co2_safety_limiter.clear_latch()`.
- Once tripped, the CO2 solenoid is locked out permanently until ESP32 reboot.

### 1.2 Critical Defect 2: ATO Safety Limiter Desynchronization in Web & BLE Handlers
In `firmware/cyd_controller/src/gui_app.cpp`:
- Web Factory Reset (line 8703):
  `ato_timeout_latched = false;` (does NOT call `ato_safety_limiter.clear_latch()`).
- Web `save_water` (line 8759):
  `if (!cfg.enableWaterLevel) { runtime.waterFillOn = false; ato_started_ms = 0U; ato_timeout_latched = false; }` (does NOT call `ato_safety_limiter.clear_latch()`).
- BLE `save_water` (line 17574):
  `if (!enabled) { runtime.waterFillOn = false; ato_started_ms = 0U; ato_timeout_latched = false; }` (does NOT call `ato_safety_limiter.clear_latch()`).
- BLE Factory Reset (line 17726):
  `ato_timeout_latched = false;` (does NOT call `ato_safety_limiter.clear_latch()`).
- In `gui_update_metrics()` (line 15529):
  `ato_timeout_latched = ato_safety_limiter.limit_latched;`
  Because `ato_safety_limiter.limit_latched` remains `true`, `ato_timeout_latched` is immediately re-asserted to `true` within 1000ms.
- Line 15457 (`if (water_level_high) { ato_timeout_latched = false; ato_safety_limiter.clear_latch(); }`) is the ONLY place that ever called `ato_safety_limiter.clear_latch()`. If the water is low (the reason ATO runs) or in systems without a high float switch, this code cannot execute.

### 1.3 `RuntimeLimiter` Edge Case at `millis() == 0U` in `aquarium_automation.h`
In `firmware/cyd_controller/lib/aquarium_domain/include/aquarium_automation.h:148-151`:
```cpp
if (started_ms == 0U) {
    started_ms = now_ms == 0U ? 1U : now_ms;
}
if (static_cast<uint32_t>(now_ms - started_ms) >= limit_ms) {
    limit_latched = true;
...
```
- If an actuator turns on at boot when `now_ms == 0U`, `started_ms` is assigned `1U`.
- The elapsed calculation `static_cast<uint32_t>(0U - 1U)` evaluates to `0xFFFFFFFF` (`4,294,967,295U`).
- For any configured `limit_ms` (e.g. 5000ms, 120000ms), `4294967295U >= limit_ms` evaluates to `true`.
- Result: Any actuator active at boot immediately trips and latches off on tick 0.

### 1.4 Native Test Harness Collision
Running `pio test -e native` in `firmware/cyd_controller`:
- Fails with linker error:
  `multiple definition of 'main'`
  `.pio\build\native\test\test_native_domain\test_main.o: first defined here`
  `.pio\build\native\test\adversarial_stress_test.o: first defined here`
- Untracked files `firmware/cyd_controller/test/adversarial_stress_test.cpp` and `adversarial_stress_test.exe` were placed in `test/`, breaking PlatformIO's native test compilation runner.

### 1.5 Additional Reviewer Findings
1. `GuiMutexGuard` in `gui_app.cpp:79-90` bypasses `gui_app_lock()` / `gui_app_unlock()`, causing all 18 guard scopes (including `gui_app_service_background()` in `io_task`) to not record lock metrics, hold duration (`lock_acquired_ms`), or fail timestamps.
2. `gui_app_lock_responsive()` in `gui_app.cpp:14898, 14904` uses unsigned direct comparisons (`ui_fail > ui_success`, `io_fail > io_success`), which fail across the 49.7-day `millis()` rollover boundary. Furthermore, it suppresses hardware TWDT resets during deadlocks because it does not exempt `RuntimeSafetyTask::Supervisor`.
3. `FeedingTriggerLatch` in `aquarium_schedule.cpp:81-83` resets `last_fed_minute_ = -1` as soon as `now_minutes != last_fed_minute_`, which invalidates the subsequent `last_fed_day_ == day_key` check if NTP clock steps backwards on the same day. Additionally, `gui_app.cpp:15661-15682` duplicated this flawed logic using local static variables instead of using the domain class.

---

## 2. Logic Chain

1. **CO2 Lockout**:
   - Limit exceeded -> `co2_safety_limiter.limit_latched = true` -> `co2_limit_latched = true`.
   - Demand ends -> `desired_co2 = false`.
   - Condition `!desired_co2 && !co2_limit_latched` -> `true && false` -> `false`.
   - Branch skipped -> `clear_latch()` never called -> CO2 remains dead until hardware power cycle.
   - **Resolution**: Change condition to `else if (!desired_co2) { co2_safety_limiter.clear_latch(); co2_limit_latched = false; co2_started_ms = 0U; }`. When demand ends (night schedule, pH reached, or manual override disabled), the latch clears cleanly for the next cycle.

2. **ATO Limiter Desynchronization**:
   - Web/BLE reset handlers set `ato_timeout_latched = false`, but leave `ato_safety_limiter.limit_latched == true`.
   - In `gui_update_metrics()`, line 15529 executes every second: `ato_timeout_latched = ato_safety_limiter.limit_latched;`.
   - This immediately re-latches `ato_timeout_latched = true`, discarding the user's reset.
   - **Resolution**:
     a) In all reset sites (web `save_water`, web factory reset, BLE `save_water`, BLE factory reset, and timed override clear/toggle off), explicitly call `ato_safety_limiter.clear_latch()`.
     b) In `gui_update_metrics()`, synchronize: `if (water_level_high || (!ato_timeout_latched && ato_safety_limiter.limit_latched)) { ato_timeout_latched = false; ato_safety_limiter.clear_latch(); }`. This guarantees mutual consistency even if external code modifies only one flag.

3. **`RuntimeLimiter` at `millis() == 0U`**:
   - Overloading `started_ms == 0U` as a sentinel creates an underflow when `now_ms == 0U`.
   - **Resolution**: Introduce an explicit `bool running;` field in `RuntimeLimiter`. `started_ms` stores `now_ms` exactly without sentinel offsets. At `now_ms == 0U`, `now_ms - started_ms == 0U < limit_ms`, so it runs normally without tripping.

4. **Test Harness**:
   - PlatformIO compiles all `.cpp` files in `test/`. Having two files with `main()` breaks `pio test -e native`.
   - **Resolution**: Remove `test/adversarial_stress_test.cpp` and `test/adversarial_stress_test.exe`, and incorporate its test scenarios into `test/test_native_domain/test_main.cpp`.

---

## 3. Caveats

- Flash memory on `esp32dev` is currently at 96.2% (1,890,741 / 1,966,080 bytes). All firmware additions must be inline, minimal, and avoid heavy string or buffer allocations.
- Changes to `gui_app.cpp` cannot be directly unit-tested in native Unity runner due to tight coupling with FreeRTOS and LVGL; domain logic (`RuntimeLimiter` and `FeedingTriggerLatch`) in `aquarium_domain` must be comprehensively verified in `test_main.cpp`.
- No caveats regarding reproducibility: both defects were reproduced and verified under trace analysis and adversarial test cases.

---

## 4. Conclusion & Precise Remediation Plan

The following changes must be applied by the Worker:

### File 1: `firmware/cyd_controller/lib/aquarium_domain/include/aquarium_automation.h`
**Lines 119-161**: Replace `RuntimeLimiter` with explicit `running` state:
```cpp
struct RuntimeLimiter {
    uint32_t started_ms;
    bool limit_latched;
    bool running;

    RuntimeLimiter() : started_ms(0U), limit_latched(false), running(false) {}

    void reset() {
        started_ms = 0U;
        limit_latched = false;
        running = false;
    }

    void clear_latch() {
        limit_latched = false;
        started_ms = 0U;
        running = false;
    }

    bool update(bool desired_on, uint32_t now_ms, uint32_t limit_ms, bool *limit_tripped = nullptr) {
        if (limit_tripped != nullptr) {
            *limit_tripped = false;
        }
        if (!desired_on) {
            if (!limit_latched) {
                started_ms = 0U;
                running = false;
            }
            return false;
        }
        if (limit_latched) {
            return false;
        }
        if (!running) {
            started_ms = now_ms;
            running = true;
        }
        if (static_cast<uint32_t>(now_ms - started_ms) >= limit_ms) {
            limit_latched = true;
            started_ms = 0U;
            running = false;
            if (limit_tripped != nullptr) {
                *limit_tripped = true;
            }
            return false;
        }
        return true;
    }
};
```

### File 2: `firmware/cyd_controller/lib/aquarium_domain/src/aquarium_schedule.cpp`
**Lines 79-93**: Fix same-day re-trigger protection across clock jumps:
```cpp
bool FeedingTriggerLatch::evaluate(uint16_t now_minutes, uint8_t second, TimeOfDay feeding_time, int day_key) {
    if (!feeding_due(now_minutes, second, feeding_time)) {
        return false;
    }
    if (day_key != 0 && last_fed_day_ == day_key) {
        return false;
    }
    const int target_minute = static_cast<int>(minutes_since_midnight(feeding_time));
    if (day_key == 0 && last_fed_minute_ == target_minute) {
        return false;
    }
    last_fed_minute_ = target_minute;
    last_fed_day_ = day_key;
    return true;
}
```

### File 3: `firmware/cyd_controller/src/gui_app.cpp`

1. **Lines 75-90 (`GuiMutexGuard`)**: Forward-declare lock functions and instrument guard:
```cpp
StaticSemaphore_t gui_mutex_storage;
SemaphoreHandle_t gui_mutex = nullptr;
bool gui_ready = false;

bool gui_app_lock(uint32_t timeout_ms);
void gui_app_unlock(void);

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

    bool locked() const {
        return locked_;
    }

    GuiMutexGuard(const GuiMutexGuard &) = delete;
    GuiMutexGuard &operator=(const GuiMutexGuard &) = delete;

private:
    bool locked_;
};
```

2. **Lines 8701-8705 (Web Factory Reset)**: Clear latches:
```cpp
        runtime.waterFillOn = false;
        ato_started_ms = 0U;
        ato_timeout_latched = false;
        ato_safety_limiter.clear_latch();
        co2_limit_latched = false;
        co2_safety_limiter.clear_latch();
        co2_started_ms = 0U;
```

3. **Lines 8756-8761 (Web `save_water`)**: Clear limiter latch when water disabled:
```cpp
        if (!cfg.enableWaterLevel) {
            runtime.waterFillOn = false;
            ato_started_ms = 0U;
            ato_timeout_latched = false;
            ato_safety_limiter.clear_latch();
        }
```

4. **Lines 14872-14909 (`gui_app_lock_responsive`)**: Exempt supervisor task and use signed difference for millis rollover:
```cpp
bool gui_app_lock_responsive(RuntimeSafetyTask task, uint32_t now_ms) {
    if (task == RuntimeSafetyTask::Supervisor) {
        return true;
    }
    if (now_ms < 15000U) {
        return true;
    }
    uint32_t held_since = 0U;
    uint32_t ui_success = 0U;
    uint32_t ui_fail = 0U;
    uint32_t io_success = 0U;
    uint32_t io_fail = 0U;
    portENTER_CRITICAL(&lock_tracker_mux);
    held_since = lock_acquired_ms;
    ui_success = last_ui_lock_success_ms;
    ui_fail = last_ui_lock_fail_ms;
    io_success = last_io_lock_success_ms;
    io_fail = last_io_lock_fail_ms;
    portEXIT_CRITICAL(&lock_tracker_mux);

    constexpr uint32_t DEADLOCK_THRESHOLD_MS = 2500U;

    if (held_since != 0U &&
        static_cast<uint32_t>(now_ms - held_since) > DEADLOCK_THRESHOLD_MS) {
        return false;
    }
    if (task == RuntimeSafetyTask::Ui) {
        if (ui_success != 0U &&
            static_cast<uint32_t>(now_ms - ui_success) > DEADLOCK_THRESHOLD_MS &&
            static_cast<int32_t>(ui_fail - ui_success) > 0) {
            return false;
        }
    } else if (task == RuntimeSafetyTask::Io) {
        if (io_success != 0U &&
            static_cast<uint32_t>(now_ms - io_success) > DEADLOCK_THRESHOLD_MS &&
            static_cast<int32_t>(io_fail - io_success) > 0) {
            return false;
        }
    }
    return true;
}
```

5. **Lines 15455-15458 (ATO Latch Synchronization in `gui_update_metrics`)**:
```cpp
    if (water_level_high || (!ato_timeout_latched && ato_safety_limiter.limit_latched)) {
        ato_timeout_latched = false;
        ato_safety_limiter.clear_latch();
    }
```

6. **Lines 15511-15514 (CO2 Latch Reset in `gui_update_metrics`)**:
```cpp
    } else if (!desired_co2) {
        co2_safety_limiter.clear_latch();
        co2_limit_latched = false;
        co2_started_ms = 0U;
    }
```

7. **Lines 15661-15684 (Feeding Schedule Integration in `gui_update_metrics`)**:
Replace local static variables with `aquarium::FeedingTriggerLatch`:
```cpp
    static aquarium::FeedingTriggerLatch feed_latch_1;
    static aquarium::FeedingTriggerLatch feed_latch_2;
    const int today_key = clock_year * 1000 + clock_month * 50 + clock_day;

    if (cfg.feedEnabled && day_active) {
        const uint16_t current_minute = static_cast<uint16_t>(hr * 60 + mn);
        const aquarium::TimeOfDay feed1 = {cfg.feedHour1, cfg.feedMinute1};
        const bool feed1_due = feed_latch_1.evaluate(current_minute, static_cast<uint8_t>(sc), feed1, today_key);
        bool feed2_due = false;
        if (cfg.feedCount == 2) {
            const aquarium::TimeOfDay feed2 = {cfg.feedHour2, cfg.feedMinute2};
            feed2_due = feed_latch_2.evaluate(current_minute, static_cast<uint8_t>(sc), feed2, today_key);
        }
        if (feed1_due || feed2_due) {
            const uint32_t nowMs = millis();
            if (runtime.lastAutoFeedMs == 0 || nowMs - runtime.lastAutoFeedMs > 60000UL) {
                if (run_feeder_pulse("Karmienie", "Dawka z harmonogramu", true)) {
                    Serial.println("GUI: Scheduled feeding triggered.");
                }
            }
        }
    }
```

8. **Lines 17571-17575 (BLE `save_water`)**:
```cpp
        if (!enabled) {
            runtime.waterFillOn = false;
            ato_started_ms = 0U;
            ato_timeout_latched = false;
            ato_safety_limiter.clear_latch();
        }
```

9. **Lines 17724-17728 (BLE Factory Reset)**:
```cpp
        runtime.waterFillOn = false;
        ato_started_ms = 0U;
        ato_timeout_latched = false;
        ato_safety_limiter.clear_latch();
        co2_limit_latched = false;
        co2_safety_limiter.clear_latch();
        co2_started_ms = 0U;
```

10. **Lines 17918-17924 & 17940-17947 (BLE/Web Timed Override Handling)**:
When `WaterDosing` override is disabled or cleared:
`ato_timeout_latched = false; ato_safety_limiter.clear_latch(); ato_started_ms = 0U;`
When `Co2` override is disabled or cleared:
`co2_limit_latched = false; co2_safety_limiter.clear_latch(); co2_started_ms = 0U;`

### File 4: Clean up `firmware/cyd_controller/test/`
- Delete `firmware/cyd_controller/test/adversarial_stress_test.cpp` and `firmware/cyd_controller/test/adversarial_stress_test.exe`.

### File 5: `firmware/cyd_controller/test/test_native_domain/test_main.cpp`
Add unit test functions:
1. `test_runtime_limiter_boot_at_millis_zero()`:
   Verify `update(true, 0U, 5000U, &tripped)` returns `true`, `tripped == false`, `limit_latched == false`.
2. `test_runtime_limiter_millis_wrap_around()`:
   Verify starting at `0xFFFFFFF0U` with `50U` limit trips at `0x00000025U` (53ms elapsed across boundary).
3. `test_runtime_limiter_rapid_toggle_resets_duration()`:
   Verify turning off before limit resets timer and allows full duration upon re-activation.
4. `test_co2_limiter_clears_latch_when_demand_drops()`:
   Verify that after limit trips, calling `update(false, ...)` followed by `clear_latch()` allows subsequent `update(true, ...)` to run normally.
5. `test_feeding_schedule_prevents_duplicate_same_day_after_minute_advances()`:
   Verify that after 14:00:00 triggers on day 1, advancing to 14:01:00 and stepping back to 14:00:05 does NOT re-trigger on the same day, but does trigger on day 2.

---

## 5. Verification Method

The Worker can independently verify all remediations using:

1. **Native Unit Tests**:
   ```powershell
   cd firmware\cyd_controller
   pio test -e native
   ```
   *Expected*: Passes with 0 errors (all existing tests + 5 new tests pass, no multiple definition of `main`).

2. **Target Compilation**:
   ```powershell
   cd firmware\cyd_controller
   pio run -e esp32dev
   ```
   *Expected*: Clean build with exit code 0, Flash < 1,966,080 bytes.

3. **Logic Verification**:
   - Inspect `gui_app.cpp:15511`: verify `} else if (!desired_co2)` allows CO2 unlatching.
   - Inspect `gui_app.cpp:8703, 8759, 15456, 17574, 17726`: verify `ato_safety_limiter.clear_latch()` is invoked alongside `ato_timeout_latched = false;`.
   - Inspect `aquarium_automation.h:119`: verify `RuntimeLimiter` uses `running` flag and survives `now_ms == 0U`.
