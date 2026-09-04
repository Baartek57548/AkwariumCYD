# Changes Summary — Milestone 1 (R1: Firmware Logic & Stability)

**Worker**: Worker 1 (Firmware Logic & Stability Worker)  
**Date**: 2026-09-04  
**Milestone**: M1 (R1: Firmware Logic & Stability)  

---

## 1. Files Modified

1. `firmware/cyd_controller/lib/aquarium_domain/include/aquarium_automation.h`
   - Added `struct RuntimeLimiter`: encapsulation for duration limiting with `started_ms`, `limit_latched`, `update()`, `reset()`, and `clear_latch()`.
   - Thread-safe and millis-wrap-safe runtime tracker for actuators (ATO, CO2) enforcing maximum runtime cutoffs and transition alarms.

2. `firmware/cyd_controller/lib/aquarium_domain/include/aquarium_schedule.h`
   - Added declaration of `class FeedingTriggerLatch` with minute-based edge latching and multi-day awareness.
   - Updated `feeding_due` signature documentation.

3. `firmware/cyd_controller/lib/aquarium_domain/src/aquarium_schedule.cpp`
   - Updated `feeding_due()` to evaluate `second < 60U` instead of fragile exact-match `second == 0U`.
   - Implemented `FeedingTriggerLatch`: tracks `last_fed_minute_` and `last_fed_day_`, preventing duplicate feeding executions within the same minute while guaranteeing execution even when FreeRTOS tick jitter or lock latency skips second 0.

4. `firmware/cyd_controller/src/hal_adc.cpp`
   - Refactored `hal_adc_read_raw()`: released `hal_i2c_bus_lock` before entering the `vTaskDelay(pdMS_TO_TICKS(1U))` polling wait loop.
   - `hal_i2c_bus_lock` is now held strictly during register writes and brief register reads (microseconds), completely eliminating bus starvation for MCP23017 relays and digital inputs during ADS1115 conversions.

5. `firmware/cyd_controller/src/runtime_safety.cpp`
   - Added `extern bool gui_app_lock_responsive(RuntimeSafetyTask task, uint32_t now_ms);`.
   - In `runtime_safety_heartbeat()`, added deadlock detection: if a task is blocked on `gui_mutex` for longer than the allowable threshold (2500ms), heartbeat registration is suppressed, allowing `safety_supervisor` to detect the deadlock and safely reboot into safe relay states.
   - Added `runtime_safety_prepare_sleep()` and `runtime_safety_post_sleep()`: resets task watchdog and updates all active task heartbeat timestamps to current `millis()`, preventing false 4-second watchdog timeouts upon waking from light sleep.

6. `firmware/cyd_controller/src/runtime_controller.cpp`
   - In `io_task`: reordered execution so `gui_app_service_background()` runs *before* `runtime_safety_heartbeat()`. If `gui_mutex` is contested/deadlocked, background service lock acquisition fails and `runtime_safety_heartbeat()` properly detects unresponsiveness.

7. `firmware/cyd_controller/src/gui_app.cpp`
   - Declared global externs `runtime_safety_prepare_sleep()` and `runtime_safety_post_sleep()`.
   - Added `co2_safety_limiter` and `ato_safety_limiter` of type `aquarium::RuntimeLimiter`.
   - Added `co2_started_ms` and `co2_limit_latched` tracking at file scope.
   - Fixed `light_sleep_authorized()`: replaced `digitalWrite(HwConfig::Backlight::PIN, LOW/HIGH)` with `hal_display_set_brightness(0U)` and `hal_display_set_brightness(display_max_brightness)` to preserve LEDC PWM attachment on GPIO 21; added `runtime_safety_prepare_sleep()` and `runtime_safety_post_sleep()` guards.
   - Added lock metrics tracking in `gui_app_lock` and `gui_app_unlock` (`last_ui_lock_success_ms`, `last_ui_lock_fail_ms`, `last_io_lock_success_ms`, `last_io_lock_fail_ms`, `lock_acquired_ms`, `lock_nesting_count`, protected by spinlock).
   - Implemented `gui_app_lock_responsive(RuntimeSafetyTask task, uint32_t now_ms)` with 2500ms deadlock threshold.
   - In `gui_update_metrics()`:
     - Resolved candidates `desired_co2` and `desired_water_fill` via `control_modes.resolve()` first.
     - Applied `co2_safety_limiter.update()` and `ato_safety_limiter.update()` on the resolved states.
     - Enforced `co2_max_limit_ms` and `ato_max_limit_ms` strictly regardless of whether the demand originated from automated schedules or manual override modes.
     - Prevented manual override from resetting start timers to 0.
     - When limits are exceeded, forced actuators OFF, latched limit states, and triggered error log messages.
     - Synchronized `ato_started_ms`, `ato_timeout_latched`, `co2_started_ms`, `co2_limit_latched`.
   - In scheduled feeding trigger: replaced `sc == 0` with minute and day edge latching (`last_fed_minute != current_minute || last_fed_day != today_key`), ensuring jitter tolerance across tick drifts.

8. `firmware/cyd_controller/test/test_native_domain/test_main.cpp`
   - Added `test_ato_runtime_safety_limit_enforced_during_manual_override`: validates that manual overrides cannot bypass ATO safety runtime limit, verifies start timer is not reset, verifies actuator is forced OFF upon limit exceeded, and checks latch clearing.
   - Added `test_co2_runtime_safety_limit_enforced_during_manual_override`: validates that manual overrides cannot bypass CO2 duration limit, verifies actuator is forced OFF upon reaching 540 min limit.
   - Added `test_feeding_schedule_trigger_succeeds_under_simulated_tick_jitter`: validates `feeding_due` and `FeedingTriggerLatch` trigger successfully when second 0 is skipped and evaluation occurs at second 1, prevents duplicate feeding within the same minute, and triggers properly on the next day.
   - Total native test count expanded from 40 to 43.
