#include "remote_alarm_relay.h"

#include "alarm_event_queue.h"
#include "config.h"
#include "gui_app.h"
#include "hal_sd.h"

#include <Preferences.h>
#include <SD.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <mbedtls/base64.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>
#include <ctype.h>
#include <string.h>
#include <time.h>

namespace {

constexpr char RELAY_NAMESPACE[] = "aq_remote";
constexpr char RELAY_CONFIG_KEY[] = "config";
constexpr char RELAY_CURSOR_KEY[] = "cursor";
constexpr char RELAY_CA_PATH[] = "/aq/config/gateway-ca.pem";
constexpr uint32_t RELAY_CONFIG_MAGIC = 0x31524741UL; // AGR1
constexpr uint16_t RELAY_CONFIG_VERSION = 1U;
constexpr size_t RELAY_SECRET_MIN_BYTES = 32U;
constexpr size_t RELAY_SECRET_MAX_BYTES = 64U;
constexpr size_t RELAY_CA_BYTES = 6144U;
constexpr size_t RELAY_BODY_BYTES = 1024U;
constexpr size_t RELAY_PATH_BYTES = 320U;
constexpr uint32_t RELAY_TASK_STACK_BYTES = 8192U;
constexpr UBaseType_t RELAY_TASK_PRIORITY = 1U;
constexpr uint32_t RELAY_IDLE_PERIOD_MS = 1000U;
constexpr uint32_t RELAY_INITIAL_RETRY_MS = 5000U;
constexpr uint32_t RELAY_MAXIMUM_RETRY_MS = 30UL * 60UL * 1000UL;
constexpr uint32_t RELAY_HTTP_TIMEOUT_MS = 8000U;
constexpr uint32_t MINIMUM_VALID_EPOCH = 1704067200UL;

struct __attribute__((packed)) PersistentRelayConfiguration {
    uint32_t magic;
    uint16_t version;
    uint8_t enabled;
    uint8_t secret_length;
    char base_url[REMOTE_ALARM_RELAY_URL_BYTES];
    char device_id[REMOTE_ALARM_RELAY_DEVICE_ID_BYTES];
    uint8_t secret[RELAY_SECRET_MAX_BYTES];
    uint32_t crc32;
};

struct __attribute__((packed)) PersistentRelayCursor {
    uint32_t boot_id;
    uint32_t event_sequence;
};

struct ParsedHttpsUrl {
    char host[128];
    uint16_t port;
    char prefix[96];
};

struct AlarmDescriptor {
    unsigned int flag;
    const char *type;
    const char *title;
    const char *message;
    const char *severity;
};

constexpr AlarmDescriptor ALARM_DESCRIPTORS[] = {
    {1U << 0, "temperature.high", "Za wysoka temperatura",
     "Temperatura wody przekroczyla bezpieczny prog.", "critical"},
    {1U << 1, "temperature.low", "Za niska temperatura",
     "Temperatura wody spadla ponizej bezpiecznego progu.", "warning"},
    {1U << 2, "ph.out_of_range", "pH poza zakresem",
     "Pomiar pH wymaga sprawdzenia.", "warning"},
    {1U << 3, "water.level_low", "Niski poziom wody",
     "Uklad wykryl zbyt niski poziom wody.", "warning"},
    {1U << 4, "leak.detected", "Wykryto wyciek",
     "Czujnik zalania wykryl wode.", "critical"},
    {1U << 5, "supply.low", "Niskie napiecie zasilania",
     "Napiecie zasilania sterownika jest zbyt niskie.", "warning"},
    {1U << 6, "sensor.missing", "Brak wymaganego czujnika",
     "Wymagany czujnik nie odpowiada.", "critical"},
    {1U << 7, "sensor.stale", "Nieaktualny pomiar",
     "Sterownik nie otrzymal swiezego pomiaru.", "warning"},
    {1U << 8, "sensor.bus_fault", "Awaria magistrali czujnikow",
     "Komunikacja z czujnikami zostala przerwana.", "critical"},
    {1U << 9, "actuator.write_failed", "Blad elementu wykonawczego",
     "Nie potwierdzono bezpiecznego zapisu wyjscia.", "critical"},
};

StaticSemaphore_t relay_mutex_storage;
SemaphoreHandle_t relay_mutex =
    xSemaphoreCreateMutexStatic(&relay_mutex_storage);
PersistentRelayConfiguration relay_config = {};
PersistentRelayCursor delivered_cursor = {};
RemoteAlarmRelayStatus relay_status = {};
TaskHandle_t relay_task_handle = nullptr;
char relay_ca[RELAY_CA_BYTES] = {};
bool relay_ca_valid = false;
bool relay_initialized = false;

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

uint32_t configuration_crc(
    const PersistentRelayConfiguration &configuration) {
    return crc32_bytes(
        &configuration,
        sizeof(configuration) - sizeof(configuration.crc32));
}

bool configuration_valid(
    const PersistentRelayConfiguration &configuration) {
    const size_t base_url_length =
        strnlen(configuration.base_url, sizeof(configuration.base_url));
    const size_t device_id_length =
        strnlen(configuration.device_id, sizeof(configuration.device_id));
    return configuration.magic == RELAY_CONFIG_MAGIC &&
           configuration.version == RELAY_CONFIG_VERSION &&
           configuration.enabled <= 1U &&
           configuration.secret_length >= RELAY_SECRET_MIN_BYTES &&
           configuration.secret_length <= RELAY_SECRET_MAX_BYTES &&
           base_url_length > 0U &&
           base_url_length < sizeof(configuration.base_url) &&
           device_id_length > 0U &&
           device_id_length < sizeof(configuration.device_id) &&
           configuration.crc32 == configuration_crc(configuration);
}

bool lock_relay(uint32_t timeout_ms = 100U) {
    return relay_mutex != nullptr &&
           xSemaphoreTake(
               relay_mutex,
               pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void unlock_relay() {
    xSemaphoreGive(relay_mutex);
}

void secure_zero(void *buffer, size_t length) {
    volatile uint8_t *bytes =
        static_cast<volatile uint8_t *>(buffer);
    while (length-- > 0U) {
        *bytes++ = 0U;
    }
}

bool valid_device_id(const char *value) {
    if (value == nullptr) {
        return false;
    }
    const size_t length =
        strnlen(value, REMOTE_ALARM_RELAY_DEVICE_ID_BYTES);
    if (length < 4U ||
        length >= REMOTE_ALARM_RELAY_DEVICE_ID_BYTES) {
        return false;
    }
    for (size_t index = 0U; index < length; ++index) {
        const char c = value[index];
        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

bool valid_url_path(const char *value) {
    if (value == nullptr || value[0] != '/') {
        return false;
    }
    const size_t length = strlen(value);
    if (strstr(value, "//") != nullptr ||
        strstr(value, "/./") != nullptr ||
        strstr(value, "/../") != nullptr ||
        (length >= 2U &&
         strcmp(value + length - 2U, "/.") == 0) ||
        (length >= 3U &&
         strcmp(value + length - 3U, "/..") == 0)) {
        return false;
    }
    for (size_t index = 0U; value[index] != '\0'; ++index) {
        const char c = value[index];
        const bool unreserved =
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '/' || c == '-' || c == '.' ||
            c == '_' || c == '~';
        if (unreserved) {
            continue;
        }
        if (c == '%' &&
            value[index + 1U] != '\0' &&
            value[index + 2U] != '\0' &&
            isxdigit(static_cast<unsigned char>(
                value[index + 1U])) &&
            isxdigit(static_cast<unsigned char>(
                value[index + 2U]))) {
            index += 2U;
            continue;
        }
        return false;
    }
    return true;
}

bool parse_https_url(const char *value, ParsedHttpsUrl *out) {
    if (value == nullptr || out == nullptr ||
        strncmp(value, "https://", 8U) != 0) {
        return false;
    }
    const size_t total =
        strnlen(value, REMOTE_ALARM_RELAY_URL_BYTES);
    if (total <= 8U || total >= REMOTE_ALARM_RELAY_URL_BYTES ||
        strchr(value + 8U, '@') != nullptr ||
        strchr(value + 8U, '?') != nullptr ||
        strchr(value + 8U, '#') != nullptr) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->port = 443U;
    const char *authority = value + 8U;
    const char *path = strchr(authority, '/');
    const size_t authority_length =
        path == nullptr
            ? strlen(authority)
            : static_cast<size_t>(path - authority);
    if (authority_length == 0U ||
        authority_length >= sizeof(out->host)) {
        return false;
    }
    char authority_copy[144] = {};
    memcpy(authority_copy, authority, authority_length);
    authority_copy[authority_length] = '\0';
    char *port_separator = strrchr(authority_copy, ':');
    if (port_separator != nullptr) {
        *port_separator++ = '\0';
        char *end = nullptr;
        const unsigned long parsed_port =
            strtoul(port_separator, &end, 10);
        if (end == port_separator || *end != '\0' ||
            parsed_port == 0UL || parsed_port > 65535UL) {
            return false;
        }
        out->port = static_cast<uint16_t>(parsed_port);
    }
    const size_t host_length = strlen(authority_copy);
    if (host_length == 0U || host_length >= sizeof(out->host)) {
        return false;
    }
    for (size_t index = 0U; index < host_length; ++index) {
        const char c = authority_copy[index];
        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '.' || c == '-')) {
            return false;
        }
    }
    snprintf(out->host, sizeof(out->host), "%s", authority_copy);
    if (path != nullptr && strcmp(path, "/") != 0) {
        const size_t path_length = strlen(path);
        if (path_length >= sizeof(out->prefix) ||
            !valid_url_path(path)) {
            return false;
        }
        snprintf(out->prefix, sizeof(out->prefix), "%s", path);
        while (strlen(out->prefix) > 0U &&
               out->prefix[strlen(out->prefix) - 1U] == '/') {
            out->prefix[strlen(out->prefix) - 1U] = '\0';
        }
    }
    return true;
}

bool persist_configuration_locked() {
    relay_config.magic = RELAY_CONFIG_MAGIC;
    relay_config.version = RELAY_CONFIG_VERSION;
    relay_config.crc32 = configuration_crc(relay_config);
    Preferences storage;
    if (!storage.begin(RELAY_NAMESPACE, false)) {
        return false;
    }
    const bool saved =
        storage.putBytes(
            RELAY_CONFIG_KEY,
            &relay_config,
            sizeof(relay_config)) == sizeof(relay_config);
    storage.end();
    return saved;
}

bool persist_cursor(const PersistentRelayCursor &cursor) {
    Preferences storage;
    if (!storage.begin(RELAY_NAMESPACE, false)) {
        return false;
    }
    const bool saved =
        storage.putBytes(
            RELAY_CURSOR_KEY,
            &cursor,
            sizeof(cursor)) == sizeof(cursor);
    storage.end();
    return saved;
}

bool load_ca_certificate() {
    if (!gui_app_lock(500U)) {
        return false;
    }
    if (!lock_relay(500U)) {
        gui_app_unlock();
        return false;
    }
    relay_ca_valid = false;
    memset(relay_ca, 0, sizeof(relay_ca));
    if ((!hal_sd_is_mounted() && !hal_sd_init()) ||
        !SD.exists(RELAY_CA_PATH)) {
        unlock_relay();
        gui_app_unlock();
        return false;
    }
    File file = SD.open(RELAY_CA_PATH, FILE_READ);
    if (!file || file.isDirectory() ||
        file.size() < 256U ||
        file.size() >= sizeof(relay_ca)) {
        if (file) {
            file.close();
        }
        unlock_relay();
        gui_app_unlock();
        return false;
    }
    const size_t bytes =
        file.readBytes(relay_ca, file.size());
    file.close();
    relay_ca[bytes] = '\0';
    relay_ca_valid =
        bytes >= 256U &&
        strstr(relay_ca, "-----BEGIN CERTIFICATE-----") != nullptr &&
        strstr(relay_ca, "-----END CERTIFICATE-----") != nullptr &&
        strstr(relay_ca, "PRIVATE KEY") == nullptr;
    if (!relay_ca_valid) {
        secure_zero(relay_ca, sizeof(relay_ca));
    }
    const bool valid = relay_ca_valid;
    unlock_relay();
    gui_app_unlock();
    return valid;
}

bool ca_certificate_available() {
    if (!lock_relay()) {
        return false;
    }
    const bool available = relay_ca_valid;
    unlock_relay();
    return available;
}

void bytes_to_hex(const uint8_t *bytes,
                  size_t length,
                  char *out,
                  size_t out_size) {
    static constexpr char HEX_DIGITS[] = "0123456789abcdef";
    if (out == nullptr || out_size < length * 2U + 1U) {
        return;
    }
    for (size_t index = 0U; index < length; ++index) {
        out[index * 2U] =
            HEX_DIGITS[(bytes[index] >> 4U) & 0x0FU];
        out[index * 2U + 1U] =
            HEX_DIGITS[bytes[index] & 0x0FU];
    }
    out[length * 2U] = '\0';
}

bool sha256_hex(const char *value,
                size_t length,
                char out[65]) {
    uint8_t digest[32] = {};
    mbedtls_sha256_context context;
    mbedtls_sha256_init(&context);
    int result = mbedtls_sha256_starts_ret(&context, 0);
    if (result == 0) {
        result = mbedtls_sha256_update_ret(
            &context,
            reinterpret_cast<const unsigned char *>(value),
            length);
    }
    if (result == 0) {
        result = mbedtls_sha256_finish_ret(&context, digest);
    }
    mbedtls_sha256_free(&context);
    if (result != 0) {
        secure_zero(digest, sizeof(digest));
        return false;
    }
    bytes_to_hex(digest, sizeof(digest), out, 65U);
    secure_zero(digest, sizeof(digest));
    return true;
}

bool hmac_sha256_hex(const uint8_t *secret,
                     size_t secret_length,
                     const char *value,
                     size_t value_length,
                     char out[65]) {
    const mbedtls_md_info_t *info =
        mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    uint8_t digest[32] = {};
    if (info == nullptr ||
        mbedtls_md_hmac(
            info,
            secret,
            secret_length,
            reinterpret_cast<const unsigned char *>(value),
            value_length,
            digest) != 0) {
        secure_zero(digest, sizeof(digest));
        return false;
    }
    bytes_to_hex(digest, sizeof(digest), out, 65U);
    secure_zero(digest, sizeof(digest));
    return true;
}

const AlarmDescriptor *descriptor_for_flag(unsigned int flag) {
    for (const AlarmDescriptor &descriptor : ALARM_DESCRIPTORS) {
        if (descriptor.flag == flag) {
            return &descriptor;
        }
    }
    return nullptr;
}

uint8_t flag_index(unsigned int flag) {
    uint8_t index = 0U;
    while (flag > 1U) {
        flag >>= 1U;
        ++index;
    }
    return index;
}

bool build_event_body(const AlarmTransitionEvent &event,
                      const AlarmDescriptor &descriptor,
                      bool raised,
                      char *out,
                      size_t out_size) {
    char occurred_at[32] = {};
    if (event.timestamp_reliable &&
        event.timestamp >= MINIMUM_VALID_EPOCH) {
        const time_t raw = static_cast<time_t>(event.timestamp);
        struct tm utc = {};
        if (gmtime_r(&raw, &utc) != nullptr) {
            strftime(
                occurred_at,
                sizeof(occurred_at),
                "%Y-%m-%dT%H:%M:%SZ",
                &utc);
        }
    }
    const uint32_t sequence =
        event.event_sequence * 16UL +
        static_cast<uint32_t>(flag_index(descriptor.flag));
    const int written = occurred_at[0] == '\0'
        ? snprintf(
              out,
              out_size,
              "{\"eventId\":\"evt-%08lx-%08lx-%04x-%c\","
              "\"bootId\":\"boot-%08lx\",\"sequence\":%lu,"
              "\"type\":\"%s\",\"severity\":\"%s\",\"state\":\"%s\","
              "\"title\":\"%s\",\"message\":\"%s\"}",
              static_cast<unsigned long>(event.boot_id),
              static_cast<unsigned long>(event.event_sequence),
              static_cast<unsigned>(descriptor.flag),
              raised ? 'r' : 'c',
              static_cast<unsigned long>(event.boot_id),
              static_cast<unsigned long>(sequence),
              descriptor.type,
              descriptor.severity,
              raised ? "raised" : "resolved",
              descriptor.title,
              descriptor.message)
        : snprintf(
              out,
              out_size,
              "{\"eventId\":\"evt-%08lx-%08lx-%04x-%c\","
              "\"bootId\":\"boot-%08lx\",\"sequence\":%lu,"
              "\"type\":\"%s\",\"severity\":\"%s\",\"state\":\"%s\","
              "\"title\":\"%s\",\"message\":\"%s\","
              "\"occurredAt\":\"%s\"}",
              static_cast<unsigned long>(event.boot_id),
              static_cast<unsigned long>(event.event_sequence),
              static_cast<unsigned>(descriptor.flag),
              raised ? 'r' : 'c',
              static_cast<unsigned long>(event.boot_id),
              static_cast<unsigned long>(sequence),
              descriptor.type,
              descriptor.severity,
              raised ? "raised" : "resolved",
              descriptor.title,
              descriptor.message,
              occurred_at);
    return written > 0 &&
           static_cast<size_t>(written) < out_size;
}

bool read_http_status(WiFiClientSecure &client,
                      int *status_code) {
    char line[128] = {};
    size_t used = 0U;
    const uint32_t deadline = millis() + RELAY_HTTP_TIMEOUT_MS;
    while (static_cast<int32_t>(millis() - deadline) < 0) {
        while (client.available() > 0) {
            const int value = client.read();
            if (value < 0) {
                break;
            }
            if (value == '\n') {
                line[used] = '\0';
                int code = 0;
                if (sscanf(line, "HTTP/%*u.%*u %d", &code) == 1 &&
                    code >= 100 && code <= 599) {
                    *status_code = code;
                    return true;
                }
                return false;
            }
            if (value != '\r' && used + 1U < sizeof(line)) {
                line[used++] = static_cast<char>(value);
            }
        }
        if (!client.connected()) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(10U));
    }
    return false;
}

bool send_event(const PersistentRelayConfiguration &configuration,
                const ParsedHttpsUrl &url,
                const AlarmTransitionEvent &event,
                const AlarmDescriptor &descriptor,
                bool raised,
                RemoteAlarmRelayError *error) {
    char body[RELAY_BODY_BYTES] = {};
    char path[RELAY_PATH_BYTES] = {};
    char nonce[33] = {};
    char body_hash[65] = {};
    char canonical[640] = {};
    char signature[65] = {};
    if (!build_event_body(
            event, descriptor, raised, body, sizeof(body))) {
        *error = RemoteAlarmRelayError::InvalidConfiguration;
        return false;
    }
    const int path_length = snprintf(
        path,
        sizeof(path),
        "%s/api/v1/devices/%s/events",
        url.prefix,
        configuration.device_id);
    if (path_length <= 0 ||
        static_cast<size_t>(path_length) >= sizeof(path)) {
        *error = RemoteAlarmRelayError::InvalidConfiguration;
        return false;
    }
    const uint32_t random_values[4] = {
        esp_random(), esp_random(), esp_random(), esp_random()
    };
    snprintf(
        nonce,
        sizeof(nonce),
        "%08lx%08lx%08lx%08lx",
        static_cast<unsigned long>(random_values[0]),
        static_cast<unsigned long>(random_values[1]),
        static_cast<unsigned long>(random_values[2]),
        static_cast<unsigned long>(random_values[3]));
    const time_t current_time = time(nullptr);
    if (current_time < static_cast<time_t>(MINIMUM_VALID_EPOCH)) {
        *error = RemoteAlarmRelayError::ClockNotSynchronized;
        return false;
    }
    if (!sha256_hex(body, strlen(body), body_hash)) {
        *error = RemoteAlarmRelayError::InvalidConfiguration;
        return false;
    }
    const int canonical_length = snprintf(
        canonical,
        sizeof(canonical),
        "%lu\n%s\nPOST\n%s\n%s",
        static_cast<unsigned long>(current_time),
        nonce,
        path,
        body_hash);
    if (canonical_length <= 0 ||
        static_cast<size_t>(canonical_length) >= sizeof(canonical) ||
        !hmac_sha256_hex(
            configuration.secret,
            configuration.secret_length,
            canonical,
            static_cast<size_t>(canonical_length),
            signature)) {
        *error = RemoteAlarmRelayError::InvalidConfiguration;
        return false;
    }

    WiFiClientSecure client;
    client.setCACert(relay_ca);
    client.setHandshakeTimeout(
        (RELAY_HTTP_TIMEOUT_MS + 999U) / 1000U);
    client.setTimeout(RELAY_HTTP_TIMEOUT_MS);
    if (!client.connect(url.host, url.port)) {
        *error = RemoteAlarmRelayError::TlsConnectionFailed;
        return false;
    }
    char host_header[144] = {};
    const int host_header_length =
        url.port == 443U
            ? snprintf(
                  host_header,
                  sizeof(host_header),
                  "%s",
                  url.host)
            : snprintf(
                  host_header,
                  sizeof(host_header),
                  "%s:%u",
                  url.host,
                  static_cast<unsigned>(url.port));
    char request_headers[768] = {};
    const int header_length = snprintf(
        request_headers,
        sizeof(request_headers),
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: AquaCYD-Firmware/%s\r\n"
        "Content-Type: application/json\r\n"
        "Accept: application/json\r\n"
        "Connection: close\r\n"
        "Content-Length: %u\r\n"
        "X-AquaCYD-Timestamp: %lu\r\n"
        "X-AquaCYD-Nonce: %s\r\n"
        "X-AquaCYD-Signature: v1=%s\r\n\r\n",
        path,
        host_header,
        FirmwareInfo::VERSION,
        static_cast<unsigned>(strlen(body)),
        static_cast<unsigned long>(current_time),
        nonce,
        signature);
    const size_t body_length = strlen(body);
    const bool request_valid =
        host_header_length > 0 &&
        static_cast<size_t>(host_header_length) <
            sizeof(host_header) &&
        header_length > 0 &&
        static_cast<size_t>(header_length) <
            sizeof(request_headers);
    const bool request_sent =
        request_valid &&
        client.write(
            reinterpret_cast<const uint8_t *>(
                request_headers),
            static_cast<size_t>(header_length)) ==
            static_cast<size_t>(header_length) &&
        client.write(
            reinterpret_cast<const uint8_t *>(body),
            body_length) == body_length;
    if (!request_sent) {
        client.stop();
        *error = RemoteAlarmRelayError::TlsConnectionFailed;
        return false;
    }
    int status_code = 0;
    const bool response_received =
        read_http_status(client, &status_code);
    client.stop();
    if (!response_received ||
        status_code < 200 ||
        status_code >= 300) {
        *error = RemoteAlarmRelayError::HttpRejected;
        return false;
    }
    *error = RemoteAlarmRelayError::None;
    return true;
}

bool deliver_transition(
    const PersistentRelayConfiguration &configuration,
    const ParsedHttpsUrl &url,
    const AlarmTransitionEvent &event,
    RemoteAlarmRelayError *error) {
    for (const AlarmDescriptor &descriptor : ALARM_DESCRIPTORS) {
        if ((event.raised_flags & descriptor.flag) != 0U &&
            !send_event(
                configuration,
                url,
                event,
                descriptor,
                true,
                error)) {
            return false;
        }
        if ((event.cleared_flags & descriptor.flag) != 0U &&
            !send_event(
                configuration,
                url,
                event,
                descriptor,
                false,
                error)) {
            return false;
        }
    }
    return true;
}

void update_failure(RemoteAlarmRelayError error,
                    uint32_t retry_ms) {
    if (!lock_relay()) {
        return;
    }
    relay_status.last_error = error;
    if (relay_status.failed_attempts < UINT32_MAX) {
        ++relay_status.failed_attempts;
    }
    relay_status.next_retry_ms = millis() + retry_ms;
    unlock_relay();
}

void update_success(const AlarmTransitionEvent &event) {
    PersistentRelayCursor cursor = {
        event.boot_id,
        event.event_sequence
    };
    persist_cursor(cursor);
    if (!lock_relay()) {
        return;
    }
    delivered_cursor = cursor;
    relay_status.last_error = RemoteAlarmRelayError::None;
    relay_status.next_retry_ms = 0U;
    relay_status.last_success_epoch =
        static_cast<uint32_t>(time(nullptr));
    if (relay_status.delivered_events < UINT32_MAX) {
        ++relay_status.delivered_events;
    }
    unlock_relay();
}

void relay_task(void *) {
    uint32_t retry_ms = RELAY_INITIAL_RETRY_MS;
    for (;;) {
        PersistentRelayConfiguration configuration = {};
        PersistentRelayCursor cursor = {};
        bool enabled = false;
        uint32_t next_retry = 0U;
        if (lock_relay()) {
            cursor = delivered_cursor;
            enabled = relay_status.enabled &&
                      relay_status.provisioned;
            next_retry = relay_status.next_retry_ms;
            unlock_relay();
        }
        const uint32_t now_ms = millis();
        if (!enabled) {
            vTaskDelay(pdMS_TO_TICKS(RELAY_IDLE_PERIOD_MS));
            continue;
        }
        if (next_retry != 0U &&
            static_cast<int32_t>(now_ms - next_retry) < 0) {
            vTaskDelay(pdMS_TO_TICKS(RELAY_IDLE_PERIOD_MS));
            continue;
        }
        if (WiFi.status() != WL_CONNECTED) {
            update_failure(
                RemoteAlarmRelayError::WifiUnavailable,
                retry_ms);
            retry_ms = min(
                retry_ms * 2U,
                RELAY_MAXIMUM_RETRY_MS);
            vTaskDelay(pdMS_TO_TICKS(RELAY_IDLE_PERIOD_MS));
            continue;
        }
        if (time(nullptr) <
            static_cast<time_t>(MINIMUM_VALID_EPOCH)) {
            update_failure(
                RemoteAlarmRelayError::ClockNotSynchronized,
                retry_ms);
            retry_ms = min(
                retry_ms * 2U,
                RELAY_MAXIMUM_RETRY_MS);
            vTaskDelay(pdMS_TO_TICKS(RELAY_IDLE_PERIOD_MS));
            continue;
        }
        if (!ca_certificate_available() && !load_ca_certificate()) {
            update_failure(
                RemoteAlarmRelayError::MissingCaCertificate,
                retry_ms);
            retry_ms = min(
                retry_ms * 2U,
                RELAY_MAXIMUM_RETRY_MS);
            vTaskDelay(pdMS_TO_TICKS(RELAY_IDLE_PERIOD_MS));
            continue;
        }
        if (lock_relay()) {
            configuration = relay_config;
            cursor = delivered_cursor;
            unlock_relay();
        } else {
            update_failure(
                RemoteAlarmRelayError::StorageFailure,
                retry_ms);
            retry_ms = min(
                retry_ms * 2U,
                RELAY_MAXIMUM_RETRY_MS);
            vTaskDelay(pdMS_TO_TICKS(RELAY_IDLE_PERIOD_MS));
            continue;
        }
        ParsedHttpsUrl parsed = {};
        if (!parse_https_url(configuration.base_url, &parsed)) {
            update_failure(
                RemoteAlarmRelayError::InvalidConfiguration,
                RELAY_MAXIMUM_RETRY_MS);
            secure_zero(
                configuration.secret,
                sizeof(configuration.secret));
            vTaskDelay(pdMS_TO_TICKS(RELAY_IDLE_PERIOD_MS));
            continue;
        }

        AlarmTransitionEvent events[ALARM_EVENT_QUEUE_CAPACITY] = {};
        const size_t count = alarm_event_queue_snapshot(
            events, ALARM_EVENT_QUEUE_CAPACITY);
        size_t first_pending = 0U;
        if (cursor.boot_id != 0U || cursor.event_sequence != 0U) {
            bool cursor_found = false;
            for (size_t index = 0U; index < count; ++index) {
                if (events[index].boot_id == cursor.boot_id &&
                    events[index].event_sequence ==
                        cursor.event_sequence) {
                    first_pending = index + 1U;
                    cursor_found = true;
                    break;
                }
            }
            // If the bounded ring already evicted the cursor, resend its
            // contents. Stable event IDs make this safe at the gateway.
            if (!cursor_found) {
                first_pending = 0U;
            }
        }
        bool delivered_any = false;
        bool failed = false;
        for (size_t index = first_pending; index < count; ++index) {
            RemoteAlarmRelayError error =
                RemoteAlarmRelayError::None;
            if (!deliver_transition(
                    configuration,
                    parsed,
                    events[index],
                    &error)) {
                update_failure(error, retry_ms);
                retry_ms = min(
                    retry_ms * 2U,
                    RELAY_MAXIMUM_RETRY_MS);
                failed = true;
                break;
            }
            update_success(events[index]);
            delivered_any = true;
        }
        secure_zero(
            configuration.secret,
            sizeof(configuration.secret));
        if (!failed) {
            retry_ms = RELAY_INITIAL_RETRY_MS;
            if (!delivered_any && lock_relay()) {
                relay_status.last_error =
                    RemoteAlarmRelayError::None;
                relay_status.next_retry_ms = 0U;
                unlock_relay();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(RELAY_IDLE_PERIOD_MS));
    }
}

bool ensure_task_started() {
    if (relay_task_handle != nullptr) {
        return true;
    }
    const BaseType_t created = xTaskCreatePinnedToCore(
        relay_task,
        "remote_alarm",
        RELAY_TASK_STACK_BYTES,
        nullptr,
        RELAY_TASK_PRIORITY,
        &relay_task_handle,
        0);
    if (created != pdPASS) {
        relay_task_handle = nullptr;
        return false;
    }
    return true;
}

} // namespace

bool remote_alarm_relay_initialize(void) {
    PersistentRelayConfiguration loaded = {};
    PersistentRelayCursor cursor = {};
    Preferences storage;
    if (storage.begin(RELAY_NAMESPACE, true)) {
        storage.getBytes(
            RELAY_CONFIG_KEY, &loaded, sizeof(loaded));
        storage.getBytes(
            RELAY_CURSOR_KEY, &cursor, sizeof(cursor));
        storage.end();
    }
    if (!lock_relay()) {
        return false;
    }
    if (configuration_valid(loaded)) {
        relay_config = loaded;
    } else {
        memset(&relay_config, 0, sizeof(relay_config));
    }
    delivered_cursor = cursor;
    relay_status = {};
    relay_status.enabled =
        configuration_valid(relay_config) &&
        relay_config.enabled != 0U;
    relay_status.provisioned =
        configuration_valid(relay_config);
    relay_status.last_error = relay_status.provisioned
        ? (relay_status.enabled
               ? RemoteAlarmRelayError::None
               : RemoteAlarmRelayError::Disabled)
        : RemoteAlarmRelayError::MissingConfiguration;
    snprintf(
        relay_status.base_url,
        sizeof(relay_status.base_url),
        "%s",
        relay_status.provisioned ? relay_config.base_url : "");
    snprintf(
        relay_status.device_id,
        sizeof(relay_status.device_id),
        "%s",
        relay_status.provisioned ? relay_config.device_id : "");
    relay_initialized = true;
    unlock_relay();
    const bool ca_loaded = load_ca_certificate();
    if (lock_relay()) {
        relay_status.ca_certificate_loaded = ca_loaded;
        unlock_relay();
    }
    const bool task_started = ensure_task_started();
    if (lock_relay()) {
        relay_status.task_running = task_started;
        unlock_relay();
    }
    return task_started;
}

bool remote_alarm_relay_configure(const char *base_url,
                                  const char *device_id,
                                  const char *base64_secret,
                                  bool enabled) {
    ParsedHttpsUrl parsed = {};
    if (!parse_https_url(base_url, &parsed) ||
        !valid_device_id(device_id) ||
        base64_secret == nullptr) {
        return false;
    }
    const char *encoded = strncmp(
        base64_secret, "base64:", 7U) == 0
        ? base64_secret + 7U
        : base64_secret;
    const size_t encoded_length = strlen(encoded);
    if (encoded_length < 44U || encoded_length > 128U) {
        return false;
    }
    uint8_t decoded[RELAY_SECRET_MAX_BYTES] = {};
    size_t decoded_length = 0U;
    const int decode_result = mbedtls_base64_decode(
        decoded,
        sizeof(decoded),
        &decoded_length,
        reinterpret_cast<const unsigned char *>(encoded),
        encoded_length);
    if (decode_result != 0 ||
        decoded_length < RELAY_SECRET_MIN_BYTES ||
        decoded_length > RELAY_SECRET_MAX_BYTES) {
        secure_zero(decoded, sizeof(decoded));
        return false;
    }
    if (!lock_relay()) {
        secure_zero(decoded, sizeof(decoded));
        return false;
    }
    memset(&relay_config, 0, sizeof(relay_config));
    relay_config.magic = RELAY_CONFIG_MAGIC;
    relay_config.version = RELAY_CONFIG_VERSION;
    relay_config.enabled = enabled ? 1U : 0U;
    relay_config.secret_length =
        static_cast<uint8_t>(decoded_length);
    snprintf(
        relay_config.base_url,
        sizeof(relay_config.base_url),
        "%s",
        base_url);
    snprintf(
        relay_config.device_id,
        sizeof(relay_config.device_id),
        "%s",
        device_id);
    memcpy(
        relay_config.secret, decoded, decoded_length);
    const bool saved = persist_configuration_locked();
    relay_status.enabled = saved && enabled;
    relay_status.provisioned = saved;
    relay_status.last_error = saved
        ? (enabled
               ? RemoteAlarmRelayError::None
               : RemoteAlarmRelayError::Disabled)
        : RemoteAlarmRelayError::StorageFailure;
    relay_status.next_retry_ms = 0U;
    snprintf(
        relay_status.base_url,
        sizeof(relay_status.base_url),
        "%s",
        saved ? base_url : "");
    snprintf(
        relay_status.device_id,
        sizeof(relay_status.device_id),
        "%s",
        saved ? device_id : "");
    unlock_relay();
    secure_zero(decoded, sizeof(decoded));
    ensure_task_started();
    return saved;
}

bool remote_alarm_relay_set_enabled(bool enabled) {
    if (!relay_initialized && !remote_alarm_relay_initialize()) {
        return false;
    }
    if (!lock_relay()) {
        return false;
    }
    if (!configuration_valid(relay_config)) {
        relay_status.last_error =
            RemoteAlarmRelayError::MissingConfiguration;
        unlock_relay();
        return false;
    }
    relay_config.enabled = enabled ? 1U : 0U;
    const bool saved = persist_configuration_locked();
    relay_status.enabled = saved && enabled;
    relay_status.last_error = saved
        ? (enabled
               ? RemoteAlarmRelayError::None
               : RemoteAlarmRelayError::Disabled)
        : RemoteAlarmRelayError::StorageFailure;
    relay_status.next_retry_ms = 0U;
    unlock_relay();
    return saved;
}

bool remote_alarm_relay_clear(void) {
    if (!lock_relay()) {
        return false;
    }
    Preferences storage;
    const bool opened = storage.begin(RELAY_NAMESPACE, false);
    const bool cleared = opened && storage.clear();
    if (opened) {
        storage.end();
    }
    secure_zero(&relay_config, sizeof(relay_config));
    delivered_cursor = {};
    relay_status = {};
    relay_status.task_running = relay_task_handle != nullptr;
    relay_status.last_error =
        RemoteAlarmRelayError::MissingConfiguration;
    unlock_relay();
    return cleared;
}

RemoteAlarmRelayStatus remote_alarm_relay_status(void) {
    RemoteAlarmRelayStatus status = {};
    if (!lock_relay()) {
        status.last_error =
            RemoteAlarmRelayError::StorageFailure;
        return status;
    }
    status = relay_status;
    status.ca_certificate_loaded = relay_ca_valid;
    unlock_relay();
    return status;
}

const char *remote_alarm_relay_error_code(
    RemoteAlarmRelayError error) {
    switch (error) {
    case RemoteAlarmRelayError::Disabled:
        return "disabled";
    case RemoteAlarmRelayError::MissingConfiguration:
        return "missing_configuration";
    case RemoteAlarmRelayError::MissingCaCertificate:
        return "missing_ca_certificate";
    case RemoteAlarmRelayError::ClockNotSynchronized:
        return "clock_not_synchronized";
    case RemoteAlarmRelayError::WifiUnavailable:
        return "wifi_unavailable";
    case RemoteAlarmRelayError::TlsConnectionFailed:
        return "tls_connection_failed";
    case RemoteAlarmRelayError::HttpRejected:
        return "http_rejected";
    case RemoteAlarmRelayError::StorageFailure:
        return "storage_failure";
    case RemoteAlarmRelayError::InvalidConfiguration:
        return "invalid_configuration";
    case RemoteAlarmRelayError::None:
    default:
        return "none";
    }
}
