#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <string>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <cassert>
#include <random>
#include <chrono>

#include "uart_protocol.h"

// Memory guard structure with sentinels to detect any memory corruption
struct GuardedMessage {
    uint32_t canary_before[4];
    AquaProtocol::AquariumMessage msg;
    uint32_t canary_after[4];

    void reset() {
        canary_before[0] = 0xDEADBEEF;
        canary_before[1] = 0xCAFEBABE;
        canary_before[2] = 0x01234567;
        canary_before[3] = 0x89ABCDEF;
        std::memset(&msg, 0xCC, sizeof(msg));
        canary_after[0] = 0xFEDCBA98;
        canary_after[1] = 0x76543210;
        canary_after[2] = 0xBAADF00D;
        canary_after[3] = 0xFEEDFACE;
    }

    bool verify_canaries() const {
        return canary_before[0] == 0xDEADBEEF &&
               canary_before[1] == 0xCAFEBABE &&
               canary_before[2] == 0x01234567 &&
               canary_before[3] == 0x89ABCDEF &&
               canary_after[0] == 0xFEDCBA98 &&
               canary_after[1] == 0x76543210 &&
               canary_after[2] == 0xBAADF00D &&
               canary_after[3] == 0xFEEDFACE;
    }
};

struct TestResult {
    std::string suite;
    std::string name;
    bool passed;
    std::string detail;
};

std::vector<TestResult> all_results;
GuardedMessage g_guarded;

void record_test(const std::string &suite, const std::string &name, bool passed, const std::string &detail = "") {
    all_results.push_back({suite, name, passed, detail});
    std::cout << (passed ? "[PASS] " : "[FAIL] ")
              << "[" << suite << "] " << name;
    if (!detail.empty()) {
        std::cout << " (" << detail << ")";
    }
    std::cout << std::endl;
}

// ---------------------------------------------------------------------------
// SUITE 1: Null and Extreme String Inputs
// ---------------------------------------------------------------------------
void test_suite_null_and_empty_inputs() {
    const std::string suite = "Suite 1: Null & Empty";

    // 1.1 Nullptr input
    g_guarded.reset();
    bool res1 = AquaProtocol::parseMessage(nullptr, g_guarded.msg);
    bool can1 = g_guarded.verify_canaries();
    record_test(suite, "nullptr input to parseMessage", (!res1 && can1),
                can1 ? "Safely returned false" : "CANARY CORRUPTION DETECTED");

    // 1.2 Empty string
    g_guarded.reset();
    bool res2 = AquaProtocol::parseMessage("", g_guarded.msg);
    bool can2 = g_guarded.verify_canaries();
    record_test(suite, "Empty string \"\"", (!res2 && can2),
                can2 ? "Safely returned false" : "CANARY CORRUPTION DETECTED");

    // 1.3 Whitespace only
    g_guarded.reset();
    bool res3 = AquaProtocol::parseMessage("   \t\r\n   ", g_guarded.msg);
    bool can3 = g_guarded.verify_canaries();
    record_test(suite, "Whitespace only", (!res3 && can3),
                can3 ? "Safely returned false" : "CANARY CORRUPTION DETECTED");

    // 1.4 Single newline
    g_guarded.reset();
    bool res4 = AquaProtocol::parseMessage("\n", g_guarded.msg);
    bool can4 = g_guarded.verify_canaries();
    record_test(suite, "Single newline \\n", (!res4 && can4),
                can4 ? "Safely returned false" : "CANARY CORRUPTION DETECTED");

    // 1.5 JSON primitives that are not objects
    const char *primitives[] = {"null", "true", "false", "0", "12345", "\"raw string\"", "-42.5"};
    bool prim_ok = true;
    for (const char *p : primitives) {
        g_guarded.reset();
        bool r = AquaProtocol::parseMessage(p, g_guarded.msg);
        if (r || !g_guarded.verify_canaries()) {
            prim_ok = false;
        }
    }
    record_test(suite, "Non-object JSON primitives", prim_ok,
                prim_ok ? "All rejected without canaries breached" : "Failure on primitive input");
}

// ---------------------------------------------------------------------------
// SUITE 2: Malformed JSON, Truncated JSON & Deep Nesting
// ---------------------------------------------------------------------------
void test_suite_malformed_and_truncated_json() {
    const std::string suite = "Suite 2: Malformed & Truncated";

    // 2.1 Truncated at every byte offset of valid frames
    const std::vector<std::string> valid_seeds = {
        "{\"type\":\"cmd\",\"cmd\":\"enter_ota\",\"ssid\":\"HomeWiFi_2.4G\",\"pass\":\"SecretPassword123\"}",
        "{\"type\":\"telemetry\",\"ts\":1700000000,\"temp\":25.5,\"temp_tgt\":25.0,\"temp_hyst\":0.3,\"ph\":7.0,\"ec\":400.0,\"relays\":0,\"alarms\":0,\"w_low\":false,\"uptime\":100,\"safe\":true,\"lmode\":1,\"ota_mode\":true,\"ota_ip\":\"192.168.4.1\"}",
        "{\"type\":\"ack\",\"cmd\":\"enter_ota\",\"ok\":true,\"msg\":\"STA:192.168.1.100\"}",
        "{\"type\":\"time_sync\",\"epoch\":1710000000}"
    };

    bool truncation_safe = true;
    size_t total_truncation_tests = 0;
    for (const auto &seed : valid_seeds) {
        for (size_t len = 1; len < seed.length(); ++len) {
            std::string sub = seed.substr(0, len);
            g_guarded.reset();
            bool r = AquaProtocol::parseMessage(sub.c_str(), g_guarded.msg);
            total_truncation_tests++;
            if (r || !g_guarded.verify_canaries()) {
                truncation_safe = false;
            }
        }
    }
    record_test(suite, "Truncated JSON at every byte offset (" + std::to_string(total_truncation_tests) + " cases)",
                truncation_safe, truncation_safe ? "All safely rejected" : "Unexpected acceptance or canary breach");

    // 2.2 Unbalanced braces and structural syntax errors (strict syntax errors that ArduinoJson MUST reject)
    const std::vector<std::string> malformed_tokens = {
        "{", "}", "{{", "}}", "}{", "{}}", "{{}",
        "{\"type\":", "{\"type\":}", "{\"type\":\"cmd\"",
        "{\"type\":\"cmd\",}", "{\"type\":\"cmd\",,}",
        "{\"type\":\"cmd\":\"enter_ota\"}",
        "{\"type\"::\"cmd\"}",
        "{\"type\" \"cmd\"}",
        "{type:cmd}",
        "{\"type\":\"cmd\",\"cmd\":\"enter_ota\",,,}",
        "[,]", "[]", "[{}]",
        "{\"type\":\"cmd\", \"extra\": [}"
    };

    bool malformed_safe = true;
    for (const auto &s : malformed_tokens) {
        g_guarded.reset();
        bool r = AquaProtocol::parseMessage(s.c_str(), g_guarded.msg);
        bool can = g_guarded.verify_canaries();
        if (!can || r) {
            malformed_safe = false;
        }
    }
    record_test(suite, "Structural JSON syntax errors (" + std::to_string(malformed_tokens.size()) + " cases)",
                malformed_safe, malformed_safe ? "All safely rejected" : "Malformed error");

    // 2.2b Lenient syntax test: single-quoted string support in ArduinoJson
    g_guarded.reset();
    bool r_single_quote = AquaProtocol::parseMessage("{\"type\":'cmd'}", g_guarded.msg);
    bool can_single_quote = g_guarded.verify_canaries();
    bool ok_single_quote = r_single_quote && can_single_quote &&
                           (g_guarded.msg.type == AquaProtocol::MessageType::Command) &&
                           (g_guarded.msg.command.type == AquaProtocol::CommandType::Unknown);
    record_test(suite, "Lenient single-quote syntax {'type':'cmd'}", ok_single_quote,
                ok_single_quote ? "Parsed safely via ArduinoJson leniency as CommandType::Unknown" : "Unexpected behavior");

    // 2.3 Deep nesting attack (50 levels of object and array nesting)
    std::string deep_obj;
    std::string deep_arr;
    for (int i = 0; i < 50; ++i) deep_obj += "{\"k\":";
    deep_obj += "\"v\"";
    for (int i = 0; i < 50; ++i) deep_obj += "}";

    for (int i = 0; i < 50; ++i) deep_arr += "[";
    deep_arr += "1";
    for (int i = 0; i < 50; ++i) deep_arr += "]";

    g_guarded.reset();
    bool r_obj = AquaProtocol::parseMessage(deep_obj.c_str(), g_guarded.msg);
    bool can_obj = g_guarded.verify_canaries();

    g_guarded.reset();
    bool r_arr = AquaProtocol::parseMessage(deep_arr.c_str(), g_guarded.msg);
    bool can_arr = g_guarded.verify_canaries();

    record_test(suite, "Deep nesting attack (50 levels)", (!r_obj && can_obj && !r_arr && can_arr),
                (can_obj && can_arr) ? "Safely rejected without stack overflow" : "Canary breach");

    // 2.4 Non-JSON protocol garbage
    const std::vector<std::string> garbage_inputs = {
        "GET /api/status HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n",
        "CONNECT 192.168.4.1:443 HTTP/1.1",
        "+++ATH0\r\n",
        "AT+RST\r\n",
        "<xml><message type=\"cmd\">enter_ota</message></xml>",
        "'; DROP TABLE telemetry; --",
        "\\x1b[2J\\x1b[H\x00\x01\x02\xFF\xFE\xFD",
        std::string(2048, 'X')
    };

    bool garbage_safe = true;
    for (const auto &g : garbage_inputs) {
        g_guarded.reset();
        bool r = AquaProtocol::parseMessage(g.c_str(), g_guarded.msg);
        if (r || !g_guarded.verify_canaries()) {
            garbage_safe = false;
        }
    }
    record_test(suite, "Non-JSON protocol garbage (HTTP, AT, SQL, binary)", garbage_safe,
                garbage_safe ? "All safely rejected" : "Canary breach or parse error");
}

// ---------------------------------------------------------------------------
// SUITE 3: Buffer Overflow Payloads & Boundary Sizes
// ---------------------------------------------------------------------------
void test_suite_buffer_overflow_payloads() {
    const std::string suite = "Suite 3: Buffer Boundaries";

    // 3.1 SSID boundary checks (buffer size: 33 chars -> max 32 chars + null)
    // Test exact 32 chars
    std::string ssid_32(32, 'A');
    std::string json_ssid_32 = "{\"type\":\"cmd\",\"cmd\":\"enter_ota\",\"ssid\":\"" + ssid_32 + "\"}";
    g_guarded.reset();
    bool r1 = AquaProtocol::parseMessage(json_ssid_32.c_str(), g_guarded.msg);
    bool can1 = g_guarded.verify_canaries();
    bool ok1 = r1 && can1 && (std::strlen(g_guarded.msg.command.ota_ssid) == 32) &&
               (g_guarded.msg.command.ota_ssid[32] == '\0') &&
               (g_guarded.msg.command.ota_ssid == ssid_32);
    record_test(suite, "SSID exact boundary (32 chars)", ok1,
                ok1 ? "Stored exactly 32 chars + null" : "Failed boundary check");

    // Test 33 chars (1 beyond limit)
    std::string ssid_33(33, 'B');
    std::string json_ssid_33 = "{\"type\":\"cmd\",\"cmd\":\"enter_ota\",\"ssid\":\"" + ssid_33 + "\"}";
    g_guarded.reset();
    bool r2 = AquaProtocol::parseMessage(json_ssid_33.c_str(), g_guarded.msg);
    bool can2 = g_guarded.verify_canaries();
    bool ok2 = r2 && can2 && (std::strlen(g_guarded.msg.command.ota_ssid) == 32) &&
               (g_guarded.msg.command.ota_ssid[32] == '\0') &&
               (std::strncmp(g_guarded.msg.command.ota_ssid, ssid_33.c_str(), 32) == 0);
    record_test(suite, "SSID boundary +1 (33 chars)", ok2,
                ok2 ? "Truncated safely to 32 chars + null" : "Canary breach or invalid length");

    // Test 100 chars, 500 chars, 4000 chars SSID
    std::vector<size_t> large_sizes = {64, 128, 256, 500, 1000, 4000};
    bool ssid_large_ok = true;
    for (size_t sz : large_sizes) {
        std::string huge_ssid(sz, 'S');
        std::string json = "{\"type\":\"cmd\",\"cmd\":\"enter_ota\",\"ssid\":\"" + huge_ssid + "\"}";
        g_guarded.reset();
        bool r = AquaProtocol::parseMessage(json.c_str(), g_guarded.msg);
        bool can = g_guarded.verify_canaries();
        if (!can || (r && (std::strlen(g_guarded.msg.command.ota_ssid) > 32 ||
                           g_guarded.msg.command.ota_ssid[sizeof(g_guarded.msg.command.ota_ssid) - 1] != '\0'))) {
            ssid_large_ok = false;
        }
    }
    record_test(suite, "SSID buffer overflow attack (64 to 4000 chars)", ssid_large_ok,
                ssid_large_ok ? "Zero canary breaches, length bounded to 32" : "Buffer overflow detected!");

    // 3.2 Password boundary checks (buffer size: 65 chars -> max 64 chars + null)
    // Test exact 64 chars
    std::string pass_64(64, 'P');
    std::string json_pass_64 = "{\"type\":\"cmd\",\"cmd\":\"enter_ota\",\"pass\":\"" + pass_64 + "\"}";
    g_guarded.reset();
    bool r3 = AquaProtocol::parseMessage(json_pass_64.c_str(), g_guarded.msg);
    bool can3 = g_guarded.verify_canaries();
    bool ok3 = r3 && can3 && (std::strlen(g_guarded.msg.command.ota_pass) == 64) &&
               (g_guarded.msg.command.ota_pass[64] == '\0') &&
               (g_guarded.msg.command.ota_pass == pass_64);
    record_test(suite, "Password exact boundary (64 chars)", ok3,
                ok3 ? "Stored exactly 64 chars + null" : "Failed boundary check");

    // Test 65 chars (1 beyond limit)
    std::string pass_65(65, 'Q');
    std::string json_pass_65 = "{\"type\":\"cmd\",\"cmd\":\"enter_ota\",\"pass\":\"" + pass_65 + "\"}";
    g_guarded.reset();
    bool r4 = AquaProtocol::parseMessage(json_pass_65.c_str(), g_guarded.msg);
    bool can4 = g_guarded.verify_canaries();
    bool ok4 = r4 && can4 && (std::strlen(g_guarded.msg.command.ota_pass) == 64) &&
               (g_guarded.msg.command.ota_pass[64] == '\0') &&
               (std::strncmp(g_guarded.msg.command.ota_pass, pass_65.c_str(), 64) == 0);
    record_test(suite, "Password boundary +1 (65 chars)", ok4,
                ok4 ? "Truncated safely to 64 chars + null" : "Canary breach or invalid length");

    // Test 100 to 4000 chars Password
    bool pass_large_ok = true;
    for (size_t sz : large_sizes) {
        std::string huge_pass(sz, 'X');
        std::string json = "{\"type\":\"cmd\",\"cmd\":\"enter_ota\",\"pass\":\"" + huge_pass + "\"}";
        g_guarded.reset();
        bool r = AquaProtocol::parseMessage(json.c_str(), g_guarded.msg);
        bool can = g_guarded.verify_canaries();
        if (!can || (r && (std::strlen(g_guarded.msg.command.ota_pass) > 64 ||
                           g_guarded.msg.command.ota_pass[sizeof(g_guarded.msg.command.ota_pass) - 1] != '\0'))) {
            pass_large_ok = false;
        }
    }
    record_test(suite, "Password buffer overflow attack (64 to 4000 chars)", pass_large_ok,
                pass_large_ok ? "Zero canary breaches, length bounded to 64" : "Buffer overflow detected!");

    // 3.3 IP address boundary checks (buffer size: 16 chars -> max 15 chars + null)
    // Test exact 15 chars: "192.168.100.200"
    std::string ip_15 = "192.168.100.200";
    std::string json_ip_15 = "{\"type\":\"telemetry\",\"ota_mode\":true,\"ota_ip\":\"" + ip_15 + "\"}";
    g_guarded.reset();
    bool r5 = AquaProtocol::parseMessage(json_ip_15.c_str(), g_guarded.msg);
    bool can5 = g_guarded.verify_canaries();
    bool ok5 = r5 && can5 && (std::strlen(g_guarded.msg.telemetry.ota_ip) == 15) &&
               (g_guarded.msg.telemetry.ota_ip[15] == '\0') &&
               (g_guarded.msg.telemetry.ota_ip == ip_15);
    record_test(suite, "IP exact boundary (15 chars)", ok5,
                ok5 ? "Stored exactly 15 chars + null" : "Failed boundary check");

    // Test 16 chars: "192.168.100.2000"
    std::string ip_16 = "192.168.100.2000";
    std::string json_ip_16 = "{\"type\":\"telemetry\",\"ota_mode\":true,\"ota_ip\":\"" + ip_16 + "\"}";
    g_guarded.reset();
    bool r6 = AquaProtocol::parseMessage(json_ip_16.c_str(), g_guarded.msg);
    bool can6 = g_guarded.verify_canaries();
    bool ok6 = r6 && can6 && (std::strlen(g_guarded.msg.telemetry.ota_ip) == 15) &&
               (g_guarded.msg.telemetry.ota_ip[15] == '\0') &&
               (std::strncmp(g_guarded.msg.telemetry.ota_ip, ip_16.c_str(), 15) == 0);
    record_test(suite, "IP boundary +1 (16 chars)", ok6,
                ok6 ? "Truncated safely to 15 chars + null" : "Canary breach or invalid length");

    // Test full IPv6 (39 chars) and huge hostnames (up to 2000 chars)
    std::vector<std::string> oversized_ips = {
        "2001:0db8:85a3:0000:0000:8a2e:0370:7334",
        "fe80::1ff:fe23:4567:890a",
        "some-very-long-aquarium-hostname-that-exceeds-ip-buffer.local",
        std::string(1000, '9')
    };
    bool ip_large_ok = true;
    for (const auto &ip : oversized_ips) {
        std::string json = "{\"type\":\"telemetry\",\"ota_mode\":true,\"ota_ip\":\"" + ip + "\"}";
        g_guarded.reset();
        bool r = AquaProtocol::parseMessage(json.c_str(), g_guarded.msg);
        bool can = g_guarded.verify_canaries();
        if (!can || (r && (std::strlen(g_guarded.msg.telemetry.ota_ip) > 15 ||
                           g_guarded.msg.telemetry.ota_ip[sizeof(g_guarded.msg.telemetry.ota_ip) - 1] != '\0'))) {
            ip_large_ok = false;
        }
    }
    record_test(suite, "IP buffer overflow attack (IPv6 & large strings)", ip_large_ok,
                ip_large_ok ? "Zero canary breaches, length bounded to 15" : "Buffer overflow detected!");

    // 3.4 ACK cmd & message boundary checks
    // cmd is 24 chars (max 23 chars + null), msg is 48 chars (max 47 chars + null)
    std::string ack_huge = "{\"type\":\"ack\",\"cmd\":\"" + std::string(100, 'C') + "\",\"ok\":true,\"msg\":\"" + std::string(200, 'M') + "\"}";
    g_guarded.reset();
    bool r_ack = AquaProtocol::parseMessage(ack_huge.c_str(), g_guarded.msg);
    bool can_ack = g_guarded.verify_canaries();
    bool ok_ack = r_ack && can_ack &&
                  (std::strlen(g_guarded.msg.ack.cmd) <= 23) &&
                  (g_guarded.msg.ack.cmd[sizeof(g_guarded.msg.ack.cmd) - 1] == '\0') &&
                  (std::strlen(g_guarded.msg.ack.message) <= 47) &&
                  (g_guarded.msg.ack.message[sizeof(g_guarded.msg.ack.message) - 1] == '\0');
    record_test(suite, "ACK cmd and msg buffer bounds (24 & 48 chars)", ok_ack,
                ok_ack ? "Truncated safely within buffers" : "Buffer overflow in ACK fields");
}

// ---------------------------------------------------------------------------
// SUITE 4: Missing Fields & Type Confusion
// ---------------------------------------------------------------------------
void test_suite_missing_fields_and_type_confusion() {
    const std::string suite = "Suite 4: Missing Fields & Types";

    // 4.1 Missing fields defaults
    // Empty object
    g_guarded.reset();
    bool r_empty = AquaProtocol::parseMessage("{}", g_guarded.msg);
    bool can_empty = g_guarded.verify_canaries();
    record_test(suite, "Empty JSON object {}", (!r_empty && can_empty),
                can_empty ? "Safely rejected unknown type" : "Canary breach");

    // Telemetry with NO fields (all missing)
    g_guarded.reset();
    bool r_tel_empty = AquaProtocol::parseMessage("{\"type\":\"telemetry\"}", g_guarded.msg);
    bool can_tel_empty = g_guarded.verify_canaries();
    bool ok_tel_defaults = r_tel_empty && can_tel_empty &&
                           (g_guarded.msg.type == AquaProtocol::MessageType::Telemetry) &&
                           (g_guarded.msg.telemetry.timestamp == 0UL) &&
                           (g_guarded.msg.telemetry.temp_target == 25.0f) &&
                           (g_guarded.msg.telemetry.temp_hysteresis == 0.3f) &&
                           (g_guarded.msg.telemetry.ph == 7.0f) &&
                           (g_guarded.msg.telemetry.light_mode == 1U) &&
                           (!g_guarded.msg.telemetry.ota_mode) &&
                           (g_guarded.msg.telemetry.ota_ip[0] == '\0');
    record_test(suite, "Telemetry frame with zero fields (defaults)", ok_tel_defaults,
                ok_tel_defaults ? "All default fallbacks applied correctly" : "Default fallback mismatch");

    // Command with NO cmd field
    g_guarded.reset();
    bool r_cmd_none = AquaProtocol::parseMessage("{\"type\":\"cmd\"}", g_guarded.msg);
    bool can_cmd_none = g_guarded.verify_canaries();
    bool ok_cmd_none = r_cmd_none && can_cmd_none &&
                       (g_guarded.msg.type == AquaProtocol::MessageType::Command) &&
                       (g_guarded.msg.command.type == AquaProtocol::CommandType::Unknown);
    record_test(suite, "Command frame without cmd field", ok_cmd_none,
                ok_cmd_none ? "Parsed as CommandType::Unknown" : "Unexpected command type");

    // ACK with NO cmd, ok, or msg
    g_guarded.reset();
    bool r_ack_none = AquaProtocol::parseMessage("{\"type\":\"ack\"}", g_guarded.msg);
    bool can_ack_none = g_guarded.verify_canaries();
    bool ok_ack_none = r_ack_none && can_ack_none &&
                       (g_guarded.msg.type == AquaProtocol::MessageType::Ack) &&
                       (g_guarded.msg.ack.cmd[0] == '\0') &&
                       (!g_guarded.msg.ack.ok) &&
                       (g_guarded.msg.ack.message[0] == '\0');
    record_test(suite, "ACK frame without fields", ok_ack_none,
                ok_ack_none ? "Defaulted to ok=false, empty strings" : "ACK default mismatch");

    // 4.2 Type confusion attacks
    const std::vector<std::pair<std::string, std::string>> type_confusion_cases = {
        {"{\"type\": null}", "type is null"},
        {"{\"type\": 12345}", "type is integer"},
        {"{\"type\": true}", "type is boolean"},
        {"{\"type\": [\"telemetry\"]}", "type is array"},
        {"{\"type\": {\"sub\":\"telemetry\"}}", "type is object"},
        {"{\"type\":\"cmd\",\"cmd\": null}", "cmd is null"},
        {"{\"type\":\"cmd\",\"cmd\": 12345}", "cmd is integer"},
        {"{\"type\":\"cmd\",\"cmd\": true}", "cmd is boolean"},
        {"{\"type\":\"cmd\",\"cmd\": []}", "cmd is array"},
        {"{\"type\":\"cmd\",\"cmd\": {}}", "cmd is object"},
        {"{\"type\":\"cmd\",\"cmd\":\"enter_ota\",\"ssid\": null, \"pass\": null}", "enter_ota credentials are null"},
        {"{\"type\":\"cmd\",\"cmd\":\"enter_ota\",\"ssid\": 123, \"pass\": 456}", "enter_ota credentials are ints"},
        {"{\"type\":\"cmd\",\"cmd\":\"enter_ota\",\"ssid\": true, \"pass\": false}", "enter_ota credentials are bools"},
        {"{\"type\":\"cmd\",\"cmd\":\"enter_ota\",\"ssid\": [], \"pass\": {}}", "enter_ota credentials are container objects"},
        {"{\"type\":\"telemetry\",\"ota_ip\": null}", "ota_ip is null"},
        {"{\"type\":\"telemetry\",\"ota_ip\": 123456}", "ota_ip is integer"},
        {"{\"type\":\"telemetry\",\"ota_ip\": true}", "ota_ip is boolean"},
        {"{\"type\":\"telemetry\",\"ota_ip\": [\"192.168.1.1\"]}", "ota_ip is array"},
        {"{\"type\":\"telemetry\",\"ota_mode\": \"true\"}", "ota_mode is string"},
        {"{\"type\":\"telemetry\",\"ota_mode\": 1}", "ota_mode is int 1"},
        {"{\"type\":\"telemetry\",\"ota_mode\": null}", "ota_mode is null"},
        {"{\"type\":\"telemetry\",\"temp\": \"not_a_float\"}", "temperature is invalid string"},
        {"{\"type\":\"ack\",\"cmd\": null, \"ok\": null, \"msg\": null}", "ack fields all null"},
        {"{\"type\":\"ack\",\"cmd\": 123, \"ok\": 1, \"msg\": 456}", "ack fields all numbers"},
        {"{\"type\":\"ack\",\"status\": null}", "ack status is null"},
        {"{\"type\":\"ack\",\"status\": 123}", "ack status is integer"},
        {"{\"type\":\"ack\",\"status\": []}", "ack status is array"}
    };

    bool type_confusion_passed = true;
    for (const auto &tc : type_confusion_cases) {
        g_guarded.reset();
        AquaProtocol::parseMessage(tc.first.c_str(), g_guarded.msg);
        if (!g_guarded.verify_canaries()) {
            type_confusion_passed = false;
            std::cerr << "Canary violation on: " << tc.second << std::endl;
        }
    }
    record_test(suite, "Type confusion across all fields (" + std::to_string(type_confusion_cases.size()) + " cases)",
                type_confusion_passed, type_confusion_passed ? "All handled with zero crashes or canary breaches" : "Type confusion crash detected!");
}

// ---------------------------------------------------------------------------
// SUITE 5: Extreme Numbers, Unicode & Encoding
// ---------------------------------------------------------------------------
void test_suite_numbers_and_unicode() {
    const std::string suite = "Suite 5: Numbers & Unicode";

    // 5.1 Negative numbers for unsigned integers
    const std::vector<std::string> negative_cases = {
        "{\"type\":\"cmd\",\"cmd\":\"set_relay\",\"ch\": -1, \"val\": 1}",
        "{\"type\":\"cmd\",\"cmd\":\"set_relay\",\"ch\": -2147483648, \"val\": -1}",
        "{\"type\":\"cmd\",\"cmd\":\"set_sched\",\"dev\": -1, \"on_h\": -5, \"on_m\": -10}",
        "{\"type\":\"cmd\",\"cmd\":\"sync_time\",\"epoch\": -1}",
        "{\"type\":\"telemetry\",\"ts\": -1, \"uptime\": -500, \"relays\": -1, \"alarms\": -1}"
    };

    bool neg_safe = true;
    for (const auto &nc : negative_cases) {
        g_guarded.reset();
        AquaProtocol::parseMessage(nc.c_str(), g_guarded.msg);
        if (!g_guarded.verify_canaries()) {
            neg_safe = false;
        }
    }
    record_test(suite, "Negative integers for unsigned fields", neg_safe,
                neg_safe ? "Handled without canary violation" : "Canary breach");

    // 5.2 Floating point boundary extremes
    const std::vector<std::string> float_cases = {
        "{\"type\":\"telemetry\",\"temp\": 1e38, \"temp_tgt\": -1e38}",
        "{\"type\":\"telemetry\",\"temp\": 1e308, \"temp_tgt\": -1e308}",
        "{\"type\":\"telemetry\",\"temp\": 1e-308, \"ph\": 0.00000000001}",
        "{\"type\":\"telemetry\",\"temp\": 0.0, \"ph\": -0.0}",
        "{\"type\":\"cmd\",\"cmd\":\"set_temp\",\"target\": 1e38, \"hyst\": -1e38}",
        "{\"type\":\"cmd\",\"cmd\":\"set_temp\",\"target\": 999999999.999, \"hyst\": 0.0000001}"
    };

    bool float_safe = true;
    for (const auto &fc : float_cases) {
        g_guarded.reset();
        bool r = AquaProtocol::parseMessage(fc.c_str(), g_guarded.msg);
        if (!g_guarded.verify_canaries() || !r) {
            float_safe = false;
        }
    }
    record_test(suite, "Extreme floating point exponents (1e38, 1e308, 1e-308)", float_safe,
                float_safe ? "Parsed safely without crashes or canary breaches" : "Float parsing failed");

    // 5.3 Unicode strings & special characters
    const std::vector<std::pair<std::string, std::string>> unicode_cases = {
        {"{\"type\":\"cmd\",\"cmd\":\"enter_ota\",\"ssid\":\"ZażółćGęśląJaźń\",\"pass\":\"PchnąćWTęŁódź\"}", "Polish diacritics"},
        {"{\"type\":\"cmd\",\"cmd\":\"enter_ota\",\"ssid\":\"🐠🐟AquaWorld🐡\",\"pass\":\"Password🔑🔒\"}", "UTF-8 emojis"},
        {"{\"type\":\"cmd\",\"cmd\":\"enter_ota\",\"ssid\":\"水槽ネットワーク\",\"pass\":\"パスワード123\"}", "Japanese Kanji/Kana"},
        {"{\"type\":\"cmd\",\"cmd\":\"enter_ota\",\"ssid\":\"شبكة_الحوض\",\"pass\":\"كلمة_السر\"}", "Arabic RTL text"},
        {"{\"type\":\"cmd\",\"cmd\":\"enter_ota\",\"ssid\":\"Größenbeschränkung\",\"pass\":\"SüßwasserAquarium\"}", "German umlauts"},
        {"{\"type\":\"cmd\",\"cmd\":\"enter_ota\",\"ssid\":\"SSID\\\"Quote\\\\Slash\",\"pass\":\"Pass\\nLine\\tTab\"}", "Escaped quotes, slashes, tabs"},
        {"{\"type\":\"ack\",\"cmd\":\"enter_ota\",\"ok\":true,\"msg\":\"Błąd połączenia: Zażółć\"}", "ACK unicode message"}
    };

    bool unicode_safe = true;
    for (const auto &uc : unicode_cases) {
        g_guarded.reset();
        bool r = AquaProtocol::parseMessage(uc.first.c_str(), g_guarded.msg);
        if (!g_guarded.verify_canaries() || !r) {
            unicode_safe = false;
            std::cerr << "Unicode failure: " << uc.second << std::endl;
        }
    }
    record_test(suite, "Unicode (Polish, Emojis, CJK, Arabic, Escaped) (" + std::to_string(unicode_cases.size()) + " cases)",
                unicode_safe, unicode_safe ? "All parsed and safely stored in buffers" : "Unicode parsing failure");
}

// ---------------------------------------------------------------------------
// SUITE 6: Serializer Stress & Round-Trip Invariants
// ---------------------------------------------------------------------------
void test_suite_serializer_stress_and_roundtrip() {
    const std::string suite = "Suite 6: Serializer Stress";

    // 6.1 Telemetry serialization with boundary values
    AquaProtocol::AquariumTelemetry t;
    std::memset(&t, 0, sizeof(t));
    t.timestamp = 1773852100UL;
    t.temperature = 25.45f;
    t.temp_target = 26.00f;
    t.temp_hysteresis = 0.50f;
    t.ph = 6.80f;
    t.ec = 450.0f;
    t.relay_states = 0xFF; // all 8 on
    t.alarms = 0xFFFF;     // all alarms
    t.water_level_low = true;
    t.water_level_max_reached = true;
    t.water_level_max_valid = true;
    t.uptime_seconds = 864000UL;
    t.safe_mode = true;
    t.light_mode = 3;
    t.ota_mode = true;
    std::strncpy(t.ota_ip, "192.168.100.200", sizeof(t.ota_ip) - 1);
    t.ota_ip[sizeof(t.ota_ip) - 1] = '\0';

    String s_tel = AquaProtocol::serializeTelemetry(t);
    bool ser_tel_ok = (s_tel.length() > 0) &&
                      (s_tel.find("\"ota_mode\":true") != std::string::npos) &&
                      (s_tel.find("\"ota_ip\":\"192.168.100.200\"") != std::string::npos);

    // Round-trip deserialization
    g_guarded.reset();
    bool r_tel = AquaProtocol::parseMessage(s_tel.c_str(), g_guarded.msg);
    bool can_tel = g_guarded.verify_canaries();
    bool rt_tel_ok = ser_tel_ok && r_tel && can_tel &&
                     (g_guarded.msg.telemetry.timestamp == t.timestamp) &&
                     (std::abs(g_guarded.msg.telemetry.temperature - t.temperature) < 0.05f) &&
                     (g_guarded.msg.telemetry.relay_states == 0xFF) &&
                     (g_guarded.msg.telemetry.alarms == 0xFFFF) &&
                     (g_guarded.msg.telemetry.water_level_max_reached == true) &&
                     (g_guarded.msg.telemetry.water_level_max_valid == true) &&
                     (g_guarded.msg.telemetry.ota_mode == true) &&
                     (std::strcmp(g_guarded.msg.telemetry.ota_ip, "192.168.100.200") == 0);
    record_test(suite, "serializeTelemetry round-trip (max fields & OTA active)", rt_tel_ok,
                rt_tel_ok ? "Serialized and restored all fields identically" : "Round-trip field mismatch");

    // 6.2 All CommandType values serialization and deserialization
    bool all_cmds_ok = true;
    for (uint8_t c = 1; c <= 10; ++c) {
        AquaProtocol::AquariumCommand cmd;
        std::memset(&cmd, 0, sizeof(cmd));
        cmd.type = static_cast<AquaProtocol::CommandType>(c);
        cmd.channel = 2;
        cmd.state = true;
        cmd.temp_target = 24.5f;
        cmd.temp_hysteresis = 0.4f;
        cmd.schedule_dev = 1;
        cmd.on_hour = 8;
        cmd.on_minute = 30;
        cmd.off_hour = 20;
        cmd.off_minute = 0;
        cmd.schedule_enabled = true;
        cmd.epoch_time = 1710000000UL;
        std::strncpy(cmd.ota_ssid, "TestSSID", sizeof(cmd.ota_ssid) - 1);
        std::strncpy(cmd.ota_pass, "TestPass", sizeof(cmd.ota_pass) - 1);

        String s_cmd = AquaProtocol::serializeCommand(cmd);
        if (s_cmd.length() == 0) {
            all_cmds_ok = false;
            break;
        }

        g_guarded.reset();
        bool parsed = AquaProtocol::parseMessage(s_cmd.c_str(), g_guarded.msg);
        if (!parsed || !g_guarded.verify_canaries() || g_guarded.msg.command.type != cmd.type) {
            all_cmds_ok = false;
            break;
        }
    }
    record_test(suite, "serializeCommand round-trip for all 10 CommandType values", all_cmds_ok,
                all_cmds_ok ? "All command types serialized and parsed correctly" : "Command serialization failed");

    // 6.3 Invalid CommandType values
    AquaProtocol::AquariumCommand inv_cmd1, inv_cmd2;
    inv_cmd1.type = AquaProtocol::CommandType::Unknown;
    inv_cmd2.type = static_cast<AquaProtocol::CommandType>(99);
    String s_inv1 = AquaProtocol::serializeCommand(inv_cmd1);
    String s_inv2 = AquaProtocol::serializeCommand(inv_cmd2);
    bool inv_cmd_safe = (s_inv1.length() == 0) && (s_inv2.length() == 0);
    record_test(suite, "serializeCommand invalid command types (Unknown, 99)", inv_cmd_safe,
                inv_cmd_safe ? "Safely returned empty string" : "Emitted invalid JSON");

    // 6.4 ACK serialization with nullptrs, empty strings, oversized message (150 chars), and huge message (2000 chars)
    String s_ack_null = AquaProtocol::serializeAck(nullptr, false, nullptr);
    bool s_ack_null_ok = (s_ack_null.length() > 0) && (s_ack_null.find("\"cmd\":\"\"") != std::string::npos);

    // Case A: 150-char message (exceeds 48-byte buffer, but fits in 768-byte doc) -> must parse and truncate safely
    std::string msg_150(150, 'M');
    String s_ack_150 = AquaProtocol::serializeAck("test_cmd", true, msg_150.c_str());
    g_guarded.reset();
    bool r_ack_150 = AquaProtocol::parseMessage(s_ack_150.c_str(), g_guarded.msg);
    bool can_ack_150 = g_guarded.verify_canaries();
    bool ack_150_ok = r_ack_150 && can_ack_150 &&
                      (std::strlen(g_guarded.msg.ack.message) == 47) &&
                      (g_guarded.msg.ack.message[47] == '\0') &&
                      (std::strncmp(g_guarded.msg.ack.message, msg_150.c_str(), 47) == 0);
    record_test(suite, "serializeAck 150-char oversized message safely truncated", ack_150_ok,
                ack_150_ok ? "Truncated safely to 47 chars + null without canary breach" : "Buffer overflow in ACK message");

    // Case B: 2000-char message (exceeds 768-byte doc) -> must safely reject without memory corruption
    std::string huge_msg(2000, 'X');
    String s_ack_huge = AquaProtocol::serializeAck("test_cmd", true, huge_msg.c_str());
    g_guarded.reset();
    bool r_ack_huge = AquaProtocol::parseMessage(s_ack_huge.c_str(), g_guarded.msg);
    bool can_ack_huge = g_guarded.verify_canaries();
    bool ack_huge_ok = s_ack_null_ok && (s_ack_huge.length() > 2000) && (!r_ack_huge) && can_ack_huge;
    record_test(suite, "serializeAck huge 2000-char message (bounds check)", ack_huge_ok,
                ack_huge_ok ? "Document capacity (768 bytes) safely rejected oversized message without crash" : "Canary breach or unexpected acceptance");

    // 6.5 Floating point NAN and INFINITY handling in serializeTelemetry
    AquaProtocol::AquariumTelemetry t_nan;
    std::memset(&t_nan, 0, sizeof(t_nan));
    t_nan.temperature = NAN;
    t_nan.ph = INFINITY;
    String s_nan = AquaProtocol::serializeTelemetry(t_nan);
    // Observe: serializeTelemetry produces "temp":nan which is non-standard JSON
    g_guarded.reset();
    bool r_nan = AquaProtocol::parseMessage(s_nan.c_str(), g_guarded.msg);
    bool can_nan = g_guarded.verify_canaries();
    record_test(suite, "serializeTelemetry NAN & INFINITY round-trip", can_nan,
                can_nan ? (r_nan ? "Parsed NAN/INF safely" : "Safely rejected unquoted NAN/INF without crash") : "Canary breach on NAN/INF");

    // 6.5 serializeTimeSync boundaries (0 and UINT32_MAX)
    String s_ts0 = AquaProtocol::serializeTimeSync(0UL);
    String s_tsmax = AquaProtocol::serializeTimeSync(0xFFFFFFFFUL);
    g_guarded.reset();
    bool r_ts0 = AquaProtocol::parseMessage(s_ts0.c_str(), g_guarded.msg) && (g_guarded.msg.sync_time_epoch == 0UL);
    g_guarded.reset();
    bool r_tsmax = AquaProtocol::parseMessage(s_tsmax.c_str(), g_guarded.msg) && (g_guarded.msg.sync_time_epoch == 0xFFFFFFFFUL);
    record_test(suite, "serializeTimeSync boundaries (0 and UINT32_MAX)", (r_ts0 && r_tsmax),
                (r_ts0 && r_tsmax) ? "Both extremes serialized and parsed correctly" : "TimeSync failure");
}

// ---------------------------------------------------------------------------
// SUITE 7: High-Throughput Randomized Fuzzing (100,000 Iterations)
// ---------------------------------------------------------------------------
void test_suite_high_throughput_fuzzing(uint32_t iterations = 100000) {
    const std::string suite = "Suite 7: Fuzzing";
    std::cout << "\n--- Launching " << iterations << " iterations of randomized fuzzing ---\n";

    // Fixed deterministic seed for reproducibility
    std::mt19937 rng(1337077331U);

    const std::vector<std::string> seed_corpus = {
        "{\"type\":\"cmd\",\"cmd\":\"enter_ota\",\"ssid\":\"AquaBox_2.4G\",\"pass\":\"SecretPassword123\"}",
        "{\"type\":\"telemetry\",\"ts\":1700000000,\"temp\":25.40,\"temp_tgt\":25.00,\"temp_hyst\":0.30,\"ph\":7.10,\"ec\":420.0,\"relays\":3,\"alarms\":0,\"w_low\":false,\"uptime\":3600,\"safe\":false,\"lmode\":1,\"ota_mode\":true,\"ota_ip\":\"192.168.4.1\"}",
        "{\"type\":\"ack\",\"cmd\":\"enter_ota\",\"ok\":true,\"msg\":\"STA:192.168.1.100\"}",
        "{\"type\":\"time_sync\",\"epoch\":1710000000}",
        "{\"type\":\"cmd\",\"cmd\":\"set_relay\",\"ch\":3,\"val\":1}",
        "{\"type\":\"cmd\",\"cmd\":\"set_temp\",\"target\":26.5,\"hyst\":0.5}",
        "{\"type\":\"cmd\",\"cmd\":\"set_sched\",\"dev\":0,\"on_h\":8,\"on_m\":0,\"off_h\":20,\"off_m\":0,\"en\":1}",
        "{\"type\":\"cmd\",\"cmd\":\"feed\"}",
        "{\"type\":\"cmd\",\"cmd\":\"safe_mode\",\"val\":1}"
    };

    const char json_chars[] = "{}[]\":,0123456789.-+eE\"\"\\ \t\r\n\0abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const size_t num_json_chars = sizeof(json_chars) - 1;

    auto t_start = std::chrono::high_resolution_clock::now();

    uint32_t accepted_count = 0;
    uint32_t rejected_count = 0;
    bool canaries_intact = true;
    uint32_t phase1_iters = iterations / 2; // Mutation fuzzing
    uint32_t phase2_iters = iterations - phase1_iters; // Generative fuzzing

    // Phase 1: Mutation Fuzzing (50,000 iterations)
    for (uint32_t i = 0; i < phase1_iters; ++i) {
        // Pick random seed
        std::string mutated = seed_corpus[rng() % seed_corpus.size()];
        uint32_t num_mutations = 1 + (rng() % 8);

        for (uint32_t m = 0; m < num_mutations; ++m) {
            uint32_t op = rng() % 6;
            if (mutated.empty()) {
                mutated = "{}";
                continue;
            }
            size_t pos = rng() % mutated.size();

            switch (op) {
            case 0: // Bit flip
                mutated[pos] ^= (1U << (rng() % 8));
                break;
            case 1: // Byte deletion
                mutated.erase(pos, 1);
                break;
            case 2: // Byte insertion of JSON character
                mutated.insert(pos, 1, json_chars[rng() % num_json_chars]);
                break;
            case 3: // Byte replacement
                mutated[pos] = json_chars[rng() % num_json_chars];
                break;
            case 4: // Insertion of huge sequence
                mutated.insert(pos, rng() % 200, json_chars[rng() % num_json_chars]);
                break;
            case 5: // Null byte injection
                mutated.insert(pos, 1, '\0');
                break;
            }
        }

        g_guarded.reset();
        bool parsed = AquaProtocol::parseMessage(mutated.c_str(), g_guarded.msg);
        if (!g_guarded.verify_canaries()) {
            canaries_intact = false;
            std::cerr << "CRITICAL FUZZING FAILURE: Canary breach in Phase 1 at iteration " << i << std::endl;
            break;
        }
        if (parsed) {
            accepted_count++;
        } else {
            rejected_count++;
        }
    }

    // Phase 2: Raw Generative Fuzzing (50,000 iterations)
    if (canaries_intact) {
        for (uint32_t i = 0; i < phase2_iters; ++i) {
            size_t len = rng() % 512;
            std::string raw;
            raw.reserve(len);
            for (size_t k = 0; k < len; ++k) {
                if (rng() % 4 == 0) {
                    raw.push_back(static_cast<char>(rng() % 256));
                } else {
                    raw.push_back(json_chars[rng() % num_json_chars]);
                }
            }

            g_guarded.reset();
            bool parsed = AquaProtocol::parseMessage(raw.c_str(), g_guarded.msg);
            if (!g_guarded.verify_canaries()) {
                canaries_intact = false;
                std::cerr << "CRITICAL FUZZING FAILURE: Canary breach in Phase 2 at iteration " << i << std::endl;
                break;
            }
            if (parsed) {
                accepted_count++;
            } else {
                rejected_count++;
            }
        }
    }

    auto t_end = std::chrono::high_resolution_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    std::stringstream detail;
    detail << "Completed " << iterations << " iterations in " << std::fixed << std::setprecision(1) << elapsed_ms
           << " ms (" << std::setprecision(0) << (iterations / (elapsed_ms / 1000.0)) << " iter/sec). "
           << "Accepted: " << accepted_count << ", Rejected: " << rejected_count << ", Canaries intact: "
           << (canaries_intact ? "YES" : "NO");

    record_test(suite, "High-throughput mutation & generative fuzzing (100,000 iterations)",
                canaries_intact, detail.str());
}

// ---------------------------------------------------------------------------
// Main Entry Point
// ---------------------------------------------------------------------------
int main() {
    std::cout << "=======================================================\n";
    std::cout << "  EMPIRICAL CHALLENGER: UART PROTOCOL NDJSON FUZZER    \n";
    std::cout << "=======================================================\n\n";

    test_suite_null_and_empty_inputs();
    test_suite_malformed_and_truncated_json();
    test_suite_buffer_overflow_payloads();
    test_suite_missing_fields_and_type_confusion();
    test_suite_numbers_and_unicode();
    test_suite_serializer_stress_and_roundtrip();
    test_suite_high_throughput_fuzzing(100000);

    int passed = 0;
    int failed = 0;
    for (const auto &r : all_results) {
        if (r.passed) {
            passed++;
        } else {
            failed++;
        }
    }

    std::cout << "\n=======================================================\n";
    std::cout << "  SUMMARY OF ADVERSARIAL PROTOCOL CHALLENGE\n";
    std::cout << "=======================================================\n";
    std::cout << "Total Assertions: " << all_results.size() << "\n";
    std::cout << "Passed:           " << passed << "\n";
    std::cout << "Failed:           " << failed << "\n";
    std::cout << "Memory Safety:    " << (failed == 0 ? "VERIFIED SAFE (0 canary breaches, 0 crashes)" : "VULNERABILITIES FOUND") << "\n";
    std::cout << "Final Verdict:    " << (failed == 0 ? "PASS" : "FAIL") << "\n";
    std::cout << "=======================================================\n";

    return failed;
}
