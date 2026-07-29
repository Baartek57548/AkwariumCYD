#include "espnow_link.h"

#ifndef AQUACYD_ESPNOW_ENABLED
#define AQUACYD_ESPNOW_ENABLED 0
#endif

#if AQUACYD_ESPNOW_ENABLED

#include <Preferences.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_idf_version.h>
#include <esp_now.h>
#include <esp_random.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <math.h>
#include <string.h>

#include "aquacyd_link_protocol.h"
#include "aquarium_automation.h"
#include "gui_app.h"

namespace {

constexpr char NVS_NAMESPACE[] = "aq-espnow";
constexpr char NVS_CONFIG_KEY[] = "config";
constexpr uint32_t CONFIGURATION_MAGIC = 0x4151454EU;
constexpr uint16_t CONFIGURATION_VERSION = 1U;
constexpr size_t MAC_LENGTH = 6U;
constexpr size_t KEY_LENGTH = 16U;
constexpr size_t TELEMETRY_QUEUE_LENGTH = 1U;
constexpr size_t RECEIVED_QUEUE_LENGTH = 8U;
constexpr uint32_t LINK_TASK_STACK_BYTES = 7168U;
constexpr UBaseType_t LINK_TASK_PRIORITY = 1U;
constexpr uint32_t TELEMETRY_TTL_MS = 3000U;
constexpr uint32_t COMMAND_MAXIMUM_TTL_MS = 10000U;
constexpr uint32_t OVERRIDE_MINIMUM_MS = 30000U;
constexpr uint32_t OVERRIDE_MAXIMUM_MS = 86400000U;
constexpr uint32_t FEEDING_MINIMUM_MS = 60000U;
constexpr uint32_t FEEDING_MAXIMUM_MS = 3600000U;

enum CommandReasonCode : uint16_t {
    CommandReasonNone = 0U,
    CommandReasonRevisionConflict = 1U,
    CommandReasonInvalidParameters = 2U,
    CommandReasonUnsupported = 3U,
    CommandReasonRejectedByController = 4U,
    CommandReasonControllerNotReady = 5U,
    CommandReasonCommandIdConflict = 6U
};

struct StoredConfiguration {
    uint32_t magic;
    uint16_t version;
    uint8_t peer_mac[MAC_LENGTH];
    uint8_t pmk[KEY_LENGTH];
    uint8_t lmk[KEY_LENGTH];
    uint8_t fallback_channel;
    uint8_t reserved[3];
    uint32_t crc;
};

struct ReceivedDatagram {
    uint8_t source_mac[MAC_LENGTH];
    uint16_t length;
    uint8_t bytes[aquacyd::link::kEspNowMaximumFrameSize];
};

StoredConfiguration active_configuration = {};
StaticQueue_t telemetry_queue_storage;
uint8_t telemetry_queue_buffer[
    TELEMETRY_QUEUE_LENGTH * sizeof(RuntimeTelemetry)] = {};
QueueHandle_t telemetry_queue = nullptr;
StaticQueue_t received_queue_storage;
uint8_t received_queue_buffer[
    RECEIVED_QUEUE_LENGTH * sizeof(ReceivedDatagram)] = {};
QueueHandle_t received_queue = nullptr;
TaskHandle_t link_task_handle = nullptr;

aquacyd::link::SequenceWindow command_sequence_window;
uint32_t source_id = 0U;
uint32_t boot_id = 0U;
uint32_t transmit_sequence = 0U;
bool link_enabled = false;

uint32_t last_command_boot_id = 0U;
uint32_t last_command_sequence = 0U;
uint8_t last_acknowledgement[aquacyd::link::kEspNowMaximumFrameSize] = {};
size_t last_acknowledgement_length = 0U;

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

bool parse_mac_address(const char *text, uint8_t *output) {
    if (text == nullptr || output == nullptr || strlen(text) != 17U) {
        return false;
    }
    for (size_t index = 0U; index < MAC_LENGTH; ++index) {
        const size_t offset = index * 3U;
        const int high = hexadecimal_value(text[offset]);
        const int low = hexadecimal_value(text[offset + 1U]);
        if (high < 0 || low < 0 ||
            (index + 1U < MAC_LENGTH && text[offset + 2U] != ':')) {
            return false;
        }
        output[index] =
            static_cast<uint8_t>((static_cast<uint8_t>(high) << 4U) |
                                 static_cast<uint8_t>(low));
    }
    const uint8_t zero_mac[MAC_LENGTH] = {};
    const uint8_t broadcast_mac[MAC_LENGTH] = {
        0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU
    };
    return memcmp(output, zero_mac, sizeof(zero_mac)) != 0 &&
           memcmp(output, broadcast_mac, sizeof(broadcast_mac)) != 0 &&
           (output[0] & 0x01U) == 0U;
}

uint32_t configuration_crc(const StoredConfiguration &configuration) {
    return aquacyd::link::crc32(
        reinterpret_cast<const uint8_t *>(&configuration),
        offsetof(StoredConfiguration, crc));
}

bool configuration_valid(const StoredConfiguration &configuration) {
    const uint8_t zero_mac[MAC_LENGTH] = {};
    return configuration.magic == CONFIGURATION_MAGIC &&
           configuration.version == CONFIGURATION_VERSION &&
           configuration.fallback_channel >= 1U &&
           configuration.fallback_channel <= 13U &&
           memcmp(
               configuration.peer_mac,
               zero_mac,
               sizeof(zero_mac)) != 0 &&
           configuration.crc == configuration_crc(configuration);
}

bool load_configuration(StoredConfiguration *output) {
    if (output == nullptr) {
        return false;
    }
    Preferences preferences;
    if (!preferences.begin(NVS_NAMESPACE, true)) {
        return false;
    }
    StoredConfiguration candidate = {};
    const size_t bytes_read = preferences.getBytes(
        NVS_CONFIG_KEY, &candidate, sizeof(candidate));
    preferences.end();
    if (bytes_read != sizeof(candidate) ||
        !configuration_valid(candidate)) {
        memset(&candidate, 0, sizeof(candidate));
        return false;
    }
    *output = candidate;
    return true;
}

bool persist_configuration(const StoredConfiguration &configuration) {
    Preferences preferences;
    if (!preferences.begin(NVS_NAMESPACE, false)) {
        return false;
    }
    const size_t bytes_written = preferences.putBytes(
        NVS_CONFIG_KEY, &configuration, sizeof(configuration));
    preferences.end();
    return bytes_written == sizeof(configuration);
}

int32_t scaled_milli(float value) {
    if (!isfinite(value)) {
        return 0;
    }
    const double scaled = static_cast<double>(value) * 1000.0;
    if (scaled > static_cast<double>(INT32_MAX)) {
        return INT32_MAX;
    }
    if (scaled < static_cast<double>(INT32_MIN)) {
        return INT32_MIN;
    }
    return static_cast<int32_t>(lround(scaled));
}

uint16_t relay_bits(const GuiBleSnapshot &snapshot) {
    uint16_t bits = 0U;
    bits |= snapshot.light_on
                ? aquacyd::link::RelayLightPrimaryOn
                : 0U;
    bits |= snapshot.plant_light_on
                ? aquacyd::link::RelayLightSecondaryOn
                : 0U;
    bits |= snapshot.filter_on
                ? aquacyd::link::RelayFilterOn
                : 0U;
    bits |= snapshot.aeration_on
                ? aquacyd::link::RelayAeratorOn
                : 0U;
    bits |= snapshot.heater_on
                ? aquacyd::link::RelayHeaterOn
                : 0U;
    return bits;
}

bool send_frame(const aquacyd::link::Frame &frame,
                uint8_t *encoded_copy,
                size_t encoded_copy_capacity,
                size_t *encoded_copy_length) {
    uint8_t wire[aquacyd::link::kEspNowMaximumFrameSize] = {};
    size_t wire_length = 0U;
    if (aquacyd::link::encode_frame(
            frame,
            wire,
            sizeof(wire),
            &wire_length) != aquacyd::link::DecodeStatus::Ok) {
        return false;
    }
    if (encoded_copy != nullptr && encoded_copy_length != nullptr) {
        if (wire_length > encoded_copy_capacity) {
            return false;
        }
        memcpy(encoded_copy, wire, wire_length);
        *encoded_copy_length = wire_length;
    }
    return esp_now_send(
               active_configuration.peer_mac,
               wire,
               wire_length) == ESP_OK;
}

void send_telemetry(const RuntimeTelemetry &telemetry) {
    GuiBleSnapshot snapshot = {};
    if (!gui_app_ble_snapshot(&snapshot)) {
        return;
    }
    aquacyd::link::TelemetryPayload payload = {};
    payload.flags =
        (snapshot.temperature_valid
             ? aquacyd::link::TelemetryTemperatureValid
             : 0U) |
        (snapshot.ph_valid ? aquacyd::link::TelemetryPhValid : 0U) |
        (snapshot.ec_valid ? aquacyd::link::TelemetryEcValid : 0U) |
        (snapshot.ldr_valid ? aquacyd::link::TelemetryLdrValid : 0U) |
        (!snapshot.water_level_high
             ? aquacyd::link::TelemetryWaterLevelLow
             : 0U) |
        (snapshot.leak_detected
             ? aquacyd::link::TelemetryLeakDetected
             : 0U) |
        (snapshot.alarm_flags == aquarium::AlarmNone
             ? aquacyd::link::TelemetryControllerSafe
             : 0U);
    payload.temperature_milli_c = scaled_milli(snapshot.temperature);
    payload.ph_milli = scaled_milli(snapshot.ph);
    payload.ec_milli_us_cm = scaled_milli(snapshot.ec);
    payload.ldr_raw = static_cast<uint16_t>(
        constrain(snapshot.ldr, 0, 65535));
    payload.relay_bits = relay_bits(snapshot);
    payload.alarm_flags = snapshot.alarm_flags;
    payload.uptime_seconds = telemetry.uptime_seconds;
    payload.free_heap_bytes = telemetry.free_heap_bytes;
    payload.wifi_rssi_dbm = static_cast<int16_t>(
        WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);
    payload.configuration_revision = snapshot.configuration_revision;

    aquacyd::link::Frame frame = {};
    frame.type = aquacyd::link::MessageType::Telemetry;
    frame.source_id = source_id;
    frame.boot_id = boot_id;
    frame.sequence = ++transmit_sequence;
    if (frame.sequence == 0U) {
        frame.sequence = ++transmit_sequence;
    }
    frame.issued_at_ms = millis();
    frame.ttl_ms = TELEMETRY_TTL_MS;
    if (aquacyd::link::encode_telemetry_payload(
            payload,
            frame.payload,
            sizeof(frame.payload),
            &frame.payload_length) == aquacyd::link::DecodeStatus::Ok) {
        send_frame(frame, nullptr, 0U, nullptr);
    }
}

const char *target_name(aquacyd::link::CommandTarget target) {
    switch (target) {
    case aquacyd::link::CommandTarget::LightPrimary:
        return "light1";
    case aquacyd::link::CommandTarget::LightSecondary:
        return "light2";
    case aquacyd::link::CommandTarget::Filter:
        return "filter";
    case aquacyd::link::CommandTarget::Aerator:
        return "aeration";
    case aquacyd::link::CommandTarget::Heater:
        return "heater";
    case aquacyd::link::CommandTarget::Co2:
        return "co2";
    case aquacyd::link::CommandTarget::AutomaticTopOff:
        return "water_dosing";
    default:
        return nullptr;
    }
}

bool remote_output_target_allowed(aquacyd::link::CommandTarget target) {
    return target == aquacyd::link::CommandTarget::LightPrimary ||
           target == aquacyd::link::CommandTarget::LightSecondary ||
           target == aquacyd::link::CommandTarget::Filter ||
           target == aquacyd::link::CommandTarget::Aerator;
}

aquacyd::link::AcknowledgementStatus execute_command(
    const aquacyd::link::CommandPayload &command,
    uint16_t *reason_code) {
    if (reason_code == nullptr) {
        return aquacyd::link::AcknowledgementStatus::Invalid;
    }
    *reason_code = CommandReasonNone;
    if (command.action ==
        aquacyd::link::CommandAction::RequestSnapshot) {
        return aquacyd::link::AcknowledgementStatus::Accepted;
    }

    GuiBleSnapshot current_snapshot = {};
    if (!gui_app_ble_snapshot(&current_snapshot)) {
        *reason_code = CommandReasonControllerNotReady;
        return aquacyd::link::AcknowledgementStatus::Rejected;
    }
    if (command.expected_configuration_revision != 0U &&
        command.expected_configuration_revision !=
            current_snapshot.configuration_revision) {
        *reason_code = CommandReasonRevisionConflict;
        return aquacyd::link::AcknowledgementStatus::Conflict;
    }

    char action[32] = {};
    char json[192] = {};
    if (command.action == aquacyd::link::CommandAction::SetOutput) {
        const char *target = target_name(command.target);
        if (target == nullptr || !remote_output_target_allowed(command.target)) {
            *reason_code = CommandReasonUnsupported;
            return aquacyd::link::AcknowledgementStatus::Rejected;
        }
        if (
            (command.value != 0 && command.value != 1) ||
            command.duration_ms < OVERRIDE_MINIMUM_MS ||
            command.duration_ms > OVERRIDE_MAXIMUM_MS) {
            *reason_code = CommandReasonInvalidParameters;
            return aquacyd::link::AcknowledgementStatus::Invalid;
        }
        snprintf(action, sizeof(action), "set_timed_override");
        snprintf(
            json,
            sizeof(json),
            "{\"target\":\"%s\",\"state\":%s,\"durationSec\":%lu}",
            target,
            command.value == 1 ? "true" : "false",
            static_cast<unsigned long>(command.duration_ms / 1000U));
    } else if (
        command.action == aquacyd::link::CommandAction::TriggerFeed &&
        command.target == aquacyd::link::CommandTarget::Feeder) {
        if (command.duration_ms != 0U &&
            (command.duration_ms < FEEDING_MINIMUM_MS ||
             command.duration_ms > FEEDING_MAXIMUM_MS)) {
            *reason_code = CommandReasonInvalidParameters;
            return aquacyd::link::AcknowledgementStatus::Invalid;
        }
        const uint32_t duration_seconds =
            command.duration_ms == 0U ? 600U : command.duration_ms / 1000U;
        snprintf(action, sizeof(action), "start_feeding_mode");
        snprintf(
            json,
            sizeof(json),
            "{\"durationSec\":%lu,\"dispense\":true}",
            static_cast<unsigned long>(duration_seconds));
    } else {
        *reason_code = CommandReasonUnsupported;
        return aquacyd::link::AcknowledgementStatus::Rejected;
    }

    char command_id[17] = {};
    snprintf(
        command_id,
        sizeof(command_id),
        "%08lx%08lx",
        static_cast<unsigned long>(command.command_id >> 32U),
        static_cast<unsigned long>(command.command_id & 0xFFFFFFFFULL));
    bool duplicate = false;
    const GuiBleCommandResult result = gui_app_trusted_link_action(
        action, json, command_id, &duplicate);
    if (duplicate) {
        return aquacyd::link::AcknowledgementStatus::Duplicate;
    }
    if (!result.success) {
        if (result.code != nullptr &&
            strcmp(result.code, "command_id_conflict") == 0) {
            *reason_code = CommandReasonCommandIdConflict;
            return aquacyd::link::AcknowledgementStatus::Conflict;
        }
        *reason_code = CommandReasonRejectedByController;
        return aquacyd::link::AcknowledgementStatus::Rejected;
    }
    return aquacyd::link::AcknowledgementStatus::Accepted;
}

void acknowledge_command(
    const aquacyd::link::Frame &received,
    const aquacyd::link::CommandPayload &command,
    aquacyd::link::AcknowledgementStatus status,
    uint16_t reason_code) {
    aquacyd::link::AcknowledgementPayload acknowledgement = {};
    acknowledgement.command_id = command.command_id;
    acknowledgement.status = status;
    acknowledgement.reason_code = reason_code;
    GuiBleSnapshot current_snapshot = {};
    acknowledgement.configuration_revision =
        gui_app_ble_snapshot(&current_snapshot)
            ? current_snapshot.configuration_revision
            : 0U;

    aquacyd::link::Frame frame = {};
    frame.type = aquacyd::link::MessageType::Acknowledgement;
    frame.flags = aquacyd::link::FlagResponse;
    frame.source_id = source_id;
    frame.boot_id = boot_id;
    frame.sequence = ++transmit_sequence;
    if (frame.sequence == 0U) {
        frame.sequence = ++transmit_sequence;
    }
    frame.acknowledged_sequence = received.sequence;
    frame.issued_at_ms = millis();
    frame.ttl_ms = TELEMETRY_TTL_MS;
    if (aquacyd::link::encode_acknowledgement_payload(
            acknowledgement,
            frame.payload,
            sizeof(frame.payload),
            &frame.payload_length) != aquacyd::link::DecodeStatus::Ok) {
        return;
    }
    last_command_boot_id = received.boot_id;
    last_command_sequence = received.sequence;
    last_acknowledgement_length = 0U;
    send_frame(
        frame,
        last_acknowledgement,
        sizeof(last_acknowledgement),
        &last_acknowledgement_length);
}

void process_received(const ReceivedDatagram &datagram) {
    if (memcmp(
            datagram.source_mac,
            active_configuration.peer_mac,
            sizeof(active_configuration.peer_mac)) != 0) {
        return;
    }
    aquacyd::link::Frame frame = {};
    if (aquacyd::link::decode_frame(
            datagram.bytes,
            datagram.length,
            &frame) != aquacyd::link::DecodeStatus::Ok ||
        frame.type != aquacyd::link::MessageType::Command ||
        frame.ttl_ms == 0U ||
        frame.ttl_ms > COMMAND_MAXIMUM_TTL_MS) {
        return;
    }
    if (frame.boot_id == last_command_boot_id &&
        frame.sequence == last_command_sequence &&
        last_acknowledgement_length != 0U) {
        esp_now_send(
            active_configuration.peer_mac,
            last_acknowledgement,
            last_acknowledgement_length);
        return;
    }
    if (!command_sequence_window.accept(frame.boot_id, frame.sequence)) {
        return;
    }

    aquacyd::link::CommandPayload command = {};
    const aquacyd::link::DecodeStatus decode_status =
        aquacyd::link::decode_command_payload(
            frame.payload, frame.payload_length, &command);
    if (decode_status != aquacyd::link::DecodeStatus::Ok) {
        return;
    }
    uint16_t reason_code = 0U;
    const aquacyd::link::AcknowledgementStatus status =
        execute_command(command, &reason_code);
    acknowledge_command(frame, command, status, reason_code);
}

void queue_received_datagram(const uint8_t *source_mac,
                             const uint8_t *data,
                             int length) {
    if (source_mac == nullptr || data == nullptr || length <= 0 ||
        static_cast<size_t>(length) >
            aquacyd::link::kEspNowMaximumFrameSize ||
        received_queue == nullptr) {
        return;
    }
    ReceivedDatagram datagram = {};
    memcpy(datagram.source_mac, source_mac, sizeof(datagram.source_mac));
    datagram.length = static_cast<uint16_t>(length);
    memcpy(datagram.bytes, data, static_cast<size_t>(length));
    xQueueSend(received_queue, &datagram, 0U);
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
void receive_callback(const esp_now_recv_info_t *info,
                      const uint8_t *data,
                      int length) {
    if (info != nullptr) {
        queue_received_datagram(info->src_addr, data, length);
    }
}
#else
void receive_callback(const uint8_t *source_mac,
                      const uint8_t *data,
                      int length) {
    queue_received_datagram(source_mac, data, length);
}
#endif

bool initialize_radio() {
    if (WiFi.getMode() == WIFI_OFF &&
        !WiFi.mode(WIFI_STA)) {
        return false;
    }
    if (WiFi.status() != WL_CONNECTED) {
        esp_wifi_set_channel(
            active_configuration.fallback_channel,
            WIFI_SECOND_CHAN_NONE);
    }
    if (esp_now_init() != ESP_OK ||
        esp_now_register_recv_cb(receive_callback) != ESP_OK ||
        esp_now_set_pmk(active_configuration.pmk) != ESP_OK) {
        esp_now_deinit();
        return false;
    }
    esp_now_peer_info_t peer = {};
    memcpy(
        peer.peer_addr,
        active_configuration.peer_mac,
        sizeof(peer.peer_addr));
    memcpy(peer.lmk, active_configuration.lmk, sizeof(peer.lmk));
    peer.channel = 0U;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = true;
    if (esp_now_add_peer(&peer) != ESP_OK) {
        esp_now_deinit();
        return false;
    }
    return true;
}

void link_task(void *) {
    RuntimeTelemetry telemetry = {};
    ReceivedDatagram datagram = {};
    for (;;) {
        while (xQueueReceive(
                   received_queue, &datagram, 0U) == pdTRUE) {
            process_received(datagram);
        }
        if (xQueueReceive(
                telemetry_queue,
                &telemetry,
                pdMS_TO_TICKS(25U)) == pdTRUE) {
            send_telemetry(telemetry);
        }
    }
}

void initialize_identity() {
    uint8_t station_mac[MAC_LENGTH] = {};
    esp_read_mac(station_mac, ESP_MAC_WIFI_STA);
    source_id =
        static_cast<uint32_t>(station_mac[2]) << 24U |
        static_cast<uint32_t>(station_mac[3]) << 16U |
        static_cast<uint32_t>(station_mac[4]) << 8U |
        static_cast<uint32_t>(station_mac[5]);
    if (source_id == 0U) {
        source_id = 1U;
    }
    boot_id = esp_random();
    if (boot_id == 0U) {
        boot_id = 1U;
    }
}

} // namespace

bool espnow_link_initialize(void) {
    if (link_task_handle != nullptr) {
        return true;
    }
    if (!load_configuration(&active_configuration)) {
        Serial.println("ESPNOW: brak konfiguracji; lacze pozostaje wylaczone.");
        return true;
    }
    telemetry_queue = xQueueCreateStatic(
        TELEMETRY_QUEUE_LENGTH,
        sizeof(RuntimeTelemetry),
        telemetry_queue_buffer,
        &telemetry_queue_storage);
    received_queue = xQueueCreateStatic(
        RECEIVED_QUEUE_LENGTH,
        sizeof(ReceivedDatagram),
        received_queue_buffer,
        &received_queue_storage);
    if (telemetry_queue == nullptr ||
        received_queue == nullptr ||
        !initialize_radio()) {
        Serial.println("ESPNOW: inicjalizacja szyfrowanego lacza nieudana.");
        return false;
    }
    initialize_identity();
    const BaseType_t created = xTaskCreatePinnedToCore(
        link_task,
        "aquacyd_espnow",
        LINK_TASK_STACK_BYTES,
        nullptr,
        LINK_TASK_PRIORITY,
        &link_task_handle,
        0);
    if (created != pdPASS) {
        link_task_handle = nullptr;
        esp_now_deinit();
        return false;
    }
    link_enabled = true;
    Serial.println("ESPNOW: szyfrowane lacze z bramka C6 aktywne.");
    return true;
}

bool espnow_link_publish(const RuntimeTelemetry &telemetry) {
    return link_enabled &&
           telemetry_queue != nullptr &&
           xQueueOverwrite(telemetry_queue, &telemetry) == pdTRUE;
}

bool espnow_link_configure(const char *peer_mac,
                           const char *pmk_hex,
                           const char *lmk_hex,
                           uint8_t fallback_channel) {
    StoredConfiguration configuration = {};
    configuration.magic = CONFIGURATION_MAGIC;
    configuration.version = CONFIGURATION_VERSION;
    configuration.fallback_channel = fallback_channel;
    if (!parse_mac_address(peer_mac, configuration.peer_mac) ||
        !parse_hexadecimal(
            pmk_hex, configuration.pmk, sizeof(configuration.pmk)) ||
        !parse_hexadecimal(
            lmk_hex, configuration.lmk, sizeof(configuration.lmk)) ||
        fallback_channel < 1U ||
        fallback_channel > 13U) {
        memset(&configuration, 0, sizeof(configuration));
        return false;
    }
    configuration.crc = configuration_crc(configuration);
    const bool saved = persist_configuration(configuration);
    memset(&configuration, 0, sizeof(configuration));
    return saved;
}

bool espnow_link_clear_configuration(void) {
    Preferences preferences;
    if (!preferences.begin(NVS_NAMESPACE, false)) {
        return false;
    }
    const bool removed =
        !preferences.isKey(NVS_CONFIG_KEY) ||
        preferences.remove(NVS_CONFIG_KEY);
    preferences.end();
    return removed;
}

bool espnow_link_is_enabled(void) {
    return link_enabled;
}

#else

bool espnow_link_initialize(void) {
    return true;
}

bool espnow_link_publish(const RuntimeTelemetry &) {
    return false;
}

bool espnow_link_configure(const char *,
                           const char *,
                           const char *,
                           uint8_t) {
    return false;
}

bool espnow_link_clear_configuration(void) {
    return false;
}

bool espnow_link_is_enabled(void) {
    return false;
}

#endif
