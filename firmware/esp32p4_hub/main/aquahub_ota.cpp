#include "aquahub_ota.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"

namespace {

constexpr char kTag[] = "aquahub_ota";
constexpr size_t kManifestBytes = 4096U;
constexpr size_t kUrlBytes = 384U;
constexpr size_t kFileNameBytes = 96U;
constexpr size_t kVersionBytes = 33U;
constexpr size_t kReleaseIdBytes = 49U;
constexpr size_t kNotesBytes = 384U;
constexpr size_t kSha256HexBytes = 65U;
constexpr size_t kDownloadBufferBytes = 4096U;
constexpr uint32_t kMaximumFirmwareBytes = 5U * 1024U * 1024U;

enum class OtaPhase : uint8_t {
    Disabled = 0U,
    Idle,
    Checking,
    Available,
    UpToDate,
    Downloading,
    Verifying,
    Rebooting,
    Failed
};

struct ReleaseManifest {
    bool valid;
    bool mandatory;
    uint32_t size;
    uint32_t security_version;
    char release_id[kReleaseIdBytes];
    char version[kVersionBytes];
    char file[kFileNameBytes];
    char sha256[kSha256HexBytes];
    char notes[kNotesBytes];
};

struct OtaState {
    OtaPhase phase;
    uint32_t progress_percent;
    uint32_t bytes_received;
    uint32_t total_bytes;
    char error[160];
    ReleaseManifest release;
};

SemaphoreHandle_t state_mutex = nullptr;
OtaState state = {};
TaskHandle_t worker_task = nullptr;

class StateLock {
public:
    StateLock()
        : locked_(state_mutex != nullptr &&
                  xSemaphoreTake(state_mutex, pdMS_TO_TICKS(500U)) == pdTRUE) {
    }

    ~StateLock() {
        if (locked_) {
            xSemaphoreGive(state_mutex);
        }
    }

    bool locked() const {
        return locked_;
    }

private:
    bool locked_;
};

const char *phase_name(OtaPhase phase) {
    switch (phase) {
    case OtaPhase::Disabled:
        return "disabled";
    case OtaPhase::Idle:
        return "idle";
    case OtaPhase::Checking:
        return "checking";
    case OtaPhase::Available:
        return "available";
    case OtaPhase::UpToDate:
        return "up_to_date";
    case OtaPhase::Downloading:
        return "downloading";
    case OtaPhase::Verifying:
        return "verifying";
    case OtaPhase::Rebooting:
        return "rebooting";
    case OtaPhase::Failed:
        return "failed";
    }
    return "failed";
}

void set_failure(const char *message) {
    StateLock lock;
    if (!lock.locked()) {
        return;
    }
    state.phase = OtaPhase::Failed;
    state.progress_percent = 0U;
    state.bytes_received = 0U;
    state.total_bytes = 0U;
    strlcpy(state.error,
            message == nullptr ? "Nieznany błąd OTA." : message,
            sizeof(state.error));
}

bool copy_json_text(cJSON *root,
                    const char *name,
                    char *output,
                    size_t capacity,
                    bool allow_empty) {
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!cJSON_IsString(item) || item->valuestring == nullptr) {
        return false;
    }
    const size_t length = strnlen(item->valuestring, capacity);
    if (length >= capacity || (!allow_empty && length == 0U)) {
        return false;
    }
    memcpy(output, item->valuestring, length + 1U);
    return true;
}

bool valid_version(const char *value) {
    const size_t length = strnlen(value, kVersionBytes);
    if (length == 0U || length >= kVersionBytes) {
        return false;
    }
    for (size_t index = 0U; index < length; ++index) {
        const unsigned char character =
            static_cast<unsigned char>(value[index]);
        if (!isalnum(character) && character != '.' && character != '-' &&
            character != '+') {
            return false;
        }
    }
    return true;
}

bool valid_file_name(const char *value) {
    const size_t length = strnlen(value, kFileNameBytes);
    if (length == 0U || length >= kFileNameBytes || value[0] == '.') {
        return false;
    }
    for (size_t index = 0U; index < length; ++index) {
        const unsigned char character =
            static_cast<unsigned char>(value[index]);
        if (!isalnum(character) && character != '.' && character != '-' &&
            character != '_') {
            return false;
        }
    }
    return length > 4U && strcmp(value + length - 4U, ".bin") == 0;
}

bool valid_sha256(const char *value) {
    if (strnlen(value, kSha256HexBytes) != 64U) {
        return false;
    }
    for (size_t index = 0U; index < 64U; ++index) {
        if (!isxdigit(static_cast<unsigned char>(value[index]))) {
            return false;
        }
    }
    return true;
}

void normalize_sha256(char *value) {
    for (size_t index = 0U; index < 64U; ++index) {
        value[index] = static_cast<char>(
            toupper(static_cast<unsigned char>(value[index])));
    }
}

bool build_url(const char *suffix, char *output, size_t output_capacity) {
    if (suffix == nullptr || output == nullptr || output_capacity == 0U ||
        strncmp(CONFIG_AQUAHUB_OTA_BASE_URL, "https://", 8U) != 0) {
        return false;
    }
    const size_t base_length = strlen(CONFIG_AQUAHUB_OTA_BASE_URL);
    const bool base_has_slash =
        base_length > 0U && CONFIG_AQUAHUB_OTA_BASE_URL[base_length - 1U] == '/';
    const int written = snprintf(output,
                                 output_capacity,
                                 "%s%s%s",
                                 CONFIG_AQUAHUB_OTA_BASE_URL,
                                 base_has_slash ? "" : "/",
                                 suffix);
    return written > 0 && static_cast<size_t>(written) < output_capacity;
}

bool read_https_document(const char *url,
                         char *output,
                         size_t output_capacity,
                         size_t *output_length) {
    if (url == nullptr || output == nullptr || output_capacity < 2U ||
        output_length == nullptr) {
        return false;
    }
    esp_http_client_config_t configuration = {};
    configuration.url = url;
    configuration.timeout_ms = CONFIG_AQUAHUB_OTA_HTTP_TIMEOUT_MS;
    configuration.crt_bundle_attach = esp_crt_bundle_attach;
    configuration.disable_auto_redirect = true;
    configuration.keep_alive_enable = false;
    esp_http_client_handle_t client = esp_http_client_init(&configuration);
    if (client == nullptr) {
        return false;
    }
    bool success = false;
    if (esp_http_client_open(client, 0) == ESP_OK) {
        const int64_t content_length = esp_http_client_fetch_headers(client);
        const int status_code = esp_http_client_get_status_code(client);
        if (status_code == 200 && content_length >= 0 &&
            static_cast<uint64_t>(content_length) < output_capacity) {
            size_t offset = 0U;
            while (offset < static_cast<size_t>(content_length)) {
                const int received = esp_http_client_read(
                    client,
                    output + offset,
                    static_cast<int>(content_length) -
                        static_cast<int>(offset));
                if (received <= 0) {
                    offset = 0U;
                    break;
                }
                offset += static_cast<size_t>(received);
            }
            if (offset == static_cast<size_t>(content_length)) {
                output[offset] = '\0';
                *output_length = offset;
                success = true;
            }
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return success;
}

bool parse_manifest(const char *json,
                    size_t json_length,
                    ReleaseManifest *output) {
    if (json == nullptr || output == nullptr) {
        return false;
    }
    cJSON *root = cJSON_ParseWithLength(json, json_length);
    cJSON *target = cJSON_IsObject(root)
                        ? cJSON_GetObjectItemCaseSensitive(root, "target")
                        : nullptr;
    cJSON *size = cJSON_GetObjectItemCaseSensitive(root, "size");
    cJSON *security_version =
        cJSON_GetObjectItemCaseSensitive(root, "security_version");
    cJSON *mandatory = cJSON_GetObjectItemCaseSensitive(root, "mandatory");
    ReleaseManifest parsed = {};
    const bool valid = cJSON_IsString(target) &&
                       target->valuestring != nullptr &&
                       strcmp(target->valuestring, "aquahub-p4") == 0 &&
                       cJSON_IsNumber(size) && size->valuedouble > 0.0 &&
                       size->valuedouble <= kMaximumFirmwareBytes &&
                       size->valuedouble ==
                           static_cast<double>(
                               static_cast<uint32_t>(size->valuedouble)) &&
                       cJSON_IsNumber(security_version) &&
                       security_version->valuedouble >= 0.0 &&
                       security_version->valuedouble <= UINT32_MAX &&
                       security_version->valuedouble ==
                           static_cast<double>(static_cast<uint32_t>(
                               security_version->valuedouble)) &&
                       copy_json_text(root,
                                      "release_id",
                                      parsed.release_id,
                                      sizeof(parsed.release_id),
                                      false) &&
                       copy_json_text(root,
                                      "version",
                                      parsed.version,
                                      sizeof(parsed.version),
                                      false) &&
                       copy_json_text(root,
                                      "file",
                                      parsed.file,
                                      sizeof(parsed.file),
                                      false) &&
                       copy_json_text(root,
                                      "sha256",
                                      parsed.sha256,
                                      sizeof(parsed.sha256),
                                      false) &&
                       copy_json_text(root,
                                      "notes",
                                      parsed.notes,
                                      sizeof(parsed.notes),
                                      true);
    if (valid) {
        parsed.size = static_cast<uint32_t>(size->valuedouble);
        parsed.security_version =
            static_cast<uint32_t>(security_version->valuedouble);
        parsed.mandatory = cJSON_IsTrue(mandatory);
        parsed.valid = valid_version(parsed.version) &&
                       valid_file_name(parsed.file) &&
                       valid_sha256(parsed.sha256);
        normalize_sha256(parsed.sha256);
    }
    cJSON_Delete(root);
    if (!valid || !parsed.valid) {
        return false;
    }
    *output = parsed;
    return true;
}

void check_task(void *) {
    char url[kUrlBytes] = {};
    char manifest_json[kManifestBytes] = {};
    size_t manifest_length = 0U;
    ReleaseManifest manifest = {};
    bool success = build_url("manifest.json", url, sizeof(url)) &&
                   read_https_document(url,
                                       manifest_json,
                                       sizeof(manifest_json),
                                       &manifest_length) &&
                   parse_manifest(manifest_json,
                                  manifest_length,
                                  &manifest);
    memset(manifest_json, 0, sizeof(manifest_json));
    if (!success) {
        set_failure("Nie udało się pobrać lub zweryfikować manifestu OTA.");
    } else {
        const esp_app_desc_t *current = esp_app_get_description();
        StateLock lock;
        if (lock.locked()) {
            state.release = manifest;
            state.error[0] = '\0';
            state.progress_percent = 0U;
            state.bytes_received = 0U;
            state.total_bytes = manifest.size;
            if (manifest.security_version < current->secure_version) {
                state.phase = OtaPhase::Failed;
                strlcpy(state.error,
                        "Manifest próbuje obniżyć wersję bezpieczeństwa.",
                        sizeof(state.error));
            } else if (strcmp(manifest.version, current->version) == 0) {
                state.phase = OtaPhase::UpToDate;
            } else {
                state.phase = OtaPhase::Available;
            }
        }
    }
    worker_task = nullptr;
    vTaskDelete(nullptr);
}

bool digest_matches(const unsigned char digest[32], const char *expected) {
    char actual[kSha256HexBytes] = {};
    for (size_t index = 0U; index < 32U; ++index) {
        snprintf(actual + (index * 2U), 3U, "%02X", digest[index]);
    }
    uint8_t difference = 0U;
    for (size_t index = 0U; index < 64U; ++index) {
        difference |= static_cast<uint8_t>(actual[index] ^ expected[index]);
    }
    return difference == 0U;
}

void update_progress(uint32_t received, uint32_t total) {
    StateLock lock;
    if (!lock.locked()) {
        return;
    }
    state.bytes_received = received;
    state.total_bytes = total;
    state.progress_percent =
        total == 0U ? 0U : static_cast<uint32_t>(
                                 (static_cast<uint64_t>(received) * 100ULL) /
                                 total);
}

void install_task(void *) {
    ReleaseManifest manifest = {};
    {
        StateLock lock;
        if (lock.locked()) {
            manifest = state.release;
        }
    }
    char url[kUrlBytes] = {};
    if (!manifest.valid ||
        !build_url(manifest.file, url, sizeof(url))) {
        set_failure("Zweryfikowany manifest OTA nie jest dostępny.");
        worker_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    esp_http_client_config_t configuration = {};
    configuration.url = url;
    configuration.timeout_ms = CONFIG_AQUAHUB_OTA_HTTP_TIMEOUT_MS;
    configuration.crt_bundle_attach = esp_crt_bundle_attach;
    configuration.disable_auto_redirect = true;
    configuration.keep_alive_enable = false;
    esp_http_client_handle_t client = esp_http_client_init(&configuration);
    const esp_partition_t *partition = esp_ota_get_next_update_partition(nullptr);
    esp_ota_handle_t ota_handle = 0U;
    bool ota_started = false;
    bool success = client != nullptr && partition != nullptr;
    if (success) {
        success = esp_http_client_open(client, 0) == ESP_OK;
    }
    int64_t content_length = -1;
    if (success) {
        content_length = esp_http_client_fetch_headers(client);
        success = esp_http_client_get_status_code(client) == 200 &&
                  content_length == static_cast<int64_t>(manifest.size);
    }
    if (success) {
        success = esp_ota_begin(partition,
                                static_cast<size_t>(content_length),
                                &ota_handle) == ESP_OK;
        ota_started = success;
    }

    unsigned char *buffer = nullptr;
    if (success) {
        buffer = static_cast<unsigned char *>(malloc(kDownloadBufferBytes));
        success = buffer != nullptr;
    }
    mbedtls_sha256_context sha_context;
    mbedtls_sha256_init(&sha_context);
    if (success) {
        success = mbedtls_sha256_starts(&sha_context, 0) == 0;
    }
    uint32_t received_total = 0U;
    while (success && received_total < manifest.size) {
        const uint32_t remaining = manifest.size - received_total;
        const int requested = static_cast<int>(
            remaining < kDownloadBufferBytes ? remaining : kDownloadBufferBytes);
        const int received = esp_http_client_read(client,
                                                  reinterpret_cast<char *>(buffer),
                                                  requested);
        if (received <= 0 ||
            esp_ota_write(ota_handle,
                          buffer,
                          static_cast<size_t>(received)) != ESP_OK ||
            mbedtls_sha256_update(&sha_context,
                                  buffer,
                                  static_cast<size_t>(received)) != 0) {
            success = false;
            break;
        }
        received_total += static_cast<uint32_t>(received);
        update_progress(received_total, manifest.size);
    }
    unsigned char digest[32] = {};
    if (success) {
        success = received_total == manifest.size &&
                  mbedtls_sha256_finish(&sha_context, digest) == 0 &&
                  digest_matches(digest, manifest.sha256);
    }
    mbedtls_sha256_free(&sha_context);
    if (buffer != nullptr) {
        memset(buffer, 0, kDownloadBufferBytes);
        free(buffer);
    }
    if (client != nullptr) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
    }

    if (!success) {
        if (ota_started) {
            esp_ota_abort(ota_handle);
        }
        set_failure("Pobieranie lub suma SHA-256 firmware’u nie powiodły się.");
        worker_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }
    {
        StateLock lock;
        if (lock.locked()) {
            state.phase = OtaPhase::Verifying;
            state.progress_percent = 100U;
        }
    }
    if (esp_ota_end(ota_handle) != ESP_OK ||
        esp_ota_set_boot_partition(partition) != ESP_OK) {
        set_failure("ESP-IDF odrzucił obraz lub nie ustawił partycji startowej.");
        worker_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }
    {
        StateLock lock;
        if (lock.locked()) {
            state.phase = OtaPhase::Rebooting;
            state.error[0] = '\0';
        }
    }
    ESP_LOGI(kTag,
             "OTA release %s installed, rebooting",
             manifest.release_id);
    vTaskDelay(pdMS_TO_TICKS(CONFIG_AQUAHUB_OTA_REBOOT_DELAY_MS));
    esp_restart();
}

bool start_worker(TaskFunction_t function,
                  const char *name,
                  uint32_t stack_bytes,
                  OtaPhase phase) {
    StateLock lock;
    if (!lock.locked() || worker_task != nullptr ||
        state.phase == OtaPhase::Checking ||
        state.phase == OtaPhase::Downloading ||
        state.phase == OtaPhase::Verifying ||
        state.phase == OtaPhase::Rebooting) {
        return false;
    }
    state.phase = phase;
    state.error[0] = '\0';
    state.progress_percent = 0U;
    state.bytes_received = 0U;
    if (xTaskCreate(function,
                    name,
                    stack_bytes,
                    nullptr,
                    5U,
                    &worker_task) != pdPASS) {
        worker_task = nullptr;
        state.phase = OtaPhase::Failed;
        strlcpy(state.error,
                "Nie udało się utworzyć zadania OTA.",
                sizeof(state.error));
        return false;
    }
    return true;
}

} // namespace

bool aquahub_ota_initialize() {
    if (state_mutex != nullptr) {
        return true;
    }
    state_mutex = xSemaphoreCreateMutex();
    if (state_mutex == nullptr) {
        return false;
    }
    memset(&state, 0, sizeof(state));
#if CONFIG_AQUAHUB_OTA_ENABLED
    state.phase = CONFIG_AQUAHUB_OTA_BASE_URL[0] == '\0'
                      ? OtaPhase::Disabled
                      : OtaPhase::Idle;
#else
    state.phase = OtaPhase::Disabled;
#endif
    return true;
}

bool aquahub_ota_confirm_running_image() {
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == nullptr) {
        return false;
    }
    esp_ota_img_states_t image_state = ESP_OTA_IMG_UNDEFINED;
    const esp_err_t read_result =
        esp_ota_get_state_partition(running, &image_state);
    if (read_result == ESP_ERR_NOT_SUPPORTED ||
        image_state != ESP_OTA_IMG_PENDING_VERIFY) {
        return true;
    }
    const esp_err_t result = esp_ota_mark_app_valid_cancel_rollback();
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "Unable to confirm OTA image: %s", esp_err_to_name(result));
        return false;
    }
    ESP_LOGI(kTag, "Running OTA image confirmed after service self-test");
    return true;
}

bool aquahub_ota_request_check() {
#if CONFIG_AQUAHUB_OTA_ENABLED
    if (CONFIG_AQUAHUB_OTA_BASE_URL[0] == '\0') {
        return false;
    }
    return start_worker(check_task, "hub_ota_check", 8192U, OtaPhase::Checking);
#else
    return false;
#endif
}

bool aquahub_ota_request_install() {
#if CONFIG_AQUAHUB_OTA_ENABLED
    {
        StateLock lock;
        if (!lock.locked() || state.phase != OtaPhase::Available ||
            !state.release.valid) {
            return false;
        }
    }
    return start_worker(install_task,
                        "hub_ota_install",
                        12288U,
                        OtaPhase::Downloading);
#else
    return false;
#endif
}

bool aquahub_ota_write_json(char *output, size_t output_capacity) {
    if (output == nullptr || output_capacity < 128U) {
        return false;
    }
    OtaState snapshot = {};
    {
        StateLock lock;
        if (!lock.locked()) {
            return false;
        }
        snapshot = state;
    }
    const esp_app_desc_t *current = esp_app_get_description();
    cJSON *root = cJSON_CreateObject();
    if (root == nullptr) {
        return false;
    }
    cJSON_AddBoolToObject(root,
                          "supported",
                          snapshot.phase != OtaPhase::Disabled);
    cJSON_AddStringToObject(root, "target", "aquahub-p4");
    cJSON_AddStringToObject(root, "current_version", current->version);
    cJSON_AddNumberToObject(root,
                            "current_security_version",
                            current->secure_version);
    cJSON_AddStringToObject(root, "phase", phase_name(snapshot.phase));
    cJSON_AddNumberToObject(root,
                            "progress_percent",
                            snapshot.progress_percent);
    cJSON_AddNumberToObject(root,
                            "bytes_received",
                            snapshot.bytes_received);
    cJSON_AddNumberToObject(root, "total_bytes", snapshot.total_bytes);
    cJSON_AddStringToObject(root, "error", snapshot.error);
    if (snapshot.release.valid) {
        cJSON *release = cJSON_AddObjectToObject(root, "release");
        cJSON_AddStringToObject(release,
                                "release_id",
                                snapshot.release.release_id);
        cJSON_AddStringToObject(release,
                                "version",
                                snapshot.release.version);
        cJSON_AddNumberToObject(release, "size", snapshot.release.size);
        cJSON_AddNumberToObject(release,
                                "security_version",
                                snapshot.release.security_version);
        cJSON_AddBoolToObject(release,
                              "mandatory",
                              snapshot.release.mandatory);
        cJSON_AddStringToObject(release, "notes", snapshot.release.notes);
    } else {
        cJSON_AddNullToObject(root, "release");
    }
    const bool printed = cJSON_PrintPreallocated(
        root,
        output,
        static_cast<int>(output_capacity),
        false);
    cJSON_Delete(root);
    return printed;
}
