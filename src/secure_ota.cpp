#include "secure_ota.h"

#include "config.h"
#include "firmware_trust_anchor.h"
#include "ota_guard.h"

#include <Arduino.h>
#include <Update.h>
#include <mbedtls/pk.h>
#include <mbedtls/rsa.h>
#include <mbedtls/sha256.h>
#include <stdio.h>
#include <string.h>

namespace {

constexpr uint32_t SECURE_BOOT_V2_SIGNATURE_SECTOR_BYTES = 4096U;
constexpr uint8_t SECURE_BOOT_V2_SIGNATURE_BLOCK_MAGIC = 0xE7U;

enum class ReceiverState : uint8_t {
    Idle,
    ReadingHeader,
    WritingImage,
    Completed,
    Failed
};

ReceiverState receiver_state = ReceiverState::Idle;
uint8_t package_header[aquarium::kOtaPackageHeaderBytes] = {};
size_t package_header_bytes = 0U;
aquarium::OtaPackageMetadata package_metadata = {};
mbedtls_sha256_context image_sha = {};
bool image_sha_initialized = false;
bool preflight_feeder_active = false;
bool preflight_backup_ok = false;
uint32_t preflight_free_heap_bytes = 0U;
uint32_t package_bytes_received = 0U;
uint32_t image_bytes_received = 0U;
uint8_t secure_boot_signature_magic = 0U;
char state_name[48] = "idle";

void set_code(char *out, size_t out_size, const char *code) {
    if (out != nullptr && out_size > 0U) {
        snprintf(out, out_size, "%s", code != nullptr ? code : "unknown");
    }
}

void set_state(const char *value) {
    snprintf(
        state_name,
        sizeof(state_name),
        "%s",
        value != nullptr ? value : "unknown");
}

void free_sha() {
    if (image_sha_initialized) {
        mbedtls_sha256_free(&image_sha);
        image_sha_initialized = false;
    }
}

void fail(const char *code, char *out_code, size_t out_code_size) {
    if (Update.isRunning()) {
        Update.abort();
    }
    ota_guard_cancel_update();
    free_sha();
    receiver_state = ReceiverState::Failed;
    set_state(code != nullptr ? code : "secure_ota_failed");
    set_code(out_code, out_code_size, state_name);
}

bool constant_time_equal(const uint8_t *left,
                         const uint8_t *right,
                         size_t length) {
    if (left == nullptr || right == nullptr) {
        return false;
    }
    uint8_t difference = 0U;
    for (size_t index = 0U; index < length; ++index) {
        difference |= static_cast<uint8_t>(left[index] ^ right[index]);
    }
    return difference == 0U;
}

bool verify_header_signature() {
    uint8_t metadata_digest[32] = {};
    if (mbedtls_sha256_ret(
            package_header,
            aquarium::kOtaPackageSignedMetadataBytes,
            metadata_digest,
            0) != 0) {
        return false;
    }

    mbedtls_pk_context key;
    mbedtls_pk_init(&key);
    const int parse_result = mbedtls_pk_parse_public_key(
        &key,
        reinterpret_cast<const unsigned char *>(
            FirmwareTrust::RSA3072_PUBLIC_KEY_PEM),
        FirmwareTrust::RSA3072_PUBLIC_KEY_PEM_BYTES);
    if (parse_result != 0 || !mbedtls_pk_can_do(&key, MBEDTLS_PK_RSA)) {
        mbedtls_pk_free(&key);
        return false;
    }

    mbedtls_rsa_context *rsa = mbedtls_pk_rsa(key);
    const bool valid_size =
        rsa != nullptr &&
        mbedtls_pk_get_bitlen(&key) == 3072U &&
        mbedtls_pk_get_len(&key) == aquarium::kOtaPackageSignatureBytes;
    int verify_result = -1;
    if (valid_size) {
        verify_result = mbedtls_rsa_rsassa_pss_verify_ext(
            rsa,
            nullptr,
            nullptr,
            MBEDTLS_RSA_PUBLIC,
            MBEDTLS_MD_SHA256,
            sizeof(metadata_digest),
            metadata_digest,
            MBEDTLS_MD_SHA256,
            sizeof(metadata_digest),
            package_metadata.signature);
    }
    mbedtls_pk_free(&key);
    return valid_size && verify_result == 0;
}

aquarium::OtaPackageTarget expected_target() {
#if CYD_PANEL_ST7789
    return aquarium::OtaPackageTarget::St7789;
#else
    return aquarium::OtaPackageTarget::Ili9341;
#endif
}

bool initialize_image_write(char *out_code, size_t out_code_size) {
    aquarium::OtaPackageValidation validation =
        aquarium::parse_ota_package_header(
            package_header,
            sizeof(package_header),
            &package_metadata);
    if (validation != aquarium::OtaPackageValidation::Valid) {
        fail(
            aquarium::ota_package_validation_code(validation),
            out_code,
            out_code_size);
        return false;
    }

    const OtaGuardStatus guard = ota_guard_status();
    validation = aquarium::validate_ota_package_metadata(
        package_metadata,
        expected_target(),
        FirmwareInfo::VERSION,
        ota_guard_minimum_security_version(),
        FirmwareInfo::BOOTLOADER_COMPATIBILITY_VERSION,
        FirmwareTrust::KEY_ID,
        guard.update_partition_bytes);
    if (validation != aquarium::OtaPackageValidation::Valid) {
        fail(
            aquarium::ota_package_validation_code(validation),
            out_code,
            out_code_size);
        return false;
    }
    if (package_metadata.image_bytes <
            SECURE_BOOT_V2_SIGNATURE_SECTOR_BYTES ||
        package_metadata.image_bytes %
                SECURE_BOOT_V2_SIGNATURE_SECTOR_BYTES !=
            0U) {
        fail("secure_boot_image_alignment", out_code, out_code_size);
        return false;
    }
    if (!verify_header_signature()) {
        fail("signature_invalid", out_code, out_code_size);
        return false;
    }

    char preflight_code[48] = {};
    if (!ota_guard_prepare_update(
            package_metadata.image_bytes,
            package_metadata.security_version,
            preflight_free_heap_bytes,
            preflight_feeder_active,
            preflight_backup_ok,
            preflight_code,
            sizeof(preflight_code))) {
        fail(
            preflight_code[0] != '\0' ? preflight_code : "preflight_failed",
            out_code,
            out_code_size);
        return false;
    }
    if (!Update.begin(package_metadata.image_bytes, U_FLASH)) {
        fail("update_begin_failed", out_code, out_code_size);
        return false;
    }

    mbedtls_sha256_init(&image_sha);
    image_sha_initialized = true;
    if (mbedtls_sha256_starts_ret(&image_sha, 0) != 0) {
        fail("sha256_init_failed", out_code, out_code_size);
        return false;
    }
    receiver_state = ReceiverState::WritingImage;
    set_state("writing_image");
    return true;
}

bool write_image_bytes(const uint8_t *data,
                       size_t length,
                       char *out_code,
                       size_t out_code_size) {
    if (data == nullptr || length == 0U) {
        return true;
    }
    if (image_bytes_received > package_metadata.image_bytes ||
        length > package_metadata.image_bytes - image_bytes_received) {
        fail("package_has_trailing_data", out_code, out_code_size);
        return false;
    }

    const uint32_t signature_sector_offset =
        package_metadata.image_bytes -
        SECURE_BOOT_V2_SIGNATURE_SECTOR_BYTES;
    if (image_bytes_received <= signature_sector_offset &&
        image_bytes_received + length > signature_sector_offset) {
        secure_boot_signature_magic =
            data[signature_sector_offset - image_bytes_received];
    }

    if (mbedtls_sha256_update_ret(&image_sha, data, length) != 0) {
        fail("sha256_update_failed", out_code, out_code_size);
        return false;
    }
    // Arduino-ESP32 2.x exposes a legacy non-const pointer even though the
    // Update implementation only reads the supplied buffer.
    const size_t written =
        Update.write(const_cast<uint8_t *>(data), length);
    if (written != length) {
        fail("update_write_failed", out_code, out_code_size);
        return false;
    }
    image_bytes_received += static_cast<uint32_t>(length);
    return true;
}

} // namespace

bool secure_ota_begin(bool feeder_active,
                      bool configuration_backed_up,
                      uint32_t free_heap_bytes,
                      char *out_code,
                      size_t out_code_size) {
    if (Update.isRunning()) {
        Update.abort();
    }
    const bool replacing_active_upload =
        receiver_state == ReceiverState::ReadingHeader ||
        receiver_state == ReceiverState::WritingImage;
    if (replacing_active_upload) {
        ota_guard_cancel_update();
    } else if (ota_guard_status().pending_verify) {
        receiver_state = ReceiverState::Failed;
        set_state("firmware_pending_validation");
        set_code(out_code, out_code_size, state_name);
        return false;
    }
    free_sha();
    memset(package_header, 0, sizeof(package_header));
    memset(&package_metadata, 0, sizeof(package_metadata));
    package_header_bytes = 0U;
    package_bytes_received = 0U;
    image_bytes_received = 0U;
    secure_boot_signature_magic = 0U;
    preflight_feeder_active = feeder_active;
    preflight_backup_ok = configuration_backed_up;
    preflight_free_heap_bytes = free_heap_bytes;
    receiver_state = ReceiverState::ReadingHeader;
    set_state("reading_header");
    set_code(out_code, out_code_size, "ok");
    return true;
}

bool secure_ota_write(const uint8_t *data,
                      size_t length,
                      char *out_code,
                      size_t out_code_size) {
    if (receiver_state != ReceiverState::ReadingHeader &&
        receiver_state != ReceiverState::WritingImage) {
        set_code(out_code, out_code_size, "upload_not_active");
        return false;
    }
    if (data == nullptr || length == 0U ||
        length > UINT32_MAX - package_bytes_received) {
        fail("invalid_upload_chunk", out_code, out_code_size);
        return false;
    }
    package_bytes_received += static_cast<uint32_t>(length);
    size_t consumed = 0U;
    if (receiver_state == ReceiverState::ReadingHeader) {
        const size_t header_remaining =
            sizeof(package_header) - package_header_bytes;
        const size_t header_chunk =
            length < header_remaining ? length : header_remaining;
        memcpy(
            package_header + package_header_bytes,
            data,
            header_chunk);
        package_header_bytes += header_chunk;
        consumed += header_chunk;
        if (package_header_bytes == sizeof(package_header) &&
            !initialize_image_write(out_code, out_code_size)) {
            return false;
        }
    }
    if (receiver_state == ReceiverState::WritingImage &&
        consumed < length) {
        return write_image_bytes(
            data + consumed,
            length - consumed,
            out_code,
            out_code_size);
    }
    set_code(out_code, out_code_size, "ok");
    return true;
}

bool secure_ota_finish(char *out_code, size_t out_code_size) {
    if (receiver_state != ReceiverState::WritingImage ||
        !image_sha_initialized) {
        fail("package_incomplete", out_code, out_code_size);
        return false;
    }
    if (image_bytes_received != package_metadata.image_bytes ||
        package_bytes_received !=
            aquarium::kOtaPackageHeaderBytes +
                package_metadata.image_bytes) {
        fail("package_size_mismatch", out_code, out_code_size);
        return false;
    }
    if (secure_boot_signature_magic !=
        SECURE_BOOT_V2_SIGNATURE_BLOCK_MAGIC) {
        fail("secure_boot_signature_missing", out_code, out_code_size);
        return false;
    }

    uint8_t actual_digest[32] = {};
    if (mbedtls_sha256_finish_ret(&image_sha, actual_digest) != 0) {
        fail("sha256_finish_failed", out_code, out_code_size);
        return false;
    }
    free_sha();
    if (!constant_time_equal(
            actual_digest,
            package_metadata.image_sha256,
            sizeof(actual_digest))) {
        fail("firmware_digest_mismatch", out_code, out_code_size);
        return false;
    }
    if (!Update.end(false)) {
        fail("update_end_failed", out_code, out_code_size);
        return false;
    }

    receiver_state = ReceiverState::Completed;
    set_state("verified");
    set_code(out_code, out_code_size, "ok");
    return true;
}

void secure_ota_abort(const char *reason) {
    if (receiver_state != ReceiverState::ReadingHeader &&
        receiver_state != ReceiverState::WritingImage) {
        return;
    }
    char ignored[2] = {};
    fail(
        reason != nullptr && reason[0] != '\0'
            ? reason
            : "upload_aborted",
        ignored,
        sizeof(ignored));
}

SecureOtaStatus secure_ota_status() {
    return {
        receiver_state == ReceiverState::ReadingHeader ||
            receiver_state == ReceiverState::WritingImage,
        receiver_state == ReceiverState::WritingImage ||
            receiver_state == ReceiverState::Completed,
        receiver_state == ReceiverState::Completed,
        package_bytes_received,
        image_bytes_received,
        package_metadata.image_bytes,
        package_metadata.security_version,
        package_metadata.firmware_version,
        state_name
    };
}

const aquarium::OtaPackageMetadata *secure_ota_metadata() {
    return receiver_state == ReceiverState::WritingImage ||
                   receiver_state == ReceiverState::Completed
               ? &package_metadata
               : nullptr;
}
