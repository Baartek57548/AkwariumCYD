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

struct HmiSnapshot {
    bool controller_online;
    bool temperature_valid;
    bool ph_valid;
    bool ec_valid;
    double temperature_c;
    double ph;
    double ec_us_cm;
    int ldr_raw;
    uint32_t alarm_flags;
    int espnow_rssi_dbm;
    uint32_t configuration_revision;
    uint32_t controller_uptime_seconds;
    uint32_t controller_free_heap_bytes;
    bool water_level_low;
    bool leak_detected;
    bool controller_safe;
    bool light_primary_on;
    bool light_secondary_on;
    bool filter_on;
    bool aerator_on;
    bool heater_on;
};

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

struct ButtonCommand {
    const char *caption;
    const char *action;
    const char *target;
    int32_t value;
    uint32_t duration_ms;
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

lv_obj_t *connection_label = nullptr;
lv_obj_t *temperature_label = nullptr;
lv_obj_t *ph_label = nullptr;
lv_obj_t *ec_label = nullptr;
lv_obj_t *ldr_label = nullptr;
lv_obj_t *radio_label = nullptr;
lv_obj_t *alarm_label = nullptr;
lv_obj_t *feedback_label = nullptr;
lv_obj_t *output_state_label = nullptr;
lv_obj_t *system_info_label = nullptr;
lv_obj_t *brightness_label = nullptr;
lv_obj_t *control_buttons[10] = {};

uint64_t pending_command_id = 0U;
uint32_t pending_command_started_ms = 0U;
uint8_t display_brightness = kDefaultBrightness;

constexpr ButtonCommand kButtonCommands[] = {
    {"Światło ON", "set_output", "light_primary", 1, 900000U},
    {"Światło OFF", "set_output", "light_primary", 0, 900000U},
    {"Roślinne ON", "set_output", "light_secondary", 1, 900000U},
    {"Roślinne OFF", "set_output", "light_secondary", 0, 900000U},
    {"Filtr ON", "set_output", "filter", 1, 900000U},
    {"Filtr OFF", "set_output", "filter", 0, 900000U},
    {"Napowietrzanie ON", "set_output", "aerator", 1, 900000U},
    {"Napowietrzanie OFF", "set_output", "aerator", 0, 900000U},
    {"Karmienie", "trigger_feed", "feeder", 1, 0U},
    {"Odśwież", "request_snapshot", "controller", 0, 0U}
};

static_assert(
    sizeof(control_buttons) / sizeof(control_buttons[0]) ==
        sizeof(kButtonCommands) / sizeof(kButtonCommands[0]),
    "Control button storage must match command definitions");

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

void queue_button_command(const ButtonCommand &command) {
    if (pending_command_id != 0U) {
        lv_label_set_text(
            feedback_label,
            "Poczekaj na potwierdzenie poprzedniego polecenia.");
        return;
    }
    if (!mqtt_connected() || !controller_online) {
        if (feedback_label != nullptr) {
            lv_label_set_text(
                feedback_label,
                "Polecenie zablokowane: sterownik jest offline.");
        }
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
        lv_label_set_text(feedback_label, "Nie można zakodować polecenia.");
        return;
    }
    outgoing.length = static_cast<size_t>(written);
    if (xQueueSend(outgoing_command_queue, &outgoing, 0U) != pdTRUE) {
        lv_label_set_text(feedback_label, "Kolejka poleceń jest pełna.");
        return;
    }
    pending_command_id = command_id;
    pending_command_started_ms = monotonic_ms();
    lv_label_set_text(feedback_label, "Polecenie wysłane, oczekiwanie na ACK…");
}

void button_event_callback(lv_event_t *event) {
    if (event == nullptr || lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    const ButtonCommand *command =
        static_cast<const ButtonCommand *>(lv_event_get_user_data(event));
    if (command != nullptr) {
        queue_button_command(*command);
    }
}

void brightness_event_callback(lv_event_t *event) {
    if (event == nullptr) {
        return;
    }
    lv_obj_t *slider =
        static_cast<lv_obj_t *>(lv_event_get_target(event));
    if (slider == nullptr) {
        return;
    }
    const lv_event_code_t code = lv_event_get_code(event);
    const int32_t value = lv_slider_get_value(slider);
    if (value < kMinimumBrightness || value > 100) {
        return;
    }
    if (code == LV_EVENT_VALUE_CHANGED) {
        display_brightness = static_cast<uint8_t>(value);
        if (brightness_label != nullptr) {
            lv_label_set_text_fmt(brightness_label, "%" PRId32 "%%", value);
        }
        const esp_err_t result =
            bsp_display_brightness_set(static_cast<int>(value));
        if (result != ESP_OK) {
            ESP_LOGW(
                kTag,
                "Unable to set display brightness: %s",
                esp_err_to_name(result));
        }
    } else if (
        code == LV_EVENT_RELEASED &&
        !save_display_brightness(static_cast<uint8_t>(value))) {
        lv_label_set_text(
            brightness_label,
            "Błąd zapisu jasności");
    }
}

lv_obj_t *create_metric_card(lv_obj_t *parent,
                             const char *title,
                             lv_obj_t **value_label) {
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 290, 150);
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x172033), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x2A3B59), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_pad_all(card, 18, 0);

    lv_obj_t *title_label = lv_label_create(card);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0x8FA7C7), 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, 0);

    *value_label = lv_label_create(card);
    lv_label_set_text(*value_label, "—");
    lv_obj_set_style_text_font(
        *value_label, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(*value_label, lv_color_hex(0xF4F7FC), 0);
    lv_obj_align(*value_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    return card;
}

void update_dashboard(const HmiSnapshot &snapshot) {
    const bool mqtt_ok = mqtt_connected();
    lv_label_set_text_fmt(
        connection_label,
        "MQTT: %s  |  CYD: %s  |  ESP-NOW: %d dBm",
        mqtt_ok ? "online" : "offline",
        snapshot.controller_online ? "online" : "offline",
        snapshot.espnow_rssi_dbm);
    lv_obj_set_style_text_color(
        connection_label,
        mqtt_ok && snapshot.controller_online
            ? lv_color_hex(0x65D69B)
            : lv_color_hex(0xFFB45C),
        0);
    if (snapshot.temperature_valid) {
        lv_label_set_text_fmt(
            temperature_label, "%.2f °C", snapshot.temperature_c);
    } else {
        lv_label_set_text(temperature_label, "brak");
    }
    if (snapshot.ph_valid) {
        lv_label_set_text_fmt(ph_label, "%.3f", snapshot.ph);
    } else {
        lv_label_set_text(ph_label, "brak");
    }
    if (snapshot.ec_valid) {
        lv_label_set_text_fmt(ec_label, "%.0f µS/cm", snapshot.ec_us_cm);
    } else {
        lv_label_set_text(ec_label, "brak");
    }
    lv_label_set_text_fmt(ldr_label, "%d", snapshot.ldr_raw);
    lv_label_set_text_fmt(radio_label, "%d dBm", snapshot.espnow_rssi_dbm);
    lv_label_set_text_fmt(
        alarm_label, "Alarmy: 0x%08" PRIX32, snapshot.alarm_flags);
    lv_obj_set_style_text_color(
        alarm_label,
        snapshot.alarm_flags == 0U
            ? lv_color_hex(0x65D69B)
            : lv_color_hex(0xFF6B6B),
        0);
    if (output_state_label != nullptr) {
        lv_label_set_text_fmt(
            output_state_label,
            "Główne: %s   Roślinne: %s   Filtr: %s   "
            "Napowietrzanie: %s   Grzałka: %s",
            snapshot.light_primary_on ? "ON" : "OFF",
            snapshot.light_secondary_on ? "ON" : "OFF",
            snapshot.filter_on ? "ON" : "OFF",
            snapshot.aerator_on ? "ON" : "OFF",
            snapshot.heater_on ? "ON" : "OFF");
    }
    if (system_info_label != nullptr) {
        lv_label_set_text_fmt(
            system_info_label,
            "CYD uptime: %" PRIu32 " s\n"
            "CYD wolna pamięć: %" PRIu32 " B\n"
            "Rewizja konfiguracji: %08" PRIX32 "\n"
            "Poziom wody: %s\n"
            "Czujnik wycieku: %s\n"
            "Fail-safe: %s\n"
            "HMI wolna pamięć: %" PRIu32 " B",
            snapshot.controller_uptime_seconds,
            snapshot.controller_free_heap_bytes,
            snapshot.configuration_revision,
            snapshot.water_level_low ? "NISKI" : "OK",
            snapshot.leak_detected ? "WYCIEK" : "OK",
            snapshot.controller_safe ? "OK" : "ALARM",
            static_cast<uint32_t>(esp_get_free_heap_size()));
    }
}

void update_control_states() {
    const bool enabled =
        mqtt_connected() && controller_online && pending_command_id == 0U;
    for (lv_obj_t *button : control_buttons) {
        if (button == nullptr) {
            continue;
        }
        if (enabled) {
            lv_obj_remove_state(button, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(button, LV_STATE_DISABLED);
        }
    }
}

void show_command_feedback(const CommandFeedback &feedback) {
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
        lv_label_set_text(
            feedback_label,
            "Brak odpowiedzi CYD. Polecenie nie zostało potwierdzone.");
        return;
    }
    switch (feedback.status) {
    case 0U:
        lv_label_set_text(feedback_label, "Polecenie wykonane przez CYD.");
        break;
    case 1U:
        lv_label_set_text(
            feedback_label,
            "Polecenie było już wykonane; duplikat bezpiecznie pominięto.");
        break;
    case 3U:
        lv_label_set_text_fmt(
            feedback_label,
            "Konflikt konfiguracji (rewizja %08" PRIX32
            "). Odświeżono stan.",
            feedback.configuration_revision);
        break;
    case 4U:
        lv_label_set_text_fmt(
            feedback_label,
            "Nieprawidłowe polecenie (kod %u).",
            static_cast<unsigned int>(feedback.reason_code));
        break;
    case 5U:
        lv_label_set_text(feedback_label, "Sterownik jest chwilowo zajęty.");
        break;
    case 6U:
        lv_label_set_text(feedback_label, "Polecenie wygasło.");
        break;
    default:
        lv_label_set_text_fmt(
            feedback_label,
            "CYD odrzucił polecenie (kod %u).",
            static_cast<unsigned int>(feedback.reason_code));
        break;
    }
}

void ui_timer_callback(lv_timer_t *) {
    HmiSnapshot snapshot = {};
    bool received = false;
    while (xQueueReceive(snapshot_queue, &snapshot, 0U) == pdTRUE) {
        received = true;
    }
    if (received) {
        update_dashboard(snapshot);
    }
    CommandFeedback feedback = {};
    while (xQueueReceive(feedback_queue, &feedback, 0U) == pdTRUE) {
        show_command_feedback(feedback);
    }
    if (pending_command_id != 0U &&
        static_cast<uint32_t>(
            monotonic_ms() - pending_command_started_ms) >
            kCommandUiTimeoutMs) {
        pending_command_id = 0U;
        pending_command_started_ms = 0U;
        lv_label_set_text(
            feedback_label,
            "Przekroczono czas oczekiwania na potwierdzenie MQTT.");
    }
    update_control_states();
}

void create_dashboard_ui() {
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x0B1020), 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(0xF4F7FC), 0);

    lv_obj_t *header = lv_obj_create(screen);
    lv_obj_set_size(header, LV_PCT(100), 82);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x10182A), 0);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "AquaCYD");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 18, -12);

    connection_label = lv_label_create(header);
    lv_label_set_text(connection_label, "MQTT: start  |  CYD: offline");
    lv_obj_set_style_text_font(
        connection_label, &lv_font_montserrat_16, 0);
    lv_obj_align(connection_label, LV_ALIGN_LEFT_MID, 18, 22);

    alarm_label = lv_label_create(header);
    lv_label_set_text(alarm_label, "Alarmy: —");
    lv_obj_set_style_text_font(alarm_label, &lv_font_montserrat_20, 0);
    lv_obj_align(alarm_label, LV_ALIGN_RIGHT_MID, -18, 0);

    lv_obj_t *tabs = lv_tabview_create(screen);
    lv_obj_set_size(tabs, LV_PCT(100), 518);
    lv_obj_align(tabs, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_tabview_set_tab_bar_size(tabs, 56);
    lv_obj_set_style_bg_color(tabs, lv_color_hex(0x0B1020), 0);

    lv_obj_t *dashboard_tab = lv_tabview_add_tab(tabs, "Podgląd");
    lv_obj_t *controls_tab = lv_tabview_add_tab(tabs, "Sterowanie");
    lv_obj_t *system_tab = lv_tabview_add_tab(tabs, "System");
    lv_obj_set_flex_flow(dashboard_tab, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(
        dashboard_tab,
        LV_FLEX_ALIGN_SPACE_EVENLY,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_SPACE_EVENLY);
    lv_obj_set_style_pad_all(dashboard_tab, 18, 0);
    lv_obj_set_style_pad_row(dashboard_tab, 16, 0);

    create_metric_card(
        dashboard_tab, "Temperatura", &temperature_label);
    create_metric_card(dashboard_tab, "pH", &ph_label);
    create_metric_card(dashboard_tab, "Przewodność", &ec_label);
    create_metric_card(dashboard_tab, "Światło LDR", &ldr_label);
    create_metric_card(dashboard_tab, "Łącze ESP-NOW", &radio_label);

    lv_obj_set_flex_flow(controls_tab, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(
        controls_tab,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(controls_tab, 24, 0);
    lv_obj_set_style_pad_row(controls_tab, 12, 0);
    lv_obj_set_style_pad_column(controls_tab, 18, 0);

    for (size_t index = 0U;
         index < sizeof(kButtonCommands) / sizeof(kButtonCommands[0]);
         ++index) {
        lv_obj_t *button = lv_button_create(controls_tab);
        control_buttons[index] = button;
        lv_obj_set_size(button, 280, 64);
        lv_obj_set_style_radius(button, 14, 0);
        lv_obj_add_event_cb(
            button,
            button_event_callback,
            LV_EVENT_CLICKED,
            const_cast<ButtonCommand *>(&kButtonCommands[index]));
        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text(label, kButtonCommands[index].caption);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
        lv_obj_center(label);
    }
    output_state_label = lv_label_create(controls_tab);
    lv_obj_set_width(output_state_label, LV_PCT(100));
    lv_label_set_long_mode(output_state_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(
        output_state_label,
        "Główne: —   Roślinne: —   Filtr: —   "
        "Napowietrzanie: —   Grzałka: —");
    lv_obj_set_style_text_align(output_state_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(
        output_state_label, &lv_font_montserrat_16, 0);

    feedback_label = lv_label_create(controls_tab);
    lv_obj_set_width(feedback_label, LV_PCT(100));
    lv_label_set_long_mode(feedback_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(
        feedback_label,
        "Sterowanie ręczne jest czasowe; zabezpieczenia pozostają w CYD.");
    lv_obj_set_style_text_align(feedback_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(
        feedback_label, &lv_font_montserrat_16, 0);

    lv_obj_set_flex_flow(system_tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        system_tab,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(system_tab, 28, 0);
    lv_obj_set_style_pad_row(system_tab, 20, 0);

    lv_obj_t *brightness_title = lv_label_create(system_tab);
    lv_label_set_text(brightness_title, "Jasność ekranu");
    lv_obj_set_style_text_font(
        brightness_title, &lv_font_montserrat_24, 0);

    lv_obj_t *brightness_row = lv_obj_create(system_tab);
    lv_obj_set_size(brightness_row, LV_PCT(100), 76);
    lv_obj_set_style_bg_opa(brightness_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(brightness_row, 0, 0);
    lv_obj_set_style_pad_all(brightness_row, 0, 0);

    lv_obj_t *brightness_slider = lv_slider_create(brightness_row);
    lv_obj_set_size(brightness_slider, 790, 28);
    lv_slider_set_range(
        brightness_slider, kMinimumBrightness, 100);
    lv_slider_set_value(
        brightness_slider, display_brightness, LV_ANIM_OFF);
    lv_obj_align(brightness_slider, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(
        brightness_slider,
        brightness_event_callback,
        LV_EVENT_ALL,
        nullptr);

    brightness_label = lv_label_create(brightness_row);
    lv_label_set_text_fmt(
        brightness_label, "%u%%", static_cast<unsigned int>(display_brightness));
    lv_obj_set_style_text_font(
        brightness_label, &lv_font_montserrat_20, 0);
    lv_obj_align(brightness_label, LV_ALIGN_RIGHT_MID, 0, 0);

    system_info_label = lv_label_create(system_tab);
    lv_obj_set_width(system_info_label, LV_PCT(100));
    lv_label_set_long_mode(system_info_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(
        system_info_label,
        "Oczekiwanie na dane diagnostyczne sterownika…");
    lv_obj_set_style_text_font(
        system_info_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_line_space(system_info_label, 10, 0);

    update_control_states();
    lv_timer_create(ui_timer_callback, 100U, nullptr);
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
    create_dashboard_ui();
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
    if (!initialize_topics()) {
        ESP_LOGE(kTag, "Run idf.py menuconfig before flashing the HMI");
        abort();
    }

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
