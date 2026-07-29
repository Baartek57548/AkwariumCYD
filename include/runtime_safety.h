#ifndef AQUARIUM_RUNTIME_SAFETY_H
#define AQUARIUM_RUNTIME_SAFETY_H

#include <Arduino.h>

enum class RuntimeSafetyTask : uint8_t {
    Ui = 0U,
    Io = 1U,
    Supervisor = 2U,
    Count = 3U
};

enum class RuntimeFaultReason : uint8_t {
    None = 0U,
    UiHeartbeatStale = 1U,
    IoHeartbeatStale = 2U,
    TaskWatchdogReset = 3U,
    PanicReset = 4U,
    GuiInitializationFailed = 5U,
    IoTaskStartFailed = 6U,
    ManualRestart = 7U,
    FactoryReset = 8U,
    OtaUpdate = 9U
};

struct RuntimeResetDiagnostic {
    uint32_t boot_id;
    uint32_t fault_uptime_ms;
    uint32_t minimum_free_heap;
    uint8_t reset_reason;
    RuntimeFaultReason fault_reason;
    bool fail_safe_confirmed;
};

struct RuntimeSafetyStatus {
    uint32_t boot_id;
    uint32_t boot_count;
    uint32_t fault_count;
    uint32_t current_minimum_free_heap;
    RuntimeResetDiagnostic last_reset;
};

bool runtime_safety_initialize(void);
bool runtime_safety_register_current_task(RuntimeSafetyTask task);
void runtime_safety_heartbeat(RuntimeSafetyTask task,
                              uint32_t now_ms,
                              uint32_t free_heap_bytes);
bool runtime_safety_start_supervisor(void);

/**
 * Persists the reason, forces all relays to their configured safe state and
 * restarts. This function does not return.
 */
[[noreturn]] void runtime_safety_fail_and_restart(
    RuntimeFaultReason reason);

/** Records an intentional restart so it is distinguishable from a crash. */
void runtime_safety_record_restart(RuntimeFaultReason reason,
                                   bool fail_safe_confirmed);

RuntimeSafetyStatus runtime_safety_status(void);
const char *runtime_fault_reason_code(RuntimeFaultReason reason);

#endif // AQUARIUM_RUNTIME_SAFETY_H
