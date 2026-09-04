# BRIEFING — 2026-09-04T12:35:00+02:00

## Mission
Objective and adversarial review of Worker 1's changes for Milestone 1 (R1: Firmware Logic & Stability).

## 🔒 My Identity
- Archetype: reviewer-critic
- Roles: reviewer, critic
- Working directory: c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\teamwork_preview_reviewer_m1_1
- Original parent: 56ceb5af-6a46-4981-bf39-e3e616dc0656
- Milestone: Milestone 1 (R1: Firmware Logic & Stability)
- Instance: 1 of 1

## 🔒 Key Constraints
- Review-only — do NOT modify implementation code
- Write only to your own folder (.agents/teamwork_preview_reviewer_m1_1)
- Verify independently, do not trust unverified claims
- Check for integrity violations (hardcoding, facade, etc.)
- Issue clear verdict: APPROVE or REQUEST_CHANGES

## Current Parent
- Conversation ID: 56ceb5af-6a46-4981-bf39-e3e616dc0656
- Updated: 2026-09-04T12:35:00+02:00

## Review Scope
- **Files to review**:
  - `src/gui_app.cpp`
  - `aquarium_automation.h`
  - `src/aquarium_schedule.cpp`
  - `src/runtime_controller.cpp`
  - `src/runtime_safety.cpp`
  - `src/hal_adc.cpp`
  - `firmware/cyd_controller/test/test_native_domain/test_main.cpp`
  - Worker 1 changes: `.agents/teamwork_preview_worker_m1/changes.md`
  - Worker 1 handoff: `.agents/teamwork_preview_worker_m1/handoff.md`
- **Interface contracts**: `PROJECT.md`, `.agents/ORIGINAL_REQUEST.md`
- **Review criteria**: correctness, safety limits, deadlock detection, sleep watchdog, I2C non-blocking, unit tests, code style, edge cases

## Key Decisions Made
- Executed independent test command: `pio test -e native` (43/43 passed).
- Executed independent target build: `pio run -e esp32dev` (build succeeded, RAM: 36.7%, Flash: 96.2%).
- Uncovered critical defect in CO2 safety limiter causing permanent lockout once tripped.
- Uncovered critical defect in ATO safety limiter desynchronization preventing reset via Web/BLE.
- Uncovered major gap in `GuiMutexGuard` bypassing lock metrics and deadlock tracking.
- Verdict reached: REQUEST_CHANGES.

## Artifact Index
- `DISPATCH.md` — record of incoming dispatch messages
- `BRIEFING.md` — persistent situational awareness
- `progress.md` — liveness heartbeat
- `handoff.md` — final review report and verdict

## Review Checklist
- **Items reviewed**:
  - `firmware/cyd_controller/lib/aquarium_domain/include/aquarium_automation.h` (RuntimeLimiter)
  - `firmware/cyd_controller/lib/aquarium_domain/include/aquarium_schedule.h` (FeedingTriggerLatch)
  - `firmware/cyd_controller/lib/aquarium_domain/src/aquarium_schedule.cpp` (FeedingTriggerLatch & feeding_due)
  - `firmware/cyd_controller/src/gui_app.cpp` (CO2/ATO limiters, feeding trigger, sleep brightness, lock metrics)
  - `firmware/cyd_controller/src/runtime_safety.cpp` (deadlock heartbeat suppression, sleep prepare/post)
  - `firmware/cyd_controller/src/runtime_controller.cpp` (service background ordering)
  - `firmware/cyd_controller/src/hal_adc.cpp` (non-blocking I2C delay)
  - `firmware/cyd_controller/test/test_native_domain/test_main.cpp` (3 new native tests)
- **Verdict**: REQUEST_CHANGES
- **Unverified claims**: none; all claims investigated and independently checked.

## Attack Surface
- **Hypotheses tested**:
  - Hypothesis 1: What happens after CO2 limit is reached and demand turns off? -> Fails: latch is never cleared due to `!co2_limit_latched` guard, leading to permanent lockout.
  - Hypothesis 2: What happens when ATO trips and user refills reservoir & saves settings? -> Fails: `ato_safety_limiter.limit_latched` is not cleared by `ato_timeout_latched = false`, immediately relatching on next tick.
  - Hypothesis 3: Does `gui_app_service_background` record lock failures as claimed? -> Fails: `GuiMutexGuard` bypasses `gui_app_lock()`, calling `xSemaphoreTakeRecursive` directly.
  - Hypothesis 4: Does light sleep trip watchdog on wake? -> Passes: `runtime_safety_post_sleep()` refreshes heartbeat timestamps.
  - Hypothesis 5: Does ADS1115 conversion stall I2C bus? -> Passes: bus unlocked during `vTaskDelay`.
- **Vulnerabilities found**:
  - Finding 1 (Critical): Permanent CO2 lockout in `gui_app.cpp:15511`.
  - Finding 2 (Critical): `ato_safety_limiter` desynchronization in `gui_app.cpp:8703, 8759, 15529, 17574, 17726`.
  - Finding 3 (Major): `GuiMutexGuard` bypasses lock tracking in `gui_app.cpp:79-90`.
  - Finding 4 (Minor): Unsigned comparison across 49.7-day millis rollover in `gui_app.cpp:14896-14906`.
- **Untested angles**: Hardware-in-the-loop physical touch jitter.
