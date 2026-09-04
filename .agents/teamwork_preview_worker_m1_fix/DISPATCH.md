## 2026-09-04T10:47:33Z
You are Worker 1 (Remediation) for Milestone 1 Gate Resolution.
Your working directory is: c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\teamwork_preview_worker_m1_fix
The workspace root is: c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium

MANDATORY FIRST STEPS:
1. Read the authoritative user request:
   c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\ORIGINAL_REQUEST.md
2. Read the project scope document:
   c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\PROJECT.md
3. Read the three Explorer handoff reports which contain the complete, verified, line-by-line remediation blueprints:
   - Explorer 1 (Actuator Latches & RuntimeLimiter): c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\teamwork_preview_explorer_m1_fix_1\handoff.md
   - Explorer 2 (Feeding Schedule Latch & Production Integration): c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\teamwork_preview_explorer_m1_fix_2\handoff.md
   - Explorer 3 (Concurrency, GuiMutexGuard & Supervisor Deadlock Exemption): c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\teamwork_preview_explorer_m1_fix_3\handoff.md

EXCLUSIVE FILE WRITE OWNERSHIP:
You own exclusively:
- firmware/cyd_controller/lib/aquarium_domain/include/aquarium_automation.h
- firmware/cyd_controller/lib/aquarium_domain/include/aquarium_schedule.h
- firmware/cyd_controller/lib/aquarium_domain/src/aquarium_schedule.cpp
- firmware/cyd_controller/src/gui_app.cpp
- firmware/cyd_controller/test/test_native_domain/test_main.cpp
- firmware/cyd_controller/test/adversarial_stress_test.cpp (remove from test/ to fix linker collision)
- firmware/cyd_controller/test/adversarial_stress_test.exe (remove)

MANDATORY INTEGRITY WARNING:
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A teamwork_preview_auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

IMPLEMENTATION TASKS:
1. In `firmware/cyd_controller/lib/aquarium_domain/include/aquarium_automation.h`:
   - Refactor `RuntimeLimiter` to use an explicit `bool running;` field so `now_ms == 0U` does not trigger an underflow trip (per Explorer 1 §4).
   - Add inline `is_lock_deadlocked(now_ms, success_ms, fail_ms, threshold_ms)` helper using signed circular difference (per Explorer 3 §4).
2. In `firmware/cyd_controller/lib/aquarium_domain/src/aquarium_schedule.cpp`:
   - In `FeedingTriggerLatch::evaluate()`, preserve `last_fed_day_ == day_key` across the entire calendar day when `day_key != 0` (do NOT reset `last_fed_minute_ = -1` when minute advances) (per Explorer 2 §4).
3. In `firmware/cyd_controller/src/gui_app.cpp`:
   - Refactor `GuiMutexGuard` (lines 79-90) constructor to `locked_(gui_app_lock(timeout_ms))` and destructor to `if (locked_) gui_app_unlock();` (per Explorer 1 & 3 §4).
   - Update Web Factory Reset (line 8703) and BLE Factory Reset (line 17726) to call `ato_safety_limiter.clear_latch()`, `co2_safety_limiter.clear_latch()`, `co2_limit_latched = false;`, and `gui_reset_feeding_latches()`.
   - Update Web `save_water` (line 8759) and BLE `save_water` (line 17574) to call `ato_safety_limiter.clear_latch()`.
   - Update Web/BLE Timed Override handlers to clear limiter latches when toggling off ATO or CO2.
   - In `gui_app_lock_responsive()` (lines 14872-14909): exempt `RuntimeSafetyTask::Supervisor` (`if (task == RuntimeSafetyTask::Supervisor) return true;`) and use `aquarium::is_lock_deadlocked()` for UI and IO tasks.
   - In `gui_update_metrics()`:
     - Clear ATO limiter latch if `water_level_high` or if `(!ato_timeout_latched && ato_safety_limiter.limit_latched)`.
     - Reset CO2 latch when `!desired_co2`: `co2_safety_limiter.clear_latch(); co2_limit_latched = false; co2_started_ms = 0U;`.
     - Replace local static feeding variables with `feeding_latch_slot1` and `feeding_latch_slot2` using `today_key`.
4. Test Suite Restoration & Expansion:
   - Clean up `firmware/cyd_controller/test/adversarial_stress_test.cpp` and `.exe` (remove them from `test/` so they do not collide with `test_native_domain/test_main.cpp`).
   - In `firmware/cyd_controller/test/test_native_domain/test_main.cpp`, add all new unit test cases specified in Explorer 1, 2, and 3 reports (`test_runtime_limiter_boot_at_millis_zero`, `test_runtime_limiter_millis_wrap_around`, `test_co2_limiter_clears_latch_when_demand_drops`, `test_feeding_schedule_latch_retains_day_latch_after_minute_advances_and_ntp_jump`, `test_feeding_schedule_multi_slot_independence_and_reset`, `test_feeding_schedule_day_key_zero_minute_window_fallback`, `test_lock_deadlock_detection_and_circular_millis_wrap`).
5. Run Verifications:
   - `pio test -e native` (in `firmware/cyd_controller`) -> all unit tests must pass cleanly.
   - `pio run -e esp32dev` (in `firmware/cyd_controller`) -> must compile cleanly with 0 errors.
   - `pio run -e esp32dev-espnow` (in `firmware/cyd_controller`) -> must compile cleanly with 0 errors.
6. Write `changes.md` and `handoff.md` in your working directory and notify the parent orchestrator with send_message.
