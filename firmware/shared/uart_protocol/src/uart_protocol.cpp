#include "uart_protocol.h"
#include <ArduinoJson.h>

namespace AquaProtocol {

String serializeTelemetry(const AquariumTelemetry &t)
{
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
    doc["w_max"] = t.water_level_max_reached;
    doc["w_max_valid"] = t.water_level_max_valid;
    doc["uptime"] = t.uptime_seconds;
    doc["safe"] = t.safe_mode;
    doc["lmode"] = t.light_mode;
    doc["ota_mode"] = t.ota_mode;
    if (t.ota_ip[0] != '\0') {
        doc["ota_ip"] = t.ota_ip;
    }

    String output;
    output.reserve(320);
    serializeJson(doc, output);
    return output;
}

String serializeCommand(const AquariumCommand &cmd)
{
    StaticJsonDocument<384> doc;
    doc["type"] = "cmd";

    switch (cmd.type) {
    case CommandType::SetRelay:
        doc["cmd"] = "set_relay";
        doc["ch"] = cmd.channel;
        doc["val"] = cmd.state ? 1 : 0;
        break;
    case CommandType::SetTempTarget:
        doc["cmd"] = "set_temp";
        doc["target"] = serialized(String(cmd.temp_target, 2));
        doc["hyst"] = serialized(String(cmd.temp_hysteresis, 2));
        break;
    case CommandType::SetSchedule:
        doc["cmd"] = "set_sched";
        doc["dev"] = cmd.schedule_dev;
        doc["on_h"] = cmd.on_hour;
        doc["on_m"] = cmd.on_minute;
        doc["off_h"] = cmd.off_hour;
        doc["off_m"] = cmd.off_minute;
        doc["en"] = cmd.schedule_enabled ? 1 : 0;
        break;
    case CommandType::FeedTrigger:
        doc["cmd"] = "feed";
        break;
    case CommandType::SyncLights:
        doc["cmd"] = "sync_lights";
        break;
    case CommandType::SyncTime:
        doc["cmd"] = "sync_time";
        doc["epoch"] = cmd.epoch_time;
        break;
    case CommandType::ClearAlarm:
        doc["cmd"] = "clear_alarm";
        break;
    case CommandType::GetStatus:
        doc["cmd"] = "get_status";
        break;
    case CommandType::SetSafeMode:
        doc["cmd"] = "safe_mode";
        doc["val"] = cmd.state ? 1 : 0;
        break;
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
    output.reserve(256);
    serializeJson(doc, output);
    return output;
}

String serializeAck(const char *cmd, bool ok, const char *msg)
{
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

String serializeTimeSync(uint32_t epoch)
{
    StaticJsonDocument<128> doc;
    doc["type"] = "time_sync";
    doc["epoch"] = epoch;

    String output;
    output.reserve(64);
    serializeJson(doc, output);
    return output;
}

bool parseMessage(const char *json_line, AquariumMessage &out_msg)
{
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
        out_msg.telemetry.water_level_max_reached = doc["w_max"] | false;
        // Brak pola w ramce starszego WeMosa oznacza nieznany stan MAX, a nie
        // bezpieczny poziom. HMI może wtedy pokazać brak wiarygodnego odczytu.
        out_msg.telemetry.water_level_max_valid = doc["w_max_valid"] | false;
        out_msg.telemetry.water_level_max_present =
            doc.containsKey("w_max") && doc.containsKey("w_max_valid");
        out_msg.telemetry.uptime_seconds = doc["uptime"] | 0UL;
        out_msg.telemetry.safe_mode = doc["safe"] | false;
        out_msg.telemetry.light_mode = doc["lmode"] | 1U;
        out_msg.telemetry.ota_mode = doc.containsKey("ota_mode") ? (doc["ota_mode"] | false) : (doc["ota"] | false);
        const char *ip_str = doc["ota_ip"] | "";
        strncpy(out_msg.telemetry.ota_ip, ip_str, sizeof(out_msg.telemetry.ota_ip) - 1);
        out_msg.telemetry.ota_ip[sizeof(out_msg.telemetry.ota_ip) - 1] = '\0';
        return true;
    }

    if (strcmp(type_str, "cmd") == 0) {
        out_msg.type = MessageType::Command;
        memset(&out_msg.command, 0, sizeof(out_msg.command));
        const char *cmd_name = doc["cmd"] | "";
        if (strcmp(cmd_name, "set_relay") == 0) {
            out_msg.command.type = CommandType::SetRelay;
            out_msg.command.channel = doc["ch"] | 0U;
            out_msg.command.state = (doc["val"] | 0) != 0;
        } else if (strcmp(cmd_name, "set_temp") == 0) {
            out_msg.command.type = CommandType::SetTempTarget;
            out_msg.command.temp_target = doc["target"] | 25.0f;
            out_msg.command.temp_hysteresis = doc["hyst"] | 0.3f;
        } else if (strcmp(cmd_name, "set_sched") == 0) {
            out_msg.command.type = CommandType::SetSchedule;
            out_msg.command.schedule_dev = doc["dev"] | 0U;
            out_msg.command.on_hour = doc["on_h"] | 0U;
            out_msg.command.on_minute = doc["on_m"] | 0U;
            out_msg.command.off_hour = doc["off_h"] | 0U;
            out_msg.command.off_minute = doc["off_m"] | 0U;
            out_msg.command.schedule_enabled = (doc["en"] | 1) != 0;
        } else if (strcmp(cmd_name, "feed") == 0) {
            out_msg.command.type = CommandType::FeedTrigger;
        } else if (strcmp(cmd_name, "sync_lights") == 0) {
            out_msg.command.type = CommandType::SyncLights;
        } else if (strcmp(cmd_name, "sync_time") == 0) {
            out_msg.command.type = CommandType::SyncTime;
            out_msg.command.epoch_time = doc["epoch"] | 0UL;
        } else if (strcmp(cmd_name, "clear_alarm") == 0) {
            out_msg.command.type = CommandType::ClearAlarm;
        } else if (strcmp(cmd_name, "get_status") == 0) {
            out_msg.command.type = CommandType::GetStatus;
        } else if (strcmp(cmd_name, "safe_mode") == 0) {
            out_msg.command.type = CommandType::SetSafeMode;
            out_msg.command.state = (doc["val"] | 0) != 0;
        } else if (strcmp(cmd_name, "enter_ota") == 0) {
            out_msg.command.type = CommandType::EnterOta;
            const char *s = doc["ssid"] | "";
            strncpy(out_msg.command.ota_ssid, s, sizeof(out_msg.command.ota_ssid) - 1);
            out_msg.command.ota_ssid[sizeof(out_msg.command.ota_ssid) - 1] = '\0';
            const char *p = doc["pass"] | "";
            strncpy(out_msg.command.ota_pass, p, sizeof(out_msg.command.ota_pass) - 1);
            out_msg.command.ota_pass[sizeof(out_msg.command.ota_pass) - 1] = '\0';
        } else {
            out_msg.command.type = CommandType::Unknown;
        }
        return true;
    }

    if (strcmp(type_str, "ack") == 0) {
        out_msg.type = MessageType::Ack;
        const char *cmd_name = doc["cmd"] | "";
        strncpy(out_msg.ack.cmd, cmd_name, sizeof(out_msg.ack.cmd) - 1);
        out_msg.ack.cmd[sizeof(out_msg.ack.cmd) - 1] = '\0';
        out_msg.ack.ok = doc.containsKey("ok") ? (doc["ok"] | false) : (strcmp(doc["status"] | "", "ok") == 0);
        const char *msg_text = doc["msg"] | "";
        strncpy(out_msg.ack.message, msg_text, sizeof(out_msg.ack.message) - 1);
        out_msg.ack.message[sizeof(out_msg.ack.message) - 1] = '\0';
        return true;
    }

    if (strcmp(type_str, "time_sync") == 0) {
        out_msg.type = MessageType::TimeSync;
        out_msg.sync_time_epoch = doc["epoch"] | 0UL;
        return true;
    }

    out_msg.type = MessageType::Unknown;
    return false;
}

} // namespace AquaProtocol
