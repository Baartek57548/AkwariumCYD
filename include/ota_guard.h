#ifndef AQUARIUM_OTA_GUARD_H
#define AQUARIUM_OTA_GUARD_H

#include <stddef.h>
#include <stdint.h>

struct OtaGuardStatus {
    bool pending_verify;
    bool rollback_available;
    uint32_t update_partition_bytes;
    uint8_t boot_attempt;
    const char *state;
};

/**
 * Initializes ESP-IDF pending-verify handling and the Arduino-compatible
 * previous-partition fallback. Must run before communication tasks start.
 */
void ota_guard_initialize(uint32_t now_ms);

/**
 * Evaluates memory, partition, active-operation and configuration-backup
 * requirements. On success it records the currently running partition so a
 * new application can return to it even when the framework bootloader does not
 * expose native pending-verify state.
 */
bool ota_guard_prepare_update(uint32_t image_bytes,
                              uint32_t free_heap_bytes,
                              bool feeder_active,
                              bool configuration_backed_up,
                              char *out_code,
                              size_t out_code_size);

/** Services the 30-second post-update health window without blocking. */
void ota_guard_service(uint32_t now_ms,
                       bool runtime_ready,
                       uint32_t free_heap_bytes);

OtaGuardStatus ota_guard_status();

#endif
