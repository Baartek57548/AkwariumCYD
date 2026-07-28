#include <Arduino.h>
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "ble_pairing_policy.h"
#include "config.h"
#include "ble_controller.h"
#include "gui_app.h"

namespace {

static_assert(
    CONFIG_BT_NIMBLE_MAX_CONNECTIONS == 1,
    "The controller security model requires exactly one BLE connection.");

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
constexpr uint16_t BLE_INVALID_CONN_HANDLE = 0xFFFFU;
constexpr uint32_t BLE_BOND_RESET_GRACE_MS = 250U;

enum class CallbackError : uint8_t {
    None = 0U,
    InvalidCommand = 1U,
    QueueFull = 2U,
    InvalidFragment = 3U
};

struct BleSession {
    uint16_t conn_handle;
    uint32_t generation;
};

struct BleQueuedCommand {
    BleSession session;
    char json[BLE_COMMAND_MAX_BYTES + 1U];
};

struct IncomingAssembly {
    bool active;
    BleSession session;
    uint16_t message_id;
    uint8_t part_count;
    uint8_t next_part;
    size_t length;
    uint32_t last_fragment_ms;
    char json[BLE_COMMAND_MAX_BYTES + 1U];
};

struct CallbackFailure {
    CallbackError error;
    BleSession session;
};

QueueHandle_t command_queue = nullptr;
NimBLEServer *ble_server = nullptr;
NimBLECharacteristic *events_characteristic = nullptr;
volatile bool ble_initialized = false;
volatile bool forget_bonds_requested = false;
portMUX_TYPE ble_session_mux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE ble_pairing_mux = portMUX_INITIALIZER_UNLOCKED;
bool client_connected = false;
uint16_t active_conn_handle = BLE_INVALID_CONN_HANDLE;
uint32_t active_conn_generation = 0U;
uint32_t next_conn_generation = 1U;
CallbackFailure pending_callback_failure = {
    CallbackError::None,
    {BLE_INVALID_CONN_HANDLE, 0U}};
aquarium::BlePairingPolicy pairing_policy;
volatile bool bond_reset_pending = false;
uint32_t bond_reset_at_ms = 0U;
uint16_t outgoing_message_id = 1U;
IncomingAssembly incoming = {};
BleQueuedCommand processing_command = {};
BleQueuedCommand callback_command = {};
BleSession processing_session = {BLE_INVALID_CONN_HANDLE, 0U};
char outgoing_json[BLE_MESSAGE_BUFFER_BYTES] = {};

bool session_is_valid(const BleSession &session) {
    return session.conn_handle != BLE_INVALID_CONN_HANDLE &&
           session.generation != 0U;
}

bool sessions_match(const BleSession &left, const BleSession &right) {
    return left.conn_handle == right.conn_handle &&
           left.generation == right.generation;
}

BleSession active_session_snapshot(bool require_authenticated) {
    BleSession session = {BLE_INVALID_CONN_HANDLE, 0U};
    portENTER_CRITICAL(&ble_session_mux);
    if (active_conn_handle != BLE_INVALID_CONN_HANDLE &&
        (!require_authenticated || client_connected)) {
        session.conn_handle = active_conn_handle;
        session.generation = active_conn_generation;
    }
    portEXIT_CRITICAL(&ble_session_mux);
    return session;
}

bool active_handle_matches(uint16_t conn_handle) {
    bool matches = false;
    portENTER_CRITICAL(&ble_session_mux);
    matches = active_conn_handle == conn_handle &&
              active_conn_generation != 0U;
    portEXIT_CRITICAL(&ble_session_mux);
    return matches;
}

bool active_secured_session_matches(const BleSession &session) {
    if (!session_is_valid(session)) {
        return false;
    }
    bool matches = false;
    portENTER_CRITICAL(&ble_session_mux);
    matches = client_connected &&
              active_conn_handle == session.conn_handle &&
              active_conn_generation == session.generation;
    portEXIT_CRITICAL(&ble_session_mux);
    return matches;
}

bool claim_connection(uint16_t conn_handle, BleSession *claimed_session) {
    if (claimed_session == nullptr ||
        conn_handle == BLE_INVALID_CONN_HANDLE) {
        return false;
    }
    bool claimed = false;
    portENTER_CRITICAL(&ble_session_mux);
    if (active_conn_handle == BLE_INVALID_CONN_HANDLE) {
        uint32_t generation = next_conn_generation++;
        if (generation == 0U) {
            generation = next_conn_generation++;
        }
        if (next_conn_generation == 0U) {
            next_conn_generation = 1U;
        }
        active_conn_handle = conn_handle;
        active_conn_generation = generation;
        client_connected = false;
        claimed_session->conn_handle = conn_handle;
        claimed_session->generation = generation;
        claimed = true;
    }
    portEXIT_CRITICAL(&ble_session_mux);
    return claimed;
}

bool set_active_connection_secured(uint16_t conn_handle, bool secured) {
    bool active = false;
    portENTER_CRITICAL(&ble_session_mux);
    if (active_conn_handle == conn_handle &&
        active_conn_generation != 0U) {
        client_connected = secured;
        active = true;
    }
    portEXIT_CRITICAL(&ble_session_mux);
    return active;
}

bool release_connection(uint16_t conn_handle) {
    bool released = false;
    portENTER_CRITICAL(&ble_session_mux);
    if (active_conn_handle == conn_handle &&
        active_conn_generation != 0U) {
        client_connected = false;
        active_conn_handle = BLE_INVALID_CONN_HANDLE;
        active_conn_generation = 0U;
        released = true;
    }
    portEXIT_CRITICAL(&ble_session_mux);
    return released;
}

uint32_t begin_pairing_attempt(const BleSession &session,
                               uint32_t now_ms) {
    if (!session_is_valid(session)) {
        return 0U;
    }
    uint32_t passkey = 0U;
    portENTER_CRITICAL(&ble_pairing_mux);
    passkey = pairing_policy.begin(
        session.conn_handle,
        session.generation,
        now_ms,
        esp_random());
    portEXIT_CRITICAL(&ble_pairing_mux);
    return passkey;
}

uint32_t pairing_passkey_snapshot(uint32_t now_ms) {
    uint32_t passkey = 0U;
    portENTER_CRITICAL(&ble_pairing_mux);
    passkey = pairing_policy.displayed_passkey(now_ms);
    portEXIT_CRITICAL(&ble_pairing_mux);
    return passkey;
}

void complete_pairing_attempt(const BleSession &session) {
    if (!session_is_valid(session)) {
        return;
    }
    portENTER_CRITICAL(&ble_pairing_mux);
    pairing_policy.complete(session.conn_handle, session.generation);
    portEXIT_CRITICAL(&ble_pairing_mux);
}

void clear_pairing_attempt() {
    portENTER_CRITICAL(&ble_pairing_mux);
    pairing_policy.clear();
    portEXIT_CRITICAL(&ble_pairing_mux);
}

bool take_expired_pairing_attempt(
    uint32_t now_ms,
    aquarium::BlePairingAttemptState *expired_attempt) {
    bool expired = false;
    portENTER_CRITICAL(&ble_pairing_mux);
    expired = pairing_policy.expire(now_ms, expired_attempt);
    portEXIT_CRITICAL(&ble_pairing_mux);
    return expired;
}

BleSession mark_active_connection_unsecured() {
    BleSession session = {BLE_INVALID_CONN_HANDLE, 0U};
    portENTER_CRITICAL(&ble_session_mux);
    if (active_conn_handle != BLE_INVALID_CONN_HANDLE &&
        active_conn_generation != 0U) {
        client_connected = false;
        session.conn_handle = active_conn_handle;
        session.generation = active_conn_generation;
    }
    portEXIT_CRITICAL(&ble_session_mux);
    return session;
}

void record_callback_failure(CallbackError error,
                             const BleSession &session) {
    if (error == CallbackError::None || !session_is_valid(session)) {
        return;
    }
    portENTER_CRITICAL(&ble_session_mux);
    pending_callback_failure.error = error;
    pending_callback_failure.session = session;
    portEXIT_CRITICAL(&ble_session_mux);
}

CallbackFailure take_callback_failure() {
    CallbackFailure failure = {};
    portENTER_CRITICAL(&ble_session_mux);
    failure = pending_callback_failure;
    pending_callback_failure.error = CallbackError::None;
    pending_callback_failure.session = {
        BLE_INVALID_CONN_HANDLE,
        0U};
    portEXIT_CRITICAL(&ble_session_mux);
    return failure;
}

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
    incoming.session = {BLE_INVALID_CONN_HANDLE, 0U};
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

bool enqueue_command(const char *json,
                     size_t length,
                     const BleSession &session) {
    if (command_queue == nullptr || json == nullptr || length == 0U ||
        length > BLE_COMMAND_MAX_BYTES ||
        memchr(json, '\0', length) != nullptr ||
        !session_is_valid(session)) {
        return false;
    }
    callback_command.session = session;
    memcpy(callback_command.json, json, length);
    callback_command.json[length] = '\0';
    return xQueueSend(command_queue, &callback_command, 0) == pdTRUE;
}

void send_json(const char *json) {
    const BleSession target_session =
        session_is_valid(processing_session)
            ? processing_session
            : active_session_snapshot(true);
    if (!active_secured_session_matches(target_session) ||
        events_characteristic == nullptr ||
        json == nullptr) {
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
        if (!active_secured_session_matches(target_session)) {
            return;
        }
        if (!events_characteristic->notify(
                frame,
                payload_length + 4U,
                target_session.conn_handle)) {
            Serial.printf(
                "BLE: notify failed for handle=%u generation=%lu.\n",
                static_cast<unsigned>(target_session.conn_handle),
                static_cast<unsigned long>(target_session.generation));
            return;
        }
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
    void onConnect(NimBLEServer *server, NimBLEConnInfo &conn_info) override {
        const uint16_t conn_handle = conn_info.getConnHandle();
        BleSession claimed_session = {BLE_INVALID_CONN_HANDLE, 0U};
        if (bond_reset_pending ||
            server->getConnectedCount() > 1U ||
            !claim_connection(conn_handle, &claimed_session)) {
            Serial.printf(
                "BLE: rejecting additional connection handle=%u.\n",
                static_cast<unsigned>(conn_handle));
            server->disconnect(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            return;
        }

        NimBLEDevice::stopAdvertising();
        reset_incoming();
        if (!conn_info.isBonded()) {
            const uint32_t passkey =
                begin_pairing_attempt(claimed_session, millis());
            if (passkey == 0U) {
                gui_app_update_ble_pairing(0U, 3U);
                server->disconnect(
                    conn_handle,
                    BLE_ERR_REM_USER_CONN_TERM);
                return;
            }
        } else {
            clear_pairing_attempt();
        }
        if (!NimBLEDevice::startSecurity(conn_handle)) {
            complete_pairing_attempt(claimed_session);
            gui_app_update_ble_pairing(0U, 3U);
            server->disconnect(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
    }

    void onDisconnect(NimBLEServer *server,
                      NimBLEConnInfo &conn_info,
                      int) override {
        const BleSession disconnected_session =
            active_session_snapshot(false);
        if (!release_connection(conn_info.getConnHandle())) {
            return;
        }
        complete_pairing_attempt(disconnected_session);
        reset_incoming();
        gui_app_update_ble_pairing(0U, 0U);
        if (!bond_reset_pending &&
            server->getConnectedCount() == 0U) {
            NimBLEDevice::startAdvertising();
        }
    }

    uint32_t onPassKeyDisplay() override {
        const uint32_t passkey = pairing_passkey_snapshot(millis());
        if (passkey == 0U) {
            gui_app_update_ble_pairing(0U, 3U);
            return 0U;
        }
        gui_app_update_ble_pairing(passkey, 1U);
        return passkey;
    }

    void onAuthenticationComplete(NimBLEConnInfo &conn_info) override {
        const uint16_t conn_handle = conn_info.getConnHandle();
        const BleSession authenticated_session =
            active_session_snapshot(false);
        const bool secured =
            !bond_reset_pending &&
            conn_info.isEncrypted() &&
            conn_info.isAuthenticated() &&
            conn_info.isBonded() &&
            conn_info.getSecKeySize() >= 16U;
        if (!set_active_connection_secured(conn_handle, secured)) {
            if (ble_server != nullptr) {
                ble_server->disconnect(
                    conn_handle,
                    BLE_ERR_REM_USER_CONN_TERM);
            }
            return;
        }

        complete_pairing_attempt(authenticated_session);
        gui_app_update_ble_pairing(0U, secured ? 2U : 3U);
        if (!secured && ble_server != nullptr) {
            ble_server->disconnect(
                conn_handle,
                BLE_ERR_REM_USER_CONN_TERM);
        }
    }
};

class CommandCharacteristicCallbacks final : public NimBLECharacteristicCallbacks {
public:
    void onWrite(NimBLECharacteristic *characteristic,
                 NimBLEConnInfo &conn_info) override {
        if (characteristic == nullptr || command_queue == nullptr) {
            return;
        }
        const uint16_t conn_handle = conn_info.getConnHandle();
        const BleSession session = active_session_snapshot(true);
        if (bond_reset_pending ||
            !session_is_valid(session) ||
            session.conn_handle != conn_handle ||
            !conn_info.isEncrypted() ||
            !conn_info.isAuthenticated() ||
            !conn_info.isBonded() ||
            conn_info.getSecKeySize() < 16U) {
            if (active_handle_matches(conn_handle)) {
                reset_incoming();
            }
            if (ble_server != nullptr) {
                ble_server->disconnect(
                    conn_handle,
                    BLE_ERR_REM_USER_CONN_TERM);
            }
            return;
        }
        const std::string value = characteristic->getValue();
        if (value.empty()) {
            record_callback_failure(
                CallbackError::InvalidCommand,
                session);
            return;
        }

        const bool unframed_json =
            value.size() >= 2U && value.front() == '{' && value.back() == '}';
        if (unframed_json) {
            reset_incoming();
            const bool queued =
                enqueue_command(value.data(), value.size(), session);
            if (!queued) {
                record_callback_failure(
                    value.size() > BLE_COMMAND_MAX_BYTES
                        ? CallbackError::InvalidCommand
                        : CallbackError::QueueFull,
                    session);
            }
            return;
        }

        if (value.size() <= 4U) {
            reset_incoming();
            record_callback_failure(
                CallbackError::InvalidFragment,
                session);
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
            record_callback_failure(
                CallbackError::InvalidFragment,
                session);
            return;
        }
        if (incoming.active && now_ms - incoming.last_fragment_ms > BLE_FRAGMENT_TIMEOUT_MS) {
            reset_incoming();
        }
        if (part_index == 0U) {
            reset_incoming();
            incoming.active = true;
            incoming.session = session;
            incoming.message_id = message_id;
            incoming.part_count = part_count;
        }
        if (!incoming.active ||
            !sessions_match(incoming.session, session) ||
            incoming.message_id != message_id ||
            incoming.part_count != part_count || incoming.next_part != part_index ||
            incoming.length + payload_length > BLE_COMMAND_MAX_BYTES) {
            reset_incoming();
            record_callback_failure(
                CallbackError::InvalidFragment,
                session);
            return;
        }
        memcpy(incoming.json + incoming.length, bytes + 4U, payload_length);
        incoming.length += payload_length;
        incoming.next_part++;
        incoming.last_fragment_ms = now_ms;

        if (incoming.next_part == incoming.part_count) {
            incoming.json[incoming.length] = '\0';
            const bool queued =
                enqueue_command(incoming.json, incoming.length, session);
            reset_incoming();
            if (!queued) {
                record_callback_failure(
                    CallbackError::QueueFull,
                    session);
            }
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
    NimBLEDevice::setSecurityAuth(true, true, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
    NimBLEDevice::setSecurityInitKey(
        BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
    NimBLEDevice::setSecurityRespKey(
        BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
    // Keeping the library sentinel makes it request the passkey through our
    // callback, where the random code is also safely forwarded to the UI task.
    NimBLEDevice::setSecurityPasskey(123456U);
    ble_server = NimBLEDevice::createServer();
    ble_server->advertiseOnDisconnect(false);
    ble_server->setCallbacks(new ControllerServerCallbacks());
    NimBLEService *service = ble_server->createService(BLE_SERVICE_UUID);
    NimBLECharacteristic *command = service->createCharacteristic(
        BLE_COMMAND_UUID,
        NIMBLE_PROPERTY::WRITE |
            NIMBLE_PROPERTY::WRITE_NR |
            NIMBLE_PROPERTY::WRITE_ENC |
            NIMBLE_PROPERTY::WRITE_AUTHEN);
    command->setCallbacks(new CommandCharacteristicCallbacks());
    events_characteristic = service->createCharacteristic(
        BLE_EVENTS_UUID,
        NIMBLE_PROPERTY::READ |
            NIMBLE_PROPERTY::NOTIFY |
            NIMBLE_PROPERTY::READ_ENC |
            NIMBLE_PROPERTY::READ_AUTHEN);
    NimBLECharacteristic *info = service->createCharacteristic(
        BLE_INFO_UUID,
        NIMBLE_PROPERTY::READ |
            NIMBLE_PROPERTY::READ_ENC |
            NIMBLE_PROPERTY::READ_AUTHEN);
    char info_json[384];
    snprintf(
        info_json,
        sizeof(info_json),
        "{\"name\":\"cydAkwarium\",\"firmwareVersion\":\"%s\","
        "\"protocol\":2,\"apiVersions\":[1,2],"
        "\"maxCommand\":4096,\"maxParts\":32,"
        "\"capabilitiesOp\":\"capabilities\",\"auth\":\"short_lived_token\","
        "\"security\":{\"linkEncryption\":true,\"bonding\":true,"
        "\"mitmProtection\":true,\"secureConnections\":true,"
        "\"minimumKeySize\":16}}",
        FirmwareInfo::VERSION);
    info->setValue(info_json);

    NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
    advertising->setName(BLE_DEVICE_NAME);
    advertising->addServiceUUID(BLE_SERVICE_UUID);
    advertising->enableScanResponse(true);
    advertising->setPreferredParams(0x06U, 0x12U);
    advertising->start();
    ble_initialized = true;
    Serial.printf(
        "BLE: secure GATT v2 ready; persisted bonds=%d.\n",
        NimBLEDevice::getNumBonds());

    uint32_t last_status_ms = 0U;
    for (;;) {
        const uint32_t now_ms = millis();
        aquarium::BlePairingAttemptState expired_pairing = {};
        if (take_expired_pairing_attempt(
                now_ms,
                &expired_pairing)) {
            gui_app_update_ble_pairing(0U, 3U);
            const BleSession active_session =
                mark_active_connection_unsecured();
            if (ble_server != nullptr &&
                session_is_valid(active_session) &&
                active_session.conn_handle ==
                    expired_pairing.connection_handle) {
                ble_server->disconnect(
                    active_session.conn_handle,
                    BLE_ERR_REM_USER_CONN_TERM);
            }
        }
        if (forget_bonds_requested) {
            forget_bonds_requested = false;
            bond_reset_pending = true;
            bond_reset_at_ms = now_ms + BLE_BOND_RESET_GRACE_MS;
            clear_pairing_attempt();
            NimBLEDevice::stopAdvertising();
            const BleSession active_session =
                mark_active_connection_unsecured();
            if (ble_server != nullptr &&
                session_is_valid(active_session) &&
                !ble_server->disconnect(
                    active_session.conn_handle,
                    BLE_ERR_REM_USER_CONN_TERM)) {
                Serial.printf(
                    "BLE: disconnect retry scheduled for handle=%u.\n",
                    static_cast<unsigned>(active_session.conn_handle));
            }
        }
        if (bond_reset_pending &&
            static_cast<int32_t>(now_ms - bond_reset_at_ms) >= 0) {
            if (ble_server != nullptr &&
                ble_server->getConnectedCount() > 0U) {
                const BleSession active_session =
                    mark_active_connection_unsecured();
                if (session_is_valid(active_session)) {
                    ble_server->disconnect(
                        active_session.conn_handle,
                        BLE_ERR_REM_USER_CONN_TERM);
                } else {
                    const NimBLEConnInfo peer = ble_server->getPeerInfo(0U);
                    ble_server->disconnect(
                        peer.getConnHandle(),
                        BLE_ERR_REM_USER_CONN_TERM);
                }
                bond_reset_at_ms =
                    now_ms + BLE_BOND_RESET_GRACE_MS;
                continue;
            }
            const bool deleted = NimBLEDevice::deleteAllBonds();
            gui_app_update_ble_pairing(0U, deleted ? 0U : 3U);
            Serial.printf(
                "BLE: bond reset %s; remaining=%d.\n",
                deleted ? "completed" : "failed",
                NimBLEDevice::getNumBonds());
            bond_reset_pending = false;
            NimBLEDevice::startAdvertising();
        }
        const CallbackFailure callback_failure =
            take_callback_failure();
        if (callback_failure.error != CallbackError::None &&
            active_secured_session_matches(callback_failure.session)) {
            processing_session = callback_failure.session;
            if (callback_failure.error == CallbackError::QueueFull) {
                send_response(0, {false, "queue_full", "Kolejka komend BLE jest pelna."});
            } else if (callback_failure.error ==
                       CallbackError::InvalidFragment) {
                send_response(0, {false, "invalid_fragment", "Nieprawidlowa sekwencja ramek BLE."});
            } else {
                send_response(0, {false, "invalid_command", "Nieprawidlowy rozmiar komendy BLE."});
            }
            processing_session = {BLE_INVALID_CONN_HANDLE, 0U};
        }
        if (xQueueReceive(command_queue, &processing_command, pdMS_TO_TICKS(50U)) == pdTRUE) {
            if (active_secured_session_matches(
                    processing_command.session)) {
                processing_session = processing_command.session;
                process_command(processing_command);
                processing_session = {
                    BLE_INVALID_CONN_HANDLE,
                    0U};
            }
            processing_command.session = {
                BLE_INVALID_CONN_HANDLE,
                0U};
            processing_command.json[0] = '\0';
        }
        if (session_is_valid(active_session_snapshot(true)) &&
            now_ms - last_status_ms >= BLE_STATUS_INTERVAL_MS) {
            last_status_ms = now_ms;
            send_legacy_status();
        }
    }
}

} // namespace

bool ble_controller_request_forget_bonds() {
    if (!ble_initialized) {
        return false;
    }
    forget_bonds_requested = true;
    return true;
}

int ble_controller_bond_count() {
    return ble_initialized ? NimBLEDevice::getNumBonds() : 0;
}

extern "C" void initVariant(void) {
    const BaseType_t created = xTaskCreatePinnedToCore(
        controller_ble_task, "ble_controller", 8192U, nullptr, 1U, nullptr, 0);
    if (created != pdPASS) {
        Serial.println("BLE: controller task creation failed.");
    }
}
