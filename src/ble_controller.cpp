#include <Arduino.h>
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "config.h"
#include "gui_app.h"

namespace {

constexpr char BLE_DEVICE_NAME[] = "cydAkwarium";
constexpr char BLE_SERVICE_UUID[] = "7c4a0001-6e8d-4f84-9f3f-2c3a0a0c0001";
constexpr char BLE_COMMAND_UUID[] = "7c4a0002-6e8d-4f84-9f3f-2c3a0a0c0001";
constexpr char BLE_EVENTS_UUID[] = "7c4a0003-6e8d-4f84-9f3f-2c3a0a0c0001";
constexpr char BLE_INFO_UUID[] = "7c4a0004-6e8d-4f84-9f3f-2c3a0a0c0001";
constexpr size_t BLE_COMMAND_MAX_BYTES = 4096U;
constexpr size_t BLE_FRAME_PAYLOAD_BYTES = 156U;
constexpr uint8_t BLE_COMMAND_MAX_PARTS = 32U;
constexpr uint8_t BLE_EVENT_MAX_PARTS = 64U;
constexpr uint32_t BLE_FRAGMENT_TIMEOUT_MS = 10000UL;
constexpr uint32_t BLE_STATUS_INTERVAL_MS = 2000UL;
constexpr uint32_t BLE_NOTIFY_GAP_MS = 12UL;
constexpr UBaseType_t BLE_COMMAND_QUEUE_LENGTH = 2U;
constexpr size_t BLE_MESSAGE_BUFFER_BYTES = 8192U;

enum class CallbackError : uint8_t {
    None = 0U,
    InvalidCommand = 1U,
    QueueFull = 2U,
    InvalidFragment = 3U
};

struct BleQueuedCommand {
    char json[BLE_COMMAND_MAX_BYTES + 1U];
};

struct IncomingAssembly {
    bool active;
    uint16_t message_id;
    uint8_t part_count;
    uint8_t next_part;
    size_t length;
    uint32_t last_fragment_ms;
    char json[BLE_COMMAND_MAX_BYTES + 1U];
};

QueueHandle_t command_queue = nullptr;
NimBLECharacteristic *events_characteristic = nullptr;
volatile bool client_connected = false;
volatile CallbackError pending_callback_error = CallbackError::None;
uint16_t outgoing_message_id = 1U;
IncomingAssembly incoming = {};
BleQueuedCommand processing_command = {};
BleQueuedCommand callback_command = {};
char outgoing_json[BLE_MESSAGE_BUFFER_BYTES] = {};

bool json_scalar_terminated(const char *cursor) {
    if (cursor == nullptr) {
        return false;
    }
    while (*cursor == ' ' || *cursor == '\t' ||
           *cursor == '\r' || *cursor == '\n') {
        ++cursor;
    }
    return *cursor == '\0' || *cursor == ',' ||
           *cursor == '}' || *cursor == ']';
}

void reset_incoming() {
    incoming.active = false;
    incoming.message_id = 0U;
    incoming.part_count = 0U;
    incoming.next_part = 0U;
    incoming.length = 0U;
    incoming.last_fragment_ms = 0U;
    incoming.json[0] = '\0';
}

bool extract_json_int(const char *json, const char *key, int *out) {
    if (json == nullptr || key == nullptr || out == nullptr) {
        return false;
    }
    char pattern[40];
    const int pattern_length = snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    if (pattern_length <= 0 || static_cast<size_t>(pattern_length) >= sizeof(pattern)) {
        return false;
    }
    const char *value = strstr(json, pattern);
    if (value == nullptr) {
        return false;
    }
    value += pattern_length;
    char *end = nullptr;
    const long parsed = strtol(value, &end, 10);
    if (end == value || parsed < 0L || parsed > 65535L ||
        !json_scalar_terminated(end)) {
        return false;
    }
    *out = static_cast<int>(parsed);
    return true;
}

bool extract_json_string(const char *json, const char *key, char *out, size_t out_size) {
    if (json == nullptr || key == nullptr || out == nullptr || out_size < 2U) {
        return false;
    }
    char pattern[40];
    const int pattern_length = snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    if (pattern_length <= 0 || static_cast<size_t>(pattern_length) >= sizeof(pattern)) {
        return false;
    }
    const char *value = strstr(json, pattern);
    if (value == nullptr) {
        return false;
    }
    value += pattern_length;
    size_t written = 0U;
    while (*value != '\0' && *value != '"') {
        const unsigned char current = static_cast<unsigned char>(*value);
        if (current < 0x20U || *value == '\\' || written + 1U >= out_size) {
            return false;
        }
        out[written++] = *value++;
    }
    if (*value != '"' || written == 0U) {
        return false;
    }
    out[written] = '\0';
    return true;
}

bool extract_json_bool(const char *json, const char *key, bool *out) {
    if (json == nullptr || key == nullptr || out == nullptr) {
        return false;
    }
    char pattern[40];
    const int pattern_length = snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    if (pattern_length <= 0 || static_cast<size_t>(pattern_length) >= sizeof(pattern)) {
        return false;
    }
    const char *value = strstr(json, pattern);
    if (value == nullptr) {
        return false;
    }
    value += pattern_length;
    if (strncmp(value, "true", 4U) == 0 &&
        json_scalar_terminated(value + 4U)) {
        *out = true;
        return true;
    }
    if (strncmp(value, "false", 5U) == 0 &&
        json_scalar_terminated(value + 5U)) {
        *out = false;
        return true;
    }
    return false;
}

bool enqueue_command(const char *json, size_t length) {
    if (command_queue == nullptr || json == nullptr || length == 0U ||
        length > BLE_COMMAND_MAX_BYTES || memchr(json, '\0', length) != nullptr) {
        return false;
    }
    memcpy(callback_command.json, json, length);
    callback_command.json[length] = '\0';
    return xQueueSend(command_queue, &callback_command, 0) == pdTRUE;
}

void send_json(const char *json) {
    if (!client_connected || events_characteristic == nullptr || json == nullptr) {
        return;
    }
    const size_t length = strlen(json);
    if (length == 0U) {
        return;
    }
    const size_t part_count_size =
        (length + BLE_FRAME_PAYLOAD_BYTES - 1U) / BLE_FRAME_PAYLOAD_BYTES;
    if (part_count_size == 0U || part_count_size > BLE_EVENT_MAX_PARTS) {
        Serial.printf("BLE: response too large (%u bytes).\n", static_cast<unsigned>(length));
        return;
    }
    const uint8_t part_count = static_cast<uint8_t>(part_count_size);
    const uint16_t message_id = outgoing_message_id++;
    if (outgoing_message_id == 0U) {
        outgoing_message_id = 1U;
    }

    for (uint8_t part_index = 0U; part_index < part_count; ++part_index) {
        const size_t offset = static_cast<size_t>(part_index) * BLE_FRAME_PAYLOAD_BYTES;
        const size_t remaining = length - offset;
        const size_t payload_length = remaining < BLE_FRAME_PAYLOAD_BYTES
                                          ? remaining
                                          : BLE_FRAME_PAYLOAD_BYTES;
        uint8_t frame[4U + BLE_FRAME_PAYLOAD_BYTES];
        frame[0] = static_cast<uint8_t>(message_id & 0xFFU);
        frame[1] = static_cast<uint8_t>((message_id >> 8U) & 0xFFU);
        frame[2] = part_index;
        frame[3] = part_count;
        memcpy(frame + 4U, json + offset, payload_length);
        events_characteristic->setValue(frame, payload_length + 4U);
        events_characteristic->notify();
        vTaskDelay(pdMS_TO_TICKS(BLE_NOTIFY_GAP_MS));
    }
}

void send_response(int id, const GuiBleCommandResult &result) {
    char response[320];
    const int length = snprintf(
        response,
        sizeof(response),
        "{\"type\":\"response\",\"id\":%d,\"ok\":%s,\"code\":\"%s\",\"message\":\"%s\"}",
        id,
        result.success ? "true" : "false",
        result.code != nullptr ? result.code : "internal_error",
        result.message != nullptr ? result.message : "Brak opisu odpowiedzi.");
    if (length > 0 && static_cast<size_t>(length) < sizeof(response)) {
        send_json(response);
    }
}

void send_v2_response(int id,
                      const char *command_id,
                      const GuiBleCommandResult &result,
                      bool duplicate) {
    char response[512];
    const int length = snprintf(
        response,
        sizeof(response),
        "{\"type\":\"response\",\"v\":2,\"id\":%d,\"commandId\":\"%s\","
        "\"ok\":%s,\"code\":\"%s\",\"message\":\"%s\","
        "\"ts\":%lu,\"tsSource\":\"uptime\",\"duplicate\":%s}",
        id,
        command_id != nullptr ? command_id : "",
        result.success ? "true" : "false",
        result.code != nullptr ? result.code : "internal_error",
        result.message != nullptr ? result.message : "Brak opisu odpowiedzi.",
        static_cast<unsigned long>(millis() / 1000UL),
        duplicate ? "true" : "false");
    if (length > 0 && static_cast<size_t>(length) < sizeof(response)) {
        send_json(response);
    }
}

void send_v2_auth_response(int id,
                           const GuiV2AuthResult &result,
                           const char *token) {
    char response[640];
    int length = 0;
    if (result.success) {
        length = snprintf(
            response,
            sizeof(response),
            "{\"type\":\"auth\",\"v\":2,\"id\":%d,\"ok\":true,"
            "\"code\":\"authenticated\",\"ts\":%lu,\"tsSource\":\"uptime\","
            "\"data\":{\"sessionToken\":\"%s\",\"expiresInSec\":%lu}}",
            id,
            static_cast<unsigned long>(millis() / 1000UL),
            token != nullptr ? token : "",
            static_cast<unsigned long>(result.expires_in_seconds));
    } else {
        length = snprintf(
            response,
            sizeof(response),
            "{\"type\":\"auth\",\"v\":2,\"id\":%d,\"ok\":false,"
            "\"code\":\"%s\",\"message\":\"%s\",\"ts\":%lu,"
            "\"tsSource\":\"uptime\",\"retryAfterSec\":%lu}",
            id,
            result.code != nullptr ? result.code : "authentication_failed",
            result.message != nullptr ? result.message : "",
            static_cast<unsigned long>(millis() / 1000UL),
            static_cast<unsigned long>(result.retry_after_seconds));
    }
    if (length > 0 && static_cast<size_t>(length) < sizeof(response)) {
        send_json(response);
    }
}

bool send_capabilities() {
    if (!gui_app_v2_capabilities_json(outgoing_json, sizeof(outgoing_json))) {
        return false;
    }
    send_json(outgoing_json);
    return true;
}

bool send_legacy_status() {
    GuiBleSnapshot snapshot = {};
    if (!gui_app_ble_snapshot(&snapshot)) {
        return false;
    }
    char status[640];
    const int length = snprintf(
        status,
        sizeof(status),
        "{\"type\":\"status\",\"v\":%u,\"dev\":%s,\"uptime\":%lu,\"heap\":%lu,"
        "\"temp\":%.2f,\"tempValid\":%s,\"targetTemp\":%.2f,"
        "\"ph\":%.3f,\"phValid\":%s,\"ec\":%.1f,\"ecValid\":%s,"
        "\"ldr\":%d,\"ldrValid\":%s,\"alarmFlags\":%u,\"water\":%s,\"leak\":%s,"
        "\"outputs\":{\"light\":%s,\"plant\":%s,\"filter\":%s,\"heater\":%s,\"aeration\":%s}}",
        static_cast<unsigned>(snapshot.protocol_version),
        snapshot.developer_mode ? "true" : "false",
        static_cast<unsigned long>(snapshot.uptime_seconds),
        static_cast<unsigned long>(snapshot.free_heap_bytes),
        static_cast<double>(snapshot.temperature),
        snapshot.temperature_valid ? "true" : "false",
        static_cast<double>(snapshot.target_temperature),
        static_cast<double>(snapshot.ph),
        snapshot.ph_valid ? "true" : "false",
        static_cast<double>(snapshot.ec),
        snapshot.ec_valid ? "true" : "false",
        snapshot.ldr,
        snapshot.ldr_valid ? "true" : "false",
        static_cast<unsigned>(snapshot.alarm_flags),
        snapshot.water_level_high ? "true" : "false",
        snapshot.leak_detected ? "true" : "false",
        snapshot.light_on ? "true" : "false",
        snapshot.plant_light_on ? "true" : "false",
        snapshot.filter_on ? "true" : "false",
        snapshot.heater_on ? "true" : "false",
        snapshot.aeration_on ? "true" : "false");
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(status)) {
        return false;
    }
    send_json(status);
    return true;
}

bool send_full_status() {
    if (!gui_app_ble_full_status_json(outgoing_json, sizeof(outgoing_json))) {
        return false;
    }
    send_json(outgoing_json);
    return true;
}

void process_command(const BleQueuedCommand &queued) {
    int id = 0;
    if (!extract_json_int(queued.json, "id", &id)) {
        send_response(0, {false, "invalid_id", "Brak poprawnego identyfikatora komendy."});
        return;
    }
    char operation[24];
    if (!extract_json_string(queued.json, "op", operation, sizeof(operation))) {
        send_response(id, {false, "invalid_operation", "Brak poprawnej operacji BLE."});
        return;
    }
    if (strcmp(operation, "capabilities") == 0) {
        if (!send_capabilities()) {
            send_response(
                id,
                {false, "capabilities_unavailable", "Nie mozna zbudowac capabilities."});
            return;
        }
        send_v2_response(
            id,
            "",
            {true, "ok", "Capabilities protokolu v2 wyslane."},
            false);
        return;
    }
    if (strcmp(operation, "status") == 0) {
        if (!send_legacy_status()) {
            send_response(id, {false, "not_ready", "Telemetria nie jest jeszcze gotowa."});
            return;
        }
        send_response(id, {true, "ok", "Telemetria BLE wyslana."});
        return;
    }
    if (strcmp(operation, "full_status") == 0) {
        if (!send_full_status()) {
            send_response(id, {false, "status_too_large", "Nie mozna zbudowac pelnego statusu BLE."});
            return;
        }
        send_response(id, {true, "ok", "Pelny status BLE wyslany."});
        return;
    }

    char pin[16] = {};
    extract_json_string(queued.json, "pin", pin, sizeof(pin));
    if (strcmp(operation, "auth") == 0) {
        char token[33] = {};
        const GuiV2AuthResult result =
            gui_app_v2_auth(pin, token, sizeof(token));
        send_v2_auth_response(id, result, token);
        // The typed auth event carries the token, while the generic response
        // completes the transport-level command future used by Flutter.
        send_v2_response(
            id,
            "",
            {result.success, result.code, result.message},
            false);
        return;
    }
    if (strcmp(operation, "logs") == 0) {
        const GuiBleCommandResult auth = gui_app_ble_action("auth_check", queued.json, pin);
        if (!auth.success) {
            send_response(id, auth);
            return;
        }
        if (!gui_app_ble_logs_json(outgoing_json, sizeof(outgoing_json), pin)) {
            send_response(id, {false, "logs_failed", "Nie mozna zbudowac logow BLE."});
            return;
        }
        send_json(outgoing_json);
        send_response(id, {true, "ok", "Logi BLE wyslane."});
        return;
    }
    if (strcmp(operation, "diagnostics") == 0) {
        const GuiBleCommandResult auth = gui_app_ble_action("auth_check", queued.json, pin);
        if (!auth.success) {
            send_response(id, auth);
            return;
        }
        if (!gui_app_ble_diagnostics_json(outgoing_json, sizeof(outgoing_json), pin)) {
            send_response(id, {false, "diagnostics_failed", "Nie mozna zbudowac diagnostyki BLE."});
            return;
        }
        send_json(outgoing_json);
        send_response(id, {true, "ok", "Diagnostyka BLE wyslana."});
        return;
    }
    if (strcmp(operation, "action") == 0) {
        char action[40];
        if (!extract_json_string(queued.json, "name", action, sizeof(action))) {
            send_response(id, {false, "invalid_action", "Brak nazwy akcji BLE."});
            return;
        }
        char command_id[49] = {};
        char token[33] = {};
        extract_json_string(
            queued.json, "commandId", command_id, sizeof(command_id));
        extract_json_string(queued.json, "token", token, sizeof(token));
        if (command_id[0] != '\0' || token[0] != '\0') {
            bool duplicate = false;
            char replay_code[40] = {};
            char replay_message[128] = {};
            const GuiBleCommandResult result = gui_app_v2_action(
                action,
                queued.json,
                pin,
                token,
                command_id,
                &duplicate,
                replay_code,
                sizeof(replay_code),
                replay_message,
                sizeof(replay_message));
            send_v2_response(id, command_id, result, duplicate);
            if (result.success) {
                send_full_status();
            }
            return;
        }
        if (pin[0] == '\0') {
            send_response(id, {false, "pin_invalid", "Brak poprawnego PIN-u."});
            return;
        }
        const GuiBleCommandResult result = gui_app_ble_action(action, queued.json, pin);
        send_response(id, result);
        if (result.success) {
            send_full_status();
        }
        return;
    }

    // Operacje v1 pozostaja aktywne, aby starsze wydania aplikacji nadal dzialaly.
    if (strcmp(operation, "feed") == 0) {
        if (pin[0] == '\0') {
            send_response(id, {false, "pin_invalid", "Brak poprawnego PIN-u."});
            return;
        }
        send_response(id, gui_app_ble_feed(pin));
        send_legacy_status();
        return;
    }
    if (strcmp(operation, "set") == 0) {
        if (pin[0] == '\0') {
            send_response(id, {false, "pin_invalid", "Brak poprawnego PIN-u."});
            return;
        }
        char target[20];
        bool state = false;
        if (!extract_json_string(queued.json, "target", target, sizeof(target)) ||
            !extract_json_bool(queued.json, "state", &state)) {
            send_response(id, {false, "invalid_arguments", "Nieprawidlowe parametry modulu."});
            return;
        }
        send_response(id, gui_app_ble_set_output(target, state, pin));
        send_legacy_status();
        return;
    }
    send_response(id, {false, "unknown_operation", "Nieznana operacja BLE."});
}

class ControllerServerCallbacks final : public NimBLEServerCallbacks {
public:
    void onConnect(NimBLEServer *, NimBLEConnInfo &) override {
        client_connected = true;
        reset_incoming();
    }

    void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int) override {
        client_connected = false;
        reset_incoming();
        NimBLEDevice::startAdvertising();
    }
};

class CommandCharacteristicCallbacks final : public NimBLECharacteristicCallbacks {
public:
    void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &) override {
        if (characteristic == nullptr || command_queue == nullptr) {
            return;
        }
        const std::string value = characteristic->getValue();
        if (value.empty()) {
            pending_callback_error = CallbackError::InvalidCommand;
            return;
        }

        const bool unframed_json =
            value.size() >= 2U && value.front() == '{' && value.back() == '}';
        if (unframed_json) {
            reset_incoming();
            pending_callback_error = enqueue_command(value.data(), value.size())
                                         ? CallbackError::None
                                         : (value.size() > BLE_COMMAND_MAX_BYTES
                                                ? CallbackError::InvalidCommand
                                                : CallbackError::QueueFull);
            return;
        }

        if (value.size() <= 4U) {
            reset_incoming();
            pending_callback_error = CallbackError::InvalidFragment;
            return;
        }
        const uint8_t *bytes = reinterpret_cast<const uint8_t *>(value.data());
        const uint16_t message_id = static_cast<uint16_t>(bytes[0]) |
                                    (static_cast<uint16_t>(bytes[1]) << 8U);
        const uint8_t part_index = bytes[2];
        const uint8_t part_count = bytes[3];
        const size_t payload_length = value.size() - 4U;
        const uint32_t now_ms = millis();

        if (part_count == 0U || part_count > BLE_COMMAND_MAX_PARTS ||
            part_index >= part_count || payload_length > BLE_FRAME_PAYLOAD_BYTES) {
            reset_incoming();
            pending_callback_error = CallbackError::InvalidFragment;
            return;
        }
        if (incoming.active && now_ms - incoming.last_fragment_ms > BLE_FRAGMENT_TIMEOUT_MS) {
            reset_incoming();
        }
        if (part_index == 0U) {
            reset_incoming();
            incoming.active = true;
            incoming.message_id = message_id;
            incoming.part_count = part_count;
        }
        if (!incoming.active || incoming.message_id != message_id ||
            incoming.part_count != part_count || incoming.next_part != part_index ||
            incoming.length + payload_length > BLE_COMMAND_MAX_BYTES) {
            reset_incoming();
            pending_callback_error = CallbackError::InvalidFragment;
            return;
        }
        memcpy(incoming.json + incoming.length, bytes + 4U, payload_length);
        incoming.length += payload_length;
        incoming.next_part++;
        incoming.last_fragment_ms = now_ms;

        if (incoming.next_part == incoming.part_count) {
            incoming.json[incoming.length] = '\0';
            const bool queued = enqueue_command(incoming.json, incoming.length);
            reset_incoming();
            pending_callback_error = queued ? CallbackError::None : CallbackError::QueueFull;
        }
    }
};

void controller_ble_task(void *) {
    GuiBleSnapshot initial_snapshot = {};
    while (!gui_app_ble_snapshot(&initial_snapshot)) {
        vTaskDelay(pdMS_TO_TICKS(250U));
    }

    command_queue = xQueueCreate(BLE_COMMAND_QUEUE_LENGTH, sizeof(BleQueuedCommand));
    if (command_queue == nullptr) {
        Serial.println("BLE: command queue allocation failed.");
        vTaskDelete(nullptr);
        return;
    }

    NimBLEDevice::init(BLE_DEVICE_NAME);
    NimBLEDevice::setMTU(185U);
    NimBLEServer *server = NimBLEDevice::createServer();
    server->setCallbacks(new ControllerServerCallbacks());
    NimBLEService *service = server->createService(BLE_SERVICE_UUID);
    NimBLECharacteristic *command = service->createCharacteristic(
        BLE_COMMAND_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    command->setCallbacks(new CommandCharacteristicCallbacks());
    events_characteristic = service->createCharacteristic(
        BLE_EVENTS_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    NimBLECharacteristic *info = service->createCharacteristic(
        BLE_INFO_UUID, NIMBLE_PROPERTY::READ);
    char info_json[384];
    snprintf(
        info_json,
        sizeof(info_json),
        "{\"name\":\"cydAkwarium\",\"firmwareVersion\":\"%s\","
        "\"protocol\":2,\"apiVersions\":[1,2],"
        "\"maxCommand\":4096,\"maxParts\":32,"
        "\"capabilitiesOp\":\"capabilities\",\"auth\":\"short_lived_token\","
        "\"security\":{\"linkEncryption\":false,\"bonding\":false,"
        "\"mitmProtection\":false}}",
        FirmwareInfo::VERSION);
    info->setValue(info_json);

    NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
    advertising->setName(BLE_DEVICE_NAME);
    advertising->addServiceUUID(BLE_SERVICE_UUID);
    advertising->enableScanResponse(true);
    advertising->setPreferredParams(0x06U, 0x12U);
    advertising->start();
    Serial.println("BLE: cydAkwarium GATT protocol v2 ready.");

    uint32_t last_status_ms = 0U;
    for (;;) {
        const CallbackError callback_error = pending_callback_error;
        if (callback_error != CallbackError::None) {
            pending_callback_error = CallbackError::None;
            if (callback_error == CallbackError::QueueFull) {
                send_response(0, {false, "queue_full", "Kolejka komend BLE jest pelna."});
            } else if (callback_error == CallbackError::InvalidFragment) {
                send_response(0, {false, "invalid_fragment", "Nieprawidlowa sekwencja ramek BLE."});
            } else {
                send_response(0, {false, "invalid_command", "Nieprawidlowy rozmiar komendy BLE."});
            }
        }
        if (xQueueReceive(command_queue, &processing_command, pdMS_TO_TICKS(50U)) == pdTRUE) {
            process_command(processing_command);
            processing_command.json[0] = '\0';
        }
        const uint32_t now_ms = millis();
        if (client_connected && now_ms - last_status_ms >= BLE_STATUS_INTERVAL_MS) {
            last_status_ms = now_ms;
            send_legacy_status();
        }
    }
}

} // namespace

extern "C" void initVariant(void) {
    const BaseType_t created = xTaskCreatePinnedToCore(
        controller_ble_task, "ble_controller", 8192U, nullptr, 1U, nullptr, 0);
    if (created != pdPASS) {
        Serial.println("BLE: controller task creation failed.");
    }
}
