#include <inttypes.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aquacyd_link_protocol.h"
#include "cJSON.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "nvs_flash.h"

namespace {

constexpr char kTag[] = "aquacyd_gateway";
constexpr size_t kMacLength = 6U;
constexpr size_t kKeyLength = 16U;
constexpr size_t kMaximumMqttCommandBytes = 512U;
constexpr size_t kMqttTopicBytes = 160U;
constexpr uint32_t kCommandTtlMs = 5000U;
constexpr uint32_t kCommandAckTimeoutMs = 450U;
constexpr uint8_t kCommandMaximumAttempts = 4U;
constexpr EventBits_t kWifiConnectedBit = 1U << 0U;
constexpr EventBits_t kMqttConnectedBit = 1U << 1U;
constexpr uint16_t kTransportTimeoutReason = 100U;

struct ReceivedDatagram {
    uint8_t source_mac[kMacLength];
    uint16_t length;
    int8_t rssi;
    uint8_t bytes[aquacyd::link::kEspNowMaximumFrameSize];
};

struct MqttCommand {
    size_t length;
    char json[kMaximumMqttCommandBytes + 1U];
};

EventGroupHandle_t connection_events = nullptr;
QueueHandle_t received_queue = nullptr;
QueueHandle_t command_queue = nullptr;
TaskHandle_t command_task_handle = nullptr;
esp_mqtt_client_handle_t mqtt_client = nullptr;
aquacyd::link::SequenceWindow receive_window;

uint8_t controller_mac[kMacLength] = {};
uint8_t espnow_pmk[kKeyLength] = {};
uint8_t espnow_lmk[kKeyLength] = {};
uint32_t gateway_source_id = 0U;
uint32_t gateway_boot_id = 0U;
uint32_t transmit_sequence = 0U;
volatile uint32_t last_telemetry_ms = 0U;
volatile uint64_t last_acknowledged_command_id = 0U;
bool espnow_initialized = false;
bool controller_reported_online = false;

portMUX_TYPE acknowledgement_lock = portMUX_INITIALIZER_UNLOCKED;

char state_topic[kMqttTopicBytes] = {};
char availability_topic[kMqttTopicBytes] = {};
char command_topic[kMqttTopicBytes] = {};
char acknowledgement_topic[kMqttTopicBytes] = {};

uint32_t monotonic_ms() {
    return static_cast<uint32_t>(
        static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL);
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

bool parse_hexadecimal(const char *text,
                       uint8_t *output,
                       size_t output_length) {
    if (text == nullptr || output == nullptr ||
        strlen(text) != output_length * 2U) {
        return false;
    }
    for (size_t index = 0U; index < output_length; ++index) {
        const int high = hexadecimal_value(text[index * 2U]);
        const int low = hexadecimal_value(text[index * 2U + 1U]);
        if (high < 0 || low < 0) {
            return false;
        }
        output[index] =
            static_cast<uint8_t>((static_cast<uint8_t>(high) << 4U) |
                                 static_cast<uint8_t>(low));
    }
    return true;
}

bool parse_command_id(const char *text, uint64_t *output) {
    if (text == nullptr || output == nullptr || strlen(text) != 16U) {
        return false;
    }
    uint64_t value = 0U;
    for (size_t index = 0U; index < 16U; ++index) {
        const int digit = hexadecimal_value(text[index]);
        if (digit < 0) {
            return false;
        }
        value = (value << 4U) | static_cast<uint64_t>(digit);
    }
    if (value == 0U) {
        return false;
    }
    *output = value;
    return true;
}

bool parse_mac_address(const char *text, uint8_t *output) {
    if (text == nullptr || output == nullptr || strlen(text) != 17U) {
        return false;
    }
    for (size_t index = 0U; index < kMacLength; ++index) {
        const size_t offset = index * 3U;
        const int high = hexadecimal_value(text[offset]);
        const int low = hexadecimal_value(text[offset + 1U]);
        if (high < 0 || low < 0 ||
            (index + 1U < kMacLength && text[offset + 2U] != ':')) {
            return false;
        }
        output[index] =
            static_cast<uint8_t>((static_cast<uint8_t>(high) << 4U) |
                                 static_cast<uint8_t>(low));
    }
    const uint8_t zero_mac[kMacLength] = {};
    const uint8_t broadcast_mac[kMacLength] = {
        0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU
    };
    return memcmp(output, zero_mac, sizeof(zero_mac)) != 0 &&
           memcmp(output, broadcast_mac, sizeof(broadcast_mac)) != 0 &&
           (output[0] & 0x01U) == 0U;
}

bool configuration_is_valid() {
    if (strlen(CONFIG_AQUACYD_WIFI_SSID) == 0U ||
        strlen(CONFIG_AQUACYD_WIFI_PASSWORD) == 0U ||
        strlen(CONFIG_AQUACYD_MQTT_BROKER_URI) == 0U ||
        strlen(CONFIG_AQUACYD_MQTT_USERNAME) == 0U ||
        strlen(CONFIG_AQUACYD_MQTT_PASSWORD) == 0U ||
        strlen(CONFIG_AQUACYD_MQTT_BASE_TOPIC) == 0U ||
        strlen(CONFIG_AQUACYD_DEVICE_ID) == 0U) {
        ESP_LOGE(
            kTag,
            "Wi-Fi, MQTT credentials and device settings must not be empty");
        return false;
    }
    if (!parse_mac_address(CONFIG_AQUACYD_CONTROLLER_MAC, controller_mac)) {
        ESP_LOGE(kTag, "CONFIG_AQUACYD_CONTROLLER_MAC is invalid");
        return false;
    }
    if (!parse_hexadecimal(
            CONFIG_AQUACYD_ESPNOW_PMK, espnow_pmk, sizeof(espnow_pmk)) ||
        !parse_hexadecimal(
            CONFIG_AQUACYD_ESPNOW_LMK, espnow_lmk, sizeof(espnow_lmk))) {
        ESP_LOGE(kTag, "ESP-NOW PMK and LMK must each contain 32 hex characters");
        return false;
    }
    if (!append_topic(
            state_topic,
            sizeof(state_topic),
            CONFIG_AQUACYD_MQTT_BASE_TOPIC,
            "state") ||
        !append_topic(
            availability_topic,
            sizeof(availability_topic),
            CONFIG_AQUACYD_MQTT_BASE_TOPIC,
            "availability") ||
        !append_topic(
            command_topic,
            sizeof(command_topic),
            CONFIG_AQUACYD_MQTT_BASE_TOPIC,
            "command/set") ||
        !append_topic(
            acknowledgement_topic,
            sizeof(acknowledgement_topic),
            CONFIG_AQUACYD_MQTT_BASE_TOPIC,
            "command/ack")) {
        ESP_LOGE(kTag, "MQTT base topic is too long");
        return false;
    }
    return true;
}

bool mqtt_is_connected() {
    return connection_events != nullptr &&
           (xEventGroupGetBits(connection_events) & kMqttConnectedBit) != 0U;
}

int publish_mqtt(const char *topic,
                 const char *payload,
                 int qos,
                 bool retain) {
    if (!mqtt_is_connected() || mqtt_client == nullptr ||
        topic == nullptr || payload == nullptr) {
        return -1;
    }
    return esp_mqtt_client_publish(
        mqtt_client, topic, payload, 0, qos, retain ? 1 : 0);
}

void add_device_descriptor(cJSON *root) {
    cJSON *device = cJSON_AddObjectToObject(root, "device");
    cJSON *identifiers = cJSON_AddArrayToObject(device, "identifiers");
    cJSON_AddItemToArray(
        identifiers, cJSON_CreateString(CONFIG_AQUACYD_DEVICE_ID));
    cJSON_AddStringToObject(device, "name", "AquaCYD Aquarium");
    cJSON_AddStringToObject(device, "manufacturer", "AquaCYD");
    cJSON_AddStringToObject(device, "model", "CYD + ESP32-C6 gateway");
}

void publish_discovery_entity(const char *component,
                              const char *object_suffix,
                              const char *name,
                              const char *value_template,
                              const char *unit,
                              const char *device_class,
                              const char *state_class) {
    char topic[kMqttTopicBytes] = {};
    const int topic_length = snprintf(
        topic,
        sizeof(topic),
        "homeassistant/%s/%s/%s_%s/config",
        component,
        CONFIG_AQUACYD_DEVICE_ID,
        CONFIG_AQUACYD_DEVICE_ID,
        object_suffix);
    if (topic_length <= 0 ||
        static_cast<size_t>(topic_length) >= sizeof(topic)) {
        ESP_LOGE(kTag, "Discovery topic is too long for %s", object_suffix);
        return;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == nullptr) {
        ESP_LOGE(kTag, "Unable to allocate discovery JSON");
        return;
    }
    char unique_id[96] = {};
    const int unique_length = snprintf(
        unique_id,
        sizeof(unique_id),
        "%s_%s",
        CONFIG_AQUACYD_DEVICE_ID,
        object_suffix);
    if (unique_length <= 0 ||
        static_cast<size_t>(unique_length) >= sizeof(unique_id)) {
        ESP_LOGE(kTag, "Discovery unique ID is too long");
        cJSON_Delete(root);
        return;
    }
    char default_entity_id[112] = {};
    const int entity_id_length = snprintf(
        default_entity_id,
        sizeof(default_entity_id),
        "%s.%s",
        component,
        unique_id);
    if (entity_id_length <= 0 ||
        static_cast<size_t>(entity_id_length) >=
            sizeof(default_entity_id)) {
        ESP_LOGE(kTag, "Discovery entity ID is too long");
        cJSON_Delete(root);
        return;
    }
    cJSON_AddStringToObject(root, "name", name);
    cJSON_AddStringToObject(root, "unique_id", unique_id);
    cJSON_AddStringToObject(
        root, "default_entity_id", default_entity_id);
    cJSON_AddStringToObject(root, "state_topic", state_topic);
    cJSON_AddStringToObject(root, "value_template", value_template);
    cJSON_AddStringToObject(root, "availability_topic", availability_topic);
    if (unit != nullptr && unit[0] != '\0') {
        cJSON_AddStringToObject(root, "unit_of_measurement", unit);
    }
    if (device_class != nullptr && device_class[0] != '\0') {
        cJSON_AddStringToObject(root, "device_class", device_class);
    }
    if (state_class != nullptr && state_class[0] != '\0') {
        cJSON_AddStringToObject(root, "state_class", state_class);
    }
    add_device_descriptor(root);

    char *json = cJSON_PrintUnformatted(root);
    if (json != nullptr) {
        if (publish_mqtt(topic, json, 1, true) < 0) {
            ESP_LOGW(kTag, "Failed to publish discovery for %s", object_suffix);
        }
        cJSON_free(json);
    }
    cJSON_Delete(root);
}

void publish_discovery() {
    publish_discovery_entity(
        "sensor",
        "temperature",
        "Temperatura",
        "{{ value_json.temperature_c }}",
        "°C",
        "temperature",
        "measurement");
    publish_discovery_entity(
        "sensor",
        "ph",
        "pH",
        "{{ value_json.ph }}",
        "",
        "",
        "measurement");
    publish_discovery_entity(
        "sensor",
        "ec",
        "Przewodność",
        "{{ value_json.ec_us_cm }}",
        "µS/cm",
        "",
        "measurement");
    publish_discovery_entity(
        "sensor",
        "ldr",
        "Jasność surowa",
        "{{ value_json.ldr_raw }}",
        "",
        "",
        "measurement");
    publish_discovery_entity(
        "sensor",
        "wifi_rssi",
        "Sygnał Wi-Fi sterownika",
        "{{ value_json.wifi_rssi_dbm }}",
        "dBm",
        "signal_strength",
        "measurement");
    publish_discovery_entity(
        "sensor",
        "espnow_rssi",
        "Sygnał ESP-NOW",
        "{{ value_json.espnow_rssi_dbm }}",
        "dBm",
        "signal_strength",
        "measurement");
    publish_discovery_entity(
        "sensor",
        "uptime",
        "Czas pracy CYD",
        "{{ value_json.uptime_seconds }}",
        "s",
        "duration",
        "measurement");
    publish_discovery_entity(
        "sensor",
        "free_heap",
        "Wolna pamięć CYD",
        "{{ value_json.free_heap_bytes }}",
        "B",
        "data_size",
        "measurement");
    publish_discovery_entity(
        "sensor",
        "configuration_revision",
        "Rewizja konfiguracji",
        "{{ value_json.configuration_revision }}",
        "",
        "",
        "");
    publish_discovery_entity(
        "sensor",
        "alarms",
        "Flagi alarmów",
        "{{ value_json.alarm_flags }}",
        "",
        "",
        "");
    publish_discovery_entity(
        "binary_sensor",
        "leak",
        "Wyciek",
        "{{ 'ON' if value_json.leak_detected else 'OFF' }}",
        "",
        "moisture",
        "");
    publish_discovery_entity(
        "binary_sensor",
        "water_low",
        "Niski poziom wody",
        "{{ 'ON' if value_json.water_level_low else 'OFF' }}",
        "",
        "problem",
        "");
    publish_discovery_entity(
        "binary_sensor",
        "controller_safe",
        "Sterownik bezpieczny",
        "{{ 'ON' if value_json.controller_safe else 'OFF' }}",
        "",
        "",
        "");
    publish_discovery_entity(
        "binary_sensor",
        "light_primary",
        "Światło główne",
        "{{ 'ON' if value_json.light_primary_on else 'OFF' }}",
        "",
        "",
        "");
    publish_discovery_entity(
        "binary_sensor",
        "light_secondary",
        "Światło roślinne",
        "{{ 'ON' if value_json.light_secondary_on else 'OFF' }}",
        "",
        "",
        "");
    publish_discovery_entity(
        "binary_sensor",
        "filter",
        "Filtr",
        "{{ 'ON' if value_json.filter_on else 'OFF' }}",
        "",
        "",
        "");
    publish_discovery_entity(
        "binary_sensor",
        "aerator",
        "Napowietrzanie",
        "{{ 'ON' if value_json.aerator_on else 'OFF' }}",
        "",
        "",
        "");
    publish_discovery_entity(
        "binary_sensor",
        "heater",
        "Grzałka",
        "{{ 'ON' if value_json.heater_on else 'OFF' }}",
        "",
        "heat",
        "");
}

void publish_availability(bool online) {
    if (publish_mqtt(
            availability_topic, online ? "online" : "offline", 1, true) >= 0) {
        controller_reported_online = online;
    }
}

void publish_telemetry(const aquacyd::link::Frame &frame,
                       const aquacyd::link::TelemetryPayload &telemetry,
                       int8_t espnow_rssi) {
    cJSON *root = cJSON_CreateObject();
    if (root == nullptr) {
        ESP_LOGE(kTag, "Unable to allocate telemetry JSON");
        return;
    }
    const uint16_t flags = telemetry.flags;
    if ((flags & aquacyd::link::TelemetryTemperatureValid) != 0U) {
        cJSON_AddNumberToObject(
            root,
            "temperature_c",
            static_cast<double>(telemetry.temperature_milli_c) / 1000.0);
    } else {
        cJSON_AddNullToObject(root, "temperature_c");
    }
    if ((flags & aquacyd::link::TelemetryPhValid) != 0U) {
        cJSON_AddNumberToObject(
            root, "ph", static_cast<double>(telemetry.ph_milli) / 1000.0);
    } else {
        cJSON_AddNullToObject(root, "ph");
    }
    if ((flags & aquacyd::link::TelemetryEcValid) != 0U) {
        cJSON_AddNumberToObject(
            root,
            "ec_us_cm",
            static_cast<double>(telemetry.ec_milli_us_cm) / 1000.0);
    } else {
        cJSON_AddNullToObject(root, "ec_us_cm");
    }
    cJSON_AddNumberToObject(root, "ldr_raw", telemetry.ldr_raw);
    cJSON_AddNumberToObject(root, "relay_bits", telemetry.relay_bits);
    cJSON_AddBoolToObject(
        root,
        "light_primary_on",
        (telemetry.relay_bits &
         aquacyd::link::RelayLightPrimaryOn) != 0U);
    cJSON_AddBoolToObject(
        root,
        "light_secondary_on",
        (telemetry.relay_bits &
         aquacyd::link::RelayLightSecondaryOn) != 0U);
    cJSON_AddBoolToObject(
        root,
        "filter_on",
        (telemetry.relay_bits & aquacyd::link::RelayFilterOn) != 0U);
    cJSON_AddBoolToObject(
        root,
        "aerator_on",
        (telemetry.relay_bits & aquacyd::link::RelayAeratorOn) != 0U);
    cJSON_AddBoolToObject(
        root,
        "heater_on",
        (telemetry.relay_bits & aquacyd::link::RelayHeaterOn) != 0U);
    cJSON_AddNumberToObject(root, "alarm_flags", telemetry.alarm_flags);
    cJSON_AddNumberToObject(root, "uptime_seconds", telemetry.uptime_seconds);
    cJSON_AddNumberToObject(root, "free_heap_bytes", telemetry.free_heap_bytes);
    cJSON_AddNumberToObject(root, "wifi_rssi_dbm", telemetry.wifi_rssi_dbm);
    cJSON_AddNumberToObject(root, "espnow_rssi_dbm", espnow_rssi);
    cJSON_AddNumberToObject(
        root, "configuration_revision", telemetry.configuration_revision);
    cJSON_AddNumberToObject(root, "sequence", frame.sequence);
    cJSON_AddBoolToObject(
        root,
        "water_level_low",
        (flags & aquacyd::link::TelemetryWaterLevelLow) != 0U);
    cJSON_AddBoolToObject(
        root,
        "leak_detected",
        (flags & aquacyd::link::TelemetryLeakDetected) != 0U);
    cJSON_AddBoolToObject(
        root,
        "controller_safe",
        (flags & aquacyd::link::TelemetryControllerSafe) != 0U);

    char *json = cJSON_PrintUnformatted(root);
    if (json != nullptr) {
        if (publish_mqtt(state_topic, json, 1, true) < 0) {
            ESP_LOGW(kTag, "Unable to publish controller state");
        }
        cJSON_free(json);
    }
    cJSON_Delete(root);
}

const char *acknowledgement_status_name(
    aquacyd::link::AcknowledgementStatus status) {
    switch (status) {
    case aquacyd::link::AcknowledgementStatus::Accepted:
        return "accepted";
    case aquacyd::link::AcknowledgementStatus::Duplicate:
        return "duplicate";
    case aquacyd::link::AcknowledgementStatus::Rejected:
        return "rejected";
    case aquacyd::link::AcknowledgementStatus::Conflict:
        return "conflict";
    case aquacyd::link::AcknowledgementStatus::Invalid:
        return "invalid";
    case aquacyd::link::AcknowledgementStatus::Busy:
        return "busy";
    case aquacyd::link::AcknowledgementStatus::Expired:
        return "expired";
    default:
        return "unknown";
    }
}

void publish_acknowledgement(
    const aquacyd::link::AcknowledgementPayload &acknowledgement,
    bool transport_timeout) {
    char json[256] = {};
    const int written = snprintf(
        json,
        sizeof(json),
        "{\"command_id\":\"%016" PRIx64
        "\",\"status\":%u,\"status_text\":\"%s\",\"reason_code\":%u,"
        "\"configuration_revision\":%" PRIu32
        ",\"transport_timeout\":%s}",
        acknowledgement.command_id,
        static_cast<unsigned int>(acknowledgement.status),
        acknowledgement_status_name(acknowledgement.status),
        static_cast<unsigned int>(acknowledgement.reason_code),
        acknowledgement.configuration_revision,
        transport_timeout ? "true" : "false");
    if (written > 0 && static_cast<size_t>(written) < sizeof(json)) {
        publish_mqtt(acknowledgement_topic, json, 1, false);
    }
}

void espnow_receive_callback(const esp_now_recv_info_t *info,
                             const uint8_t *data,
                             int length) {
    if (info == nullptr || data == nullptr || length <= 0 ||
        static_cast<size_t>(length) >
            aquacyd::link::kEspNowMaximumFrameSize ||
        received_queue == nullptr) {
        return;
    }
    ReceivedDatagram datagram = {};
    memcpy(datagram.source_mac, info->src_addr, sizeof(datagram.source_mac));
    datagram.length = static_cast<uint16_t>(length);
    datagram.rssi =
        info->rx_ctrl != nullptr ? info->rx_ctrl->rssi : INT8_MIN;
    memcpy(datagram.bytes, data, static_cast<size_t>(length));
    xQueueSend(received_queue, &datagram, 0U);
}

void espnow_send_callback(const uint8_t *,
                          esp_now_send_status_t status) {
    if (status != ESP_NOW_SEND_SUCCESS) {
        ESP_EARLY_LOGW(kTag, "ESP-NOW MAC delivery failed");
    }
}

esp_err_t initialize_espnow() {
    if (espnow_initialized) {
        return ESP_OK;
    }
    esp_err_t error = esp_now_init();
    if (error != ESP_OK) {
        return error;
    }
    error = esp_now_register_recv_cb(espnow_receive_callback);
    if (error != ESP_OK) {
        esp_now_deinit();
        return error;
    }
    error = esp_now_register_send_cb(espnow_send_callback);
    if (error != ESP_OK) {
        esp_now_deinit();
        return error;
    }
    error = esp_now_set_pmk(espnow_pmk);
    if (error != ESP_OK) {
        esp_now_deinit();
        return error;
    }

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, controller_mac, sizeof(peer.peer_addr));
    memcpy(peer.lmk, espnow_lmk, sizeof(peer.lmk));
    peer.channel = 0U;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = true;
    error = esp_now_add_peer(&peer);
    if (error != ESP_OK) {
        esp_now_deinit();
        return error;
    }
    espnow_initialized = true;
    ESP_LOGI(kTag, "Encrypted ESP-NOW peer initialized on the AP channel");
    return ESP_OK;
}

void process_acknowledgement(const aquacyd::link::Frame &frame) {
    aquacyd::link::AcknowledgementPayload acknowledgement = {};
    if (aquacyd::link::decode_acknowledgement_payload(
            frame.payload,
            frame.payload_length,
            &acknowledgement) != aquacyd::link::DecodeStatus::Ok) {
        ESP_LOGW(kTag, "Malformed acknowledgement payload");
        return;
    }
    portENTER_CRITICAL(&acknowledgement_lock);
    last_acknowledged_command_id = acknowledgement.command_id;
    portEXIT_CRITICAL(&acknowledgement_lock);
    if (command_task_handle != nullptr) {
        xTaskNotifyGive(command_task_handle);
    }
    publish_acknowledgement(acknowledgement, false);
}

void receive_task(void *) {
    ReceivedDatagram datagram = {};
    for (;;) {
        if (xQueueReceive(received_queue, &datagram, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (memcmp(
                datagram.source_mac,
                controller_mac,
                sizeof(controller_mac)) != 0) {
            ESP_LOGW(kTag, "Rejected datagram from an unknown peer");
            continue;
        }
        aquacyd::link::Frame frame = {};
        const aquacyd::link::DecodeStatus status =
            aquacyd::link::decode_frame(
                datagram.bytes, datagram.length, &frame);
        if (status != aquacyd::link::DecodeStatus::Ok) {
            ESP_LOGW(
                kTag,
                "Rejected frame with decode status %u",
                static_cast<unsigned int>(status));
            continue;
        }
        if (!receive_window.accept(frame.boot_id, frame.sequence)) {
            ESP_LOGW(kTag, "Rejected duplicate or replayed frame");
            continue;
        }
        if (frame.type == aquacyd::link::MessageType::Telemetry) {
            aquacyd::link::TelemetryPayload telemetry = {};
            if (aquacyd::link::decode_telemetry_payload(
                    frame.payload,
                    frame.payload_length,
                    &telemetry) != aquacyd::link::DecodeStatus::Ok) {
                ESP_LOGW(kTag, "Malformed telemetry payload");
                continue;
            }
            last_telemetry_ms = monotonic_ms();
            publish_telemetry(frame, telemetry, datagram.rssi);
            if (!controller_reported_online) {
                publish_availability(true);
            }
        } else if (
            frame.type == aquacyd::link::MessageType::Acknowledgement) {
            process_acknowledgement(frame);
        }
    }
}

bool json_integer_in_range(cJSON *item,
                           int64_t minimum,
                           int64_t maximum,
                           int64_t *output) {
    if (!cJSON_IsNumber(item) || output == nullptr) {
        return false;
    }
    const double value = item->valuedouble;
    if (!isfinite(value) ||
        value < static_cast<double>(minimum) ||
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

bool parse_action(const char *text, aquacyd::link::CommandAction *output) {
    if (text == nullptr || output == nullptr) {
        return false;
    }
    struct Mapping {
        const char *name;
        aquacyd::link::CommandAction value;
    };
    static constexpr Mapping mappings[] = {
        {"set_output", aquacyd::link::CommandAction::SetOutput},
        {"set_mode", aquacyd::link::CommandAction::SetMode},
        {"set_setpoint", aquacyd::link::CommandAction::SetSetpoint},
        {"trigger_feed", aquacyd::link::CommandAction::TriggerFeed},
        {"acknowledge_alarm",
         aquacyd::link::CommandAction::AcknowledgeAlarm},
        {"request_snapshot",
         aquacyd::link::CommandAction::RequestSnapshot},
        {"synchronize_time",
         aquacyd::link::CommandAction::SynchronizeTime}
    };
    for (const Mapping &mapping : mappings) {
        if (strcmp(text, mapping.name) == 0) {
            *output = mapping.value;
            return true;
        }
    }
    return false;
}

bool parse_target(const char *text, aquacyd::link::CommandTarget *output) {
    if (text == nullptr || output == nullptr) {
        return false;
    }
    struct Mapping {
        const char *name;
        aquacyd::link::CommandTarget value;
    };
    static constexpr Mapping mappings[] = {
        {"controller", aquacyd::link::CommandTarget::Controller},
        {"light_primary", aquacyd::link::CommandTarget::LightPrimary},
        {"light_secondary", aquacyd::link::CommandTarget::LightSecondary},
        {"filter", aquacyd::link::CommandTarget::Filter},
        {"aerator", aquacyd::link::CommandTarget::Aerator},
        {"heater", aquacyd::link::CommandTarget::Heater},
        {"co2", aquacyd::link::CommandTarget::Co2},
        {"feeder", aquacyd::link::CommandTarget::Feeder},
        {"ato", aquacyd::link::CommandTarget::AutomaticTopOff}
    };
    for (const Mapping &mapping : mappings) {
        if (strcmp(text, mapping.name) == 0) {
            *output = mapping.value;
            return true;
        }
    }
    return false;
}

bool parse_mqtt_command(const MqttCommand &message,
                        aquacyd::link::CommandPayload *output) {
    if (output == nullptr || message.length == 0U ||
        message.length > kMaximumMqttCommandBytes) {
        return false;
    }
    cJSON *root = cJSON_ParseWithLength(message.json, message.length);
    if (root == nullptr || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }

    cJSON *action_item = cJSON_GetObjectItemCaseSensitive(root, "action");
    cJSON *command_id_item =
        cJSON_GetObjectItemCaseSensitive(root, "command_id");
    cJSON *target_item = cJSON_GetObjectItemCaseSensitive(root, "target");
    cJSON *value_item = cJSON_GetObjectItemCaseSensitive(root, "value");
    cJSON *duration_item =
        cJSON_GetObjectItemCaseSensitive(root, "duration_ms");
    cJSON *revision_item =
        cJSON_GetObjectItemCaseSensitive(root, "expected_revision");

    aquacyd::link::CommandPayload parsed = {};
    uint64_t command_id = 0U;
    int64_t numeric_value = 0;
    int64_t duration = 0;
    int64_t revision = 0;
    const bool valid =
        cJSON_IsString(command_id_item) &&
        parse_command_id(command_id_item->valuestring, &command_id) &&
        cJSON_IsString(action_item) &&
        cJSON_IsString(target_item) &&
        parse_action(action_item->valuestring, &parsed.action) &&
        parse_target(target_item->valuestring, &parsed.target) &&
        json_integer_in_range(
            value_item, INT32_MIN, INT32_MAX, &numeric_value) &&
        (duration_item == nullptr ||
         json_integer_in_range(
             duration_item, 0, 86400000, &duration)) &&
        (revision_item == nullptr ||
         json_integer_in_range(
             revision_item, 0, UINT32_MAX, &revision));
    parsed.command_id = command_id;
    *output = parsed;
    if (valid) {
        parsed.value = static_cast<int32_t>(numeric_value);
        parsed.duration_ms = static_cast<uint32_t>(duration);
        parsed.expected_configuration_revision =
            static_cast<uint32_t>(revision);
        *output = parsed;
    }
    cJSON_Delete(root);
    return valid;
}

bool acknowledgement_matches(uint64_t command_id) {
    portENTER_CRITICAL(&acknowledgement_lock);
    const uint64_t acknowledged = last_acknowledged_command_id;
    portEXIT_CRITICAL(&acknowledgement_lock);
    return acknowledged == command_id;
}

void command_task(void *) {
    command_task_handle = xTaskGetCurrentTaskHandle();
    MqttCommand message = {};
    for (;;) {
        if (xQueueReceive(command_queue, &message, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        aquacyd::link::CommandPayload command = {};
        if (!parse_mqtt_command(message, &command)) {
            ESP_LOGW(kTag, "Rejected invalid MQTT command");
            if (command.command_id != 0U) {
                const aquacyd::link::AcknowledgementPayload invalid = {
                    command.command_id,
                    aquacyd::link::AcknowledgementStatus::Invalid,
                    2U,
                    0U
                };
                publish_acknowledgement(invalid, false);
            }
            continue;
        }

        aquacyd::link::Frame frame = {};
        frame.type = aquacyd::link::MessageType::Command;
        frame.flags = aquacyd::link::FlagAckRequested;
        frame.source_id = gateway_source_id;
        frame.boot_id = gateway_boot_id;
        frame.sequence = ++transmit_sequence;
        if (frame.sequence == 0U) {
            frame.sequence = ++transmit_sequence;
        }
        frame.issued_at_ms = monotonic_ms();
        frame.ttl_ms = kCommandTtlMs;
        if (aquacyd::link::encode_command_payload(
                command,
                frame.payload,
                sizeof(frame.payload),
                &frame.payload_length) != aquacyd::link::DecodeStatus::Ok) {
            ESP_LOGE(kTag, "Unable to encode command payload");
            continue;
        }
        uint8_t wire[aquacyd::link::kEspNowMaximumFrameSize] = {};
        size_t wire_length = 0U;
        if (aquacyd::link::encode_frame(
                frame,
                wire,
                sizeof(wire),
                &wire_length) != aquacyd::link::DecodeStatus::Ok) {
            ESP_LOGE(kTag, "Unable to encode command frame");
            continue;
        }

        bool acknowledged = false;
        portENTER_CRITICAL(&acknowledgement_lock);
        last_acknowledged_command_id = 0U;
        portEXIT_CRITICAL(&acknowledgement_lock);
        for (uint8_t attempt = 0U;
             attempt < kCommandMaximumAttempts && !acknowledged;
             ++attempt) {
            if (!espnow_initialized ||
                esp_now_send(
                    controller_mac,
                    wire,
                    wire_length) != ESP_OK) {
                ESP_LOGW(kTag, "Unable to queue ESP-NOW command");
            }
            ulTaskNotifyTake(
                pdTRUE, pdMS_TO_TICKS(kCommandAckTimeoutMs));
            acknowledged = acknowledgement_matches(command.command_id);
        }
        if (!acknowledged) {
            const aquacyd::link::AcknowledgementPayload timeout = {
                command.command_id,
                aquacyd::link::AcknowledgementStatus::Busy,
                kTransportTimeoutReason,
                command.expected_configuration_revision
            };
            publish_acknowledgement(timeout, true);
        }
    }
}

void availability_task(void *) {
    const uint32_t offline_timeout_ms =
        static_cast<uint32_t>(
            CONFIG_AQUACYD_CONTROLLER_OFFLINE_TIMEOUT_SECONDS) *
        1000U;
    for (;;) {
        const uint32_t last_seen = last_telemetry_ms;
        if (controller_reported_online &&
            last_seen != 0U &&
            static_cast<uint32_t>(monotonic_ms() - last_seen) >
                offline_timeout_ms) {
            publish_availability(false);
        }
        vTaskDelay(pdMS_TO_TICKS(1000U));
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
        esp_mqtt_client_subscribe(mqtt_client, command_topic, 1);
        publish_discovery();
        if (last_telemetry_ms != 0U) {
            publish_availability(true);
        } else {
            publish_availability(false);
        }
        ESP_LOGI(kTag, "MQTT connected");
    } else if (event_id == MQTT_EVENT_DISCONNECTED) {
        xEventGroupClearBits(connection_events, kMqttConnectedBit);
        controller_reported_online = false;
        ESP_LOGW(kTag, "MQTT disconnected");
    } else if (event_id == MQTT_EVENT_DATA) {
        if (event->current_data_offset != 0 ||
            event->data_len != event->total_data_len ||
            event->topic_len <= 0 ||
            static_cast<size_t>(event->topic_len) != strlen(command_topic) ||
            memcmp(
                event->topic,
                command_topic,
                static_cast<size_t>(event->topic_len)) != 0 ||
            event->data_len <= 0 ||
            static_cast<size_t>(event->data_len) >
                kMaximumMqttCommandBytes) {
            return;
        }
        MqttCommand command = {};
        command.length = static_cast<size_t>(event->data_len);
        memcpy(command.json, event->data, command.length);
        command.json[command.length] = '\0';
        if (xQueueSend(command_queue, &command, 0U) != pdTRUE) {
            ESP_LOGW(kTag, "MQTT command queue is full");
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
    configuration.broker.address.uri = CONFIG_AQUACYD_MQTT_BROKER_URI;
    configuration.credentials.username = CONFIG_AQUACYD_MQTT_USERNAME;
    configuration.credentials.authentication.password =
        CONFIG_AQUACYD_MQTT_PASSWORD;
    configuration.session.last_will.topic = availability_topic;
    configuration.session.last_will.msg = "offline";
    configuration.session.last_will.qos = 1;
    configuration.session.last_will.retain = 1;
    configuration.network.reconnect_timeout_ms = 3000;
    mqtt_client = esp_mqtt_client_init(&configuration);
    if (mqtt_client == nullptr) {
        ESP_LOGE(kTag, "MQTT client allocation failed");
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
        const esp_err_t espnow_error = initialize_espnow();
        if (espnow_error != ESP_OK) {
            ESP_LOGE(
                kTag,
                "ESP-NOW initialization failed: %s",
                esp_err_to_name(espnow_error));
        }
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
        CONFIG_AQUACYD_WIFI_SSID,
        sizeof(configuration.sta.ssid));
    strlcpy(
        reinterpret_cast<char *>(configuration.sta.password),
        CONFIG_AQUACYD_WIFI_PASSWORD,
        sizeof(configuration.sta.password));
    configuration.sta.threshold.authmode =
        strlen(CONFIG_AQUACYD_WIFI_PASSWORD) == 0U
            ? WIFI_AUTH_OPEN
            : WIFI_AUTH_WPA2_PSK;
    configuration.sta.pmf_cfg.capable = true;
    configuration.sta.pmf_cfg.required = false;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &configuration));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void initialize_identity() {
    uint8_t station_mac[kMacLength] = {};
    ESP_ERROR_CHECK(esp_read_mac(station_mac, ESP_MAC_WIFI_STA));
    gateway_source_id =
        static_cast<uint32_t>(station_mac[2]) << 24U |
        static_cast<uint32_t>(station_mac[3]) << 16U |
        static_cast<uint32_t>(station_mac[4]) << 8U |
        static_cast<uint32_t>(station_mac[5]);
    if (gateway_source_id == 0U) {
        gateway_source_id = 1U;
    }
    gateway_boot_id = esp_random();
    if (gateway_boot_id == 0U) {
        gateway_boot_id = 1U;
    }
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
    if (!configuration_is_valid()) {
        ESP_LOGE(kTag, "Run idf.py menuconfig before flashing the gateway");
        abort();
    }

    connection_events = xEventGroupCreate();
    received_queue = xQueueCreate(16U, sizeof(ReceivedDatagram));
    command_queue = xQueueCreate(8U, sizeof(MqttCommand));
    if (connection_events == nullptr ||
        received_queue == nullptr ||
        command_queue == nullptr) {
        ESP_LOGE(kTag, "Unable to allocate gateway RTOS resources");
        abort();
    }

    initialize_identity();
    const BaseType_t receiver_created = xTaskCreate(
        receive_task, "espnow_rx", 6144U, nullptr, 6U, nullptr);
    const BaseType_t command_created = xTaskCreate(
        command_task, "command_tx", 6144U, nullptr, 5U, nullptr);
    const BaseType_t availability_created = xTaskCreate(
        availability_task, "availability", 3072U, nullptr, 3U, nullptr);
    if (receiver_created != pdPASS ||
        command_created != pdPASS ||
        availability_created != pdPASS) {
        ESP_LOGE(kTag, "Unable to create gateway tasks");
        abort();
    }
    initialize_wifi();
    ESP_LOGI(kTag, "AquaCYD ESP32-C6 gateway started");
}
