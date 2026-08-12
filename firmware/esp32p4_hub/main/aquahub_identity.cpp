#include "aquahub_identity.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "mbedtls/ecp.h"
#include "mbedtls/md.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"
#include "mbedtls/x509_crt.h"
#include "nvs.h"

namespace {

constexpr char kTag[] = "aquahub_identity";
constexpr char kNamespace[] = "aquahub-id";
constexpr char kCertificateKey[] = "certificate";
constexpr char kPrivateKeyKey[] = "private-key";
constexpr char kTokenHashKey[] = "token-hash";
constexpr size_t kCertificateCapacity = 2048U;
constexpr size_t kPrivateKeyCapacity = 768U;
constexpr size_t kSha256Bytes = 32U;
constexpr uint64_t kPairingLifetimeUs = 10ULL * 60ULL * 1000000ULL;
constexpr uint64_t kPairingLockoutUs = 60ULL * 1000000ULL;
constexpr uint8_t kMaximumPairingFailures = 5U;

uint8_t certificate_pem[kCertificateCapacity] = {};
size_t certificate_length = 0U;
uint8_t private_key_pem[kPrivateKeyCapacity] = {};
size_t private_key_length = 0U;
char fingerprint[kAquaHubFingerprintBytes] = {};
uint8_t access_token_hash[kSha256Bytes] = {};
bool access_token_configured = false;
uint32_t pairing_code = 0U;
uint64_t pairing_expires_at_us = 0U;
uint64_t pairing_locked_until_us = 0U;
uint8_t pairing_failures = 0U;

int random_callback(void *, unsigned char *output, size_t length) {
    if (output == nullptr) {
        return -1;
    }
    esp_fill_random(output, length);
    return 0;
}

void bytes_to_hex(const uint8_t *input,
                  size_t input_length,
                  char *output,
                  size_t output_capacity) {
    static constexpr char digits[] = "0123456789abcdef";
    if (input == nullptr || output == nullptr ||
        output_capacity < input_length * 2U + 1U) {
        if (output != nullptr && output_capacity > 0U) {
            output[0] = '\0';
        }
        return;
    }
    for (size_t index = 0U; index < input_length; ++index) {
        output[index * 2U] = digits[input[index] >> 4U];
        output[index * 2U + 1U] = digits[input[index] & 0x0FU];
    }
    output[input_length * 2U] = '\0';
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

void sha256(const uint8_t *input, size_t length, uint8_t *output) {
    mbedtls_sha256(input, length, output, 0);
}

bool valid_loaded_pem() {
    return certificate_length > 1U &&
           certificate_length <= sizeof(certificate_pem) &&
           private_key_length > 1U &&
           private_key_length <= sizeof(private_key_pem) &&
           memcmp(certificate_pem,
                  "-----BEGIN CERTIFICATE-----",
                  strlen("-----BEGIN CERTIFICATE-----")) == 0 &&
           memcmp(private_key_pem,
                  "-----BEGIN ",
                  strlen("-----BEGIN ")) == 0;
}

bool load_identity(nvs_handle_t handle) {
    size_t certificate_size = sizeof(certificate_pem);
    size_t private_key_size = sizeof(private_key_pem);
    if (nvs_get_blob(handle,
                     kCertificateKey,
                     certificate_pem,
                     &certificate_size) != ESP_OK ||
        nvs_get_blob(handle,
                     kPrivateKeyKey,
                     private_key_pem,
                     &private_key_size) != ESP_OK) {
        return false;
    }
    certificate_length = certificate_size;
    private_key_length = private_key_size;
    return valid_loaded_pem();
}

bool generate_identity() {
    mbedtls_pk_context key;
    mbedtls_x509write_cert certificate;
    mbedtls_pk_init(&key);
    mbedtls_x509write_crt_init(&certificate);

    int result = mbedtls_pk_setup(
        &key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
    if (result == 0) {
        result = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1,
                                     mbedtls_pk_ec(key),
                                     random_callback,
                                     nullptr);
    }
    if (result == 0) {
        result = mbedtls_pk_write_key_pem(
            &key, private_key_pem, sizeof(private_key_pem));
    }

    uint8_t serial[16] = {};
    esp_fill_random(serial, sizeof(serial));
    serial[0] &= 0x7FU;
    serial[0] |= 0x01U;
    if (result == 0) {
        mbedtls_x509write_crt_set_version(
            &certificate, MBEDTLS_X509_CRT_VERSION_3);
        mbedtls_x509write_crt_set_md_alg(&certificate, MBEDTLS_MD_SHA256);
        mbedtls_x509write_crt_set_subject_key(&certificate, &key);
        mbedtls_x509write_crt_set_issuer_key(&certificate, &key);
        result = mbedtls_x509write_crt_set_subject_name(
            &certificate, "CN=aquahub.local,O=AquaCYD,C=PL");
    }
    if (result == 0) {
        result = mbedtls_x509write_crt_set_issuer_name(
            &certificate, "CN=aquahub.local,O=AquaCYD,C=PL");
    }
    if (result == 0) {
        result = mbedtls_x509write_crt_set_serial_raw(
            &certificate, serial, sizeof(serial));
    }
    if (result == 0) {
        result = mbedtls_x509write_crt_set_validity(
            &certificate, "20260101000000", "20451231235959");
    }
    if (result == 0) {
        result = mbedtls_x509write_crt_set_basic_constraints(
            &certificate, 0, -1);
    }
    if (result == 0) {
        result = mbedtls_x509write_crt_set_key_usage(
            &certificate,
            MBEDTLS_X509_KU_DIGITAL_SIGNATURE |
                MBEDTLS_X509_KU_KEY_AGREEMENT);
    }
    if (result == 0) {
        result = mbedtls_x509write_crt_set_subject_key_identifier(
            &certificate);
    }
    if (result == 0) {
        result = mbedtls_x509write_crt_set_authority_key_identifier(
            &certificate);
    }
    if (result == 0) {
        result = mbedtls_x509write_crt_pem(&certificate,
                                          certificate_pem,
                                          sizeof(certificate_pem),
                                          random_callback,
                                          nullptr);
    }

    if (result == 0) {
        certificate_length =
            strnlen(reinterpret_cast<const char *>(certificate_pem),
                    sizeof(certificate_pem)) +
            1U;
        private_key_length =
            strnlen(reinterpret_cast<const char *>(private_key_pem),
                    sizeof(private_key_pem)) +
            1U;
    }
    mbedtls_x509write_crt_free(&certificate);
    mbedtls_pk_free(&key);
    if (result != 0 || !valid_loaded_pem()) {
        ESP_LOGE(kTag, "Unable to generate TLS identity: -0x%04x", -result);
        memset(certificate_pem, 0, sizeof(certificate_pem));
        memset(private_key_pem, 0, sizeof(private_key_pem));
        certificate_length = 0U;
        private_key_length = 0U;
        return false;
    }
    return true;
}

bool store_identity(nvs_handle_t handle) {
    return nvs_set_blob(handle,
                        kCertificateKey,
                        certificate_pem,
                        certificate_length) == ESP_OK &&
           nvs_set_blob(handle,
                        kPrivateKeyKey,
                        private_key_pem,
                        private_key_length) == ESP_OK &&
           nvs_commit(handle) == ESP_OK;
}

bool calculate_fingerprint() {
    mbedtls_x509_crt parsed;
    mbedtls_x509_crt_init(&parsed);
    const int result = mbedtls_x509_crt_parse(
        &parsed, certificate_pem, certificate_length);
    if (result != 0 || parsed.raw.p == nullptr || parsed.raw.len == 0U) {
        mbedtls_x509_crt_free(&parsed);
        return false;
    }
    uint8_t digest[kSha256Bytes] = {};
    sha256(parsed.raw.p, parsed.raw.len, digest);
    bytes_to_hex(digest, sizeof(digest), fingerprint, sizeof(fingerprint));
    mbedtls_x509_crt_free(&parsed);
    return fingerprint[0] != '\0';
}

void load_token_hash(nvs_handle_t handle) {
    size_t length = sizeof(access_token_hash);
    access_token_configured =
        nvs_get_blob(handle,
                     kTokenHashKey,
                     access_token_hash,
                     &length) == ESP_OK &&
        length == sizeof(access_token_hash);
    if (!access_token_configured) {
        memset(access_token_hash, 0, sizeof(access_token_hash));
    }
}

} // namespace

bool aquahub_identity_initialize() {
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }
    if (!load_identity(handle)) {
        memset(certificate_pem, 0, sizeof(certificate_pem));
        memset(private_key_pem, 0, sizeof(private_key_pem));
        if (!generate_identity() || !store_identity(handle)) {
            nvs_close(handle);
            return false;
        }
    }
    load_token_hash(handle);
    nvs_close(handle);
    if (!calculate_fingerprint()) {
        return false;
    }
    aquahub_identity_rotate_pairing_code();
    ESP_LOGI(kTag,
             "TLS identity ready, certificate SHA-256 %.16s...",
             fingerprint);
    return true;
}

const uint8_t *aquahub_identity_certificate(size_t *length) {
    if (length != nullptr) {
        *length = certificate_length;
    }
    return certificate_length > 0U ? certificate_pem : nullptr;
}

const uint8_t *aquahub_identity_private_key(size_t *length) {
    if (length != nullptr) {
        *length = private_key_length;
    }
    return private_key_length > 0U ? private_key_pem : nullptr;
}

const char *aquahub_identity_fingerprint() {
    return fingerprint;
}

uint32_t aquahub_identity_pairing_code() {
    const uint64_t now = static_cast<uint64_t>(esp_timer_get_time());
    if (now >= pairing_expires_at_us) {
        return 0U;
    }
    return pairing_code;
}

uint32_t aquahub_identity_pairing_seconds_remaining() {
    const uint64_t now = static_cast<uint64_t>(esp_timer_get_time());
    if (now >= pairing_expires_at_us) {
        return 0U;
    }
    return static_cast<uint32_t>(
        (pairing_expires_at_us - now + 999999ULL) / 1000000ULL);
}

void aquahub_identity_rotate_pairing_code() {
    pairing_code = 100000U + esp_random() % 900000U;
    pairing_expires_at_us =
        static_cast<uint64_t>(esp_timer_get_time()) + kPairingLifetimeUs;
    pairing_locked_until_us = 0U;
    pairing_failures = 0U;
}

bool aquahub_identity_pair(uint32_t code,
                           char *token,
                           size_t token_capacity) {
    if (token == nullptr || token_capacity < kAquaHubAccessTokenBytes) {
        return false;
    }
    const uint64_t now = static_cast<uint64_t>(esp_timer_get_time());
    if (now < pairing_locked_until_us || now >= pairing_expires_at_us ||
        code == 0U || code != pairing_code) {
        if (now >= pairing_locked_until_us) {
            ++pairing_failures;
            if (pairing_failures >= kMaximumPairingFailures) {
                pairing_locked_until_us = now + kPairingLockoutUs;
                pairing_failures = 0U;
            }
        }
        return false;
    }

    uint8_t random_token[32] = {};
    esp_fill_random(random_token, sizeof(random_token));
    bytes_to_hex(random_token, sizeof(random_token), token, token_capacity);
    uint8_t digest[kSha256Bytes] = {};
    sha256(reinterpret_cast<const uint8_t *>(token),
           strlen(token),
           digest);
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        memset(token, 0, token_capacity);
        return false;
    }
    const bool stored =
        nvs_set_blob(handle, kTokenHashKey, digest, sizeof(digest)) == ESP_OK &&
        nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    if (!stored) {
        memset(token, 0, token_capacity);
        return false;
    }
    memcpy(access_token_hash, digest, sizeof(access_token_hash));
    access_token_configured = true;
    aquahub_identity_rotate_pairing_code();
    return true;
}

bool aquahub_identity_authorize(const char *authorization_header) {
    static constexpr char prefix[] = "Bearer ";
    if (!access_token_configured || authorization_header == nullptr ||
        strncmp(authorization_header, prefix, sizeof(prefix) - 1U) != 0) {
        return false;
    }
    const char *token = authorization_header + sizeof(prefix) - 1U;
    if (strnlen(token, kAquaHubAccessTokenBytes) !=
        kAquaHubAccessTokenBytes - 1U) {
        return false;
    }
    for (size_t index = 0U; index < kAquaHubAccessTokenBytes - 1U; ++index) {
        if (!((token[index] >= '0' && token[index] <= '9') ||
              (token[index] >= 'a' && token[index] <= 'f'))) {
            return false;
        }
    }
    uint8_t digest[kSha256Bytes] = {};
    sha256(reinterpret_cast<const uint8_t *>(token),
           kAquaHubAccessTokenBytes - 1U,
           digest);
    return constant_time_equal(
        digest, access_token_hash, sizeof(access_token_hash));
}
