## 2026-09-04T10:12:27Z
You are Worker 1 (Firmware Logic & Stability Worker).
Your working directory is: c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\teamwork_preview_worker_m1
The workspace root is: c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium
The authoritative user request is in: c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\ORIGINAL_REQUEST.md
The project master scope is in: c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\PROJECT.md

MANDATORY INTEGRITY WARNING:
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A teamwork_preview_auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

You MUST read ORIGINAL_REQUEST.md, PROJECT.md, and Explorer 1's detailed reports before starting:
- Explorer 1 Analysis: c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\teamwork_preview_explorer_survey_1\analysis.md
- Explorer 1 Handoff: c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\teamwork_preview_explorer_survey_1\handoff.md

Your Milestone 1 Assignment (R1: Firmware Logic & Stability):
You have exclusive write ownership over:
- `firmware/cyd_controller/src/gui_app.cpp`
- `firmware/cyd_controller/src/runtime_controller.cpp`
- `firmware/cyd_controller/src/runtime_safety.cpp`
- `firmware/cyd_controller/src/hal_adc.cpp`
- `firmware/cyd_controller/lib/aquarium_schedule/`
- `firmware/cyd_controller/lib/aquarium_automation/`
- `firmware/cyd_controller/test/test_native_domain/`

Required Fixes:
1. **Manual Override Safety Limits**:
   In `src/gui_app.cpp` (around lines 15366-15457), ensure ATO and CO2 safety runtime duration limiters (`ato_max_limit_ms`, `co2_max_limit_ms`) are strictly enforced even when manual override mode is engaged. Do not let manual override continuously reset start timers to 0. When runtime exceeds limit, force the actuator OFF and trigger an alarm/error state.
2. **Resilient Feeding Schedule Trigger**:
   In `src/gui_app.cpp:15583` and `lib/aquarium_schedule/`:
   Replace fragile exact-second check (`now.second == 0`) with a minute-based trigger latch (e.g. tracking `last_fed_minute` / `last_fed_day`) so that FreeRTOS tick jitter or lock latency cannot cause scheduled feedings to be missed.
3. **Supervisor Deadlock Detection**:
   In `src/runtime_controller.cpp:234` and `src/main.cpp:281`:
   Ensure task heartbeats (`runtime_safety_heartbeat`) accurately reflect task health and lock responsiveness. If a task is blocked trying to acquire `gui_mutex` for longer than the allowable threshold, it must NOT unconditionally report healthy heartbeats, allowing the safety supervisor to detect deadlocks.
4. **Light Sleep Safety**:
   In `src/gui_app.cpp:5731-5743`:
   Fix the light sleep routine so that entering sleep either appropriately suspends/feeds the supervisor watchdog or ensures wakeup does not trigger an immediate 4-second timeout watchdog crash. Resolve any GPIO conflict with PWM backlight on pin 21.
5. **ADS1115 Non-Blocking I2C Bus Conversion**:
   In `src/hal_adc.cpp:166-203`:
   Do not hold `hal_i2c_bus_lock` across the FreeRTOS context-switching wait loop while waiting for ADS1115 conversion to finish (or release the lock during delay, or use one-shot without locking out other I2C peripherals).
6. **Native Domain Unit Tests Expansion**:
   In `firmware/cyd_controller/test/test_native_domain/`:
   Add unit tests verifying:
   - ATO and CO2 runtime safety limits properly cut off actuators during manual overrides.
   - Feeding schedule trigger succeeds under simulated tick jitter (e.g., when second 0 is skipped and evaluation occurs at second 1).
   - Ensure all existing 40 tests + new tests pass (`pio test -e native`).

Verification Requirements:
1. Run `pio test -e native` inside `firmware/cyd_controller` — all tests MUST pass.
2. Run `pio run -e esp32dev` inside `firmware/cyd_controller` — compilation MUST succeed cleanly.
3. Document all changes in `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\teamwork_preview_worker_m1\changes.md` and provide full handoff in `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\teamwork_preview_worker_m1\handoff.md`.
4. Notify orchestrator via send_message when done.
