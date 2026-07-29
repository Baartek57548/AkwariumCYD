#include <Arduino.h>
#include <driver/adc.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>

#include "config.h"
#include "events.h"
#include "gui_app.h"
#include "hal_display.h"
#include "hal_sd.h"
#include "ota_guard.h"
#include "remote_alarm_relay.h"
#include "runtime_controller.h"
#include "runtime_safety.h"

bool wifi_connected = false;
int wifi_rssi = 0;
bool wifi_ota_active = false;

int clock_hour = 20;
int clock_minute = 30;
int clock_second = 0;
int clock_day = 31;
int clock_month = 5;
int clock_year = 2026;

namespace {

constexpr uint32_t UI_LOOP_PERIOD_MS = 2U;
constexpr uint32_t UI_MUTEX_TIMEOUT_MS = 20U;
constexpr uint32_t OTA_HEALTH_SERVICE_MS = 500U;
constexpr uint32_t OTA_HEARTBEAT_MAX_AGE_MS = 3000U;

uint32_t last_lvgl_tick_ms = 0U;
uint32_t last_clock_tick_ms = 0U;
uint32_t last_status_bar_ms = 0U;
uint32_t last_ota_health_ms = 0U;
uint32_t last_applied_telemetry_ms = 0U;
RuntimeTelemetry pending_telemetry = {};
bool pending_telemetry_valid = false;

uint32_t system_uptime_seconds() {
    return static_cast<uint32_t>(
        static_cast<uint64_t>(esp_timer_get_time()) / 1000000ULL);
}

void log_ram_checkpoint(const char *stage) {
    Serial.printf("RAM: %s free=%lu min_free=%lu\n",
                  stage != nullptr ? stage : "unknown",
                  static_cast<unsigned long>(ESP.getFreeHeap()),
                  static_cast<unsigned long>(ESP.getMinFreeHeap()));
}

void service_ui_startup_watchdog() {
    runtime_safety_heartbeat(
        RuntimeSafetyTask::Ui,
        millis(),
        ESP.getFreeHeap());
}

bool is_leap_year(int year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

uint8_t days_in_month(int month, int year) {
    static constexpr uint8_t DAYS[] = {
        31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U
    };
    if (month < 1 || month > 12) {
        return 31U;
    }
    if (month == 2 && is_leap_year(year)) {
        return 29U;
    }
    return DAYS[month - 1];
}

void advance_local_clock(uint32_t elapsed_seconds) {
    while (elapsed_seconds-- > 0U) {
        ++clock_second;
        if (clock_second < 60) {
            continue;
        }
        clock_second = 0;
        ++clock_minute;
        if (clock_minute < 60) {
            continue;
        }
        clock_minute = 0;
        ++clock_hour;
        if (clock_hour < 24) {
            continue;
        }
        clock_hour = 0;
        ++clock_day;
        if (clock_day <= days_in_month(clock_month, clock_year)) {
            continue;
        }
        clock_day = 1;
        ++clock_month;
        if (clock_month <= 12) {
            continue;
        }
        clock_month = 1;
        ++clock_year;
    }
}

void service_clock(uint32_t now_ms) {
    const uint32_t elapsed_ms = static_cast<uint32_t>(now_ms - last_clock_tick_ms);
    const uint32_t elapsed_seconds = elapsed_ms / 1000UL;
    if (elapsed_seconds == 0U) {
        return;
    }
    // NTP is serviced on Core 0 and updates the same clock fields. The shared
    // GUI mutex keeps an NTP correction and the local tick atomic.
    if (!gui_app_lock(5U)) {
        return;
    }
    last_clock_tick_ms += elapsed_seconds * 1000UL;
    advance_local_clock(elapsed_seconds);
    gui_app_unlock();
}

void service_status_bar(uint32_t now_ms) {
    if (static_cast<uint32_t>(now_ms - last_status_bar_ms) < 1000U) {
        return;
    }
    last_status_bar_ms = now_ms;
    if (gui_app_lock(5U)) {
        gui_app_update_status_bar(system_uptime_seconds());
        gui_app_unlock();
    }
}

void drain_sensor_events() {
    SensorSample sample = {};
    while (events_poll_sample(sample)) {
    }
}

bool apply_telemetry(const RuntimeTelemetry &frame) {
    if (!gui_app_lock(UI_MUTEX_TIMEOUT_MS)) {
        return false;
    }

    char time_text[9];
    snprintf(time_text, sizeof(time_text), "%02d:%02d:%02d",
             constrain(clock_hour, 0, 23),
             constrain(clock_minute, 0, 59),
             constrain(clock_second, 0, 59));

    gui_app_update_ldr(frame.ldr_value, frame.ldr_valid);
    gui_app_update_sensor_debug(
        frame.ldr_value,
        frame.temperature_present,
        frame.temperature_stale,
        frame.temperature_age_ms,
        frame.temperature_error_count,
        frame.adc_present,
        frame.ph_valid,
        frame.ph_raw,
        frame.ph_voltage,
        frame.ph_value,
        frame.ec_valid,
        frame.ec_raw,
        frame.ec_voltage,
        frame.ec_value,
        frame.mcp_present,
        frame.mcp_valid,
        frame.mcp_state);
    gui_update_metrics(
        frame.temperature_valid ? frame.temperature_c : NAN,
        frame.ph_valid ? frame.ph_value : NAN,
        frame.free_heap_bytes,
        time_text);
    gui_app_update_system_info(frame.free_heap_bytes, frame.uptime_seconds);
    gui_app_update_wifi(frame.wifi_state, frame.wifi_rssi);

    gui_app_unlock();
    drain_sensor_events();
    return true;
}

void service_lvgl(uint32_t now_ms) {
    const uint32_t elapsed_ms = static_cast<uint32_t>(now_ms - last_lvgl_tick_ms);
    if (elapsed_ms > 0U) {
        lv_tick_inc(elapsed_ms);
        last_lvgl_tick_ms = now_ms;
    }

    if (!gui_app_lock(UI_MUTEX_TIMEOUT_MS)) {
        return;
    }
    hal_display_loop_cb();
    lv_timer_handler();
    gui_app_unlock();
}

} // namespace

void setup() {
    Serial.begin(HwConfig::UartConsole::BAUD);
    Serial.println();
    Serial.println("--- ESP32 CYD AQUARIUM / PRODUKCYJNY RUNTIME ---");
    runtime_safety_initialize();
    runtime_safety_register_current_task(RuntimeSafetyTask::Ui);
    ota_guard_initialize(millis());

    if (!gui_app_sync_init()) {
        Serial.println("FATAL: nie można utworzyć blokady GUI.");
        runtime_safety_fail_and_restart(
            RuntimeFaultReason::GuiInitializationFailed);
    }
    if (!events_init()) {
        Serial.println("EVENTS: inicjalizacja kolejek nie powiodła się.");
    }

    // Przekaźniki otrzymują stan bezpieczny przed inicjalizacją grafiki i SD.
    runtime_controller_prepare_hardware();
    service_ui_startup_watchdog();
    log_ram_checkpoint("after_safe_hardware");

    lv_init();
    hal_display_init();
    service_ui_startup_watchdog();
    log_ram_checkpoint("after_display");

    if (hal_sd_init()) {
        const bool animation_ok = hal_display_play_rgb565_sequence(
            HwConfig::SdCard::WELCOME_FRAME_PATTERN,
            HwConfig::SdCard::WELCOME_FRAME_COUNT,
            HwConfig::SdCard::WELCOME_WIDTH,
            HwConfig::SdCard::WELCOME_HEIGHT,
            HwConfig::SdCard::WELCOME_FRAME_RATE_FPS);
        if (!animation_ok) {
            hal_display_draw_rgb565_file(
                HwConfig::SdCard::WELCOME_POSTER_PATH,
                HwConfig::SdCard::WELCOME_WIDTH,
                HwConfig::SdCard::WELCOME_HEIGHT);
        }
    }

    service_ui_startup_watchdog();
    gui_app_init();
    service_ui_startup_watchdog();
    if (!remote_alarm_relay_initialize()) {
        Serial.println(
            "REMOTE_ALARM: relay task unavailable.");
    }
    service_ui_startup_watchdog();
    log_ram_checkpoint("after_gui");

    const uint32_t now_ms = millis();
    last_lvgl_tick_ms = now_ms;
    last_clock_tick_ms = now_ms;
    last_status_bar_ms = now_ms;
    last_ota_health_ms = now_ms;
    if (!runtime_controller_start()) {
        Serial.println("FATAL: zadanie I/O Core 0 nie wystartowało.");
        runtime_safety_fail_and_restart(
            RuntimeFaultReason::IoTaskStartFailed);
    }
    if (!runtime_safety_start_supervisor()) {
        Serial.println("FATAL: safety supervisor did not start.");
        runtime_safety_fail_and_restart(
            RuntimeFaultReason::IoTaskStartFailed);
    }
    Serial.printf("SYSTEM: UI Core=%d, I/O Core=0, gotowy.\n", xPortGetCoreID());
}

void loop() {
    const uint32_t now_ms = millis();
    runtime_safety_heartbeat(
        RuntimeSafetyTask::Ui, now_ms, ESP.getFreeHeap());
    service_clock(now_ms);
    service_status_bar(now_ms);
    service_lvgl(now_ms);

    RuntimeTelemetry newest_frame = {};
    if (runtime_controller_take_latest(&newest_frame)) {
        pending_telemetry = newest_frame;
        pending_telemetry_valid = true;
    }
    if (pending_telemetry_valid && apply_telemetry(pending_telemetry)) {
        pending_telemetry_valid = false;
        last_applied_telemetry_ms = now_ms;
    }

    if (static_cast<uint32_t>(now_ms - last_ota_health_ms) >=
        OTA_HEALTH_SERVICE_MS) {
        last_ota_health_ms = now_ms;
        const bool telemetry_applied =
            last_applied_telemetry_ms != 0U &&
            static_cast<uint32_t>(
                now_ms - last_applied_telemetry_ms) <=
                OTA_HEARTBEAT_MAX_AGE_MS;
        const bool runtime_ready =
            telemetry_applied &&
            runtime_controller_is_healthy(
                now_ms, OTA_HEARTBEAT_MAX_AGE_MS) &&
            gui_app_runtime_ready() &&
            gui_app_ota_health_ready();
        ota_guard_service(now_ms, runtime_ready, ESP.getFreeHeap());
    }

    vTaskDelay(pdMS_TO_TICKS(UI_LOOP_PERIOD_MS));
}
