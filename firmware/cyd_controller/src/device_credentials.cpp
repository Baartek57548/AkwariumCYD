#include "device_credentials.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>
#include <mbedtls/sha256.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef AQUARIUM_FORCE_DEVELOPER_MODE
#define AQUARIUM_FORCE_DEVELOPER_MODE 0
#endif

namespace {

constexpr char NVS_NAMESPACE[] = "aq_security";
constexpr char NVS_KEY[] = "credentials";
constexpr uint32_t CREDENTIAL_MAGIC = 0x31435241UL; // ARC1
constexpr uint16_t CREDENTIAL_VERSION = 1U;
constexpr size_t PRODUCTION_PIN_DIGITS = 6U;
constexpr size_t MAX_PIN_DIGITS = 8U;
constexpr size_t OTA_PASSWORD_CHARS = 16U;
constexpr char DEVELOPMENT_PIN[] = "1234";
constexpr char DEVELOPMENT_OTA_PASSWORD[] = "admin1234";
constexpr char PASSWORD_ALPHABET[] =
    "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";

struct PersistentCredentials {
    uint32_t magic;
    uint16_t version;
    uint8_t pin_length;
    uint8_t setup_pin_pending;
    uint8_t pin_sha256[32];
    char setup_pin[MAX_PIN_DIGITS + 1U];
    char ota_password[OTA_PASSWORD_CHARS + 1U];
    uint32_t crc32;
};

PersistentCredentials credentials = {};
bool initialized = false;

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

uint32_t credential_crc(const PersistentCredentials &value) {
    return crc32_bytes(&value, sizeof(value) - sizeof(value.crc32));
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

bool sha256_text(const char *text, uint8_t out[32]) {
    if (text == nullptr || out == nullptr) {
        return false;
    }
    return mbedtls_sha256_ret(
               reinterpret_cast<const unsigned char *>(text),
               strlen(text),
               out,
               0) == 0;
}

bool pin_is_valid(const char *pin, size_t expected_length) {
    if (pin == nullptr ||
        expected_length < 4U ||
        expected_length > MAX_PIN_DIGITS ||
        strlen(pin) != expected_length) {
        return false;
    }
    for (size_t index = 0U; index < expected_length; ++index) {
        if (pin[index] < '0' || pin[index] > '9') {
            return false;
        }
    }
    return true;
}

bool password_is_valid(const char *password) {
    if (password == nullptr ||
        strnlen(password, OTA_PASSWORD_CHARS + 1U) != OTA_PASSWORD_CHARS) {
        return false;
    }
    for (size_t index = 0U; index < OTA_PASSWORD_CHARS; ++index) {
        if (strchr(PASSWORD_ALPHABET, password[index]) == nullptr) {
            return false;
        }
    }
    return true;
}

uint32_t random_below(uint32_t upper_bound) {
    if (upper_bound <= 1U) {
        return 0U;
    }
    const uint32_t rejection_limit =
        UINT32_MAX - (UINT32_MAX % upper_bound);
    uint32_t sample = 0U;
    do {
        sample = esp_random();
    } while (sample >= rejection_limit);
    return sample % upper_bound;
}

void generate_production_credentials() {
    memset(&credentials, 0, sizeof(credentials));
    credentials.magic = CREDENTIAL_MAGIC;
    credentials.version = CREDENTIAL_VERSION;
    credentials.pin_length = PRODUCTION_PIN_DIGITS;
    credentials.setup_pin_pending = 1U;
    credentials.setup_pin[0] =
        static_cast<char>('1' + random_below(9U));
    for (size_t index = 1U; index < PRODUCTION_PIN_DIGITS; ++index) {
        credentials.setup_pin[index] =
            static_cast<char>('0' + random_below(10U));
    }
    credentials.setup_pin[PRODUCTION_PIN_DIGITS] = '\0';
    sha256_text(credentials.setup_pin, credentials.pin_sha256);

    constexpr size_t alphabet_length = sizeof(PASSWORD_ALPHABET) - 1U;
    for (size_t index = 0U; index < OTA_PASSWORD_CHARS; ++index) {
        credentials.ota_password[index] =
            PASSWORD_ALPHABET[random_below(alphabet_length)];
    }
    credentials.ota_password[OTA_PASSWORD_CHARS] = '\0';
}

void load_development_credentials() {
    memset(&credentials, 0, sizeof(credentials));
    credentials.magic = CREDENTIAL_MAGIC;
    credentials.version = CREDENTIAL_VERSION;
    credentials.pin_length = sizeof(DEVELOPMENT_PIN) - 1U;
    snprintf(credentials.ota_password,
             sizeof(credentials.ota_password),
             "%s",
             DEVELOPMENT_OTA_PASSWORD);
    sha256_text(DEVELOPMENT_PIN, credentials.pin_sha256);
}

bool credentials_are_valid(const PersistentCredentials &value) {
    if (value.magic != CREDENTIAL_MAGIC ||
        value.version != CREDENTIAL_VERSION ||
        value.pin_length < 4U ||
        value.pin_length > MAX_PIN_DIGITS ||
        value.setup_pin_pending > 1U ||
        value.crc32 != credential_crc(value) ||
        !password_is_valid(value.ota_password)) {
        return false;
    }
    uint8_t nonzero_hash = 0U;
    for (uint8_t value_byte : value.pin_sha256) {
        nonzero_hash |= value_byte;
    }
    if (nonzero_hash == 0U) {
        return false;
    }
    return value.setup_pin_pending == 0U ||
           pin_is_valid(value.setup_pin, value.pin_length);
}

bool save_credentials() {
    credentials.magic = CREDENTIAL_MAGIC;
    credentials.version = CREDENTIAL_VERSION;
    credentials.crc32 = credential_crc(credentials);
    Preferences storage;
    if (!storage.begin(NVS_NAMESPACE, false)) {
        return false;
    }
    const bool stored =
        storage.putBytes(NVS_KEY, &credentials, sizeof(credentials)) ==
        sizeof(credentials);
    storage.end();
    return stored;
}

bool load_credentials() {
    PersistentCredentials stored = {};
    Preferences storage;
    if (!storage.begin(NVS_NAMESPACE, true)) {
        return false;
    }
    const size_t read =
        storage.getBytes(NVS_KEY, &stored, sizeof(stored));
    storage.end();
    if (read != sizeof(stored) || !credentials_are_valid(stored)) {
        return false;
    }
    credentials = stored;
    return true;
}

} // namespace

bool device_credentials_initialize() {
    if (initialized) {
        return true;
    }
#if AQUARIUM_FORCE_DEVELOPER_MODE
    load_development_credentials();
    initialized = true;
    return true;
#else
    if (!load_credentials()) {
        generate_production_credentials();
        if (!save_credentials()) {
            // The current boot remains usable and fail-closed from public
            // defaults; the setup screen warns again after the next boot.
            Serial.println(
                "SECURITY: unique credentials generated but NVS persistence failed.");
        }
    }
    initialized = true;
    return true;
#endif
}

bool device_credentials_admin_pin_matches(const char *candidate) {
    if (!initialized) {
        device_credentials_initialize();
    }
    if (!pin_is_valid(candidate, credentials.pin_length)) {
        return false;
    }
    uint8_t candidate_hash[32] = {};
    if (!sha256_text(candidate, candidate_hash)) {
        return false;
    }
    return constant_time_equal(
        candidate_hash,
        credentials.pin_sha256,
        sizeof(candidate_hash));
}

size_t device_credentials_admin_pin_length() {
    if (!initialized) {
        device_credentials_initialize();
    }
    return credentials.pin_length;
}

const char *device_credentials_setup_pin() {
    if (!initialized) {
        device_credentials_initialize();
    }
    return credentials.setup_pin_pending != 0U
               ? credentials.setup_pin
               : nullptr;
}

bool device_credentials_acknowledge_setup_pin() {
    if (!initialized) {
        device_credentials_initialize();
    }
    if (credentials.setup_pin_pending == 0U) {
        return true;
    }
    PersistentCredentials previous = credentials;
    credentials.setup_pin_pending = 0U;
    memset(credentials.setup_pin, 0, sizeof(credentials.setup_pin));
    if (save_credentials()) {
        return true;
    }
    credentials = previous;
    return false;
}

const char *device_credentials_ota_ap_password() {
    if (!initialized) {
        device_credentials_initialize();
    }
    return credentials.ota_password;
}

bool device_credentials_factory_reset() {
    Preferences storage;
    if (!storage.begin(NVS_NAMESPACE, false)) {
        return false;
    }
    const bool cleared = storage.clear();
    storage.end();
    memset(&credentials, 0, sizeof(credentials));
    initialized = false;
    return cleared;
}
