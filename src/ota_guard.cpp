#include "ota_guard.h"

#include "config.h"
#include "ota_safety_policy.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <stdio.h>
#include <string.h>

namespace {

constexpr char OTA_GUARD_NAMESPACE[] = "aq_ota_guard";
constexpr char OTA_GUARD_KEY[] = "state";
constexpr uint32_t OTA_GUARD_MAGIC = 0x3341544FUL; // OTA3
constexpr uint8_t OTA_GUARD_MAX_BOOT_ATTEMPTS = 3U;
constexpr size_t PARTITION_LABEL_BYTES = 17U;

struct PersistentGuardState {
    uint32_t magic;
    bool pending;
    uint8_t boot_attempt;
    char previous_partition[PARTITION_LABEL_BYTES];
    uint32_t pending_security_version;
    uint32_t accepted_security_version;
    uint32_t crc32;
};

aquarium::BootValidationPolicy validation_policy;
PersistentGuardState persistent_state = {};
bool initialized = false;
bool native_pending_verify = false;
bool rollback_available = false;
uint32_t update_partition_bytes = 0U;
const char *guard_state_name = "idle";
bool state_storage_healthy = true;

uint32_t crc32_bytes(const void *data, size_t length) {
    uint32_t crc = 0xFFFFFFFFUL;
    const uint8_t *bytes = static_cast<const uint8_t *>(data);
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

uint32_t guard_crc(const PersistentGuardState &state) {
    return crc32_bytes(&state, sizeof(state) - sizeof(state.crc32));
}

bool load_state(PersistentGuardState *out) {
    if (out == nullptr) {
        return false;
    }
    Preferences storage;
    if (!storage.begin(OTA_GUARD_NAMESPACE, true)) {
        return false;
    }
    const size_t read = storage.getBytes(OTA_GUARD_KEY, out, sizeof(*out));
    storage.end();
    return read == sizeof(*out) &&
           out->magic == OTA_GUARD_MAGIC &&
           out->crc32 == guard_crc(*out);
}

bool save_state(PersistentGuardState &state) {
    state.magic = OTA_GUARD_MAGIC;
    state.crc32 = guard_crc(state);
    Preferences storage;
    if (!storage.begin(OTA_GUARD_NAMESPACE, false)) {
        return false;
    }
    const bool stored =
        storage.putBytes(OTA_GUARD_KEY, &state, sizeof(state)) == sizeof(state);
    storage.end();
    return stored;
}

bool clear_pending_state(bool accept_pending_version) {
    PersistentGuardState next = persistent_state;
    if (accept_pending_version &&
        next.pending_security_version >
            next.accepted_security_version) {
        next.accepted_security_version =
            next.pending_security_version;
    }
    next.pending = false;
    next.boot_attempt = 0U;
    next.previous_partition[0] = '\0';
    next.pending_security_version = 0U;
    if (!save_state(next)) {
        state_storage_healthy = false;
        return false;
    }
    persistent_state = next;
    state_storage_healthy = true;
    return true;
}

const esp_partition_t *find_previous_partition() {
    if (persistent_state.previous_partition[0] == '\0') {
        return nullptr;
    }
    esp_partition_iterator_t iterator = esp_partition_find(
        ESP_PARTITION_TYPE_APP,
        ESP_PARTITION_SUBTYPE_ANY,
        nullptr);
    const esp_partition_t *match = nullptr;
    while (iterator != nullptr) {
        const esp_partition_t *candidate = esp_partition_get(iterator);
        if (candidate != nullptr &&
            strncmp(candidate->label,
                    persistent_state.previous_partition,
                    sizeof(candidate->label)) == 0) {
            match = candidate;
            break;
        }
        iterator = esp_partition_next(iterator);
    }
    if (iterator != nullptr) {
        esp_partition_iterator_release(iterator);
    }
    return match;
}

void rollback_and_reboot() {
    guard_state_name = "rollback";
    Serial.println("OTA_GUARD: runtime validation failed; rolling back.");

    if (native_pending_verify) {
        const esp_err_t native_result =
            esp_ota_mark_app_invalid_rollback_and_reboot();
        // Success reboots and never returns. If the IDF path cannot select a
        // rollback image, continue with the explicitly recorded A/B slot.
        Serial.printf(
            "OTA_GUARD: native rollback failed: %d; trying recorded slot.\n",
            static_cast<int>(native_result));
    }
    const esp_partition_t *previous = find_previous_partition();
    if (previous != nullptr && esp_ota_set_boot_partition(previous) == ESP_OK) {
        if (!clear_pending_state(false)) {
            Serial.println(
                "OTA_GUARD: rollback selected but state persistence failed.");
        }
        ESP.restart();
        return;
    }
    // Keep outputs under the caller's hardware failsafe and retain the pending
    // record for diagnostics when rollback cannot be selected.
    guard_state_name = "rollback_failed";
}

} // namespace

void ota_guard_initialize(uint32_t now_ms) {
    if (initialized) {
        return;
    }
    initialized = true;

    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);
    rollback_available = running != nullptr && next != nullptr && running != next;
    update_partition_bytes = next != nullptr ? next->size : 0U;

    esp_ota_img_states_t image_state = ESP_OTA_IMG_UNDEFINED;
    native_pending_verify =
        running != nullptr &&
        esp_ota_get_state_partition(running, &image_state) == ESP_OK &&
        image_state == ESP_OTA_IMG_PENDING_VERIFY;

    memset(&persistent_state, 0, sizeof(persistent_state));
    const bool state_loaded = load_state(&persistent_state);
    if (!state_loaded) {
        persistent_state.accepted_security_version =
            FirmwareInfo::SECURITY_VERSION;
        state_storage_healthy = save_state(persistent_state);
    } else if (!persistent_state.pending &&
               persistent_state.accepted_security_version <
                   FirmwareInfo::SECURITY_VERSION) {
        persistent_state.accepted_security_version =
            FirmwareInfo::SECURITY_VERSION;
        state_storage_healthy = save_state(persistent_state);
    }
    const bool running_matches_previous =
        running == nullptr ||
        strncmp(running->label,
                persistent_state.previous_partition,
                sizeof(running->label)) == 0;
    const aquarium::OtaBootRecovery boot_recovery =
        aquarium::evaluate_ota_boot_recovery(
            persistent_state.pending,
            native_pending_verify,
            running_matches_previous);
    if (boot_recovery ==
        aquarium::OtaBootRecovery::TrackPendingBoot) {
        if (persistent_state.boot_attempt < UINT8_MAX) {
            ++persistent_state.boot_attempt;
        }
        state_storage_healthy = save_state(persistent_state);
        if (!state_storage_healthy) {
            guard_state_name = "ota_state_write_failed";
        }
        if (persistent_state.boot_attempt > OTA_GUARD_MAX_BOOT_ATTEMPTS) {
            rollback_and_reboot();
            return;
        }
    } else if (boot_recovery ==
               aquarium::OtaBootRecovery::FinalizeAcceptedState) {
        if (!clear_pending_state(true)) {
            guard_state_name = "ota_state_write_failed";
        }
    } else if (boot_recovery ==
               aquarium::OtaBootRecovery::ClearAbortedState) {
        if (!clear_pending_state(false)) {
            guard_state_name = "ota_state_write_failed";
        }
    }

    const bool pending = native_pending_verify || persistent_state.pending;
    validation_policy.begin(now_ms, pending);
    if (!state_storage_healthy) {
        guard_state_name = "ota_state_write_failed";
    } else {
        guard_state_name = pending ? "pending_verify" : "valid";
    }
    Serial.printf("OTA_GUARD: state=%s rollback=%s next=%lu attempt=%u\n",
                  guard_state_name,
                  rollback_available ? "yes" : "no",
                  static_cast<unsigned long>(update_partition_bytes),
                  static_cast<unsigned>(persistent_state.boot_attempt));
}

bool ota_guard_prepare_update(uint32_t image_bytes,
                              uint32_t package_security_version,
                              uint32_t free_heap_bytes,
                              bool feeder_active,
                              bool configuration_backed_up,
                              char *out_code,
                              size_t out_code_size) {
    if (!initialized) {
        ota_guard_initialize(millis());
    }
    if (!state_storage_healthy) {
        guard_state_name = "ota_state_write_failed";
        if (out_code != nullptr && out_code_size > 0U) {
            snprintf(out_code, out_code_size, "%s", guard_state_name);
        }
        return false;
    }
    if (package_security_version <
        persistent_state.accepted_security_version) {
        guard_state_name = "security_rollback_blocked";
        if (out_code != nullptr && out_code_size > 0U) {
            snprintf(out_code, out_code_size, "%s", guard_state_name);
        }
        return false;
    }
    const aquarium::OtaPreflightResult result = aquarium::evaluate_ota_preflight({
        image_bytes,
        update_partition_bytes,
        free_heap_bytes,
        feeder_active,
        configuration_backed_up,
        rollback_available
    });
    const char *code = aquarium::ota_preflight_code(result);
    if (out_code != nullptr && out_code_size > 0U) {
        snprintf(out_code, out_code_size, "%s", code);
    }
    if (result != aquarium::OtaPreflightResult::Ready) {
        guard_state_name = code;
        return false;
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == nullptr) {
        guard_state_name = "running_partition_missing";
        return false;
    }
    const uint32_t accepted_security_version =
        persistent_state.accepted_security_version;
    memset(&persistent_state, 0, sizeof(persistent_state));
    persistent_state.pending = true;
    persistent_state.boot_attempt = 0U;
    persistent_state.pending_security_version = package_security_version;
    persistent_state.accepted_security_version =
        accepted_security_version > FirmwareInfo::SECURITY_VERSION
            ? accepted_security_version
            : FirmwareInfo::SECURITY_VERSION;
    snprintf(persistent_state.previous_partition,
             sizeof(persistent_state.previous_partition),
             "%s",
             running->label);
    if (!save_state(persistent_state)) {
        state_storage_healthy = false;
        guard_state_name = "ota_state_write_failed";
        if (out_code != nullptr && out_code_size > 0U) {
            snprintf(out_code, out_code_size, "%s", guard_state_name);
        }
        return false;
    }
    state_storage_healthy = true;
    guard_state_name = "upload_ready";
    return true;
}

void ota_guard_cancel_update() {
    if (!initialized || !persistent_state.pending) {
        return;
    }
    if (clear_pending_state(false)) {
        guard_state_name = "upload_cancelled";
    } else {
        guard_state_name = "ota_state_write_failed";
    }
}

uint32_t ota_guard_minimum_security_version() {
    return persistent_state.accepted_security_version >
                   FirmwareInfo::SECURITY_VERSION
               ? persistent_state.accepted_security_version
               : FirmwareInfo::SECURITY_VERSION;
}

void ota_guard_service(uint32_t now_ms,
                       bool runtime_ready,
                       uint32_t free_heap_bytes) {
    if (!initialized) {
        return;
    }
    const aquarium::BootValidationDecision decision =
        validation_policy.evaluate(now_ms, runtime_ready, free_heap_bytes);
    if (decision == aquarium::BootValidationDecision::MarkValid) {
        aquarium::OtaFinalizeStep finalize_step =
            aquarium::next_ota_finalize_step(
                native_pending_verify,
                persistent_state.pending);
        if (finalize_step ==
            aquarium::OtaFinalizeStep::MarkNativeValid) {
            const esp_err_t result = esp_ota_mark_app_valid_cancel_rollback();
            if (result != ESP_OK) {
                guard_state_name = "mark_valid_failed";
                Serial.printf("OTA_GUARD: mark valid failed: %d\n",
                              static_cast<int>(result));
                validation_policy.begin(now_ms, true);
                return;
            }
            native_pending_verify = false;
            finalize_step = aquarium::next_ota_finalize_step(
                native_pending_verify,
                persistent_state.pending);
        }
        if (finalize_step ==
                aquarium::OtaFinalizeStep::PersistAcceptedState &&
            !clear_pending_state(true)) {
            guard_state_name = "ota_state_write_failed";
            validation_policy.begin(now_ms, true);
            return;
        }
        guard_state_name = "valid";
        Serial.println("OTA_GUARD: new firmware validated.");
    } else if (decision == aquarium::BootValidationDecision::Rollback) {
        rollback_and_reboot();
    }
}

OtaGuardStatus ota_guard_status() {
    return {
        native_pending_verify || persistent_state.pending,
        rollback_available,
        update_partition_bytes,
        ota_guard_minimum_security_version(),
        persistent_state.pending_security_version,
        persistent_state.boot_attempt,
        guard_state_name
    };
}
