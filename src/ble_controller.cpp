#include <Arduino.h>
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "gui_app.h"

namespace {

constexpr char BLE_DEVICE_NAME[] = "cydAkwarium";
constexpr char BLE_SERVICE_UUID[] = "7c4a0001-6e8d-4f84-9f3f-2c3a0a0c0001";
constexpr char BLE_COMMAND_UUID[] = "7c4a0002-6e8d-4f84-9f3f-2c3a0a0c0001";
constexpr char BLE_EVENTS_UUID[] = "7c4a0003-6e8d-4f84-9f3f-2c3a0a0c0001";
constexpr char BLE_INFO_UUID[] = "7c4a0004-6e8d-4f84-9f3f-2c3a0a0c0001";
constexpr size_t BLE_COMMAND_MAX_BYTES = 160U;
constexpr size_t BLE_FRAME_PAYLOAD_BYTES = 156U;
constexpr uint32_t BLE_STATUS_INTERVAL_MS = 2000UL;
constexpr uint32_t BLE_NOTIFY_GAP_MS = 12UL;
constexpr UBaseType_t BLE_COMMAND_QUEUE_LENGTH = 8U;

struct BleQueuedCommand {
    char json[BLE_COMMAND_MAX_BYTES + 1U];
};

QueueHandle_t command_queue = nullptr;
NimBLECharacteristic *events_characteristic = nullptr;
volatile bool client_connected = false;
volatile uint8_t pending_callback_error = 0U;
uint16_t outgoing_message_id = 1U;

bool extract_json_int(const char *json, const char *key, int *out) {
    if (json == nullptr || key == nullptr || out == nullptr) {
        return false;
    }
    char pattern[32];
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
    if (end == value || parsed < 0L || parsed > 65535L) {
        return false;
    }
    *out = static_cast<int>(parsed);
    return true;
}

bool extract_json_string(const char *json, const char *key, char *out, size_t out_size) {
    if (json == nullptr || key == nullptr || out == nullptr || out_size < 2U) {
        return false;
    }
    char pattern[32];
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
    char pattern[32];
    const int pattern_length = snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    if (pattern_length <= 0 || static_cast<size_t>(pattern_length) >= sizeof(pattern)) {
        return false;
    }
    const char *value = strstr(json, pattern);
    if (value == nullptr) {
        return false;
    }
    value += pattern_length;
    if (strncmp(value, "true", 4U) == 0) {
        *out = true;
        return true;
    }
    if (strncmp(value, "false", 5U) == 0) {
        *out = false;
        return true;
    }
    return false;
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
    if (part_count_size == 0U || part_count_size > 255U) {
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
    char response[256];
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

bool send_status() {
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

void process_command(const BleQueuedCommand &queued) {
    int id = 0;
    if (!extract_json_int(queued.json, "id", &id)) {
        send_response(0, {false, "invalid_id", "Brak poprawnego identyfikatora komendy."});
        return;
    }
    char operation[16];
    if (!extract_json_string(queued.json, "op", operation, sizeof(operation))) {
        send_response(id, {false, "invalid_operation", "Brak poprawnej operacji BLE."});
        return;
    }
    if (strcmp(operation, "status") == 0) {
        if (!send_status()) {
            send_response(id, {false, "not_ready", "Telemetria nie jest jeszcze gotowa."});
            return;
        }
        send_response(id, {true, "ok", "Telemetria BLE wyslana."});
        return;
    }

    char pin[12];
    if (!extract_json_string(queued.json, "pin", pin, sizeof(pin))) {
        send_response(id, {false, "pin_invalid", "Brak poprawnego PIN-u."});
        return;
    }
    if (strcmp(operation, "feed") == 0) {
        send_response(id, gui_app_ble_feed(pin));
        send_status();
        return;
    }
    if (strcmp(operation, "set") == 0) {
        char target[20];
        bool state = false;
        if (!extract_json_string(queued.json, "target", target, sizeof(target)) ||
            !extract_json_bool(queued.json, "state", &state)) {
            send_response(id, {false, "invalid_arguments", "Nieprawidlowe parametry modulu."});
            return;
        }
        send_response(id, gui_app_ble_set_output(target, state, pin));
        send_status();
        return;
    }
    send_response(id, {false, "unknown_operation", "Nieznana operacja BLE."});
}

class ControllerServerCallbacks final : public NimBLEServerCallbacks {
public:
    void onConnect(NimBLEServer *, NimBLEConnInfo &) override {
        client_connected = true;
    }

    void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int) override {
        client_connected = false;
        NimBLEDevice::startAdvertising();
    }
};

class CommandCharacteristicCallbacks final : public NimBLECharacteristicCallbacks {
public:
    void onWrite(NimBLECharacteristic *characteristic,
                 NimBLEConnInfo &) override {
        if (characteristic == nullptr || command_queue == nullptr) {
            return;
        }
        const std::string value = characteristic->getValue();
        if (value.empty() || value.size() > BLE_COMMAND_MAX_BYTES ||
            memchr(value.data(), '\0', value.size()) != nullptr) {
            pending_callback_error = 1U;
            return;
        }
        BleQueuedCommand queued = {};
        memcpy(queued.json, value.data(), value.size());
        queued.json[value.size()] = '\0';
        if (xQueueSend(command_queue, &queued, 0) != pdTRUE) {
            pending_callback_error = 2U;
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
        BLE_COMMAND_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    command->setCallbacks(new CommandCharacteristicCallbacks());
    events_characteristic = service->createCharacteristic(
        BLE_EVENTS_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    NimBLECharacteristic *info = service->createCharacteristic(
        BLE_INFO_UUID,
        NIMBLE_PROPERTY::READ);
    info->setValue("{\"name\":\"cydAkwarium\",\"protocol\":1}");

    NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
    advertising->setName(BLE_DEVICE_NAME);
    advertising->addServiceUUID(BLE_SERVICE_UUID);
    advertising->enableScanResponse(true);
    advertising->setPreferredParams(0x06U, 0x12U);
    advertising->start();
    Serial.println("BLE: cydAkwarium GATT service ready.");

    uint32_t last_status_ms = 0U;
    for (;;) {
        const uint8_t callback_error = pending_callback_error;
        if (callback_error != 0U) {
            pending_callback_error = 0U;
            if (callback_error == 1U) {
                send_response(0, {false, "invalid_command", "Nieprawidlowy rozmiar komendy BLE."});
            } else {
                send_response(0, {false, "queue_full", "Kolejka komend BLE jest pelna."});
            }
        }
        BleQueuedCommand queued = {};
        if (xQueueReceive(command_queue, &queued, pdMS_TO_TICKS(50U)) == pdTRUE) {
            process_command(queued);
        }
        const uint32_t now_ms = millis();
        if (client_connected && now_ms - last_status_ms >= BLE_STATUS_INTERVAL_MS) {
            last_status_ms = now_ms;
            send_status();
        }
    }
}

} // namespace

extern "C" void initVariant(void) {
    BaseType_t created = xTaskCreate(
        controller_ble_task,
        "ble_controller",
        6144U,
        nullptr,
        1U,
        nullptr);
    if (created != pdPASS) {
        Serial.println("BLE: controller task creation failed.");
    }
}
