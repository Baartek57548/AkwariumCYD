#include "ota_package.h"

#include <limits.h>
#include <string.h>

namespace aquarium {
namespace {

constexpr uint8_t kMagic[8] = {'A', 'Q', 'C', 'Y', 'D', 'O', 'T', 'A'};

uint16_t read_u16_le(const uint8_t *value) {
    return static_cast<uint16_t>(value[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(value[1]) << 8U);
}

uint32_t read_u32_le(const uint8_t *value) {
    return static_cast<uint32_t>(value[0]) |
           (static_cast<uint32_t>(value[1]) << 8U) |
           (static_cast<uint32_t>(value[2]) << 16U) |
           (static_cast<uint32_t>(value[3]) << 24U);
}

bool copy_canonical_text(const uint8_t *source,
                         size_t source_bytes,
                         char *destination,
                         size_t destination_bytes,
                         bool allow_dot) {
    if (source == nullptr || destination == nullptr ||
        source_bytes == 0U || destination_bytes < source_bytes) {
        return false;
    }
    size_t length = 0U;
    while (length < source_bytes && source[length] != 0U) {
        const uint8_t value = source[length];
        const bool allowed =
            (value >= 'a' && value <= 'z') ||
            (value >= 'A' && value <= 'Z') ||
            (value >= '0' && value <= '9') ||
            value == '-' || value == '_' || (allow_dot && value == '.');
        if (!allowed) {
            return false;
        }
        ++length;
    }
    if (length == 0U || length == source_bytes) {
        return false;
    }
    for (size_t index = length; index < source_bytes; ++index) {
        if (source[index] != 0U) {
            return false;
        }
    }
    memcpy(destination, source, length);
    destination[length] = '\0';
    return true;
}

bool parse_semantic_version(const char *value, uint32_t out[3]) {
    if (value == nullptr || out == nullptr || *value == '\0') {
        return false;
    }
    const char *cursor = value;
    for (size_t component = 0U; component < 3U; ++component) {
        if (*cursor < '0' || *cursor > '9') {
            return false;
        }
        if (*cursor == '0' && cursor[1] >= '0' && cursor[1] <= '9') {
            return false;
        }
        uint32_t parsed = 0U;
        while (*cursor >= '0' && *cursor <= '9') {
            const uint32_t digit = static_cast<uint32_t>(*cursor - '0');
            if (parsed > (UINT32_MAX - digit) / 10U) {
                return false;
            }
            parsed = parsed * 10U + digit;
            ++cursor;
        }
        out[component] = parsed;
        if (component < 2U) {
            if (*cursor != '.') {
                return false;
            }
            ++cursor;
        }
    }
    return *cursor == '\0';
}

bool all_zero(const uint8_t *value, size_t bytes) {
    uint8_t combined = 0U;
    for (size_t index = 0U; index < bytes; ++index) {
        combined |= value[index];
    }
    return combined == 0U;
}

bool canonical_hex(const char *value, size_t length) {
    if (value == nullptr || strlen(value) != length) {
        return false;
    }
    for (size_t index = 0U; index < length; ++index) {
        if (!((value[index] >= '0' && value[index] <= '9') ||
              (value[index] >= 'a' && value[index] <= 'f'))) {
            return false;
        }
    }
    return true;
}

} // namespace

OtaPackageValidation parse_ota_package_header(
    const uint8_t *header,
    size_t header_bytes,
    OtaPackageMetadata *out) {
    if (header == nullptr || out == nullptr ||
        header_bytes < kOtaPackageHeaderBytes) {
        return OtaPackageValidation::HeaderTooShort;
    }
    if (memcmp(header, kMagic, sizeof(kMagic)) != 0) {
        return OtaPackageValidation::InvalidMagic;
    }
    if (read_u16_le(header + 8U) != kOtaPackageFormatVersion ||
        read_u16_le(header + 10U) != kOtaPackageHeaderBytes) {
        return OtaPackageValidation::UnsupportedFormat;
    }
    if (header[12U] != kOtaPackageAlgorithmRsa3072PssSha256) {
        return OtaPackageValidation::UnsupportedAlgorithm;
    }
    if (read_u16_le(header + 14U) != 0U) {
        return OtaPackageValidation::InvalidFlags;
    }
    if (header[13U] != static_cast<uint8_t>(OtaPackageTarget::Ili9341) &&
        header[13U] != static_cast<uint8_t>(OtaPackageTarget::St7789)) {
        return OtaPackageValidation::InvalidTarget;
    }

    OtaPackageMetadata parsed = {};
    parsed.target = static_cast<OtaPackageTarget>(header[13U]);
    parsed.image_bytes = read_u32_le(header + 16U);
    parsed.security_version = read_u32_le(header + 20U);
    memcpy(parsed.image_sha256, header + 24U, sizeof(parsed.image_sha256));
    if (parsed.image_bytes == 0U) {
        return OtaPackageValidation::InvalidImageSize;
    }
    if (all_zero(parsed.image_sha256, sizeof(parsed.image_sha256))) {
        return OtaPackageValidation::InvalidDigest;
    }
    if (!copy_canonical_text(
            header + 56U, 16U,
            parsed.firmware_version, sizeof(parsed.firmware_version), true)) {
        return OtaPackageValidation::InvalidVersion;
    }
    uint32_t version[3] = {};
    if (!parse_semantic_version(parsed.firmware_version, version)) {
        return OtaPackageValidation::InvalidVersion;
    }
    if (!copy_canonical_text(
            header + 72U, 16U,
            parsed.product_id, sizeof(parsed.product_id), false)) {
        return OtaPackageValidation::InvalidProduct;
    }
    memcpy(parsed.key_id, header + 88U, 16U);
    parsed.key_id[16U] = '\0';
    if (!canonical_hex(parsed.key_id, 16U)) {
        return OtaPackageValidation::InvalidKeyId;
    }
    if (!copy_canonical_text(
            header + 104U, 20U,
            parsed.commit, sizeof(parsed.commit), false)) {
        return OtaPackageValidation::InvalidCommit;
    }
    const size_t commit_length = strlen(parsed.commit);
    if (commit_length < 7U || commit_length > 20U ||
        !canonical_hex(parsed.commit, commit_length)) {
        return OtaPackageValidation::InvalidCommit;
    }
    parsed.minimum_bootloader_version = read_u16_le(header + 124U);
    if (read_u16_le(header + 126U) != 0U) {
        return OtaPackageValidation::InvalidFlags;
    }
    memcpy(
        parsed.signature,
        header + kOtaPackageSignedMetadataBytes,
        sizeof(parsed.signature));
    if (all_zero(parsed.signature, sizeof(parsed.signature))) {
        return OtaPackageValidation::InvalidSignatureEncoding;
    }
    *out = parsed;
    return OtaPackageValidation::Valid;
}

OtaPackageValidation validate_ota_package_metadata(
    const OtaPackageMetadata &metadata,
    OtaPackageTarget expected_target,
    const char *current_version,
    uint32_t minimum_security_version,
    uint16_t bootloader_version,
    const char *trusted_key_id,
    uint32_t maximum_image_bytes) {
    if (metadata.target != expected_target) {
        return OtaPackageValidation::WrongTarget;
    }
    if (metadata.image_bytes == 0U ||
        metadata.image_bytes > maximum_image_bytes) {
        return OtaPackageValidation::InvalidImageSize;
    }
    if (strcmp(metadata.product_id, kOtaPackageProductId) != 0) {
        return OtaPackageValidation::InvalidProduct;
    }
    if (trusted_key_id == nullptr ||
        strcmp(metadata.key_id, trusted_key_id) != 0) {
        return OtaPackageValidation::InvalidKeyId;
    }
    if (metadata.minimum_bootloader_version > bootloader_version) {
        return OtaPackageValidation::BootloaderTooOld;
    }
    if (metadata.security_version < minimum_security_version) {
        return OtaPackageValidation::SecurityRollbackBlocked;
    }
    const int version_comparison =
        compare_semantic_versions(metadata.firmware_version, current_version);
    if (version_comparison == INT_MIN) {
        return OtaPackageValidation::InvalidVersion;
    }
    if (version_comparison < 0) {
        return OtaPackageValidation::DowngradeBlocked;
    }
    return OtaPackageValidation::Valid;
}

int compare_semantic_versions(const char *left, const char *right) {
    uint32_t left_parts[3] = {};
    uint32_t right_parts[3] = {};
    if (!parse_semantic_version(left, left_parts) ||
        !parse_semantic_version(right, right_parts)) {
        return INT_MIN;
    }
    for (size_t index = 0U; index < 3U; ++index) {
        if (left_parts[index] < right_parts[index]) {
            return -1;
        }
        if (left_parts[index] > right_parts[index]) {
            return 1;
        }
    }
    return 0;
}

const char *ota_package_validation_code(OtaPackageValidation validation) {
    switch (validation) {
    case OtaPackageValidation::Valid:
        return "ok";
    case OtaPackageValidation::HeaderTooShort:
        return "header_too_short";
    case OtaPackageValidation::InvalidMagic:
        return "invalid_magic";
    case OtaPackageValidation::UnsupportedFormat:
        return "unsupported_format";
    case OtaPackageValidation::UnsupportedAlgorithm:
        return "unsupported_algorithm";
    case OtaPackageValidation::InvalidFlags:
        return "invalid_flags";
    case OtaPackageValidation::InvalidTarget:
        return "invalid_target";
    case OtaPackageValidation::WrongTarget:
        return "wrong_target";
    case OtaPackageValidation::InvalidImageSize:
        return "invalid_image_size";
    case OtaPackageValidation::InvalidDigest:
        return "invalid_digest";
    case OtaPackageValidation::InvalidVersion:
        return "invalid_version";
    case OtaPackageValidation::DowngradeBlocked:
        return "downgrade_blocked";
    case OtaPackageValidation::InvalidProduct:
        return "invalid_product";
    case OtaPackageValidation::InvalidKeyId:
        return "invalid_key_id";
    case OtaPackageValidation::InvalidCommit:
        return "invalid_commit";
    case OtaPackageValidation::BootloaderTooOld:
        return "bootloader_too_old";
    case OtaPackageValidation::SecurityRollbackBlocked:
        return "security_rollback_blocked";
    case OtaPackageValidation::InvalidSignatureEncoding:
        return "invalid_signature";
    }
    return "unknown_validation";
}

} // namespace aquarium
