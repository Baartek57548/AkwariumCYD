#include "aquahub_api.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aquahub_identity.h"
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
                       "GET, POST, OPTIONS");
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
    httpd_uri_t handlers[9] = {};
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
    handlers[8].uri = "/api/v1/*";
    handlers[8].method = HTTP_OPTIONS;
    handlers[8].handler = cors_handler;
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
    configuration.httpd.max_uri_handlers = 12U;
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
