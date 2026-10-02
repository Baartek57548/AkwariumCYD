#ifndef AQUARIUM_SECURE_OTA_H
#define AQUARIUM_SECURE_OTA_H

#include <stddef.h>
#include <stdint.h>

#include "ota_package.h"

struct SecureOtaStatus {
    bool active;
    bool header_verified;
    bool completed;
    uint32_t package_bytes_received;
    uint32_t image_bytes_received;
    uint32_t expected_image_bytes;
    uint32_t security_version;
    const char *firmware_version;
    const char *state;
};

/**
 * Starts a new fixed-format .aqfw upload. No flash partition is touched until
 * the signed 512-byte package header has been parsed and verified.
 */
bool secure_ota_begin(bool feeder_active,
                      bool configuration_backed_up,
                      uint32_t free_heap_bytes,
                      char *out_code,
                      size_t out_code_size);

/** Streams one multipart file chunk into the verifier and inactive OTA slot. */
bool secure_ota_write(const uint8_t *data,
                      size_t length,
                      char *out_code,
                      size_t out_code_size);

/**
 * Verifies exact length, SHA-256, Secure Boot v2 signature-block shape and the
 * ESP image before selecting the inactive partition.
 */
bool secure_ota_finish(char *out_code, size_t out_code_size);

/** Aborts the inactive write and clears the pending rollback record. */
void secure_ota_abort(const char *reason);

SecureOtaStatus secure_ota_status();
const aquarium::OtaPackageMetadata *secure_ota_metadata();

#endif
