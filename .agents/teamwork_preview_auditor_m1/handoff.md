# Forensic Audit Report — Milestone 1 (R1: Firmware Logic & Stability)

**Auditor**: Forensic Integrity Auditor (`teamwork_preview_auditor_m1`)  
**Target Work Product**: Worker 1 Deliverables for Milestone 1 (R1: Firmware Logic & Stability)  
**Parent Orchestrator**: `56ceb5af-6a46-4981-bf39-e3e616dc0656`  
**Workspace**: `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium`  
**Integrity Mode**: `development` (per `ORIGINAL_REQUEST.md:8`)  
**Audit Profile**: General Project  
**Date**: 2026-09-04  
**Verdict**: **CLEAN**

---

## Forensic Audit Summary

| Check | Result | Details |
|---|---|---|
| **Hardcoded Test Results** | **PASS** | No tautological checks (`TEST_ASSERT_TRUE(true)`) or static return matching in `test_main.cpp`. |
| **Facade Implementations** | **PASS** | Authentic domain logic in `RuntimeLimiter`, `FeedingTriggerLatch`, `hal_adc.cpp`, `gui_app_lock_responsive`. |
| **Pre-populated Artifacts** | **PASS** | No pre-generated or stale test log artifacts detected. |
| **Native Unit Test Suite** | **PASS** | `pio test -e native`: 43/43 test cases passed in 2.37s. |
| **Target Build Integrity (`esp32dev`)** | **PASS** | `pio run -e esp32dev`: Build SUCCESS in 29.96s (RAM: 36.7%, Flash: 96.2%). |
| **Cross-Target Build Integrity (`esp32dev-espnow`)** | **PASS** | `pio run -e esp32dev-espnow`: Build SUCCESS in 140.67s (RAM: 37.5%, Flash: 97.0%). |
| **Evasion & Hidden Bypasses** | **PASS** | Manual overrides strictly route through `RuntimeLimiter::update` with duration enforcement. |

---

## 1. Observation

Direct empirical observations gathered across modified files, test suites, and compiler builds:

1. **Source Code Modifications**:
   - `git status -s` confirms exactly 8 modified files matching Worker 1's `changes.md`:
     ```
     M firmware/cyd_controller/lib/aquarium_domain/include/aquarium_automation.h
     M firmware/cyd_controller/lib/aquarium_domain/include/aquarium_schedule.h
     M firmware/cyd_controller/lib/aquarium_domain/src/aquarium_schedule.cpp
     M firmware/cyd_controller/src/gui_app.cpp
     M firmware/cyd_controller/src/hal_adc.cpp
     M firmware/cyd_controller/src/runtime_controller.cpp
     M firmware/cyd_controller/src/runtime_safety.cpp
     M firmware/cyd_controller/test/test_native_domain/test_main.cpp
     ```

2. **Automated Safety Duration Limiting (`aquarium_automation.h` & `gui_app.cpp`)**:
   - `aquarium_automation.h:119-161`: `struct RuntimeLimiter` implements dynamic time difference tracking:
     `static_cast<uint32_t>(now_ms - started_ms) >= limit_ms`.
     Correctly prevents 0 sentinel collisions (`started_ms = now_ms == 0U ? 1U : now_ms;`) and handles unsigned 32-bit wrap-around.
   - `gui_app.cpp:15500-15535`: Both `desired_co2` and `desired_water_fill` are resolved with manual overrides via `control_modes.resolve()` *prior* to feeding candidate states into `co2_safety_limiter.update()` and `ato_safety_limiter.update()`. Actuator state is forced `false` when `limit_ms` is reached, latches `limit_latched`, and logs an error alarm.

3. **Jitter-Tolerant Schedule Triggering (`aquarium_schedule.cpp` & `gui_app.cpp`)**:
   - `aquarium_schedule.cpp:66`: `feeding_due()` updated from `second == 0U` to `second < 60U && is_valid_time(feeding_time) && now_minutes == minutes_since_midnight(feeding_time)`.
   - `aquarium_schedule.cpp:78-100`: `FeedingTriggerLatch::evaluate()` tracks `last_fed_minute_` and `last_fed_day_`. If `last_fed_minute_ == target_minute && (day_key == 0 || last_fed_day_ == day_key)`, trigger is suppressed, guaranteeing edge-triggered execution without duplicate feeds in the same minute.
   - `gui_app.cpp:15661-15685`: Implements matching minute/day edge latching:
     `const int today_key = clock_year * 1000 + clock_month * 50 + clock_day;`
     `const int current_minute = hr * 60 + mn;`
     `if (last_fed_minute != current_minute || last_fed_day != today_key)`.

4. **Non-Blocking ADS1115 Conversion Polling (`hal_adc.cpp`)**:
   - `hal_adc.cpp:176-204`: `hal_i2c_bus_unlock()` is called immediately following config write. `vTaskDelay(pdMS_TO_TICKS(1U))` runs *without* holding `hal_i2c_bus_lock`. Lock is acquired briefly only for register reads/writes, completely eliminating 8-20ms bus lockouts on MCP23017 relays and inputs.

5. **Supervisor Deadlock Detection & Safe Sleep (`runtime_safety.cpp`, `gui_app.cpp`, `runtime_controller.cpp`)**:
   - `gui_app.cpp:14808-14890`: Instrumented `gui_app_lock` and `gui_app_unlock` with `portENTER_CRITICAL(&lock_tracker_mux)`.
   - `gui_app.cpp:14865-14890`: `gui_app_lock_responsive()` evaluates whether `gui_mutex` has been held or blocked for longer than `DEADLOCK_THRESHOLD_MS` (2500ms).
   - `runtime_safety.cpp:345`: `runtime_safety_heartbeat()` checks `gui_app_lock_responsive(task, now_ms)`. If unresponsive, returns early to suppress heartbeat update, allowing `safety_supervisor` to detect starvation at 4000ms and execute safe reboot.
   - `runtime_safety.cpp:363-386`: `runtime_safety_prepare_sleep()` and `runtime_safety_post_sleep()` refresh all task heartbeat timestamps to current `millis()` and invoke `esp_task_wdt_reset()`.
   - `gui_app.cpp:5742-5747`: `light_sleep_authorized()` replaces `digitalWrite(HwConfig::Backlight::PIN, ...)` with `hal_display_set_brightness()`, preserving LEDC PWM timer attachment on GPIO 21.

6. **Native Unit Test Suite Execution**:
   - Command: `pio test -e native`
   - Output verbatim:
     ```
     ================= 43 test cases: 43 succeeded in 00:00:02.367 =================
     ```
   - Three newly added tests:
     - `test_ato_runtime_safety_limit_enforced_during_manual_override`: validates cutoff and latching during manual override.
     - `test_co2_runtime_safety_limit_enforced_during_manual_override`: validates CO2 duration limit under override.
     - `test_feeding_schedule_trigger_succeeds_under_simulated_tick_jitter`: validates trigger at second 1, suppression of duplicate at second 2, and rollover to day 2.

7. **Compilation Verification**:
   - Command: `pio run -e esp32dev`
     - Result: Code 0, SUCCESS in 29.96s (RAM: 36.7%, Flash: 96.2%).
   - Command: `pio run -e esp32dev-espnow`
     - Result: Code 0, SUCCESS in 140.67s (RAM: 37.5%, Flash: 97.0%).

---

## 2. Logic Chain

1. **Integrity Rule Compliance**:
   - Per `ORIGINAL_REQUEST.md:8`, the required integrity mode is `development`.
   - Prohibited patterns in development mode are hardcoded test results, facade implementations, and fabricated verification outputs.
   - Inspection of `test_main.cpp` shows all test cases invoke member functions (`update()`, `evaluate()`, `resolve()`) and assert on dynamically calculated values. Grep for `TEST_ASSERT_TRUE(true)` and `TEST_ASSERT_FALSE(false)` yielded 0 matches.
   - Code inspections across all 8 modified files confirm full algorithmic implementations with proper state machines, concurrency primitives (FreeRTOS spinlocks and mutexes), and error handling.
   - Therefore, no prohibited patterns exist.

2. **Firmware Logic Correctness**:
   - Prior code reset `started_ms` to 0 whenever automatic demand was false, allowing manual overrides to bypass duration limits. By tracking elapsed time across candidate states in `RuntimeLimiter`, the limiter terminates actuator activation when `now_ms - started_ms >= limit_ms` regardless of input mode.
   - Prior feeding logic compared `second == 0U`. In FreeRTOS dual-core environments with 1000ms periodic tasks, tick drift can skip second 0. By latching on `current_minute` and tracking `last_fed_minute` / `last_fed_day`, feeding reliably fires once during the scheduled minute without duplicate pulses.
   - Prior ADS1115 driver held `hal_i2c_bus_lock` across `vTaskDelay(1)`. Releasing the lock before sleeping ensures the I2C bus remains available for MCP23017 relay commands during ADC conversions.
   - Prior light sleep detached GPIO 21 PWM and triggered 4-second supervisor watchdogs upon wake. Using `hal_display_set_brightness()` keeps the PWM channel attached, and updating heartbeats in `runtime_safety_post_sleep()` prevents false watchdog resets.

3. **Empirical Verification**:
   - The native test suite ran independently with 43/43 passing tests.
   - Both target PlatformIO environments (`esp32dev` and `esp32dev-espnow`) compiled cleanly to completion.

---

## 3. Caveats

- **Flash Usage Proximity**: Firmware flash utilization is at 96.2% on `esp32dev` and 97.0% on `esp32dev-espnow`. While within partition bounds, subsequent milestones (M2, M3) should avoid unnecessary code bloat to stay comfortably below 100%.
- **Hardware Simulation**: I2C bus timings and relay switching were verified through unit test harness logic and static code analysis; physical oscilloscope measurements of I2C bus traffic require physical hardware bench testing.

---

## 4. Conclusion

The work product delivered by Worker 1 for Milestone 1 (R1: Firmware Logic & Stability) satisfies all acceptance criteria, adheres strictly to the architectural constraints in `PROJECT.md` and user requirements in `ORIGINAL_REQUEST.md`, and is completely free of facades, hardcoding, or integrity violations.

**Verdict**: **CLEAN**

---

## 5. Verification Method

To independently reproduce this forensic audit:

1. **Run Native Domain Unit Tests**:
   ```powershell
   cd c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\firmware\cyd_controller
   pio test -e native
   ```
   *Expected*: 43 test cases: 43 succeeded.

2. **Run ESP32 Target Compilation**:
   ```powershell
   cd c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\firmware\cyd_controller
   pio run -e esp32dev
   ```
   *Expected*: Exit code 0, firmware image generated under `.pio/build/esp32dev/firmware.bin`.

3. **Verify Absence of Tautological Test Patterns**:
   ```powershell
   git grep "TEST_ASSERT_TRUE(true)" firmware/cyd_controller/test/
   git grep "TEST_ASSERT_FALSE(false)" firmware/cyd_controller/test/
   ```
   *Expected*: 0 matches.
