#include <Arduino.h>
#include <driver/adc.h>
#include <esp_log.h>
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
#include <WiFi.h>
#include <esp_bt.h>

bool wifi_connected = false;
int wifi_rssi = 0;
bool wifi_ota_active = false;

int clock_hour = 0;
int clock_minute = 0;
int clock_second = 0;
int clock_day = 1;
int clock_month = 1;
int clock_year = 2026;
uint32_t last_clock_tick_ms = 0U;

void main_reset_clock_tick(uint32_t now_ms) {
    last_clock_tick_ms = now_ms;
}

namespace {

int discard_esp_idf_log(const char *, va_list) {
    return 0;
}

constexpr uint32_t UI_LOOP_PERIOD_MS = 2U;
constexpr uint32_t UI_MUTEX_TIMEOUT_MS = 50U;
// HTTP remains serviced every loop. During browser activity the CYD is locked
// to its compact Wi-Fi page, so two LVGL passes per second preserve the
// explicit disconnect action while leaving CPU time for TCP and SD transfers.
constexpr uint32_t WEB_LVGL_SERVICE_INTERVAL_MS = 500U;
constexpr uint32_t OTA_HEALTH_SERVICE_MS = 500U;
constexpr uint32_t OTA_HEARTBEAT_MAX_AGE_MS = 3000U;

uint32_t last_lvgl_tick_ms = 0U;
uint32_t last_lvgl_handler_ms = 0U;
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
    AQUARIUM_LOG_PRINTF("RAM: %s free=%lu min_free=%lu\n",
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

    // The web-focus screen deliberately remains static.  Updating the status
    // bar would invalidate the display once per second even though that data
    // is available through the browser, stealing a display transaction from
    // HTTP and SD-card streaming.
    if (gui_app_is_web_focus_active()) {
        return;
    }

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
        frame.mcp_state,
        frame.water_level_max_valid);
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

    // Keep LVGL's monotonic clock precise, but do not schedule a complete
    // render/input pass at 500 Hz while the web client owns the full panel.
    // The compact Wi-Fi screen still accepts the explicit disconnect action
    // at 2 FPS, while the CPU, SPI and GUI mutex remain available to HTTP.
    const bool web_focus_active = gui_app_is_web_focus_active();
    if (web_focus_active &&
        last_lvgl_handler_ms != 0U &&
        static_cast<uint32_t>(now_ms - last_lvgl_handler_ms) <
            WEB_LVGL_SERVICE_INTERVAL_MS) {
        return;
    }

    // A local redraw is optional during an active web session.  Never let it
    // wait 50 ms for a competing transfer; consume this 500 ms UI slot and
    // keep the event loop responsive for the browser instead.
    const uint32_t lock_timeout_ms = web_focus_active ? 5U : UI_MUTEX_TIMEOUT_MS;
    if (!gui_app_lock(lock_timeout_ms)) {
        if (web_focus_active) {
            last_lvgl_handler_ms = now_ms;
        }
        return;
    }
    hal_display_loop_cb();
    gui_app_service_web_focus(now_ms);
    lv_timer_handler();
    last_lvgl_handler_ms = now_ms;
    gui_app_unlock();
}

} // namespace

void setup() {
    // UART0 belongs exclusively to the CYD <-> AquaBox NDJSON transport.
    // Redirect ESP-IDF diagnostics before Wi-Fi/mDNS starts: an mDNS error log
    // from the TCP/IP task otherwise enters newlib's lazy stdout lock and can
    // abort the controller while also corrupting a live UART frame.
    esp_log_set_vprintf(discard_esp_idf_log);
    esp_log_level_set("*", ESP_LOG_NONE);
    AQUARIUM_LOG_PRINTLN("--- ESP32 CYD AQUARIUM / PRODUKCYJNY RUNTIME ---");

    // Stos radiowy krótkiego zasięgu nie jest częścią produktu. Zwalniamy jego
    // rezerwę BTDM do ogólnej sterty DRAM, zanim wystartują Wi-Fi i LVGL.
    const esp_err_t bt_rel_err = esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);
    AQUARIUM_LOG_PRINTF("BOOT: unused BTDM memory release: %s\n",
                  bt_rel_err == ESP_OK ? "OK (+66KB DRAM)" : "pominięto");

    // Wczesna inicjalizacja sterownika Wi-Fi w niesfragmentowanej pamięci DMA.
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    log_ram_checkpoint("after_early_wifi");

    runtime_safety_initialize();
    runtime_safety_register_current_task(RuntimeSafetyTask::Ui);
    ota_guard_initialize(millis());

    if (!gui_app_sync_init()) {
        AQUARIUM_LOG_PRINTLN("FATAL: nie można utworzyć blokady GUI.");
        runtime_safety_fail_and_restart(
            RuntimeFaultReason::GuiInitializationFailed);
    }
    if (!events_init()) {
        AQUARIUM_LOG_PRINTLN("EVENTS: inicjalizacja kolejek nie powiodła się.");
    }

    // Przekaźniki otrzymują stan bezpieczny przed inicjalizacją grafiki i SD.
    runtime_controller_prepare_hardware();
    service_ui_startup_watchdog();
    log_ram_checkpoint("after_safe_hardware");

    lv_init();
    hal_display_init();
    hal_display_set_brightness(80);
    service_ui_startup_watchdog();
    log_ram_checkpoint("after_display");

    if (hal_sd_init()) {
        const uint32_t splash_start_ms = millis();
        constexpr uint32_t SPLASH_MIN_DISPLAY_MS = 5000U;

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

        // Gwarantuj pełny czas wyświetlania ekranu powitalnego (min. 5 s).
        const uint32_t splash_elapsed_ms = millis() - splash_start_ms;
        if (splash_elapsed_ms < SPLASH_MIN_DISPLAY_MS) {
            vTaskDelay(pdMS_TO_TICKS(SPLASH_MIN_DISPLAY_MS - splash_elapsed_ms));
        }
    }

    service_ui_startup_watchdog();
    gui_app_init();
    service_ui_startup_watchdog();
    if (!remote_alarm_relay_initialize()) {
        AQUARIUM_LOG_PRINTLN(
            "REMOTE_ALARM: relay task unavailable.");
    }
    service_ui_startup_watchdog();
    log_ram_checkpoint("after_gui");

    const uint32_t now_ms = millis();
    last_lvgl_tick_ms = now_ms;
    last_lvgl_handler_ms = now_ms;
    last_clock_tick_ms = now_ms;
    last_status_bar_ms = now_ms;
    last_ota_health_ms = now_ms;
    if (!runtime_controller_start()) {
        AQUARIUM_LOG_PRINTLN("FATAL: zadanie I/O Core 0 nie wystartowało.");
        runtime_safety_fail_and_restart(
            RuntimeFaultReason::IoTaskStartFailed);
    }
    if (!runtime_safety_start_supervisor()) {
        AQUARIUM_LOG_PRINTLN("FATAL: safety supervisor did not start.");
        runtime_safety_fail_and_restart(
            RuntimeFaultReason::IoTaskStartFailed);
    }
    AQUARIUM_LOG_PRINTF("SYSTEM: UI Core=%d, I/O Core=0, gotowy.\n", xPortGetCoreID());
}

void loop() {
    const uint32_t now_ms = millis();
    runtime_safety_heartbeat(
        RuntimeSafetyTask::Ui, now_ms, ESP.getFreeHeap());

    gui_app_service_web_portal();
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
