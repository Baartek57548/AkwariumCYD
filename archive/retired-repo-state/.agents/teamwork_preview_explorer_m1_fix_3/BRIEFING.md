# BRIEFING — 2026-09-04T10:47:00Z

## Mission
Investigate concurrency, lock metrics, and supervisor deadlock exemption issues for Milestone 1 Gate Resolution, and formulate a concrete remediation plan.

## 🔒 My Identity
- Archetype: explorer
- Roles: read-only investigation, synthesis, structured reporting
- Working directory: c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\teamwork_preview_explorer_m1_fix_3
- Original parent: d608c00a-48aa-4e84-ad45-bc28b06cef03
- Milestone: Milestone 1 Gate Resolution (Concurrency, Lock Metrics & Supervisor Deadlock Exemption)

## 🔒 Key Constraints
- Read-only investigation — do NOT implement or modify source files
- Strictly respect File Workspace Convention (write only to .agents/teamwork_preview_explorer_m1_fix_3/)
- Focus on GuiMutexGuard lock metrics bypass, supervisor deadlock exemption in gui_app_lock_responsive(), and millis rollover comparison

## Current Parent
- Conversation ID: d608c00a-48aa-4e84-ad45-bc28b06cef03
- Updated: 2026-09-04T10:47:00Z

## Investigation State
- **Explored paths**:
  - `firmware/cyd_controller/src/gui_app.cpp` (lines 75-101, 14810-14930, 14990-15050, 16190-18350)
  - `firmware/cyd_controller/include/gui_app.h`
  - `firmware/cyd_controller/src/runtime_safety.cpp` & `include/runtime_safety.h`
  - `firmware/cyd_controller/src/runtime_controller.cpp`
  - `firmware/cyd_controller/src/main.cpp`
  - `firmware/cyd_controller/src/remote_alarm_relay.cpp`
  - `firmware/cyd_controller/lib/aquarium_domain/include/aquarium_automation.h`
  - `firmware/cyd_controller/test/test_native_domain/test_main.cpp`
  - `firmware/cyd_controller/test/adversarial_stress_test.cpp`
- **Key findings**:
  1. `GuiMutexGuard` (lines 79-90 of `gui_app.cpp`) calls `xSemaphoreTakeRecursive`/`xSemaphoreGiveRecursive` directly, bypassing `gui_app_lock()` / `gui_app_unlock()`. All 18 call sites across `gui_app.cpp` fail to record `lock_acquired_ms`, `lock_nesting_count`, or core-specific success/fail metrics.
  2. In `gui_app_lock_responsive()`, `held_since != 0U && (now_ms - held_since) > DEADLOCK_THRESHOLD_MS` checks ALL tasks unconditionally, suppressing `runtime_safety_heartbeat()` and TWDT hardware resets (`esp_task_wdt_reset()`) on `RuntimeSafetyTask::Supervisor`. Supervisor never acquires `gui_mutex` and must be exempted immediately via `if (task == RuntimeSafetyTask::Supervisor) return true;`.
  3. `ui_fail > ui_success` in `gui_app_lock_responsive()` is vulnerable to 32-bit `millis()` rollover, causing false deadlocks when wrap occurs between fail and success. Must use signed circular difference: `ui_fail != 0U && static_cast<int32_t>(ui_fail - ui_success) > 0`.
  4. PlatformIO native test suite collision: `firmware/cyd_controller/test/adversarial_stress_test.cpp` left in `test/` causes duplicate definition of `main` when compiling `pio test -e native`.
- **Unexplored areas**: None within the scope of Concurrency, Lock Metrics & Supervisor Deadlock Exemption.

## Key Decisions Made
- Confirmed `GuiMutexGuard` refactoring directly calls `gui_app_lock(timeout_ms)` and `gui_app_unlock()`.
- Designed `aquarium::is_lock_deadlocked()` helper in `lib/aquarium_domain/include/aquarium_automation.h` to enable 100% unit test coverage of circular arithmetic and rollover in `test_main.cpp`.
- Formulated exact remediation steps and verification procedures for Worker.

## Artifact Index
- DISPATCH.md — Initial dispatch instructions
- progress.md — Liveness and progress tracking
- BRIEFING.md — Situational awareness and working memory
- handoff.md — Final 5-component hard handoff report
