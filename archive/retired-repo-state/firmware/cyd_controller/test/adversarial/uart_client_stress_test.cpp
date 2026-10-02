#include <iostream>
#include <cassert>
#include <cstdint>
#include <vector>
#include <string>
#include <sstream>

#include "Arduino.h"
#include "uart_client.h"

// Define the mock globals
uint32_t g_mock_millis = 1000;
uint8_t g_speaker_pin_state = 0;
int g_speaker_pin_mode = 0;
std::vector<std::pair<uint32_t, uint8_t>> g_speaker_transitions;
MockSerial Serial;

struct TestResult {
    std::string name;
    bool passed;
    std::string detail;
};

std::vector<TestResult> g_results;

void record(const std::string &name, bool passed, const std::string &detail = "") {
    g_results.push_back({name, passed, detail});
    std::cout << (passed ? "[PASS] " : "[FAIL] ") << name;
    if (!detail.empty()) {
        std::cout << " -- " << detail;
    }
    std::cout << std::endl;
}

void reset_environment(UartClient &client, uint32_t start_millis = 1000) {
    g_mock_millis = start_millis;
    g_speaker_pin_state = 0;
    g_speaker_transitions.clear();
    Serial.clearRx();
    Serial.clearTx();
    client.resetWemosOtaState();
    client.clearNewAck();
}

void update_until_uart_idle(UartClient &client) {
    // Production bounds each Core0 service pass so a malformed or busy UART
    // cannot starve Wi-Fi and UI work.  The harness therefore models several
    // normal scheduler passes for a complete frame instead of assuming an
    // arbitrarily long telemetry packet is consumed in one call.
    constexpr uint8_t kMaximumServicePasses = 8U;
    for (uint8_t pass = 0U; pass < kMaximumServicePasses && Serial.available() > 0; ++pass) {
        client.update();
    }
    assert(Serial.available() == 0);
}

// -------------------------------------------------------------
// Test Group 1: Command TX Suppression during OTA
// -------------------------------------------------------------

void test_tx_suppression_normal_vs_ota() {
    UartClient client;
    reset_environment(client, 1000);

    // Initial state: not in OTA
    assert(!client.isWemosInOta());

    // 1.1 In normal mode, all commands should transmit
    bool r_relay = client.sendSetRelay(0, true);
    bool r_temp = client.sendSetTemp(24.5f, 0.5f);
    bool r_sched = client.sendSetSchedule(0, 8, 0, 20, 0, true);
    bool r_feed = client.sendFeedTrigger();
    bool r_lights = client.sendSyncLights();
    bool r_time = client.sendSyncTime(1700000000UL);
    bool r_alarm = client.sendClearAlarm();
    bool r_safe = client.sendSafeMode(false);
    bool r_ota = client.sendEnterOta();

    bool all_normal_sent = r_relay && r_temp && r_sched && r_feed && r_lights && r_time && r_alarm && r_safe && r_ota;
    if (all_normal_sent && Serial.tx_lines.size() == 9 && Serial.write_calls == 9U) {
        record("TX Suppression: All commands allowed in normal mode", true,
               "9 of 9 command types transmitted as complete one-write NDJSON frames");
    } else {
        record("TX Suppression: All commands allowed in normal mode", false,
               "A command was not emitted as exactly one complete NDJSON frame");
    }

    // Now put WeMos into OTA via ACK ok: true
    reset_environment(client, 2000);
    Serial.feedLine("{\"type\":\"ack\",\"cmd\":\"enter_ota\",\"ok\":true,\"msg\":\"AP:192.168.4.1\"}");
    client.update();

    if (!client.isWemosInOta()) {
        record("TX Suppression: Transition to OTA via ACK", false, "wemos_in_ota_ remained false");
        return;
    }

    // 1.2 In OTA mode, normal commands MUST be suppressed (return false, nothing sent)
    Serial.clearTx();
    r_relay = client.sendSetRelay(1, false);
    r_temp = client.sendSetTemp(26.0f, 0.2f);
    r_sched = client.sendSetSchedule(1, 9, 0, 18, 0, false);
    r_feed = client.sendFeedTrigger();
    r_lights = client.sendSyncLights();
    r_time = client.sendSyncTime(1700000100UL);
    r_alarm = client.sendClearAlarm();
    r_safe = client.sendSafeMode(true);

    bool all_suppressed = (!r_relay) && (!r_temp) && (!r_sched) && (!r_feed) &&
                          (!r_lights) && (!r_time) && (!r_alarm) && (!r_safe);

    if (all_suppressed && Serial.tx_lines.empty()) {
        record("TX Suppression: Normal commands blocked during OTA", true,
               "8 normal command types blocked; 0 bytes emitted to Serial TX");
    } else {
        record("TX Suppression: Normal commands blocked during OTA", false,
               "Command was NOT blocked or emitted data to Serial TX during OTA!");
    }

    // 1.3 EnterOta command must be EXCEPTED from suppression
    r_ota = client.sendEnterOta("HomeNet", "secret123");
    if (r_ota && Serial.tx_lines.size() == 1) {
        record("TX Suppression: EnterOta allowed during OTA", true,
               "EnterOta command successfully transmitted while wemos_in_ota_ == true");
    } else {
        record("TX Suppression: EnterOta allowed during OTA", false,
               "EnterOta was incorrectly blocked during OTA");
    }
}

// -------------------------------------------------------------
// Test Group 2: Offline Timeout (3s) Pausing & 6-Minute Watchdog
// -------------------------------------------------------------

void test_offline_timeout_normal_mode() {
    UartClient client;
    reset_environment(client, 1000);

    // Receive telemetry at t=1000
    Serial.feedLine("{\"type\":\"telemetry\",\"ts\":1700000000,\"temp\":25.0,\"temp_tgt\":25.0,\"temp_hyst\":0.3,\"ph\":7.0,\"ec\":400.0,\"relays\":0,\"alarms\":0,\"w_low\":false,\"uptime\":100,\"safe\":false,\"lmode\":1}");
    client.update();

    if (!client.isControllerOnline()) {
        record("Offline Timeout: Normal initial online", false, "Did not go online on telemetry");
        return;
    }

    // Advance 2999 ms (t = 3999) -> must remain online
    g_mock_millis = 3999;
    client.update();
    bool online_at_2999 = client.isControllerOnline();

    // Advance 3001 ms (t = 4001) -> must go offline
    g_mock_millis = 4001;
    client.update();
    bool offline_at_3001 = !client.isControllerOnline();

    if (online_at_2999 && offline_at_3001) {
        record("Offline Timeout: Standard 3s timeout in normal mode", true,
               "Remains online at 2999ms, goes offline at 3001ms");
    } else {
        record("Offline Timeout: Standard 3s timeout in normal mode", false,
               "Offline timeout behavior deviated from 3000ms specification");
    }
}

void test_offline_timeout_paused_during_ota() {
    UartClient client;
    reset_environment(client, 1000);

    // Telemetry at t=1000
    Serial.feedLine("{\"type\":\"telemetry\",\"ts\":1700000000,\"temp\":25.0,\"temp_tgt\":25.0,\"temp_hyst\":0.3,\"ph\":7.0,\"ec\":400.0,\"relays\":0,\"alarms\":0,\"w_low\":false,\"uptime\":100,\"safe\":false,\"lmode\":1}");
    client.update();
    assert(client.isControllerOnline());

    // Enter OTA via ACK at t=1050
    g_mock_millis = 1050;
    Serial.feedLine("{\"type\":\"ack\",\"cmd\":\"enter_ota\",\"ok\":true,\"msg\":\"AP:192.168.4.1\"}");
    client.update();
    assert(client.isWemosInOta());

    // Check at 4 seconds (t = 5050) -> should be ONLINE because 3s timeout is paused
    g_mock_millis = 5050;
    client.update();
    bool online_4s = client.isControllerOnline();

    // Check at 60 seconds (t = 61050) -> should be ONLINE
    g_mock_millis = 61050;
    client.update();
    bool online_60s = client.isControllerOnline();

    // Check at 300 seconds (5 minutes, t = 301050) -> should be ONLINE
    g_mock_millis = 301050;
    client.update();
    bool online_300s = client.isControllerOnline();

    // Check at 359 seconds (5m 59s, t = 360050) -> should be ONLINE
    g_mock_millis = 360050;
    client.update();
    bool online_359s = client.isControllerOnline();

    if (online_4s && online_60s && online_300s && online_359s) {
        record("Offline Timeout: 3s timeout successfully paused during OTA", true,
               "Controller stayed online at 4s, 60s, 300s (5m), and 359s");
    } else {
        record("Offline Timeout: 3s timeout successfully paused during OTA", false,
               "Controller prematurely went offline during OTA mode");
    }
}

void test_watchdog_timeout_at_6_minutes() {
    UartClient client;
    reset_environment(client, 1000);

    // Initial telemetry
    Serial.feedLine("{\"type\":\"telemetry\",\"ts\":1700000000,\"temp\":25.0,\"temp_tgt\":25.0,\"temp_hyst\":0.3,\"ph\":7.0,\"ec\":400.0,\"relays\":0,\"alarms\":0,\"w_low\":false,\"uptime\":100,\"safe\":false,\"lmode\":1}");
    client.update();

    // Enter OTA at t=1000
    g_mock_millis = 1000;
    Serial.feedLine("{\"type\":\"ack\",\"cmd\":\"enter_ota\",\"ok\":true,\"msg\":\"AP:192.168.4.1\"}");
    client.update();
    assert(client.isWemosInOta());

    // Exactly at 360,000 ms elapsed (t = 361,000 ms):
    g_mock_millis = 361000;
    client.update();
    bool in_ota_at_360s = client.isWemosInOta();
    bool online_at_360s = client.isControllerOnline();

    // At 360,001 ms elapsed (t = 361,001 ms) -> watchdog MUST trigger
    g_mock_millis = 361001;
    client.update();
    bool ota_cleared = !client.isWemosInOta();
    bool went_offline = !client.isControllerOnline();

    if (in_ota_at_360s && online_at_360s && ota_cleared && went_offline) {
        record("Watchdog: 6-minute (360s) safety timeout triggers", true,
               "At 360000ms stayed in OTA/online; at 360001ms reset OTA and marked offline");
    } else {
        record("Watchdog: 6-minute (360s) safety timeout triggers", false,
               "Watchdog timing did not match exact 360000ms boundary");
    }
}

void test_watchdog_state_lingering_vulnerability() {
    // Adversarial challenge: when the 6-minute watchdog trips, does it cleanly reset
    // wemos_ota_ack_received_, wemos_ota_ack_ok_, and wemos_ota_ip_?
    UartClient client;
    reset_environment(client, 1000);

    // Enter OTA via ACK
    g_mock_millis = 1000;
    Serial.feedLine("{\"type\":\"ack\",\"cmd\":\"enter_ota\",\"ok\":true,\"msg\":\"AP:192.168.4.1\"}");
    client.update();
    assert(client.isWemosInOta());
    assert(client.hasWemosOtaAck());
    assert(client.isWemosOtaAckOk());
    assert(std::string(client.getWemosOtaIp()) == "192.168.4.1");

    // Fast-forward past watchdog (6 minutes + 10s)
    g_mock_millis = 1000 + 360000 + 10000;
    client.update();

    // Watchdog triggered:
    assert(!client.isWemosInOta());
    assert(!client.isControllerOnline());

    bool has_ack = client.hasWemosOtaAck();
    bool ack_ok = client.isWemosOtaAckOk();
    std::string ota_ip = client.getWemosOtaIp();

    // Check what happens when WeMos later sends normal telemetry after rebooting
    g_mock_millis += 5000;
    Serial.feedLine("{\"type\":\"telemetry\",\"ts\":1700001000,\"temp\":25.0,\"temp_tgt\":25.0,\"temp_hyst\":0.3,\"ph\":7.0,\"ec\":400.0,\"relays\":0,\"alarms\":0,\"w_low\":false,\"uptime\":10,\"safe\":false,\"lmode\":1,\"ota_mode\":false}");
    update_until_uart_idle(client);

    bool has_ack_after_normal = client.hasWemosOtaAck();
    std::string ip_after_normal = client.getWemosOtaIp();

    if (has_ack || ack_ok || !ota_ip.empty() || has_ack_after_normal || !ip_after_normal.empty()) {
        record("Adversarial: Watchdog leaves stale ACK and IP flags lingering", false,
               "FLAW FOUND: Watchdog sets wemos_in_ota_=false but omits resetWemosOtaState(); hasWemosOtaAck()=true and IP='192.168.4.1' persist even after normal telemetry resumes!");
    } else {
        record("Adversarial: Watchdog leaves stale ACK and IP flags lingering", true,
               "All OTA state fields were completely wiped on watchdog timeout");
    }
}

void test_millis_wraparound_during_ota() {
    UartClient client;
    // Set millis close to 0xFFFFFFFF (e.g. 4000 ms before rollover)
    uint32_t start = 0xFFFFFF00UL;
    reset_environment(client, start);

    // Initial telemetry
    Serial.feedLine("{\"type\":\"telemetry\",\"ts\":1700000000,\"temp\":25.0,\"temp_tgt\":25.0,\"temp_hyst\":0.3,\"ph\":7.0,\"ec\":400.0,\"relays\":0,\"alarms\":0,\"w_low\":false,\"uptime\":100,\"safe\":false,\"lmode\":1}");
    client.update();

    // Enter OTA
    Serial.feedLine("{\"type\":\"ack\",\"cmd\":\"enter_ota\",\"ok\":true,\"msg\":\"AP:192.168.4.1\"}");
    client.update();
    assert(client.isWemosInOta());

    // Rollover across 0 to 10,000 ms (elapsed = (10000 - 0xFFFFFF00) = 10256 ms)
    g_mock_millis = 10000UL;
    client.update();

    bool in_ota_after_wrap = client.isWemosInOta();
    bool online_after_wrap = client.isControllerOnline();

    // Advance to 360,001 ms elapsed across wrap
    g_mock_millis = start + 360001UL; // In 32-bit unsigned arithmetic: 0xFFFFFF00 + 360001 = 359745
    client.update();

    bool ota_cleared_wrap = !client.isWemosInOta();
    bool went_offline_wrap = !client.isControllerOnline();

    if (in_ota_after_wrap && online_after_wrap && ota_cleared_wrap && went_offline_wrap) {
        record("Watchdog: Millis 32-bit wraparound handling", true,
               "Correctly stays online across 0xFFFFFFFF boundary and trips at 360001ms");
    } else {
        record("Watchdog: Millis 32-bit wraparound handling", false,
               "Wraparound triggered premature or late timeout");
    }
}

// -------------------------------------------------------------
// Test Group 3: Audio Buzzer Muting during OTA
// -------------------------------------------------------------

void test_buzzer_muting_during_ota() {
    UartClient client;
    client.begin(); // sets pinMode
    reset_environment(client, 1000);

    // In normal mode with ALARM_OVERHEAT active (alarms = 1)
    Serial.feedLine("{\"type\":\"telemetry\",\"ts\":1700000000,\"temp\":32.0,\"temp_tgt\":25.0,\"temp_hyst\":0.3,\"ph\":7.0,\"ec\":400.0,\"relays\":0,\"alarms\":1,\"w_low\":false,\"uptime\":100,\"safe\":false,\"lmode\":1}");
    client.update();

    // At t=2000 (1000ms after start), audio beep should trigger HIGH
    g_mock_millis = 2000;
    client.checkAlarmAudio();
    bool beep_started = (g_speaker_pin_state == HIGH);

    // At t=2100 (100ms later), still HIGH
    g_mock_millis = 2100;
    client.checkAlarmAudio();
    bool beep_mid = (g_speaker_pin_state == HIGH);

    // At t=2160 (160ms later >= 150ms), should turn LOW
    g_mock_millis = 2160;
    client.checkAlarmAudio();
    bool beep_ended = (g_speaker_pin_state == LOW);

    // Now at t=3000, trigger beep again (HIGH)
    g_mock_millis = 3000;
    client.checkAlarmAudio();
    bool beep_restarted = (g_speaker_pin_state == HIGH);

    // WHILE BEEPING (HIGH), enter OTA!
    Serial.feedLine("{\"type\":\"ack\",\"cmd\":\"enter_ota\",\"ok\":true,\"msg\":\"AP:192.168.4.1\"}");
    client.update(); // calls processLine and checkAlarmAudio

    bool muted_immediately = (g_speaker_pin_state == LOW);

    // Advance 5 seconds with alarm still present in telemetry while in OTA
    bool stayed_silent = true;
    for (uint32_t t = 3100; t <= 8000; t += 100) {
        g_mock_millis = t;
        client.checkAlarmAudio();
        if (g_speaker_pin_state != LOW) {
            stayed_silent = false;
            break;
        }
    }

    if (beep_started && beep_mid && beep_ended && beep_restarted && muted_immediately && stayed_silent) {
        record("Audio Muting: Buzzer immediately muted and silenced during OTA", true,
               "Active beep instantly cut to LOW; stayed 100% silent over multiple alarm periods");
    } else {
        record("Audio Muting: Buzzer immediately muted and silenced during OTA", false,
               "Buzzer failed to mute or emitted sound while wemos_in_ota_ was true");
    }
}

void test_buzzer_resumes_after_ota_exit() {
    UartClient client;
    client.begin();
    reset_environment(client, 1000);

    // Telemetry in OTA mode with active alarm
    Serial.feedLine("{\"type\":\"telemetry\",\"ts\":1700000000,\"temp\":32.0,\"temp_tgt\":25.0,\"temp_hyst\":0.3,\"ph\":7.0,\"ec\":400.0,\"relays\":0,\"alarms\":1,\"w_low\":false,\"uptime\":100,\"safe\":false,\"lmode\":1,\"ota_mode\":true,\"ota_ip\":\"192.168.4.1\"}");
    update_until_uart_idle(client);
    assert(client.isWemosInOta());
    assert(g_speaker_pin_state == LOW);

    // Advance 2 seconds while in OTA
    g_mock_millis = 3000;
    client.checkAlarmAudio();
    assert(g_speaker_pin_state == LOW);

    // Now WeMos finishes OTA and reboots, sending telemetry with ota_mode = false and alarms = 1
    g_mock_millis = 4000;
    Serial.feedLine("{\"type\":\"telemetry\",\"ts\":1700000010,\"temp\":32.0,\"temp_tgt\":25.0,\"temp_hyst\":0.3,\"ph\":7.0,\"ec\":400.0,\"relays\":0,\"alarms\":1,\"w_low\":false,\"uptime\":5,\"safe\":false,\"lmode\":1,\"ota_mode\":false}");
    update_until_uart_idle(client);

    assert(!client.isWemosInOta());

    // At t=5000 (1000ms elapsed since unmuting), audio beep should trigger HIGH again
    g_mock_millis = 5000;
    client.checkAlarmAudio();
    bool resumed_beep = (g_speaker_pin_state == HIGH);

    if (resumed_beep) {
        record("Audio Muting: Buzzer correctly resumes alerting after OTA exit", true,
               "Intermittent alarm beeping resumed once ota_mode cleared and controller online");
    } else {
        record("Audio Muting: Buzzer correctly resumes alerting after OTA exit", false,
               "Buzzer did not resume after exiting OTA mode");
    }
}

// -------------------------------------------------------------
// Test Group 4: State Transitions on ACK and Telemetry
// -------------------------------------------------------------

void test_ack_ip_parsing_variants() {
    UartClient client;

    // Variant A: STA IP
    reset_environment(client, 1000);
    Serial.feedLine("{\"type\":\"ack\",\"cmd\":\"enter_ota\",\"ok\":true,\"msg\":\"STA:192.168.1.150\"}");
    client.update();
    bool sta_ok = (client.isWemosInOta() && std::string(client.getWemosOtaIp()) == "192.168.1.150");

    // Variant B: AP IP
    reset_environment(client, 2000);
    Serial.feedLine("{\"type\":\"ack\",\"cmd\":\"enter_ota\",\"ok\":true,\"msg\":\"AP:192.168.4.1\"}");
    client.update();
    bool ap_ok = (client.isWemosInOta() && std::string(client.getWemosOtaIp()) == "192.168.4.1");

    // Variant C: Direct IP (no prefix)
    reset_environment(client, 3000);
    Serial.feedLine("{\"type\":\"ack\",\"cmd\":\"enter_ota\",\"ok\":true,\"msg\":\"10.0.0.55\"}");
    client.update();
    bool direct_ok = (client.isWemosInOta() && std::string(client.getWemosOtaIp()) == "10.0.0.55");

    // Variant D: Negative ACK (ok == false)
    reset_environment(client, 4000);
    Serial.feedLine("{\"type\":\"ack\",\"cmd\":\"enter_ota\",\"ok\":false,\"msg\":\"Flash memory error\"}");
    client.update();
    bool neg_ack_ok = (!client.isWemosInOta() && client.hasWemosOtaAck() && !client.isWemosOtaAckOk());

    if (sta_ok && ap_ok && direct_ok && neg_ack_ok) {
        record("State Transitions: ACK IP parsing and negative ACK handling", true,
               "Parsed 'STA:192.168.1.150', 'AP:192.168.4.1', '10.0.0.55', and handled ok:false");
    } else {
        record("State Transitions: ACK IP parsing and negative ACK handling", false,
               "Failed on one or more ACK parsing variants");
    }
}

void test_telemetry_ota_ip_update() {
    UartClient client;
    reset_environment(client, 1000);

    // Initial ACK gave fallback AP IP
    Serial.feedLine("{\"type\":\"ack\",\"cmd\":\"enter_ota\",\"ok\":true,\"msg\":\"AP:192.168.4.1\"}");
    client.update();
    assert(std::string(client.getWemosOtaIp()) == "192.168.4.1");

    // Telemetry subsequently arrives with assigned STA IP
    Serial.feedLine("{\"type\":\"telemetry\",\"ts\":1700000000,\"temp\":25.0,\"temp_tgt\":25.0,\"temp_hyst\":0.3,\"ph\":7.0,\"ec\":400.0,\"relays\":0,\"alarms\":0,\"w_low\":false,\"uptime\":100,\"safe\":false,\"lmode\":1,\"ota_mode\":true,\"ota_ip\":\"192.168.1.222\"}");
    update_until_uart_idle(client);

    bool updated_ip = (std::string(client.getWemosOtaIp()) == "192.168.1.222");

    // Subsequent telemetry with empty IP string does not erase stored IP
    Serial.feedLine("{\"type\":\"telemetry\",\"ts\":1700000001,\"temp\":25.0,\"temp_tgt\":25.0,\"temp_hyst\":0.3,\"ph\":7.0,\"ec\":400.0,\"relays\":0,\"alarms\":0,\"w_low\":false,\"uptime\":101,\"safe\":false,\"lmode\":1,\"ota_mode\":true,\"ota_ip\":\"\"}");
    update_until_uart_idle(client);

    bool preserved_ip = (std::string(client.getWemosOtaIp()) == "192.168.1.222");

    if (updated_ip && preserved_ip) {
        record("State Transitions: Telemetry updates IP and guards against empty string overwrite", true,
               "Updated to 192.168.1.222 and retained it when empty ota_ip arrived");
    } else {
        record("State Transitions: Telemetry updates IP and guards against empty string overwrite", false,
               "Failed to update or preserved IP was overwritten by empty string");
    }
}

void test_rapid_stream_stress_and_buffer_overflow() {
    UartClient client;
    reset_environment(client, 1000);

    // Feed a massive stream of mixed valid and malformed data:
    // 1. Valid ACK
    // 2. Giant junk line (> 600 chars to test 512-byte rx_buf_ overflow protection)
    // 3. Valid telemetry with ota_mode=true
    // 4. Broken JSON fragments
    // 5. Valid telemetry with ota_mode=false (clean exit)

    std::string giant_line(600, 'X');
    giant_line += "\n";

    std::string test_stream =
        "{\"type\":\"ack\",\"cmd\":\"enter_ota\",\"ok\":true,\"msg\":\"AP:192.168.4.1\"}\n" +
        giant_line +
        "{\"type\":\"telemetry\",\"ts\":1700000000,\"temp\":25.0,\"temp_tgt\":25.0,\"temp_hyst\":0.3,\"ph\":7.0,\"ec\":400.0,\"relays\":0,\"alarms\":0,\"w_low\":false,\"uptime\":100,\"safe\":false,\"lmode\":1,\"ota_mode\":true,\"ota_ip\":\"192.168.4.1\"}\n" +
        "{corrupted json line 12345\n" +
        "\n\r\n" +
        "{\"type\":\"telemetry\",\"ts\":1700000010,\"temp\":25.0,\"temp_tgt\":25.0,\"temp_hyst\":0.3,\"ph\":7.0,\"ec\":400.0,\"relays\":0,\"alarms\":0,\"w_low\":false,\"uptime\":10,\"safe\":false,\"lmode\":1,\"ota_mode\":false}\n";

    Serial.feed(test_stream);
    update_until_uart_idle(client);

    // Once the bounded scheduler passes drain the burst, the final clean frame
    // must win over malformed and oversized predecessors.
    bool in_ota = client.isWemosInOta();
    bool has_ack = client.hasWemosOtaAck();
    std::string ip = client.getWemosOtaIp();

    if (!in_ota && !has_ack && ip.empty()) {
        record("Stress Test: Rapid stream, buffer overflow, and corrupted JSON resilience", true,
               "Handled 600-byte line without crash; parsed valid frames before and after junk");
    } else {
        record("Stress Test: Rapid stream, buffer overflow, and corrupted JSON resilience", false,
               "State corruption after processing rapid stream with oversized line");
    }
}

// -------------------------------------------------------------
// Test Group 5: Clean Recovery when WeMos Exits OTA
// -------------------------------------------------------------

void test_clean_recovery_on_normal_telemetry() {
    UartClient client;
    reset_environment(client, 1000);

    // 1. Enter OTA
    Serial.feedLine("{\"type\":\"ack\",\"cmd\":\"enter_ota\",\"ok\":true,\"msg\":\"AP:192.168.4.1\"}");
    client.update();
    assert(client.isWemosInOta());
    assert(client.hasWemosOtaAck());
    assert(std::string(client.getWemosOtaIp()) == "192.168.4.1");

    // Advance 30 seconds
    g_mock_millis = 31000;
    assert(client.getWemosOtaElapsedMs() >= 30000);

    // Normal commands are currently blocked
    assert(!client.sendSetRelay(0, true));

    // 2. WeMos reboots and sends normal telemetry frame
    Serial.feedLine("{\"type\":\"telemetry\",\"ts\":1700000030,\"temp\":25.2,\"temp_tgt\":25.0,\"temp_hyst\":0.3,\"ph\":7.05,\"ec\":410.0,\"relays\":1,\"alarms\":0,\"w_low\":false,\"uptime\":2,\"safe\":false,\"lmode\":1,\"ota_mode\":false}");
    update_until_uart_idle(client);

    bool ota_cleared = !client.isWemosInOta();
    bool ack_cleared = !client.hasWemosOtaAck();
    bool ack_ok_cleared = !client.isWemosOtaAckOk();
    bool ip_cleared = (client.getWemosOtaIp()[0] == '\0');
    bool elapsed_cleared = (client.getWemosOtaElapsedMs() == 0);
    bool online = client.isControllerOnline();

    // Normal command must now succeed
    Serial.clearTx();
    bool relay_unblocked = client.sendSetRelay(0, true);
    bool tx_emitted = (Serial.tx_lines.size() == 1);

    if (ota_cleared && ack_cleared && ack_ok_cleared && ip_cleared &&
        elapsed_cleared && online && relay_unblocked && tx_emitted) {
        record("Clean Recovery: Complete state reset when WeMos exits OTA", true,
               "All flags, IP, and elapsed timer cleared; normal TX commands unblocked immediately");
    } else {
        record("Clean Recovery: Complete state reset when WeMos exits OTA", false,
               "Incomplete state reset or commands still blocked after WeMos returned to normal mode");
    }
}

void test_non_ip_ack_message() {
    UartClient client;
    reset_environment(client, 1000);

    // If WeMos replies with a status text rather than an IP, e.g. "OTA Ready" or "OK"
    Serial.feedLine("{\"type\":\"ack\",\"cmd\":\"enter_ota\",\"ok\":true,\"msg\":\"OTA Ready\"}");
    client.update();

    std::string extracted_ip = client.getWemosOtaIp();
    if (extracted_ip == "OTA") {
        record("Adversarial: Non-IP ACK message extracted as bogus IP", false,
               "LOGIC FLAW: extractOtaIp parses non-IP status string 'OTA Ready' into bogus IP 'OTA' because it lacks IPv4 syntax validation");
    } else {
        record("Adversarial: Non-IP ACK message extracted as bogus IP", true,
               "Non-IP status message correctly rejected as IP");
    }
}

void test_unacknowledged_enter_ota_reverts_to_3s_timeout() {
    UartClient client;
    reset_environment(client, 1000);

    // Initially online
    Serial.feedLine("{\"type\":\"telemetry\",\"ts\":1700000000,\"temp\":25.0,\"temp_tgt\":25.0,\"temp_hyst\":0.3,\"ph\":7.0,\"ec\":400.0,\"relays\":0,\"alarms\":0,\"w_low\":false,\"uptime\":100,\"safe\":false,\"lmode\":1}");
    client.update();
    assert(client.isControllerOnline());

    // User triggers enter_ota, but WeMos never responds (e.g. cable disconnected)
    g_mock_millis = 1050;
    bool sent = client.sendEnterOta();
    assert(sent);
    assert(!client.isWemosInOta()); // Should not immediately be in OTA

    // At t=4051 (3001ms after last telemetry at 1050)
    g_mock_millis = 4051;
    client.update();

    if (!client.isControllerOnline() && !client.isWemosInOta()) {
        record("Timeout: Unacknowledged enter_ota falls back to standard 3s offline timeout", true,
               "Offline timeout fired normally when enter_ota was lost/unanswered");
    } else {
        record("Timeout: Unacknowledged enter_ota falls back to standard 3s offline timeout", false,
               "Did not go offline after lost enter_ota");
    }
}

void test_other_ack_during_ota_does_not_disturb_state() {
    UartClient client;
    reset_environment(client, 1000);

    // Enter OTA
    Serial.feedLine("{\"type\":\"ack\",\"cmd\":\"enter_ota\",\"ok\":true,\"msg\":\"AP:192.168.4.1\"}");
    client.update();
    assert(client.isWemosInOta());

    // Another ACK arrives (e.g. delayed ACK for set_relay)
    g_mock_millis = 2000;
    Serial.feedLine("{\"type\":\"ack\",\"cmd\":\"set_relay\",\"ok\":true,\"msg\":\"ok\"}");
    client.update();

    bool still_in_ota = client.isWemosInOta();
    std::string ip = client.getWemosOtaIp();
    bool has_new_ack = client.hasNewAck();

    if (still_in_ota && ip == "192.168.4.1" && has_new_ack) {
        record("State Transitions: Non-OTA ACK received during OTA does not corrupt OTA state", true,
               "OTA state and IP preserved while handling other ACK");
    } else {
        record("State Transitions: Non-OTA ACK received during OTA does not corrupt OTA state", false,
               "OTA state was disturbed by non-OTA ACK");
    }
}

// -------------------------------------------------------------
// Main Runner
// -------------------------------------------------------------

int main() {
    std::cout << "=======================================================\n";
    std::cout << "  EMPIRICAL CHALLENGER: CYD UartClient OTA Stress Suite\n";
    std::cout << "=======================================================\n\n";

    std::cout << "--- 1. Command TX Suppression during OTA ---\n";
    test_tx_suppression_normal_vs_ota();

    std::cout << "\n--- 2. Offline Timeout (3s) Pausing & 6-Minute Watchdog ---\n";
    test_offline_timeout_normal_mode();
    test_offline_timeout_paused_during_ota();
    test_watchdog_timeout_at_6_minutes();
    test_watchdog_state_lingering_vulnerability();
    test_unacknowledged_enter_ota_reverts_to_3s_timeout();
    test_millis_wraparound_during_ota();

    std::cout << "\n--- 3. Audio Buzzer Muting during OTA ---\n";
    test_buzzer_muting_during_ota();
    test_buzzer_resumes_after_ota_exit();

    std::cout << "\n--- 4. State Transitions on ACK and Telemetry ---\n";
    test_ack_ip_parsing_variants();
    test_non_ip_ack_message();
    test_telemetry_ota_ip_update();
    test_other_ack_during_ota_does_not_disturb_state();
    test_rapid_stream_stress_and_buffer_overflow();

    std::cout << "\n--- 5. Clean Recovery when WeMos Exits OTA ---\n";
    test_clean_recovery_on_normal_telemetry();

    int failed = 0;
    for (const auto &r : g_results) {
        if (!r.passed) ++failed;
    }

    std::cout << "\n=======================================================\n";
    std::cout << "  SUMMARY: " << g_results.size() << " tests, "
              << (g_results.size() - failed) << " passed, "
              << failed << " failed\n";
    std::cout << "=======================================================\n";

    return failed;
}
