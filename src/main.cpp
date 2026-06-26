#include <Arduino.h>
#include <driver/adc.h>
#include <lvgl.h>
#include "config.h"
#include "hal_display.h"
#include "hal_sd.h"
#include "hal_mcp23017.h"
#include "hal_adc.h"
#include "gui_app.h"
#include "events.h"
#include <ArduinoOTA.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>

// Definicja pinu, do którego podłączony jest fotorezystor (LDR)
const int LDR_PIN = HwConfig::LDR_PIN;

// Zmienne do obsługi czasu i okresowości (non-blocking millis)
static unsigned long last_lvgl_tick = 0;
static unsigned long last_lvgl_handler_time = 0;
static unsigned long last_gui_update_time = 0;
constexpr unsigned long WEB_FOCUS_LVGL_HANDLER_INTERVAL_MS = 20UL;

// Lokalny zegar programowy uzywany do czasu podlaczenia RTC/NTP.
static uint32_t uptime_seconds = 0;
int clock_hour = 20;
int clock_minute = 30;
int clock_second = 0;
int clock_day = 31;
int clock_month = 5;
int clock_year = 2026;

bool wifi_connected = false;
int wifi_rssi = 0;
bool wifi_ota_active = false;

static void log_ram_checkpoint(const char *stage)
{
    Serial.printf("RAM: %s free=%lu min_free=%lu\n",
                  stage != nullptr ? stage : "unknown",
                  static_cast<unsigned long>(ESP.getFreeHeap()),
                  static_cast<unsigned long>(ESP.getMinFreeHeap()));
}

static float ads_raw_to_voltage(int16_t raw)
{
    return (static_cast<float>(raw) * 4.096f) / 32768.0f;
}

static int16_t voltage_to_ads_raw(float voltage)
{
    const float clamped = constrain(voltage, 0.0f, 4.095f);
    return static_cast<int16_t>(lroundf((clamped * 32767.0f) / 4.096f));
}

struct DevTelemetrySample
{
    float temperatureC;
    float ph;
    float phVoltage;
    int16_t phRaw;
    float ecConductivity;
    float ecVoltage;
    int16_t ecRaw;
    int ldr;
    uint16_t mcpState;
};

static uint32_t dev_telemetry_noise = 0x5A17C0DEUL;

static float dev_unit_noise()
{
    dev_telemetry_noise = dev_telemetry_noise * 1664525UL + 1013904223UL;
    const uint16_t raw = static_cast<uint16_t>((dev_telemetry_noise >> 16) & 0x03FFU);
    return (static_cast<float>(raw) / 511.5f) - 1.0f;
}

static DevTelemetrySample build_dev_telemetry_sample(uint32_t now_ms)
{
    const float t = static_cast<float>(now_ms) * 0.001f;
    DevTelemetrySample sample = {};

    sample.temperatureC = 25.0f +
                          0.42f * sinf(t / 43.0f) +
                          0.16f * sinf(t / 9.5f) +
                          0.04f * dev_unit_noise();
    sample.ph = 6.86f +
                0.10f * sinf(t / 57.0f) +
                0.035f * sinf(t / 13.0f) +
                0.012f * dev_unit_noise();
    sample.ecConductivity = 440.0f +
                            34.0f * sinf(t / 61.0f) +
                            9.0f * sinf(t / 17.0f) +
                            4.0f * dev_unit_noise();

    sample.ph = constrain(sample.ph, 6.55f, 7.25f);
    sample.ecConductivity = constrain(sample.ecConductivity, 360.0f, 540.0f);
    sample.phVoltage = constrain(((sample.ph - 4.0f) / 6.0f) * 4.096f, 0.0f, 4.095f);
    sample.ecVoltage = constrain(sample.ecConductivity / 1000.0f, 0.0f, 4.095f);
    sample.phRaw = voltage_to_ads_raw(sample.phVoltage);
    sample.ecRaw = voltage_to_ads_raw(sample.ecVoltage);

    const float ldr_wave = 760.0f + 420.0f * sinf(t / 73.0f) + 95.0f * sinf(t / 11.0f);
    sample.ldr = constrain(static_cast<int>(lroundf(ldr_wave + 18.0f * dev_unit_noise())), 120, 1550);

    const uint16_t water_level_mask = static_cast<uint16_t>(1U << static_cast<uint8_t>(HwConfig::CH_WATER_LEVEL));
    const uint16_t flow_mask = static_cast<uint16_t>(1U << static_cast<uint8_t>(HwConfig::CH_FLOW_PULSE));
    sample.mcpState = water_level_mask;
    if ((now_ms / 5000UL) % 2UL == 0UL) {
        sample.mcpState |= flow_mask;
    }

    return sample;
}

static void publish_sensor_sample(SensorId id, float value, bool valid)
{
    SensorSample sample = {};
    sample.id = id;
    sample.value = value;
    sample.timestampMs = millis();
    sample.valid = valid;
    events_publish_sample(sample);
}

static void drain_event_samples()
{
    SensorSample sample = {};
    while (events_poll_sample(sample)) {
    }
}

static void run_i2c_startup_diagnostics()
{
    const bool mcp_ok = hal_mcp_init();
    Serial.printf("HAL: MCP23017 address 0x%02X present: %s\n",
                  HwConfig::MCP23017_ADDR,
                  mcp_ok ? "yes" : "no");

    if (mcp_ok) {
        uint16_t mcp_state = 0;
        if (hal_mcp_read_all(&mcp_state)) {
            Serial.printf("HAL: MCP23017 logical state mask: 0x%04X\n", mcp_state);
        } else {
            Serial.println("HAL: MCP23017 state read failed.");
        }
    }

    const bool adc_ok = hal_adc_init();
    Serial.printf("HAL: ADS1115 address 0x%02X present: %s\n",
                  HwConfig::ADS1115_ADDR,
                  adc_ok ? "yes" : "no");

    if (adc_ok) {
        for (uint8_t ch = HwConfig::ADC_PH; ch <= HwConfig::ADC_SPARE; ++ch) {
            int16_t raw = 0;
            if (hal_adc_read_raw(ch, &raw)) {
                Serial.printf("HAL: ADS1115 A%u raw: %d\n", static_cast<unsigned>(ch), raw);
            } else {
                Serial.printf("HAL: ADS1115 A%u read failed.\n", static_cast<unsigned>(ch));
            }
        }
    }
}

// Lokalny zegar programowy dziala bez RTC/NTP, ale nie generuje danych sensorow.
static void update_local_clock() {
    clock_second++;
    uptime_seconds++;
    if (clock_second >= 60) {
        clock_second = 0;
        clock_minute++;
        if (clock_minute >= 60) {
            clock_minute = 0;
            clock_hour++;
            if (clock_hour >= 24) {
                clock_hour = 0;
                clock_day++;
                int days_in_month = 31;
                if (clock_month == 4 || clock_month == 6 || clock_month == 9 || clock_month == 11) {
                    days_in_month = 30;
                } else if (clock_month == 2) {
                    bool is_leap = (clock_year % 4 == 0 && (clock_year % 100 != 0 || clock_year % 400 == 0));
                    days_in_month = is_leap ? 29 : 28;
                }
                if (clock_day > days_in_month) {
                    clock_day = 1;
                    clock_month++;
                    if (clock_month > 12) {
                        clock_month = 1;
                        clock_year++;
                    }
                }
            }
        }
    }


}

void setup() {
    // Initialize serial port
    Serial.begin(115200);
    delay(100);
    Serial.println("\n--- STARTING ESP32 CYD DASHBOARD (STABLE RUNTIME) ---");
    if (!events_init()) {
        Serial.println("EVENTS: initialization failed.");
    }
    log_ram_checkpoint("boot_start");

    // Set up LDR input using standard Arduino ADC APIs (ADC1 / GPIO 34)
    pinMode(LDR_PIN, INPUT);
    analogRead(LDR_PIN); // Force core oneshot driver to claim GPIO 34 as analog channel
    analogSetPinAttenuation(LDR_PIN, ADC_11db);
    
    Serial.println("System: LDR pin 34 configured.");
    // Step 1: Initialize the LVGL graphics library
    lv_init();
    log_ram_checkpoint("after_lvgl_init");

    // Step 2: Initialize LovyanGFX display and XPT2046 touch via HAL
    hal_display_init();
    Serial.println("System: Graphics layer initialization (HAL Display) completed.");
    log_ram_checkpoint("after_display_init");

    if (hal_sd_init()) {
        const bool splash_ok = hal_display_play_rgb565_sequence(
            HwConfig::SdCard::WELCOME_FRAME_PATTERN,
            HwConfig::SdCard::WELCOME_FRAME_COUNT,
            HwConfig::SdCard::WELCOME_WIDTH,
            HwConfig::SdCard::WELCOME_HEIGHT,
            HwConfig::SdCard::WELCOME_FRAME_RATE_FPS);
        if (!splash_ok) {
            hal_display_draw_rgb565_file(HwConfig::SdCard::WELCOME_POSTER_PATH,
                                         HwConfig::SdCard::WELCOME_WIDTH,
                                         HwConfig::SdCard::WELCOME_HEIGHT);
        }
    }

    run_i2c_startup_diagnostics();

    // Step 3: Initialize user interface (GUI)
    gui_app_init();
    Serial.println("System: Graphical User Interface (GUI) creation completed.");
    log_ram_checkpoint("after_gui_init");

    // Initialize tick timer for LVGL reference
    last_lvgl_tick = millis();
    Serial.println("System: Device is ready for stable operation.");
}

void loop() {
    unsigned long current_time = millis();

    // --- WĄTEK LVGL (Precyzyjne i nieblokujące taktowanie) ---
    // Obliczanie rzeczywistego czasu jaki upłynął i przekazanie go do rdzenia LVGL
    unsigned long elapsed = current_time - last_lvgl_tick;
    if (elapsed > 0) {
        lv_tick_inc(elapsed);
        last_lvgl_tick = current_time;
    }
    
    // Obsługa asynchronicznego zwalniania buforów wyświetlacza LovyanGFX
    hal_display_loop_cb();
    
    // Płynna i nieblokowana pętla obsługi grafiki oraz dotyku
    const bool web_focus_active = gui_app_is_web_focus_active();
    if (!web_focus_active ||
        current_time - last_lvgl_handler_time >= WEB_FOCUS_LVGL_HANDLER_INTERVAL_MS) {
        lv_timer_handler();
        last_lvgl_handler_time = current_time;
    }

    if (wifi_ota_active) {
        ArduinoOTA.handle();
    }
    gui_app_handle_ota_portal();

    // --- WĄTEK AKTUALIZACJI METRYK (Nieblokujący - Co 1000ms) ---
    if (current_time - last_gui_update_time >= 1000) {
        last_gui_update_time = current_time;

        // Aktualizacja lokalnego zegara programowego bez generowania probek sensorow
        update_local_clock();

        // Przygotowanie ciągu tekstowego czasu w formacie HH:MM:SS
        char time_str[9];
        snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d", clock_hour, clock_minute, clock_second);

        // Pobranie wolnej pamięci RAM systemu ESP32
        uint32_t free_heap = ESP.getFreeHeap();

        const bool dev_mode = gui_app_is_dev_mode();

        DevTelemetrySample dev_sample = {};
        if (dev_mode) {
            dev_sample = build_dev_telemetry_sample(current_time);
        }

        int ldr_value = dev_mode ? dev_sample.ldr : -1;
        const bool ldr_valid = true;
        if (!dev_mode) {
            ldr_value = analogRead(LDR_PIN);
        }
        gui_app_update_ldr(ldr_value, true);

        const bool adc_present = dev_mode || hal_adc_is_present();
        int16_t ph_raw = 0;
        int16_t ec_raw = 0;
        bool ph_adc_ok = false;
        bool ec_adc_ok = false;
        float ph_voltage = 0.0f;
        float ec_voltage = 0.0f;
        float ec_publish_value = NAN;
        if (dev_mode) {
            ph_raw = dev_sample.phRaw;
            ec_raw = dev_sample.ecRaw;
            ph_voltage = dev_sample.phVoltage;
            ec_voltage = dev_sample.ecVoltage;
            ec_publish_value = dev_sample.ecConductivity;
            ph_adc_ok = true;
            ec_adc_ok = true;
        } else if (adc_present) {
            ph_adc_ok = hal_adc_read_raw(HwConfig::ADC_PH, &ph_raw);
            if (ph_adc_ok) {
                ph_voltage = ads_raw_to_voltage(ph_raw);
            }
            ec_adc_ok = hal_adc_read_raw(HwConfig::ADC_EC, &ec_raw);
            if (ec_adc_ok) {
                ec_voltage = ads_raw_to_voltage(ec_raw);
                ec_publish_value = ec_voltage;
            }
        }

        uint16_t mcp_state = 0;
        bool mcp_present = false;
        bool mcp_ok = false;
        if (dev_mode) {
            mcp_state = dev_sample.mcpState;
            mcp_present = true;
            mcp_ok = true;
        } else {
            mcp_present = hal_mcp_is_present();
            mcp_ok = mcp_present && hal_mcp_read_all(&mcp_state);
        }

        const float temp_to_send = dev_mode ? dev_sample.temperatureC : NAN;
        const float ph_to_send = dev_mode ? dev_sample.ph : (ph_adc_ok ? (4.0f + (ph_voltage / 4.096f) * 6.0f) : NAN);

        // Aktualizacja interfejsu graficznego (Zegar, Temperatura, pH, RAM)
        gui_update_metrics(temp_to_send, ph_to_send, free_heap, time_str);
        publish_sensor_sample(SensorId::Temp, temp_to_send, isfinite(temp_to_send));
        publish_sensor_sample(SensorId::Ph, ph_to_send, isfinite(ph_to_send));
        publish_sensor_sample(SensorId::Ec, ec_publish_value, ec_adc_ok && isfinite(ec_publish_value));
        publish_sensor_sample(SensorId::Ldr, ldr_valid ? static_cast<float>(ldr_value) : NAN, ldr_valid);
        publish_sensor_sample(SensorId::Heap, static_cast<float>(free_heap), true);
        drain_event_samples();

        // Aktualizacja czasu pracy (uptime) w zakładce System
        gui_app_update_system_info(free_heap, uptime_seconds);
        gui_app_update_sensor_debug(ldr_value,
                                    adc_present,
                                    ph_adc_ok,
                                    ph_raw,
                                    ph_voltage,
                                    ec_adc_ok,
                                    ec_raw,
                                    ec_voltage,
                                    mcp_present,
                                    mcp_ok,
                                    mcp_state);

        // Update Wi-Fi status in the header (0=OFF, 1=STA, 2=AP)
        gui_app_update_wifi(wifi_ota_active ? 2 : (wifi_connected ? 1 : 0), wifi_rssi);

    }

    // Short delay (5ms) to prevent watchdog starvation in FreeRTOS
    delay(5);
}
