#ifndef AQUARIUM_OTA_PACKAGE_H
#define AQUARIUM_OTA_PACKAGE_H

#include <stddef.h>
#include <stdint.h>

namespace aquarium {

constexpr size_t kOtaPackageSignedMetadataBytes = 128U;
constexpr size_t kOtaPackageSignatureBytes = 384U;
constexpr size_t kOtaPackageHeaderBytes =
    kOtaPackageSignedMetadataBytes + kOtaPackageSignatureBytes;
constexpr uint16_t kOtaPackageFormatVersion = 1U;
constexpr uint8_t kOtaPackageAlgorithmRsa3072PssSha256 = 1U;
constexpr uint16_t kOtaPackageMinimumBootloaderVersion = 1U;
constexpr char kOtaPackageProductId[] = "aquacyd-cyd";

enum class OtaPackageTarget : uint8_t {
    Ili9341 = 1U,
    St7789 = 2U
};

enum class OtaPackageValidation : uint8_t {
    Valid = 0U,
    HeaderTooShort,
    InvalidMagic,
    UnsupportedFormat,
    UnsupportedAlgorithm,
    InvalidFlags,
    InvalidTarget,
    WrongTarget,
    InvalidImageSize,
    InvalidDigest,
    InvalidVersion,
    DowngradeBlocked,
    InvalidProduct,
    InvalidKeyId,
    InvalidCommit,
    BootloaderTooOld,
    SecurityRollbackBlocked,
    InvalidSignatureEncoding
};

struct OtaPackageMetadata {
    OtaPackageTarget target;
    uint32_t image_bytes;
    uint32_t security_version;
    uint8_t image_sha256[32];
    char firmware_version[16];
    char product_id[16];
    char key_id[17];
    char commit[21];
    uint16_t minimum_bootloader_version;
    uint8_t signature[kOtaPackageSignatureBytes];
};

OtaPackageValidation parse_ota_package_header(
    const uint8_t *header,
    size_t header_bytes,
    OtaPackageMetadata *out);

OtaPackageValidation validate_ota_package_metadata(
    const OtaPackageMetadata &metadata,
    OtaPackageTarget expected_target,
    const char *current_version,
    uint32_t minimum_security_version,
    uint16_t bootloader_version,
    const char *trusted_key_id,
    uint32_t maximum_image_bytes);

int compare_semantic_versions(const char *left, const char *right);
const char *ota_package_validation_code(OtaPackageValidation validation);

} // namespace aquarium

#endif
