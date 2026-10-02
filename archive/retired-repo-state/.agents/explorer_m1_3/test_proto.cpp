#include <unity.h>
#include "Arduino.h"
#include <ArduinoJson.h>

// Extended Protocol prototype for EnterOta, ACK, and Telemetry OTA flags

namespace AquaProtocolProto {

constexpr uint32_t OFFLINE_TIMEOUT_MS = 3000UL;

enum class CommandType : uint8_t {
    Unknown = 0,
    SetRelay,
    SetTempTarget,
    SetSchedule,
    FeedTrigger,
    SyncLights,
    SyncTime,
    ClearAlarm,
    GetStatus,
    SetSafeMode,
    EnterOta
};

enum class MessageType : uint8_t {
    Unknown = 0,
    Telemetry,
    Command,
    Ack,
    TimeSync
};

struct AquariumTelemetry {
    uint32_t timestamp;
    float temperature;
    float temp_target;
    float temp_hysteresis;
    float ph;
    float ec;
    uint8_t relay_states;
    uint16_t alarms;
    bool water_level_low;
    bool leak_detected;
    uint32_t uptime_seconds;
    bool safe_mode;
    uint8_t light_mode;
    bool ota_mode;
    char ota_ip[16];
};

struct AquariumCommand {
    CommandType type;
    uint8_t channel;
    bool state;
    float temp_target;
    float temp_hysteresis;
    uint8_t schedule_dev;
    uint8_t on_hour;
    uint8_t on_minute;
    uint8_t off_hour;
    uint8_t off_minute;
    bool schedule_enabled;
    uint32_t epoch_time;
    char ota_ssid[33];
    char ota_pass[65];
};

struct AquariumAck {
    char cmd[24];
    bool ok;
    char message[48];
};

struct AquariumMessage {
    MessageType type;
    AquariumTelemetry telemetry;
    AquariumCommand command;
    AquariumAck ack;
    uint32_t sync_time_epoch;
};

String serializeTelemetry(const AquariumTelemetry &t) {
    StaticJsonDocument<512> doc;
    doc["type"] = "telemetry";
    doc["ts"] = t.timestamp;
    doc["temp"] = serialized(String(t.temperature, 2));
    doc["temp_tgt"] = serialized(String(t.temp_target, 2));
    doc["temp_hyst"] = serialized(String(t.temp_hysteresis, 2));
    doc["ph"] = serialized(String(t.ph, 2));
    doc["ec"] = serialized(String(t.ec, 1));
    doc["relays"] = t.relay_states;
    doc["alarms"] = t.alarms;
    doc["w_low"] = t.water_level_low;
    doc["leak"] = t.leak_detected;
    doc["uptime"] = t.uptime_seconds;
    doc["safe"] = t.safe_mode;
    doc["lmode"] = t.light_mode;
    doc["ota"] = t.ota_mode;
    if (t.ota_ip[0] != '\0') {
        doc["ota_ip"] = t.ota_ip;
    }

    String output;
    output.reserve(256);
    serializeJson(doc, output);
    return output;
}

String serializeCommand(const AquariumCommand &cmd) {
    StaticJsonDocument<384> doc;
    doc["type"] = "cmd";

    switch (cmd.type) {
    case CommandType::EnterOta:
        doc["cmd"] = "enter_ota";
        if (cmd.ota_ssid[0] != '\0') {
            doc["ssid"] = cmd.ota_ssid;
        }
        if (cmd.ota_pass[0] != '\0') {
            doc["pass"] = cmd.ota_pass;
        }
        break;
    default:
        return String();
    }

    String output;
    output.reserve(160);
    serializeJson(doc, output);
    return output;
}

String serializeAck(const char *cmd, bool ok, const char *msg = nullptr) {
    StaticJsonDocument<256> doc;
    doc["type"] = "ack";
    doc["cmd"] = cmd ? cmd : "";
    doc["ok"] = ok;
    if (msg && msg[0] != '\0') {
        doc["msg"] = msg;
    }

    String output;
    output.reserve(128);
    serializeJson(doc, output);
    return output;
}

bool parseMessage(const char *json_line, AquariumMessage &out_msg) {
    if (json_line == nullptr || json_line[0] == '\0') {
        return false;
    }

    StaticJsonDocument<768> doc;
    DeserializationError error = deserializeJson(doc, json_line);
    if (error) {
        return false;
    }

    const char *type_str = doc["type"] | "";
    if (strcmp(type_str, "telemetry") == 0) {
        out_msg.type = MessageType::Telemetry;
        out_msg.telemetry.timestamp = doc["ts"] | 0UL;
        out_msg.telemetry.temperature = doc["temp"] | 0.0f;
        out_msg.telemetry.temp_target = doc["temp_tgt"] | 25.0f;
        out_msg.telemetry.temp_hysteresis = doc["temp_hyst"] | 0.3f;
        out_msg.telemetry.ph = doc["ph"] | 7.0f;
        out_msg.telemetry.ec = doc["ec"] | 0.0f;
        out_msg.telemetry.relay_states = doc["relays"] | 0U;
        out_msg.telemetry.alarms = doc["alarms"] | 0U;
        out_msg.telemetry.water_level_low = doc["w_low"] | false;
        out_msg.telemetry.leak_detected = doc["leak"] | false;
        out_msg.telemetry.uptime_seconds = doc["uptime"] | 0UL;
        out_msg.telemetry.safe_mode = doc["safe"] | false;
        out_msg.telemetry.light_mode = doc["lmode"] | 1U;
        out_msg.telemetry.ota_mode = doc["ota"] | false;
        const char *ip_str = doc["ota_ip"] | "";
        strncpy(out_msg.telemetry.ota_ip, ip_str, sizeof(out_msg.telemetry.ota_ip) - 1);
        out_msg.telemetry.ota_ip[sizeof(out_msg.telemetry.ota_ip) - 1] = '\0';
        return true;
    }

    if (strcmp(type_str, "cmd") == 0) {
        out_msg.type = MessageType::Command;
        const char *cmd_name = doc["cmd"] | "";
        if (strcmp(cmd_name, "enter_ota") == 0) {
            out_msg.command.type = CommandType::EnterOta;
            const char *ssid = doc["ssid"] | "";
            strncpy(out_msg.command.ota_ssid, ssid, sizeof(out_msg.command.ota_ssid) - 1);
            out_msg.command.ota_ssid[sizeof(out_msg.command.ota_ssid) - 1] = '\0';
            const char *pass = doc["pass"] | "";
            strncpy(out_msg.command.ota_pass, pass, sizeof(out_msg.command.ota_pass) - 1);
            out_msg.command.ota_pass[sizeof(out_msg.command.ota_pass) - 1] = '\0';
            return true;
        }
    }

    if (strcmp(type_str, "ack") == 0) {
        out_msg.type = MessageType::Ack;
        const char *cmd_name = doc["cmd"] | "";
        strncpy(out_msg.ack.cmd, cmd_name, sizeof(out_msg.ack.cmd) - 1);
        out_msg.ack.cmd[sizeof(out_msg.ack.cmd) - 1] = '\0';
        out_msg.ack.ok = doc["ok"] | false;
        const char *msg_text = doc["msg"] | "";
        strncpy(out_msg.ack.message, msg_text, sizeof(out_msg.ack.message) - 1);
        out_msg.ack.message[sizeof(out_msg.ack.message) - 1] = '\0';
        return true;
    }

    out_msg.type = MessageType::Unknown;
    return false;
}

} // namespace AquaProtocolProto

extern "C" void setUp(void) {}
extern "C" void tearDown(void) {}

static void test_enter_ota_serialization_round_trip() {
    using namespace AquaProtocolProto;
    AquariumCommand cmd = {};
    cmd.type = CommandType::EnterOta;
    strncpy(cmd.ota_ssid, "HomeWiFi_2.4G", sizeof(cmd.ota_ssid) - 1);
    strncpy(cmd.ota_pass, "SecretPass123", sizeof(cmd.ota_pass) - 1);

    String serialized = serializeCommand(cmd);
    TEST_ASSERT_TRUE(strstr(serialized.c_str(), "\"cmd\":\"enter_ota\"") != nullptr);
    TEST_ASSERT_TRUE(strstr(serialized.c_str(), "\"ssid\":\"HomeWiFi_2.4G\"") != nullptr);
    TEST_ASSERT_TRUE(strstr(serialized.c_str(), "\"pass\":\"SecretPass123\"") != nullptr);

    AquariumMessage msg = {};
    bool parsed = parseMessage(serialized.c_str(), msg);
    TEST_ASSERT_TRUE(parsed);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MessageType::Command), static_cast<uint8_t>(msg.type));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CommandType::EnterOta), static_cast<uint8_t>(msg.command.type));
    TEST_ASSERT_EQUAL_STRING("HomeWiFi_2.4G", msg.command.ota_ssid);
    TEST_ASSERT_EQUAL_STRING("SecretPass123", msg.command.ota_pass);
}

static void test_enter_ota_default_fallback_round_trip() {
    using namespace AquaProtocolProto;
    AquariumCommand cmd = {};
    cmd.type = CommandType::EnterOta;
    cmd.ota_ssid[0] = '\0';
    cmd.ota_pass[0] = '\0';

    String serialized = serializeCommand(cmd);
    TEST_ASSERT_TRUE(strstr(serialized.c_str(), "\"cmd\":\"enter_ota\"") != nullptr);
    // ssid and pass should not be serialized when empty
    TEST_ASSERT_TRUE(strstr(serialized.c_str(), "\"ssid\"") == nullptr);

    AquariumMessage msg = {};
    bool parsed = parseMessage(serialized.c_str(), msg);
    TEST_ASSERT_TRUE(parsed);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CommandType::EnterOta), static_cast<uint8_t>(msg.command.type));
    TEST_ASSERT_EQUAL_STRING("", msg.command.ota_ssid);
    TEST_ASSERT_EQUAL_STRING("", msg.command.ota_pass);
}

static void test_ack_enter_ota_handling() {
    using namespace AquaProtocolProto;
    String ack_sta = serializeAck("enter_ota", true, "STA:192.168.1.150");
    AquariumMessage msg = {};
    TEST_ASSERT_TRUE(parseMessage(ack_sta.c_str(), msg));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MessageType::Ack), static_cast<uint8_t>(msg.type));
    TEST_ASSERT_EQUAL_STRING("enter_ota", msg.ack.cmd);
    TEST_ASSERT_TRUE(msg.ack.ok);
    TEST_ASSERT_EQUAL_STRING("STA:192.168.1.150", msg.ack.message);

    String ack_err = serializeAck("enter_ota", false, "Relay cutoff failed");
    TEST_ASSERT_TRUE(parseMessage(ack_err.c_str(), msg));
    TEST_ASSERT_FALSE(msg.ack.ok);
    TEST_ASSERT_EQUAL_STRING("Relay cutoff failed", msg.ack.message);
}

static void test_telemetry_ota_flags_and_backward_compatibility() {
    using namespace AquaProtocolProto;
    // 1. Telemetry in OTA mode
    AquariumTelemetry t = {};
    t.timestamp = 1773852100;
    t.temperature = 24.8f;
    t.temp_target = 25.0f;
    t.temp_hysteresis = 0.3f;
    t.ph = 6.85f;
    t.relay_states = 0; // Cutoff
    t.safe_mode = true;
    t.ota_mode = true;
    strncpy(t.ota_ip, "192.168.4.1", sizeof(t.ota_ip) - 1);

    String serialized = serializeTelemetry(t);
    TEST_ASSERT_TRUE(strstr(serialized.c_str(), "\"ota\":true") != nullptr);
    TEST_ASSERT_TRUE(strstr(serialized.c_str(), "\"ota_ip\":\"192.168.4.1\"") != nullptr);

    AquariumMessage msg = {};
    TEST_ASSERT_TRUE(parseMessage(serialized.c_str(), msg));
    TEST_ASSERT_TRUE(msg.telemetry.ota_mode);
    TEST_ASSERT_EQUAL_STRING("192.168.4.1", msg.telemetry.ota_ip);
    TEST_ASSERT_EQUAL_UINT8(0, msg.telemetry.relay_states);
    TEST_ASSERT_TRUE(msg.telemetry.safe_mode);

    // 2. Legacy Telemetry frame without ota fields
    const char *legacy = "{\"type\":\"telemetry\",\"ts\":1700000000,\"temp\":25.50,\"temp_tgt\":25.00,\"temp_hyst\":0.30,\"ph\":7.10,\"ec\":420.0,\"relays\":3,\"alarms\":0,\"w_low\":false,\"leak\":false,\"uptime\":3600,\"safe\":false,\"lmode\":1}";
    memset(&msg, 0, sizeof(msg));
    TEST_ASSERT_TRUE(parseMessage(legacy, msg));
    TEST_ASSERT_FALSE(msg.telemetry.ota_mode);
    TEST_ASSERT_EQUAL_STRING("", msg.telemetry.ota_ip);
    TEST_ASSERT_EQUAL_UINT8(3, msg.telemetry.relay_states);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_enter_ota_serialization_round_trip);
    RUN_TEST(test_enter_ota_default_fallback_round_trip);
    RUN_TEST(test_ack_enter_ota_handling);
    RUN_TEST(test_telemetry_ota_flags_and_backward_compatibility);
    return UNITY_END();
}
