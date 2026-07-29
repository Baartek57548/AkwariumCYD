#include "runtime_safety.h"

#include "hal_mcp23017.h"

#include <Preferences.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <string.h>

namespace {

constexpr char SAFETY_NAMESPACE[] = "aq_runtime";
constexpr char SAFETY_KEY[] = "diag";
constexpr uint32_t SAFETY_MAGIC = 0x32514641UL; // AFQ2
constexpr uint16_t SAFETY_VERSION = 2U;
constexpr uint32_t TASK_WATCHDOG_TIMEOUT_SECONDS = 8U;
constexpr uint32_t SUPERVISOR_PERIOD_MS = 250U;
constexpr uint32_t HEARTBEAT_TIMEOUT_MS = 4000U;
constexpr uint32_t STARTUP_GRACE_MS = 15000U;
constexpr uint32_t SUPERVISOR_STACK_BYTES = 4096U;
constexpr UBaseType_t SUPERVISOR_PRIORITY = 4U;
constexpr uint8_t RESET_HISTORY_CAPACITY = 8U;

struct __attribute__((packed)) PersistentSafetyState {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t boot_count;
    uint32_t fault_count;
    RuntimeFaultReason pending_fault;
    bool pending_fail_safe_confirmed;
    uint16_t pending_reserved;
    uint32_t pending_fault_uptime_ms;
    uint32_t pending_minimum_free_heap;
    uint8_t history_count;
    uint8_t history_next;
    uint16_t history_reserved;
    RuntimeResetDiagnostic history[RESET_HISTORY_CAPACITY];
    uint32_t crc32;
};

portMUX_TYPE safety_mux = portMUX_INITIALIZER_UNLOCKED;
StaticSemaphore_t safety_state_mutex_storage;
SemaphoreHandle_t safety_state_mutex =
    xSemaphoreCreateMutexStatic(
        &safety_state_mutex_storage);
PersistentSafetyState persistent = {};
uint32_t heartbeat_ms[
    static_cast<uint8_t>(RuntimeSafetyTask::Count)] = {};
bool heartbeat_seen[
    static_cast<uint8_t>(RuntimeSafetyTask::Count)] = {};
bool watchdog_registered[
    static_cast<uint8_t>(RuntimeSafetyTask::Count)] = {};
TaskHandle_t supervisor_handle = nullptr;
uint32_t initialized_ms = 0U;
uint32_t current_minimum_free_heap = UINT32_MAX;

bool lock_safety_state(
    TickType_t timeout = pdMS_TO_TICKS(250U)) {
    return safety_state_mutex != nullptr &&
           xSemaphoreTake(
               safety_state_mutex,
               timeout) == pdTRUE;
}

uint32_t crc32_bytes(const void *buffer, size_t length) {
    uint32_t crc = 0xFFFFFFFFUL;
    const uint8_t *bytes = static_cast<const uint8_t *>(buffer);
    for (size_t index = 0U; index < length; ++index) {
        crc ^= bytes[index];
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            crc = (crc & 1U) != 0U
                      ? (crc >> 1U) ^ 0xEDB88320UL
                      : crc >> 1U;
        }
    }
    return ~crc;
}

uint32_t state_crc(const PersistentSafetyState &state) {
    return crc32_bytes(
        &state, sizeof(state) - sizeof(state.crc32));
}

bool persistent_valid(const PersistentSafetyState &state) {
    return state.magic == SAFETY_MAGIC &&
           state.version == SAFETY_VERSION &&
           state.history_count <= RESET_HISTORY_CAPACITY &&
           state.history_next < RESET_HISTORY_CAPACITY &&
           state.crc32 == state_crc(state);
}

bool persist_state_locked() {
    persistent.magic = SAFETY_MAGIC;
    persistent.version = SAFETY_VERSION;
    persistent.crc32 = state_crc(persistent);
    Preferences storage;
    if (!storage.begin(SAFETY_NAMESPACE, false)) {
        return false;
    }
    const bool saved =
        storage.putBytes(SAFETY_KEY, &persistent, sizeof(persistent)) ==
        sizeof(persistent);
    storage.end();
    return saved;
}

RuntimeFaultReason fault_from_reset_reason(esp_reset_reason_t reason) {
    switch (reason) {
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
        return RuntimeFaultReason::TaskWatchdogReset;
    case ESP_RST_PANIC:
        return RuntimeFaultReason::PanicReset;
    default:
        return RuntimeFaultReason::None;
    }
}

void append_boot_diagnostic(esp_reset_reason_t reset_reason) {
    RuntimeResetDiagnostic diagnostic = {};
    diagnostic.boot_id = persistent.boot_count;
    diagnostic.reset_reason = static_cast<uint8_t>(reset_reason);
    diagnostic.fault_reason =
        persistent.pending_fault != RuntimeFaultReason::None
            ? persistent.pending_fault
            : fault_from_reset_reason(reset_reason);
    diagnostic.fault_uptime_ms =
        persistent.pending_fault_uptime_ms;
    diagnostic.minimum_free_heap =
        persistent.pending_minimum_free_heap;
    diagnostic.fail_safe_confirmed =
        persistent.pending_fail_safe_confirmed;

    persistent.history[persistent.history_next] = diagnostic;
    persistent.history_next =
        static_cast<uint8_t>(
            (persistent.history_next + 1U) %
            RESET_HISTORY_CAPACITY);
    if (persistent.history_count < RESET_HISTORY_CAPACITY) {
        ++persistent.history_count;
    }
    if (diagnostic.fault_reason != RuntimeFaultReason::None &&
        diagnostic.fault_reason != RuntimeFaultReason::ManualRestart &&
        diagnostic.fault_reason != RuntimeFaultReason::FactoryReset &&
        diagnostic.fault_reason != RuntimeFaultReason::OtaUpdate) {
        ++persistent.fault_count;
    }
    persistent.pending_fault = RuntimeFaultReason::None;
    persistent.pending_fail_safe_confirmed = false;
    persistent.pending_fault_uptime_ms = 0U;
    persistent.pending_minimum_free_heap = 0U;
}

uint8_t task_index(RuntimeSafetyTask task) {
    return static_cast<uint8_t>(task);
}

bool valid_task(RuntimeSafetyTask task) {
    return task_index(task) <
           static_cast<uint8_t>(RuntimeSafetyTask::Count);
}

bool persist_pending_fault(RuntimeFaultReason reason,
                           bool fail_safe_confirmed) {
    uint32_t minimum_free_heap = UINT32_MAX;
    portENTER_CRITICAL(&safety_mux);
    minimum_free_heap = current_minimum_free_heap;
    portEXIT_CRITICAL(&safety_mux);
    if (!lock_safety_state()) {
        return false;
    }
    persistent.pending_fault = reason;
    persistent.pending_fail_safe_confirmed =
        fail_safe_confirmed;
    persistent.pending_fault_uptime_ms = millis();
    persistent.pending_minimum_free_heap =
        minimum_free_heap == UINT32_MAX
            ? ESP.getFreeHeap()
            : minimum_free_heap;
    const bool saved = persist_state_locked();
    xSemaphoreGive(safety_state_mutex);
    return saved;
}

[[noreturn]] void restart_after_fault(RuntimeFaultReason reason) {
    const bool fail_safe_confirmed =
        hal_mcp_latch_all_relays_safe();
    const bool diagnostic_saved =
        persist_pending_fault(
            reason, fail_safe_confirmed);
    Serial.printf(
        "SAFETY: restart reason=%s relays_safe=%s diagnostic=%s.\n",
        runtime_fault_reason_code(reason),
        fail_safe_confirmed ? "yes" : "no",
        diagnostic_saved ? "saved" : "not_saved");
    Serial.flush();
    vTaskDelay(pdMS_TO_TICKS(40U));
    ESP.restart();
    for (;;) {
        vTaskDelay(portMAX_DELAY);
    }
}

void supervisor_task(void *) {
    runtime_safety_register_current_task(
        RuntimeSafetyTask::Supervisor);
    TickType_t next_wake = xTaskGetTickCount();
    for (;;) {
        const uint32_t now_ms = millis();
        runtime_safety_heartbeat(
            RuntimeSafetyTask::Supervisor,
            now_ms,
            ESP.getFreeHeap());

        bool ui_seen = false;
        bool io_seen = false;
        uint32_t ui_heartbeat = 0U;
        uint32_t io_heartbeat = 0U;
        portENTER_CRITICAL(&safety_mux);
        ui_seen =
            heartbeat_seen[task_index(RuntimeSafetyTask::Ui)];
        io_seen =
            heartbeat_seen[task_index(RuntimeSafetyTask::Io)];
        ui_heartbeat =
            heartbeat_ms[task_index(RuntimeSafetyTask::Ui)];
        io_heartbeat =
            heartbeat_ms[task_index(RuntimeSafetyTask::Io)];
        portEXIT_CRITICAL(&safety_mux);

        if (static_cast<uint32_t>(now_ms - initialized_ms) >=
            STARTUP_GRACE_MS) {
            if (!ui_seen ||
                static_cast<uint32_t>(
                    now_ms - ui_heartbeat) >
                    HEARTBEAT_TIMEOUT_MS) {
                restart_after_fault(
                    RuntimeFaultReason::UiHeartbeatStale);
            }
            if (!io_seen ||
                static_cast<uint32_t>(
                    now_ms - io_heartbeat) >
                    HEARTBEAT_TIMEOUT_MS) {
                restart_after_fault(
                    RuntimeFaultReason::IoHeartbeatStale);
            }
        }
        vTaskDelayUntil(
            &next_wake, pdMS_TO_TICKS(SUPERVISOR_PERIOD_MS));
    }
}

} // namespace

bool runtime_safety_initialize(void) {
    initialized_ms = millis();
    current_minimum_free_heap = ESP.getFreeHeap();
    bool state_saved = false;
    uint32_t boot_count = 0U;
    uint32_t fault_count = 0U;
    if (lock_safety_state()) {
        memset(&persistent, 0, sizeof(persistent));

        Preferences storage;
        if (storage.begin(SAFETY_NAMESPACE, true)) {
            PersistentSafetyState loaded = {};
            const size_t bytes =
                storage.getBytes(
                    SAFETY_KEY,
                    &loaded,
                    sizeof(loaded));
            storage.end();
            if (bytes == sizeof(loaded) &&
                persistent_valid(loaded)) {
                persistent = loaded;
            }
        }
        if (persistent.boot_count < UINT32_MAX) {
            ++persistent.boot_count;
        }
        append_boot_diagnostic(esp_reset_reason());
        state_saved = persist_state_locked();
        boot_count = persistent.boot_count;
        fault_count = persistent.fault_count;
        xSemaphoreGive(safety_state_mutex);
    }

    const esp_err_t watchdog =
        esp_task_wdt_init(TASK_WATCHDOG_TIMEOUT_SECONDS, true);
    const bool watchdog_ready =
        watchdog == ESP_OK ||
        watchdog == ESP_ERR_INVALID_STATE;
    if (!watchdog_ready) {
        Serial.printf(
            "SAFETY: task watchdog init failed: %d.\n",
            static_cast<int>(watchdog));
    } else if (watchdog == ESP_ERR_INVALID_STATE) {
        Serial.println(
            "SAFETY: task watchdog already active; "
            "task subscription will verify it.");
    }
    Serial.printf(
        "SAFETY: boot=%lu faults=%lu storage=%s reset=%u.\n",
        static_cast<unsigned long>(boot_count),
        static_cast<unsigned long>(fault_count),
        state_saved ? "ok" : "error",
        static_cast<unsigned>(esp_reset_reason()));
    return state_saved && watchdog_ready;
}

bool runtime_safety_register_current_task(RuntimeSafetyTask task) {
    if (!valid_task(task)) {
        return false;
    }
    const esp_err_t result = esp_task_wdt_add(nullptr);
    const bool registered =
        result == ESP_OK || result == ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&safety_mux);
    watchdog_registered[task_index(task)] = registered;
    heartbeat_seen[task_index(task)] = true;
    heartbeat_ms[task_index(task)] = millis();
    portEXIT_CRITICAL(&safety_mux);
    if (!registered) {
        Serial.printf(
            "SAFETY: WDT subscribe failed task=%u err=%d.\n",
            static_cast<unsigned>(task_index(task)),
            static_cast<int>(result));
    }
    return registered;
}

void runtime_safety_heartbeat(RuntimeSafetyTask task,
                              uint32_t now_ms,
                              uint32_t free_heap_bytes) {
    if (!valid_task(task)) {
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

bool runtime_safety_start_supervisor(void) {
    if (supervisor_handle != nullptr) {
        return true;
    }
    const BaseType_t created = xTaskCreatePinnedToCore(
        supervisor_task,
        "safety_supervisor",
        SUPERVISOR_STACK_BYTES,
        nullptr,
        SUPERVISOR_PRIORITY,
        &supervisor_handle,
        1);
    if (created != pdPASS) {
        supervisor_handle = nullptr;
        return false;
    }
    return true;
}

[[noreturn]] void runtime_safety_fail_and_restart(
    RuntimeFaultReason reason) {
    restart_after_fault(reason);
}

void runtime_safety_record_restart(RuntimeFaultReason reason,
                                   bool fail_safe_confirmed) {
    persist_pending_fault(reason, fail_safe_confirmed);
}

RuntimeSafetyStatus runtime_safety_status(void) {
    RuntimeSafetyStatus status = {};
    PersistentSafetyState snapshot = {};
    if (lock_safety_state(pdMS_TO_TICKS(100U))) {
        snapshot = persistent;
        xSemaphoreGive(safety_state_mutex);
    }
    uint32_t minimum_free_heap = UINT32_MAX;
    portENTER_CRITICAL(&safety_mux);
    minimum_free_heap = current_minimum_free_heap;
    portEXIT_CRITICAL(&safety_mux);
    status.boot_id = snapshot.boot_count;
    status.boot_count = snapshot.boot_count;
    status.fault_count = snapshot.fault_count;
    status.current_minimum_free_heap =
        minimum_free_heap == UINT32_MAX
            ? 0U
            : minimum_free_heap;
    if (snapshot.history_count > 0U) {
        const uint8_t last_index =
            static_cast<uint8_t>(
                (snapshot.history_next +
                 RESET_HISTORY_CAPACITY - 1U) %
                RESET_HISTORY_CAPACITY);
        status.last_reset = snapshot.history[last_index];
    }
    return status;
}

const char *runtime_fault_reason_code(RuntimeFaultReason reason) {
    switch (reason) {
    case RuntimeFaultReason::UiHeartbeatStale:
        return "ui_heartbeat_stale";
    case RuntimeFaultReason::IoHeartbeatStale:
        return "io_heartbeat_stale";
    case RuntimeFaultReason::TaskWatchdogReset:
        return "task_watchdog_reset";
    case RuntimeFaultReason::PanicReset:
        return "panic_reset";
    case RuntimeFaultReason::GuiInitializationFailed:
        return "gui_initialization_failed";
    case RuntimeFaultReason::IoTaskStartFailed:
        return "io_task_start_failed";
    case RuntimeFaultReason::ManualRestart:
        return "manual_restart";
    case RuntimeFaultReason::FactoryReset:
        return "factory_reset";
    case RuntimeFaultReason::OtaUpdate:
        return "ota_update";
    case RuntimeFaultReason::None:
    default:
        return "none";
    }
}
