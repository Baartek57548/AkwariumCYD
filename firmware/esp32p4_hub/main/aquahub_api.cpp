#include "aquahub_api.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aquahub_identity.h"
#include "aquahub_ota.h"
#include "aquahub_service.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_https_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr char kTag[] = "aquahub_api";
constexpr size_t kAuthorizationBytes = 80U;
constexpr size_t kRequestBytes = 512U;
constexpr size_t kSystemResponseBytes = 2048U;
constexpr size_t kDevicesResponseBytes = 12288U;
constexpr size_t kEntitiesResponseBytes = 28672U;
constexpr size_t kHistoryResponseBytes = 28672U;
constexpr size_t kUpdatesResponseBytes = 2048U;
constexpr size_t kAutomationsResponseBytes = 12288U;
constexpr size_t kQueryBytes = 256U;
constexpr uint32_t kEventPollMs = 250U;

httpd_handle_t server = nullptr;
AquaHubPublishCallback publish_callback = nullptr;
void *publish_callback_context = nullptr;
TaskHandle_t event_task_handle = nullptr;
volatile bool event_task_stop = false;

void secure_zero(char *buffer, size_t length) {
    if (buffer == nullptr) {
        return;
    }
    volatile char *cursor = buffer;
    while (length-- > 0U) {
        *cursor++ = 0;
    }
}

bool receive_request_body(httpd_req_t *request,
                          char *buffer,
                          size_t capacity,
                          size_t *received_length) {
    if (request == nullptr || buffer == nullptr || received_length == nullptr ||
        request->content_len <= 0 ||
        static_cast<size_t>(request->content_len) >= capacity) {
        return false;
    }
    const size_t expected = static_cast<size_t>(request->content_len);
    size_t offset = 0U;
    uint8_t timeout_count = 0U;
    while (offset < expected) {
        const int received = httpd_req_recv(
            request, buffer + offset, expected - offset);
        if (received == HTTPD_SOCK_ERR_TIMEOUT && timeout_count < 3U) {
            ++timeout_count;
            continue;
        }
        if (received <= 0) {
            return false;
        }
        offset += static_cast<size_t>(received);
    }
    buffer[offset] = '\0';
    *received_length = offset;
    return true;
}

void prepare_json_response(httpd_req_t *request) {
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(request, "X-Frame-Options", "DENY");
    if (CONFIG_AQUAHUB_CORS_ORIGIN[0] != '\0') {
        httpd_resp_set_hdr(request,
                           "Access-Control-Allow-Origin",
                           CONFIG_AQUAHUB_CORS_ORIGIN);
        httpd_resp_set_hdr(request, "Vary", "Origin");
    }
}

esp_err_t send_error(httpd_req_t *request,
                     const char *status,
                     const char *code,
                     const char *message);

esp_err_t cors_handler(httpd_req_t *request) {
    if (CONFIG_AQUAHUB_CORS_ORIGIN[0] == '\0') {
        return send_error(request,
                          "403 Forbidden",
                          "cors_disabled",
                          "Dostęp przeglądarkowy z innej domeny jest wyłączony.");
    }
    char origin[192] = {};
    if (httpd_req_get_hdr_value_str(
            request, "Origin", origin, sizeof(origin)) != ESP_OK ||
        strcmp(origin, CONFIG_AQUAHUB_CORS_ORIGIN) != 0) {
        return send_error(request,
                          "403 Forbidden",
                          "origin_rejected",
                          "Domena aplikacji webowej nie jest dozwolona.");
    }
    httpd_resp_set_status(request, "204 No Content");
    httpd_resp_set_hdr(request,
                       "Access-Control-Allow-Origin",
                       CONFIG_AQUAHUB_CORS_ORIGIN);
    httpd_resp_set_hdr(request,
                       "Access-Control-Allow-Methods",
                       "GET, POST, DELETE, OPTIONS");
    httpd_resp_set_hdr(request,
                       "Access-Control-Allow-Headers",
                       "Authorization, Content-Type");
    httpd_resp_set_hdr(request, "Access-Control-Max-Age", "600");
    httpd_resp_set_hdr(request, "Vary", "Origin");
    return httpd_resp_send(request, nullptr, 0U);
}

esp_err_t send_error(httpd_req_t *request,
                     const char *status,
                     const char *code,
                     const char *message) {
    prepare_json_response(request);
    httpd_resp_set_status(request, status);
    char body[256] = {};
    const int written = snprintf(body,
                                 sizeof(body),
                                 "{\"error\":\"%s\",\"message\":\"%s\"}",
                                 code,
                                 message);
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(body)) {
        return httpd_resp_send_500(request);
    }
    return httpd_resp_send(request, body, written);
}

bool authorized(httpd_req_t *request) {
    char authorization[kAuthorizationBytes] = {};
    const bool present =
        httpd_req_get_hdr_value_str(request,
                                    "Authorization",
                                    authorization,
                                    sizeof(authorization)) == ESP_OK;
    const bool result =
        present && aquahub_identity_authorize(authorization);
    secure_zero(authorization, sizeof(authorization));
    return result;
}

esp_err_t require_authorization(httpd_req_t *request) {
    if (authorized(request)) {
        return ESP_OK;
    }
    httpd_resp_set_hdr(request,
                       "WWW-Authenticate",
                       "Bearer realm=\"AquaHub\"");
    return send_error(request,
                      "401 Unauthorized",
                      "unauthorized",
                      "Wymagany jest prawidłowy token AquaHub.");
}

char *allocate_response(size_t bytes) {
    char *buffer = static_cast<char *>(
        heap_caps_calloc(1U, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (buffer == nullptr) {
        buffer = static_cast<char *>(calloc(1U, bytes));
    }
    return buffer;
}

esp_err_t send_generated_json(httpd_req_t *request,
                              size_t capacity,
                              bool (*writer)(char *, size_t)) {
    char *response = allocate_response(capacity);
    if (response == nullptr) {
        return send_error(request,
                          "503 Service Unavailable",
                          "out_of_memory",
                          "Brak pamięci na odpowiedź API.");
    }
    const bool written = writer(response, capacity);
    if (!written) {
        free(response);
        return send_error(request,
                          "500 Internal Server Error",
                          "serialization_failed",
                          "Nie udało się utworzyć odpowiedzi.");
    }
    prepare_json_response(request);
    const esp_err_t result =
        httpd_resp_send(request, response, HTTPD_RESP_USE_STRLEN);
    free(response);
    return result;
}

bool write_system(char *output, size_t capacity) {
    return aquahub_service_write_system_json(
        output,
        capacity,
        static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL,
        esp_get_free_heap_size());
}

bool write_updates(char *output, size_t capacity) {
    return aquahub_ota_write_json(output, capacity);
}

bool write_automations(char *output, size_t capacity) {
    return aquahub_service_write_automations_json(output, capacity);
}

esp_err_t info_handler(httpd_req_t *request) {
    prepare_json_response(request);
    char body[384] = {};
    const int written = snprintf(
        body,
        sizeof(body),
        "{\"product\":\"aquahub-p4\",\"api_version\":1,"
        "\"hostname\":\"aquahub.local\",\"tls_fingerprint\":\"%s\","
        "\"pairing_available\":%s}",
        aquahub_identity_fingerprint(),
        aquahub_identity_pairing_code() != 0U ? "true" : "false");
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(body)) {
        return httpd_resp_send_500(request);
    }
    return httpd_resp_send(request, body, written);
}

esp_err_t pair_handler(httpd_req_t *request) {
    if (request->content_len <= 0 ||
        request->content_len >= static_cast<int>(kRequestBytes)) {
        return send_error(request,
                          "400 Bad Request",
                          "invalid_body",
                          "Nieprawidłowy rozmiar żądania parowania.");
    }
    char body[kRequestBytes] = {};
    size_t received = 0U;
    if (!receive_request_body(
            request, body, sizeof(body), &received)) {
        secure_zero(body, sizeof(body));
        return send_error(request,
                          "400 Bad Request",
                          "truncated_body",
                          "Żądanie parowania jest niekompletne.");
    }
    cJSON *root = cJSON_ParseWithLength(body, received);
    secure_zero(body, sizeof(body));
    cJSON *code = cJSON_IsObject(root)
                      ? cJSON_GetObjectItemCaseSensitive(root, "code")
                      : nullptr;
    if (!cJSON_IsNumber(code) || code->valuedouble < 100000.0 ||
        code->valuedouble > 999999.0 ||
        code->valuedouble !=
            static_cast<double>(static_cast<uint32_t>(code->valuedouble))) {
        cJSON_Delete(root);
        return send_error(request,
                          "400 Bad Request",
                          "invalid_code",
                          "Kod musi mieć dokładnie sześć cyfr.");
    }
    const uint32_t pairing_code =
        static_cast<uint32_t>(code->valuedouble);
    cJSON_Delete(root);

    char token[kAquaHubAccessTokenBytes] = {};
    if (!aquahub_identity_pair(pairing_code, token, sizeof(token))) {
        secure_zero(token, sizeof(token));
        return send_error(request,
                          "403 Forbidden",
                          "pairing_rejected",
                          "Kod wygasł, jest nieprawidłowy lub parowanie zablokowano.");
    }
    char response[512] = {};
    const int written = snprintf(
        response,
        sizeof(response),
        "{\"token\":\"%s\",\"token_type\":\"Bearer\","
        "\"tls_fingerprint\":\"%s\",\"api_version\":1}",
        token,
        aquahub_identity_fingerprint());
    secure_zero(token, sizeof(token));
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(response)) {
        secure_zero(response, sizeof(response));
        return httpd_resp_send_500(request);
    }
    prepare_json_response(request);
    const esp_err_t result = httpd_resp_send(request, response, written);
    secure_zero(response, sizeof(response));
    return result;
}

esp_err_t system_handler(httpd_req_t *request) {
    if (require_authorization(request) != ESP_OK) {
        return ESP_OK;
    }
    return send_generated_json(
        request, kSystemResponseBytes, write_system);
}

esp_err_t devices_handler(httpd_req_t *request) {
    if (require_authorization(request) != ESP_OK) {
        return ESP_OK;
    }
    return send_generated_json(request,
                               kDevicesResponseBytes,
                               aquahub_service_write_devices_json);
}

size_t query_size(const char *query,
                  const char *name,
                  size_t default_value,
                  size_t maximum) {
    char value[24] = {};
    if (query == nullptr ||
        httpd_query_key_value(query, name, value, sizeof(value)) != ESP_OK) {
        return default_value;
    }
    char *end = nullptr;
    const unsigned long parsed = strtoul(value, &end, 10);
    if (end == value || *end != '\0' || parsed > maximum) {
        return default_value;
    }
    return static_cast<size_t>(parsed);
}

esp_err_t entities_handler(httpd_req_t *request) {
    if (require_authorization(request) != ESP_OK) {
        return ESP_OK;
    }
    char query[kQueryBytes] = {};
    const bool has_query =
        httpd_req_get_url_query_str(request,
                                    query,
                                    sizeof(query)) == ESP_OK;
    const size_t offset =
        query_size(has_query ? query : nullptr, "offset", 0U, 128U);
    const size_t limit =
        query_size(has_query ? query : nullptr, "limit", 32U, 48U);
    char *response = allocate_response(kEntitiesResponseBytes);
    if (response == nullptr) {
        return send_error(request,
                          "503 Service Unavailable",
                          "out_of_memory",
                          "Brak pamięci na listę encji.");
    }
    const bool written = aquahub_service_write_entities_json(
        response, kEntitiesResponseBytes, offset, limit);
    if (!written) {
        free(response);
        return send_error(request,
                          "500 Internal Server Error",
                          "serialization_failed",
                          "Nie udało się utworzyć listy encji.");
    }
    prepare_json_response(request);
    const esp_err_t result =
        httpd_resp_send(request, response, HTTPD_RESP_USE_STRLEN);
    free(response);
    return result;
}

esp_err_t history_handler(httpd_req_t *request) {
    if (require_authorization(request) != ESP_OK) {
        return ESP_OK;
    }
    char query[kQueryBytes] = {};
    if (httpd_req_get_url_query_str(request,
                                    query,
                                    sizeof(query)) != ESP_OK) {
        return send_error(request,
                          "400 Bad Request",
                          "missing_query",
                          "Wymagany jest identyfikator encji.");
    }
    char entity_id[aquahub::kEntityIdBytes] = {};
    if (httpd_query_key_value(query,
                              "entity_id",
                              entity_id,
                              sizeof(entity_id)) != ESP_OK ||
        !aquahub::valid_identifier(entity_id, sizeof(entity_id))) {
        return send_error(request,
                          "400 Bad Request",
                          "invalid_entity",
                          "Nieprawidłowy identyfikator encji.");
    }
    const size_t limit = query_size(query, "limit", 180U, 512U);
    char *response = allocate_response(kHistoryResponseBytes);
    if (response == nullptr) {
        return send_error(request,
                          "503 Service Unavailable",
                          "out_of_memory",
                          "Brak pamięci na historię.");
    }
    const bool written = aquahub_service_write_history_json(
        response, kHistoryResponseBytes, entity_id, limit);
    if (!written) {
        free(response);
        return send_error(request,
                          "500 Internal Server Error",
                          "serialization_failed",
                          "Nie udało się utworzyć historii.");
    }
    prepare_json_response(request);
    const esp_err_t result =
        httpd_resp_send(request, response, HTTPD_RESP_USE_STRLEN);
    free(response);
    return result;
}

esp_err_t updates_handler(httpd_req_t *request) {
    if (require_authorization(request) != ESP_OK) {
        return ESP_OK;
    }
    return send_generated_json(
        request, kUpdatesResponseBytes, write_updates);
}

esp_err_t update_action_handler(httpd_req_t *request) {
    if (require_authorization(request) != ESP_OK) {
        return ESP_OK;
    }
    bool accepted = false;
    if (strcmp(request->uri, "/api/v1/updates/check") == 0) {
        accepted = aquahub_ota_request_check();
    } else if (strcmp(request->uri, "/api/v1/updates/install") == 0) {
        accepted = aquahub_ota_request_install();
    } else {
        return send_error(request,
                          "404 Not Found",
                          "not_found",
                          "Nie znaleziono operacji aktualizacji.");
    }
    if (!accepted) {
        return send_error(request,
                          "409 Conflict",
                          "update_rejected",
                          "Kanał OTA nie jest gotowy albo inna operacja już trwa.");
    }
    prepare_json_response(request);
    httpd_resp_set_status(request, "202 Accepted");
    return httpd_resp_sendstr(request, "{\"accepted\":true}");
}

bool parse_command_value(cJSON *item, aquahub::StateValue *output) {
    if (item == nullptr || output == nullptr) {
        return false;
    }
    memset(output, 0, sizeof(*output));
    output->valid = true;
    if (cJSON_IsBool(item)) {
        output->type = aquahub::ValueType::Boolean;
        output->boolean_value = cJSON_IsTrue(item);
        return true;
    }
    if (cJSON_IsNumber(item)) {
        output->type = aquahub::ValueType::Number;
        output->number_value = item->valuedouble;
        return true;
    }
    if (cJSON_IsString(item) && item->valuestring != nullptr) {
        const size_t length = strnlen(item->valuestring,
                                      sizeof(output->text_value));
        if (length >= sizeof(output->text_value)) {
            return false;
        }
        output->type = aquahub::ValueType::Text;
        memcpy(output->text_value, item->valuestring, length + 1U);
        return true;
    }
    if (cJSON_IsNull(item)) {
        output->type = aquahub::ValueType::None;
        output->valid = false;
        return true;
    }
    return false;
}

bool copy_rule_text(cJSON *object,
                    const char *name,
                    char *output,
                    size_t output_capacity) {
    cJSON *item = cJSON_IsObject(object)
                      ? cJSON_GetObjectItemCaseSensitive(object, name)
                      : nullptr;
    if (!cJSON_IsString(item) || item->valuestring == nullptr ||
        output == nullptr || output_capacity == 0U) {
        return false;
    }
    const size_t length = strnlen(item->valuestring, output_capacity);
    if (length == 0U || length >= output_capacity) {
        return false;
    }
    memcpy(output, item->valuestring, length + 1U);
    return true;
}

bool parse_comparison(cJSON *object,
                      aquahub::Comparison *comparison) {
    cJSON *item = cJSON_IsObject(object)
                      ? cJSON_GetObjectItemCaseSensitive(object, "comparison")
                      : nullptr;
    if (!cJSON_IsString(item) || item->valuestring == nullptr ||
        comparison == nullptr) {
        return false;
    }
    if (strcmp(item->valuestring, "changed") == 0) {
        *comparison = aquahub::Comparison::Changed;
        return true;
    }
    if (strcmp(item->valuestring, "equals") == 0) {
        *comparison = aquahub::Comparison::Equals;
        return true;
    }
    if (strcmp(item->valuestring, "above") == 0) {
        *comparison = aquahub::Comparison::Above;
        return true;
    }
    if (strcmp(item->valuestring, "below") == 0) {
        *comparison = aquahub::Comparison::Below;
        return true;
    }
    return false;
}

bool parse_automation_rule(cJSON *root,
                           aquahub::AutomationRule *output) {
    if (!cJSON_IsObject(root) || output == nullptr) {
        return false;
    }
    memset(output, 0, sizeof(*output));
    cJSON *enabled = cJSON_GetObjectItemCaseSensitive(root, "enabled");
    cJSON *cooldown = cJSON_GetObjectItemCaseSensitive(root, "cooldown_ms");
    cJSON *trigger = cJSON_GetObjectItemCaseSensitive(root, "trigger");
    cJSON *condition = cJSON_GetObjectItemCaseSensitive(root, "condition");
    cJSON *action = cJSON_GetObjectItemCaseSensitive(root, "action");
    if (!cJSON_IsBool(enabled) || !cJSON_IsNumber(cooldown) ||
        cooldown->valuedouble < 0.0 ||
        cooldown->valuedouble > 86400000.0 ||
        cooldown->valuedouble != static_cast<double>(
                                         static_cast<uint32_t>(
                                             cooldown->valuedouble)) ||
        !cJSON_IsObject(trigger) || !cJSON_IsObject(action) ||
        !copy_rule_text(root,
                        "id",
                        output->id,
                        sizeof(output->id)) ||
        !copy_rule_text(root,
                        "name",
                        output->name,
                        sizeof(output->name)) ||
        !copy_rule_text(trigger,
                        "entity_id",
                        output->trigger_entity,
                        sizeof(output->trigger_entity)) ||
        !parse_comparison(trigger, &output->trigger_comparison) ||
        !copy_rule_text(action,
                        "entity_id",
                        output->action_entity,
                        sizeof(output->action_entity)) ||
        !parse_command_value(
            cJSON_GetObjectItemCaseSensitive(action, "value"),
            &output->action_value) ||
        !output->action_value.valid) {
        return false;
    }
    output->enabled = cJSON_IsTrue(enabled);
    output->cooldown_ms = static_cast<uint32_t>(cooldown->valuedouble);
    cJSON *trigger_value =
        cJSON_GetObjectItemCaseSensitive(trigger, "value");
    if (trigger_value != nullptr) {
        if (!parse_command_value(trigger_value, &output->trigger_value)) {
            return false;
        }
    } else if (output->trigger_comparison !=
               aquahub::Comparison::Changed) {
        return false;
    }

    if (cJSON_IsObject(condition)) {
        output->condition_enabled = true;
        if (!copy_rule_text(condition,
                            "entity_id",
                            output->condition_entity,
                            sizeof(output->condition_entity)) ||
            !parse_comparison(condition,
                              &output->condition_comparison)) {
            return false;
        }
        cJSON *condition_value =
            cJSON_GetObjectItemCaseSensitive(condition, "value");
        if (condition_value != nullptr) {
            if (!parse_command_value(condition_value,
                                     &output->condition_value)) {
                return false;
            }
        } else if (output->condition_comparison !=
                   aquahub::Comparison::Changed) {
            return false;
        }
    } else if (!cJSON_IsNull(condition)) {
        return false;
    }
    return true;
}

esp_err_t automation_status_error(
    httpd_req_t *request,
    aquahub::AutomationStatus status) {
    switch (status) {
    case aquahub::AutomationStatus::Ok:
        prepare_json_response(request);
        return httpd_resp_sendstr(request, "{\"accepted\":true}");
    case aquahub::AutomationStatus::InvalidRule:
        return send_error(request,
                          "400 Bad Request",
                          "invalid_automation",
                          "Reguła automatyzacji jest nieprawidłowa.");
    case aquahub::AutomationStatus::CapacityReached:
        return send_error(request,
                          "409 Conflict",
                          "automation_capacity",
                          "Osiągnięto limit automatyzacji centrum.");
    case aquahub::AutomationStatus::NotFound:
        return send_error(request,
                          "404 Not Found",
                          "automation_not_found",
                          "Nie znaleziono automatyzacji.");
    case aquahub::AutomationStatus::PersistenceFailed:
        return send_error(request,
                          "503 Service Unavailable",
                          "automation_storage",
                          "Nie udało się bezpiecznie zapisać automatyzacji.");
    }
    return httpd_resp_send_500(request);
}

esp_err_t automations_handler(httpd_req_t *request) {
    if (require_authorization(request) != ESP_OK) {
        return ESP_OK;
    }
    return send_generated_json(request,
                               kAutomationsResponseBytes,
                               write_automations);
}

esp_err_t automation_upsert_handler(httpd_req_t *request) {
    if (require_authorization(request) != ESP_OK) {
        return ESP_OK;
    }
    if (request->content_len <= 0 ||
        request->content_len >= static_cast<int>(kRequestBytes)) {
        return send_error(request,
                          "400 Bad Request",
                          "invalid_body",
                          "Nieprawidłowy rozmiar automatyzacji.");
    }
    char body[kRequestBytes] = {};
    size_t received = 0U;
    if (!receive_request_body(request, body, sizeof(body), &received)) {
        secure_zero(body, sizeof(body));
        return send_error(request,
                          "400 Bad Request",
                          "truncated_body",
                          "Automatyzacja jest niekompletna.");
    }
    cJSON *root = cJSON_ParseWithLength(body, received);
    secure_zero(body, sizeof(body));
    aquahub::AutomationRule rule = {};
    const bool parsed = parse_automation_rule(root, &rule);
    cJSON_Delete(root);
    if (!parsed) {
        return automation_status_error(
            request, aquahub::AutomationStatus::InvalidRule);
    }
    return automation_status_error(
        request, aquahub_service_upsert_automation(rule));
}

esp_err_t automation_delete_handler(httpd_req_t *request) {
    if (require_authorization(request) != ESP_OK) {
        return ESP_OK;
    }
    static constexpr char prefix[] = "/api/v1/automations/";
    if (strncmp(request->uri, prefix, sizeof(prefix) - 1U) != 0) {
        return automation_status_error(
            request, aquahub::AutomationStatus::NotFound);
    }
    const char *id = request->uri + sizeof(prefix) - 1U;
    if (!aquahub::valid_identifier(id, aquahub::kAutomationIdBytes)) {
        return automation_status_error(
            request, aquahub::AutomationStatus::InvalidRule);
    }
    return automation_status_error(
        request, aquahub_service_remove_automation(id));
}

esp_err_t command_handler(httpd_req_t *request) {
    if (require_authorization(request) != ESP_OK) {
        return ESP_OK;
    }
    static constexpr char prefix[] = "/api/v1/entities/";
    static constexpr char suffix[] = "/command";
    const char *uri = request->uri;
    if (strncmp(uri, prefix, sizeof(prefix) - 1U) != 0) {
        return send_error(request,
                          "404 Not Found",
                          "not_found",
                          "Nie znaleziono encji.");
    }
    const char *start = uri + sizeof(prefix) - 1U;
    const char *end = strstr(start, suffix);
    if (end == nullptr || end == start ||
        end[strlen(suffix)] != '\0') {
        return send_error(request,
                          "404 Not Found",
                          "not_found",
                          "Nie znaleziono komendy.");
    }
    char entity_id[aquahub::kEntityIdBytes] = {};
    const size_t entity_length = static_cast<size_t>(end - start);
    if (entity_length >= sizeof(entity_id)) {
        return send_error(request,
                          "400 Bad Request",
                          "invalid_entity",
                          "Identyfikator encji jest za długi.");
    }
    memcpy(entity_id, start, entity_length);
    entity_id[entity_length] = '\0';
    if (!aquahub::valid_identifier(entity_id, sizeof(entity_id)) ||
        request->content_len <= 0 ||
        request->content_len >= static_cast<int>(kRequestBytes)) {
        return send_error(request,
                          "400 Bad Request",
                          "invalid_command",
                          "Nieprawidłowa encja lub treść komendy.");
    }

    char request_body[kRequestBytes] = {};
    size_t received = 0U;
    if (!receive_request_body(
            request, request_body, sizeof(request_body), &received)) {
        secure_zero(request_body, sizeof(request_body));
        return send_error(request,
                          "400 Bad Request",
                          "truncated_body",
                          "Komenda jest niekompletna.");
    }
    cJSON *root = cJSON_ParseWithLength(request_body,
                                       received);
    secure_zero(request_body, sizeof(request_body));
    cJSON *json_value = cJSON_IsObject(root)
                            ? cJSON_GetObjectItemCaseSensitive(root, "value")
                            : nullptr;
    aquahub::StateValue value = {};
    const bool parsed = parse_command_value(json_value, &value);
    cJSON_Delete(root);
    if (!parsed) {
        return send_error(request,
                          "400 Bad Request",
                          "invalid_value",
                          "Nieprawidłowa wartość komendy.");
    }

    char topic[aquahub::kTopicBytes] = {};
    char payload[160] = {};
    if (!aquahub_service_build_command(entity_id,
                                       value,
                                       topic,
                                       sizeof(topic),
                                       payload,
                                       sizeof(payload))) {
        return send_error(request,
                          "409 Conflict",
                          "command_rejected",
                          "Encja jest tylko do odczytu, krytyczna lub wartość jest poza zakresem.");
    }
    if (publish_callback == nullptr ||
        !publish_callback(topic,
                          payload,
                          1,
                          false,
                          publish_callback_context)) {
        return send_error(request,
                          "503 Service Unavailable",
                          "transport_unavailable",
                          "Broker nie przyjął komendy.");
    }
    prepare_json_response(request);
    return httpd_resp_sendstr(request, "{\"accepted\":true}");
}

esp_err_t websocket_handler(httpd_req_t *request) {
    if (request->method == HTTP_GET) {
        if (require_authorization(request) != ESP_OK) {
            return ESP_FAIL;
        }
        return ESP_OK;
    }
    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_TEXT;
    esp_err_t result = httpd_ws_recv_frame(request, &frame, 0U);
    if (result != ESP_OK || frame.len > 256U) {
        return ESP_FAIL;
    }
    uint8_t payload[257] = {};
    frame.payload = payload;
    result = httpd_ws_recv_frame(request, &frame, sizeof(payload) - 1U);
    if (result != ESP_OK) {
        return result;
    }
    static constexpr char pong[] = "{\"type\":\"pong\"}";
    httpd_ws_frame_t response = {};
    response.type = HTTPD_WS_TYPE_TEXT;
    response.payload = reinterpret_cast<uint8_t *>(
        const_cast<char *>(pong));
    response.len = sizeof(pong) - 1U;
    return httpd_ws_send_frame(request, &response);
}

void event_task(void *) {
    uint32_t last_revision = UINT32_MAX;
    while (!event_task_stop) {
        const AquaHubSummary summary = aquahub_service_summary();
        if (server != nullptr &&
            summary.registry_revision != last_revision) {
            last_revision = summary.registry_revision;
            char payload[192] = {};
            const int written = snprintf(
                payload,
                sizeof(payload),
                "{\"type\":\"registry_changed\",\"revision\":%" PRIu32
                ",\"devices\":%u,\"entities\":%u}",
                summary.registry_revision,
                static_cast<unsigned>(summary.device_count),
                static_cast<unsigned>(summary.entity_count));
            if (written > 0 &&
                static_cast<size_t>(written) < sizeof(payload)) {
                size_t client_count = 8U;
                int client_fds[8] = {};
                if (httpd_get_client_list(server,
                                          &client_count,
                                          client_fds) == ESP_OK) {
                    httpd_ws_frame_t frame = {};
                    frame.type = HTTPD_WS_TYPE_TEXT;
                    frame.payload = reinterpret_cast<uint8_t *>(payload);
                    frame.len = static_cast<size_t>(written);
                    for (size_t index = 0U;
                         index < client_count;
                         ++index) {
                        if (httpd_ws_get_fd_info(server, client_fds[index]) ==
                            HTTPD_WS_CLIENT_WEBSOCKET) {
                            httpd_ws_send_frame_async(server,
                                                      client_fds[index],
                                                      &frame);
                        }
                    }
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(kEventPollMs));
    }
    event_task_handle = nullptr;
    vTaskDelete(nullptr);
}

bool register_handlers() {
    httpd_uri_t handlers[15] = {};
    handlers[0].uri = "/api/v1/info";
    handlers[0].method = HTTP_GET;
    handlers[0].handler = info_handler;
    handlers[1].uri = "/api/v1/pair";
    handlers[1].method = HTTP_POST;
    handlers[1].handler = pair_handler;
    handlers[2].uri = "/api/v1/system";
    handlers[2].method = HTTP_GET;
    handlers[2].handler = system_handler;
    handlers[3].uri = "/api/v1/devices";
    handlers[3].method = HTTP_GET;
    handlers[3].handler = devices_handler;
    handlers[4].uri = "/api/v1/entities";
    handlers[4].method = HTTP_GET;
    handlers[4].handler = entities_handler;
    handlers[5].uri = "/api/v1/history";
    handlers[5].method = HTTP_GET;
    handlers[5].handler = history_handler;
    handlers[6].uri = "/api/v1/entities/*";
    handlers[6].method = HTTP_POST;
    handlers[6].handler = command_handler;
    handlers[7].uri = "/api/v1/events";
    handlers[7].method = HTTP_GET;
    handlers[7].handler = websocket_handler;
    handlers[7].is_websocket = true;
    handlers[8].uri = "/api/v1/updates";
    handlers[8].method = HTTP_GET;
    handlers[8].handler = updates_handler;
    handlers[9].uri = "/api/v1/updates/check";
    handlers[9].method = HTTP_POST;
    handlers[9].handler = update_action_handler;
    handlers[10].uri = "/api/v1/updates/install";
    handlers[10].method = HTTP_POST;
    handlers[10].handler = update_action_handler;
    handlers[11].uri = "/api/v1/automations";
    handlers[11].method = HTTP_GET;
    handlers[11].handler = automations_handler;
    handlers[12].uri = "/api/v1/automations";
    handlers[12].method = HTTP_POST;
    handlers[12].handler = automation_upsert_handler;
    handlers[13].uri = "/api/v1/automations/*";
    handlers[13].method = HTTP_DELETE;
    handlers[13].handler = automation_delete_handler;
    handlers[14].uri = "/api/v1/*";
    handlers[14].method = HTTP_OPTIONS;
    handlers[14].handler = cors_handler;
    for (const httpd_uri_t &handler : handlers) {
        if (httpd_register_uri_handler(server, &handler) != ESP_OK) {
            return false;
        }
    }
    return true;
}

} // namespace

bool aquahub_api_start(AquaHubPublishCallback publish,
                       void *publish_context) {
    if (server != nullptr) {
        return true;
    }
    size_t certificate_length = 0U;
    size_t private_key_length = 0U;
    const uint8_t *certificate =
        aquahub_identity_certificate(&certificate_length);
    const uint8_t *private_key =
        aquahub_identity_private_key(&private_key_length);
    if (certificate == nullptr || private_key == nullptr) {
        return false;
    }
    httpd_ssl_config_t configuration = HTTPD_SSL_CONFIG_DEFAULT();
    configuration.servercert = certificate;
    configuration.servercert_len = certificate_length;
    configuration.prvtkey_pem = private_key;
    configuration.prvtkey_len = private_key_length;
    configuration.httpd.server_port = CONFIG_AQUAHUB_API_PORT;
    configuration.httpd.max_uri_handlers = 18U;
    configuration.httpd.max_open_sockets = 6U;
    configuration.httpd.stack_size = 12288U;
    configuration.httpd.uri_match_fn = httpd_uri_match_wildcard;
    if (httpd_ssl_start(&server, &configuration) != ESP_OK) {
        server = nullptr;
        return false;
    }
    publish_callback = publish;
    publish_callback_context = publish_context;
    if (!register_handlers()) {
        httpd_ssl_stop(server);
        server = nullptr;
        publish_callback = nullptr;
        publish_callback_context = nullptr;
        return false;
    }
    event_task_stop = false;
    if (xTaskCreate(event_task,
                    "hub_events",
                    6144U,
                    nullptr,
                    4U,
                    &event_task_handle) != pdPASS) {
        httpd_ssl_stop(server);
        server = nullptr;
        publish_callback = nullptr;
        publish_callback_context = nullptr;
        return false;
    }
    ESP_LOGI(kTag,
             "AquaHub HTTPS API started on port %d",
             CONFIG_AQUAHUB_API_PORT);
    return true;
}

void aquahub_api_stop() {
    event_task_stop = true;
    for (uint8_t attempt = 0U;
         event_task_handle != nullptr && attempt < 20U;
         ++attempt) {
        vTaskDelay(pdMS_TO_TICKS(25U));
    }
    if (server != nullptr) {
        httpd_ssl_stop(server);
        server = nullptr;
    }
    publish_callback = nullptr;
    publish_callback_context = nullptr;
}

bool aquahub_api_running() {
    return server != nullptr;
}
