# Handoff Report — Explorer 3: Concurrency, Lock Metrics & Supervisor Deadlock Exemption

**Agent**: Explorer 3 (Milestone 1 Gate Resolution)  
**Recipient**: Parent Orchestrator (`d608c00a-48aa-4e84-ad45-bc28b06cef03`)  
**Workspace**: `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium`  
**Working Directory**: `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\teamwork_preview_explorer_m1_fix_3`  
**Date**: 2026-09-04  
**Type**: Hard Handoff (Investigation & Remediation Plan Complete)  

---

## 1. Observation

Direct observations from codebase inspection, compilation checks, and test runner execution:

### Observation 1: `GuiMutexGuard` Bypasses Lock Metrics and Contention Tracking
- **File**: `firmware/cyd_controller/src/gui_app.cpp`, lines 79-90:
  ```cpp
  class GuiMutexGuard {
  public:
      explicit GuiMutexGuard(uint32_t timeout_ms)
          : locked_(gui_mutex != nullptr &&
                    xSemaphoreTakeRecursive(gui_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
      }

      ~GuiMutexGuard() {
          if (locked_) {
              xSemaphoreGiveRecursive(gui_mutex);
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
- **Context**: `gui_app_lock(uint32_t timeout_ms)` and `gui_app_unlock(void)` are defined at lines 14827–14870 and declared in `firmware/cyd_controller/include/gui_app.h:16-19`.
- **Call Sites**: There are exactly 18 occurrences of `GuiMutexGuard` across `firmware/cyd_controller/src/gui_app.cpp`:
  1. Line 81: `GuiMutexGuard(uint32_t timeout_ms)` (class constructor)
  2. Line 14916: `gui_app_init()` (`GuiMutexGuard guard(2000U);`)
  3. Line 14995: `gui_app_service_background()` (`GuiMutexGuard guard(50U);`)
  4. Line 15048: `gui_app_handle_ota_portal()` (`GuiMutexGuard guard(50U);`)
  5. Line 16193: `gui_app_update_wifi()` (`GuiMutexGuard guard(20U);`)
  6. Line 16198: `gui_app_update_ble_pairing()` (`GuiMutexGuard guard(20U);`)
  7. Line 16203: `gui_app_update_espnow()` (`GuiMutexGuard guard(50U);`)
  8. Line 16212: `gui_app_update_ble_connection()` (`GuiMutexGuard guard(50U);`)
  9. Line 16261: `gui_app_publish_log()` (`GuiMutexGuard guard(200U);`)
  10. Line 16349: `handle_web_json_status()` (`GuiMutexGuard guard(500U);`)
  11. Line 16400: `handle_web_json_settings()` (`GuiMutexGuard guard(500U);`)
  12. Line 16642: `handle_web_settings_post()` (`GuiMutexGuard guard(500U);`)
  13. Line 17030: `handle_web_factory_reset()` (`GuiMutexGuard guard(500U);`)
  14. Line 17060: `handle_web_reboot()` (`GuiMutexGuard guard(500U);`)
  15. Line 17408: `ble_command_processor()` (`GuiMutexGuard guard(1000U);`)
  16. Line 17745: `ble_command_status()` (`GuiMutexGuard guard(500U);`)
  17. Line 18243: `handle_web_manual_override()` (`GuiMutexGuard guard(1000U);`)
  18. Line 18343: `handle_web_dosing_override()` (`GuiMutexGuard guard(1000U);`)
- None of these 17 call sites invoke `gui_app_lock()` or `gui_app_unlock()`. Consequently:
  - `lock_acquired_ms` is never set.
  - `lock_nesting_count` is never tracked.
  - `last_io_lock_success_ms` / `last_io_lock_fail_ms` and `last_ui_lock_success_ms` / `last_ui_lock_fail_ms` are never updated on lock acquisition or timeout.
  - In `io_task` (running `gui_app_service_background()`), any lock timeout or contention is completely invisible to the lock monitoring subsystem.

---

### Observation 2: Hardware TWDT Watchdog Suppression in Deadlock Detection
- **File**: `firmware/cyd_controller/src/gui_app.cpp`, lines 14872-14894:
  ```cpp
  bool gui_app_lock_responsive(RuntimeSafetyTask task, uint32_t now_ms) {
      if (now_ms < 15000U) {
          return true;
      }
      uint32_t held_since = 0U;
      ...
      constexpr uint32_t DEADLOCK_THRESHOLD_MS = 2500U;

      if (held_since != 0U &&
          static_cast<uint32_t>(now_ms - held_since) > DEADLOCK_THRESHOLD_MS) {
          return false;
      }
      ...
  ```
- **File**: `firmware/cyd_controller/src/runtime_safety.cpp`, lines 337-360:
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
      if (free_heap_bytes < current_minimum_free_heap) {
          current_minimum_free_heap = free_heap_bytes;
      }
      reset_watchdog = watchdog_registered[task_index(task)];
      portEXIT_CRITICAL(&safety_mux);
      if (reset_watchdog) {
          esp_task_wdt_reset();
      }
  }
  ```
- `RuntimeSafetyTask::Supervisor` is registered with the ESP-IDF Task Watchdog Timer (`esp_task_wdt_add(nullptr)` in `runtime_safety_register_current_task()`, line 318).
- The safety supervisor task (`supervisor_task()` in `runtime_safety.cpp:208-254`) never acquires or contends for `gui_mutex`.
- However, if `held_since != 0U` and `(now_ms - held_since) > 2500U`, `gui_app_lock_responsive(RuntimeSafetyTask::Supervisor, now_ms)` returns `false`.
- This causes `runtime_safety_heartbeat(RuntimeSafetyTask::Supervisor, ...)` to return early at line 347, suppressing `esp_task_wdt_reset()`.
- If the hardware TWDT reset is suppressed, an uncontrolled hardware watchdog panic can occur before the supervisor can execute its controlled fail-safe restart (`restart_after_fault()`), which latches all MCP23017 relays into their safe OFF state and logs the diagnostic to NVS.

---

### Observation 3: Millis Rollover Bug in Deadlock Detection
- **File**: `firmware/cyd_controller/src/gui_app.cpp`, lines 14895-14907:
  ```cpp
      if (task == RuntimeSafetyTask::Ui) {
          if (ui_success != 0U &&
              static_cast<uint32_t>(now_ms - ui_success) > DEADLOCK_THRESHOLD_MS &&
              ui_fail > ui_success) {
              return false;
          }
      } else if (task == RuntimeSafetyTask::Io) {
          if (io_success != 0U &&
              static_cast<uint32_t>(now_ms - io_success) > DEADLOCK_THRESHOLD_MS &&
              io_fail > io_success) {
              return false;
          }
      }
  ```
- Direct unsigned 32-bit comparison (`ui_fail > ui_success`) fails around `millis()` 32-bit rollover (~49.7 days uptime):
  1. **False Positive Deadlock**: If a failure occurred before rollover (`ui_fail = 0xFFFFFFF0`) and a success occurred after rollover (`ui_success = 0x00000010`), `ui_fail > ui_success` evaluates to `true` (`0xFFFFFFF0 > 0x10`), falsely declaring deadlock even though the task successfully acquired the lock *after* the failure.
  2. **Missed Deadlock**: If a success occurred before rollover (`ui_success = 0xFFFFFFF0`) and a failure occurred after rollover (`ui_fail = 0x00000010`), `ui_fail > ui_success` evaluates to `false` (`0x10 > 0xFFFFFFF0` is false), completely missing the deadlock condition.
  3. **Zero Failure Vulnerability**: If `ui_fail == 0U` (no lock failures have ever occurred), a naive signed conversion without checking `ui_fail != 0U` when `ui_success >= 0x80000000` evaluates `static_cast<int32_t>(0 - ui_success) > 0` to true, falsely tripping deadlock detection.

---

### Observation 4: Residual Test Collision in `firmware/cyd_controller/test/`
- Running `pio test -e native`:
  ```
  .pio\build\native\test\test_native_domain\test_main.o:test_main.cpp:(.text+0x6a87): multiple definition of `main'
  .pio\build\native\test\adversarial_stress_test.o:adversarial_stress_test.cpp:(.text+0x1d0e): first defined here
  collect2.exe: error: ld returned 1 exit status
  ```
- Reviewer 2 created `firmware/cyd_controller/test/adversarial_stress_test.cpp` containing an `int main()` directly under `test/`. PlatformIO compiles all source files in `test/`, conflicting with `test/test_native_domain/test_main.cpp`.

---

## 2. Logic Chain

1. **Lock Metrics & Contention Visibility (Observation 1)**:
   - `gui_app_lock()` maintains `lock_nesting_count`, `lock_acquired_ms`, `last_ui_lock_success_ms`, `last_ui_lock_fail_ms`, `last_io_lock_success_ms`, and `last_io_lock_fail_ms` under `portENTER_CRITICAL(&lock_tracker_mux)`.
   - `GuiMutexGuard` is the RAII guard used exclusively throughout `gui_app.cpp` (all 18 call sites).
   - Because `GuiMutexGuard` directly invoked `xSemaphoreTakeRecursive(gui_mutex, ...)` and `xSemaphoreGiveRecursive(gui_mutex)`, none of those 18 call sites ever updated the lock tracking variables.
   - Specifically, `io_task` calls `gui_app_service_background()`, which wraps all its work in `GuiMutexGuard guard(50U)`. Any contention or lock timeout in `gui_app_service_background()` was completely invisible to `gui_app_lock_responsive(RuntimeSafetyTask::Io, ...)`.
   - Modifying `GuiMutexGuard` constructor to `: locked_(gui_app_lock(timeout_ms))` and destructor to `if (locked_) gui_app_unlock();` routes 100% of mutex usage in `gui_app.cpp` through the central tracker without altering any call-site API.

2. **Supervisor Watchdog Exemption (Observation 2)**:
   - `RuntimeSafetyTask::Supervisor` is an independent watchdog monitor that executes on Core 1 every `SUPERVISOR_PERIOD_MS` (250ms).
   - The supervisor task never takes `gui_mutex`; it only inspects `heartbeat_ms` of UI and IO tasks.
   - When a mutex deadlock occurs (e.g. `held_since` exceeds 2500ms), UI and IO tasks should be identified as unresponsive so their heartbeats are suppressed in `runtime_safety_heartbeat()`.
   - When their heartbeats become stale (> 4000ms), the supervisor triggers `restart_after_fault()`, which safely de-energizes all relays (`hal_mcp_latch_all_relays_safe()`), writes the fault reason (`UiHeartbeatStale` or `IoHeartbeatStale`) to NVS, and reboots.
   - If `gui_app_lock_responsive()` does not exempt `RuntimeSafetyTask::Supervisor`, the supervisor's own call to `runtime_safety_heartbeat()` is aborted, preventing `esp_task_wdt_reset()`. This risks an ungraceful hardware TWDT panic before the controlled fail-safe shutdown can execute.
   - Adding `if (task == RuntimeSafetyTask::Supervisor) return true;` at the entry of `gui_app_lock_responsive()` eliminates this hazard completely.

3. **Circular Arithmetic for Millis Rollover (Observation 3)**:
   - In modular arithmetic (modulo $2^{32}$), the signed difference between two timestamps $t_1 - t_2$ cast to `int32_t` correctly determines whether $t_1$ occurred after $t_2$ for any interval up to $2^{31} - 1$ milliseconds (~24.8 days).
   - Evaluating `ui_fail != 0U && static_cast<int32_t>(ui_fail - ui_success) > 0` guarantees that:
     1. Uninitialized failure timestamps (`ui_fail == 0U`) never trigger a false deadlock.
     2. If a success occurred after a failure across the rollover boundary ($0xFFFFFFFF \to 0x00000000$), the signed difference is negative ($\le 0$), correctly recognizing recovery.
     3. If a failure occurred after a success across the rollover boundary, the signed difference is positive ($> 0$), correctly recognizing deadlock.
   - Encapsulating this comparison in a pure domain helper `aquarium::is_lock_deadlocked(now_ms, success_ms, fail_ms, threshold_ms)` allows exhaustive unit testing without FreeRTOS/Arduino dependencies.

---

## 3. Caveats

- **No Caveats on Core Logic**: The 3 investigated issues are clear, isolated, and verified with exact line numbers and code paths.
- **Flash Utilization**: Flash memory utilization on `esp32dev` is at 96.2% (1,890,741 / 1,966,080 bytes). Replacing inlined semaphore calls in `GuiMutexGuard` with function calls to `gui_app_lock()` / `gui_app_unlock()` reduces duplicated code and preserves flash headroom.
- **Test File Collision**: `firmware/cyd_controller/test/adversarial_stress_test.cpp` and `adversarial_stress_test.exe` were left by Reviewer 2 in `test/`. These must be removed or moved to the reviewer folder before `pio test -e native` can execute.

---

## 4. Conclusion & Remediation Plan for Worker

### Plan Overview:
1. **Refactor `GuiMutexGuard`** in `firmware/cyd_controller/src/gui_app.cpp:79-90` to use `gui_app_lock(timeout_ms)` and `gui_app_unlock()`.
2. **Add Supervisor Exemption and Circular Millis Difference** in `gui_app_lock_responsive()` (`firmware/cyd_controller/src/gui_app.cpp:14872-14909`).
3. **Add `is_lock_deadlocked()` helper** to `firmware/cyd_controller/lib/aquarium_domain/include/aquarium_automation.h` to cleanly isolate circular difference logic.
4. **Expand Native Unit Tests** in `firmware/cyd_controller/test/test_native_domain/test_main.cpp` covering normal deadlock, recovery, wrap-around across $0xFFFFFFFF$, and zero-fail guardrails.
5. **Clean up Test Collision**: Move or remove `adversarial_stress_test.cpp` from `firmware/cyd_controller/test/`.

---

### Detailed Code Modifications:

#### 1. `firmware/cyd_controller/src/gui_app.cpp` (Lines 79-90)
**Replace**:
```cpp
class GuiMutexGuard {
public:
    explicit GuiMutexGuard(uint32_t timeout_ms)
        : locked_(gui_mutex != nullptr &&
                  xSemaphoreTakeRecursive(gui_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
    }

    ~GuiMutexGuard() {
        if (locked_) {
            xSemaphoreGiveRecursive(gui_mutex);
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
**With**:
```cpp
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

---

#### 2. `firmware/cyd_controller/lib/aquarium_domain/include/aquarium_automation.h` (Lines 118-120)
**Add Helper Function**:
```cpp
inline bool is_lock_deadlocked(uint32_t now_ms, uint32_t success_ms, uint32_t fail_ms, uint32_t threshold_ms) {
    if (success_ms == 0U || fail_ms == 0U) {
        return false;
    }
    if (static_cast<uint32_t>(now_ms - success_ms) <= threshold_ms) {
        return false;
    }
    return static_cast<int32_t>(fail_ms - success_ms) > 0;
}
```

---

#### 3. `firmware/cyd_controller/src/gui_app.cpp` (Lines 14872-14909)
**Refactor `gui_app_lock_responsive`**:
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
        if (aquarium::is_lock_deadlocked(now_ms, ui_success, ui_fail, DEADLOCK_THRESHOLD_MS)) {
            return false;
        }
    } else if (task == RuntimeSafetyTask::Io) {
        if (aquarium::is_lock_deadlocked(now_ms, io_success, io_fail, DEADLOCK_THRESHOLD_MS)) {
            return false;
        }
    }
    return true;
}
```

---

#### 4. `firmware/cyd_controller/test/test_native_domain/test_main.cpp`
**Add New Unit Test**:
```cpp
static void test_lock_deadlock_detection_and_circular_millis_wrap() {
    constexpr uint32_t THRESHOLD = 2500U;

    // 1. Initial / boot state: no failures
    TEST_ASSERT_FALSE(aquarium::is_lock_deadlocked(5000U, 1000U, 0U, THRESHOLD));
    // 2. Initial state: no successes yet
    TEST_ASSERT_FALSE(aquarium::is_lock_deadlocked(5000U, 0U, 1000U, THRESHOLD));

    // 3. Normal active lock: success is recent (within threshold)
    TEST_ASSERT_FALSE(aquarium::is_lock_deadlocked(3000U, 2000U, 1500U, THRESHOLD));

    // 4. Normal active lock: failed recently, but succeeded afterward
    TEST_ASSERT_FALSE(aquarium::is_lock_deadlocked(6000U, 3000U, 2500U, THRESHOLD));

    // 5. Deadlock condition: elapsed > threshold and failure occurred AFTER last success
    TEST_ASSERT_TRUE(aquarium::is_lock_deadlocked(6000U, 3000U, 3500U, THRESHOLD));

    // 6. Wrap-around across 0xFFFFFFFF boundary: failure after success
    const uint32_t pre_wrap_success = 0xFFFFFFF0U;
    const uint32_t post_wrap_fail = 0x00000010U; // 32ms after success
    const uint32_t now_ms = 0x00000AF0U;        // 2816ms after success
    TEST_ASSERT_TRUE(aquarium::is_lock_deadlocked(now_ms, pre_wrap_success, post_wrap_fail, THRESHOLD));

    // 7. Wrap-around across 0xFFFFFFFF boundary: recovered! Success after failure
    const uint32_t pre_wrap_fail = 0xFFFFFFF0U;
    const uint32_t post_wrap_success = 0x00000010U;
    TEST_ASSERT_FALSE(aquarium::is_lock_deadlocked(now_ms, post_wrap_success, pre_wrap_fail, THRESHOLD));

    // 8. High uptime (> 2^31 ms, ~25 days) with zero failures: never deadlocked
    const uint32_t high_uptime_success = 0x90000000U;
    TEST_ASSERT_FALSE(aquarium::is_lock_deadlocked(0x90001000U, high_uptime_success, 0U, THRESHOLD));
}
```
And register with `RUN_TEST(test_lock_deadlock_detection_and_circular_millis_wrap);` in `main()`.

---

#### 5. Remove `adversarial_stress_test.cpp` and `.exe` from `firmware/cyd_controller/test/`
Move or remove `firmware/cyd_controller/test/adversarial_stress_test.cpp` and `firmware/cyd_controller/test/adversarial_stress_test.exe` so `pio test -e native` links `test_native_domain/test_main.cpp` cleanly.

---

## 5. Verification Method

To independently verify the implementation after Worker applies the changes:

1. **Native Unit Tests**:
   ```powershell
   cd firmware\cyd_controller
   pio test -e native
   ```
   *Expected Result*: 44/44 test cases pass cleanly (including `test_lock_deadlock_detection_and_circular_millis_wrap`).

2. **ESP32 Firmware Compilation**:
   ```powershell
   cd firmware\cyd_controller
   pio run -e esp32dev
   ```
   *Expected Result*: Build completes with SUCCESS (code 0) in under 45s. Flash utilization remains comfortably under 1,966,080 bytes.

3. **Static Audit**:
   - Verify that in `gui_app.cpp:79-90`, `GuiMutexGuard` contains NO calls to FreeRTOS primitives (`xSemaphoreTakeRecursive` / `xSemaphoreGiveRecursive`) and routes exclusively through `gui_app_lock` / `gui_app_unlock`.
   - Verify that `gui_app_lock_responsive()` immediately returns `true` when `task == RuntimeSafetyTask::Supervisor`.
   - Verify that all deadlock evaluation uses `aquarium::is_lock_deadlocked()`.
