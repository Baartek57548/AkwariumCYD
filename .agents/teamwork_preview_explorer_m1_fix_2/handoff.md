# Handoff Report — Explorer 2 (Milestone 1 Gate: Feeding Trigger Latch & Multi-Day Retention)

**Author**: Explorer 2 (Teamwork Explorer)  
**Recipient**: Parent Orchestrator (`d608c00a-48aa-4e84-ad45-bc28b06cef03`)  
**Workspace Root**: `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium`  
**Working Directory**: `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\teamwork_preview_explorer_m1_fix_2`  
**Date**: 2026-09-04  
**Type**: Hard Handoff (Investigation Complete)  

---

## 1. Observation

Direct code observations from inspection, static analysis, and test runs:

### Observation 1: Date Latch Destruction in `FeedingTriggerLatch::evaluate`
In `firmware/cyd_controller/lib/aquarium_domain/src/aquarium_schedule.cpp:79-93`:
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
- Line 81-83: When `now_minutes` advances away from `target_minute` (e.g. at 14:01 after a 14:00 feeding), `last_fed_minute_` is unconditionally reset to `-1`.
- Line 87: The day-latch check `(day_key == 0 || last_fed_day_ == day_key)` is guarded by `last_fed_minute_ == target_minute && ...`.
- When `last_fed_minute_` has been reset to `-1`, `last_fed_minute_ == target_minute` evaluates to `false` (-1 == 840 is false).
- Consequently, if the system clock steps backward (e.g. via NTP sync or manual time update) back to the feeding minute on the SAME day, line 87 evaluates to `false` and line 90 executes, triggering a duplicate feed.

### Observation 2: Test Harness Empirical Proof of Duplicate Feeding
In `firmware/cyd_controller/test/adversarial_stress_test.cpp:173-198`:
```cpp
// 7. Challenge: NTP backward clock jump triggers duplicate feeding
void challenge_feeding_backward_clock_jump_same_day() {
    FeedingTriggerLatch latch;
    TimeOfDay feed_time = {14, 0}; // 14:00 (minute 840)
    int day_key = 20260904;

    bool f1 = latch.evaluate(840, 5, feed_time, day_key);
    assert(f1);

    bool f2 = latch.evaluate(840, 30, feed_time, day_key);
    assert(!f2);

    bool f3 = latch.evaluate(841, 5, feed_time, day_key);
    assert(!f3);

    // NTP sync steps clock backward to 14:00 on the same day
    bool f4 = latch.evaluate(840, 10, feed_time, day_key);

    if (f4) {
        record("FeedingTriggerLatch: NTP backward jump duplicate feeding", false,
               "LOGIC FLAW: Clearing last_fed_minute_ at minute 841 allows duplicate feeding at 14:00 if clock steps backwards on same day!");
    } else {
        record("FeedingTriggerLatch: NTP backward jump duplicate feeding", true,
               "Correctly prevents duplicate feeding after backward clock jump");
    }
}
```
- `f4` evaluates to `true` (tripping the duplicate feeding bug) because `f3` at minute 841 reset `last_fed_minute_ = -1`.

### Observation 3: `gui_app.cpp` Bypasses `FeedingTriggerLatch` with Local Static Variables
In `firmware/cyd_controller/src/gui_app.cpp:15657-15684`:
```cpp
    int wday = get_weekday(clock_day, clock_month, clock_year);
    int bit_idx = (wday == 0) ? 6 : (wday - 1);
    bool day_active = (cfg.feedDays & (1 << bit_idx)) != 0;

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
- `gui_app.cpp` never instantiates `aquarium::FeedingTriggerLatch`.
- Lines 15661-15662 define raw local statics `last_fed_minute` and `last_fed_day`.
- Both Feed 1 (`cfg.feedHour1`, `cfg.feedMinute1`) and Feed 2 (`cfg.feedHour2`, `cfg.feedMinute2`) share the single set of static variables.
- Line 15680-15682 resets `last_fed_minute = -1;` as soon as `last_fed_minute != current_minute`.
- When `last_fed_minute == -1`, line 15670 `if (last_fed_minute != current_minute || last_fed_day != today_key)` ALWAYS evaluates to `true` because `(-1 != current_minute)` is `true`. The `last_fed_day` check is completely bypassed on any subsequent trigger within the same day!

### Observation 4: Existing Unit Test Coverage in `test_main.cpp:1544-1569`
```cpp
static void test_feeding_schedule_trigger_succeeds_under_simulated_tick_jitter() {
    aquarium::FeedingTriggerLatch latch;
    const aquarium::TimeOfDay feeding_time = {14U, 0U}; // 14:00
    const uint16_t target_minute = 14U * 60U;          // 840 minutes

    // 13:59:59 - not feeding time yet
    TEST_ASSERT_FALSE(latch.evaluate(target_minute - 1U, 59U, feeding_time, 1));
    TEST_ASSERT_FALSE(aquarium::feeding_due(target_minute - 1U, 59U, feeding_time));

    // Jitter occurrence: tick drifts past second 0, arrives at second 1 (14:00:01)
    // Both feeding_due and FeedingTriggerLatch must recognize the trigger!
    TEST_ASSERT_TRUE(aquarium::feeding_due(target_minute, 1U, feeding_time));
    TEST_ASSERT_TRUE(latch.evaluate(target_minute, 1U, feeding_time, 1));

    // Next tick at second 2 (14:00:02): latch prevents duplicate feeding
    TEST_ASSERT_FALSE(latch.evaluate(target_minute, 2U, feeding_time, 1));

    // Tick at second 59: still latched
    TEST_ASSERT_FALSE(latch.evaluate(target_minute, 59U, feeding_time, 1));

    // Next minute (14:01:00): not feeding time
    TEST_ASSERT_FALSE(latch.evaluate(target_minute + 1U, 0U, feeding_time, 1));

    // Next day at 14:00:03 (second 0 skipped again on day 2): successfully triggers!
    TEST_ASSERT_TRUE(latch.evaluate(target_minute, 3U, feeding_time, 2));
}
```
- Line 1565 calls `evaluate(target_minute + 1U, 0U, feeding_time, 1)`, resetting `last_fed_minute_` to `-1`.
- The test only passes because line 1568 tests Day 2 (`day_key = 2`), never checking whether Day 1 would re-trigger if evaluated again at `target_minute`.

### Observation 5: Build Toolchain Conflict in `firmware/cyd_controller/test`
- Executing `pio test -e native` failed with:
  ```
  .pio\build\native\test\test_native_domain\test_main.o:test_main.cpp: multiple definition of 'main'
  .pio\build\native\test\adversarial_stress_test.o:adversarial_stress_test.cpp: first defined here
  collect2.exe: error: ld returned 1 exit status
  ```
- Reviewer 2 added `adversarial_stress_test.cpp` directly in `firmware/cyd_controller/test/`. Because PlatformIO builds all `.cpp` files in `test/`, both files provide a `main()` function, breaking `pio test -e native`.
- The test harness build must be restored either by adding `test_ignore = adversarial_stress_test*` to `platformio.ini` or moving `adversarial_stress_test.cpp` into its own test subdirectory.

---

## 2. Logic Chain

1. **Root Cause of Duplicate Feeding**:
   - A scheduled daily feeding event is intended to occur **at most once per calendar day** per configured slot.
   - In `FeedingTriggerLatch::evaluate()`, clearing `last_fed_minute_ = -1` when leaving the target minute was intended only as a fallback re-arm mechanism for cases where no date information exists (`day_key == 0`).
   - However, Worker 1 made this reset unconditional for all `day_key` values.
   - When `last_fed_minute_` becomes `-1`, the expression `last_fed_minute_ == target_minute && (day_key == 0 || last_fed_day_ == day_key)` becomes false because `last_fed_minute_ == target_minute` is false.
   - The short-circuit evaluation prevents `last_fed_day_ == day_key` from ever being checked once `now_minutes` has left `target_minute`.
   - Any subsequent evaluation at `target_minute` on the same day (such as after an NTP clock slew or manual clock adjustment) treats the schedule as armed and fires again.

2. **Integration Gap in `gui_app.cpp`**:
   - `gui_app.cpp` failed to adopt the domain library `FeedingTriggerLatch`, instead duplicating the flawed logic using local static variables.
   - Furthermore, `gui_app.cpp` evaluated both Feed 1 and Feed 2 against a single set of static variables:
     `time_match = (hr == cfg.feedHour1 && mn == cfg.feedMinute1) || (cfg.feedCount == 2 && hr == cfg.feedHour2 && mn == cfg.feedMinute2);`
   - If Feed 1 triggered at 08:00, `last_fed_minute` was 480. At 18:00 (Feed 2), `last_fed_minute != current_minute` (480 != 1080) was true, firing Feed 2.
   - But at 18:01, `last_fed_minute` was reset to `-1`.
   - If time ever fluctuated back to 08:00 or 18:00 on that same day, `-1 != current_minute` triggered another feeding.
   - To have robust, independent schedule tracking, Feed 1 and Feed 2 must each possess an independent `FeedingTriggerLatch` instance.

3. **Multi-Day Retention Model**:
   - When `day_key != 0`:
     - Once triggered on `day_key`, the latch must record `last_fed_day_ = day_key` and `last_fed_minute_ = target_minute`.
     - Advancing `now_minutes` past `target_minute` must NOT modify `last_fed_minute_` or `last_fed_day_`.
     - When `feeding_due` is true, if `day_key != 0 && last_fed_day_ == day_key`, the latch immediately returns `false`.
     - When a new day arrives (`day_key != last_fed_day_`), the check `last_fed_day_ == day_key` naturally evaluates to `false`, permitting the next day's feeding to trigger at `target_minute`.
   - When `day_key == 0`:
     - In the absence of a day key, the latch falls back to minute-window latching: it returns `false` while `last_fed_minute_ == target_minute`, and resets `last_fed_minute_ = -1` when `now_minutes != target_minute`.

---

## 3. Caveats

- **Clock Sanity**: The multi-day retention logic relies on `today_key` being positive and unique per calendar day (`clock_year * 1000 + clock_month * 50 + clock_day`). In `gui_app.cpp`, `clock_year` is initialized to 2026 (or 1970 if uncalibrated RTC), ensuring `today_key` is always non-zero.
- **Schedule Configuration Updates**: If a user reconfigures `cfg.feedHour1` or `cfg.feedCount` during the day, the latches must be explicitly cleared via `reset()` so that newly scheduled times take effect immediately rather than waiting for tomorrow.
- **Flash Utilization**: `esp32dev` Flash is currently at 96.2% (1,890,741 / 1,966,080 bytes). The proposed remediation reuses existing domain structures and replaces 25 lines of ad-hoc code in `gui_app.cpp` with clean calls, adding essentially 0 bytes of Flash overhead.

---

## 4. Conclusion & Precise Remediation Plan

### Remediation Step 1: Fix `FeedingTriggerLatch::evaluate` in `lib/aquarium_domain/src/aquarium_schedule.cpp`

**Target File**: `firmware/cyd_controller/lib/aquarium_domain/src/aquarium_schedule.cpp`  
**Target Lines**: 79–93

**Replacement Content**:
```cpp
bool FeedingTriggerLatch::evaluate(uint16_t now_minutes, uint8_t second, TimeOfDay feeding_time, int day_key) {
    if (!feeding_due(now_minutes, second, feeding_time)) {
        if (day_key == 0 && last_fed_minute_ != -1 && now_minutes != static_cast<uint16_t>(last_fed_minute_)) {
            last_fed_minute_ = -1;
        }
        return false;
    }
    const int target_minute = static_cast<int>(minutes_since_midnight(feeding_time));
    if ((day_key != 0 && last_fed_day_ == day_key) ||
        (day_key == 0 && last_fed_minute_ == target_minute)) {
        return false;
    }
    last_fed_minute_ = target_minute;
    last_fed_day_ = day_key;
    return true;
}
```

**Key Rationale**:
- If `day_key != 0`, `last_fed_minute_` is NEVER reset when leaving the target minute, preserving `last_fed_day_ == day_key` for the entire day.
- If `day_key != 0` and `last_fed_day_ == day_key`, `evaluate()` immediately returns `false`, preventing any duplicate trigger on that date even if clock time jitters backwards to `target_minute`.
- If `day_key == 0`, the fallback minute-window latching continues to function safely.

---

### Remediation Step 2: Integrate `FeedingTriggerLatch` into `src/gui_app.cpp`

**Target File**: `firmware/cyd_controller/src/gui_app.cpp`

1. **Declare File-Level Latch Instances and Helper (around line 380)**:
   Place immediately after `static aquarium::RuntimeLimiter ato_safety_limiter;`:
   ```cpp
   static aquarium::RuntimeLimiter ato_safety_limiter;
   static aquarium::FeedingTriggerLatch feeding_latch_slot1;
   static aquarium::FeedingTriggerLatch feeding_latch_slot2;

   static void gui_reset_feeding_latches() {
       feeding_latch_slot1.reset();
       feeding_latch_slot2.reset();
   }
   ```

2. **Refactor Feeding Evaluation in `gui_update_metrics()` (lines 15657–15684)**:
   Replace lines 15657–15684 with:
   ```cpp
       int wday = get_weekday(clock_day, clock_month, clock_year);
       int bit_idx = (wday == 0) ? 6 : (wday - 1);
       bool day_active = (cfg.feedDays & (1 << bit_idx)) != 0;
       const int today_key = clock_year * 1000 + clock_month * 50 + clock_day;
       const uint8_t current_sec = static_cast<uint8_t>(constrain(sc, 0, 59));

       if (cfg.feedEnabled && day_active) {
           const aquarium::TimeOfDay feed_time1 = {cfg.feedHour1, cfg.feedMinute1};
           bool feed_due = feeding_latch_slot1.evaluate(nowMins, current_sec, feed_time1, today_key);

           if (cfg.feedCount == 2) {
               const aquarium::TimeOfDay feed_time2 = {cfg.feedHour2, cfg.feedMinute2};
               if (feeding_latch_slot2.evaluate(nowMins, current_sec, feed_time2, today_key)) {
                   feed_due = true;
               }
           }

           if (feed_due) {
               const uint32_t nowMs = millis();
               if (runtime.lastAutoFeedMs == 0 || nowMs - runtime.lastAutoFeedMs > 60000UL) {
                   if (run_feeder_pulse("Karmienie", "Dawka z harmonogramu", true)) {
                       Serial.println("GUI: Scheduled feeding triggered.");
                   }
               }
           }
       }
   ```

3. **Wire Latch Resets on Configuration Changes**:
   - In Web Portal `save_feeding` (line 8375):
     ```cpp
     sanitize_config(cfg);
     gui_reset_feeding_latches();
     gui_app_save_settings();
     ```
   - In BLE `apply_schedules_json_locked` (line 17371):
     ```cpp
     sanitize_config(cfg);
     gui_reset_feeding_latches();
     gui_app_save_settings();
     ```
   - In Factory Reset handlers (lines 8703, 17726):
     ```cpp
     gui_reset_feeding_latches();
     ```

---

### Remediation Step 3: Expand Native Unit Tests in `test_main.cpp`

**Target File**: `firmware/cyd_controller/test/test_native_domain/test_main.cpp`  
**Location**: After line 1569 (after `test_feeding_schedule_trigger_succeeds_under_simulated_tick_jitter`)

**Add 3 New Dedicated Unit Tests**:

```cpp
static void test_feeding_schedule_latch_retains_day_latch_after_minute_advances_and_ntp_jump() {
    aquarium::FeedingTriggerLatch latch;
    const aquarium::TimeOfDay feeding_time = {14U, 0U}; // 14:00 (840 min)
    const uint16_t target_minute = 14U * 60U;          // 840 minutes
    const int day_1 = 20260904;
    const int day_2 = 20260905;

    // Day 1: normal trigger at 14:00:05
    TEST_ASSERT_TRUE(latch.evaluate(target_minute, 5U, feeding_time, day_1));

    // Day 1: immediate latching within same minute
    TEST_ASSERT_FALSE(latch.evaluate(target_minute, 20U, feeding_time, day_1));

    // Day 1: minute advances past target (14:01:00). Must NOT disarm day latch!
    TEST_ASSERT_FALSE(latch.evaluate(target_minute + 1U, 0U, feeding_time, day_1));
    TEST_ASSERT_FALSE(latch.evaluate(target_minute + 15U, 30U, feeding_time, day_1));

    // Day 1: NTP backward clock jump back to 14:00:10 on Day 1 must NOT re-trigger!
    TEST_ASSERT_FALSE(latch.evaluate(target_minute, 10U, feeding_time, day_1));
    TEST_ASSERT_FALSE(latch.evaluate(target_minute, 0U, feeding_time, day_1));

    // Day 2 arrives: triggers successfully at 14:00:02
    TEST_ASSERT_TRUE(latch.evaluate(target_minute, 2U, feeding_time, day_2));

    // Day 2: minute advances past target (14:02:00)
    TEST_ASSERT_FALSE(latch.evaluate(target_minute + 2U, 0U, feeding_time, day_2));

    // Day 2: NTP backward clock jump back to 14:00:30 on Day 2 must NOT re-trigger!
    TEST_ASSERT_FALSE(latch.evaluate(target_minute, 30U, feeding_time, day_2));
}

static void test_feeding_schedule_multi_slot_independence_and_reset() {
    aquarium::FeedingTriggerLatch slot1;
    aquarium::FeedingTriggerLatch slot2;
    const aquarium::TimeOfDay feed1 = {8U, 30U};   // 08:30 (510 min)
    const aquarium::TimeOfDay feed2 = {18U, 15U};  // 18:15 (1095 min)
    const uint16_t min1 = 8U * 60U + 30U;
    const uint16_t min2 = 18U * 60U + 15U;
    const int day_1 = 20260904;
    const int day_2 = 20260905;

    // Morning: Slot 1 triggers, Slot 2 does not
    TEST_ASSERT_TRUE(slot1.evaluate(min1, 0U, feed1, day_1));
    TEST_ASSERT_FALSE(slot2.evaluate(min1, 0U, feed2, day_1));

    // Slot 1 remains latched later in morning and survives time jump
    TEST_ASSERT_FALSE(slot1.evaluate(min1 + 5U, 0U, feed1, day_1));
    TEST_ASSERT_FALSE(slot1.evaluate(min1, 15U, feed1, day_1));

    // Evening: Slot 2 triggers, Slot 1 does not
    TEST_ASSERT_FALSE(slot1.evaluate(min2, 0U, feed1, day_1));
    TEST_ASSERT_TRUE(slot2.evaluate(min2, 0U, feed2, day_1));

    // Slot 2 remains latched and survives time jump
    TEST_ASSERT_FALSE(slot2.evaluate(min2 + 1U, 0U, feed2, day_1));
    TEST_ASSERT_FALSE(slot2.evaluate(min2, 45U, feed2, day_1));

    // Manual reset (e.g. user updated schedule or factory reset) re-arms
    slot1.reset();
    TEST_ASSERT_TRUE(slot1.evaluate(min1, 30U, feed1, day_1));

    // Day 2: both slots trigger independently at their respective times
    TEST_ASSERT_TRUE(slot1.evaluate(min1, 1U, feed1, day_2));
    TEST_ASSERT_TRUE(slot2.evaluate(min2, 1U, feed2, day_2));
}

static void test_feeding_schedule_day_key_zero_minute_window_fallback() {
    aquarium::FeedingTriggerLatch latch;
    const aquarium::TimeOfDay feed_time = {14U, 0U};
    const uint16_t target_minute = 840U;

    // Initial trigger at target minute
    TEST_ASSERT_TRUE(latch.evaluate(target_minute, 0U, feed_time, 0));

    // Latched during target minute
    TEST_ASSERT_FALSE(latch.evaluate(target_minute, 30U, feed_time, 0));

    // Advances to minute 841: re-arms because day_key == 0
    TEST_ASSERT_FALSE(latch.evaluate(target_minute + 1U, 0U, feed_time, 0));

    // Next occurrence: triggers again
    TEST_ASSERT_TRUE(latch.evaluate(target_minute, 0U, feed_time, 0));
}
```

And register in `main()` of `test_main.cpp`:
```cpp
    RUN_TEST(test_feeding_schedule_latch_retains_day_latch_after_minute_advances_and_ntp_jump);
    RUN_TEST(test_feeding_schedule_multi_slot_independence_and_reset);
    RUN_TEST(test_feeding_schedule_day_key_zero_minute_window_fallback);
```

---

### Remediation Step 4: Fix `platformio.ini` Native Test Collision
To eliminate the linker conflict caused by `test/adversarial_stress_test.cpp`:
In `firmware/cyd_controller/platformio.ini` under `[env:native]`, add:
```ini
test_ignore = adversarial_stress_test*
```
Or move `firmware/cyd_controller/test/adversarial_stress_test.cpp` into `firmware/cyd_controller/test/test_adversarial_stress/adversarial_stress_test.cpp`.

---

## 5. Verification Method

To verify these fixes independently:

1. **Native Unit Tests Execution**:
   ```powershell
   cd firmware\cyd_controller
   pio test -e native
   ```
   *Expected Output*:
   - 46 test cases passed (43 previous + 3 new feeding tests).
   - Zero linker errors (`multiple definition of 'main'`).

2. **Adversarial Stress Test Verification**:
   ```powershell
   cd firmware\cyd_controller
   g++ -std=c++11 -I lib/aquarium_domain/include lib/aquarium_domain/src/aquarium_schedule.cpp test/adversarial_stress_test.cpp -o test/adversarial_stress_test.exe
   .\test\adversarial_stress_test.exe
   ```
   *Expected Output*:
   `[PASS] FeedingTriggerLatch: NTP backward jump duplicate feeding -- Correctly prevents duplicate feeding after backward clock jump`
   `[PASS] FeedingTriggerLatch: Next day trigger -- Successfully triggers at 12:30 on the next day`

3. **Target Platform Build Integrity**:
   ```powershell
   cd firmware\cyd_controller
   pio run -e esp32dev
   ```
   *Expected Output*: Exit code 0, Flash memory usage <= 1,966,080 bytes.

4. **Code Inspection**:
   - Confirm `gui_app.cpp` imports and declares `feeding_latch_slot1` and `feeding_latch_slot2`.
   - Confirm `gui_update_metrics()` does not contain local statics `last_fed_minute` / `last_fed_day`.
   - Confirm `aquarium_schedule.cpp` preserves `last_fed_day_` and does not clear `last_fed_minute_` when `day_key != 0`.
