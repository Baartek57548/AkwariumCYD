#include "aquahub_broker.h"

#include <string.h>

#include "aquahub_identity.h"
#include "aquahub_service.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_tls.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mosq_broker.h"

namespace {

constexpr char kTag[] = "aquahub_broker";
constexpr size_t kCredentialBytes = 65U;

char configured_username[kCredentialBytes] = {};
char configured_password[kCredentialBytes] = {};
struct mosq_broker_config broker_configuration = {};
esp_tls_cfg_server_t broker_tls_configuration = {};
TaskHandle_t broker_task_handle = nullptr;
volatile bool broker_active = false;

bool copy_credential(char *output,
                     size_t output_capacity,
                     const char *input,
                     size_t minimum_length) {
    if (output == nullptr || input == nullptr) {
        return false;
    }
    const size_t length = strnlen(input, output_capacity);
    if (length < minimum_length || length >= output_capacity) {
        return false;
    }
    memcpy(output, input, length + 1U);
    return true;
}

bool constant_time_string_equal(const char *left,
                                size_t left_length,
                                const char *right,
                                size_t right_length) {
    const size_t maximum = left_length > right_length
                               ? left_length
                               : right_length;
    uint8_t difference =
        static_cast<uint8_t>(left_length != right_length);
    for (size_t index = 0U; index < maximum; ++index) {
        const uint8_t left_byte = index < left_length
                                      ? static_cast<uint8_t>(left[index])
                                      : 0U;
        const uint8_t right_byte = index < right_length
                                       ? static_cast<uint8_t>(right[index])
                                       : 0U;
        difference |= static_cast<uint8_t>(left_byte ^ right_byte);
    }
    return difference == 0U;
}

int connect_callback(const char *client_id,
                     const char *username,
                     const char *password,
                     int password_length) {
    if (client_id == nullptr || client_id[0] == '\0' || username == nullptr ||
        password == nullptr || password_length < 0) {
        return 1;
    }
    const size_t username_length =
        strnlen(username, sizeof(configured_username));
    const size_t expected_username_length = strlen(configured_username);
    const size_t expected_password_length = strlen(configured_password);
    const size_t supplied_password_length =
        static_cast<size_t>(password_length);
    const bool accepted =
        username_length < sizeof(configured_username) &&
        supplied_password_length < kCredentialBytes &&
        constant_time_string_equal(username,
                                   username_length,
                                   configured_username,
                                   expected_username_length) &&
        constant_time_string_equal(password,
                                   supplied_password_length,
                                   configured_password,
                                   expected_password_length);
    if (!accepted) {
        ESP_LOGW(kTag, "Rejected MQTT client %.24s", client_id);
    }
    return accepted ? 0 : 1;
}

void message_callback(char *client,
                      char *topic,
                      char *data,
                      int length,
                      int qos,
                      int retained) {
    if (topic == nullptr || data == nullptr || length < 0) {
        return;
    }
    aquahub_service_handle_mqtt(
        client,
        topic,
        data,
        static_cast<size_t>(length),
        qos,
        retained != 0,
        static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL);
}

void broker_task(void *) {
    broker_active = true;
    ESP_LOGI(kTag,
             "Starting local MQTTS broker on port %d",
             broker_configuration.port);
    const int result = mosq_broker_run(&broker_configuration);
    ESP_LOGW(kTag, "MQTTS broker stopped with code %d", result);
    broker_active = false;
    broker_task_handle = nullptr;
    vTaskDelete(nullptr);
}

void secure_clear_credentials() {
    volatile char *username = configured_username;
    volatile char *password = configured_password;
    for (size_t index = 0U; index < sizeof(configured_username); ++index) {
        username[index] = 0;
        password[index] = 0;
    }
}

} // namespace

bool aquahub_broker_start(const char *username,
                          const char *password,
                          uint16_t port) {
    if (broker_task_handle != nullptr || broker_active) {
        return true;
    }
    if (port == 0U || !copy_credential(configured_username,
                                      sizeof(configured_username),
                                      username,
                                      4U) ||
        !copy_credential(configured_password,
                         sizeof(configured_password),
                         password,
                         12U)) {
        secure_clear_credentials();
        return false;
    }
    size_t certificate_length = 0U;
    size_t private_key_length = 0U;
    const uint8_t *certificate =
        aquahub_identity_certificate(&certificate_length);
    const uint8_t *private_key =
        aquahub_identity_private_key(&private_key_length);
    if (certificate == nullptr || private_key == nullptr) {
        secure_clear_credentials();
        return false;
    }
    memset(&broker_tls_configuration, 0, sizeof(broker_tls_configuration));
    broker_tls_configuration.servercert_buf = certificate;
    broker_tls_configuration.servercert_bytes =
        static_cast<unsigned int>(certificate_length);
    broker_tls_configuration.serverkey_buf = private_key;
    broker_tls_configuration.serverkey_bytes =
        static_cast<unsigned int>(private_key_length);

    memset(&broker_configuration, 0, sizeof(broker_configuration));
    broker_configuration.host = "0.0.0.0";
    broker_configuration.port = static_cast<int>(port);
    broker_configuration.tls_cfg = &broker_tls_configuration;
    broker_configuration.handle_connect_cb = connect_callback;
    broker_configuration.handle_message_cb = message_callback;

    if (xTaskCreate(broker_task,
                    "aquahub_mqtt",
                    12288U,
                    nullptr,
                    5U,
                    &broker_task_handle) != pdPASS) {
        broker_task_handle = nullptr;
        secure_clear_credentials();
        return false;
    }
    return true;
}

void aquahub_broker_stop() {
    if (broker_task_handle != nullptr || broker_active) {
        mosq_broker_stop();
        for (uint8_t attempt = 0U;
             broker_task_handle != nullptr && attempt < 40U;
             ++attempt) {
            vTaskDelay(pdMS_TO_TICKS(25U));
        }
    }
    secure_clear_credentials();
}

bool aquahub_broker_running() {
    return broker_active;
}
