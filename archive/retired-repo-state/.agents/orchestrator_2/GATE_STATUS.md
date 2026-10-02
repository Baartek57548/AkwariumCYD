# Gate Status — Milestone 1 (Iteration 1)

## Gate — Iteration 1
| Agent | Role | Verdict | Source | Notes |
|-------|------|---------|--------|-------|
| worker_m1 | teamwork_preview_worker | DONE | handoff.md | 43/43 native tests pass, esp32dev build passes |
| reviewer_m1_1 | teamwork_preview_reviewer | REQUEST_CHANGES | handoff.md | CO2 permanent lockout, ATO reset desync, GuiMutexGuard tracking bypass |
| reviewer_m1_2 | teamwork_preview_reviewer | REQUEST_CHANGES | handoff.md | CO2 permanent lockout, ATO reset desync, FeedingTriggerLatch dead code & day latch bug |
| challenger_m1_1 | teamwork_preview_challenger | INCOMPLETE | progress.md | Interrupted by predecessor succession |
| challenger_m1_2 | teamwork_preview_challenger | APPROVE | handoff.md | ADS1115 non-blocking, safe sleep, deadlock threshold verified |
| auditor_m1 | teamwork_preview_auditor | CLEAN | handoff.md | Authentic logic, no facades, no hardcoded tests |

Gate Result: **FAIL** (reviewer_m1_1 and reviewer_m1_2 REQUEST_CHANGES)

### Action Items for Iteration 2:
1. Fix CO2 safety limiter unlatching logic in `gui_app.cpp:15511`: ensure `clear_latch()` and `started_ms = 0` are executed whenever `!desired_co2`.
2. Fix ATO safety limiter synchronization across all reset and config save endpoints (`save_water`, factory reset in Web and BLE): call `ato_safety_limiter.clear_latch()`.
3. Integrate `FeedingTriggerLatch` into `gui_app.cpp` and fix `FeedingTriggerLatch::evaluate()` so advancing minutes does not wipe the day latch (`last_fed_day_`). Add native unit tests for day-latching.
4. Route `GuiMutexGuard` through `gui_app_lock()` / `gui_app_unlock()`.
5. Exempt `RuntimeSafetyTask::Supervisor` from lock responsiveness lockout in `gui_app_lock_responsive()`, and use circular signed differences for rollover safety.
