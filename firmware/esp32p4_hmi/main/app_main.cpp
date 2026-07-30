#include <inttypes.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "cJSON.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "hmi_ui.h"
#include "lvgl.h"
#include "mqtt_client.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace {

constexpr char kTag[] = "aquacyd_hmi";
constexpr size_t kTopicBytes = 160U;
constexpr size_t kCommandBytes = 224U;
constexpr size_t kStateJsonBytes = 1024U;
constexpr size_t kAcknowledgementJsonBytes = 512U;
constexpr uint32_t kCommandUiTimeoutMs = 7000U;
constexpr char kPreferencesNamespace[] = "aquacyd-hmi";
constexpr char kBrightnessKey[] = "brightness";
constexpr uint8_t kDefaultBrightness = 80U;
constexpr uint8_t kMinimumBrightness = 10U;
constexpr EventBits_t kWifiConnectedBit = 1U << 0U;
constexpr EventBits_t kMqttConnectedBit = 1U << 1U;

struct OutgoingCommand {
    size_t length;
    char json[kCommandBytes];
};

struct CommandFeedback {
    uint64_t command_id;
    uint8_t status;
    uint16_t reason_code;
    uint32_t configuration_revision;
    bool transport_timeout;
};

EventGroupHandle_t connection_events = nullptr;
QueueHandle_t snapshot_queue = nullptr;
QueueHandle_t outgoing_command_queue = nullptr;
QueueHandle_t feedback_queue = nullptr;
esp_mqtt_client_handle_t mqtt_client = nullptr;

char state_topic[kTopicBytes] = {};
char availability_topic[kTopicBytes] = {};
char command_topic[kTopicBytes] = {};
char acknowledgement_topic[kTopicBytes] = {};
char hmi_availability_topic[kTopicBytes] = {};

volatile bool controller_online = false;
HmiSnapshot latest_snapshot = {};

uint64_t pending_command_id = 0U;
uint32_t pending_command_started_ms = 0U;
uint8_t display_brightness = kDefaultBrightness;

uint32_t monotonic_ms() {
    return static_cast<uint32_t>(
        static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL);
}

int hexadecimal_value(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

bool parse_command_id(const char *text, uint64_t *output) {
    if (text == nullptr || output == nullptr || strlen(text) != 16U) {
        return false;
    }
    uint64_t parsed = 0U;
    for (size_t index = 0U; index < 16U; ++index) {
        const int digit = hexadecimal_value(text[index]);
        if (digit < 0) {
            return false;
        }
        parsed = (parsed << 4U) | static_cast<uint64_t>(digit);
    }
    if (parsed == 0U) {
        return false;
    }
    *output = parsed;
    return true;
}

uint64_t create_command_id() {
    uint64_t value =
        (static_cast<uint64_t>(esp_random()) << 32U) |
        static_cast<uint64_t>(esp_random());
    value ^= static_cast<uint64_t>(esp_timer_get_time());
    return value == 0U ? 1U : value;
}

bool append_topic(char *destination,
                  size_t destination_size,
                  const char *base,
                  const char *suffix) {
    if (destination == nullptr ||
        destination_size == 0U ||
        base == nullptr ||
        suffix == nullptr) {
        return false;
    }
    const int written =
        snprintf(destination, destination_size, "%s/%s", base, suffix);
    return written > 0 && static_cast<size_t>(written) < destination_size;
}

bool initialize_topics() {
    if (strlen(CONFIG_AQUACYD_HMI_WIFI_SSID) == 0U ||
        strlen(CONFIG_AQUACYD_HMI_WIFI_PASSWORD) == 0U ||
        strlen(CONFIG_AQUACYD_HMI_MQTT_BROKER_URI) == 0U ||
        strlen(CONFIG_AQUACYD_HMI_MQTT_USERNAME) == 0U ||
        strlen(CONFIG_AQUACYD_HMI_MQTT_PASSWORD) == 0U ||
        strlen(CONFIG_AQUACYD_HMI_MQTT_BASE_TOPIC) == 0U) {
        ESP_LOGE(kTag, "Wi-Fi and MQTT credentials must not be empty");
        return false;
    }
    return append_topic(
               state_topic,
               sizeof(state_topic),
               CONFIG_AQUACYD_HMI_MQTT_BASE_TOPIC,
               "state") &&
           append_topic(
               availability_topic,
               sizeof(availability_topic),
               CONFIG_AQUACYD_HMI_MQTT_BASE_TOPIC,
               "availability") &&
           append_topic(
               command_topic,
               sizeof(command_topic),
               CONFIG_AQUACYD_HMI_MQTT_BASE_TOPIC,
               "command/set") &&
           append_topic(
               acknowledgement_topic,
               sizeof(acknowledgement_topic),
               CONFIG_AQUACYD_HMI_MQTT_BASE_TOPIC,
               "command/ack") &&
           append_topic(
               hmi_availability_topic,
               sizeof(hmi_availability_topic),
               CONFIG_AQUACYD_HMI_MQTT_BASE_TOPIC,
               "hmi/availability");
}

bool topic_matches(const esp_mqtt_event_handle_t event, const char *topic) {
    return event != nullptr &&
           topic != nullptr &&
           event->topic_len > 0 &&
           static_cast<size_t>(event->topic_len) == strlen(topic) &&
           memcmp(event->topic, topic, static_cast<size_t>(event->topic_len)) ==
               0;
}

bool json_number(cJSON *root, const char *name, double *output) {
    if (root == nullptr || name == nullptr || output == nullptr) {
        return false;
    }
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) {
        return false;
    }
    *output = item->valuedouble;
    return true;
}

bool json_integer(cJSON *root,
                  const char *name,
                  int64_t minimum,
                  int64_t maximum,
                  int64_t *output) {
    double value = 0.0;
    if (!json_number(root, name, &value) || output == nullptr) {
        return false;
    }
    if (value < static_cast<double>(minimum) ||
        value > static_cast<double>(maximum)) {
        return false;
    }
    const int64_t integer = static_cast<int64_t>(value);
    if (value != static_cast<double>(integer) ||
        integer < minimum ||
        integer > maximum) {
        return false;
    }
    *output = integer;
    return true;
}

bool json_boolean(cJSON *root, const char *name, bool *output) {
    if (root == nullptr || name == nullptr || output == nullptr) {
        return false;
    }
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!cJSON_IsBool(item)) {
        return false;
    }
    *output = cJSON_IsTrue(item);
    return true;
}

bool json_schedule(cJSON *root,
                   const char *prefix,
                   HmiSchedule *output) {
    if (root == nullptr || prefix == nullptr || output == nullptr) {
        return false;
    }
    char key[48] = {};
    int64_t mode = 0;
    int64_t profile = 0;
    int64_t start = 0;
    int64_t end = 0;
    const int mode_length =
        snprintf(key, sizeof(key), "%s_mode", prefix);
    if (mode_length <= 0 ||
        static_cast<size_t>(mode_length) >= sizeof(key) ||
        !json_integer(root, key, 0, 2, &mode)) {
        return false;
    }
    snprintf(key, sizeof(key), "%s_profile", prefix);
    if (!json_integer(root, key, 0, 3, &profile)) {
        return false;
    }
    snprintf(key, sizeof(key), "%s_start_minute", prefix);
    if (!json_integer(root, key, 0, 1439, &start)) {
        return false;
    }
    snprintf(key, sizeof(key), "%s_end_minute", prefix);
    if (!json_integer(root, key, 0, 1439, &end)) {
        return false;
    }
    output->mode = static_cast<uint8_t>(mode);
    output->profile = static_cast<uint8_t>(profile);
    output->start_minute = static_cast<uint16_t>(start);
    output->end_minute = static_cast<uint16_t>(end);
    return true;
}

uint8_t load_display_brightness() {
    nvs_handle_t handle = 0;
    const esp_err_t open_result =
        nvs_open(kPreferencesNamespace, NVS_READONLY, &handle);
    if (open_result == ESP_ERR_NVS_NOT_FOUND) {
        return kDefaultBrightness;
    }
    if (open_result != ESP_OK) {
        ESP_LOGW(
            kTag,
            "Unable to open HMI preferences: %s",
            esp_err_to_name(open_result));
        return kDefaultBrightness;
    }
    uint8_t value = kDefaultBrightness;
    const esp_err_t read_result = nvs_get_u8(handle, kBrightnessKey, &value);
    nvs_close(handle);
    if (read_result != ESP_OK ||
        value < kMinimumBrightness ||
        value > 100U) {
        return kDefaultBrightness;
    }
    return value;
}

bool save_display_brightness(uint8_t value) {
    if (value < kMinimumBrightness || value > 100U) {
        return false;
    }
    nvs_handle_t handle = 0;
    esp_err_t result =
        nvs_open(kPreferencesNamespace, NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "Unable to open HMI preferences for write: %s",
            esp_err_to_name(result));
        return false;
    }
    result = nvs_set_u8(handle, kBrightnessKey, value);
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    if (result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "Unable to save display brightness: %s",
            esp_err_to_name(result));
        return false;
    }
    return true;
}

void queue_latest_snapshot(const HmiSnapshot &snapshot) {
    if (snapshot_queue != nullptr) {
        xQueueOverwrite(snapshot_queue, &snapshot);
    }
}

void parse_state_message(const char *json, size_t length) {
    if (json == nullptr || length == 0U || length > kStateJsonBytes) {
        return;
    }
    cJSON *root = cJSON_ParseWithLength(json, length);
    if (root == nullptr || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        ESP_LOGW(kTag, "Rejected malformed state JSON");
        return;
    }

    HmiSnapshot parsed = latest_snapshot;
    parsed.controller_online = controller_online;
    parsed.temperature_valid =
        json_number(root, "temperature_c", &parsed.temperature_c);
    parsed.ph_valid = json_number(root, "ph", &parsed.ph);
    parsed.ec_valid = json_number(root, "ec_us_cm", &parsed.ec_us_cm);
    int64_t integer = 0;
    if (json_integer(root, "ldr_raw", 0, 65535, &integer)) {
        parsed.ldr_raw = static_cast<int>(integer);
    }
    if (json_integer(root, "alarm_flags", 0, UINT32_MAX, &integer)) {
        parsed.alarm_flags = static_cast<uint32_t>(integer);
    }
    if (json_integer(root, "espnow_rssi_dbm", INT8_MIN, 0, &integer)) {
        parsed.espnow_rssi_dbm = static_cast<int>(integer);
    }
    if (json_integer(
            root,
            "configuration_revision",
            0,
            UINT32_MAX,
            &integer)) {
        parsed.configuration_revision = static_cast<uint32_t>(integer);
    }
    bool configuration_valid = false;
    if (json_boolean(
            root, "configuration_valid", &configuration_valid) &&
        configuration_valid) {
        int64_t heater_mode = 0;
        const bool complete =
            json_number(
                root,
                "target_temperature_c",
                &parsed.target_temperature_c) &&
            parsed.target_temperature_c >= 18.0 &&
            parsed.target_temperature_c <= 30.0 &&
            json_number(
                root,
                "temperature_hysteresis_c",
                &parsed.temperature_hysteresis_c) &&
            parsed.temperature_hysteresis_c >= 0.1 &&
            parsed.temperature_hysteresis_c <= 5.0 &&
            json_integer(root, "heater_mode", 0, 1, &heater_mode) &&
            json_schedule(
                root,
                "light_primary",
                &parsed.light_primary_schedule) &&
            json_schedule(
                root,
                "light_secondary",
                &parsed.light_secondary_schedule) &&
            json_schedule(
                root, "filter", &parsed.filter_schedule) &&
            json_schedule(
                root, "aerator", &parsed.aerator_schedule);
        parsed.heater_mode = static_cast<uint8_t>(heater_mode);
        parsed.configuration_valid = complete;
    } else {
        parsed.configuration_valid = false;
    }
    if (json_integer(
            root,
            "uptime_seconds",
            0,
            UINT32_MAX,
            &integer)) {
        parsed.controller_uptime_seconds = static_cast<uint32_t>(integer);
    }
    if (json_integer(
            root,
            "free_heap_bytes",
            0,
            UINT32_MAX,
            &integer)) {
        parsed.controller_free_heap_bytes = static_cast<uint32_t>(integer);
    }
    json_boolean(root, "water_level_low", &parsed.water_level_low);
    json_boolean(root, "leak_detected", &parsed.leak_detected);
    json_boolean(root, "controller_safe", &parsed.controller_safe);
    json_boolean(root, "light_primary_on", &parsed.light_primary_on);
    json_boolean(root, "light_secondary_on", &parsed.light_secondary_on);
    json_boolean(root, "filter_on", &parsed.filter_on);
    json_boolean(root, "aerator_on", &parsed.aerator_on);
    json_boolean(root, "heater_on", &parsed.heater_on);
    latest_snapshot = parsed;
    queue_latest_snapshot(parsed);
    cJSON_Delete(root);
}

void parse_acknowledgement_message(const char *json, size_t length) {
    if (json == nullptr || length == 0U ||
        length > kAcknowledgementJsonBytes || feedback_queue == nullptr) {
        return;
    }
    cJSON *root = cJSON_ParseWithLength(json, length);
    if (root == nullptr || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        ESP_LOGW(kTag, "Rejected malformed acknowledgement JSON");
        return;
    }
    cJSON *command_id_item =
        cJSON_GetObjectItemCaseSensitive(root, "command_id");
    int64_t status = 0;
    int64_t reason_code = 0;
    int64_t revision = 0;
    bool transport_timeout = false;
    CommandFeedback feedback = {};
    const bool valid =
        cJSON_IsString(command_id_item) &&
        parse_command_id(command_id_item->valuestring, &feedback.command_id) &&
        json_integer(root, "status", 0, 6, &status) &&
        json_integer(root, "reason_code", 0, UINT16_MAX, &reason_code) &&
        json_integer(
            root,
            "configuration_revision",
            0,
            UINT32_MAX,
            &revision) &&
        json_boolean(root, "transport_timeout", &transport_timeout);
    if (valid) {
        feedback.status = static_cast<uint8_t>(status);
        feedback.reason_code = static_cast<uint16_t>(reason_code);
        feedback.configuration_revision = static_cast<uint32_t>(revision);
        feedback.transport_timeout = transport_timeout;
        if (xQueueSend(feedback_queue, &feedback, 0U) != pdTRUE) {
            ESP_LOGW(kTag, "Acknowledgement queue is full");
        }
    } else {
        ESP_LOGW(kTag, "Rejected incomplete acknowledgement JSON");
    }
    cJSON_Delete(root);
}

bool mqtt_connected() {
    return connection_events != nullptr &&
           (xEventGroupGetBits(connection_events) & kMqttConnectedBit) != 0U;
}

void queue_command(const HmiCommandRequest &command) {
    if (pending_command_id != 0U) {
        hmi_ui_show_toast(
            HmiFeedbackKind::Information,
            "Poczekaj na potwierdzenie poprzedniego polecenia.");
        return;
    }
    if (!mqtt_connected() || !controller_online) {
        hmi_ui_show_toast(
            HmiFeedbackKind::Warning,
            "Polecenie zablokowane: sterownik jest offline.");
        return;
    }
    if (command.action == nullptr || command.target == nullptr) {
        hmi_ui_show_toast(
            HmiFeedbackKind::Error,
            "Interfejs przekazał niekompletne polecenie.");
        return;
    }
    OutgoingCommand outgoing = {};
    const uint64_t command_id = create_command_id();
    const int written = snprintf(
        outgoing.json,
        sizeof(outgoing.json),
        "{\"command_id\":\"%016" PRIx64
        "\",\"action\":\"%s\",\"target\":\"%s\",\"value\":%" PRId32
        ",\"duration_ms\":%" PRIu32
        ",\"expected_revision\":%" PRIu32 "}",
        command_id,
        command.action,
        command.target,
        command.value,
        command.duration_ms,
        latest_snapshot.configuration_revision);
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(outgoing.json)) {
        hmi_ui_show_toast(
            HmiFeedbackKind::Error,
            "Nie można zakodować polecenia.");
        return;
    }
    outgoing.length = static_cast<size_t>(written);
    if (xQueueSend(outgoing_command_queue, &outgoing, 0U) != pdTRUE) {
        hmi_ui_show_toast(
            HmiFeedbackKind::Error,
            "Kolejka poleceń jest pełna.");
        return;
    }
    pending_command_id = command_id;
    pending_command_started_ms = monotonic_ms();
    hmi_ui_set_command_pending(
        "Polecenie wysłane, oczekiwanie na ACK CYD…");
}

void professional_command_callback(const HmiCommandRequest &request, void *) {
    queue_command(request);
}

void professional_brightness_callback(uint8_t value,
                                      bool persist,
                                      void *) {
    if (value < kMinimumBrightness || value > 100U) {
        hmi_ui_show_toast(
            HmiFeedbackKind::Error,
            "Jasność panelu jest poza dozwolonym zakresem.");
        return;
    }
    display_brightness = value;
    const esp_err_t result =
        bsp_display_brightness_set(static_cast<int>(value));
    if (result != ESP_OK) {
        ESP_LOGW(
            kTag,
            "Unable to set display brightness: %s",
            esp_err_to_name(result));
        hmi_ui_show_toast(
            HmiFeedbackKind::Error,
            "Nie udało się ustawić jasności wyświetlacza.");
        return;
    }
    if (persist && !save_display_brightness(value)) {
        hmi_ui_show_toast(
            HmiFeedbackKind::Error,
            "Nie udało się zapisać jasności w pamięci panelu.");
    }
}

void show_professional_feedback(const CommandFeedback &feedback) {
    if (feedback.command_id != pending_command_id) {
        return;
    }
    pending_command_id = 0U;
    pending_command_started_ms = 0U;
    if (feedback.configuration_revision != 0U) {
        latest_snapshot.configuration_revision =
            feedback.configuration_revision;
    }
    if (feedback.transport_timeout) {
        hmi_ui_set_command_result(
            HmiFeedbackKind::Error,
            "Brak odpowiedzi CYD. Polecenie nie zostało potwierdzone.");
        return;
    }
    switch (feedback.status) {
    case 0U:
        hmi_ui_set_command_result(
            HmiFeedbackKind::Success,
            "Polecenie zostało wykonane przez CYD.");
        break;
    case 1U:
        hmi_ui_set_command_result(
            HmiFeedbackKind::Success,
            "Duplikat polecenia został bezpiecznie pominięty.");
        break;
    case 3U: {
        char message[128] = {};
        snprintf(
            message,
            sizeof(message),
            "Konflikt konfiguracji. Aktualna rewizja: %08" PRIX32 ".",
            feedback.configuration_revision);
        hmi_ui_set_command_result(HmiFeedbackKind::Warning, message);
        break;
    }
    case 4U: {
        char message[112] = {};
        snprintf(
            message,
            sizeof(message),
            "Nieprawidłowe polecenie. Kod przyczyny: %u.",
            static_cast<unsigned int>(feedback.reason_code));
        hmi_ui_set_command_result(HmiFeedbackKind::Error, message);
        break;
    }
    case 5U:
        hmi_ui_set_command_result(
            HmiFeedbackKind::Warning,
            "Sterownik jest chwilowo zajęty. Spróbuj ponownie.");
        break;
    case 6U:
        hmi_ui_set_command_result(
            HmiFeedbackKind::Error,
            "Polecenie wygasło przed wykonaniem.");
        break;
    default: {
        char message[112] = {};
        snprintf(
            message,
            sizeof(message),
            "CYD odrzucił polecenie. Kod przyczyny: %u.",
            static_cast<unsigned int>(feedback.reason_code));
        hmi_ui_set_command_result(HmiFeedbackKind::Error, message);
        break;
    }
    }
}

void professional_ui_timer_callback(lv_timer_t *) {
    HmiSnapshot snapshot = {};
    bool received = false;
    while (xQueueReceive(snapshot_queue, &snapshot, 0U) == pdTRUE) {
        received = true;
    }
    const uint32_t now = monotonic_ms();
    if (received) {
        hmi_ui_apply_snapshot(snapshot, now);
    }
    CommandFeedback feedback = {};
    while (xQueueReceive(feedback_queue, &feedback, 0U) == pdTRUE) {
        show_professional_feedback(feedback);
    }
    if (pending_command_id != 0U &&
        static_cast<uint32_t>(now - pending_command_started_ms) >
            kCommandUiTimeoutMs) {
        pending_command_id = 0U;
        pending_command_started_ms = 0U;
        hmi_ui_set_command_result(
            HmiFeedbackKind::Error,
            "Przekroczono czas oczekiwania na potwierdzenie MQTT.");
    }
    const EventBits_t bits =
        connection_events != nullptr
            ? xEventGroupGetBits(connection_events)
            : 0U;
    hmi_ui_set_connectivity(
        (bits & kWifiConnectedBit) != 0U,
        (bits & kMqttConnectedBit) != 0U,
        controller_online);
    hmi_ui_tick(
        now, static_cast<uint32_t>(esp_get_free_heap_size()));
}

bool create_professional_ui() {
    const HmiUiCallbacks callbacks = {
        professional_command_callback,
        professional_brightness_callback,
        nullptr
    };
    if (!hmi_ui_create(callbacks, display_brightness)) {
        return false;
    }
    lv_timer_create(professional_ui_timer_callback, 100U, nullptr);
    return true;
}

void command_publisher_task(void *) {
    OutgoingCommand command = {};
    for (;;) {
        if (xQueueReceive(
                outgoing_command_queue,
                &command,
                portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (!mqtt_connected() || mqtt_client == nullptr) {
            continue;
        }
        const int message_id = esp_mqtt_client_publish(
            mqtt_client,
            command_topic,
            command.json,
            static_cast<int>(command.length),
            1,
            0);
        if (message_id < 0) {
            ESP_LOGW(kTag, "Unable to publish HMI command");
        }
    }
}

void mqtt_event_handler(void *,
                        esp_event_base_t,
                        int32_t event_id,
                        void *event_data) {
    esp_mqtt_event_handle_t event =
        static_cast<esp_mqtt_event_handle_t>(event_data);
    if (event == nullptr) {
        return;
    }
    if (event_id == MQTT_EVENT_CONNECTED) {
        xEventGroupSetBits(connection_events, kMqttConnectedBit);
        esp_mqtt_client_subscribe(mqtt_client, state_topic, 1);
        esp_mqtt_client_subscribe(mqtt_client, availability_topic, 1);
        esp_mqtt_client_subscribe(mqtt_client, acknowledgement_topic, 1);
        esp_mqtt_client_publish(
            mqtt_client,
            hmi_availability_topic,
            "online",
            0,
            1,
            1);
        queue_latest_snapshot(latest_snapshot);
        ESP_LOGI(kTag, "MQTT connected");
    } else if (event_id == MQTT_EVENT_DISCONNECTED) {
        xEventGroupClearBits(connection_events, kMqttConnectedBit);
        controller_online = false;
        latest_snapshot.controller_online = false;
        queue_latest_snapshot(latest_snapshot);
        ESP_LOGW(kTag, "MQTT disconnected");
    } else if (event_id == MQTT_EVENT_DATA) {
        if (event->current_data_offset != 0 ||
            event->data_len != event->total_data_len) {
            ESP_LOGW(kTag, "Rejected fragmented MQTT message");
            return;
        }
        if (topic_matches(event, state_topic)) {
            parse_state_message(
                event->data, static_cast<size_t>(event->data_len));
        } else if (topic_matches(event, availability_topic)) {
            controller_online =
                event->data_len == 6 &&
                memcmp(event->data, "online", 6U) == 0;
            latest_snapshot.controller_online = controller_online;
            queue_latest_snapshot(latest_snapshot);
        } else if (topic_matches(event, acknowledgement_topic)) {
            parse_acknowledgement_message(
                event->data, static_cast<size_t>(event->data_len));
        }
    } else if (event_id == MQTT_EVENT_ERROR) {
        ESP_LOGE(kTag, "MQTT transport error");
    }
}

void start_mqtt() {
    if (mqtt_client != nullptr) {
        return;
    }
    esp_mqtt_client_config_t configuration = {};
    configuration.broker.address.uri =
        CONFIG_AQUACYD_HMI_MQTT_BROKER_URI;
    configuration.credentials.username =
        CONFIG_AQUACYD_HMI_MQTT_USERNAME;
    configuration.credentials.authentication.password =
        CONFIG_AQUACYD_HMI_MQTT_PASSWORD;
    configuration.session.last_will.topic = hmi_availability_topic;
    configuration.session.last_will.msg = "offline";
    configuration.session.last_will.qos = 1;
    configuration.session.last_will.retain = 1;
    configuration.network.reconnect_timeout_ms = 3000;
    mqtt_client = esp_mqtt_client_init(&configuration);
    if (mqtt_client == nullptr) {
        ESP_LOGE(kTag, "Unable to allocate MQTT client");
        return;
    }
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(
        mqtt_client,
        static_cast<esp_mqtt_event_id_t>(ESP_EVENT_ANY_ID),
        mqtt_event_handler,
        nullptr));
    ESP_ERROR_CHECK(esp_mqtt_client_start(mqtt_client));
}

void wifi_event_handler(void *,
                        esp_event_base_t event_base,
                        int32_t event_id,
                        void *) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (
        event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(connection_events, kWifiConnectedBit);
        esp_wifi_connect();
    } else if (
        event_base == IP_EVENT &&
        event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(connection_events, kWifiConnectedBit);
        start_mqtt();
    }
}

void initialize_wifi() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t initialization = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&initialization));
    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, nullptr));

    wifi_config_t configuration = {};
    strlcpy(
        reinterpret_cast<char *>(configuration.sta.ssid),
        CONFIG_AQUACYD_HMI_WIFI_SSID,
        sizeof(configuration.sta.ssid));
    strlcpy(
        reinterpret_cast<char *>(configuration.sta.password),
        CONFIG_AQUACYD_HMI_WIFI_PASSWORD,
        sizeof(configuration.sta.password));
    configuration.sta.threshold.authmode =
        strlen(CONFIG_AQUACYD_HMI_WIFI_PASSWORD) == 0U
            ? WIFI_AUTH_OPEN
            : WIFI_AUTH_WPA2_PSK;
    configuration.sta.pmf_cfg.capable = true;
    configuration.sta.pmf_cfg.required = false;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &configuration));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void initialize_display() {
    bsp_display_cfg_t configuration = {};
    configuration.lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    configuration.buffer_size = BSP_LCD_DRAW_BUFF_SIZE;
    configuration.double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE;
    configuration.flags.buff_dma = true;
    configuration.flags.buff_spiram = false;
    configuration.flags.sw_rotate = true;
    lv_display_t *display =
        bsp_display_start_with_config(&configuration);
    if (display == nullptr) {
        ESP_LOGE(kTag, "Display initialization failed");
        abort();
    }
    ESP_ERROR_CHECK(bsp_display_backlight_on());
    ESP_ERROR_CHECK(bsp_display_brightness_set(display_brightness));
#if CONFIG_AQUACYD_HMI_ROTATE_180
    bsp_display_rotate(display, LV_DISPLAY_ROTATION_180);
#endif
    if (!bsp_display_lock(0U)) {
        ESP_LOGE(kTag, "Unable to lock LVGL during startup");
        abort();
    }
    if (!create_professional_ui()) {
        bsp_display_unlock();
        ESP_LOGE(kTag, "Unable to create professional HMI interface");
        abort();
    }
    bsp_display_unlock();
}

} // namespace

extern "C" void app_main(void) {
    esp_err_t nvs_result = nvs_flash_init();
    if (nvs_result == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_result);
    display_brightness = load_display_brightness();
    const bool connectivity_configured = initialize_topics();

    connection_events = xEventGroupCreate();
    snapshot_queue = xQueueCreate(1U, sizeof(HmiSnapshot));
    outgoing_command_queue = xQueueCreate(8U, sizeof(OutgoingCommand));
    feedback_queue = xQueueCreate(8U, sizeof(CommandFeedback));
    if (connection_events == nullptr ||
        snapshot_queue == nullptr ||
        outgoing_command_queue == nullptr ||
        feedback_queue == nullptr) {
        ESP_LOGE(kTag, "Unable to allocate HMI RTOS resources");
        abort();
    }

    initialize_display();
    if (!connectivity_configured) {
        ESP_LOGW(
            kTag,
            "HMI started offline; configure Wi-Fi and MQTT with menuconfig");
        if (bsp_display_lock(0U)) {
            hmi_ui_show_toast(
                HmiFeedbackKind::Warning,
                "Panel działa offline. Skonfiguruj Wi-Fi i MQTT przed instalacją.");
            bsp_display_unlock();
        }
        return;
    }
    const BaseType_t publisher_created = xTaskCreate(
        command_publisher_task,
        "hmi_mqtt_tx",
        4096U,
        nullptr,
        4U,
        nullptr);
    if (publisher_created != pdPASS) {
        ESP_LOGE(kTag, "Unable to create MQTT publisher task");
        abort();
    }
    initialize_wifi();
    ESP_LOGI(kTag, "AquaCYD ESP32-P4 HMI started");
}
