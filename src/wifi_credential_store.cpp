#include "wifi_credential_store.h"

#include <Preferences.h>
#include <esp_system.h>
#include <string.h>

namespace {

constexpr char WIFI_CREDENTIAL_NAMESPACE[] = "aq_wifi_sec";
constexpr char WIFI_LATEST_KEY[] = "latest";
constexpr uint32_t WIFI_CREDENTIAL_MAGIC = 0x32464957UL; // WIF2
constexpr uint16_t WIFI_CREDENTIAL_VERSION = 2U;

struct __attribute__((packed)) WifiCredentialRecord {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    char ssid[WIFI_CREDENTIAL_SSID_BYTES];
    char password[WIFI_CREDENTIAL_PASSWORD_BYTES];
    uint32_t nonce;
    uint32_t crc32;
};

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

uint32_t ssid_hash(const char *ssid) {
    uint32_t hash = 2166136261UL;
    if (ssid == nullptr) {
        return hash;
    }
    while (*ssid != '\0') {
        hash ^= static_cast<uint8_t>(*ssid++);
        hash *= 16777619UL;
    }
    return hash;
}

void credential_key(const char *ssid, char *out, size_t out_size) {
    if (out == nullptr || out_size == 0U) {
        return;
    }
    snprintf(
        out, out_size, "p%08lx",
        static_cast<unsigned long>(ssid_hash(ssid)));
}

bool valid_ssid(const char *ssid) {
    if (ssid == nullptr) {
        return false;
    }
    const size_t length = strnlen(ssid, WIFI_CREDENTIAL_SSID_BYTES);
    return length > 0U && length < WIFI_CREDENTIAL_SSID_BYTES;
}

bool valid_password(const char *password) {
    if (password == nullptr) {
        return false;
    }
    const size_t length =
        strnlen(password, WIFI_CREDENTIAL_PASSWORD_BYTES);
    return length < WIFI_CREDENTIAL_PASSWORD_BYTES &&
           (length == 0U || length >= 8U);
}

bool record_valid(const WifiCredentialRecord &record,
                  const char *expected_ssid) {
    return record.magic == WIFI_CREDENTIAL_MAGIC &&
           record.version == WIFI_CREDENTIAL_VERSION &&
           record.ssid[WIFI_CREDENTIAL_SSID_BYTES - 1U] == '\0' &&
           record.password[WIFI_CREDENTIAL_PASSWORD_BYTES - 1U] == '\0' &&
           valid_ssid(record.ssid) &&
           valid_password(record.password) &&
           (expected_ssid == nullptr ||
            strcmp(record.ssid, expected_ssid) == 0) &&
           record.crc32 ==
               crc32_bytes(
                   &record, sizeof(record) - sizeof(record.crc32));
}

void secure_clear(void *buffer, size_t length) {
    volatile uint8_t *bytes = static_cast<volatile uint8_t *>(buffer);
    while (length-- > 0U) {
        *bytes++ = 0U;
    }
}

bool load_record(Preferences &storage,
                 const char *key,
                 const char *expected_ssid,
                 WifiCredentialRecord *out) {
    if (key == nullptr || out == nullptr) {
        return false;
    }
    WifiCredentialRecord record = {};
    const size_t bytes = storage.getBytes(key, &record, sizeof(record));
    if (bytes != sizeof(record) ||
        !record_valid(record, expected_ssid)) {
        secure_clear(&record, sizeof(record));
        return false;
    }
    *out = record;
    secure_clear(&record, sizeof(record));
    return true;
}

} // namespace

bool wifi_credential_store_save(const char *ssid, const char *password) {
    if (!valid_ssid(ssid) || !valid_password(password)) {
        return false;
    }

    WifiCredentialRecord record = {};
    record.magic = WIFI_CREDENTIAL_MAGIC;
    record.version = WIFI_CREDENTIAL_VERSION;
    snprintf(record.ssid, sizeof(record.ssid), "%s", ssid);
    snprintf(record.password, sizeof(record.password), "%s", password);
    record.nonce = esp_random();
    record.crc32 =
        crc32_bytes(&record, sizeof(record) - sizeof(record.crc32));

    char key[12] = {};
    credential_key(ssid, key, sizeof(key));
    Preferences storage;
    if (!storage.begin(WIFI_CREDENTIAL_NAMESPACE, false)) {
        secure_clear(&record, sizeof(record));
        return false;
    }
    const bool saved =
        storage.putBytes(key, &record, sizeof(record)) == sizeof(record) &&
        storage.putString(WIFI_LATEST_KEY, ssid) == strlen(ssid);
    storage.end();
    secure_clear(&record, sizeof(record));
    return saved;
}

bool wifi_credential_store_load(const char *ssid,
                                char *password,
                                size_t password_size) {
    if (!valid_ssid(ssid) || password == nullptr ||
        password_size == 0U) {
        return false;
    }
    password[0] = '\0';
    char key[12] = {};
    credential_key(ssid, key, sizeof(key));
    Preferences storage;
    if (!storage.begin(WIFI_CREDENTIAL_NAMESPACE, true)) {
        return false;
    }
    WifiCredentialRecord record = {};
    const bool loaded = load_record(storage, key, ssid, &record);
    storage.end();
    if (!loaded ||
        strnlen(record.password, sizeof(record.password)) >=
            password_size) {
        secure_clear(&record, sizeof(record));
        return false;
    }
    snprintf(password, password_size, "%s", record.password);
    secure_clear(&record, sizeof(record));
    return true;
}

bool wifi_credential_store_load_latest(char *ssid,
                                       size_t ssid_size,
                                       char *password,
                                       size_t password_size) {
    if (ssid == nullptr || ssid_size == 0U ||
        password == nullptr || password_size == 0U) {
        return false;
    }
    ssid[0] = '\0';
    password[0] = '\0';

    Preferences storage;
    if (!storage.begin(WIFI_CREDENTIAL_NAMESPACE, true)) {
        return false;
    }
    char latest[WIFI_CREDENTIAL_SSID_BYTES] = {};
    const size_t latest_length =
        storage.getString(WIFI_LATEST_KEY, latest, sizeof(latest));
    char key[12] = {};
    credential_key(latest, key, sizeof(key));
    WifiCredentialRecord record = {};
    const bool loaded =
        latest_length > 0U &&
        latest_length < sizeof(latest) &&
        load_record(storage, key, latest, &record);
    storage.end();
    if (!loaded ||
        strlen(record.ssid) >= ssid_size ||
        strlen(record.password) >= password_size) {
        secure_clear(&record, sizeof(record));
        secure_clear(latest, sizeof(latest));
        return false;
    }
    snprintf(ssid, ssid_size, "%s", record.ssid);
    snprintf(password, password_size, "%s", record.password);
    secure_clear(&record, sizeof(record));
    secure_clear(latest, sizeof(latest));
    return true;
}

bool wifi_credential_store_clear(void) {
    Preferences storage;
    if (!storage.begin(WIFI_CREDENTIAL_NAMESPACE, false)) {
        return false;
    }
    const bool cleared = storage.clear();
    storage.end();
    return cleared;
}
