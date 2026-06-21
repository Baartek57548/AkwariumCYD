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
static unsigned long last_gui_update_time = 0;

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
    lv_timer_handler();

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

        int ldr_value = -1;
        const bool ldr_valid = !dev_mode;
        if (ldr_valid) {
            ldr_value = analogRead(LDR_PIN);
        }
        gui_app_update_ldr(ldr_value, ldr_valid);

        const bool adc_present = !dev_mode && hal_adc_is_present();
        int16_t ph_raw = 0;
        int16_t ec_raw = 0;
        bool ph_adc_ok = false;
        bool ec_adc_ok = false;
        float ph_voltage = 0.0f;
        float ec_voltage = 0.0f;
        if (adc_present) {
            ph_adc_ok = hal_adc_read_raw(HwConfig::ADC_PH, &ph_raw);
            if (ph_adc_ok) {
                ph_voltage = ads_raw_to_voltage(ph_raw);
            }
            ec_adc_ok = hal_adc_read_raw(HwConfig::ADC_EC, &ec_raw);
            if (ec_adc_ok) {
                ec_voltage = ads_raw_to_voltage(ec_raw);
            }
        }

        uint16_t mcp_state = 0;
        const bool mcp_present = hal_mcp_is_present();
        const bool mcp_ok = mcp_present && hal_mcp_read_all(&mcp_state);

        const float temp_to_send = NAN;
        const float ph_to_send = ph_adc_ok ? (4.0f + (ph_voltage / 4.096f) * 6.0f) : NAN;

        // Aktualizacja interfejsu graficznego (Zegar, Temperatura, pH, RAM)
        gui_update_metrics(temp_to_send, ph_to_send, free_heap, time_str);
        publish_sensor_sample(SensorId::Temp, temp_to_send, isfinite(temp_to_send));
        publish_sensor_sample(SensorId::Ph, ph_to_send, isfinite(ph_to_send));
        publish_sensor_sample(SensorId::Ec, ec_voltage, ec_adc_ok);
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
