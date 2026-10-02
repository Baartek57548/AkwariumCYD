#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>
#include "wemos_config.h"
#include "hardware_i2c.h"
#include "temp_control.h"
#include "automation_engine.h"
#include "storage_manager.h"
#include "uart_protocol.h"

// =============================================================================
// Zmienne globalne trybu normalnego
// =============================================================================
namespace {

char serial_rx_buf[512];
size_t serial_rx_idx = 0;

uint32_t last_telemetry_ms = 0;
uint32_t last_adc_read_ms = 0;
uint32_t led_off_deadline_ms = 0;

float current_ph = 7.0f;
float current_ec = 0.0f;
ControllerConfig current_config;
bool safe_mode_active = false;

// =============================================================================
// Stan trybu serwisowego OTA
// =============================================================================
bool ota_mode_active = false;
uint32_t ota_mode_start_ms = 0;
char ota_ssid_buf[33] = {};
char ota_pass_buf[65] = {};
char ota_ip_buf[16] = {};

ESP8266WebServer *ota_web_server = nullptr; // alokowany tylko w trybie OTA

// =============================================================================
// Deklaracje forward
// =============================================================================
void send_telemetry();
void process_serial_line(const char *line);
void enter_ota_mode(const char *ssid_hint = nullptr, const char *pass_hint = nullptr);
void ota_loop();
void force_relays_safe();
void led_blink(uint8_t times, uint32_t on_ms, uint32_t off_ms);

// =============================================================================
// Wymuszone bezpieczne stany przekaźników przed OTA
// =============================================================================
void force_relays_safe()
{
    // Wyłącz wszystkie kanały krytyczne: grzałka, CO2, karmnik
    g_hardware_i2c.setRelay(WemosConfig::RELAY_CH_HEATER, false);
    g_hardware_i2c.setRelay(WemosConfig::RELAY_CH_CO2, false);
    g_hardware_i2c.setRelay(WemosConfig::RELAY_CH_FEEDER, false);
    // Opcjonalnie można też wyłączyć światła i filtr — decyzja projektowa
    // g_hardware_i2c.setRelay(WemosConfig::RELAY_CH_LIGHT_A, false);
    // g_hardware_i2c.setRelay(WemosConfig::RELAY_CH_LIGHT_B, false);
    safe_mode_active = true;
}

// =============================================================================
// Sygnał LED: n mrugnięć
// =============================================================================
void led_blink(uint8_t times, uint32_t on_ms, uint32_t off_ms)
{
    for (uint8_t i = 0; i < times; ++i) {
        digitalWrite(WemosConfig::PIN_LED_STATUS, LOW);  // ON
        delay(on_ms);
        digitalWrite(WemosConfig::PIN_LED_STATUS, HIGH); // OFF
        delay(off_ms);
    }
}

// =============================================================================
// Telemetria WeMos -> CYD
// =============================================================================
void send_telemetry()
{
    AquaProtocol::AquariumTelemetry t;
    t.timestamp = g_hardware_i2c.getEpochTime();
    t.temperature = g_temp_control.getTemperature();
    t.temp_target = g_temp_control.getTargetTemp();
    t.temp_hysteresis = g_temp_control.getHysteresis();
    t.ph = current_ph;
    t.ec = current_ec;
    t.relay_states = g_hardware_i2c.getRelayStates();

    uint16_t alarms = g_temp_control.getAlarms();
    if (g_hardware_i2c.isWaterLevelLow()) {
        alarms |= AquaProtocol::ALARM_WATER_LOW;
    }
    if (!g_hardware_i2c.isMcpPresent()) {
        alarms |= AquaProtocol::ALARM_I2C_FAIL;
    }
    t.alarms = alarms;

    t.water_level_low = g_hardware_i2c.isWaterLevelLow();
    t.water_level_max_reached = g_hardware_i2c.isWaterLevelMaxReached();
    t.water_level_max_valid = g_hardware_i2c.areWaterLevelInputsValid();
    t.uptime_seconds = millis() / 1000UL;
    t.safe_mode = safe_mode_active;
    t.light_mode = g_automation_engine.getAquaelLightMode();
    t.ota_mode = ota_mode_active;
    if (ota_mode_active && ota_ip_buf[0] != '\0') {
        strncpy(t.ota_ip, ota_ip_buf, sizeof(t.ota_ip) - 1);
        t.ota_ip[sizeof(t.ota_ip) - 1] = '\0';
    } else {
        t.ota_ip[0] = '\0';
    }

    String line = AquaProtocol::serializeTelemetry(t);
    Serial.println(line);

    // Mrugnięcie diodą statusu (Active Low)
    digitalWrite(WemosConfig::PIN_LED_STATUS, LOW);
    led_off_deadline_ms = millis() + 20UL;
}

// =============================================================================
// Obsługa komend UART (normalny tryb pracy)
// =============================================================================
void process_serial_line(const char *line)
{
    AquaProtocol::AquariumMessage msg;
    if (!AquaProtocol::parseMessage(line, msg)) {
        return;
    }

    if (msg.type == AquaProtocol::MessageType::TimeSync) {
        g_hardware_i2c.setEpochTime(msg.sync_time_epoch);
        Serial.println(AquaProtocol::serializeAck("time_sync", true));
        return;
    }

    if (msg.type != AquaProtocol::MessageType::Command) {
        return;
    }

    switch (msg.command.type) {
    case AquaProtocol::CommandType::SetRelay:
        g_automation_engine.setManualOverride(msg.command.channel, msg.command.state);
        Serial.println(AquaProtocol::serializeAck("set_relay", true));
        send_telemetry(); // Natychmiastowa telemetria po zmianie stanu
        break;

    case AquaProtocol::CommandType::SetTempTarget:
        g_temp_control.setTarget(msg.command.temp_target, msg.command.temp_hysteresis);
        current_config.target_temp = msg.command.temp_target;
        current_config.hysteresis = msg.command.temp_hysteresis;
        g_storage_manager.saveConfig(current_config);
        Serial.println(AquaProtocol::serializeAck("set_temp", true));
        send_telemetry();
        break;

    case AquaProtocol::CommandType::SetSchedule:
        if (msg.command.schedule_dev < 4) {
            DeviceSchedule ds;
            ds.enabled = msg.command.schedule_enabled;
            ds.on_hour = msg.command.on_hour;
            ds.on_minute = msg.command.on_minute;
            ds.off_hour = msg.command.off_hour;
            ds.off_minute = msg.command.off_minute;
            g_automation_engine.setSchedule(msg.command.schedule_dev, ds);
            current_config.schedules[msg.command.schedule_dev] = ds;
            g_storage_manager.saveConfig(current_config);
            Serial.println(AquaProtocol::serializeAck("set_sched", true));
        } else {
            Serial.println(AquaProtocol::serializeAck("set_sched", false, "Invalid device ID"));
        }
        break;

    case AquaProtocol::CommandType::FeedTrigger:
        g_automation_engine.triggerFeeder();
        Serial.println(AquaProtocol::serializeAck("feed", true));
        send_telemetry();
        break;

    case AquaProtocol::CommandType::SyncLights:
        g_automation_engine.triggerAquaelSync();
        Serial.println(AquaProtocol::serializeAck("sync_lights", true));
        send_telemetry();
        break;

    case AquaProtocol::CommandType::SyncTime:
        g_hardware_i2c.setEpochTime(msg.command.epoch_time);
        Serial.println(AquaProtocol::serializeAck("sync_time", true));
        break;

    case AquaProtocol::CommandType::ClearAlarm:
        g_temp_control.clearAlarms();
        Serial.println(AquaProtocol::serializeAck("clear_alarm", true));
        send_telemetry();
        break;

    case AquaProtocol::CommandType::GetStatus:
        send_telemetry();
        break;

    case AquaProtocol::CommandType::SetSafeMode:
        safe_mode_active = msg.command.state;
        if (safe_mode_active) {
            // W trybie bezpiecznym wyłączamy grzałkę, CO2 i oświetlenie
            g_hardware_i2c.setRelay(WemosConfig::RELAY_CH_HEATER, false);
            g_hardware_i2c.setRelay(WemosConfig::RELAY_CH_CO2, false);
            g_hardware_i2c.setRelay(WemosConfig::RELAY_CH_LIGHT_A, false);
            g_hardware_i2c.setRelay(WemosConfig::RELAY_CH_LIGHT_B, false);
        }
        Serial.println(AquaProtocol::serializeAck("safe_mode", true));
        send_telemetry();
        break;

    case AquaProtocol::CommandType::EnterOta:
        // Wyzwolenie trybu serwisowego OTA przez UART z CYD
        enter_ota_mode(msg.command.ota_ssid, msg.command.ota_pass);
        break;

    default:
        Serial.println(AquaProtocol::serializeAck("unknown", false));
        break;
    }
}

// =============================================================================
// Wejście w tryb serwisowy OTA
// ssid_hint / pass_hint — opcjonalne dane STA z komendy UART (mogą być puste)
// =============================================================================
void enter_ota_mode(const char *ssid_hint, const char *pass_hint)
{
    if (ota_mode_active) {
        // Już w trybie OTA — wyślij ponownie ACK z aktualnym IP
        char ack_msg[32];
        if (ota_ip_buf[0] != '\0') {
            snprintf(ack_msg, sizeof(ack_msg), "STA:%s", ota_ip_buf);
        } else {
            snprintf(ack_msg, sizeof(ack_msg), "AP:192.168.4.1");
        }
        Serial.println(AquaProtocol::serializeAck("enter_ota", true, ack_msg));
        return;
    }

    // 1. Bezpieczne wyłączenie przekaźników krytycznych
    force_relays_safe();
    led_blink(3, 150, 100);

    // 2. Wyciszenie telemetrii UART (od teraz blokujemy pętlę normalną)
    ota_mode_active = true;
    ota_mode_start_ms = millis();
    ota_ip_buf[0] = '\0';

    // 3. Skopiuj dane STA z komendy UART lub z konfiguracji domyślnej
    const char *sta_ssid = (ssid_hint && ssid_hint[0] != '\0')
                            ? ssid_hint
                            : WemosConfig::OTA_STA_SSID;
    const char *sta_pass = (pass_hint && pass_hint[0] != '\0')
                            ? pass_hint
                            : WemosConfig::OTA_STA_PASSWORD;

    strncpy(ota_ssid_buf, sta_ssid, sizeof(ota_ssid_buf) - 1);
    ota_ssid_buf[sizeof(ota_ssid_buf) - 1] = '\0';
    strncpy(ota_pass_buf, sta_pass, sizeof(ota_pass_buf) - 1);
    ota_pass_buf[sizeof(ota_pass_buf) - 1] = '\0';

    // 4. Przywróć WiFi ze snu i spróbuj STA
    WiFi.forceSleepWake();
    delay(10);
    WiFi.mode(WIFI_STA);
    WiFi.setSleepMode(WIFI_NONE_SLEEP);

    bool sta_ok = false;
    if (ota_ssid_buf[0] != '\0') {
        WiFi.begin(ota_ssid_buf, ota_pass_buf[0] != '\0' ? ota_pass_buf : nullptr);

        uint32_t connect_start = millis();
        while (millis() - connect_start < WemosConfig::OTA_STA_CONNECT_TIMEOUT_MS) {
            delay(200);
            if (WiFi.status() == WL_CONNECTED) {
                sta_ok = true;
                break;
            }
            // Szybkie mrugnięcie LED podczas łączenia
            digitalWrite(WemosConfig::PIN_LED_STATUS,
                        (millis() / 200) % 2 == 0 ? LOW : HIGH);
        }
    }

    IPAddress local_ip;
    if (sta_ok) {
        local_ip = WiFi.localIP();
        snprintf(ota_ip_buf, sizeof(ota_ip_buf), "%d.%d.%d.%d",
                 local_ip[0], local_ip[1], local_ip[2], local_ip[3]);
    } else {
        // Fallback: własny punkt dostępowy
        WiFi.mode(WIFI_AP);
        delay(50);
        WiFi.softAPConfig(IPAddress(192, 168, 4, 1),
                          IPAddress(192, 168, 4, 1),
                          IPAddress(255, 255, 255, 0));
        WiFi.softAP(WemosConfig::OTA_AP_SSID, WemosConfig::OTA_AP_PASSWORD);
        local_ip = WiFi.softAPIP();
        snprintf(ota_ip_buf, sizeof(ota_ip_buf), "192.168.4.1");
    }
    digitalWrite(WemosConfig::PIN_LED_STATUS, HIGH); // LED OFF

    // 5. Wyślij ACK do CYD zanim wyciszymy Serial (OTA może je zdezorganizować)
    {
        char ack_msg[32];
        if (sta_ok) {
            snprintf(ack_msg, sizeof(ack_msg), "STA:%s", ota_ip_buf);
        } else {
            snprintf(ack_msg, sizeof(ack_msg), "AP:192.168.4.1");
        }
        Serial.println(AquaProtocol::serializeAck("enter_ota", true, ack_msg));
        Serial.flush();
    }

    // 6. Konfiguracja ArduinoOTA
    ArduinoOTA.setHostname("aquabox-ota");
    ArduinoOTA.setPassword(WemosConfig::OTA_AP_PASSWORD);
    ArduinoOTA.onStart([]() {
        // Wyłącz diodę podczas flashowania
        digitalWrite(WemosConfig::PIN_LED_STATUS, HIGH);
    });
    ArduinoOTA.onEnd([]() {
        // 3 długie mrugnięcia = sukces
        led_blink(3, 400, 150);
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        // Szybkie mrugnięcie postępu co ~10%
        static unsigned int last_pct = 0;
        const unsigned int pct = total > 0U ? (progress * 100U / total) : 0U;
        if (pct / 10 != last_pct / 10) {
            last_pct = pct;
            digitalWrite(WemosConfig::PIN_LED_STATUS, LOW);
            delay(30);
            digitalWrite(WemosConfig::PIN_LED_STATUS, HIGH);
        }
    });
    ArduinoOTA.onError([](ota_error_t err) {
        // 5 szybkich mrugnięć = błąd
        led_blink(5, 80, 80);
    });
    ArduinoOTA.begin();

    // 7. Web OTA — prosty formularz HTTP do wgrania pliku .bin
    ota_web_server = new ESP8266WebServer(80);

    ota_web_server->on("/", HTTP_GET, []() {
        const char *html =
            "<!DOCTYPE html><html><head>"
            "<meta charset='utf-8'>"
            "<title>AquaBox OTA</title>"
            "<style>body{font-family:monospace;background:#0f172a;color:#e2e8f0;padding:2em;}"
            "h2{color:#14b8a6;}form{margin-top:1em;}"
            "input[type=file]{margin:.5em 0;display:block;}"
            "button{background:#14b8a6;color:#000;border:none;padding:.5em 1.5em;"
            "border-radius:4px;cursor:pointer;font-size:1em;}"
            ".warn{color:#f59e0b;margin-top:1em;font-size:.9em;}"
            ".info{color:#64748b;font-size:.85em;}"
            "</style></head><body>"
            "<h2>&#128032; AquaBox OTA Update</h2>"
            "<p class='info'>Wgraj plik <b>wemos_d1_uno.bin</b> z katalogu "
            "<b>.pio/build/wemos_d1_uno/</b></p>"
            "<form method='POST' action='/update' enctype='multipart/form-data'>"
            "<input type='file' name='firmware' accept='.bin'>"
            "<br><button type='submit'>&#9650; Wgraj firmware</button>"
            "</form>"
            "<p class='warn'>&#9888; Po wgraniu WeMos uruchomi si&#281; automatycznie.<br>"
            "Watchdog: restart za 5 minut je&#380;li nic nie wgrasz.</p>"
            "</body></html>";
        ota_web_server->send(200, "text/html", html);
    });

    ota_web_server->on("/update", HTTP_POST,
        []() {
            // Callback po zakończeniu uploadu
            ota_web_server->sendHeader("Connection", "close");
            bool ok = !Update.hasError();
            ota_web_server->send(200, "text/html",
                ok ? "<html><body style='background:#0f172a;color:#14b8a6;font-family:monospace;"
                     "padding:2em'><h2>&#10003; Sukces! Restart za 3s...</h2></body></html>"
                   : "<html><body style='background:#0f172a;color:#ef4444;font-family:monospace;"
                     "padding:2em'><h2>&#10007; Blad aktualizacji. Sprobuj ponownie.</h2></body></html>");
            if (ok) {
                delay(3000);
                ESP.restart();
            }
        },
        []() {
            // Streaming upload handler
            HTTPUpload &upload = ota_web_server->upload();
            if (upload.status == UPLOAD_FILE_START) {
                Serial.setDebugOutput(true);
                WiFiUDP::stopAll();
                uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
                if (!Update.begin(maxSketchSpace)) {
                    Update.printError(Serial);
                }
            } else if (upload.status == UPLOAD_FILE_WRITE) {
                if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                    Update.printError(Serial);
                }
                // Mrugnięcie LED podczas uploadu
                digitalWrite(WemosConfig::PIN_LED_STATUS,
                             (millis() / 250) % 2 == 0 ? LOW : HIGH);
            } else if (upload.status == UPLOAD_FILE_END) {
                if (Update.end(true)) {
                    led_blink(3, 300, 100);
                } else {
                    led_blink(5, 80, 80);
                    Update.printError(Serial);
                }
                Serial.setDebugOutput(false);
            }
        }
    );

    ota_web_server->on("/status", HTTP_GET, []() {
        char json[128];
        snprintf(json, sizeof(json),
                 "{\"mode\":\"ota\",\"ip\":\"%s\",\"uptime\":%lu,\"watchdog_ms\":%lu}",
                 ota_ip_buf,
                 millis() / 1000UL,
                 WemosConfig::OTA_WATCHDOG_MS / 1000UL);
        ota_web_server->send(200, "application/json", json);
    });

    ota_web_server->begin();
}

// =============================================================================
// Pętla trybu serwisowego OTA
// Blokuje normalną pracę do momentu resetu lub timeoutu watchdoga
// =============================================================================
void ota_loop()
{
    uint32_t now_ms = millis();

    // Obsługa HTTP i ArduinoOTA
    if (ota_web_server != nullptr) {
        ota_web_server->handleClient();
    }
    ArduinoOTA.handle();

    // Mrugnięcie LED co 1 sekundę = "jestem żywy w trybie OTA"
    static uint32_t last_led_toggle_ms = 0;
    if (now_ms - last_led_toggle_ms >= 1000UL) {
        last_led_toggle_ms = now_ms;
        static bool led_state = false;
        led_state = !led_state;
        digitalWrite(WemosConfig::PIN_LED_STATUS, led_state ? LOW : HIGH);
    }

    // Watchdog — restart do normalnej pracy po OTA_WATCHDOG_MS bez aktywności
    if (now_ms - ota_mode_start_ms >= WemosConfig::OTA_WATCHDOG_MS) {
        // Wyślij informację o powrocie (UART może już nie działać, ale próbujemy)
        Serial.println(F("{\"type\":\"event\",\"msg\":\"OTA watchdog timeout, restarting\"}"));
        Serial.flush();
        delay(200);
        ESP.restart();
    }
}

} // namespace

// =============================================================================
// SETUP
// =============================================================================
void setup()
{
    // Dioda statusu na WeMos D1 Uno (GPIO2 / D4)
    pinMode(WemosConfig::PIN_LED_STATUS, OUTPUT);
    digitalWrite(WemosConfig::PIN_LED_STATUS, HIGH); // OFF

    // Konfiguracja pinu przycisku serwisowego OTA (GPIO0/D3, active-low, pull-up)
    pinMode(WemosConfig::PIN_OTA_BUTTON, INPUT_PULLUP);

    // Krótkie opóźnienie stabilizacji przy starcie przed odczytem pinu
    delay(50);
    const bool ota_button_held = (digitalRead(WemosConfig::PIN_OTA_BUTTON) == LOW);

    // Wyłączenie Wi-Fi dla maksymalnej stabilności, zerowego zużycia prądu RF i zwolnienia pamięci RAM
    // (tylko gdy NIE wchodzimy w tryb OTA)
    if (!ota_button_held) {
        WiFi.mode(WIFI_OFF);
        WiFi.forceSleepBegin();
    }

    // Inicjalizacja portu szeregowego UART0
    Serial.begin(WemosConfig::UART_BAUD_RATE);
    Serial.println();
    Serial.println(F("{\"type\":\"event\",\"msg\":\"Wemos Aquarium Controller Booting...\"}"));

    // Inicjalizacja pamięci trwałej EEPROM
    g_storage_manager.begin();
    g_storage_manager.loadConfig(current_config);

    // Inicjalizacja magistrali I2C (MCP23017, ADS1115, DS3231)
    g_hardware_i2c.begin();

    // Inicjalizacja magistrali 1-Wire i termometru DS18B20
    g_temp_control.begin();
    g_temp_control.setTarget(current_config.target_temp, current_config.hysteresis);

    // Inicjalizacja silnika harmonogramów
    for (uint8_t i = 0; i < 4; ++i) {
        g_automation_engine.setSchedule(i, current_config.schedules[i]);
    }
    g_automation_engine.begin();

    Serial.println(F("{\"type\":\"event\",\"msg\":\"Wemos Aquarium Controller Ready\"}"));

    // Sprawdzenie czy przycisk serwisowy był wciśnięty przy starcie
    if (ota_button_held) {
        Serial.println(F("{\"type\":\"event\",\"msg\":\"OTA button held at boot - entering service mode\"}"));
        // Poczekaj chwilę na init I2C zanim wyłączymy przekaźniki
        delay(200);
        // Wejdź w tryb OTA — użyj domyślnych danych STA z wemos_config.h
        enter_ota_mode(nullptr, nullptr);
    }
}

// =============================================================================
// LOOP
// =============================================================================
void loop()
{
    // -------------------------------------------------------------------------
    // Tryb serwisowy OTA — blokuje normalną pracę
    // -------------------------------------------------------------------------
    if (ota_mode_active) {
        ota_loop();
        return;
    }

    uint32_t now_ms = millis();

    // 1. Odbiór komend z UART
    while (Serial.available() > 0) {
        char c = static_cast<char>(Serial.read());
        if (c == '\n' || c == '\r') {
            if (serial_rx_idx > 0) {
                serial_rx_buf[serial_rx_idx] = '\0';
                process_serial_line(serial_rx_buf);
                serial_rx_idx = 0;
            }
        } else if (serial_rx_idx < sizeof(serial_rx_buf) - 1) {
            serial_rx_buf[serial_rx_idx++] = c;
        } else {
            // Przepełnienie bufora
            serial_rx_idx = 0;
        }
    }

    // 2. Cykliczne gaszenie diody LED po mrugnięciu
    if (led_off_deadline_ms > 0 && now_ms >= led_off_deadline_ms) {
        digitalWrite(WemosConfig::PIN_LED_STATUS, HIGH); // OFF
        led_off_deadline_ms = 0;
    }

    // 3. Odczyt wejść cyfrowych (pływak i wejścia rezerwowe)
    uint8_t inputs = 0;
    g_hardware_i2c.readDigitalInputs(inputs);

    // 4. Pomiary analogowe ADS1115 (pH, EC) co 2 sekundy
    if (now_ms - last_adc_read_ms >= 2000UL) {
        last_adc_read_ms = now_ms;
        current_ph = g_hardware_i2c.readPh(current_config.ph_cal_offset, current_config.ph_cal_slope);
        current_ec = g_hardware_i2c.readEc();
    }

    // 5. Aktualizacja termostatu (jeśli nie w trybie awaryjnym)
    if (!safe_mode_active) {
        g_temp_control.update(now_ms);
    }

    // 6. Aktualizacja harmonogramów (jeśli nie w trybie awaryjnym)
    if (!safe_mode_active) {
        uint32_t current_epoch = g_hardware_i2c.getEpochTime();
        g_automation_engine.update(current_epoch, now_ms);
    }

    // 7. Cykliczne wysyłanie telemetrii co 1 sekundę
    if (now_ms - last_telemetry_ms >= WemosConfig::TELEMETRY_INTERVAL_MS) {
        last_telemetry_ms = now_ms;
        send_telemetry();
    }
}
