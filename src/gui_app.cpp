#include "gui_app.h"

#include "ble_controller.h"
#include "config.h"
#include "device_credentials.h"
#include "events.h"
#include "espnow_link.h"
#include "firmware_trust_anchor.h"
#include "hal_adc.h"
#include "hal_display.h"
#include "hal_i2c_bus.h"
#include "hal_mcp23017.h"
#include "hal_onewire_bus.h"
#include "hal_sd.h"
#include "admin_session.h"
#include "alarm_event_queue.h"
#include "aquael_light_controller.h"
#include "aquarium_automation.h"
#include "aquarium_schedule.h"
#include "control_modes.h"
#include "dev_simulator.h"
#include "idempotency_ledger.h"
#include "ota_guard.h"
#include "remote_alarm_relay.h"
#include "secure_ota.h"
#include "sensor_calibration_store.h"
#include "runtime_safety.h"
#include "web_activity_tracker.h"
#include "wifi_retry_policy.h"
#include "wifi_credential_store.h"

#include <Preferences.h>
#include <SD.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <lvgl.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <ctype.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <new>
#include <stdarg.h>

extern bool wifi_connected;
extern int wifi_rssi;
extern bool wifi_ota_active;
extern int clock_hour;
extern int clock_minute;
extern int clock_second;
extern int clock_day;
extern int clock_month;
extern int clock_year;

static int get_weekday(int d, int m, int y) {
    static int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y -= 1;
    return (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
}

namespace {

StaticSemaphore_t gui_mutex_storage;
SemaphoreHandle_t gui_mutex = nullptr;
bool gui_ready = false;

class GuiMutexGuard {
public:
    explicit GuiMutexGuard(uint32_t timeout_ms)
        : locked_(gui_mutex != nullptr &&
                  xSemaphoreTakeRecursive(gui_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
    }

    ~GuiMutexGuard() {
        if (locked_) {
            xSemaphoreGiveRecursive(gui_mutex);
        }
    }

    bool locked() const {
        return locked_;
    }

    GuiMutexGuard(const GuiMutexGuard &) = delete;
    GuiMutexGuard &operator=(const GuiMutexGuard &) = delete;

private:
    bool locked_;
};

static void label_set_text_if_changed(lv_obj_t *label, const char *text) {
    if (label == nullptr) {
        return;
    }
    const char *safe_text = text != nullptr ? text : "";
    const char *current = lv_label_get_text(label);
    if (current == nullptr || strcmp(current, safe_text) != 0) {
        lv_label_set_text(label, safe_text);
    }
}

static void label_set_text_fmt_if_changed(lv_obj_t *label, const char *format, ...) {
    if (label == nullptr || format == nullptr) {
        return;
    }

    char text[256];
    va_list args;
    va_start(args, format);
    const int written = vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    if (written < 0) {
        return;
    }
    text[sizeof(text) - 1U] = '\0';
    label_set_text_if_changed(label, text);
}

static lv_obj_t *create_cyd_switch(lv_obj_t *parent) {
    lv_obj_t *sw = lv_switch_create(parent);
    if (sw != nullptr) {
        // Wizualny przełącznik może pozostać kompaktowy, ale jego pole dotyku
        // spełnia minimum 40 px na małym ekranie 2,8".
        lv_obj_set_ext_click_area(sw, 10);
    }
    return sw;
}

#define lv_label_set_text label_set_text_if_changed
#define lv_label_set_text_fmt label_set_text_fmt_if_changed
#define lv_switch_create create_cyd_switch

constexpr uint32_t UI_CONFIG_MAGIC = 0x43594441UL;
constexpr uint16_t UI_CONFIG_VERSION = 17;
constexpr uint16_t UI_CONFIG_VERSION_DUAL_AQUAEL_DN = 16;
constexpr uint16_t UI_CONFIG_VERSION_FACTORY_SCHEDULE_BASE = 15;
constexpr uint16_t UI_CONFIG_VERSION_AQUAEL_DN = 14;
constexpr uint16_t UI_CONFIG_VERSION_DEV_NO_SENSORS = 13;
constexpr uint16_t UI_CONFIG_VERSION_LDR_DEFAULT_OFF = 12;
#ifndef AQUARIUM_FORCE_DEVELOPER_MODE
#define AQUARIUM_FORCE_DEVELOPER_MODE 0
#endif
constexpr bool FORCE_DEVELOPER_MODE = AQUARIUM_FORCE_DEVELOPER_MODE != 0;
constexpr uint8_t MINUTE_STEP = 5;
constexpr uint8_t PAGE_COUNT = 5;
constexpr uint8_t TEMP_HISTORY_POINTS = 32;
constexpr uint32_t HISTORY_ARCHIVE_INTERVAL_MS = 60000UL;
constexpr uint64_t HISTORY_ARCHIVE_MIN_FREE_BYTES = 1024ULL * 1024ULL;
constexpr uint16_t HISTORY_ARCHIVE_COMPACT_DROP_RECORDS = 256;
constexpr size_t HISTORY_ARCHIVE_COPY_BUFFER_BYTES = 128;
constexpr uint32_t HISTORY_ARCHIVE_MAGIC = 0x31485141UL; // AQH1
constexpr uint16_t HISTORY_ARCHIVE_VERSION = 1;
constexpr int LDR_ADC_MIN = 0;
constexpr int LDR_ADC_MAX = 4095;
constexpr int LDR_THEME_THRESHOLD = 200;
constexpr int LDR_THEME_HYSTERESIS = 60;
constexpr uint8_t LDR_THEME_CONFIRM_READS = 5;
constexpr uint32_t UI_RUNTIME_SUBPAGE_MIN_FREE = 18000UL;
constexpr uint32_t UI_RUNTIME_MODAL_MIN_FREE = 12000UL;
constexpr uint32_t UI_RUNTIME_BIGGEST_MIN = 4096UL;
constexpr uint32_t UI_RUNTIME_HARDWARE_MIN_FREE = 6000UL;
constexpr uint32_t UI_RUNTIME_HARDWARE_MIN_LARGEST = 2048UL;
constexpr uint32_t SENSOR_FRAME_STALE_MS = 3500UL;
constexpr uint32_t ACTUATOR_FAILURE_HOLD_MS = 5000UL;
constexpr size_t WIFI_PASSWORD_MAX_LEN = 64;
constexpr char WIFI_PROFILE_DIR[] = "/aq/config/wifi";
constexpr uint16_t OTA_PORTAL_HTTP_PORT = 80;
constexpr uint16_t OTA_PORTAL_DNS_PORT = 53;
constexpr char OTA_PORTAL_INDEX_PATH[] = "/aq/ota/index.html";
constexpr char OTA_PORTAL_HISTORY_DIR[] = "/aq/data/history";
constexpr char OTA_PORTAL_LOG_DIR[] = "/aq/data/logs";
constexpr char OTA_PORTAL_DIAG_DIR[] = "/aq/data/diagnostics";
constexpr uint32_t WEB_UI_ACTIVITY_TIMEOUT_MS = 12000UL;
constexpr uint32_t WEB_UI_CLIENT_TIMEOUT_MS = 15000UL;
constexpr uint32_t WEB_UI_CONTROL_REFRESH_MS = 1000UL;
constexpr size_t WEB_UI_SESSION_ID_MAX_LEN = 24U;
constexpr char RELAY_PROFILE_PATH[] = "/aq/config/relays.json";
constexpr char RELAY_PROFILE_TEMP_PATH[] = "/aq/config/relays.json.tmp";
constexpr char RELAY_PROFILE_BACKUP_PATH[] = "/aq/config/relays.json.bak";
constexpr size_t RELAY_PROFILE_MIN_BYTES = 96U;
constexpr size_t RELAY_PROFILE_MAX_BYTES = 4096U;
constexpr uint32_t RELAY_TEST_MAX_DURATION_MS = 3000UL;
constexpr uint8_t FACTORY_DAY_START_HOUR = 10;
constexpr uint8_t FACTORY_DAY_START_MINUTE = 0;
constexpr uint8_t FACTORY_DAY_END_HOUR = 22;
constexpr uint8_t FACTORY_DAY_END_MINUTE = 0;
constexpr uint8_t FACTORY_DAYBREAK_MORNING_END_HOUR = 10;
constexpr uint8_t FACTORY_DAYBREAK_MORNING_END_MINUTE = 30;
constexpr uint8_t FACTORY_DAY_END_PROFILE_HOUR = 20;
constexpr uint8_t FACTORY_DAY_END_PROFILE_MINUTE = 0;
constexpr uint8_t FACTORY_DAYBREAK_EVENING_END_HOUR = 21;
constexpr uint8_t FACTORY_DAYBREAK_EVENING_END_MINUTE = 0;
constexpr uint8_t FACTORY_FILTER_START_HOUR = 10;
constexpr uint8_t FACTORY_FILTER_START_MINUTE = 30;
constexpr uint8_t FACTORY_FILTER_END_HOUR = 20;
constexpr uint8_t FACTORY_FILTER_END_MINUTE = 30;
constexpr uint8_t FACTORY_CO2_AIR_START_HOUR = 10;
constexpr uint8_t FACTORY_CO2_AIR_START_MINUTE = 0;
constexpr uint8_t FACTORY_CO2_AIR_END_HOUR = 19;
constexpr uint8_t FACTORY_CO2_AIR_END_MINUTE = 0;
constexpr uint8_t FACTORY_FEED_HOUR = 14;
constexpr uint8_t FACTORY_FEED_MINUTE = 0;
constexpr float FACTORY_CO2_TARGET_PH = 6.80f;
constexpr uint32_t CLOCK_NVS_MAGIC = 0x4151434BUL;
constexpr uint16_t CLOCK_NVS_VERSION = 1;
constexpr uint8_t GUI_LOG_CAPACITY = 15;
constexpr size_t GUI_LOG_MESSAGE_LEN = 96;
constexpr uint32_t WIFI_PROFILE_CONNECT_TIMEOUT_MS = 30000UL;
constexpr uint8_t WIFI_REASON_ASSOC_TOOMANY_CODE = 5U;
constexpr uint32_t WIFI_ASSOC_TOOMANY_COOLDOWN_MS = 300000UL;
constexpr char NTP_TZ_POLAND[] = "CET-1CEST,M3.5.0/2,M10.5.0/3";
constexpr char NTP_SERVER_1[] = "pool.ntp.org";
constexpr char NTP_SERVER_2[] = "time.nist.gov";

using ScheduleMode = aquarium::ScheduleMode;

enum class HeaterMode : uint8_t {
    Threshold = 0,
    Off = 1
};

enum class ScheduleDevice : uint8_t {
    Light = 0,
    PlantLight = 1,
    Filter = 2,
    Air = 3,
    Feed = 4,
    QuietHours = 5
};

enum class SoundType {
    Click,
    Save,
    Warning
};

enum class DisplayPowerProfile : uint8_t {
    AlwaysOn = 0,
    Timeout60Seconds = 1,
    AlwaysOff = 2
};

enum class LeakAction : uint8_t {
    AlarmOnly = 0,
    DisableValves = 1,
    DisableAll = 2
};

constexpr uint8_t SPEAKER_PIN = HwConfig::Audio::SPEAKER_PIN;
constexpr uint8_t SPEAKER_LEDC_CHANNEL = HwConfig::Audio::LEDC_CHANNEL;

static bool audio_initialized = false;

static void play_system_sound(SoundType type);

struct AquariumUiConfig {
    uint32_t magic;
    uint16_t version;
    uint8_t lightMode;
    uint8_t lightColorMode;
    uint8_t lightStartHour;
    uint8_t lightStartMinute;
    uint8_t lightEndHour;
    uint8_t lightEndMinute;
    uint8_t lightSchedColorMode;
    uint8_t plantLightMode;
    uint8_t plantLightColorMode;
    uint8_t plantStartHour;
    uint8_t plantStartMinute;
    uint8_t plantEndHour;
    uint8_t plantEndMinute;
    uint8_t plantSchedColorMode;
    uint8_t filterMode;
    uint8_t filterStartHour;
    uint8_t filterStartMinute;
    uint8_t filterEndHour;
    uint8_t filterEndMinute;
    uint8_t airMode;
    uint8_t airStartHour;
    uint8_t airStartMinute;
    uint8_t airEndHour;
    uint8_t airEndMinute;
    uint8_t heaterMode;
    float targetTemp;
    float tempHysteresis;
    bool feedEnabled;
    uint8_t feedDays;
    uint8_t feedCount;
    uint8_t feedHour1;
    uint8_t feedMinute1;
    uint8_t feedHour2;
    uint8_t feedMinute2;
    bool alwaysScreenOn;
    bool ldrThemeEnabled;
    uint8_t ldrSensitivity;
    bool manualLightTheme;
    bool showPhSensor;
    bool enableHeater;
    bool enableAerator;
    bool enableEc;
    bool enableCo2;
    bool enableWaterLevel;
    bool enableLeak;
    bool enableFlow;
    bool soundEnabled;
    bool quietHoursEnabled;
    uint8_t quietStartHour;
    uint8_t quietStartMinute;
    uint8_t quietEndHour;
    uint8_t quietEndMinute;
    bool devMode;
    bool modemSleep;
    uint32_t crc32;
};

struct UiRuntimeState {
    uint8_t lightActiveMode;
    uint8_t plantLightActiveMode;
    bool lightOn;
    bool plantLightOn;
    bool filterOn;
    bool airOn;
    bool heaterOn;
    bool co2On;
    bool waterFillOn;
    float lastTemp;
    float previousTemp;
    float lastPh;
    uint32_t lastAutoFeedMs;
};

static Preferences prefs;
static AquariumUiConfig cfg;
static aquarium::ControlModeManager control_modes;
static aquarium::AdminSessionManager admin_sessions;
static aquarium::IdempotencyLedger command_ledger;
constexpr uint32_t OTA_CONFIG_BACKUP_MAGIC = 0x32424341UL; // ACB2
constexpr uint16_t OTA_CONFIG_BACKUP_VERSION = 1U;
constexpr char OTA_CONFIG_BACKUP_NAMESPACE[] = "aq_ota_cfg";
constexpr char OTA_CONFIG_BACKUP_KEY[] = "snapshot";

struct OtaConfigBackup {
    uint32_t magic;
    uint16_t version;
    AquariumUiConfig config;
    bool display_auto;
    uint8_t display_brightness;
    uint8_t display_profile;
    float co2_ph;
    uint16_t co2_max_minutes;
    uint16_t ato_max_seconds;
    uint8_t leak_action;
    uint32_t crc32;
};
static bool display_auto_brightness = true;
static uint8_t display_max_brightness = 100U;
static DisplayPowerProfile display_power_profile = DisplayPowerProfile::AlwaysOn;
static float co2_target_ph = FACTORY_CO2_TARGET_PH;
static uint16_t co2_max_time_minutes = 540U;
static uint16_t water_timeout_seconds = 120U;
static LeakAction leak_action = LeakAction::DisableAll;
static uint32_t ato_started_ms = 0U;
static bool ato_timeout_latched = false;
static UiRuntimeState runtime = {
    0,     // lightActiveMode
    0,     // plantLightActiveMode
    false, // lightOn
    false, // plantLightOn
    false, // filterOn
    false, // airOn
    false, // heaterOn
    false, // co2On
    false, // waterFillOn
    NAN,   // lastTemp
    NAN,   // previousTemp
    NAN,   // lastPh
    0      // lastAutoFeedMs
};

struct SensorDebugSnapshot {
    int ldrValue;
    bool temperaturePresent;
    bool temperatureStale;
    uint32_t temperatureAgeMs;
    uint32_t temperatureErrorCount;
    bool adcPresent;
    bool phValid;
    int16_t phRaw;
    float phVoltage;
    float phValue;
    bool ecValid;
    int16_t ecRaw;
    float ecVoltage;
    float ecValue;
    bool mcpPresent;
    bool mcpValid;
    uint16_t mcpState;
    uint32_t updatedMs;
};

static SensorDebugSnapshot sensor_debug = {};

struct GuiLogEntry {
    uint32_t ts;
    char message[GUI_LOG_MESSAGE_LEN];
    bool critical;
};

struct __attribute__((packed)) HistoryArchiveHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t headerSize;
    uint16_t recordSize;
    uint16_t year;
    uint8_t month;
    uint8_t flags;
    uint32_t createdEpoch;
    uint8_t reserved[14];
};

struct __attribute__((packed)) HistoryArchiveRecord {
    uint32_t epoch;
    int16_t tempCx100;
    int16_t phX1000;
    int16_t ldr;
    uint32_t heapBytes;
    uint8_t flags;
    uint8_t reserved[3];
};

static_assert(sizeof(HistoryArchiveHeader) == 32, "History archive header must stay fixed-size.");
static_assert(sizeof(HistoryArchiveRecord) == 18, "History archive record must stay fixed-size.");

struct McpOutputState {
    bool initialized;
    bool light;
    bool plantLight;
    bool filter;
    bool aerator;
    bool heater;
    bool co2;
    bool waterDosing;
    bool feeder;
    aquarium::AquaelLightController frontLight;
    aquarium::AquaelLightController rearLight;
};

static GuiLogEntry gui_logs_normal[GUI_LOG_CAPACITY] = {};
static GuiLogEntry gui_logs_important[GUI_LOG_CAPACITY] = {};
static uint8_t gui_logs_normal_count = 0;
static uint8_t gui_logs_important_count = 0;
static McpOutputState mcp_outputs = {};
static unsigned int current_alarm_flags = aquarium::AlarmNone;
static aquarium::AlarmStabilityFilter alarm_stability_filter;
static bool actuator_write_failed = false;
static uint32_t actuator_write_failure_ms = 0U;
static uint32_t actuator_write_error_count = 0U;
static uint32_t history_archive_last_write_ms = 0;
static bool history_archive_has_written = false;
static bool controller_clock_reliable = false;
static char controller_clock_source[12] = "start";
static bool feeder_pulse_active = false;
static bool feeder_start_pending = false;
static uint32_t feeder_pulse_deadline_ms = 0U;
static uint32_t last_feed_epoch = 0;
static char last_feed_result[16] = "none";

static void secure_clear_gui_buffer(
    void *buffer, size_t length) {
    volatile uint8_t *bytes =
        static_cast<volatile uint8_t *>(buffer);
    while (length-- > 0U) {
        *bytes++ = 0U;
    }
}

static void record_actuator_write_result(bool success) {
    const uint32_t now_ms = millis();
    if (!success) {
        actuator_write_failed = true;
        actuator_write_failure_ms = now_ms == 0U ? 1U : now_ms;
        if (actuator_write_error_count < UINT32_MAX) {
            ++actuator_write_error_count;
        }
        return;
    }
    if (actuator_write_failed &&
        static_cast<uint32_t>(now_ms - actuator_write_failure_ms) >=
            ACTUATOR_FAILURE_HOLD_MS) {
        actuator_write_failed = false;
    }
}

static unsigned int configured_alarm_sensor_mask() {
    unsigned int required = aquarium::AlarmSensorTemperature;
    if (cfg.showPhSensor || cfg.enableCo2) {
        required |= aquarium::AlarmSensorPh;
    }
    if (cfg.enableEc) {
        required |= aquarium::AlarmSensorEc;
    }
    if (cfg.enableWaterLevel) {
        required |= aquarium::AlarmSensorWaterLevel;
    }
    if (cfg.enableLeak) {
        required |= aquarium::AlarmSensorLeak;
    }
    return required;
}

static unsigned int evaluate_live_alarm_flags(float temperature,
                                              bool temperature_valid,
                                              float ph,
                                              bool ph_valid) {
    const uint32_t now_ms = millis();
    const bool frame_stale =
        sensor_debug.updatedMs == 0U ||
        static_cast<uint32_t>(now_ms - sensor_debug.updatedMs) >
            SENSOR_FRAME_STALE_MS;
    const unsigned int required = configured_alarm_sensor_mask();
    unsigned int present = aquarium::AlarmNone;
    unsigned int stale = aquarium::AlarmNone;

    if (sensor_debug.temperaturePresent) {
        present |= aquarium::AlarmSensorTemperature;
        if (sensor_debug.temperatureStale ||
            !temperature_valid ||
            sensor_debug.temperatureAgeMs > SENSOR_FRAME_STALE_MS ||
            frame_stale) {
            stale |= aquarium::AlarmSensorTemperature;
        }
    }
    if (sensor_debug.adcPresent) {
        present |= aquarium::AlarmSensorPh |
                   aquarium::AlarmSensorEc;
        if ((required & aquarium::AlarmSensorPh) != 0U &&
            (!sensor_debug.phValid || !ph_valid || frame_stale)) {
            stale |= aquarium::AlarmSensorPh;
        }
        if ((required & aquarium::AlarmSensorEc) != 0U &&
            (!sensor_debug.ecValid || frame_stale)) {
            stale |= aquarium::AlarmSensorEc;
        }
    }
    if (sensor_debug.mcpPresent) {
        present |= aquarium::AlarmSensorWaterLevel |
                   aquarium::AlarmSensorLeak;
        if (!sensor_debug.mcpValid || frame_stale) {
            if ((required & aquarium::AlarmSensorWaterLevel) != 0U) {
                stale |= aquarium::AlarmSensorWaterLevel;
            }
            if ((required & aquarium::AlarmSensorLeak) != 0U) {
                stale |= aquarium::AlarmSensorLeak;
            }
        }
    }

    const bool adc_required =
        (required &
         (aquarium::AlarmSensorPh | aquarium::AlarmSensorEc)) != 0U;
    const bool temperature_bus_fault =
        (required & aquarium::AlarmSensorTemperature) != 0U &&
        sensor_debug.temperatureErrorCount > 0U &&
        (!sensor_debug.temperaturePresent ||
         sensor_debug.temperatureStale);
    const bool sensor_bus_fault =
        temperature_bus_fault ||
        (adc_required &&
         (!sensor_debug.adcPresent || frame_stale)) ||
        !sensor_debug.mcpPresent ||
        !sensor_debug.mcpValid ||
        frame_stale;

    const uint16_t water_level_mask = static_cast<uint16_t>(
        1U << static_cast<uint8_t>(HwConfig::CH_WATER_LEVEL));
    const uint16_t leak_mask = static_cast<uint16_t>(
        1U << static_cast<uint8_t>(HwConfig::CH_LEAK));
    const bool water_level_valid =
        cfg.enableWaterLevel &&
        sensor_debug.mcpPresent &&
        sensor_debug.mcpValid &&
        !frame_stale;
    const bool leak_valid =
        cfg.enableLeak &&
        sensor_debug.mcpPresent &&
        sensor_debug.mcpValid &&
        !frame_stale;
    const bool water_level_high =
        water_level_valid &&
        (sensor_debug.mcpState & water_level_mask) != 0U;
    const bool leak_detected =
        leak_valid &&
        (sensor_debug.mcpState & leak_mask) != 0U;

    return aquarium::evaluate_alarm_flags({
        temperature_valid,
        temperature,
        (required & aquarium::AlarmSensorPh) != 0U && ph_valid,
        ph,
        water_level_valid,
        water_level_high,
        leak_valid,
        leak_detected,
        false,
        NAN,
        required,
        present,
        stale,
        sensor_bus_fault,
        actuator_write_failed
    });
}

static lv_obj_t *pages[PAGE_COUNT];
static lv_obj_t *nav_btns[PAGE_COUNT];

static lv_obj_t *label_date;
static lv_obj_t *label_power_mode;
static lv_obj_t *label_rtc_bat;
static lv_obj_t *label_wifi_state;
static char status_ip_address[16] = "0.0.0.0";
static uint32_t status_last_sample_ms = 0U;
static bool status_sensor_bus_ok = false;
static bool status_temperature_ok = false;
static lv_obj_t *label_clock_time;
static lv_obj_t *label_clock_date;

static lv_obj_t *home_temp_current;
static void rebuild_gui_tree_for_theme();
static void apply_3d_button_properties(lv_obj_t *btn);
static void show_top_notification(const char *text, bool success);
static lv_obj_t *home_ph_current;
static lv_obj_t *home_temp_target_lbl;
static lv_obj_t *home_temp_trend_lbl;
static lv_obj_t *home_feed_time_lbl;
static lv_obj_t *home_light_state_lbl;
static lv_obj_t *home_light_mode_lbl;
static lv_obj_t *home_light_color_lbl;
static lv_obj_t *home_plant_state_lbl;
static lv_obj_t *home_plant_mode_lbl;
static lv_obj_t *home_plant_color_lbl;
static lv_obj_t *home_filter_state_lbl;
static lv_obj_t *home_filter_mode_lbl;
static lv_obj_t *home_heater_state_lbl;
static lv_obj_t *home_heater_mode_lbl;
static lv_obj_t *home_air_state_lbl;
static lv_obj_t *home_air_mode_lbl;

static lv_obj_t *device_light_mode_lbl;
static lv_obj_t *device_light_detail_lbl;
static lv_obj_t *device_plant_mode_lbl;
static lv_obj_t *device_plant_detail_lbl;
static lv_obj_t *device_filter_mode_lbl;
static lv_obj_t *device_filter_detail_lbl;
static lv_obj_t *device_heater_mode_lbl;
static lv_obj_t *device_heater_detail_lbl;
static lv_obj_t *device_air_mode_lbl;
static lv_obj_t *device_air_detail_lbl;

static lv_obj_t *sched_light_lbl;
static lv_obj_t *sched_plant_lbl;
static lv_obj_t *sched_filter_lbl;
static lv_obj_t *sched_air_lbl;
static lv_obj_t *sched_feed_lbl;

static lv_obj_t *temp_auto_sw;
static lv_obj_t *temp_target_val_lbl;
static lv_obj_t *temp_hysteresis_val_lbl;
static lv_obj_t *temp_pump_power_lbl;
static lv_obj_t *temp_pump_power_slider;
static lv_obj_t *feed_mode_val_lbl;

static lv_obj_t *subpage_wifi;
static lv_obj_t *subpage_clock;
static lv_obj_t *subpage_diagnostics;
static lv_obj_t *subpage_power;
static lv_obj_t *subpage_screen;
static lv_obj_t *subpage_logs;
static lv_obj_t *subpage_sched_editor;
static lv_obj_t *modal_feeder;
static lv_obj_t *subpage_feed_editor = nullptr;
static lv_obj_t *editor_start_h_lbl = nullptr;
static lv_obj_t *editor_start_m_lbl = nullptr;
static lv_obj_t *feed_editor_mode_lbl = nullptr;
static lv_obj_t *screen_ph_enable_sw = nullptr;


static lv_obj_t *wifi_ssid_lbl;
static lv_obj_t *wifi_ip_lbl;
static lv_obj_t *wifi_status_message_lbl;
static lv_obj_t *wifi_mode_lbl = nullptr;
static lv_obj_t *wifi_rssi_lbl = nullptr;
static lv_obj_t *wifi_mac_lbl = nullptr;
static lv_obj_t *sta_list_obj = nullptr;
static lv_obj_t *wifi_info_card = nullptr;
static lv_obj_t *diag_uptime_lbl = nullptr;
static lv_obj_t *diag_heap_lbl = nullptr;
static lv_obj_t *diag_reset_reason_lbl = nullptr;
static lv_obj_t *diag_restarts_lbl = nullptr;
static lv_obj_t *diag_cpu_temp_lbl = nullptr;
static lv_obj_t *diag_cpu_freq_lbl = nullptr;
static lv_obj_t *diag_flash_lbl = nullptr;
static lv_obj_t *diag_adc_lbl = nullptr;
static lv_obj_t *diag_mcp_lbl = nullptr;
static lv_obj_t *diag_queue_lbl = nullptr;
static lv_obj_t *diag_ldr_lbl = nullptr;
static lv_obj_t *diag_eco_lbl = nullptr;
static lv_obj_t *diag_rtc_lbl = nullptr;
static uint32_t boot_count_val = 0;
static lv_obj_t *power_warning_lbl_global = nullptr;
static lv_obj_t *power_state_lbl = nullptr;
static lv_obj_t *screen_always_on_sw;
static lv_obj_t *diag_dev_mode_sw = nullptr;
static lv_obj_t *screen_manual_theme_sw;
static lv_obj_t *screen_ldr_enable_sw;
static lv_obj_t *log_list_normal = nullptr;
static lv_obj_t *log_list_important = nullptr;
static lv_obj_t *btn_log_normal = nullptr;
static lv_obj_t *btn_log_important = nullptr;
static bool showing_important_logs = false;
static lv_obj_t *subpage_sounds;
static lv_obj_t *sound_enable_sw = nullptr;
static lv_obj_t *sound_quiet_enable_sw = nullptr;
static lv_obj_t *sound_quiet_sched_lbl = nullptr;
static lv_obj_t *power_modem_sleep_sw = nullptr;
static lv_obj_t *sched_editor_mode_btn = nullptr;
static lv_obj_t *subpage_heater = nullptr;
static lv_obj_t *subpage_ph = nullptr;
static lv_obj_t *subpage_hardware = nullptr;
static lv_obj_t *subpage_co2 = nullptr;
static lv_obj_t *subpage_ec = nullptr;
static lv_obj_t *subpage_water = nullptr;
static lv_obj_t *subpage_leak = nullptr;
static lv_obj_t *subpage_flow = nullptr;
static lv_obj_t *tile_co2 = nullptr;
static lv_obj_t *tile_ec = nullptr;
static lv_obj_t *tile_water = nullptr;
static lv_obj_t *tile_leak = nullptr;
static lv_obj_t *tile_flow = nullptr;
static lv_obj_t *device_co2_detail_lbl = nullptr;
static lv_obj_t *device_ec_detail_lbl = nullptr;
static lv_obj_t *device_water_detail_lbl = nullptr;
static lv_obj_t *device_leak_detail_lbl = nullptr;
static lv_obj_t *device_flow_detail_lbl = nullptr;
static lv_obj_t *co2_state_lbl = nullptr;
static lv_obj_t *co2_ph_lbl = nullptr;
static lv_obj_t *co2_mcp_lbl = nullptr;
static lv_obj_t *ec_value_lbl = nullptr;
static lv_obj_t *ec_raw_lbl = nullptr;
static lv_obj_t *water_state_lbl = nullptr;
static lv_obj_t *leak_state_lbl = nullptr;
static lv_obj_t *flow_state_lbl = nullptr;
static lv_obj_t *calib_value_lbl = nullptr;
static int calib_active_type = -1;
static lv_obj_t *hw_heater_sw = nullptr;
static lv_obj_t *hw_aerator_sw = nullptr;
static lv_obj_t *hw_co2_sw = nullptr;
static lv_obj_t *hw_ec_sw = nullptr;
static lv_obj_t *hw_water_level_sw = nullptr;
static lv_obj_t *hw_leak_sw = nullptr;
static lv_obj_t *hw_flow_sw = nullptr;
static lv_obj_t *hw_matrix = nullptr;
static lv_obj_t *hw_summary_lbl = nullptr;
static lv_obj_t *subpage_service = nullptr;
static lv_obj_t *service_light_sw = nullptr;
static lv_obj_t *service_filter_sw = nullptr;
static lv_obj_t *service_vol_lbl = nullptr;
static std::atomic<bool> musicPlaying{false};
static std::atomic<int> musicVolume{5}; // default 50%
static std::atomic<int> selectedSongIndex{0};
static TaskHandle_t musicTaskHandle = nullptr;
enum class AudioEffect : uint8_t {
    Click = 0,
    Save,
    Warning,
    Mario
};
static StaticQueue_t audio_queue_storage;
static uint8_t audio_queue_buffer[6U * sizeof(AudioEffect)] = {};
static QueueHandle_t audio_queue = nullptr;
static lv_obj_t *device_ph_detail_lbl = nullptr;
static lv_obj_t *btn_sync_ntp_global = nullptr;
static lv_obj_t *btn_sync_ntp_lbl_global;
static lv_obj_t *clock_ntp_row = nullptr;
static lv_obj_t *modal_feeder_title_lbl;
static lv_obj_t *modal_feeder_msg_lbl;
static lv_timer_t *feeder_modal_close_timer = nullptr;

struct SchedSnapshot {
    uint8_t mode;
    uint8_t startH, startM, endH, endM;
    uint8_t colorMode;
};
static SchedSnapshot sched_snapshot;

struct FeedSnapshot {
    bool enabled;
    uint8_t days;
    uint8_t count;
    uint8_t hour1, minute1;
    uint8_t hour2, minute2;
};
static FeedSnapshot feed_snapshot;

struct HeaterSnapshot {
    uint8_t mode;
    float targetTemp;
    float tempHysteresis;
};
static HeaterSnapshot heater_snapshot;

struct PhSnapshot {
    bool showPh;
};
static PhSnapshot ph_snapshot;

struct ClockSnapshot {
    int hour;
    int minute;
    int second;
    int day;
    int month;
    int year;
};
static ClockSnapshot clock_snapshot;

struct ScreenSnapshot {
    bool alwaysScreenOn;
    bool ldrThemeEnabled;
    bool manualLightTheme;
};
static ScreenSnapshot screen_snapshot;

struct SoundSnapshot {
    bool soundEnabled;
    bool quietHoursEnabled;
    uint8_t quietStartHour;
    uint8_t quietStartMinute;
    uint8_t quietEndHour;
    uint8_t quietEndMinute;
};
static SoundSnapshot sound_snapshot;

enum class ActiveSubpage : int {
    None = -1,
    Wifi = 0,
    Screen = 1,
    Logs = 2,
    Clock = 3,
    Diagnostics = 4,
    Power = 5,
    Sounds = 6,
    FeedEditor = 7,
    SchedEditor = 8,
    Heater = 9,
    Ph = 10,
    Service = 11,
    Hardware = 12,
    Co2 = 13,
    Ec = 14,
    WaterLevel = 15,
    Leak = 16,
    Flow = 17
};
static ActiveSubpage current_subpage = ActiveSubpage::None;
static int current_page_index = 0;
static aquarium::WebActivityTracker web_ui_clients(WEB_UI_CLIENT_TIMEOUT_MS);
static uint32_t web_ui_last_request_ms = 0;
static uint32_t web_ui_last_control_ms = 0;
static uint32_t web_ui_last_screen_ms = 0;
static bool web_ui_focus_active = false;
static bool web_ui_restore_valid = false;
static int web_ui_restore_page_index = 0;
static ActiveSubpage web_ui_restore_subpage = ActiveSubpage::None;

static const char *nav_page_name(int index) {
    switch (index) {
    case 0: return "Start";
    case 1: return "Plan";
    case 2: return "Mod";
    case 3: return "Hist";
    case 4: return "Sys";
    default: return "Unknown";
    }
}

static const char *nav_subpage_name(ActiveSubpage subpage) {
    switch (subpage) {
    case ActiveSubpage::None: return "None";
    case ActiveSubpage::Wifi: return "WiFi";
    case ActiveSubpage::Screen: return "Screen";
    case ActiveSubpage::Logs: return "Logs";
    case ActiveSubpage::Clock: return "Clock";
    case ActiveSubpage::Diagnostics: return "Diagnostics";
    case ActiveSubpage::Power: return "Power";
    case ActiveSubpage::Sounds: return "Sounds";
    case ActiveSubpage::FeedEditor: return "FeedEditor";
    case ActiveSubpage::SchedEditor: return "SchedEditor";
    case ActiveSubpage::Heater: return "Heater";
    case ActiveSubpage::Ph: return "pH";
    case ActiveSubpage::Service: return "Service";
    case ActiveSubpage::Hardware: return "Hardware";
    case ActiveSubpage::Co2: return "CO2";
    case ActiveSubpage::Ec: return "EC";
    case ActiveSubpage::WaterLevel: return "WaterLevel";
    case ActiveSubpage::Leak: return "Leak";
    case ActiveSubpage::Flow: return "Flow";
    default: return "Unknown";
    }
}

static const char *nav_schedule_device_name(ScheduleDevice device) {
    switch (device) {
    case ScheduleDevice::Light: return "FrontLight";
    case ScheduleDevice::PlantLight: return "RearLight";
    case ScheduleDevice::Filter: return "Filter";
    case ScheduleDevice::Air: return "Air";
    case ScheduleDevice::Feed: return "Feed";
    case ScheduleDevice::QuietHours: return "QuietHours";
    default: return "Unknown";
    }
}

static void log_ram_checkpoint(const char *stage) {
    Serial.printf("UI_RAM: %s free=%lu min_free=%lu heap8=%lu largest8=%lu\n",
                  stage != nullptr ? stage : "unknown",
                  static_cast<unsigned long>(ESP.getFreeHeap()),
                  static_cast<unsigned long>(ESP.getMinFreeHeap()),
                  static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_8BIT)),
                  static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
}

static void log_page_ram(const char *stage, int index) {
    Serial.printf("UI_RAM: %s page=%d name=%s free=%lu min_free=%lu heap8=%lu largest8=%lu\n",
                  stage != nullptr ? stage : "page",
                  index,
                  nav_page_name(index),
                  static_cast<unsigned long>(ESP.getFreeHeap()),
                  static_cast<unsigned long>(ESP.getMinFreeHeap()),
                  static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_8BIT)),
                  static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
}

static void log_subpage_ram(const char *stage, ActiveSubpage subpage) {
    Serial.printf("UI_RAM: %s subpage=%s free=%lu min_free=%lu heap8=%lu largest8=%lu\n",
                  stage != nullptr ? stage : "subpage",
                  nav_subpage_name(subpage),
                  static_cast<unsigned long>(ESP.getFreeHeap()),
                  static_cast<unsigned long>(ESP.getMinFreeHeap()),
                  static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_8BIT)),
                  static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
}

static void delayed_ram_checkpoint_cb(lv_timer_t *timer) {
    const char *stage = static_cast<const char *>(timer->user_data);
    log_ram_checkpoint(stage != nullptr ? stage : "delayed");
    lv_timer_del(timer);
}

static void schedule_delayed_ram_checkpoint(const char *stage) {
    lv_timer_t *timer = lv_timer_create(delayed_ram_checkpoint_cb, 80, const_cast<char *>(stage));
    if (timer == nullptr) {
        log_ram_checkpoint(stage);
    }
}

static void log_tab_enter_request(int index) {
    const uint32_t heap_free = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    const uint32_t heap_largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    Serial.printf("UI_NAV: tab enter request page=%d name=%s previous_page=%d previous_subpage=%s heap_free=%lu heap_largest=%lu\n",
                  index,
                  nav_page_name(index),
                  current_page_index,
                  nav_subpage_name(current_subpage),
                  static_cast<unsigned long>(heap_free),
                  static_cast<unsigned long>(heap_largest));
}

static void log_subpage_enter_request(ActiveSubpage target, const char *source) {
    const uint32_t heap_free = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    const uint32_t heap_largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    Serial.printf("UI_NAV: subpage enter request target=%s source=%s page=%d current_subpage=%s heap_free=%lu heap_largest=%lu\n",
                  nav_subpage_name(target),
                  source != nullptr ? source : "unknown",
                  current_page_index,
                  nav_subpage_name(current_subpage),
                  static_cast<unsigned long>(heap_free),
                  static_cast<unsigned long>(heap_largest));
}

static void log_schedule_editor_enter_request(ScheduleDevice device, ActiveSubpage target, const char *source) {
    const uint32_t heap_free = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    const uint32_t heap_largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    Serial.printf("UI_NAV: subpage enter request target=%s source=%s device=%s page=%d current_subpage=%s heap_free=%lu heap_largest=%lu\n",
                  nav_subpage_name(target),
                  source != nullptr ? source : "unknown",
                  nav_schedule_device_name(device),
                  current_page_index,
                  nav_subpage_name(current_subpage),
                  static_cast<unsigned long>(heap_free),
                  static_cast<unsigned long>(heap_largest));
}

static bool ensure_runtime_ui_heap(const char *target, uint32_t min_free, uint32_t min_largest) {
    const uint32_t heap_free = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    const uint32_t heap_largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    if (heap_free < min_free || heap_largest < min_largest) {
        Serial.printf("UI_NAV: blocked runtime UI allocation target=%s heap_free=%lu heap_largest=%lu min_free=%lu min_largest=%lu\n",
                      target != nullptr ? target : "unknown",
                      static_cast<unsigned long>(heap_free),
                      static_cast<unsigned long>(heap_largest),
                      static_cast<unsigned long>(min_free),
                      static_cast<unsigned long>(min_largest));
        return false;
    }
    Serial.printf("UI_MEM: runtime UI allocation allowed target=%s heap_free=%lu heap_largest=%lu\n",
                  target != nullptr ? target : "unknown",
                  static_cast<unsigned long>(heap_free),
                  static_cast<unsigned long>(heap_largest));
    return true;
}

enum class PinAction : uint8_t {
    None,
    OpenScheduleEditor,
    OpenHeater,
    OpenPh,
    OpenTimePicker,
    OpenDatePicker,
    StartOta,
    Restart,
    LightSleep,
    DeepSleep,
    Hibernation,
    FactoryReset,
    ToggleModemSleep,
    ToggleHardware,
    ToggleDevMode,
    TogglePhSensor,
    StartCalibration
};

enum class HardwareToggle : uint8_t {
    Heater,
    Aerator,
    Co2,
    Ec,
    WaterLevel,
    Leak,
    Flow
};

struct PendingPinAction {
    PinAction action;
    intptr_t value;
    bool state;
};

constexpr uint32_t PIN_AUTH_WINDOW_MS = 5UL * 60UL * 1000UL;
// PinGuard must stay extremely small: this UI runs with a fragmented LVGL heap,
// so the PIN prompt is intentionally one btnmatrix plus labels, not a tree of
// separate buttons/cards. The same thresholds are used for init and fallback
// because the object tree is now small enough to be rebuilt safely at runtime.
constexpr uint32_t UI_PIN_INIT_MIN_FREE = 6000UL;
constexpr uint32_t UI_PIN_INIT_MIN_LARGEST = 2048UL;
constexpr uint32_t UI_RUNTIME_PIN_MIN_FREE = 6000UL;
constexpr uint32_t UI_RUNTIME_PIN_MIN_LARGEST = 2048UL;
constexpr uint32_t PIN_KEY_DEBOUNCE_MS = 70UL;
constexpr char PIN_KEY_BACK[] = "DEL";
constexpr char PIN_KEY_OK[] = "OK";
constexpr uint16_t PIN_BACK_BTN_ID = 3;
constexpr uint16_t PIN_OK_BTN_ID = 7;
static const char *pin_map[] = {
    "1", "2", "3", LV_SYMBOL_LEFT,
    "\n",
    "4", "5", "6", PIN_KEY_OK,
    "\n",
    "7", "8", "9", "0",
    ""
};
static bool pin_authenticated = false;
static uint32_t pin_auth_until_ms = 0;
static uint32_t pin_last_key_ms = 0;
static PendingPinAction pending_pin_action = {PinAction::None, 0, false};
static lv_obj_t *pin_overlay = nullptr;
static lv_obj_t *pin_value_lbl = nullptr;
static lv_obj_t *pin_status_lbl = nullptr;
static lv_obj_t *pin_matrix = nullptr;
static char pin_entry[9] = "";

static bool is_scanning = false;
static unsigned long conn_start_ms = 0;
static bool is_connecting = false;
static char selected_ssid[64] = "";
static char pending_wifi_password[WIFI_PASSWORD_MAX_LEN + 1] = "";
static bool pending_wifi_password_valid = false;
static lv_timer_t *wifi_check_timer = nullptr;
static bool scan_started = false;
static unsigned long scan_start_ms = 0;
static bool wifi_events_registered = false;
static volatile uint8_t wifi_last_disconnect_reason = 0;
static volatile uint32_t wifi_last_disconnect_ms = 0;
static aquarium::WifiRetryPolicy wifi_retry_policy(WIFI_REASON_ASSOC_TOOMANY_CODE,
                                                    WIFI_ASSOC_TOOMANY_COOLDOWN_MS);
static IPAddress ota_portal_ip(192, 168, 4, 1);
static IPAddress ota_portal_gateway(192, 168, 4, 1);
static IPAddress ota_portal_subnet(255, 255, 255, 0);
static DNSServer ota_dns_server;
static WebServer ota_http_server(OTA_PORTAL_HTTP_PORT);
static bool ota_portal_running = false;
static bool ota_portal_dns_running = false;
static bool ota_portal_sta_running = false;
static bool ota_mdns_running = false;
static bool ota_http_update_ok = false;
static bool ota_http_update_failed = false;
static bool ota_http_service_mode_owned = false;
static bool arduino_ota_service_mode_owned = false;
static bool ota_reboot_pending = false;
static RuntimeFaultReason ota_reboot_reason =
    RuntimeFaultReason::ManualRestart;
static bool ota_shutdown_pending = false;
static bool ota_start_pending = false;
static bool wifi_disconnect_pending = false;
static bool wifi_autoconnect_pending = false;
static bool wifi_scan_prepare_pending = false;
static bool wifi_connect_pending = false;
static bool ntp_sync_pending = false;
static uint32_t ntp_sync_deadline_ms = 0U;
static uint32_t ntp_result_until_ms = 0U;
static bool ota_http_upload_active = false;
static uint32_t ota_reboot_at_ms = 0;
static uint32_t ota_shutdown_at_ms = 0;
static uint32_t wifi_disconnect_at_ms = 0;
static uint32_t ota_http_upload_bytes = 0;
static uint32_t ota_http_upload_total = 0;
static int ota_http_upload_percent = -1;
static char ota_http_update_msg[96] = "";
static portMUX_TYPE ble_pairing_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t ble_pairing_passkey = 0U;
static volatile uint32_t ble_pairing_until_ms = 0U;
static volatile uint8_t ble_pairing_state = 0U;
static uint8_t relay_test_active_mask = 0U;
static uint8_t relay_test_applied_mask = 0U;
static uint32_t relay_test_deadline_ms[8] = {0U};

static lv_obj_t *tile_light = nullptr;
static lv_obj_t *tile_plant = nullptr;
static lv_obj_t *tile_filter = nullptr;
static lv_obj_t *tile_feeder = nullptr;
static lv_obj_t *tile_heater = nullptr;
static lv_obj_t *tile_ph = nullptr;
static lv_obj_t *tile_air = nullptr;

static lv_obj_t *feed_day_btns[7] = {nullptr};
static lv_obj_t *feed_enable_sw = nullptr;
static lv_obj_t *feed_freq_btn = nullptr;
static lv_obj_t *feed_time2_row = nullptr;
static lv_obj_t *feed_time1_h_lbl = nullptr;
static lv_obj_t *feed_time1_m_lbl = nullptr;
static lv_obj_t *feed_time2_h_lbl = nullptr;
static lv_obj_t *feed_time2_m_lbl = nullptr;

static lv_obj_t *device_btns[5][3] = {{nullptr}};
static void set_device_mode_cb(lv_event_t *e);


static lv_obj_t *chart_temp;
static lv_chart_series_t *chart_temp_series;
static lv_obj_t *chart_min_lbl;
static lv_obj_t *chart_max_lbl;
static lv_obj_t *chart_cur_lbl;
static lv_obj_t *chart_target_lbl;
static lv_obj_t *chart_range_lbl;
static float temp_history[TEMP_HISTORY_POINTS];
static bool heater_history[TEMP_HISTORY_POINTS];
static float ph_history[TEMP_HISTORY_POINTS];
static int ldr_history[TEMP_HISTORY_POINTS];
static uint32_t heap_history[TEMP_HISTORY_POINTS];
static uint32_t history_epoch[TEMP_HISTORY_POINTS];
static uint8_t history_count = 0;

static lv_obj_t *chart_ph = nullptr;
static lv_chart_series_t *chart_ph_series = nullptr;
static lv_obj_t *chart_ldr = nullptr;
static lv_chart_series_t *chart_ldr_series = nullptr;
static lv_obj_t *chart_heap = nullptr;
static lv_chart_series_t *chart_heap_series = nullptr;
static lv_obj_t *btn_chart_heap = nullptr;

static lv_chart_series_t *chart_temp_target_series = nullptr;
static lv_chart_series_t *chart_temp_upper_series = nullptr;
static lv_chart_series_t *chart_temp_lower_series = nullptr;
static lv_chart_series_t *chart_temp_heater_series = nullptr;

// WiFi Panels & State
static lv_obj_t *wifi_main_panel = nullptr;
static lv_obj_t *wifi_sta_panel = nullptr;
static lv_obj_t *wifi_pwd_panel = nullptr;
static lv_obj_t *wifi_ota_panel = nullptr;
static lv_obj_t *btn_sta = nullptr;
static lv_obj_t *btn_ota = nullptr;
static lv_obj_t *btn_disconnect = nullptr;
static lv_obj_t *wifi_pwd_ta = nullptr;
static lv_obj_t *wifi_pwd_kb = nullptr;
static lv_obj_t *wifi_pwd_title_lbl = nullptr;
static lv_obj_t *web_client_screen = nullptr;
static lv_obj_t *web_client_state_lbl = nullptr;
static lv_obj_t *web_client_url_lbl = nullptr;
static lv_obj_t *web_client_status_lbl = nullptr;
static lv_obj_t *web_client_ssid_lbl = nullptr;
static lv_obj_t *web_client_ip_lbl = nullptr;
static lv_obj_t *web_client_rssi_lbl = nullptr;
static lv_obj_t *web_client_uptime_lbl = nullptr;
static lv_obj_t *web_client_progress_bar = nullptr;
static lv_obj_t *web_client_progress_lbl = nullptr;

static void add_gui_log(const char *msg, bool is_important);
static void chart_draw_event_cb(lv_event_t *e);
static void redraw_charts();
static void update_chart_stats();
static void btn_sta_handler(lv_event_t *e);
static void btn_ota_handler(lv_event_t *e);
static void btn_wifi_disc_handler(lv_event_t *e);
static void select_network_cb(lv_event_t *e);
static void keyboard_ready_cb(lv_event_t *e);
static void cancel_sta_cb(lv_event_t *e);
static void cancel_pwd_cb(lv_event_t *e);
static void stop_ota_cb(lv_event_t *e);
static uint32_t controller_unix_time(void);
static void gui_load_clock_settings(void);
static bool gui_save_clock_settings(bool reliable, const char *source);
static bool sync_clock_from_ntp(uint32_t timeout_ms);
static void free_wifi_scan_user_data(void);
static void try_autoconnect_wifi_profile(void);
static bool begin_sta_connection(const char *ssid, const char *password);
static void apply_mcp_outputs(void);
static bool run_feeder_pulse(const char *title, const char *message, bool critical_log);


static lv_obj_t *btn_chart_temp = nullptr;
static lv_obj_t *btn_chart_ph = nullptr;
static lv_obj_t *btn_chart_ldr = nullptr;

enum class ActiveChart {
    Temp,
    Ph,
    Ldr,
    Heap
};

static ActiveChart active_chart = ActiveChart::Temp;
static bool ui_light_theme = false;
static bool gui_rebuild_pending = false;
static int last_ldr_value = 0;
static bool last_ldr_valid = false;

static void select_chart_cb(lv_event_t *e);
static void style_chart_btn(lv_obj_t *btn);
static void test_speaker_cb(lv_event_t *e);
static void open_heater_subpage_cb(lv_event_t *e);
static void open_ph_subpage_cb(lv_event_t *e);
static void open_hardware_subpage_cb(lv_event_t *e);
static void open_co2_subpage_cb(lv_event_t *e);
static void open_ec_subpage_cb(lv_event_t *e);
static void open_water_subpage_cb(lv_event_t *e);
static void open_leak_subpage_cb(lv_event_t *e);
static void open_flow_subpage_cb(lv_event_t *e);
static void save_ph_settings_cb(lv_event_t *e);
static void adjust_clock_cb(lv_event_t *e);
static void save_clock_settings_cb(lv_event_t *e);
static void build_hardware_subpage();
static void build_co2_subpage();
static void build_ec_subpage();
static void build_water_subpage();
static void build_leak_subpage();
static void build_flow_subpage();
static void build_subpages(ActiveSubpage target);
static bool build_page_by_index(uint8_t index);
static void switch_to_page(uint8_t index);
static void delete_active_page();
static void delete_runtime_subpages(bool async_delete);
static bool open_or_build_subpage(ActiveSubpage target);
static void ensure_feeder_modal();
static void close_feeder_modal_cb(lv_timer_t *timer);
static void schedule_feeder_modal_close(uint32_t delay_ms);
static void request_gui_rebuild_async();
static void execute_pin_action(const PendingPinAction &action);
static void pin_submit_current_entry();
static void open_sched_editor_authorized(ScheduleDevice device);
static void open_heater_subpage_authorized();
static void open_ph_subpage_authorized();
static void open_time_picker_authorized();
static void open_date_picker_authorized();
static void start_ota_authorized();
static void start_ota_background();
static void start_ota_portal();
static void start_sta_service_portal();
static void stop_ota_portal();
static void stop_mdns_service();
static void stop_ota_runtime(bool play_sound);
static void prepare_wifi_sta_radio();
static void register_wifi_event_handlers();
static const char *wifi_disconnect_reason_name(uint8_t reason);
static bool wifi_disconnect_reason_is_ap_capacity(uint8_t reason);
static bool web_ui_session_id_valid(const char *session_id);
static uint8_t web_ui_active_client_count(uint32_t now_ms);
static void web_ui_track_client(const char *session_id, uint32_t now_ms);
static void web_ui_release_client(const char *session_id);
static void web_ui_clear_clients();
static void ota_portal_mark_web_activity();
static bool ota_portal_has_recent_web_activity(uint32_t now_ms);
static bool gui_web_focus_blocks_local_ui();
static void web_client_back_cb(lv_event_t *e);
static void gui_web_client_screen_create();
static void gui_web_client_screen_delete();
static void gui_web_client_screen_update(bool force);
static void gui_web_focus_apply_wifi_controls(bool force);
static void gui_web_focus_enter();
static void gui_web_focus_exit();
static void gui_web_focus_update();
static void ota_portal_handle_root();
static void ota_portal_handle_status();
static void ota_portal_handle_v2_capabilities();
static void ota_portal_handle_v2_auth();
static void ota_portal_handle_v2_logout();
static void ota_portal_handle_i2c_scan();
static void ota_portal_handle_logs();
static void ota_portal_handle_action();
static void ota_portal_handle_web_session();
static void ota_portal_handle_events();
static void ota_portal_handle_alarm_events();
static void ota_portal_handle_settime();
static bool is_v2_control_action(const char *action);
static void force_safe_service_outputs();
static bool ble_json_read_bool(const char *json, const char *key, bool *out);
static bool ble_json_read_long(const char *json,
                               const char *key,
                               long minimum,
                               long maximum,
                               long *out);
static bool ble_json_read_float(const char *json,
                                const char *key,
                                float minimum,
                                float maximum,
                                float *out);
static bool ble_json_read_string(const char *json,
                                 const char *key,
                                 char *out,
                                 size_t out_size);
static void ota_portal_handle_current_history_csv();
static void ota_portal_handle_files();
static void ota_portal_handle_download();
static void ota_portal_handle_stop_ota();
static void ota_portal_handle_update_finish();
static void ota_portal_handle_update_upload();
static void ota_portal_handle_not_found();
static bool ota_portal_require_pin();
static bool ota_portal_require_admin_session();
static bool ota_portal_sd_ready();
static const char *ota_portal_basename(const char *path);
static void restart_authorized();
static void light_sleep_authorized();
static void deep_sleep_authorized();
static void hibernation_authorized();
static void factory_reset_authorized();
static void apply_modem_sleep_authorized(bool enabled);
static void apply_hardware_toggle_authorized(HardwareToggle toggle, bool enabled);
static void apply_dev_mode_authorized(bool enabled);
static void apply_ph_sensor_authorized(bool enabled);
static void open_calibration_wizard_authorized(int type);
static bool pin_guard_execute_or_prompt(PinAction action, intptr_t value = 0, bool state = false);

static void back_service_cb(lv_event_t *e);
static void service_tile_cb(lv_event_t *e);
static void service_light_sw_cb(lv_event_t *e);
static void service_filter_sw_cb(lv_event_t *e);
static void service_song_dd_cb(lv_event_t *e);
static void service_volume_slider_cb(lv_event_t *e);
static void service_play_cb(lv_event_t *e);
static void service_stop_cb(lv_event_t *e);

static ScheduleDevice current_editor_device = ScheduleDevice::Light;
static lv_obj_t *editor_title_lbl;
static lv_obj_t *editor_mode_lbl;
static uint8_t editor_selected_hour = 12;
static lv_obj_t *timeline_blocks[24] = {nullptr};
static lv_obj_t *editor_hour_lbl = nullptr;
static lv_obj_t *editor_hourly_mode_lbl = nullptr;
static lv_obj_t *editor_mode_btns[4] = {nullptr};
static lv_obj_t *editor_start_hour_lbl = nullptr;
static lv_obj_t *editor_start_min_lbl = nullptr;
static lv_obj_t *sched_editor_start_h_lbl = nullptr;
static lv_obj_t *sched_editor_start_m_lbl = nullptr;
static lv_obj_t *sched_editor_end_h_lbl = nullptr;
static lv_obj_t *sched_editor_end_m_lbl = nullptr;
static lv_obj_t *sched_editor_color_row = nullptr;

static uint32_t crc32_bytes(const void *buffer, size_t length) {
    uint32_t crc = 0xFFFFFFFFUL;
    const uint8_t *data = static_cast<const uint8_t *>(buffer);
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 1U) ? ((crc >> 1U) ^ 0xEDB88320UL) : (crc >> 1U);
        }
    }
    return ~crc;
}

static uint32_t config_crc(const AquariumUiConfig &value) {
    return crc32_bytes(&value, sizeof(AquariumUiConfig) - sizeof(uint32_t));
}

static uint32_t ota_config_backup_crc(const OtaConfigBackup &value) {
    return crc32_bytes(&value, sizeof(OtaConfigBackup) - sizeof(uint32_t));
}

static bool backup_configuration_for_ota() {
    OtaConfigBackup backup = {};
    backup.magic = OTA_CONFIG_BACKUP_MAGIC;
    backup.version = OTA_CONFIG_BACKUP_VERSION;
    backup.config = cfg;
    backup.display_auto = display_auto_brightness;
    backup.display_brightness = display_max_brightness;
    backup.display_profile = static_cast<uint8_t>(display_power_profile);
    backup.co2_ph = co2_target_ph;
    backup.co2_max_minutes = co2_max_time_minutes;
    backup.ato_max_seconds = water_timeout_seconds;
    backup.leak_action = static_cast<uint8_t>(leak_action);
    backup.crc32 = ota_config_backup_crc(backup);

    Preferences storage;
    if (!storage.begin(OTA_CONFIG_BACKUP_NAMESPACE, false)) {
        return false;
    }
    const bool saved =
        storage.putBytes(OTA_CONFIG_BACKUP_KEY, &backup, sizeof(backup)) ==
        sizeof(backup);
    storage.end();
    return saved;
}

static bool restore_configuration_from_ota_backup_if_needed() {
    Preferences active;
    bool current_valid = false;
    if (active.begin("aquarium", true)) {
        if (active.isKey("uiCfg")) {
            AquariumUiConfig stored = {};
            const size_t bytes = active.getBytes("uiCfg", &stored, sizeof(stored));
            current_valid =
                bytes == sizeof(stored) &&
                stored.magic == UI_CONFIG_MAGIC &&
                stored.crc32 == config_crc(stored);
        }
        active.end();
    }
    if (current_valid) {
        return false;
    }

    OtaConfigBackup backup = {};
    Preferences storage;
    if (!storage.begin(OTA_CONFIG_BACKUP_NAMESPACE, true)) {
        return false;
    }
    const size_t bytes =
        storage.getBytes(OTA_CONFIG_BACKUP_KEY, &backup, sizeof(backup));
    storage.end();
    if (bytes != sizeof(backup) ||
        backup.magic != OTA_CONFIG_BACKUP_MAGIC ||
        backup.version != OTA_CONFIG_BACKUP_VERSION ||
        backup.crc32 != ota_config_backup_crc(backup) ||
        backup.config.magic != UI_CONFIG_MAGIC ||
        backup.config.crc32 != config_crc(backup.config)) {
        return false;
    }

    if (!active.begin("aquarium", false)) {
        return false;
    }
    const bool restored =
        active.putBytes("uiCfg", &backup.config, sizeof(backup.config)) ==
            sizeof(backup.config) &&
        active.putBool("dispAuto", backup.display_auto) == 1U &&
        active.putUChar("dispBr", backup.display_brightness) == 1U &&
        active.putUChar("dispProf", backup.display_profile) == 1U &&
        active.putFloat("co2Ph", backup.co2_ph) == sizeof(float) &&
        active.putUShort("co2Max", backup.co2_max_minutes) == sizeof(uint16_t) &&
        active.putUShort("atoMax", backup.ato_max_seconds) == sizeof(uint16_t) &&
        active.putUChar("leakAct", backup.leak_action) == 1U;
    active.end();
    if (restored) {
        Serial.println("OTA_GUARD: restored controller configuration from backup.");
    }
    return restored;
}

static uint8_t clamp_u8(int value, int low, int high) {
    if (value < low) {
        return static_cast<uint8_t>(low);
    }
    if (value > high) {
        return static_cast<uint8_t>(high);
    }
    return static_cast<uint8_t>(value);
}

static int clamp_ldr_value(int value) {
    if (value < LDR_ADC_MIN) {
        return LDR_ADC_MIN;
    }
    if (value > LDR_ADC_MAX) {
        return LDR_ADC_MAX;
    }
    return value;
}

static const char *display_profile_code(DisplayPowerProfile profile) {
    switch (profile) {
    case DisplayPowerProfile::Timeout60Seconds:
        return "timeout_60s";
    case DisplayPowerProfile::AlwaysOff:
        return "always_off";
    case DisplayPowerProfile::AlwaysOn:
    default:
        return "always_on";
    }
}

static bool parse_display_profile(const String &value, DisplayPowerProfile *out_profile) {
    if (out_profile == nullptr) {
        return false;
    }
    if (value == "always_on") {
        *out_profile = DisplayPowerProfile::AlwaysOn;
        return true;
    }
    if (value == "timeout_60s") {
        *out_profile = DisplayPowerProfile::Timeout60Seconds;
        return true;
    }
    if (value == "always_off") {
        *out_profile = DisplayPowerProfile::AlwaysOff;
        return true;
    }
    return false;
}

static const char *leak_action_code(LeakAction action) {
    switch (action) {
    case LeakAction::AlarmOnly:
        return "alarm";
    case LeakAction::DisableValves:
        return "disable_valves";
    case LeakAction::DisableAll:
    default:
        return "disable_all";
    }
}

static bool parse_leak_action(const String &value, LeakAction *out_action) {
    if (out_action == nullptr) {
        return false;
    }
    if (value == "alarm") {
        *out_action = LeakAction::AlarmOnly;
        return true;
    }
    if (value == "disable_valves") {
        *out_action = LeakAction::DisableValves;
        return true;
    }
    if (value == "disable_all") {
        *out_action = LeakAction::DisableAll;
        return true;
    }
    return false;
}

static void sanitize_extended_settings() {
    display_max_brightness = clamp_u8(display_max_brightness, 10, 100);
    if (static_cast<uint8_t>(display_power_profile) > static_cast<uint8_t>(DisplayPowerProfile::AlwaysOff)) {
        display_power_profile = DisplayPowerProfile::AlwaysOn;
    }
    if (!isfinite(co2_target_ph)) {
        co2_target_ph = FACTORY_CO2_TARGET_PH;
    }
    co2_target_ph = constrain(co2_target_ph, 5.0f, 8.5f);
    co2_max_time_minutes = static_cast<uint16_t>(constrain(static_cast<int>(co2_max_time_minutes), 1, 1440));
    water_timeout_seconds = static_cast<uint16_t>(constrain(static_cast<int>(water_timeout_seconds), 5, 300));
    if (static_cast<uint8_t>(leak_action) > static_cast<uint8_t>(LeakAction::DisableAll)) {
        leak_action = LeakAction::DisableAll;
    }
    cfg.alwaysScreenOn = display_power_profile == DisplayPowerProfile::AlwaysOn;
}

static void apply_display_backlight(int ldr_value, bool ldr_valid) {
    uint8_t desired_percent = display_max_brightness;
    const uint32_t now_ms = millis();

    if (display_power_profile == DisplayPowerProfile::AlwaysOff) {
        desired_percent = 0U;
    } else if (display_power_profile == DisplayPowerProfile::Timeout60Seconds) {
        const uint32_t last_touch_ms = hal_display_last_touch_ms();
        const bool initial_window = last_touch_ms == 0U && now_ms <= 60000UL;
        const bool recently_touched = last_touch_ms != 0U && static_cast<uint32_t>(now_ms - last_touch_ms) <= 60000UL;
        if (!initial_window && !recently_touched) {
            desired_percent = 0U;
        }
    }

    if (desired_percent > 0U && display_auto_brightness && ldr_valid) {
        const uint8_t minimum_percent = display_max_brightness < 15U ? display_max_brightness : 15U;
        const uint32_t span = static_cast<uint32_t>(display_max_brightness - minimum_percent);
        desired_percent = static_cast<uint8_t>(minimum_percent +
            (span * static_cast<uint32_t>(clamp_ldr_value(ldr_value)) + (LDR_ADC_MAX / 2U)) /
            static_cast<uint32_t>(LDR_ADC_MAX));
    }

    if (hal_display_get_brightness() != desired_percent) {
        hal_display_set_brightness(desired_percent);
    }
}

static bool ldr_value_to_light_theme(int ldr_value, bool *out_light_theme) {
    if (out_light_theme == nullptr) {
        return false;
    }

    const int value = clamp_ldr_value(ldr_value);
    const int light_threshold = LDR_THEME_THRESHOLD - LDR_THEME_HYSTERESIS;
    const int dark_threshold = LDR_THEME_THRESHOLD + LDR_THEME_HYSTERESIS;
    if (!ui_light_theme && value <= light_threshold) {
        *out_light_theme = true;
        return true;
    }
    if (ui_light_theme && value >= dark_threshold) {
        *out_light_theme = false;
        return true;
    }

    return false;
}

static uint8_t snap_minute(int minute) {
    const int bounded = constrain(minute, 0, 59);
    const int snapped = ((bounded + (MINUTE_STEP / 2)) / MINUTE_STEP) * MINUTE_STEP;
    return static_cast<uint8_t>(min(snapped, 55));
}

static bool is_leap_year(int year) {
    return (year % 4 == 0) && ((year % 100) != 0 || (year % 400) == 0);
}

static uint8_t days_in_month(int month, int year) {
    switch (month) {
    case 4:
    case 6:
    case 9:
    case 11:
        return 30;
    case 2:
        return is_leap_year(year) ? 29 : 28;
    default:
        return 31;
    }
}

static bool calendar_date_valid(int day, int month, int year) {
    if (year < 2024 || year > 2099 || month < 1 || month > 12) {
        return false;
    }
    return day >= 1 && day <= days_in_month(month, year);
}

static bool clock_fields_valid(int day, int month, int year, int hour, int minute, int second) {
    return calendar_date_valid(day, month, year) &&
           hour >= 0 && hour <= 23 &&
           minute >= 0 && minute <= 59 &&
           second >= 0 && second <= 59;
}

static void set_controller_clock_source(bool reliable, const char *source) {
    controller_clock_reliable = reliable;
    snprintf(controller_clock_source, sizeof(controller_clock_source), "%s",
             source != nullptr && source[0] != '\0' ? source : (reliable ? "manual" : "start"));
}

static uint32_t controller_unix_time(void) {
    if (!clock_fields_valid(clock_day, clock_month, clock_year, clock_hour, clock_minute, clock_second)) {
        return 0;
    }

    struct tm tmv = {};
    tmv.tm_year = clock_year - 1900;
    tmv.tm_mon = clock_month - 1;
    tmv.tm_mday = clock_day;
    tmv.tm_hour = clock_hour;
    tmv.tm_min = clock_minute;
    tmv.tm_sec = clock_second;
    tmv.tm_isdst = -1;
    const time_t epoch = mktime(&tmv);
    return epoch > 0 ? static_cast<uint32_t>(epoch) : 0U;
}

static void gui_load_clock_settings(void) {
    if (!prefs.begin("aquarium", true)) {
        set_controller_clock_source(false, "start");
        return;
    }

    const uint32_t magic = prefs.getUInt("clkMagic", 0);
    const uint16_t version = prefs.getUShort("clkVer", 0);
    const int year = prefs.getUShort("clkYear", 0);
    const int month = prefs.getUChar("clkMonth", 0);
    const int day = prefs.getUChar("clkDay", 0);
    const int hour = prefs.getUChar("clkHour", 255);
    const int minute = prefs.getUChar("clkMin", 255);
    const int second = prefs.getUChar("clkSec", 255);
    const bool reliable = prefs.getBool("clkReliable", false);
    prefs.end();

    if (magic == CLOCK_NVS_MAGIC && version == CLOCK_NVS_VERSION &&
        clock_fields_valid(day, month, year, hour, minute, second)) {
        clock_year = year;
        clock_month = month;
        clock_day = day;
        clock_hour = hour;
        clock_minute = minute;
        clock_second = second;
        set_controller_clock_source(reliable, reliable ? "nvs" : "start");
        Serial.printf("CLOCK: loaded %04d-%02d-%02d %02d:%02d:%02d reliable=%d\n",
                      clock_year, clock_month, clock_day, clock_hour, clock_minute, clock_second,
                      reliable ? 1 : 0);
        return;
    }

    set_controller_clock_source(false, "start");
}

static bool gui_save_clock_settings(bool reliable, const char *source) {
    if (!clock_fields_valid(clock_day, clock_month, clock_year, clock_hour, clock_minute, clock_second)) {
        Serial.println("CLOCK: invalid date/time, save rejected.");
        set_controller_clock_source(false, "invalid");
        return false;
    }
    if (!prefs.begin("aquarium", false)) {
        Serial.println("CLOCK: NVS open failed.");
        return false;
    }

    bool ok = true;
    ok = ok && prefs.putUInt("clkMagic", CLOCK_NVS_MAGIC) > 0;
    ok = ok && prefs.putUShort("clkVer", CLOCK_NVS_VERSION) > 0;
    ok = ok && prefs.putUShort("clkYear", static_cast<uint16_t>(clock_year)) > 0;
    ok = ok && prefs.putUChar("clkMonth", static_cast<uint8_t>(clock_month)) > 0;
    ok = ok && prefs.putUChar("clkDay", static_cast<uint8_t>(clock_day)) > 0;
    ok = ok && prefs.putUChar("clkHour", static_cast<uint8_t>(clock_hour)) > 0;
    ok = ok && prefs.putUChar("clkMin", static_cast<uint8_t>(clock_minute)) > 0;
    ok = ok && prefs.putUChar("clkSec", static_cast<uint8_t>(clock_second)) > 0;
    ok = ok && prefs.putBool("clkReliable", reliable) > 0;
    prefs.end();

    set_controller_clock_source(ok && reliable, ok ? source : "nvs_err");
    Serial.printf("CLOCK: saved reliable=%d source=%s ok=%d\n",
                  reliable ? 1 : 0,
                  controller_clock_source,
                  ok ? 1 : 0);
    return ok;
}

static bool sync_clock_from_ntp(uint32_t timeout_ms) {
    if (!wifi_connected || timeout_ms == 0U) {
        return false;
    }
    configTzTime(NTP_TZ_POLAND, NTP_SERVER_1, NTP_SERVER_2);
    ntp_sync_pending = true;
    ntp_sync_deadline_ms = millis() + timeout_ms;
    ntp_result_until_ms = 0U;
    if (btn_sync_ntp_lbl_global != nullptr) {
        lv_label_set_text(btn_sync_ntp_lbl_global, "Pobieram czas...");
    }
    return true;
}

static void service_ntp_sync(uint32_t now_ms) {
    if (!ntp_sync_pending) {
        if (ntp_result_until_ms != 0U &&
            static_cast<int32_t>(now_ms - ntp_result_until_ms) >= 0) {
            ntp_result_until_ms = 0U;
            if (btn_sync_ntp_lbl_global != nullptr) {
                lv_label_set_text(btn_sync_ntp_lbl_global, "Synchronizuj NTP");
            }
            if (btn_sync_ntp_global != nullptr) {
                lv_obj_set_style_bg_color(
                    btn_sync_ntp_global,
                    lv_color_make(35, 41, 55),
                    0);
            }
        }
        return;
    }

    struct tm timeinfo = {};
    bool success = false;
    if (getLocalTime(&timeinfo, 0U)) {
        const int year = timeinfo.tm_year + 1900;
        const int month = timeinfo.tm_mon + 1;
        const int day = timeinfo.tm_mday;
        const int hour = timeinfo.tm_hour;
        const int minute = timeinfo.tm_min;
        const int second = timeinfo.tm_sec;
        if (clock_fields_valid(day, month, year, hour, minute, second)) {
            clock_year = year;
            clock_month = month;
            clock_day = day;
            clock_hour = hour;
            clock_minute = minute;
            clock_second = second;
            success = gui_save_clock_settings(true, "ntp");
        }
    }

    const bool timed_out = static_cast<int32_t>(now_ms - ntp_sync_deadline_ms) >= 0;
    if (!success && !timed_out) {
        return;
    }

    ntp_sync_pending = false;
    ntp_result_until_ms = now_ms + 2500U;
    if (!success) {
        set_controller_clock_source(false, "ntp_err");
    }
    if (btn_sync_ntp_lbl_global != nullptr) {
        lv_label_set_text(btn_sync_ntp_lbl_global, success ? "Czas zapisany" : "Blad NTP");
    }
    if (btn_sync_ntp_global != nullptr) {
        lv_obj_set_style_bg_color(
            btn_sync_ntp_global,
            success ? lv_color_make(16, 185, 129) : lv_color_make(239, 68, 68),
            0);
    }
}

static bool same_color(lv_color_t color, uint8_t r, uint8_t g, uint8_t b) {
    return color.full == lv_color_make(r, g, b).full;
}

static lv_color_t theme_screen_bg() {
    return ui_light_theme ? lv_color_make(241, 245, 249) : lv_color_make(3, 7, 18);
}

static lv_color_t theme_card_bg() {
    return ui_light_theme ? lv_color_make(255, 255, 255) : lv_color_make(20, 26, 40);
}

static lv_color_t theme_card_border() {
    return ui_light_theme ? lv_color_make(203, 213, 225) : lv_color_make(35, 41, 55);
}

static lv_color_t theme_header_bg() {
    return ui_light_theme ? lv_color_make(224, 242, 254) : lv_color_make(15, 23, 42);
}

static lv_color_t theme_nav_bg() {
    return ui_light_theme ? lv_color_make(255, 255, 255) : lv_color_make(5, 8, 17);
}

static lv_color_t theme_text_main() {
    return ui_light_theme ? lv_color_make(15, 23, 42) : lv_color_white();
}

static lv_color_t theme_text_muted() {
    return ui_light_theme ? lv_color_make(71, 85, 105) : lv_color_make(148, 163, 184);
}

static lv_color_t theme_matrix_item_bg() {
    return ui_light_theme ? lv_color_make(255, 255, 255) : lv_color_make(35, 41, 55);
}

static lv_color_t theme_matrix_pressed_bg() {
    return ui_light_theme ? lv_color_make(224, 242, 254) : lv_color_make(30, 41, 59);
}

static lv_color_t resolve_bg_color(lv_color_t color) {
    if (!ui_light_theme) {
        return color;
    }
    if (same_color(color, 3, 7, 18)) {
        return theme_screen_bg();
    }
    if (same_color(color, 5, 8, 17)) {
        return theme_nav_bg();
    }
    if (same_color(color, 8, 13, 24) || same_color(color, 11, 15, 25)) {
        return lv_color_make(241, 245, 249);
    }
    if (same_color(color, 15, 23, 42) || same_color(color, 20, 26, 40)) {
        return theme_card_bg();
    }
    if (same_color(color, 30, 41, 59) || same_color(color, 35, 41, 55)) {
        return lv_color_make(226, 232, 240);
    }
    if (same_color(color, 30, 38, 56)) {
        return lv_color_make(203, 213, 225); // light gray for pressed menu items in light theme
    }
    return color;
}

static lv_color_t resolve_text_color(lv_color_t color) {
    if (!ui_light_theme) {
        if (same_color(color, 71, 85, 105) ||
            same_color(color, 100, 116, 139) ||
            same_color(color, 148, 163, 184)) {
            return theme_text_muted();
        }
        return color;
    }
    if (same_color(color, 255, 255, 255) || same_color(color, 226, 232, 240)) {
        return theme_text_main();
    }
    if (same_color(color, 71, 85, 105) ||
        same_color(color, 100, 116, 139) ||
        same_color(color, 148, 163, 184)) {
        return theme_text_muted();
    }
    if (same_color(color, 6, 182, 212)) {
        return lv_color_make(3, 105, 161); // Map cyan text to sky-700 in light mode
    }
    if (same_color(color, 16, 185, 129)) {
        return lv_color_make(4, 120, 87);  // Map green text to emerald-700 in light mode
    }
    if (same_color(color, 245, 158, 11)) {
        return lv_color_make(180, 83, 9);   // Map orange/yellow text to amber-700 in light mode
    }
    if (same_color(color, 239, 68, 68)) {
        return lv_color_make(185, 28, 28);  // Map red text to red-700 in light mode
    }
    return color;
}

static bool is_schedule_mode(uint8_t mode) {
    return mode == static_cast<uint8_t>(ScheduleMode::Schedule) ||
           mode == static_cast<uint8_t>(ScheduleMode::AlwaysOn) ||
           mode == static_cast<uint8_t>(ScheduleMode::AlwaysOff);
}

static void apply_developer_demo_profile(AquariumUiConfig &value);
static void apply_factory_schedule(AquariumUiConfig &value);
static uint8_t normalize_aquael_profile(uint8_t profile);
static uint8_t schedule_profile_to_aquael(uint8_t encoded_profile);
static uint8_t aquael_profile_to_schedule(uint8_t profile);
static uint8_t migrate_legacy_light_profile(uint8_t legacy_profile);
static uint8_t migrate_legacy_schedule_light_profile(uint8_t legacy_encoded_profile);
static bool time_pair_matches(uint8_t hour, uint8_t minute, uint8_t expected_hour, uint8_t expected_minute);

static void load_default_config(AquariumUiConfig &out) {
    memset(&out, 0, sizeof(out));
    out.magic = UI_CONFIG_MAGIC;
    out.version = UI_CONFIG_VERSION;
    out.enableHeater = false;
    out.enableAerator = false;
    out.enableEc = false;
    out.enableCo2 = false;
    out.enableWaterLevel = false;
    out.enableLeak = false;
    out.enableFlow = false;
    out.lightMode = static_cast<uint8_t>(ScheduleMode::Schedule);
    out.lightColorMode = 0;
    out.plantLightMode = static_cast<uint8_t>(ScheduleMode::Schedule);
    out.plantLightColorMode = 0;
    out.filterMode = static_cast<uint8_t>(ScheduleMode::Schedule);
    out.airMode = static_cast<uint8_t>(ScheduleMode::AlwaysOff); // OFF by default
    out.lightStartHour = FACTORY_DAY_START_HOUR;
    out.lightStartMinute = FACTORY_DAY_START_MINUTE;
    out.lightEndHour = FACTORY_DAY_END_HOUR;
    out.lightEndMinute = FACTORY_DAY_END_MINUTE;
    out.lightSchedColorMode = 0U; // Automatyczny cykl DAYBREAK -> DAY -> DAYBREAK -> NIGHT.
    
    out.plantStartHour = FACTORY_DAY_START_HOUR;
    out.plantStartMinute = FACTORY_DAY_START_MINUTE;
    out.plantEndHour = FACTORY_DAY_END_HOUR;
    out.plantEndMinute = FACTORY_DAY_END_MINUTE;
    out.plantSchedColorMode = 0U; // Automatyczny cykl DAYBREAK -> DAY -> DAYBREAK -> NIGHT.
    
    out.filterStartHour = FACTORY_FILTER_START_HOUR;
    out.filterStartMinute = FACTORY_FILTER_START_MINUTE;
    out.filterEndHour = FACTORY_FILTER_END_HOUR;
    out.filterEndMinute = FACTORY_FILTER_END_MINUTE;
    
    out.airStartHour = FACTORY_CO2_AIR_START_HOUR;
    out.airStartMinute = FACTORY_CO2_AIR_START_MINUTE;
    out.airEndHour = FACTORY_CO2_AIR_END_HOUR;
    out.airEndMinute = FACTORY_CO2_AIR_END_MINUTE;
    out.heaterMode = static_cast<uint8_t>(HeaterMode::Off); // OFF by default
    out.targetTemp = 25.0f;
    out.tempHysteresis = 0.5f;
    out.feedEnabled = false; // Factory profile enables it below.
    out.feedDays = 0x7F; // Mon-Sun enabled
    out.feedCount = 1; // 1 time per day
    out.feedHour1 = FACTORY_FEED_HOUR;
    out.feedMinute1 = FACTORY_FEED_MINUTE;
    out.feedHour2 = 8;
    out.feedMinute2 = 0;
    out.alwaysScreenOn = false;
    out.ldrThemeEnabled = false;
    out.ldrSensitivity = 50;
    out.manualLightTheme = true; // Default: light theme
    out.showPhSensor = false; // OFF by default
    out.soundEnabled = true;
    out.quietHoursEnabled = true;
    out.quietStartHour = 20;
    out.quietStartMinute = 0;
    out.quietEndHour = 10;
    out.quietEndMinute = 0;
    out.devMode = true;
    out.modemSleep = false;
    apply_factory_schedule(out);
    apply_developer_demo_profile(out);
    out.crc32 = config_crc(out);
}

static void apply_factory_schedule(AquariumUiConfig &value) {
    value.lightMode = static_cast<uint8_t>(ScheduleMode::Schedule);
    value.lightStartHour = FACTORY_DAY_START_HOUR;
    value.lightStartMinute = FACTORY_DAY_START_MINUTE;
    value.lightEndHour = FACTORY_DAY_END_HOUR;
    value.lightEndMinute = FACTORY_DAY_END_MINUTE;
    value.lightSchedColorMode = 0U;

    value.plantLightMode = static_cast<uint8_t>(ScheduleMode::Schedule);
    value.plantStartHour = FACTORY_DAY_START_HOUR;
    value.plantStartMinute = FACTORY_DAY_START_MINUTE;
    value.plantEndHour = FACTORY_DAY_END_HOUR;
    value.plantEndMinute = FACTORY_DAY_END_MINUTE;
    value.plantSchedColorMode = 0U;

    value.filterMode = static_cast<uint8_t>(ScheduleMode::Schedule);
    value.filterStartHour = FACTORY_FILTER_START_HOUR;
    value.filterStartMinute = FACTORY_FILTER_START_MINUTE;
    value.filterEndHour = FACTORY_FILTER_END_HOUR;
    value.filterEndMinute = FACTORY_FILTER_END_MINUTE;

    value.airMode = static_cast<uint8_t>(ScheduleMode::Schedule);
    value.airStartHour = FACTORY_CO2_AIR_START_HOUR;
    value.airStartMinute = FACTORY_CO2_AIR_START_MINUTE;
    value.airEndHour = FACTORY_CO2_AIR_END_HOUR;
    value.airEndMinute = FACTORY_CO2_AIR_END_MINUTE;

    value.enableHeater = true;
    value.heaterMode = static_cast<uint8_t>(HeaterMode::Threshold);
    value.showPhSensor = true;
    value.enableCo2 = true;
    value.enableAerator = true;
    value.enableWaterLevel = true;
    value.enableLeak = true;
    value.feedEnabled = true;
    value.feedDays = 0x7F;
    value.feedCount = 1;
    value.feedHour1 = FACTORY_FEED_HOUR;
    value.feedMinute1 = FACTORY_FEED_MINUTE;
}

static void apply_developer_demo_profile(AquariumUiConfig &value) {
    value.devMode = true;
    value.ldrThemeEnabled = false;
    value.showPhSensor = true;
    value.enableHeater = true;
    value.enableAerator = true;
    value.enableEc = true;
    value.enableCo2 = true;
    value.enableWaterLevel = true;
    value.enableLeak = true;
    value.enableFlow = true;
    value.heaterMode = static_cast<uint8_t>(HeaterMode::Threshold);
    if (value.airMode == static_cast<uint8_t>(ScheduleMode::AlwaysOff)) {
        value.airMode = static_cast<uint8_t>(ScheduleMode::Schedule);
    }
    if (value.targetTemp < 20.0f || value.targetTemp > 29.0f || !isfinite(value.targetTemp)) {
        value.targetTemp = 26.0f;
    }
    if (value.tempHysteresis < 0.2f || value.tempHysteresis > 2.0f || !isfinite(value.tempHysteresis)) {
        value.tempHysteresis = 0.8f;
    }
    if (value.feedDays == 0) {
        value.feedDays = 0x7F;
    }
    if (value.feedCount != 1 && value.feedCount != 2) {
        value.feedCount = 1;
    }
}

static void sanitize_config(AquariumUiConfig &value) {
    value.magic = UI_CONFIG_MAGIC;
    value.version = UI_CONFIG_VERSION;
    if (FORCE_DEVELOPER_MODE) {
        value.devMode = true;
    }

    if (!is_schedule_mode(value.lightMode)) {
        value.lightMode = static_cast<uint8_t>(ScheduleMode::Schedule);
    }
    if (!is_schedule_mode(value.plantLightMode)) {
        value.plantLightMode = static_cast<uint8_t>(ScheduleMode::Schedule);
    }
    if (!is_schedule_mode(value.filterMode)) {
        value.filterMode = static_cast<uint8_t>(ScheduleMode::Schedule);
    }
    if (!is_schedule_mode(value.airMode)) {
        value.airMode = static_cast<uint8_t>(ScheduleMode::Schedule);
    }
    if (value.heaterMode != static_cast<uint8_t>(HeaterMode::Threshold) &&
        value.heaterMode != static_cast<uint8_t>(HeaterMode::Off)) {
        value.heaterMode = static_cast<uint8_t>(HeaterMode::Off);
    }

    value.lightColorMode = normalize_aquael_profile(value.lightColorMode);
    value.plantLightColorMode = normalize_aquael_profile(value.plantLightColorMode);

    value.lightStartHour = clamp_u8(value.lightStartHour, 0, 23);
    value.lightStartMinute = snap_minute(value.lightStartMinute);
    value.lightEndHour = clamp_u8(value.lightEndHour, 0, 23);
    value.lightEndMinute = snap_minute(value.lightEndMinute);
    value.lightSchedColorMode = clamp_u8(value.lightSchedColorMode, 0, 3);
    
    value.plantStartHour = clamp_u8(value.plantStartHour, 0, 23);
    value.plantStartMinute = snap_minute(value.plantStartMinute);
    value.plantEndHour = clamp_u8(value.plantEndHour, 0, 23);
    value.plantEndMinute = snap_minute(value.plantEndMinute);
    value.plantSchedColorMode = clamp_u8(value.plantSchedColorMode, 0, 3);
    
    value.filterStartHour = clamp_u8(value.filterStartHour, 0, 23);
    value.filterStartMinute = snap_minute(value.filterStartMinute);
    value.filterEndHour = clamp_u8(value.filterEndHour, 0, 23);
    value.filterEndMinute = snap_minute(value.filterEndMinute);
    
    value.airStartHour = clamp_u8(value.airStartHour, 0, 23);
    value.airStartMinute = snap_minute(value.airStartMinute);
    value.airEndHour = clamp_u8(value.airEndHour, 0, 23);
    value.airEndMinute = snap_minute(value.airEndMinute);


    if (!isfinite(value.targetTemp)) {
        value.targetTemp = 25.0f;
    }
    value.targetTemp = constrain(value.targetTemp, 18.0f, 30.0f);

    if (!isfinite(value.tempHysteresis)) {
        value.tempHysteresis = 0.5f;
    }
    value.tempHysteresis = constrain(value.tempHysteresis, 0.1f, 5.0f);
    value.tempHysteresis = roundf(value.tempHysteresis * 10.0f) / 10.0f;

    value.feedDays = value.feedDays & 0x7F;
    if (value.feedCount != 1 && value.feedCount != 2) value.feedCount = 1;
    value.feedHour1 = clamp_u8(value.feedHour1, 0, 23);
    value.feedMinute1 = snap_minute(value.feedMinute1);
    value.feedHour2 = clamp_u8(value.feedHour2, 0, 23);
    value.feedMinute2 = snap_minute(value.feedMinute2);

    value.ldrSensitivity = clamp_u8(value.ldrSensitivity, 0, 100);
    value.quietStartHour = clamp_u8(value.quietStartHour, 0, 23);
    value.quietStartMinute = snap_minute(value.quietStartMinute);
    value.quietEndHour = clamp_u8(value.quietEndHour, 0, 23);
    value.quietEndMinute = snap_minute(value.quietEndMinute);

    if (value.devMode) {
        apply_developer_demo_profile(value);
    }
    value.crc32 = config_crc(value);
}

static void gui_app_save_settings() {
    sanitize_config(cfg);
    sanitize_extended_settings();
    if (!prefs.begin("aquarium", false)) {
        Serial.println("GUI: NVS open failed while saving settings.");
        return;
    }
    const size_t written = prefs.putBytes("uiCfg", &cfg, sizeof(cfg));
    const bool extended_written =
        prefs.putBool("dispAuto", display_auto_brightness) == 1U &&
        prefs.putUChar("dispBr", display_max_brightness) == 1U &&
        prefs.putUChar("dispProf", static_cast<uint8_t>(display_power_profile)) == 1U &&
        prefs.putFloat("co2Ph", co2_target_ph) == sizeof(float) &&
        prefs.putUShort("co2Max", co2_max_time_minutes) == sizeof(uint16_t) &&
        prefs.putUShort("atoMax", water_timeout_seconds) == sizeof(uint16_t) &&
        prefs.putUChar("leakAct", static_cast<uint8_t>(leak_action)) == 1U;
    prefs.end();
    if (written != sizeof(cfg) || !extended_written) {
        Serial.printf("GUI: NVS write failed, config=%u/%u extras=%s.\n",
                      static_cast<unsigned>(written),
                      static_cast<unsigned>(sizeof(cfg)),
                      extended_written ? "ok" : "error");
        return;
    }
    Serial.println("GUI: NVS settings saved.");
}

static void toast_y_anim_cb(void *var, int32_t val) {
    lv_obj_set_y((lv_obj_t *)var, val);
}

static void toast_hide_anim_ready_cb(lv_anim_t *anim) {
    lv_obj_t *toast = (lv_obj_t *)anim->var;
    if (toast != nullptr && lv_obj_is_valid(toast)) {
        lv_obj_del(toast);
    }
}

static void toast_timer_cb(lv_timer_t *timer) {
    lv_obj_t *toast = (lv_obj_t *)timer->user_data;
    if (toast != nullptr && lv_obj_is_valid(toast)) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, toast);
        lv_anim_set_values(&a, lv_obj_get_y(toast), -60);
        lv_anim_set_time(&a, 350);
        lv_anim_set_exec_cb(&a, toast_y_anim_cb);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
        lv_anim_set_ready_cb(&a, toast_hide_anim_ready_cb);
        lv_anim_start(&a);
    }
    lv_timer_del(timer);
}

static void show_save_toast(const char *msg) {
    play_system_sound(SoundType::Save);
    if (!ensure_runtime_ui_heap("SaveToast", UI_RUNTIME_MODAL_MIN_FREE, UI_RUNTIME_BIGGEST_MIN)) {
        return;
    }
    static lv_obj_t *active_toast = nullptr;
    if (active_toast != nullptr && lv_obj_is_valid(active_toast)) {
        lv_obj_del(active_toast);
        active_toast = nullptr;
    }
    
    active_toast = lv_obj_create(lv_scr_act());
    lv_obj_set_size(active_toast, 240, 42);
    lv_obj_align(active_toast, LV_ALIGN_TOP_MID, 0, -60);
    lv_obj_clear_flag(active_toast, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_set_style_pad_all(active_toast, 0, 0);
    lv_obj_set_style_radius(active_toast, 8, 0);
    
    if (ui_light_theme) {
        lv_obj_set_style_bg_color(active_toast, lv_color_make(241, 245, 249), 0);
        lv_obj_set_style_border_color(active_toast, lv_color_make(16, 185, 129), 0);
        lv_obj_set_style_border_width(active_toast, 1, 0);
    } else {
        lv_obj_set_style_bg_color(active_toast, lv_color_make(15, 23, 42), 0);
        lv_obj_set_style_bg_opa(active_toast, LV_OPA_90, 0);
        lv_obj_set_style_border_color(active_toast, lv_color_make(16, 185, 129), 0);
        lv_obj_set_style_border_width(active_toast, 1, 0);
    }
    
    lv_obj_set_style_shadow_width(active_toast, 12, 0);
    lv_obj_set_style_shadow_color(active_toast, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(active_toast, LV_OPA_30, 0);
    
    lv_obj_t *stripe = lv_obj_create(active_toast);
    lv_obj_set_size(stripe, 4, 42);
    lv_obj_align(stripe, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(stripe, lv_color_make(16, 185, 129), 0);
    lv_obj_set_style_border_width(stripe, 0, 0);
    lv_obj_clear_flag(stripe, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *icon = lv_label_create(active_toast);
    lv_label_set_text(icon, LV_SYMBOL_OK);
    lv_obj_set_style_text_color(icon, lv_color_make(16, 185, 129), 0);
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 14, 0);
    
    lv_obj_t *label = lv_label_create(active_toast);
    lv_label_set_text(label, msg);
    lv_obj_set_style_text_color(label, ui_light_theme ? lv_color_make(15, 23, 42) : lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 36, 0);
    
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, active_toast);
    lv_anim_set_values(&a, -60, 15);
    lv_anim_set_time(&a, 450);
    lv_anim_set_exec_cb(&a, toast_y_anim_cb);
    lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
    lv_anim_start(&a);
    
    lv_timer_create(toast_timer_cb, 1800, active_toast);
}

static void gui_app_load_settings() {
    restore_configuration_from_ota_backup_if_needed();
    load_default_config(cfg);
    display_auto_brightness = true;
    display_max_brightness = 100U;
    display_power_profile = DisplayPowerProfile::AlwaysOn;
    co2_target_ph = FACTORY_CO2_TARGET_PH;
    co2_max_time_minutes = 540U;
    water_timeout_seconds = 120U;
    leak_action = LeakAction::DisableAll;
    if (!prefs.begin("aquarium", false)) {
        Serial.println("GUI: NVS open failed, defaults loaded.");
        sanitize_extended_settings();
        apply_display_backlight(0, false);
        return;
    }

    // Increment boot count
    boot_count_val = prefs.getUInt("bootCount", 0);
    boot_count_val++;
    prefs.putUInt("bootCount", boot_count_val);
    display_auto_brightness = prefs.getBool("dispAuto", display_auto_brightness);
    display_max_brightness = prefs.getUChar("dispBr", display_max_brightness);
    display_power_profile = static_cast<DisplayPowerProfile>(
        prefs.getUChar("dispProf", static_cast<uint8_t>(display_power_profile)));
    co2_target_ph = prefs.getFloat("co2Ph", co2_target_ph);
    co2_max_time_minutes = prefs.getUShort("co2Max", co2_max_time_minutes);
    water_timeout_seconds = prefs.getUShort("atoMax", water_timeout_seconds);
    leak_action = static_cast<LeakAction>(prefs.getUChar("leakAct", static_cast<uint8_t>(leak_action)));

    bool loaded = false;
    bool save_after_load = false;
    if (prefs.isKey("uiCfg")) {
        AquariumUiConfig stored = {};
        const size_t bytes = prefs.getBytes("uiCfg", &stored, sizeof(stored));
        if (bytes == sizeof(stored) && stored.magic == UI_CONFIG_MAGIC &&
            stored.crc32 == config_crc(stored)) {
            if (stored.version == UI_CONFIG_VERSION) {
                cfg = stored;
                loaded = true;
            } else if (stored.version == UI_CONFIG_VERSION_DUAL_AQUAEL_DN) {
                cfg = stored;
                cfg.version = UI_CONFIG_VERSION;
                const bool legacy_factory_light2 =
                    cfg.plantLightMode == static_cast<uint8_t>(ScheduleMode::Schedule) &&
                    time_pair_matches(cfg.plantStartHour, cfg.plantStartMinute,
                                      FACTORY_DAYBREAK_MORNING_END_HOUR,
                                      FACTORY_DAYBREAK_MORNING_END_MINUTE) &&
                    time_pair_matches(cfg.plantEndHour, cfg.plantEndMinute,
                                      FACTORY_DAY_END_PROFILE_HOUR,
                                      FACTORY_DAY_END_PROFILE_MINUTE);
                if (legacy_factory_light2) {
                    cfg.plantStartHour = FACTORY_DAY_START_HOUR;
                    cfg.plantStartMinute = FACTORY_DAY_START_MINUTE;
                    cfg.plantEndHour = FACTORY_DAY_END_HOUR;
                    cfg.plantEndMinute = FACTORY_DAY_END_MINUTE;
                    cfg.plantSchedColorMode = 0U;
                }
                if (time_pair_matches(cfg.lightStartHour, cfg.lightStartMinute,
                                      FACTORY_DAY_START_HOUR, FACTORY_DAY_START_MINUTE) &&
                    time_pair_matches(cfg.lightEndHour, cfg.lightEndMinute,
                                      FACTORY_DAY_END_HOUR, FACTORY_DAY_END_MINUTE)) {
                    cfg.lightSchedColorMode = 0U;
                }
                loaded = true;
                save_after_load = true;
                Serial.println("GUI: NVS migrated to two independent Aquael D&N lights.");
            } else if (stored.version == UI_CONFIG_VERSION_FACTORY_SCHEDULE_BASE) {
                cfg = stored;
                cfg.version = UI_CONFIG_VERSION;
                apply_factory_schedule(cfg);
                loaded = true;
                save_after_load = true;
                Serial.println("GUI: NVS migrated to factory 24h schedule.");
            } else if (stored.version == UI_CONFIG_VERSION_AQUAEL_DN) {
                cfg = stored;
                cfg.version = UI_CONFIG_VERSION;
                cfg.lightColorMode = migrate_legacy_light_profile(stored.lightColorMode);
                cfg.plantLightColorMode = migrate_legacy_light_profile(stored.plantLightColorMode);
                cfg.lightSchedColorMode = migrate_legacy_schedule_light_profile(stored.lightSchedColorMode);
                cfg.plantSchedColorMode = migrate_legacy_schedule_light_profile(stored.plantSchedColorMode);
                apply_factory_schedule(cfg);
                loaded = true;
                save_after_load = true;
                Serial.println("GUI: NVS migrated to Aquael DAY/DAYBREAK/NIGHT profile.");
            } else if (stored.version == UI_CONFIG_VERSION_DEV_NO_SENSORS) {
                cfg = stored;
                cfg.devMode = true;
                cfg.ldrThemeEnabled = false;
                cfg.showPhSensor = false;
                cfg.enableHeater = false;
                cfg.enableAerator = false;
                cfg.enableEc = false;
                cfg.enableCo2 = false;
                cfg.enableWaterLevel = false;
                cfg.enableLeak = false;
                cfg.enableFlow = false;
                loaded = true;
                save_after_load = true;
                Serial.println("GUI: NVS migrated to developer no-sensors profile.");
            } else if (stored.version == UI_CONFIG_VERSION_LDR_DEFAULT_OFF) {
                cfg = stored;
                cfg.devMode = true;
                cfg.ldrThemeEnabled = false;
                cfg.showPhSensor = false;
                cfg.enableHeater = false;
                cfg.enableAerator = false;
                cfg.enableEc = false;
                cfg.enableCo2 = false;
                cfg.enableWaterLevel = false;
                cfg.enableLeak = false;
                cfg.enableFlow = false;
                loaded = true;
                save_after_load = true;
                Serial.println("GUI: NVS migrated from legacy LDR config to developer no-sensors profile.");
            }
        }
    }
    prefs.end();

    const bool force_dev_save = loaded && FORCE_DEVELOPER_MODE && !cfg.devMode;
    const uint32_t crc_before_sanitize = loaded ? config_crc(cfg) : 0UL;
    sanitize_config(cfg);
    sanitize_extended_settings();
    if (loaded && cfg.crc32 != crc_before_sanitize) {
        save_after_load = true;
        Serial.println("GUI: NVS sanitized to active firmware profile.");
    }
    if (force_dev_save) {
        save_after_load = true;
        Serial.println("GUI: Developer mode forced by firmware profile.");
    }
    if (!loaded) {
        gui_app_save_settings();
        Serial.println("GUI: NVS defaults initialized.");
    } else if (save_after_load) {
        gui_app_save_settings();
        Serial.println("GUI: NVS settings migrated.");
    } else {
        Serial.println("GUI: NVS settings loaded.");
    }
    gui_load_clock_settings();
    apply_display_backlight(last_ldr_value, last_ldr_valid);
}

static const char *mode_label(uint8_t mode) {
    switch (static_cast<ScheduleMode>(mode)) {
    case ScheduleMode::AlwaysOn:
        return "ON";
    case ScheduleMode::AlwaysOff:
        return "OFF";
    case ScheduleMode::Schedule:
    default:
        return "AUTO";
    }
}

static const char *feed_mode_label(uint8_t mode) {
    switch (mode) {
    case 1:
        return "Co 2 dni";
    case 2:
        return "Co 3 dni";
    case 3:
        return "Co tydzien";
    case 4:
        return "Tylko recznie";
    default:
        return "Codziennie";
    }
}

static uint8_t normalize_aquael_profile(uint8_t profile) {
    return profile > 2U ? 0U : profile;
}

static uint8_t schedule_profile_to_aquael(uint8_t encoded_profile) {
    if (encoded_profile == 0U) {
        return 0U;
    }
    return normalize_aquael_profile(static_cast<uint8_t>(encoded_profile - 1U));
}

static uint8_t aquael_profile_to_schedule(uint8_t profile) {
    return static_cast<uint8_t>(normalize_aquael_profile(profile) + 1U);
}

static const char *aquael_profile_code(uint8_t profile) {
    switch (normalize_aquael_profile(profile)) {
    case 1:
        return "daybreak";
    case 2:
        return "night";
    default:
        return "day";
    }
}

static const char *light_color_mode_label(uint8_t profile) {
    switch (normalize_aquael_profile(profile)) {
    case 1:
        return "DAYBREAK";
    case 2:
        return "NIGHT";
    default:
        return "DAY";
    }
}

static const char *aquael_profile_description(uint8_t profile) {
    switch (normalize_aquael_profile(profile)) {
    case 1:
        return "50% dzienne + niebieskie";
    case 2:
        return "niebieska poswiata nocna";
    default:
        return "najmocniejsze swiatlo dzienne";
    }
}

static uint8_t migrate_legacy_light_profile(uint8_t legacy_profile) {
    switch (legacy_profile) {
    case 1:
        return 1U; // MIX was closest to the lower DAYBREAK visual profile.
    case 2:
        return 0U; // Old plant profile has no D&N equivalent; DAY is the plant-growth mode.
    default:
        return 0U;
    }
}

static uint8_t migrate_legacy_schedule_light_profile(uint8_t legacy_encoded_profile) {
    switch (legacy_encoded_profile) {
    case 1:
        return aquael_profile_to_schedule(1U);
    case 2:
    case 3:
    default:
        return aquael_profile_to_schedule(0U);
    }
}

static uint16_t to_minutes(uint8_t hour, uint8_t minute) {
    return aquarium::minutes_since_midnight({hour, minute});
}

static bool time_pair_matches(uint8_t hour, uint8_t minute, uint8_t expected_hour, uint8_t expected_minute) {
    return hour == expected_hour && minute == expected_minute;
}

static bool config_uses_factory_light_window() {
    return time_pair_matches(cfg.lightStartHour, cfg.lightStartMinute, FACTORY_DAY_START_HOUR, FACTORY_DAY_START_MINUTE) &&
           time_pair_matches(cfg.lightEndHour, cfg.lightEndMinute, FACTORY_DAY_END_HOUR, FACTORY_DAY_END_MINUTE) &&
           cfg.lightSchedColorMode == 0U;
}

static bool config_uses_factory_light2_window() {
    return time_pair_matches(cfg.plantStartHour, cfg.plantStartMinute,
                             FACTORY_DAY_START_HOUR, FACTORY_DAY_START_MINUTE) &&
           time_pair_matches(cfg.plantEndHour, cfg.plantEndMinute,
                             FACTORY_DAY_END_HOUR, FACTORY_DAY_END_MINUTE) &&
           cfg.plantSchedColorMode == 0U;
}

static bool factory_light_profile_at(uint16_t now, uint8_t *profile) {
    if (profile == nullptr) {
        return false;
    }
    aquarium::LightProfile resolved = aquarium::LightProfile::Day;
    const bool active = aquarium::factory_light_profile_at(now, &resolved);
    *profile = static_cast<uint8_t>(resolved);
    return active;
}

static bool is_within_window(uint16_t now, uint8_t startHour, uint8_t startMinute,
                             uint8_t endHour, uint8_t endMinute) {
    return aquarium::is_within_window(now, {{startHour, startMinute}, {endHour, endMinute}});
}

static bool schedule_active(uint8_t mode, uint16_t now, uint8_t startHour,
                            uint8_t startMinute, uint8_t endHour,
                            uint8_t endMinute) {
    return aquarium::schedule_active(static_cast<ScheduleMode>(mode),
                                     now,
                                     {{startHour, startMinute}, {endHour, endMinute}});
}

static bool is_quiet_hours() {
    if (!cfg.quietHoursEnabled) {
        return false;
    }
    uint16_t current_min = static_cast<uint16_t>(clock_hour) * 60U + static_cast<uint16_t>(clock_minute);
    return is_within_window(current_min, cfg.quietStartHour, cfg.quietStartMinute, cfg.quietEndHour, cfg.quietEndMinute);
}

static void ota_portal_send_json_escaped(const char *text);

constexpr uint32_t ECO_RTC_MAGIC = 0x41514543UL;
constexpr uint16_t ECO_RTC_VERSION = 1;
constexpr uint16_t ECO_DEEP_GUARD_MINUTES = 30;
constexpr uint32_t ECO_DEEP_SLEEP_FALLBACK_SECONDS = 30UL;
constexpr uint32_t ECO_DEEP_SLEEP_MIN_SECONDS = 60UL;
constexpr uint32_t ECO_DEEP_SLEEP_MAX_SECONDS = 12UL * 60UL * 60UL;
constexpr float ECO_TEMP_SAFE_LOW = 20.0f;
constexpr float ECO_TEMP_SAFE_HIGH = 28.0f;

enum EcoBlocker : uint16_t {
    ECO_BLOCK_NONE = 0,
    ECO_BLOCK_TIME_INVALID = 1U << 0,
    ECO_BLOCK_WINDOW_CLOSED = 1U << 1,
    ECO_BLOCK_WIFI_ACTIVE = 1U << 2,
    ECO_BLOCK_OTA_ACTIVE = 1U << 3,
    ECO_BLOCK_OUTPUTS_ACTIVE = 1U << 4,
    ECO_BLOCK_HEATER_ACTIVE = 1U << 5,
    ECO_BLOCK_TEMP_UNSAFE = 1U << 6,
    ECO_BLOCK_FEED_SOON = 1U << 7
};

struct EcoRtcState {
    uint32_t magic;
    uint16_t version;
    uint16_t lastBlockers;
    uint32_t bootCounter;
    uint32_t lastSleepPlanMs;
    uint32_t plannedWakeAfterSec;
    uint8_t lastWakeCause;
    bool deepReady;
};

struct EcoRuntimeStatus {
    bool quietWindow;
    bool safeEcoActive;
    bool deepReady;
    bool rtcReady;
    uint16_t blockers;
    uint32_t plannedWakeAfterSec;
};

static RTC_DATA_ATTR EcoRtcState eco_rtc_state;

static bool controller_clock_valid() {
    return controller_clock_reliable &&
           clock_fields_valid(clock_day, clock_month, clock_year,
                              clock_hour, clock_minute, clock_second);
}

static uint16_t current_clock_minutes() {
    return static_cast<uint16_t>(constrain(clock_hour, 0, 23)) * 60U +
           static_cast<uint16_t>(constrain(clock_minute, 0, 59));
}

static uint32_t seconds_until_window_end(uint16_t now, uint8_t endHour, uint8_t endMinute) {
    const uint16_t end = to_minutes(endHour, endMinute);
    uint16_t minutes = 0;
    if (end > now) {
        minutes = static_cast<uint16_t>(end - now);
    } else {
        minutes = static_cast<uint16_t>((24U * 60U) - now + end);
    }
    return static_cast<uint32_t>(minutes) * 60UL;
}

static bool feed_time_is_soon(uint16_t now, uint8_t hour, uint8_t minute) {
    const uint16_t target = to_minutes(hour, minute);
    const uint16_t delta = target >= now
        ? static_cast<uint16_t>(target - now)
        : static_cast<uint16_t>((24U * 60U) - now + target);
    return delta <= ECO_DEEP_GUARD_MINUTES;
}

static bool eco_feeding_soon(uint16_t now) {
    if (!cfg.feedEnabled) {
        return false;
    }
    const int wday = get_weekday(clock_day, clock_month, clock_year);
    const int bit_idx = (wday == 0) ? 6 : (wday - 1);
    if ((cfg.feedDays & (1 << bit_idx)) == 0) {
        return false;
    }
    if (feed_time_is_soon(now, cfg.feedHour1, cfg.feedMinute1)) {
        return true;
    }
    return cfg.feedCount == 2 && feed_time_is_soon(now, cfg.feedHour2, cfg.feedMinute2);
}

static bool eco_outputs_active() {
    return runtime.lightOn || runtime.plantLightOn || runtime.filterOn || runtime.airOn;
}

static bool wifi_radio_active_for_sleep() {
    const wifi_mode_t mode = WiFi.getMode();
    return wifi_ota_active || is_connecting || mode == WIFI_AP || mode == WIFI_AP_STA;
}

static void eco_rtc_ensure_state() {
    if (eco_rtc_state.magic == ECO_RTC_MAGIC && eco_rtc_state.version == ECO_RTC_VERSION) {
        return;
    }
    memset(&eco_rtc_state, 0, sizeof(eco_rtc_state));
    eco_rtc_state.magic = ECO_RTC_MAGIC;
    eco_rtc_state.version = ECO_RTC_VERSION;
    eco_rtc_state.lastWakeCause = static_cast<uint8_t>(esp_sleep_get_wakeup_cause());
}

static EcoRuntimeStatus eco_collect_status() {
    eco_rtc_ensure_state();

    EcoRuntimeStatus status = {};
    const bool time_ok = controller_clock_valid();
    const uint16_t now = current_clock_minutes();
    status.quietWindow = time_ok && cfg.quietHoursEnabled &&
                         is_within_window(now, cfg.quietStartHour, cfg.quietStartMinute,
                                          cfg.quietEndHour, cfg.quietEndMinute);
    status.safeEcoActive = status.quietWindow;
    status.rtcReady = time_ok && eco_rtc_state.magic == ECO_RTC_MAGIC &&
                      eco_rtc_state.version == ECO_RTC_VERSION;

    if (!time_ok) {
        status.blockers |= ECO_BLOCK_TIME_INVALID;
    }
    if (!status.quietWindow) {
        status.blockers |= ECO_BLOCK_WINDOW_CLOSED;
    }
    if (wifi_ota_active) {
        status.blockers |= ECO_BLOCK_OTA_ACTIVE;
    }
    if (wifi_radio_active_for_sleep()) {
        status.blockers |= ECO_BLOCK_WIFI_ACTIVE;
    }
    if (eco_outputs_active()) {
        status.blockers |= ECO_BLOCK_OUTPUTS_ACTIVE;
    }
    if (runtime.heaterOn) {
        status.blockers |= ECO_BLOCK_HEATER_ACTIVE;
    }
    if (!isfinite(runtime.lastTemp) || runtime.lastTemp < ECO_TEMP_SAFE_LOW || runtime.lastTemp > ECO_TEMP_SAFE_HIGH) {
        status.blockers |= ECO_BLOCK_TEMP_UNSAFE;
    }
    if (time_ok && eco_feeding_soon(now)) {
        status.blockers |= ECO_BLOCK_FEED_SOON;
    }

    status.deepReady = status.rtcReady && status.blockers == ECO_BLOCK_NONE;
    status.plannedWakeAfterSec = status.quietWindow
        ? seconds_until_window_end(now, cfg.quietEndHour, cfg.quietEndMinute)
        : 0UL;

    eco_rtc_state.lastBlockers = status.blockers;
    eco_rtc_state.deepReady = status.deepReady;
    eco_rtc_state.bootCounter = boot_count_val;
    eco_rtc_state.lastSleepPlanMs = millis();
    eco_rtc_state.plannedWakeAfterSec = status.plannedWakeAfterSec;
    return status;
}

static uint32_t planned_deep_sleep_seconds(const EcoRuntimeStatus &status) {
    if (!status.deepReady || status.plannedWakeAfterSec < ECO_DEEP_SLEEP_MIN_SECONDS) {
        return ECO_DEEP_SLEEP_FALLBACK_SECONDS;
    }
    if (status.plannedWakeAfterSec > ECO_DEEP_SLEEP_MAX_SECONDS) {
        return ECO_DEEP_SLEEP_MAX_SECONDS;
    }
    return status.plannedWakeAfterSec;
}

static void append_blocker_text(char *buf, size_t len, bool &first, const char *text) {
    if (buf == nullptr || len == 0 || text == nullptr) {
        return;
    }
    const size_t used = strlen(buf);
    if (used >= len - 1) {
        return;
    }
    snprintf(buf + used, len - used, "%s%s", first ? "" : ",", text);
    first = false;
}

static void eco_blockers_to_csv(uint16_t blockers, char *buf, size_t len) {
    if (buf == nullptr || len == 0) {
        return;
    }
    buf[0] = '\0';
    bool first = true;
    if ((blockers & ECO_BLOCK_TIME_INVALID) != 0) append_blocker_text(buf, len, first, "czas");
    if ((blockers & ECO_BLOCK_WINDOW_CLOSED) != 0) append_blocker_text(buf, len, first, "okno");
    if ((blockers & ECO_BLOCK_WIFI_ACTIVE) != 0) append_blocker_text(buf, len, first, "wifi");
    if ((blockers & ECO_BLOCK_OTA_ACTIVE) != 0) append_blocker_text(buf, len, first, "ota");
    if ((blockers & ECO_BLOCK_OUTPUTS_ACTIVE) != 0) append_blocker_text(buf, len, first, "wyjscia");
    if ((blockers & ECO_BLOCK_HEATER_ACTIVE) != 0) append_blocker_text(buf, len, first, "grzalka");
    if ((blockers & ECO_BLOCK_TEMP_UNSAFE) != 0) append_blocker_text(buf, len, first, "temp");
    if ((blockers & ECO_BLOCK_FEED_SOON) != 0) append_blocker_text(buf, len, first, "karmienie");
    if (first) {
        snprintf(buf, len, "brak");
    }
}

static void ota_portal_send_eco_blockers_json(uint16_t blockers) {
    ota_http_server.sendContent("[");
    bool first = true;
    auto send_code = [&first, blockers](EcoBlocker bit, const char *code) {
        if ((blockers & bit) == 0) {
            return;
        }
        if (!first) {
            ota_http_server.sendContent(",");
        }
        first = false;
        ota_portal_send_json_escaped(code);
    };
    send_code(ECO_BLOCK_TIME_INVALID, "time_invalid");
    send_code(ECO_BLOCK_WINDOW_CLOSED, "window_closed");
    send_code(ECO_BLOCK_WIFI_ACTIVE, "wifi_active");
    send_code(ECO_BLOCK_OTA_ACTIVE, "ota_active");
    send_code(ECO_BLOCK_OUTPUTS_ACTIVE, "outputs_active");
    send_code(ECO_BLOCK_HEATER_ACTIVE, "heater_active");
    send_code(ECO_BLOCK_TEMP_UNSAFE, "temp_unsafe");
    send_code(ECO_BLOCK_FEED_SOON, "feed_soon");
    ota_http_server.sendContent("]");
}

static void speaker_ledc_attach() {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(SPEAKER_PIN, 2000, 8);
#else
    ledcSetup(SPEAKER_LEDC_CHANNEL, 2000, 8);
    ledcAttachPin(SPEAKER_PIN, SPEAKER_LEDC_CHANNEL);
#endif
}

static void speaker_ledc_write_tone(uint32_t frequency) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWriteTone(SPEAKER_PIN, frequency);
#else
    ledcWriteTone(SPEAKER_LEDC_CHANNEL, frequency);
#endif
}

static void speaker_ledc_write(uint32_t duty) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(SPEAKER_PIN, duty);
#else
    ledcWrite(SPEAKER_LEDC_CHANNEL, duty);
#endif
}

static void init_audio_system() {
    if (audio_initialized) return;
    speaker_ledc_attach();
    audio_initialized = true;
}

static void play_beep(uint32_t frequency, uint32_t durationMs, uint8_t volumePercent) {
    init_audio_system();
    uint32_t duty = (volumePercent * 128) / 100;
    if (duty == 0) duty = 1;
    
    speaker_ledc_write_tone(frequency);
    speaker_ledc_write(duty);
    vTaskDelay(pdMS_TO_TICKS(durationMs));
    speaker_ledc_write(0);
}

static void play_audio_effect_blocking(AudioEffect effect) {
    switch (effect) {
    case AudioEffect::Click:
        play_beep(3800, 8, 2); // Tiny, high-pitched mechanical click (2% volume)
        break;
    case AudioEffect::Save:
        play_beep(2800, 20, 3);
        vTaskDelay(pdMS_TO_TICKS(15U));
        play_beep(3500, 35, 3);
        break;
    case AudioEffect::Warning:
        play_beep(1200, 80, 2);
        break;
    case AudioEffect::Mario: {
        static constexpr uint16_t NOTES[] = {659U, 659U, 659U, 523U, 659U, 784U, 392U};
        static constexpr uint16_t DURATIONS[] = {100U, 100U, 100U, 100U, 100U, 100U, 100U};
        static constexpr uint16_t GAPS[] = {50U, 100U, 100U, 50U, 150U, 300U, 0U};
        for (size_t i = 0U; i < sizeof(NOTES) / sizeof(NOTES[0]); ++i) {
            play_beep(NOTES[i], DURATIONS[i], 3U);
            if (GAPS[i] > 0U) {
                vTaskDelay(pdMS_TO_TICKS(GAPS[i]));
            }
        }
        break;
    }
    }
}

static void enqueue_audio_effect(AudioEffect effect) {
    if (audio_queue != nullptr) {
        xQueueSend(audio_queue, &effect, 0U);
    }
}

static void play_system_sound(SoundType type) {
    if (!cfg.soundEnabled || is_quiet_hours()) {
        return;
    }

    switch (type) {
    case SoundType::Click:
        enqueue_audio_effect(AudioEffect::Click);
        break;
    case SoundType::Save:
        enqueue_audio_effect(AudioEffect::Save);
        break;
    case SoundType::Warning:
        enqueue_audio_effect(AudioEffect::Warning);
        break;
    }
}

static void play_mario_tune() {
    enqueue_audio_effect(AudioEffect::Mario);
}

// RTTTL melody definitions
static const char* RTTTL_SONGS[] = {
    "Popcorn:d=4,o=5,b=160:8c6,8a#,8c6,8g,8d#,8g,c,8c6,8a#,8c6,8g,8d#,8g,c,8c6,8d6,8d#6,16c6,8d#6,16c6,8d#6,8d6,16a#,8d6,16a#,8d6,8c6,8a#,8g,8a#,c6",
    "ducktales:d=4,o=5,b=112:8e6,8e6,16p,16g6,8b6,g#6,p,8e6,8d6,8c6,8d6,8e6,8d6,8c6,8d6,8e6,8e6,16p,16g6,8b6,g#6,p,8e6,8d6,8c6,8d6,8e6,8d6,8c6,8g6,8e6,8e6",
    "Contra:d=8,o=5,b=160:f,f,f,a#.,c6,f,f,f,a#.,c6,f,f,f,g,g#,g,f,d,c,d,f,d,c,a#4"
};

static int get_note_index(char c, bool sharp) {
    int idx = -1;
    switch (c) {
        case 'c': idx = 0; break;
        case 'd': idx = 2; break;
        case 'e': idx = 4; break;
        case 'f': idx = 5; break;
        case 'g': idx = 7; break;
        case 'a': idx = 9; break;
        case 'b': idx = 11; break;
        default: return -1;
    }
    if (sharp) idx++;
    return idx;
}

static void play_rtttl(const char *rtttl_str) {
    if (rtttl_str == nullptr) return;
    init_audio_system();

    const char *p = rtttl_str;
    while (*p && *p != ':') p++;
    if (*p == ':') p++;

    int default_dur = 4;
    int default_oct = 5;
    int bpm = 120;

    while (*p && *p != ':') {
        if (*p == 'd' && *(p+1) == '=') {
            p += 2;
            default_dur = 0;
            while (*p >= '0' && *p <= '9') {
                default_dur = default_dur * 10 + (*p - '0');
                p++;
            }
        } else if (*p == 'o' && *(p+1) == '=') {
            p += 2;
            default_oct = 0;
            while (*p >= '0' && *p <= '9') {
                default_oct = default_oct * 10 + (*p - '0');
                p++;
            }
        } else if (*p == 'b' && *(p+1) == '=') {
            p += 2;
            bpm = 0;
            while (*p >= '0' && *p <= '9') {
                bpm = bpm * 10 + (*p - '0');
                p++;
            }
        } else {
            p++;
        }
        if (*p == ',') p++;
    }

    if (*p == ':') p++;

    uint32_t wholenote = 240000 / bpm;

    while (*p && musicPlaying.load()) {
        int duration = 0;
        while (*p >= '0' && *p <= '9') {
            duration = duration * 10 + (*p - '0');
            p++;
        }
        if (duration == 0) {
            duration = default_dur;
        }

        char note_char = *p;
        if (note_char >= 'A' && note_char <= 'G') {
            note_char = note_char - 'A' + 'a';
        } else if (note_char >= 'a' && note_char <= 'g') {
            // ok
        } else if (note_char == 'p' || note_char == 'P') {
            note_char = 'p';
        } else {
            p++;
            continue;
        }
        p++;

        bool sharp = false;
        if (*p == '#') {
            sharp = true;
            p++;
        }

        bool dot = false;
        if (*p == '.') {
            dot = true;
            p++;
        }

        int octave = default_oct;
        if (*p >= '0' && *p <= '9') {
            octave = *p - '0';
            p++;
        }

        if (*p == '.') {
            dot = true;
            p++;
        }

        if (*p == ',') {
            p++;
        }

        uint32_t note_duration = wholenote / duration;
        if (dot) {
            note_duration = note_duration * 3 / 2;
        }

        if (note_char == 'p') {
            uint32_t elapsed = 0;
            while (elapsed < note_duration && musicPlaying.load()) {
                uint32_t sleep_time = (note_duration - elapsed > 10) ? 10 : (note_duration - elapsed);
                vTaskDelay(pdMS_TO_TICKS(sleep_time));
                elapsed += sleep_time;
            }
        } else {
            int note_idx = get_note_index(note_char, sharp);
            uint32_t freq = 0;
            if (note_idx >= 0) {
                const uint16_t notes_octave4[] = {262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494};
                freq = notes_octave4[note_idx];
                if (octave > 4) {
                    freq = freq << (octave - 4);
                } else if (octave < 4) {
                    freq = freq >> (4 - octave);
                }
            }

            if (freq > 0 && musicPlaying.load()) {
                uint32_t duty = ((musicVolume.load() * 10) * 128) / 100;
                if (duty == 0) duty = 1;

                speaker_ledc_write_tone(freq);
                speaker_ledc_write(duty);

                uint32_t sound_duration = note_duration * 9 / 10;
                uint32_t gap_duration = note_duration - sound_duration;

                uint32_t elapsed = 0;
                while (elapsed < sound_duration && musicPlaying.load()) {
                    uint32_t sleep_time = (sound_duration - elapsed > 10) ? 10 : (sound_duration - elapsed);
                    vTaskDelay(pdMS_TO_TICKS(sleep_time));
                    elapsed += sleep_time;
                }

                speaker_ledc_write(0);

                elapsed = 0;
                while (elapsed < gap_duration && musicPlaying.load()) {
                    uint32_t sleep_time = (gap_duration - elapsed > 10) ? 10 : (gap_duration - elapsed);
                    vTaskDelay(pdMS_TO_TICKS(sleep_time));
                    elapsed += sleep_time;
                }
            }
        }
    }

    speaker_ledc_write(0);
}

static void music_player_task(void *pvParameters) {
    LV_UNUSED(pvParameters);
    while (true) {
        AudioEffect effect = AudioEffect::Click;
        if (audio_queue != nullptr &&
            xQueueReceive(audio_queue, &effect, pdMS_TO_TICKS(20U)) == pdTRUE) {
            play_audio_effect_blocking(effect);
        }
        if (musicPlaying.load()) {
            const int idx = selectedSongIndex.load();
            if (idx >= 0 && idx < 3) {
                play_rtttl(RTTTL_SONGS[idx]);
            }
            musicPlaying = false;
        }
        vTaskDelay(pdMS_TO_TICKS(10U));
    }
}

static void set_label_text(lv_obj_t *label, const char *text) {
    if (label != nullptr) {
        lv_label_set_text(label, text);
    }
}

static void set_checked(lv_obj_t *obj, bool checked) {
    if (obj == nullptr) {
        return;
    }
    if (checked) {
        lv_obj_add_state(obj, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(obj, LV_STATE_CHECKED);
    }
}

static void style_panel(lv_obj_t *obj, lv_color_t bg, lv_color_t border, lv_coord_t radius) {
    lv_obj_set_style_bg_color(obj, resolve_bg_color(bg), 0);
    lv_obj_set_style_border_color(obj, ui_light_theme ? theme_card_border() : border, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

static lv_obj_t *create_card(lv_obj_t *parent, lv_coord_t w, lv_coord_t h,
                             lv_coord_t x, lv_coord_t y) {
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_style_pad_all(card, 6, 0);
    style_panel(card, lv_color_make(20, 26, 40), lv_color_make(35, 41, 55), 8);
    return card;
}

static lv_obj_t *create_label(lv_obj_t *parent, const char *text, lv_color_t color,
                              const lv_font_t *font) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, resolve_text_color(color), 0);
    if (font != nullptr) {
        lv_obj_set_style_text_font(label, font, 0);
    }
    return label;
}

static lv_obj_t *create_fixed_label(lv_obj_t *parent,
                                    const char *text,
                                    lv_color_t color,
                                    const lv_font_t *font,
                                    lv_coord_t width,
                                    lv_coord_t x,
                                    lv_coord_t y,
                                    lv_label_long_mode_t long_mode = LV_LABEL_LONG_DOT) {
    lv_obj_t *label = create_label(parent, text, color, font);
    lv_obj_set_width(label, width);
    lv_label_set_long_mode(label, long_mode);
    lv_obj_set_pos(label, x, y);
    return label;
}

static lv_obj_t *create_button(lv_obj_t *parent, const char *text, lv_coord_t w,
                               lv_coord_t h, lv_color_t bg,
                               lv_event_cb_t cb, void *userData) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, resolve_bg_color(bg), 0);
    lv_obj_set_style_bg_color(btn, lv_color_darken(resolve_bg_color(bg), LV_OPA_30), LV_STATE_PRESSED);
    lv_obj_set_style_translate_y(btn, 1, LV_STATE_PRESSED);
    lv_obj_set_style_opa(btn, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *label = create_label(btn, text, lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    if (cb != nullptr) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, userData);
    }
    const lv_coord_t smaller_dimension = w < h ? w : h;
    if (smaller_dimension < 40) {
        const lv_coord_t extension = static_cast<lv_coord_t>((40 - smaller_dimension + 1) / 2);
        lv_obj_set_ext_click_area(btn, extension > 10 ? 10 : extension);
    }
    return btn;
}

static void style_colored_button_label(lv_obj_t *btn, lv_coord_t width) {
    if (btn == nullptr) {
        return;
    }

    lv_obj_t *label = lv_obj_get_child(btn, 0);
    if (label == nullptr) {
        return;
    }

    lv_obj_set_width(label, width);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
}

static void apply_colored_3d_button(lv_obj_t *btn, lv_color_t bg, lv_coord_t label_width) {
    if (btn == nullptr) {
        return;
    }

    apply_3d_button_properties(btn);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_bg_color(btn, lv_color_darken(bg, LV_OPA_30), LV_STATE_PRESSED);
    style_colored_button_label(btn, label_width);
    lv_obj_t *label = lv_obj_get_child(btn, 0);
    if (label != nullptr) {
        lv_obj_set_style_text_color(
            label,
            lv_color_brightness(bg) > 145U ? lv_color_make(8, 13, 24) : lv_color_white(),
            0);
    }
}

static void style_wifi_list_item(lv_obj_t *item, lv_color_t text_color) {
    if (item == nullptr) {
        return;
    }

    lv_obj_set_height(item, 30);
    lv_obj_set_style_radius(item, 6, 0);
    lv_obj_set_style_bg_color(item, resolve_bg_color(lv_color_make(15, 23, 42)), 0);
    lv_obj_set_style_border_color(item, theme_card_border(), 0);
    lv_obj_set_style_border_width(item, 1, 0);
    lv_obj_set_style_pad_all(item, 4, 0);
    lv_obj_set_style_text_font(item, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(item, resolve_text_color(text_color), 0);
}

static lv_obj_t *create_accent_bar(lv_obj_t *parent, lv_color_t color, lv_coord_t h);
static void style_switch_cyd(lv_obj_t *sw);
static void hw_switch_handler(lv_event_t *e);
static void hardware_card_click_handler(lv_event_t *e);
static lv_event_cb_t hardware_detail_cb_for_switch(lv_obj_t *sw);
static bool hardware_toggle_metadata_for_switch(lv_obj_t *sw, HardwareToggle &toggle, bool &old_state);
static void hardware_toggle_request(lv_obj_t *sw, bool state);
static void make_object_clickable(lv_obj_t *obj, lv_event_cb_t cb, void *user_data);

static lv_obj_t *create_heading_card(lv_obj_t *parent, lv_coord_t w, lv_coord_t h,
                                     lv_coord_t x, lv_coord_t y,
                                     const char *title, const char *subtitle,
                                     lv_color_t accent) {
    lv_obj_t *card = create_card(parent, w, h, x, y);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_border_color(card, accent, 0);
    create_accent_bar(card, accent, static_cast<lv_coord_t>(h - 12));

    lv_obj_t *title_lbl = create_label(card, title, accent, &lv_font_montserrat_12);
    lv_obj_set_width(title_lbl, static_cast<lv_coord_t>(w - 28));
    lv_label_set_long_mode(title_lbl, LV_LABEL_LONG_DOT);
    lv_obj_align(title_lbl, LV_ALIGN_TOP_LEFT, 10, 4);

    if (subtitle != nullptr && subtitle[0] != '\0') {
        lv_obj_t *subtitle_lbl = create_label(card, subtitle, theme_text_muted(), &lv_font_montserrat_12);
        lv_obj_set_width(subtitle_lbl, static_cast<lv_coord_t>(w - 28));
        lv_label_set_long_mode(subtitle_lbl, LV_LABEL_LONG_DOT);
        lv_obj_align(subtitle_lbl, LV_ALIGN_BOTTOM_LEFT, 10, -6);
    }

    return card;
}

static lv_obj_t *create_toggle_card(lv_obj_t *parent, const char *title, const char *subtitle,
                                    lv_color_t accent, lv_obj_t **sw_ptr,
                                    bool initial_state, lv_event_cb_t cb) {
    const bool has_subtitle = subtitle != nullptr && subtitle[0] != '\0';
    const lv_coord_t height = has_subtitle ? 54 : 44;
    lv_obj_t *card = create_card(parent, 300, height, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_border_color(card, initial_state ? accent : theme_card_border(), 0);
    create_accent_bar(card, accent, static_cast<lv_coord_t>(height - 12));

    lv_obj_t *detail_btn = lv_btn_create(card);
    lv_obj_set_size(detail_btn, 224, static_cast<lv_coord_t>(height - 8));
    lv_obj_align(detail_btn, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_opa(detail_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(detail_btn, 0, 0);
    lv_obj_set_style_radius(detail_btn, 6, 0);
    lv_obj_set_style_pad_all(detail_btn, 0, 0);
    lv_obj_clear_flag(detail_btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title_lbl = create_label(detail_btn, title, initial_state ? lv_color_white() : theme_text_muted(), &lv_font_montserrat_12);
    lv_obj_set_width(title_lbl, 176);
    lv_label_set_long_mode(title_lbl, LV_LABEL_LONG_DOT);
    lv_obj_align(title_lbl, has_subtitle ? LV_ALIGN_TOP_LEFT : LV_ALIGN_LEFT_MID, 10, has_subtitle ? 5 : 0);

    lv_obj_t *subtitle_lbl = nullptr;
    if (has_subtitle) {
        subtitle_lbl = create_label(detail_btn, subtitle, theme_text_muted(), &lv_font_montserrat_12);
        lv_obj_set_width(subtitle_lbl, 176);
        lv_label_set_long_mode(subtitle_lbl, LV_LABEL_LONG_DOT);
        lv_obj_align(subtitle_lbl, LV_ALIGN_BOTTOM_LEFT, 10, -6);
    }

    lv_obj_t *arrow_lbl = create_label(detail_btn, LV_SYMBOL_RIGHT, theme_text_muted(), &lv_font_montserrat_12);
    lv_obj_align(arrow_lbl, LV_ALIGN_RIGHT_MID, -6, 0);

    if (sw_ptr != nullptr) {
        *sw_ptr = lv_switch_create(card);
        lv_obj_set_size(*sw_ptr, 42, 22);
        lv_obj_align(*sw_ptr, LV_ALIGN_RIGHT_MID, -10, 0);
        style_switch_cyd(*sw_ptr);
        if (initial_state) {
            lv_obj_add_state(*sw_ptr, LV_STATE_CHECKED);
        }
        if (cb != nullptr) {
            lv_obj_add_event_cb(*sw_ptr, cb, LV_EVENT_VALUE_CHANGED, nullptr);
        }
    }

    if (cb != nullptr && sw_ptr != nullptr && *sw_ptr != nullptr) {
        // Detail button needs the actual switch object, so the callback is bound
        // only after the switch exists. This keeps the card behavior deterministic.
        lv_obj_add_event_cb(detail_btn, hardware_card_click_handler, LV_EVENT_CLICKED, *sw_ptr);
    }
    return card;
}

static void delete_obj_async_cb(void *user_data) {
    lv_obj_t *obj = static_cast<lv_obj_t *>(user_data);
    if (obj != nullptr && lv_obj_is_valid(obj)) {
        lv_obj_del(obj);
    }
}

static void delete_obj_async(lv_obj_t *obj) {
    if (obj != nullptr && lv_obj_is_valid(obj)) {
        lv_async_call(delete_obj_async_cb, obj);
    }
}

static bool pin_guard_is_authorized() {
    if (!pin_authenticated) {
        return false;
    }
    const uint32_t now = millis();
    if (static_cast<int32_t>(pin_auth_until_ms - now) <= 0) {
        pin_authenticated = false;
        return false;
    }
    return true;
}

static void pin_update_label() {
    if (pin_value_lbl == nullptr) {
        return;
    }
    char masked[9] = "";
    const size_t len = strlen(pin_entry);
    for (size_t i = 0; i < len && i < sizeof(masked) - 1U; ++i) {
        masked[i] = '*';
    }
    masked[len < sizeof(masked) - 1U ? len : sizeof(masked) - 1U] = '\0';
    char placeholder[9] = {};
    const size_t pin_length =
        device_credentials_admin_pin_length();
    const size_t placeholder_length =
        pin_length < sizeof(placeholder) ? pin_length : sizeof(placeholder) - 1U;
    memset(placeholder, '-', placeholder_length);
    placeholder[placeholder_length] = '\0';
    lv_label_set_text(
        pin_value_lbl,
        masked[0] != '\0' ? masked : placeholder);
}

static void pin_refresh_back_key_label();
static void pin_matrix_draw_cb(lv_event_t *e);
static void pin_matrix_cb(lv_event_t *e);
static void build_pin_guard_modal();

static void pin_set_status(const char *text, lv_color_t color) {
    if (pin_status_lbl == nullptr) {
        return;
    }
    lv_obj_set_style_text_color(pin_status_lbl, color, 0);
    lv_label_set_text(pin_status_lbl, text != nullptr ? text : "");
}

static void pin_show_setup_hint_if_needed() {
    const char *setup_pin = device_credentials_setup_pin();
    if (setup_pin == nullptr) {
        pin_set_status("", theme_text_muted());
        return;
    }
    char message[64];
    snprintf(message, sizeof(message), "Nowy PIN: %s - zapisz go", setup_pin);
    pin_set_status(message, lv_color_make(245, 158, 11));
}

static void pin_refresh_back_key_label() {
    const char *wanted = pin_entry[0] == '\0' ? LV_SYMBOL_LEFT : PIN_KEY_BACK;
    if (pin_map[PIN_BACK_BTN_ID] == wanted) {
        return;
    }
    pin_map[PIN_BACK_BTN_ID] = wanted;
    if (pin_matrix != nullptr && lv_obj_is_valid(pin_matrix)) {
        lv_obj_invalidate(pin_matrix);
    }
}

static bool pin_guard_modal_is_ready() {
    return pin_overlay != nullptr && lv_obj_is_valid(pin_overlay) &&
           pin_matrix != nullptr && lv_obj_is_valid(pin_matrix);
}

static void close_pin_overlay() {
    if (pin_overlay == nullptr) {
        pin_value_lbl = nullptr;
        pin_status_lbl = nullptr;
        pin_matrix = nullptr;
        pin_entry[0] = '\0';
        pin_last_key_ms = 0;
        return;
    }
    if (!lv_obj_is_valid(pin_overlay)) {
        pin_overlay = nullptr;
        pin_value_lbl = nullptr;
        pin_status_lbl = nullptr;
        pin_matrix = nullptr;
        pin_entry[0] = '\0';
        pin_last_key_ms = 0;
        return;
    }

    // Keep the modal alive and hidden instead of deleting it. Reallocating the
    // keypad tree later is what caused the fragmented-heap failures in practice.
    pin_entry[0] = '\0';
    pin_last_key_ms = 0;
    pin_refresh_back_key_label();
    pin_update_label();
    pin_set_status("", theme_text_muted());
    if (pin_matrix != nullptr && lv_obj_is_valid(pin_matrix)) {
        lv_btnmatrix_set_selected_btn(pin_matrix, LV_BTNMATRIX_BTN_NONE);
    }
    lv_obj_add_flag(pin_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void prime_pin_guard_modal() {
    if (pin_guard_modal_is_ready()) {
        Serial.println("UI_PIN: prime skipped, modal already valid");
        return;
    }
    // This runs after the main UI exists, so we keep the eager allocation
    // small enough to avoid stealing memory from the visible startup screens.
    if (!ensure_runtime_ui_heap("PinGuardInit", UI_PIN_INIT_MIN_FREE, UI_PIN_INIT_MIN_LARGEST)) {
        Serial.println("UI_PIN: prime skipped, heap reserve too small");
        return;
    }
    build_pin_guard_modal();
    Serial.printf("UI_PIN: prime result overlay=%p valid=%d matrix=%p\n",
                  static_cast<void *>(pin_overlay),
                  pin_guard_modal_is_ready() ? 1 : 0,
                  static_cast<void *>(pin_matrix));
}

static void build_pin_guard_modal() {
    if (pin_guard_modal_is_ready()) {
        return;
    }
    if (pin_overlay != nullptr && lv_obj_is_valid(pin_overlay)) {
        lv_obj_del(pin_overlay);
    }
    pin_overlay = nullptr;
    pin_value_lbl = nullptr;
    pin_status_lbl = nullptr;
    pin_matrix = nullptr;

    pin_entry[0] = '\0';

    pin_overlay = lv_obj_create(lv_scr_act());
    if (pin_overlay == nullptr) {
        Serial.println("UI_PIN: overlay allocation failed");
        return;
    }
    lv_obj_set_size(pin_overlay, 320, 240);
    lv_obj_set_pos(pin_overlay, 0, 0);
    lv_obj_set_style_bg_color(pin_overlay, theme_screen_bg(), 0);
    lv_obj_set_style_border_width(pin_overlay, 0, 0);
    lv_obj_set_style_radius(pin_overlay, 0, 0);
    lv_obj_set_style_pad_all(pin_overlay, 0, 0);
    lv_obj_clear_flag(pin_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = create_label(pin_overlay, "Kod PIN", theme_text_main(), &lv_font_montserrat_14);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    pin_value_lbl = create_label(pin_overlay, "----", lv_color_make(6, 182, 212), &lv_font_montserrat_24);
    lv_obj_align(pin_value_lbl, LV_ALIGN_TOP_MID, 0, 28);

    pin_status_lbl = create_label(pin_overlay, "", theme_text_muted(), &lv_font_montserrat_12);
    lv_obj_set_width(pin_status_lbl, 300);
    lv_label_set_long_mode(pin_status_lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(pin_status_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(pin_status_lbl, LV_ALIGN_TOP_MID, 0, 62);

    pin_refresh_back_key_label();

    pin_matrix = lv_btnmatrix_create(pin_overlay);
    if (pin_matrix == nullptr) {
        Serial.println("UI_PIN: keypad allocation failed");
        lv_obj_del(pin_overlay);
        pin_overlay = nullptr;
        pin_value_lbl = nullptr;
        pin_status_lbl = nullptr;
        return;
    }
    lv_obj_set_size(pin_matrix, 304, 156);
    lv_obj_align(pin_matrix, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_btnmatrix_set_map(pin_matrix, pin_map);
    lv_btnmatrix_set_one_checked(pin_matrix, false);
    lv_btnmatrix_set_btn_ctrl_all(pin_matrix, LV_BTNMATRIX_CTRL_NO_REPEAT);
    lv_obj_add_event_cb(pin_matrix, pin_matrix_draw_cb, LV_EVENT_DRAW_PART_BEGIN, nullptr);
    lv_obj_add_event_cb(pin_matrix, pin_matrix_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    // A single button-matrix widget keeps PinGuard under the small contiguous
    // blocks available after the main UI is built. Do not expand this back into
    // individual LVGL buttons unless the whole UI memory model changes.
    lv_obj_set_style_bg_color(pin_matrix, resolve_bg_color(lv_color_make(20, 26, 40)), LV_PART_MAIN);
    lv_obj_set_style_border_color(pin_matrix, theme_card_border(), LV_PART_MAIN);
    lv_obj_set_style_border_width(pin_matrix, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(pin_matrix, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(pin_matrix, 4, LV_PART_MAIN);
    lv_obj_set_style_text_font(pin_matrix, &lv_font_montserrat_12, LV_PART_ITEMS);
    lv_obj_set_style_text_color(pin_matrix, theme_text_main(), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(pin_matrix, theme_matrix_item_bg(), LV_PART_ITEMS);
    const lv_style_selector_t pin_matrix_checked_selector =
        static_cast<lv_style_selector_t>(static_cast<uint32_t>(LV_PART_ITEMS) | static_cast<uint32_t>(LV_STATE_CHECKED));
    const lv_style_selector_t pin_matrix_pressed_selector =
        static_cast<lv_style_selector_t>(static_cast<uint32_t>(LV_PART_ITEMS) | static_cast<uint32_t>(LV_STATE_PRESSED));
    lv_obj_set_style_bg_color(pin_matrix, lv_color_make(16, 185, 129), pin_matrix_checked_selector);
    lv_obj_set_style_bg_color(pin_matrix, theme_matrix_pressed_bg(), pin_matrix_pressed_selector);
    lv_obj_set_style_border_width(pin_matrix, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(pin_matrix, theme_card_border(), LV_PART_ITEMS);
    lv_obj_set_style_radius(pin_matrix, 6, LV_PART_ITEMS);

    // Create the object tree once; later we only toggle HIDDEN to avoid
    // repeating a large LVGL allocation on a fragmented runtime heap.
    lv_obj_add_flag(pin_overlay, LV_OBJ_FLAG_HIDDEN);
    Serial.printf("UI_PIN: modal ready overlay=%p matrix=%p heap_free=%lu heap_largest=%lu\n",
                  static_cast<void *>(pin_overlay),
                  static_cast<void *>(pin_matrix),
                  static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_8BIT)),
                  static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
}

static void pin_matrix_draw_cb(lv_event_t *e) {
    lv_obj_t *matrix = lv_event_get_target(e);
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
    if (matrix == nullptr || dsc == nullptr || dsc->part != LV_PART_ITEMS || dsc->rect_dsc == nullptr) {
        return;
    }

    const bool pressed = lv_btnmatrix_get_selected_btn(matrix) == dsc->id;
    if (dsc->id == PIN_BACK_BTN_ID) {
        dsc->rect_dsc->bg_opa = LV_OPA_COVER;
        dsc->rect_dsc->bg_color = pressed ? lv_color_make(153, 27, 27) : lv_color_make(185, 28, 28);
        dsc->rect_dsc->border_color = lv_color_make(248, 113, 113);
        if (dsc->label_dsc != nullptr) {
            dsc->label_dsc->color = lv_color_white();
        }
        return;
    }

    if (dsc->id == PIN_OK_BTN_ID) {
        dsc->rect_dsc->bg_opa = LV_OPA_COVER;
        dsc->rect_dsc->bg_color = pressed ? lv_color_make(4, 120, 87) : lv_color_make(5, 150, 105);
        dsc->rect_dsc->border_color = lv_color_make(52, 211, 153);
        if (dsc->label_dsc != nullptr) {
            dsc->label_dsc->color = lv_color_white();
        }
    }
}

static void pin_submit_current_entry() {
    if (device_credentials_admin_pin_matches(pin_entry)) {
        pin_authenticated = true;
        pin_auth_until_ms = millis() + PIN_AUTH_WINDOW_MS;
        device_credentials_acknowledge_setup_pin();
        PendingPinAction action = pending_pin_action;
        pending_pin_action = {PinAction::None, 0, false};
        close_pin_overlay();
        execute_pin_action(action);
        return;
    }

    pin_entry[0] = '\0';
    pin_refresh_back_key_label();
    pin_update_label();
    pin_set_status("Bledny PIN", lv_color_make(239, 68, 68));
    Serial.println("UI_PIN: invalid PIN");
    play_system_sound(SoundType::Warning);
}

static void pin_matrix_cb(lv_event_t *e) {
    lv_obj_t *matrix = lv_event_get_target(e);
    if (matrix == nullptr || !lv_obj_is_valid(matrix)) {
        return;
    }

    const uint16_t btn_id = lv_btnmatrix_get_selected_btn(matrix);
    if (btn_id == LV_BTNMATRIX_BTN_NONE) {
        return;
    }

    const char *key = lv_btnmatrix_get_btn_text(matrix, btn_id);
    lv_btnmatrix_set_selected_btn(matrix, LV_BTNMATRIX_BTN_NONE);
    if (key == nullptr || key[0] == '\0') {
        return;
    }

    const uint32_t now = millis();
    if (pin_last_key_ms != 0 && static_cast<uint32_t>(now - pin_last_key_ms) < PIN_KEY_DEBOUNCE_MS) {
        return;
    }
    pin_last_key_ms = now;

    play_system_sound(SoundType::Click);

    if (btn_id == PIN_BACK_BTN_ID) {
        const size_t len = strlen(pin_entry);
        if (len > 0) {
            pin_entry[len - 1] = '\0';
            pin_refresh_back_key_label();
            pin_update_label();
            pin_set_status("", theme_text_muted());
        } else {
            pending_pin_action = {PinAction::None, 0, false};
            close_pin_overlay();
        }
        return;
    }

    if (btn_id == PIN_OK_BTN_ID) {
        pin_submit_current_entry();
        return;
    }

    if (key[0] >= '0' && key[0] <= '9' && key[1] == '\0') {
        const size_t len = strlen(pin_entry);
        const size_t required_length =
            device_credentials_admin_pin_length();
        if (len < required_length && len < sizeof(pin_entry) - 1U) {
            pin_entry[len] = key[0];
            pin_entry[len + 1] = '\0';
        }
        pin_refresh_back_key_label();
        pin_update_label();
        pin_set_status("", theme_text_muted());
        if (strlen(pin_entry) == required_length) {
            pin_submit_current_entry();
        }
        return;
    }
}

static bool show_pin_guard_modal() {
    Serial.printf("UI_PIN: show request overlay=%p valid=%d heap_free=%lu heap_largest=%lu\n",
                  static_cast<void *>(pin_overlay),
                  pin_guard_modal_is_ready() ? 1 : 0,
                  static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_8BIT)),
                  static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
    if (pin_guard_modal_is_ready()) {
        pin_entry[0] = '\0';
        pin_last_key_ms = 0;
        pin_refresh_back_key_label();
        pin_update_label();
        pin_show_setup_hint_if_needed();
        lv_obj_clear_flag(pin_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(pin_overlay);
        Serial.printf("UI_PIN: shown overlay=%p hidden=%d\n",
                      static_cast<void *>(pin_overlay),
                      lv_obj_has_flag(pin_overlay, LV_OBJ_FLAG_HIDDEN) ? 1 : 0);
        return true;
    }
    if (!ensure_runtime_ui_heap("PinGuard", UI_RUNTIME_PIN_MIN_FREE, UI_RUNTIME_PIN_MIN_LARGEST)) {
        Serial.println("UI_PIN: show failed before allocation");
        return false;
    }
    build_pin_guard_modal();
    if (!pin_guard_modal_is_ready()) {
        Serial.println("UI_PIN: show failed after allocation");
        return false;
    }
    pin_entry[0] = '\0';
    pin_last_key_ms = 0;
    pin_refresh_back_key_label();
    pin_update_label();
    pin_show_setup_hint_if_needed();
    lv_obj_clear_flag(pin_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(pin_overlay);
    Serial.printf("UI_PIN: shown overlay=%p hidden=%d\n",
                  static_cast<void *>(pin_overlay),
                  lv_obj_has_flag(pin_overlay, LV_OBJ_FLAG_HIDDEN) ? 1 : 0);
    return true;
}

static bool pin_guard_execute_or_prompt(PinAction action, intptr_t value, bool state) {
    if (pin_guard_is_authorized()) {
        execute_pin_action({action, value, state});
        return true;
    }
    pending_pin_action = {action, value, state};
    if (!show_pin_guard_modal()) {
        pending_pin_action = {PinAction::None, 0, false};
        play_system_sound(SoundType::Warning);
        Serial.println("UI_PIN: protected action cancelled because PIN UI is unavailable");
    }
    return false;
}

static void style_switch_cyd(lv_obj_t *sw) {
    lv_obj_set_style_bg_color(sw, resolve_bg_color(lv_color_make(30, 41, 59)), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, lv_color_make(6, 182, 212), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sw, lv_color_make(148, 163, 184), LV_PART_KNOB);
    const lv_style_selector_t checked_knob =
        static_cast<lv_style_selector_t>(LV_PART_KNOB) |
        static_cast<lv_style_selector_t>(LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, lv_color_make(11, 15, 25), checked_knob);
}

static void update_editor_fields();
static void gui_sync_widgets_to_state();
static void sync_nav_bar_visuals();
static void ensure_feeder_modal() {
    if (modal_feeder != nullptr && lv_obj_is_valid(modal_feeder)) {
        return;
    }

    modal_feeder = lv_obj_create(lv_scr_act());
    if (modal_feeder == nullptr) {
        Serial.println("UI_FEED: feeder modal allocation failed.");
        modal_feeder_title_lbl = nullptr;
        modal_feeder_msg_lbl = nullptr;
        return;
    }

    lv_obj_set_size(modal_feeder, 240, 140);
    lv_obj_align(modal_feeder, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(modal_feeder, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_pad_all(modal_feeder, 0, 0);
    style_panel(modal_feeder, lv_color_make(20, 26, 40), lv_color_make(6, 182, 212), 12);

    lv_obj_t *spinner = lv_spinner_create(modal_feeder, 1000, 60);
    if (spinner != nullptr) {
        lv_obj_set_size(spinner, 40, 40);
        lv_obj_align(spinner, LV_ALIGN_TOP_MID, 0, 15);
        lv_obj_set_style_arc_color(spinner, lv_color_make(6, 182, 212), LV_PART_INDICATOR);
    }

    modal_feeder_title_lbl = create_label(modal_feeder, "Karmienie", lv_color_white(), &lv_font_montserrat_14);
    lv_obj_align(modal_feeder_title_lbl, LV_ALIGN_BOTTOM_MID, 0, -35);
    modal_feeder_msg_lbl = create_label(modal_feeder, "Start napedu", lv_color_make(100, 116, 139), &lv_font_montserrat_12);
    lv_obj_align(modal_feeder_msg_lbl, LV_ALIGN_BOTTOM_MID, 0, -15);
}

static void show_feeder_modal(const char *line1, const char *line2) {
    ensure_feeder_modal();
    if (modal_feeder == nullptr) {
        return;
    }
    lv_obj_clear_flag(modal_feeder, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(modal_feeder);
    if (modal_feeder_title_lbl != nullptr) {
        lv_label_set_text(modal_feeder_title_lbl, line1 != nullptr ? line1 : "Karmienie");
    }
    if (modal_feeder_msg_lbl != nullptr) {
        lv_label_set_text(modal_feeder_msg_lbl, line2 != nullptr ? line2 : "Naped aktywny");
    }
}

static bool write_mcp_output_if_needed(bool force,
                                       bool &shadow,
                                       HwConfig::McpChannel channel,
                                       bool desired) {
    if (!force && shadow == desired) {
        return true;
    }
    const bool ok = hal_mcp_write_channel(channel, desired);
    if (ok) {
        shadow = desired;
    }
    return ok;
}

static aquarium::AquaelProfile aquael_domain_profile(uint8_t profile) {
    return static_cast<aquarium::AquaelProfile>(
        normalize_aquael_profile(profile));
}

static bool service_aquael_light_output(
    aquarium::AquaelLightController &controller,
    bool &shadow,
    HwConfig::McpChannel channel,
    bool desired_on,
    uint8_t desired_profile) {
    const uint32_t now_ms = millis();
    controller.request(desired_on, aquael_domain_profile(desired_profile));
    const aquarium::AquaelDriveDecision decision = controller.poll(now_ms);
    if (!decision.write_required) {
        shadow = controller.snapshot(now_ms).relay_on;
        return true;
    }
    const bool written = hal_mcp_write_channel(channel, decision.relay_on);
    controller.acknowledge_write(written, now_ms);
    if (written) {
        shadow = decision.relay_on;
    }
    return written;
}

static void apply_mcp_outputs(void) {
    // Regular actuator traffic belongs to the Core 0 service task. LVGL
    // callbacks only update the desired state and return immediately.
    if (xPortGetCoreID() != 0) {
        return;
    }
    if (cfg.devMode || !hal_mcp_is_present()) {
        return;
    }

    const bool desired_light = runtime.lightOn;
    const bool desired_plant = runtime.plantLightOn;
    const bool desired_filter = runtime.filterOn;
    const bool desired_air = cfg.enableAerator && runtime.airOn;
    const bool desired_heater = cfg.enableHeater && runtime.heaterOn;
    const bool desired_co2 = cfg.enableCo2 && runtime.co2On;
    const bool desired_water_dosing = cfg.enableWaterLevel && runtime.waterFillOn;
    const bool force = !mcp_outputs.initialized;

    bool ok = true;
    if ((relay_test_active_mask & (1U << HwConfig::CH_LIGHT_A)) == 0U) {
        ok = service_aquael_light_output(
                 mcp_outputs.frontLight,
                 mcp_outputs.light,
                 HwConfig::CH_LIGHT_A,
                 desired_light,
                 runtime.lightActiveMode) &&
             ok;
    }
    if ((relay_test_active_mask & (1U << HwConfig::CH_LIGHT_B)) == 0U) {
        ok = service_aquael_light_output(
                 mcp_outputs.rearLight,
                 mcp_outputs.plantLight,
                 HwConfig::CH_LIGHT_B,
                 desired_plant,
                 runtime.plantLightActiveMode) &&
             ok;
    }
    if ((relay_test_active_mask & (1U << HwConfig::CH_FILTER)) == 0U) {
        ok = write_mcp_output_if_needed(force, mcp_outputs.filter, HwConfig::CH_FILTER, desired_filter) && ok;
    }
    if ((relay_test_active_mask & (1U << HwConfig::CH_AERATOR)) == 0U) {
        ok = write_mcp_output_if_needed(force, mcp_outputs.aerator, HwConfig::CH_AERATOR, desired_air) && ok;
    }
    if ((relay_test_active_mask & (1U << HwConfig::CH_HEATER)) == 0U) {
        ok = write_mcp_output_if_needed(force, mcp_outputs.heater, HwConfig::CH_HEATER, desired_heater) && ok;
    }
    if ((relay_test_active_mask & (1U << HwConfig::CH_CO2)) == 0U) {
        ok = write_mcp_output_if_needed(force, mcp_outputs.co2, HwConfig::CH_CO2, desired_co2) && ok;
    }
    if ((relay_test_active_mask & (1U << HwConfig::CH_RELAY_SPARE)) == 0U) {
        ok = write_mcp_output_if_needed(force, mcp_outputs.waterDosing, HwConfig::CH_RELAY_SPARE, desired_water_dosing) && ok;
    }
    for (uint8_t channel = 0U; channel < 8U; ++channel) {
        const uint8_t channel_mask = static_cast<uint8_t>(1U << channel);
        if ((relay_test_active_mask & channel_mask) != 0U &&
            (relay_test_applied_mask & channel_mask) == 0U) {
            const bool test_ok = hal_mcp_write_channel(
                static_cast<HwConfig::McpChannel>(channel), true);
            if (test_ok) {
                relay_test_applied_mask |= channel_mask;
            }
            ok = test_ok && ok;
        }
    }

    static bool error_latched = false;
    if (!ok) {
        record_actuator_write_result(false);
        if (!error_latched) {
            add_gui_log("MCP: blad zapisu wyjsc", true);
            error_latched = true;
        }
        return;
    }

    record_actuator_write_result(true);
    error_latched = false;
    mcp_outputs.initialized = true;
}

static bool start_relay_test(uint8_t channel, bool state, uint32_t duration_ms) {
    if (channel < 1U || channel > 8U) {
        return false;
    }

    const uint8_t channel_index = static_cast<uint8_t>(channel - 1U);
    const uint8_t channel_mask = static_cast<uint8_t>(1U << channel_index);
    if (channel_index == static_cast<uint8_t>(HwConfig::CH_FEEDER_DRIVE) &&
        feeder_pulse_active) {
        return false;
    }
    if (!cfg.devMode && !sensor_debug.mcpPresent) {
        return false;
    }

    if (state) {
        relay_test_active_mask |= channel_mask;
        relay_test_deadline_ms[channel_index] = millis() + constrain(duration_ms, 100UL, RELAY_TEST_MAX_DURATION_MS);
    } else {
        relay_test_active_mask &= static_cast<uint8_t>(~channel_mask);
        relay_test_applied_mask &= static_cast<uint8_t>(~channel_mask);
        relay_test_deadline_ms[channel_index] = 0U;
        if (channel_index == static_cast<uint8_t>(HwConfig::CH_LIGHT_A)) {
            const bool written =
                hal_mcp_write_channel(HwConfig::CH_LIGHT_A, false);
            record_actuator_write_result(written);
            if (!written) {
                return false;
            }
            mcp_outputs.frontLight.reset_unknown(millis());
            mcp_outputs.light = false;
        } else if (channel_index == static_cast<uint8_t>(HwConfig::CH_LIGHT_B)) {
            const bool written =
                hal_mcp_write_channel(HwConfig::CH_LIGHT_B, false);
            record_actuator_write_result(written);
            if (!written) {
                return false;
            }
            mcp_outputs.rearLight.reset_unknown(millis());
            mcp_outputs.plantLight = false;
        }
        mcp_outputs.initialized = false;
    }
    return true;
}

static void update_relay_tests() {
    if (relay_test_active_mask == 0U) {
        return;
    }

    const uint32_t now_ms = millis();
    bool expired = false;
    for (uint8_t channel = 0U; channel < 8U; ++channel) {
        const uint8_t channel_mask = static_cast<uint8_t>(1U << channel);
        if ((relay_test_active_mask & channel_mask) == 0U) {
            continue;
        }
        if (static_cast<int32_t>(now_ms - relay_test_deadline_ms[channel]) >= 0) {
            relay_test_active_mask &= static_cast<uint8_t>(~channel_mask);
            relay_test_applied_mask &= static_cast<uint8_t>(~channel_mask);
            relay_test_deadline_ms[channel] = 0U;
            if (channel == static_cast<uint8_t>(HwConfig::CH_LIGHT_A)) {
                record_actuator_write_result(
                    hal_mcp_write_channel(
                        HwConfig::CH_LIGHT_A, false));
                mcp_outputs.frontLight.reset_unknown(now_ms);
                mcp_outputs.light = false;
            } else if (channel == static_cast<uint8_t>(HwConfig::CH_LIGHT_B)) {
                record_actuator_write_result(
                    hal_mcp_write_channel(
                        HwConfig::CH_LIGHT_B, false));
                mcp_outputs.rearLight.reset_unknown(now_ms);
                mcp_outputs.plantLight = false;
            }
            expired = true;
        }
    }

    if (expired) {
        mcp_outputs.initialized = false;
        apply_mcp_outputs();
    }
}

static void finish_feeder_pulse(bool ok, bool development_mode) {
    mcp_outputs.feeder = false;
    feeder_pulse_active = false;
    feeder_start_pending = false;
    feeder_pulse_deadline_ms = 0U;
    snprintf(last_feed_result,
             sizeof(last_feed_result),
             "%s",
             development_mode ? "dev_ok" : (ok ? "ok" : "mcp_error"));
    if (modal_feeder_msg_lbl != nullptr) {
        lv_label_set_text(modal_feeder_msg_lbl,
                          development_mode
                              ? "Dawka DEV zakonczona"
                              : (ok ? "Dawka zakonczona"
                                    : "Blad MCP karmnika"));
    }
    if (!ok) {
        add_gui_log("Karmnik: blad sterowania napedem", true);
    }
}

static void service_feeder_pulse(uint32_t now_ms) {
    if (!feeder_pulse_active) {
        return;
    }

    if (cfg.devMode) {
        if (static_cast<int32_t>(now_ms - feeder_pulse_deadline_ms) >= 0) {
            finish_feeder_pulse(true, true);
        }
        return;
    }

    if (feeder_start_pending) {
        const bool written =
            hal_mcp_write_channel(HwConfig::CH_FEEDER_DRIVE, true);
        record_actuator_write_result(written);
        if (!written) {
            finish_feeder_pulse(false, false);
            return;
        }
        feeder_start_pending = false;
        mcp_outputs.feeder = true;
        feeder_pulse_deadline_ms =
            now_ms + HwConfig::Debounce::FEEDER_PULSE_MS;
        snprintf(last_feed_result, sizeof(last_feed_result), "active");
        return;
    }

    if (static_cast<int32_t>(now_ms - feeder_pulse_deadline_ms) >= 0) {
        const bool ok =
            hal_mcp_write_channel(HwConfig::CH_FEEDER_DRIVE, false);
        record_actuator_write_result(ok);
        finish_feeder_pulse(ok, false);
    }
}

static bool run_feeder_pulse(const char *title, const char *message, bool critical_log) {
    const uint8_t feeder_channel_mask = static_cast<uint8_t>(
        1U << static_cast<uint8_t>(HwConfig::CH_FEEDER_DRIVE));
    if (feeder_pulse_active ||
        (relay_test_active_mask & feeder_channel_mask) != 0U) {
        show_feeder_modal("Karmienie", "Poprzedni cykl trwa");
        schedule_feeder_modal_close(1800);
        add_gui_log("Karmnik: poprzedni cykl nadal trwa", true);
        snprintf(last_feed_result, sizeof(last_feed_result), "busy");
        return false;
    }
    if (!cfg.feedEnabled && critical_log) {
        add_gui_log("Karmnik: harmonogram wylaczony", false);
    }
    show_feeder_modal(title != nullptr ? title : "Karmienie",
                      message != nullptr ? message : "Naped aktywny");
    if (!cfg.devMode && !sensor_debug.mcpPresent) {
        if (modal_feeder_msg_lbl != nullptr) {
            lv_label_set_text(modal_feeder_msg_lbl, "MCP niedostepny");
        }
        add_gui_log("Karmnik: MCP23017 niedostepny", true);
        snprintf(last_feed_result, sizeof(last_feed_result), "mcp_unavailable");
        schedule_feeder_modal_close(3000);
        return false;
    }

    const uint32_t now_ms = millis();
    feeder_pulse_active = true;
    feeder_start_pending = !cfg.devMode;
    feeder_pulse_deadline_ms =
        cfg.devMode ? now_ms + HwConfig::Debounce::FEEDER_PULSE_MS : 0U;
    runtime.lastAutoFeedMs = now_ms;
    last_feed_epoch =
        controller_clock_reliable ? controller_unix_time() : (now_ms / 1000UL);

    if (cfg.devMode) {
        mcp_outputs.feeder = true;
        snprintf(last_feed_result, sizeof(last_feed_result), "dev_simulated");
        add_gui_log("Karmnik DEV: symulacja dawki", false);
        return true;
    }

    snprintf(last_feed_result, sizeof(last_feed_result), "queued");
    show_feeder_modal(title != nullptr ? title : "Karmienie",
                      message != nullptr ? message : "Napęd oczekuje");
    schedule_feeder_modal_close(3000);
    add_gui_log(critical_log ? "Karmnik: dawka automatyczna" : "Karmnik: dawka reczna", critical_log);
    return true;
}

static void close_feeder_modal_cb(lv_timer_t *timer) {
    if (modal_feeder != nullptr) {
        lv_obj_add_flag(modal_feeder, LV_OBJ_FLAG_HIDDEN);
    }
    if (timer != nullptr) {
        if (timer == feeder_modal_close_timer) {
            feeder_modal_close_timer = nullptr;
        }
        lv_timer_del(timer);
    }
}

static void schedule_feeder_modal_close(uint32_t delay_ms) {
    if (feeder_modal_close_timer != nullptr) {
        lv_timer_del(feeder_modal_close_timer);
        feeder_modal_close_timer = nullptr;
    }
    feeder_modal_close_timer = lv_timer_create(close_feeder_modal_cb, delay_ms, nullptr);
    if (feeder_modal_close_timer != nullptr) {
        lv_timer_set_repeat_count(feeder_modal_close_timer, 1);
    }
}

static void feed_now_event_handler(lv_event_t *e) {
    LV_UNUSED(e);
    play_system_sound(SoundType::Save);
    if (run_feeder_pulse("Karmienie", "Dawka reczna", false)) {
        Serial.println("GUI: Manual feeding requested.");
    }
}

static void cycle_schedule_mode(uint8_t &mode) {
    if (mode == static_cast<uint8_t>(ScheduleMode::Schedule)) {
        mode = static_cast<uint8_t>(ScheduleMode::AlwaysOn);
    } else if (mode == static_cast<uint8_t>(ScheduleMode::AlwaysOn)) {
        mode = static_cast<uint8_t>(ScheduleMode::AlwaysOff);
    } else {
        mode = static_cast<uint8_t>(ScheduleMode::Schedule);
    }
}

static void cycle_light_color_mode(uint8_t &mode) {
    mode = static_cast<uint8_t>((normalize_aquael_profile(mode) + 1U) % 3U);
}

static void cycle_heater_mode(lv_event_t *e) {
    LV_UNUSED(e);
    if (cfg.heaterMode == static_cast<uint8_t>(HeaterMode::Threshold)) {
        cfg.heaterMode = static_cast<uint8_t>(HeaterMode::Off);
        runtime.heaterOn = false;
    } else {
        cfg.heaterMode = static_cast<uint8_t>(HeaterMode::Threshold);
    }
    gui_app_save_settings();
    gui_sync_widgets_to_state();
}

static void cycle_light_mode(lv_event_t *e) {
    LV_UNUSED(e);
    play_system_sound(SoundType::Click);
    cycle_schedule_mode(cfg.lightMode);
    gui_sync_widgets_to_state();
    gui_app_save_settings();
}

static void cycle_plant_mode(lv_event_t *e) {
    LV_UNUSED(e);
    play_system_sound(SoundType::Click);
    cycle_schedule_mode(cfg.plantLightMode);
    gui_sync_widgets_to_state();
    gui_app_save_settings();
}

static void cycle_filter_mode(lv_event_t *e) {
    LV_UNUSED(e);
    play_system_sound(SoundType::Click);
    cycle_schedule_mode(cfg.filterMode);
    gui_sync_widgets_to_state();
    gui_app_save_settings();
}

static void cycle_air_mode(lv_event_t *e) {
    LV_UNUSED(e);
    play_system_sound(SoundType::Click);
    cycle_schedule_mode(cfg.airMode);
    gui_sync_widgets_to_state();
    gui_app_save_settings();
}

static void cycle_home_light_color_mode(lv_event_t *e) {
    LV_UNUSED(e);
    play_system_sound(SoundType::Click);
    cycle_light_color_mode(cfg.lightColorMode);
    gui_sync_widgets_to_state();
    gui_app_save_settings();
}

static void cycle_home_plant_color_mode(lv_event_t *e) {
    LV_UNUSED(e);
    play_system_sound(SoundType::Click);
    cycle_light_color_mode(cfg.plantLightColorMode);
    gui_sync_widgets_to_state();
    gui_app_save_settings();
}

static void toggle_heater_auto_handler(lv_event_t *e) {
    play_system_sound(SoundType::Click);
    lv_obj_t *obj = lv_event_get_target(e);
    cfg.heaterMode = lv_obj_has_state(obj, LV_STATE_CHECKED)
                         ? static_cast<uint8_t>(HeaterMode::Threshold)
                         : static_cast<uint8_t>(HeaterMode::Off);
    if (cfg.heaterMode == static_cast<uint8_t>(HeaterMode::Off)) {
        runtime.heaterOn = false;
    }
    gui_sync_widgets_to_state();
    gui_app_save_settings();
}

static void adjust_target_temp_cb(lv_event_t *e) {
    play_system_sound(SoundType::Click);
    const int dir = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    cfg.targetTemp += static_cast<float>(dir) * 0.5f;
    cfg.targetTemp = constrain(cfg.targetTemp, 18.0f, 30.0f);
    gui_sync_widgets_to_state();
}

static void adjust_hysteresis_cb(lv_event_t *e) {
    play_system_sound(SoundType::Click);
    const int dir = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    cfg.tempHysteresis += static_cast<float>(dir) * 0.1f;
    cfg.tempHysteresis = constrain(cfg.tempHysteresis, 0.1f, 5.0f);
    cfg.tempHysteresis = roundf(cfg.tempHysteresis * 10.0f) / 10.0f;
    gui_sync_widgets_to_state();
}


static uint8_t get_editor_hourly_mode(int hour) {
    if (hour < 0 || hour > 23) return 0;
    const uint16_t checkMins = hour * 60 + 30;
    switch (current_editor_device) {
    case ScheduleDevice::Light:
        if (is_within_window(checkMins, cfg.lightStartHour, cfg.lightStartMinute, cfg.lightEndHour, cfg.lightEndMinute)) {
            return cfg.lightSchedColorMode;
        }
        break;
    case ScheduleDevice::PlantLight:
        if (is_within_window(checkMins, cfg.plantStartHour, cfg.plantStartMinute, cfg.plantEndHour, cfg.plantEndMinute)) {
            return cfg.plantSchedColorMode;
        }
        break;
    case ScheduleDevice::Filter:
        if (is_within_window(checkMins, cfg.filterStartHour, cfg.filterStartMinute, cfg.filterEndHour, cfg.filterEndMinute)) {
            return 1;
        }
        break;
    case ScheduleDevice::Air:
        if (is_within_window(checkMins, cfg.airStartHour, cfg.airStartMinute, cfg.airEndHour, cfg.airEndMinute)) {
            return 1;
        }
        break;
    case ScheduleDevice::QuietHours:
        if (is_within_window(checkMins, cfg.quietStartHour, cfg.quietStartMinute, cfg.quietEndHour, cfg.quietEndMinute)) {
            return 1;
        }
        break;
    default:
        break;
    }
    return 0;
}

static const char* hourly_mode_label(uint8_t val) {
    if (current_editor_device == ScheduleDevice::Light || current_editor_device == ScheduleDevice::PlantLight) {
        switch(val) {
            case 0: return "OFF";
            case 1: return "DAY";
            case 2: return "DAYBR";
            case 3: return "NIGHT";
            default: return "OFF";
        }
    } else {
        return val > 0 ? "ON" : "OFF";
    }
}

static lv_color_t hourly_mode_color(uint8_t val) {
    if (val == 0) return resolve_bg_color(lv_color_make(30, 41, 59)); // OFF - dark gray
    if (current_editor_device == ScheduleDevice::QuietHours) {
        return lv_color_make(139, 92, 246); // Indigo / Purple for Quiet Hours
    }
    if (current_editor_device == ScheduleDevice::Light || current_editor_device == ScheduleDevice::PlantLight) {
        if (val == 1) return lv_color_make(245, 158, 11); // DAY - warm white/daylight
        if (val == 2) return lv_color_make(14, 165, 233); // DAYBREAK - lower daylight with blue
        if (val == 3) return lv_color_make(59, 130, 246); // NIGHT - blue night glow
    }
    return lv_color_make(16, 185, 129); // ON - Green
}

static void update_editor_fields() {
    if (editor_title_lbl != nullptr) {
        switch (current_editor_device) {
        case ScheduleDevice::Light:
            lv_label_set_text(editor_title_lbl, "Przednia");
            break;
        case ScheduleDevice::PlantLight:
            lv_label_set_text(editor_title_lbl, "Tylna");
            break;
        case ScheduleDevice::Filter:
            lv_label_set_text(editor_title_lbl, "Filtr");
            break;
        case ScheduleDevice::Air:
            lv_label_set_text(editor_title_lbl, "Napowietrzanie");
            break;
        case ScheduleDevice::Feed:
            lv_label_set_text(editor_title_lbl, "Karmienie");
            break;
        case ScheduleDevice::QuietHours:
            lv_label_set_text(editor_title_lbl, "Cisza nocna");
            break;
        }
    }
    if (sched_editor_mode_btn != nullptr) {
        if (current_editor_device == ScheduleDevice::QuietHours) {
            lv_obj_add_flag(sched_editor_mode_btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(sched_editor_mode_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (editor_mode_lbl != nullptr) {
        uint8_t mode = 0;
        switch (current_editor_device) {
        case ScheduleDevice::Light: mode = cfg.lightMode; break;
        case ScheduleDevice::PlantLight: mode = cfg.plantLightMode; break;
        case ScheduleDevice::Filter: mode = cfg.filterMode; break;
        case ScheduleDevice::Air: mode = cfg.airMode; break;
        default: break;
        }
        lv_label_set_text(editor_mode_lbl, mode_label(mode));
    }
    
    // Update timeline block colors
    for (int i = 0; i < 24; i++) {
        if (timeline_blocks[i] != nullptr) {
            uint8_t val = get_editor_hourly_mode(i);
            lv_obj_set_style_bg_color(timeline_blocks[i], hourly_mode_color(val), 0);
            lv_obj_set_style_border_width(timeline_blocks[i], 0, 0);
        }
    }
    
    uint8_t startH = 0, startM = 0, endH = 0, endM = 0;
    uint8_t activeMode = 0;
    bool is_light = (current_editor_device == ScheduleDevice::Light || current_editor_device == ScheduleDevice::PlantLight);
    
    switch (current_editor_device) {
    case ScheduleDevice::Light:
        startH = cfg.lightStartHour; startM = cfg.lightStartMinute;
        endH = cfg.lightEndHour; endM = cfg.lightEndMinute;
        activeMode = cfg.lightSchedColorMode;
        break;
    case ScheduleDevice::PlantLight:
        startH = cfg.plantStartHour; startM = cfg.plantStartMinute;
        endH = cfg.plantEndHour; endM = cfg.plantEndMinute;
        activeMode = cfg.plantSchedColorMode;
        break;
    case ScheduleDevice::Filter:
        startH = cfg.filterStartHour; startM = cfg.filterStartMinute;
        endH = cfg.filterEndHour; endM = cfg.filterEndMinute;
        activeMode = 1;
        break;
    case ScheduleDevice::Air:
        startH = cfg.airStartHour; startM = cfg.airStartMinute;
        endH = cfg.airEndHour; endM = cfg.airEndMinute;
        activeMode = 1;
        break;
    case ScheduleDevice::QuietHours:
        startH = cfg.quietStartHour; startM = cfg.quietStartMinute;
        endH = cfg.quietEndHour; endM = cfg.quietEndMinute;
        activeMode = 1;
        break;
    default:
        break;
    }
    
    if (sched_editor_start_h_lbl != nullptr) {
        lv_label_set_text_fmt(sched_editor_start_h_lbl, "%02u", startH);
    }
    if (sched_editor_start_m_lbl != nullptr) {
        lv_label_set_text_fmt(sched_editor_start_m_lbl, "%02u", startM);
    }
    if (sched_editor_end_h_lbl != nullptr) {
        lv_label_set_text_fmt(sched_editor_end_h_lbl, "%02u", endH);
    }
    if (sched_editor_end_m_lbl != nullptr) {
        lv_label_set_text_fmt(sched_editor_end_m_lbl, "%02u", endM);
    }
    
    if (sched_editor_color_row != nullptr) {
        if (is_light) {
            lv_obj_clear_flag(sched_editor_color_row, LV_OBJ_FLAG_HIDDEN);
            for (int i = 0; i < 3; i++) {
                if (editor_mode_btns[i] != nullptr) {
                    if (i + 1 == activeMode) {
                        lv_obj_set_style_border_color(editor_mode_btns[i], lv_color_make(250, 204, 21), 0);
                        lv_obj_set_style_border_width(editor_mode_btns[i], 2, 0);
                    } else {
                        lv_obj_set_style_border_width(editor_mode_btns[i], 0, 0);
                    }
                }
            }
        } else {
            lv_obj_add_flag(sched_editor_color_row, LV_OBJ_FLAG_HIDDEN);
        }
    }
}


static void capture_sched_snapshot() {
    switch (current_editor_device) {
    case ScheduleDevice::Light:
        sched_snapshot.mode = cfg.lightMode;
        sched_snapshot.startH = cfg.lightStartHour;
        sched_snapshot.startM = cfg.lightStartMinute;
        sched_snapshot.endH = cfg.lightEndHour;
        sched_snapshot.endM = cfg.lightEndMinute;
        sched_snapshot.colorMode = cfg.lightSchedColorMode;
        break;
    case ScheduleDevice::PlantLight:
        sched_snapshot.mode = cfg.plantLightMode;
        sched_snapshot.startH = cfg.plantStartHour;
        sched_snapshot.startM = cfg.plantStartMinute;
        sched_snapshot.endH = cfg.plantEndHour;
        sched_snapshot.endM = cfg.plantEndMinute;
        sched_snapshot.colorMode = cfg.plantSchedColorMode;
        break;
    case ScheduleDevice::Filter:
        sched_snapshot.mode = cfg.filterMode;
        sched_snapshot.startH = cfg.filterStartHour;
        sched_snapshot.startM = cfg.filterStartMinute;
        sched_snapshot.endH = cfg.filterEndHour;
        sched_snapshot.endM = cfg.filterEndMinute;
        sched_snapshot.colorMode = 0;
        break;
    case ScheduleDevice::Air:
        sched_snapshot.mode = cfg.airMode;
        sched_snapshot.startH = cfg.airStartHour;
        sched_snapshot.startM = cfg.airStartMinute;
        sched_snapshot.endH = cfg.airEndHour;
        sched_snapshot.endM = cfg.airEndMinute;
        sched_snapshot.colorMode = 0;
        break;
    case ScheduleDevice::QuietHours:
        sched_snapshot.mode = cfg.quietHoursEnabled ? 1 : 0;
        sched_snapshot.startH = cfg.quietStartHour;
        sched_snapshot.startM = cfg.quietStartMinute;
        sched_snapshot.endH = cfg.quietEndHour;
        sched_snapshot.endM = cfg.quietEndMinute;
        sched_snapshot.colorMode = 0;
        break;
    default:
        break;
    }
}

static bool is_sched_changed() {
    switch (current_editor_device) {
    case ScheduleDevice::Light:
        return (sched_snapshot.mode != cfg.lightMode ||
                sched_snapshot.startH != cfg.lightStartHour ||
                sched_snapshot.startM != cfg.lightStartMinute ||
                sched_snapshot.endH != cfg.lightEndHour ||
                sched_snapshot.endM != cfg.lightEndMinute ||
                sched_snapshot.colorMode != cfg.lightSchedColorMode);
    case ScheduleDevice::PlantLight:
        return (sched_snapshot.mode != cfg.plantLightMode ||
                sched_snapshot.startH != cfg.plantStartHour ||
                sched_snapshot.startM != cfg.plantStartMinute ||
                sched_snapshot.endH != cfg.plantEndHour ||
                sched_snapshot.endM != cfg.plantEndMinute ||
                sched_snapshot.colorMode != cfg.plantSchedColorMode);
    case ScheduleDevice::Filter:
        return (sched_snapshot.mode != cfg.filterMode ||
                sched_snapshot.startH != cfg.filterStartHour ||
                sched_snapshot.startM != cfg.filterStartMinute ||
                sched_snapshot.endH != cfg.filterEndHour ||
                sched_snapshot.endM != cfg.filterEndMinute);
    case ScheduleDevice::Air:
        return (sched_snapshot.mode != cfg.airMode ||
                sched_snapshot.startH != cfg.airStartHour ||
                sched_snapshot.startM != cfg.airStartMinute ||
                sched_snapshot.endH != cfg.airEndHour ||
                sched_snapshot.endM != cfg.airEndMinute);
    case ScheduleDevice::QuietHours:
        return (sched_snapshot.mode != (cfg.quietHoursEnabled ? 1 : 0) ||
                sched_snapshot.startH != cfg.quietStartHour ||
                sched_snapshot.startM != cfg.quietStartMinute ||
                sched_snapshot.endH != cfg.quietEndHour ||
                sched_snapshot.endM != cfg.quietEndMinute);
    default:
        return false;
    }
}

static void back_sched_editor_cb(lv_event_t *e) {
    LV_UNUSED(e);
    if (is_sched_changed()) {
        sanitize_config(cfg);
        gui_app_save_settings();
        gui_sync_widgets_to_state();
        show_top_notification("Zapisano zmiany", true);
    }
    delete_runtime_subpages(true);
}

static void capture_feed_snapshot() {
    feed_snapshot.enabled = cfg.feedEnabled;
    feed_snapshot.days = cfg.feedDays;
    feed_snapshot.count = cfg.feedCount;
    feed_snapshot.hour1 = cfg.feedHour1;
    feed_snapshot.minute1 = cfg.feedMinute1;
    feed_snapshot.hour2 = cfg.feedHour2;
    feed_snapshot.minute2 = cfg.feedMinute2;
}

static bool is_feed_changed() {
    return (feed_snapshot.enabled != cfg.feedEnabled ||
            feed_snapshot.days != cfg.feedDays ||
            feed_snapshot.count != cfg.feedCount ||
            feed_snapshot.hour1 != cfg.feedHour1 ||
            feed_snapshot.minute1 != cfg.feedMinute1 ||
            feed_snapshot.hour2 != cfg.feedHour2 ||
            feed_snapshot.minute2 != cfg.feedMinute2);
}

static void back_feed_editor_cb(lv_event_t *e) {
    LV_UNUSED(e);
    if (is_feed_changed()) {
        sanitize_config(cfg);
        gui_app_save_settings();
        gui_sync_widgets_to_state();
        show_top_notification("Zapisano zmiany", true);
    }
    delete_runtime_subpages(true);
}

static void capture_heater_snapshot() {
    heater_snapshot.mode = cfg.heaterMode;
    heater_snapshot.targetTemp = cfg.targetTemp;
    heater_snapshot.tempHysteresis = cfg.tempHysteresis;
}

static bool is_heater_changed() {
    return (heater_snapshot.mode != cfg.heaterMode ||
            heater_snapshot.targetTemp != cfg.targetTemp ||
            heater_snapshot.tempHysteresis != cfg.tempHysteresis);
}

static void back_heater_cb(lv_event_t *e) {
    LV_UNUSED(e);
    if (is_heater_changed()) {
        sanitize_config(cfg);
        gui_app_save_settings();
        gui_sync_widgets_to_state();
        show_top_notification("Zapisano zmiany", true);
    }
    delete_runtime_subpages(true);
}

static void capture_ph_snapshot() {
    ph_snapshot.showPh = cfg.showPhSensor;
}

static bool is_ph_changed() {
    return (ph_snapshot.showPh != cfg.showPhSensor);
}

static void back_ph_cb(lv_event_t *e) {
    LV_UNUSED(e);
    if (is_ph_changed()) {
        sanitize_config(cfg);
        gui_app_save_settings();
        delete_runtime_subpages(true);
        rebuild_gui_tree_for_theme();
        show_top_notification("Zapisano zmiany", true);
    } else {
        delete_runtime_subpages(true);
    }
}

static void capture_clock_snapshot() {
    clock_snapshot.hour = clock_hour;
    clock_snapshot.minute = clock_minute;
    clock_snapshot.second = clock_second;
    clock_snapshot.day = clock_day;
    clock_snapshot.month = clock_month;
    clock_snapshot.year = clock_year;
}

static bool is_clock_changed() {
    return (clock_snapshot.hour != clock_hour ||
            clock_snapshot.minute != clock_minute ||
            clock_snapshot.second != clock_second ||
            clock_snapshot.day != clock_day ||
            clock_snapshot.month != clock_month ||
            clock_snapshot.year != clock_year);
}

static void capture_screen_snapshot() {
    screen_snapshot.alwaysScreenOn = cfg.alwaysScreenOn;
    screen_snapshot.ldrThemeEnabled = cfg.ldrThemeEnabled;
    screen_snapshot.manualLightTheme = cfg.manualLightTheme;
}

static bool is_screen_changed() {
    return screen_snapshot.alwaysScreenOn != cfg.alwaysScreenOn ||
           screen_snapshot.ldrThemeEnabled != cfg.ldrThemeEnabled ||
           screen_snapshot.manualLightTheme != cfg.manualLightTheme;
}

static void capture_sound_snapshot() {
    sound_snapshot.soundEnabled = cfg.soundEnabled;
    sound_snapshot.quietHoursEnabled = cfg.quietHoursEnabled;
    sound_snapshot.quietStartHour = cfg.quietStartHour;
    sound_snapshot.quietStartMinute = cfg.quietStartMinute;
    sound_snapshot.quietEndHour = cfg.quietEndHour;
    sound_snapshot.quietEndMinute = cfg.quietEndMinute;
}

static bool is_sound_changed() {
    return sound_snapshot.soundEnabled != cfg.soundEnabled ||
           sound_snapshot.quietHoursEnabled != cfg.quietHoursEnabled ||
           sound_snapshot.quietStartHour != cfg.quietStartHour ||
           sound_snapshot.quietStartMinute != cfg.quietStartMinute ||
           sound_snapshot.quietEndHour != cfg.quietEndHour ||
           sound_snapshot.quietEndMinute != cfg.quietEndMinute;
}

static void back_clock_cb(lv_event_t *e) {
    LV_UNUSED(e);
    if (is_clock_changed()) {
        if (gui_save_clock_settings(true, "manual")) {
            show_top_notification("Zapisano czas", true);
        } else {
            show_top_notification("Nie zapisano czasu", false);
        }
    }
    delete_runtime_subpages(true);
}

static void open_sched_editor_cb(lv_event_t *e) {
    ScheduleDevice requested_device = static_cast<ScheduleDevice>(
        reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    pin_guard_execute_or_prompt(PinAction::OpenScheduleEditor,
                                static_cast<intptr_t>(requested_device), false);
}

static void open_sched_editor_authorized(ScheduleDevice device) {
    const ActiveSubpage target = (device == ScheduleDevice::Feed) ? ActiveSubpage::FeedEditor : ActiveSubpage::SchedEditor;
    log_schedule_editor_enter_request(device, target, "schedule_tile");
    current_editor_device = device;
    if (current_editor_device == ScheduleDevice::Feed) {
        capture_feed_snapshot();
        open_or_build_subpage(ActiveSubpage::FeedEditor);
    } else {
        capture_sched_snapshot();
        open_or_build_subpage(ActiveSubpage::SchedEditor);
    }
    update_editor_fields();
}

static void cycle_editor_mode_cb(lv_event_t *e) {
    LV_UNUSED(e);
    play_system_sound(SoundType::Click);
    switch (current_editor_device) {
    case ScheduleDevice::Light:
        cycle_schedule_mode(cfg.lightMode);
        break;
    case ScheduleDevice::PlantLight:
        cycle_schedule_mode(cfg.plantLightMode);
        break;
    case ScheduleDevice::Filter:
        cycle_schedule_mode(cfg.filterMode);
        break;
    case ScheduleDevice::Air:
        cycle_schedule_mode(cfg.airMode);
        break;
    default:
        break;
    }
    update_editor_fields();
    gui_sync_widgets_to_state();
}

static void cycle_editor_light_mode_cb(lv_event_t *e) {
    LV_UNUSED(e);
    play_system_sound(SoundType::Click);
    switch (current_editor_device) {
    case ScheduleDevice::Light:
        cycle_light_color_mode(cfg.lightColorMode);
        break;
    case ScheduleDevice::PlantLight:
        cycle_light_color_mode(cfg.plantLightColorMode);
        break;
    default:
        return;
    }
    update_editor_fields();
    gui_sync_widgets_to_state();
}

static void set_time_pair(uint8_t &h, uint8_t &m, int field, int delta) {
    if (field <= 2) {
        int new_h = h + delta;
        if (new_h < 0) new_h = 23;
        if (new_h > 23) new_h = 0;
        h = static_cast<uint8_t>(new_h);
    } else {
        int new_m = m + delta * MINUTE_STEP;
        if (new_m < 0) new_m = 60 - MINUTE_STEP;
        if (new_m > 59) new_m = 0;
        m = snap_minute(new_m);
    }
}

static void feed_day_click_cb(lv_event_t *e) {
    play_system_sound(SoundType::Click);
    int day_idx = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    if (day_idx >= 0 && day_idx < 7) {
        cfg.feedDays ^= (1 << day_idx);
        gui_sync_widgets_to_state();
    }
}

static void feed_freq_click_cb(lv_event_t *e) {
    LV_UNUSED(e);
    play_system_sound(SoundType::Click);
    cfg.feedCount = (cfg.feedCount == 2) ? 1 : 2;
    gui_sync_widgets_to_state();
}

static void adjust_feed_time_new(lv_event_t *e) {
    play_system_sound(SoundType::Click);
    const int param = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    int time_idx = param / 100;
    int field = (param % 100) / 10;
    int delta_code = param % 10;
    
    int delta = (delta_code == 1) ? 1 : -1;
    
    uint8_t *h_ptr = (time_idx == 1) ? &cfg.feedHour1 : &cfg.feedHour2;
    uint8_t *m_ptr = (time_idx == 1) ? &cfg.feedMinute1 : &cfg.feedMinute2;
    
    if (field == 1) { // Hour
        int new_h = *h_ptr + delta;
        if (new_h < 0) new_h = 23;
        if (new_h > 23) new_h = 0;
        *h_ptr = static_cast<uint8_t>(new_h);
    } else { // Minute
        int new_m = *m_ptr + delta * MINUTE_STEP;
        if (new_m < 0) new_m = 60 - MINUTE_STEP;
        if (new_m > 59) new_m = 0;
        *m_ptr = snap_minute(new_m);
    }
    
    gui_sync_widgets_to_state();
}

static void adjust_schedule_time_cb(lv_event_t *e) {
    play_system_sound(SoundType::Click);
    const int param = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    int action = param / 10;
    int delta = param % 10;
    if (delta == 9) delta = -1;
    if (delta > 5) delta -= 10;
    
    uint8_t *h_ptr = nullptr;
    uint8_t *m_ptr = nullptr;
    
    switch (current_editor_device) {
    case ScheduleDevice::Light:
        if (action <= 2) { h_ptr = &cfg.lightStartHour; m_ptr = &cfg.lightStartMinute; }
        else { h_ptr = &cfg.lightEndHour; m_ptr = &cfg.lightEndMinute; }
        break;
    case ScheduleDevice::PlantLight:
        if (action <= 2) { h_ptr = &cfg.plantStartHour; m_ptr = &cfg.plantStartMinute; }
        else { h_ptr = &cfg.plantEndHour; m_ptr = &cfg.plantEndMinute; }
        break;
    case ScheduleDevice::Filter:
        if (action <= 2) { h_ptr = &cfg.filterStartHour; m_ptr = &cfg.filterStartMinute; }
        else { h_ptr = &cfg.filterEndHour; m_ptr = &cfg.filterEndMinute; }
        break;
    case ScheduleDevice::Air:
        if (action <= 2) { h_ptr = &cfg.airStartHour; m_ptr = &cfg.airStartMinute; }
        else { h_ptr = &cfg.airEndHour; m_ptr = &cfg.airEndMinute; }
        break;
    case ScheduleDevice::QuietHours:
        if (action <= 2) { h_ptr = &cfg.quietStartHour; m_ptr = &cfg.quietStartMinute; }
        else { h_ptr = &cfg.quietEndHour; m_ptr = &cfg.quietEndMinute; }
        break;
    default:
        return;
    }
    
    if (h_ptr != nullptr && m_ptr != nullptr) {
        set_time_pair(*h_ptr, *m_ptr, (action == 1 || action == 3) ? 1 : 3, delta);
    }
    
    update_editor_fields();
    gui_sync_widgets_to_state();
}

static void editor_mode_btn_cb(lv_event_t *e) {
    play_system_sound(SoundType::Click);
    int mode = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e))) + 1; // 1 = DAY, 2 = DAYBREAK, 3 = NIGHT
    switch (current_editor_device) {
    case ScheduleDevice::Light:
        cfg.lightSchedColorMode = mode;
        break;
    case ScheduleDevice::PlantLight:
        cfg.plantSchedColorMode = mode;
        break;
    default:
        break;
    }
    update_editor_fields();
    gui_sync_widgets_to_state();
}

static lv_obj_t *subpage_root_for(ActiveSubpage subpage) {
    switch (subpage) {
    case ActiveSubpage::Wifi: return subpage_wifi;
    case ActiveSubpage::Screen: return subpage_screen;
    case ActiveSubpage::Logs: return subpage_logs;
    case ActiveSubpage::Clock: return subpage_clock;
    case ActiveSubpage::Diagnostics: return subpage_diagnostics;
    case ActiveSubpage::Power: return subpage_power;
    case ActiveSubpage::Sounds: return subpage_sounds;
    case ActiveSubpage::FeedEditor: return subpage_feed_editor;
    case ActiveSubpage::SchedEditor: return subpage_sched_editor;
    case ActiveSubpage::Heater: return subpage_heater;
    case ActiveSubpage::Ph: return subpage_ph;
    case ActiveSubpage::Service: return subpage_service;
    case ActiveSubpage::Hardware: return subpage_hardware;
    case ActiveSubpage::Co2: return subpage_co2;
    case ActiveSubpage::Ec: return subpage_ec;
    case ActiveSubpage::WaterLevel: return subpage_water;
    case ActiveSubpage::Leak: return subpage_leak;
    case ActiveSubpage::Flow: return subpage_flow;
    default: return nullptr;
    }
}

static void reset_subpage_refs(ActiveSubpage subpage) {
    switch (subpage) {
    case ActiveSubpage::Wifi:
        subpage_wifi = nullptr;
        wifi_main_panel = nullptr;
        wifi_sta_panel = nullptr;
        wifi_pwd_panel = nullptr;
        wifi_ota_panel = nullptr;
        wifi_info_card = nullptr;
        wifi_ssid_lbl = nullptr;
        wifi_ip_lbl = nullptr;
        wifi_status_message_lbl = nullptr;
        wifi_mode_lbl = nullptr;
        wifi_rssi_lbl = nullptr;
        wifi_mac_lbl = nullptr;
        sta_list_obj = nullptr;
        btn_sta = nullptr;
        btn_ota = nullptr;
        btn_disconnect = nullptr;
        wifi_pwd_ta = nullptr;
        wifi_pwd_kb = nullptr;
        wifi_pwd_title_lbl = nullptr;
        break;
    case ActiveSubpage::Screen:
        subpage_screen = nullptr;
        screen_always_on_sw = nullptr;
        screen_manual_theme_sw = nullptr;
        screen_ldr_enable_sw = nullptr;
        diag_dev_mode_sw = nullptr;
        break;
    case ActiveSubpage::Logs:
        subpage_logs = nullptr;
        log_list_normal = nullptr;
        log_list_important = nullptr;
        btn_log_normal = nullptr;
        btn_log_important = nullptr;
        break;
    case ActiveSubpage::Clock:
        subpage_clock = nullptr;
        label_clock_time = nullptr;
        label_clock_date = nullptr;
        clock_ntp_row = nullptr;
        btn_sync_ntp_global = nullptr;
        btn_sync_ntp_lbl_global = nullptr;
        break;
    case ActiveSubpage::Diagnostics:
        subpage_diagnostics = nullptr;
        diag_uptime_lbl = nullptr;
        diag_heap_lbl = nullptr;
        diag_reset_reason_lbl = nullptr;
        diag_restarts_lbl = nullptr;
        diag_cpu_temp_lbl = nullptr;
        diag_cpu_freq_lbl = nullptr;
        diag_flash_lbl = nullptr;
        diag_adc_lbl = nullptr;
        diag_mcp_lbl = nullptr;
        diag_queue_lbl = nullptr;
        diag_ldr_lbl = nullptr;
        diag_eco_lbl = nullptr;
        diag_rtc_lbl = nullptr;
        diag_dev_mode_sw = nullptr;
        break;
    case ActiveSubpage::Power:
        subpage_power = nullptr;
        power_state_lbl = nullptr;
        power_modem_sleep_sw = nullptr;
        power_warning_lbl_global = nullptr;
        break;
    case ActiveSubpage::Sounds:
        subpage_sounds = nullptr;
        sound_enable_sw = nullptr;
        sound_quiet_enable_sw = nullptr;
        sound_quiet_sched_lbl = nullptr;
        break;
    case ActiveSubpage::FeedEditor:
        subpage_feed_editor = nullptr;
        memset(feed_day_btns, 0, sizeof(feed_day_btns));
        feed_enable_sw = nullptr;
        feed_freq_btn = nullptr;
        feed_time2_row = nullptr;
        feed_time1_h_lbl = nullptr;
        feed_time1_m_lbl = nullptr;
        feed_time2_h_lbl = nullptr;
        feed_time2_m_lbl = nullptr;
        feed_editor_mode_lbl = nullptr;
        break;
    case ActiveSubpage::SchedEditor:
        subpage_sched_editor = nullptr;
        editor_title_lbl = nullptr;
        editor_mode_lbl = nullptr;
        editor_start_h_lbl = nullptr;
        editor_start_m_lbl = nullptr;
        editor_start_hour_lbl = nullptr;
        editor_start_min_lbl = nullptr;
        editor_hour_lbl = nullptr;
        editor_hourly_mode_lbl = nullptr;
        sched_editor_mode_btn = nullptr;
        sched_editor_start_h_lbl = nullptr;
        sched_editor_start_m_lbl = nullptr;
        sched_editor_end_h_lbl = nullptr;
        sched_editor_end_m_lbl = nullptr;
        sched_editor_color_row = nullptr;
        for (int i = 0; i < 4; ++i) {
            editor_mode_btns[i] = nullptr;
        }
        for (int i = 0; i < 24; ++i) {
            timeline_blocks[i] = nullptr;
        }
        break;
    case ActiveSubpage::Heater:
        subpage_heater = nullptr;
        temp_auto_sw = nullptr;
        temp_target_val_lbl = nullptr;
        temp_hysteresis_val_lbl = nullptr;
        temp_pump_power_lbl = nullptr;
        temp_pump_power_slider = nullptr;
        break;
    case ActiveSubpage::Ph:
        subpage_ph = nullptr;
        screen_ph_enable_sw = nullptr;
        break;
    case ActiveSubpage::Service:
        subpage_service = nullptr;
        service_light_sw = nullptr;
        service_filter_sw = nullptr;
        service_vol_lbl = nullptr;
        break;
    case ActiveSubpage::Hardware:
        subpage_hardware = nullptr;
        hw_heater_sw = nullptr;
        hw_aerator_sw = nullptr;
        hw_co2_sw = nullptr;
        hw_ec_sw = nullptr;
        hw_water_level_sw = nullptr;
        hw_leak_sw = nullptr;
        hw_flow_sw = nullptr;
        hw_matrix = nullptr;
        hw_summary_lbl = nullptr;
        break;
    case ActiveSubpage::Co2:
        subpage_co2 = nullptr;
        co2_state_lbl = nullptr;
        co2_ph_lbl = nullptr;
        co2_mcp_lbl = nullptr;
        break;
    case ActiveSubpage::Ec:
        subpage_ec = nullptr;
        ec_value_lbl = nullptr;
        ec_raw_lbl = nullptr;
        break;
    case ActiveSubpage::WaterLevel:
        subpage_water = nullptr;
        water_state_lbl = nullptr;
        break;
    case ActiveSubpage::Leak:
        subpage_leak = nullptr;
        leak_state_lbl = nullptr;
        break;
    case ActiveSubpage::Flow:
        subpage_flow = nullptr;
        flow_state_lbl = nullptr;
        break;
    default:
        break;
    }
}

static void delete_one_subpage(ActiveSubpage subpage, bool async_delete) {
    lv_obj_t *root = subpage_root_for(subpage);
    if (root == nullptr) {
        reset_subpage_refs(subpage);
        return;
    }

    if (subpage == ActiveSubpage::Wifi) {
        free_wifi_scan_user_data();
    }
    reset_subpage_refs(subpage);
    if (lv_obj_is_valid(root)) {
        if (async_delete) {
            delete_obj_async(root);
            log_subpage_ram("subpage_delete_scheduled", subpage);
            schedule_delayed_ram_checkpoint("subpage_deleted_async");
            return;
        } else {
            lv_obj_del(root);
        }
    }
    log_subpage_ram("subpage_deleted", subpage);
}

static void delete_runtime_subpages_except(ActiveSubpage keep, bool async_delete) {
    auto remove_if_not_kept = [keep, async_delete](ActiveSubpage subpage) {
        if (subpage != keep) {
            delete_one_subpage(subpage, async_delete);
        }
    };

    remove_if_not_kept(ActiveSubpage::Wifi);
    remove_if_not_kept(ActiveSubpage::Screen);
    remove_if_not_kept(ActiveSubpage::Logs);
    remove_if_not_kept(ActiveSubpage::Clock);
    remove_if_not_kept(ActiveSubpage::Diagnostics);
    remove_if_not_kept(ActiveSubpage::Power);
    remove_if_not_kept(ActiveSubpage::Sounds);
    remove_if_not_kept(ActiveSubpage::FeedEditor);
    remove_if_not_kept(ActiveSubpage::SchedEditor);
    remove_if_not_kept(ActiveSubpage::Heater);
    remove_if_not_kept(ActiveSubpage::Ph);
    remove_if_not_kept(ActiveSubpage::Service);
    remove_if_not_kept(ActiveSubpage::Hardware);
    remove_if_not_kept(ActiveSubpage::Co2);
    remove_if_not_kept(ActiveSubpage::Ec);
    remove_if_not_kept(ActiveSubpage::WaterLevel);
    remove_if_not_kept(ActiveSubpage::Leak);
    remove_if_not_kept(ActiveSubpage::Flow);
}

static void delete_runtime_subpages(bool async_delete) {
    delete_runtime_subpages_except(ActiveSubpage::None, async_delete);
    current_subpage = ActiveSubpage::None;
}

static bool is_bundle_subpage(ActiveSubpage target) {
    switch (target) {
    case ActiveSubpage::Wifi:
    case ActiveSubpage::Screen:
    case ActiveSubpage::Logs:
    case ActiveSubpage::Clock:
    case ActiveSubpage::Diagnostics:
    case ActiveSubpage::Power:
    case ActiveSubpage::Sounds:
    case ActiveSubpage::FeedEditor:
    case ActiveSubpage::SchedEditor:
    case ActiveSubpage::Heater:
    case ActiveSubpage::Ph:
    case ActiveSubpage::Service:
        return true;
    default:
        return false;
    }
}

static void build_single_subpage(ActiveSubpage target) {
    switch (target) {
    case ActiveSubpage::Hardware:
        build_hardware_subpage();
        break;
    case ActiveSubpage::Co2:
        build_co2_subpage();
        break;
    case ActiveSubpage::Ec:
        build_ec_subpage();
        break;
    case ActiveSubpage::WaterLevel:
        build_water_subpage();
        break;
    case ActiveSubpage::Leak:
        build_leak_subpage();
        break;
    case ActiveSubpage::Flow:
        build_flow_subpage();
        break;
    default:
        break;
    }
}

static bool open_or_build_subpage(ActiveSubpage target) {
    if (target == ActiveSubpage::None) {
        return false;
    }

    delete_runtime_subpages(false);
    log_subpage_ram("subpage_build_start", target);

    if (is_bundle_subpage(target)) {
        if (!ensure_runtime_ui_heap(nav_subpage_name(target), UI_RUNTIME_SUBPAGE_MIN_FREE, UI_RUNTIME_BIGGEST_MIN)) {
            return false;
        }
        build_subpages(target);
    } else {
        if (!ensure_runtime_ui_heap(nav_subpage_name(target), UI_RUNTIME_HARDWARE_MIN_FREE, UI_RUNTIME_HARDWARE_MIN_LARGEST)) {
            return false;
        }
        build_single_subpage(target);
    }

    lv_obj_t *target_subpage = subpage_root_for(target);
    if (target_subpage == nullptr || !lv_obj_is_valid(target_subpage)) {
        reset_subpage_refs(target);
        Serial.printf("UI_NAV: subpage allocation failed target=%s\n", nav_subpage_name(target));
        return false;
    }

    current_subpage = target;
    lv_obj_clear_flag(target_subpage, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(target_subpage);
    gui_sync_widgets_to_state();
    gui_app_update_wifi(wifi_connected ? 1 : 0, wifi_rssi);
    log_subpage_ram("subpage_enter", target);
    return true;
}

static bool web_ui_session_id_valid(const char *session_id) {
    return aquarium::WebActivityTracker::valid_session_id(session_id);
}

static uint8_t web_ui_active_client_count(uint32_t now_ms) {
    return web_ui_clients.active_count(now_ms);
}

static void web_ui_track_client(const char *session_id, uint32_t now_ms) {
    web_ui_clients.touch(session_id, now_ms);
}

static void web_ui_release_client(const char *session_id) {
    web_ui_clients.release(session_id);
}

static void web_ui_clear_clients() {
    web_ui_clients.clear();
}

static void ota_portal_mark_web_activity() {
    web_ui_last_request_ms = millis();
}

static bool ota_portal_has_recent_web_activity(uint32_t now_ms) {
    if (!ota_portal_running) {
        return false;
    }
    if (web_ui_active_client_count(now_ms) > 0U) {
        return true;
    }
    if (web_ui_last_request_ms == 0) {
        return false;
    }
    return static_cast<uint32_t>(now_ms - web_ui_last_request_ms) <= WEB_UI_ACTIVITY_TIMEOUT_MS;
}

static bool gui_web_focus_blocks_local_ui() {
    return web_ui_focus_active;
}

static void web_client_back_cb(lv_event_t *e) {
    LV_UNUSED(e);
    play_system_sound(SoundType::Click);

    const uint32_t now_ms = millis();
    if (ota_portal_has_recent_web_activity(now_ms)) {
        gui_web_client_screen_update(true);
        if (web_client_status_lbl != nullptr) {
            lv_label_set_text(web_client_status_lbl, "WWW aktywne. Zamknij strone.");
            lv_obj_set_style_text_color(web_client_status_lbl, lv_color_make(245, 158, 11), 0);
        }
        return;
    }

    gui_web_focus_exit();
}

static void gui_web_client_screen_delete() {
    if (web_client_screen != nullptr && lv_obj_is_valid(web_client_screen)) {
        lv_obj_add_flag(web_client_screen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_del_async(web_client_screen);
    }
    web_client_screen = nullptr;
    web_client_state_lbl = nullptr;
    web_client_url_lbl = nullptr;
    web_client_status_lbl = nullptr;
    web_client_ssid_lbl = nullptr;
    web_client_ip_lbl = nullptr;
    web_client_rssi_lbl = nullptr;
    web_client_uptime_lbl = nullptr;
    web_client_progress_bar = nullptr;
    web_client_progress_lbl = nullptr;
    web_ui_last_screen_ms = 0;
}

static void gui_web_client_screen_create() {
    if (web_client_screen != nullptr && lv_obj_is_valid(web_client_screen)) {
        lv_obj_move_foreground(web_client_screen);
        return;
    }

    web_client_screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(web_client_screen, 320, 240);
    lv_obj_set_pos(web_client_screen, 0, 0);
    lv_obj_set_style_pad_all(web_client_screen, 0, 0);
    style_panel(web_client_screen, lv_color_make(3, 7, 18), lv_color_make(3, 7, 18), 0);

    lv_obj_t *header = lv_obj_create(web_client_screen);
    lv_obj_set_size(header, 320, 36);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    style_panel(header, lv_color_make(8, 13, 24), lv_color_make(6, 182, 212), 0);

    lv_obj_t *back_btn = create_button(header, LV_SYMBOL_LEFT " Wstecz", 76, 26,
                                       lv_color_make(30, 41, 59), web_client_back_cb, nullptr);
    lv_obj_set_pos(back_btn, 6, 5);

    lv_obj_t *title_lbl = create_label(header, "Panel WWW aktywny", lv_color_white(), &lv_font_montserrat_14);
    lv_obj_align(title_lbl, LV_ALIGN_CENTER, 20, 0);

    lv_obj_t *state_card = create_card(web_client_screen, 304, 116, 8, 44);
    lv_obj_set_style_border_color(state_card, lv_color_make(6, 182, 212), 0);
    web_client_state_lbl = create_fixed_label(state_card, "Klient strony aktywny",
                                              lv_color_make(125, 211, 252), &lv_font_montserrat_16,
                                              280, 8, 8, LV_LABEL_LONG_DOT);
    web_client_url_lbl = create_fixed_label(state_card, "URL: --",
                                            theme_text_main(), &lv_font_montserrat_12,
                                            280, 8, 36, LV_LABEL_LONG_DOT);
    web_client_status_lbl = create_fixed_label(state_card, "Czujniki i sterowanie pracuja. UI lokalny ograniczony.",
                                               lv_color_make(148, 163, 184), &lv_font_montserrat_12,
                                               280, 8, 62, LV_LABEL_LONG_DOT);

    web_client_progress_bar = lv_bar_create(state_card);
    lv_obj_set_size(web_client_progress_bar, 214, 8);
    lv_obj_set_pos(web_client_progress_bar, 8, 90);
    lv_bar_set_range(web_client_progress_bar, 0, 100);
    lv_bar_set_value(web_client_progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(web_client_progress_bar, resolve_bg_color(lv_color_make(30, 41, 59)), LV_PART_MAIN);
    lv_obj_set_style_bg_color(web_client_progress_bar, lv_color_make(6, 182, 212), LV_PART_INDICATOR);
    lv_obj_add_flag(web_client_progress_bar, LV_OBJ_FLAG_HIDDEN);

    web_client_progress_lbl = create_fixed_label(state_card, "OTA: --",
                                                 lv_color_make(125, 211, 252), &lv_font_montserrat_12,
                                                 64, 230, 84, LV_LABEL_LONG_DOT);
    lv_obj_add_flag(web_client_progress_lbl, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *info_card = create_card(web_client_screen, 304, 38, 8, 164);
    web_client_ssid_lbl = create_fixed_label(info_card, LV_SYMBOL_WIFI "  SSID: --",
                                             theme_text_main(), &lv_font_montserrat_12,
                                             138, 8, 0, LV_LABEL_LONG_DOT);
    web_client_ip_lbl = create_fixed_label(info_card, "IP: --",
                                           theme_text_main(), &lv_font_montserrat_12,
                                           138, 158, 0, LV_LABEL_LONG_DOT);
    web_client_rssi_lbl = create_fixed_label(info_card, "RSSI: --",
                                             theme_text_main(), &lv_font_montserrat_12,
                                             138, 8, 18, LV_LABEL_LONG_DOT);
    web_client_uptime_lbl = create_fixed_label(info_card, "WWW: --",
                                               theme_text_main(), &lv_font_montserrat_12,
                                               138, 158, 18, LV_LABEL_LONG_DOT);

    lv_obj_t *disconnect_btn = create_button(web_client_screen, LV_SYMBOL_CLOSE " Rozlacz WiFi",
                                             148, 28, lv_color_make(239, 68, 68),
                                             btn_wifi_disc_handler, nullptr);
    lv_obj_set_pos(disconnect_btn, 8, 206);

    lv_obj_t *return_btn = create_button(web_client_screen, LV_SYMBOL_LEFT " Wstecz",
                                         148, 28, lv_color_make(30, 41, 59),
                                         web_client_back_cb, nullptr);
    lv_obj_set_pos(return_btn, 164, 206);

    lv_obj_move_foreground(web_client_screen);
}

static void gui_web_client_screen_update(bool force) {
    if (!web_ui_focus_active) {
        return;
    }

    gui_web_client_screen_create();
    if (web_client_screen == nullptr || !lv_obj_is_valid(web_client_screen)) {
        return;
    }

    const uint32_t now_ms = millis();
    if (!force && static_cast<uint32_t>(now_ms - web_ui_last_screen_ms) < WEB_UI_CONTROL_REFRESH_MS) {
        return;
    }
    web_ui_last_screen_ms = now_ms;

    const bool sta_online = WiFi.status() == WL_CONNECTED;
    const bool ap_online = wifi_ota_active;
    const bool any_wifi = sta_online || ap_online || is_connecting || WiFi.getMode() != WIFI_OFF;
    const IPAddress ip = sta_online ? WiFi.localIP() : WiFi.softAPIP();
    const String ssid = sta_online ? WiFi.SSID() : WiFi.softAPSSID();
    const uint32_t idle_s = web_ui_last_request_ms == 0 ? 0U : static_cast<uint32_t>((now_ms - web_ui_last_request_ms) / 1000U);
    const bool ota_screen = ota_http_upload_active || ota_reboot_pending || ota_http_update_failed;
    const uint8_t active_web_clients = web_ui_active_client_count(now_ms);

    char url_buf[96];
    if (sta_online || ap_online) {
        snprintf(url_buf, sizeof(url_buf), "URL: http://%u.%u.%u.%u/  %s.local",
                 ip[0], ip[1], ip[2], ip[3], Secrets::OTA_HOSTNAME);
    } else {
        snprintf(url_buf, sizeof(url_buf), "URL: oczekiwanie na WiFi");
    }

    char ssid_buf[96];
    snprintf(ssid_buf, sizeof(ssid_buf), LV_SYMBOL_WIFI "  SSID: %s",
             ssid.length() > 0 ? ssid.c_str() : (sta_online ? selected_ssid : Secrets::OTA_AP_SSID));

    char ip_buf[64];
    snprintf(ip_buf, sizeof(ip_buf), "IP: %u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);

    if (web_client_state_lbl != nullptr) {
        if (ota_http_update_failed) {
            lv_label_set_text(web_client_state_lbl, "Blad OTA");
            lv_obj_set_style_text_color(web_client_state_lbl, lv_color_make(239, 68, 68), 0);
        } else if (ota_http_upload_active) {
            lv_label_set_text(web_client_state_lbl, "Aktualizacja OTA");
            lv_obj_set_style_text_color(web_client_state_lbl, lv_color_make(245, 158, 11), 0);
        } else if (ota_reboot_pending) {
            lv_label_set_text(web_client_state_lbl, "OTA zapisane");
            lv_obj_set_style_text_color(web_client_state_lbl, lv_color_make(16, 185, 129), 0);
        } else if (is_connecting) {
            lv_label_set_text(web_client_state_lbl, "Laczenie z WiFi STA");
            lv_obj_set_style_text_color(web_client_state_lbl, lv_color_make(245, 158, 11), 0);
        } else if (sta_online && ota_portal_running) {
            lv_label_set_text_fmt(web_client_state_lbl, "Panel WWW na STA: %u", static_cast<unsigned>(active_web_clients));
            lv_obj_set_style_text_color(web_client_state_lbl, lv_color_make(16, 185, 129), 0);
        } else if (ap_online) {
            lv_label_set_text_fmt(web_client_state_lbl, "Panel WWW na AP: %u", static_cast<unsigned>(active_web_clients));
            lv_obj_set_style_text_color(web_client_state_lbl, lv_color_make(6, 182, 212), 0);
        } else if (any_wifi) {
            lv_label_set_text(web_client_state_lbl, "WiFi aktywne");
            lv_obj_set_style_text_color(web_client_state_lbl, lv_color_make(125, 211, 252), 0);
        } else {
            lv_label_set_text(web_client_state_lbl, "WiFi wylaczone");
            lv_obj_set_style_text_color(web_client_state_lbl, lv_color_make(248, 113, 113), 0);
        }
    }

    set_label_text(web_client_url_lbl, url_buf);
    set_label_text(web_client_ssid_lbl, ssid_buf);
    set_label_text(web_client_ip_lbl, ip_buf);

    if (web_client_rssi_lbl != nullptr) {
        if (sta_online) {
            lv_label_set_text_fmt(web_client_rssi_lbl, "RSSI: %d dBm", wifi_rssi);
        } else if (ap_online) {
            lv_label_set_text_fmt(web_client_rssi_lbl, "AP clients: %u",
                                  static_cast<unsigned>(WiFi.softAPgetStationNum()));
        } else {
            lv_label_set_text(web_client_rssi_lbl, "RSSI: --");
        }
    }

    if (web_client_uptime_lbl != nullptr) {
        lv_label_set_text_fmt(web_client_uptime_lbl, "WWW: %u / %lus",
                              static_cast<unsigned>(active_web_clients),
                              static_cast<unsigned long>(idle_s));
    }

    if (web_client_progress_bar != nullptr && web_client_progress_lbl != nullptr) {
        if (ota_screen) {
            lv_obj_clear_flag(web_client_progress_bar, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(web_client_progress_lbl, LV_OBJ_FLAG_HIDDEN);
            const int progress = ota_http_update_failed ? 0 : (ota_reboot_pending ? 100 : constrain(ota_http_upload_percent, 0, 100));
            lv_bar_set_value(web_client_progress_bar, progress, LV_ANIM_ON);
            if (ota_http_update_failed) {
                lv_label_set_text(web_client_progress_lbl, "ERR");
            } else if (ota_http_upload_percent >= 0 || ota_reboot_pending) {
                lv_label_set_text_fmt(web_client_progress_lbl, "%d%%", progress);
            } else {
                lv_label_set_text_fmt(web_client_progress_lbl, "%lu KB",
                                      static_cast<unsigned long>((ota_http_upload_bytes + 512UL) / 1024UL));
            }
        } else {
            lv_obj_add_flag(web_client_progress_bar, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(web_client_progress_lbl, LV_OBJ_FLAG_HIDDEN);
            lv_bar_set_value(web_client_progress_bar, 0, LV_ANIM_OFF);
        }
    }

    if (web_client_status_lbl != nullptr) {
        if (ota_http_update_failed) {
            lv_label_set_text(web_client_status_lbl,
                              ota_http_update_msg[0] != '\0' ? ota_http_update_msg : "Blad aktualizacji");
            lv_obj_set_style_text_color(web_client_status_lbl, lv_color_make(239, 68, 68), 0);
        } else if (ota_http_upload_active) {
            if (ota_http_upload_total > 0U) {
                lv_label_set_text_fmt(web_client_status_lbl, "Odbieram firmware: %lu/%lu KB",
                                      static_cast<unsigned long>((ota_http_upload_bytes + 512UL) / 1024UL),
                                      static_cast<unsigned long>((ota_http_upload_total + 512UL) / 1024UL));
            } else {
                lv_label_set_text_fmt(web_client_status_lbl, "Odbieram firmware: %lu KB",
                                      static_cast<unsigned long>((ota_http_upload_bytes + 512UL) / 1024UL));
            }
            lv_obj_set_style_text_color(web_client_status_lbl, lv_color_make(245, 158, 11), 0);
        } else if (ota_reboot_pending) {
            lv_label_set_text(web_client_status_lbl, "Zapis OK. Restart urzadzenia...");
            lv_obj_set_style_text_color(web_client_status_lbl, lv_color_make(16, 185, 129), 0);
        } else if (ota_portal_has_recent_web_activity(now_ms)) {
            lv_label_set_text(web_client_status_lbl, "WWW + czujniki + sterowanie. Lokalny UI ograniczony.");
            lv_obj_set_style_text_color(web_client_status_lbl, lv_color_make(148, 163, 184), 0);
        } else {
            lv_label_set_text(web_client_status_lbl, "WWW wygaslo. Wstecz przywraca panel.");
            lv_obj_set_style_text_color(web_client_status_lbl, lv_color_make(16, 185, 129), 0);
        }
    }
}

static void gui_web_focus_apply_wifi_controls(bool force) {
    if (!web_ui_focus_active) {
        return;
    }

    const uint32_t now_ms = millis();
    if (!force && static_cast<uint32_t>(now_ms - web_ui_last_control_ms) < WEB_UI_CONTROL_REFRESH_MS) {
        return;
    }
    web_ui_last_control_ms = now_ms;

    gui_web_client_screen_update(force);
}

static void gui_web_focus_show_wifi_main_panel() {
    if (wifi_sta_panel != nullptr) {
        lv_obj_add_flag(wifi_sta_panel, LV_OBJ_FLAG_HIDDEN);
    }
    if (wifi_pwd_panel != nullptr) {
        lv_obj_add_flag(wifi_pwd_panel, LV_OBJ_FLAG_HIDDEN);
    }
    if (wifi_ota_panel != nullptr) {
        lv_obj_add_flag(wifi_ota_panel, LV_OBJ_FLAG_HIDDEN);
    }
    if (wifi_main_panel != nullptr) {
        lv_obj_clear_flag(wifi_main_panel, LV_OBJ_FLAG_HIDDEN);
    }
    gui_web_client_screen_update(true);
}

static void gui_web_focus_enter() {
    if (!web_ui_focus_active) {
        web_ui_restore_page_index = current_page_index;
        web_ui_restore_subpage = current_subpage;
        web_ui_restore_valid = true;
        web_ui_focus_active = true;
        web_ui_last_control_ms = 0;
        web_ui_last_screen_ms = 0;
        Serial.printf("WEB_FOCUS: enter restore_page=%d restore_subpage=%s\n",
                      web_ui_restore_page_index,
                      nav_subpage_name(web_ui_restore_subpage));
        add_gui_log("Panel WWW aktywny - ekran CYD ograniczony", false);
    }

    gui_web_client_screen_create();
    gui_web_client_screen_update(true);
    gui_web_focus_apply_wifi_controls(true);
}

static void gui_web_focus_exit() {
    if (!web_ui_focus_active) {
        return;
    }

    web_ui_focus_active = false;
    web_ui_last_control_ms = 0;
    web_ui_last_screen_ms = 0;
    web_ui_last_request_ms = 0;
    gui_web_client_screen_delete();
    Serial.println("WEB_FOCUS: exit");
    add_gui_log("Panel WWW nieaktywny - ekran CYD odblokowany", false);

    if (web_ui_restore_valid) {
        const int restore_page = (web_ui_restore_page_index >= 0 &&
                                  web_ui_restore_page_index < PAGE_COUNT) ? web_ui_restore_page_index : 0;
        const ActiveSubpage restore_subpage = web_ui_restore_subpage;
        web_ui_restore_valid = false;

        if (current_page_index != restore_page) {
            switch_to_page(static_cast<uint8_t>(restore_page));
        } else if (restore_subpage == ActiveSubpage::None && current_subpage != ActiveSubpage::None) {
            delete_runtime_subpages(true);
        }

        if (restore_subpage != ActiveSubpage::None && current_subpage != restore_subpage) {
            open_or_build_subpage(restore_subpage);
        }
    }

    gui_app_update_wifi(wifi_ota_active ? 2 : (wifi_connected ? 1 : 0), wifi_rssi);
}

static void gui_web_focus_update() {
    const uint32_t now_ms = millis();
    if (ota_portal_has_recent_web_activity(now_ms)) {
        const bool needs_web_screen =
            web_client_screen == nullptr ||
            !lv_obj_is_valid(web_client_screen);

        if (!web_ui_focus_active || needs_web_screen) {
            gui_web_focus_enter();
        } else {
            gui_web_client_screen_update(false);
            gui_web_focus_apply_wifi_controls(false);
        }
        return;
    }

    gui_web_focus_exit();
}

static void hide_runtime_subpages() {
    delete_runtime_subpages(true);
}

static void open_system_subpage(lv_event_t *e) {
    const ActiveSubpage target = static_cast<ActiveSubpage>(
        reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    log_subpage_enter_request(target, "system_hub");
    play_system_sound(SoundType::Click);
    if (gui_web_focus_blocks_local_ui() && target != ActiveSubpage::Wifi) {
        gui_web_focus_apply_wifi_controls(true);
        return;
    }

    if (target == ActiveSubpage::Clock) {
        capture_clock_snapshot();
    } else if (target == ActiveSubpage::Screen) {
        capture_screen_snapshot();
    } else if (target == ActiveSubpage::Sounds) {
        capture_sound_snapshot();
    }
    open_or_build_subpage(target);
}

static void nav_btn_event_handler(lv_event_t *e) {
    const int index = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    log_tab_enter_request(index);
    play_system_sound(SoundType::Click);
    if (gui_web_focus_blocks_local_ui()) {
        gui_web_focus_enter();
        return;
    }
    if (index >= 0 && index < PAGE_COUNT) {
        switch_to_page(static_cast<uint8_t>(index));
    }
}

static void btn_restart_event_handler(lv_event_t *e) {
    LV_UNUSED(e);
    pin_guard_execute_or_prompt(PinAction::Restart, 0, false);
}

static void restart_authorized() {
    play_system_sound(SoundType::Warning);
    Serial.println("System: Device restart requested.");
    runtime_safety_record_restart(
        RuntimeFaultReason::ManualRestart,
        hal_mcp_latch_all_relays_safe());
    ota_reboot_reason = RuntimeFaultReason::ManualRestart;
    ota_reboot_pending = true;
    ota_reboot_at_ms = millis() + 300U;
}

static void factory_reset_timer_cb(lv_timer_t *timer) {
    LV_UNUSED(timer);
    ESP.restart();
}

static void power_modem_sleep_handler(lv_event_t *e) {
    play_system_sound(SoundType::Click);
    lv_obj_t *obj = lv_event_get_target(e);
    const bool requested = lv_obj_has_state(obj, LV_STATE_CHECKED);
    if (!pin_guard_execute_or_prompt(PinAction::ToggleModemSleep, 0, requested)) {
        set_checked(obj, cfg.modemSleep);
    }
}

static void apply_modem_sleep_authorized(bool enabled) {
    cfg.modemSleep = enabled;
    gui_app_save_settings();
    
    if (cfg.modemSleep) {
        WiFi.disconnect(true);
        stop_ota_portal();
        WiFi.mode(WIFI_OFF);
        wifi_connected = false;
        wifi_rssi = 0;
        gui_app_update_wifi(0, 0);
        add_gui_log("Wlaczono Modem Sleep (Radio OFF)", false);
    } else {
        WiFi.mode(WIFI_STA);
        add_gui_log("Wylaczono Modem Sleep (Radio ON)", false);
    }
    gui_sync_widgets_to_state();
}

static void btn_light_sleep_handler(lv_event_t *e) {
    LV_UNUSED(e);
    pin_guard_execute_or_prompt(PinAction::LightSleep, 0, false);
}

static void prepare_outputs_for_sleep() {
    runtime.lightOn = false;
    runtime.plantLightOn = false;
    runtime.filterOn = false;
    runtime.airOn = false;
    runtime.heaterOn = false;
    runtime.co2On = false;
    runtime.waterFillOn = false;
    const bool outputs_safe = cfg.devMode || hal_mcp_all_relays_safe();
    if (!outputs_safe) {
        add_gui_log("MCP: nie potwierdzono stanu bezpiecznego przed uspieniem",
                    true);
    }
    mcp_outputs = {};
    feeder_pulse_active = false;
    feeder_start_pending = false;
    feeder_pulse_deadline_ms = 0U;
    relay_test_active_mask = 0U;
    relay_test_applied_mask = 0U;
}

static void prepare_network_for_sleep() {
    stop_ota_portal();
    stop_mdns_service();
    if (WiFi.getMode() != WIFI_OFF) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
    }
    wifi_connected = false;
    wifi_rssi = 0;
    is_connecting = false;
}

static void light_sleep_authorized() {
    play_system_sound(SoundType::Warning);
    Serial.println("System: Light sleep requested for 10s.");
    add_gui_log("Uruchamianie Light Sleep (10s)", true);
    
    digitalWrite(HwConfig::Backlight::PIN, LOW);
    esp_sleep_enable_timer_wakeup(10ULL * 1000000ULL);
    esp_light_sleep_start();
    digitalWrite(HwConfig::Backlight::PIN, HIGH);
    
    Serial.println("System: Woke up from light sleep.");
    add_gui_log("Obudzono z Light Sleep", false);
}

static void btn_deep_sleep_handler(lv_event_t *e) {
    LV_UNUSED(e);
    pin_guard_execute_or_prompt(PinAction::DeepSleep, 0, false);
}

static void deep_sleep_authorized() {
    play_system_sound(SoundType::Warning);
    const EcoRuntimeStatus eco = eco_collect_status();
    const uint32_t sleep_seconds = planned_deep_sleep_seconds(eco);
    Serial.printf("System: Deep sleep requested for %lu s, blockers=0x%04x.\n",
                  static_cast<unsigned long>(sleep_seconds),
                  static_cast<unsigned>(eco.blockers));
    char log_line[64];
    snprintf(log_line, sizeof(log_line), "Uruchamianie Deep Sleep (%lus)",
             static_cast<unsigned long>(sleep_seconds));
    add_gui_log(log_line, true);
    
    prepare_network_for_sleep();
    prepare_outputs_for_sleep();
    digitalWrite(HwConfig::Backlight::PIN, LOW);
    esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(sleep_seconds) * 1000000ULL);
    esp_deep_sleep_start();
}

static void btn_hibernation_handler(lv_event_t *e) {
    LV_UNUSED(e);
    pin_guard_execute_or_prompt(PinAction::Hibernation, 0, false);
}

static void hibernation_authorized() {
    play_system_sound(SoundType::Warning);
    Serial.println("System: Hibernation requested for 30s.");
    add_gui_log("Uruchamianie Hibernacji (30s)", true);
    
    prepare_network_for_sleep();
    prepare_outputs_for_sleep();
    digitalWrite(HwConfig::Backlight::PIN, LOW);
    esp_sleep_enable_timer_wakeup(30ULL * 1000000ULL);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_OFF);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);
    esp_deep_sleep_start();
}

static void btn_factory_reset_handler(lv_event_t *e) {
    LV_UNUSED(e);
    pin_guard_execute_or_prompt(PinAction::FactoryReset, 0, false);
}

static void factory_reset_authorized() {
    play_system_sound(SoundType::Warning);
    runtime_safety_record_restart(
        RuntimeFaultReason::FactoryReset,
        hal_mcp_latch_all_relays_safe());
    if (prefs.begin("aquarium", false)) {
        prefs.clear();
        prefs.end();
    }
    device_credentials_factory_reset();
    wifi_credential_store_clear();
    sensor_calibration_store_reset_defaults();
    remote_alarm_relay_clear();
    admin_sessions.clear();
    load_default_config(cfg);
    gui_app_save_settings();
    WiFi.disconnect(true, true);
    if (power_warning_lbl_global != nullptr) {
        lv_label_set_text(power_warning_lbl_global, "Cfg wyczyszczona. Restart...");
        lv_obj_set_style_text_color(power_warning_lbl_global, lv_color_make(16, 185, 129), 0);
    }
    lv_timer_create(factory_reset_timer_cb, 1500, nullptr);
}

static void clear_pending_wifi_password() {
    secure_clear_gui_buffer(
        pending_wifi_password,
        sizeof(pending_wifi_password));
    pending_wifi_password_valid = false;
}

static uint32_t wifi_profile_hash(const char *ssid) {
    uint32_t hash = 2166136261UL;
    if (ssid == nullptr) {
        return hash;
    }
    while (*ssid != '\0') {
        hash ^= static_cast<uint8_t>(*ssid);
        hash *= 16777619UL;
        ++ssid;
    }
    return hash;
}

static bool ensure_sd_directory(const char *path) {
    if (path == nullptr || path[0] == '\0') {
        return false;
    }
    if (SD.exists(path)) {
        return true;
    }
    return SD.mkdir(path);
}

static void write_escaped_config_value(File &file, const char *value) {
    if (value == nullptr) {
        return;
    }

    for (size_t i = 0; value[i] != '\0'; ++i) {
        const uint8_t c = static_cast<uint8_t>(value[i]);
        if (c == '\\') {
            file.print("\\\\");
        } else if (c == '\r') {
            file.print("\\r");
        } else if (c == '\n') {
            file.print("\\n");
        } else if (c == '=') {
            file.print("\\=");
        } else if (c < 32U || c > 126U) {
            char escaped[5];
            snprintf(escaped, sizeof(escaped), "\\x%02X", static_cast<unsigned>(c));
            file.print(escaped);
        } else {
            file.write(c);
        }
    }
}

static bool save_wifi_profile_to_sd(
    const char *ssid,
    const char *password,
    const char *ip,
    int rssi,
    const char *metadata_path = nullptr) {
    if (ssid == nullptr || ssid[0] == '\0' ||
        strnlen(ssid, WIFI_CREDENTIAL_SSID_BYTES) >=
            WIFI_CREDENTIAL_SSID_BYTES) {
        Serial.println("WIFI_SD: skipped profile save, empty SSID.");
        return false;
    }

    if (!wifi_credential_store_save(
            ssid, password != nullptr ? password : "")) {
        Serial.println("WIFI_NVS: credential save failed.");
        return false;
    }

    if (!hal_sd_is_mounted() && !hal_sd_init()) {
        Serial.println(
            "WIFI_NVS: credential saved; SD metadata unavailable.");
        return true;
    }

    if (!ensure_sd_directory("/aq") ||
        !ensure_sd_directory("/aq/config") ||
        !ensure_sd_directory(WIFI_PROFILE_DIR)) {
        Serial.println(
            "WIFI_NVS: credential saved; SD metadata directory unavailable.");
        return true;
    }

    char path[96];
    if (metadata_path != nullptr) {
        const size_t directory_length =
            strlen(WIFI_PROFILE_DIR);
        const size_t path_length =
            strnlen(metadata_path, sizeof(path));
        if (path_length == 0U ||
            path_length >= sizeof(path) ||
            strncmp(
                metadata_path,
                WIFI_PROFILE_DIR,
                directory_length) != 0 ||
            metadata_path[directory_length] != '/' ||
            strstr(metadata_path, "..") != nullptr) {
            Serial.println(
                "WIFI_NVS: rejected unsafe metadata path.");
            return true;
        }
        snprintf(path, sizeof(path), "%s", metadata_path);
    } else {
        snprintf(
            path, sizeof(path),
            "%s/profile_%08lx.cfg",
            WIFI_PROFILE_DIR,
            static_cast<unsigned long>(
                wifi_profile_hash(ssid)));
    }

    char temp_path[80];
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", path);
    if (SD.exists(temp_path)) {
        SD.remove(temp_path);
    }

    File file = SD.open(temp_path, FILE_WRITE);
    if (!file) {
        Serial.printf(
            "WIFI_NVS: credential saved; cannot create metadata %s\n",
            temp_path);
        return true;
    }

    file.println("format=aq-wifi-profile-v2");
    file.println("schema_version=2");
    file.println("credential_store=nvs");
    file.print("ssid=");
    write_escaped_config_value(file, ssid);
    file.println();
    file.print("last_ip=");
    write_escaped_config_value(file, ip != nullptr ? ip : "");
    file.println();
    file.printf("last_rssi=%d\n", rssi);
    file.printf("updated_ms=%lu\n", static_cast<unsigned long>(millis()));
    file.flush();
    file.close();

    // A v1 file may contain a plaintext password. Overwrite its allocated
    // bytes before unlinking; this is best-effort media hygiene and is not a
    // substitute for keeping secrets off removable storage.
    if (SD.exists(path)) {
        File old = SD.open(path, "r+");
        if (old && !old.isDirectory()) {
            const size_t old_size = old.size();
            uint8_t zeros[64] = {};
            old.seek(0U);
            size_t remaining = old_size;
            while (remaining > 0U) {
                const size_t chunk =
                    remaining < sizeof(zeros) ? remaining : sizeof(zeros);
                if (old.write(zeros, chunk) != chunk) {
                    break;
                }
                remaining -= chunk;
            }
            old.flush();
        }
        if (old) {
            old.close();
        }
        if (!SD.remove(path)) {
            SD.remove(temp_path);
            Serial.printf(
                "WIFI_NVS: credential saved; cannot replace metadata %s\n",
                path);
            return true;
        }
    }
    if (!SD.rename(temp_path, path)) {
        SD.remove(temp_path);
        Serial.printf(
            "WIFI_NVS: credential saved; metadata rename failed %s\n",
            path);
        return true;
    }

    Serial.printf(
        "WIFI_NVS: saved credential and sanitized metadata %s for SSID %s\n",
        path,
        ssid);
    return true;
}

static bool read_config_line(File &file, char *line, size_t len) {
    if (line == nullptr || len == 0) {
        return false;
    }
    size_t pos = 0;
    bool read_any = false;
    while (file.available()) {
        const int c = file.read();
        if (c < 0) {
            break;
        }
        read_any = true;
        if (c == '\n') {
            break;
        }
        if (c == '\r') {
            continue;
        }
        if (pos + 1U < len) {
            line[pos++] = static_cast<char>(c);
        }
    }
    line[pos] = '\0';
    return read_any || pos > 0;
}

static uint8_t hex_digit_value(char c) {
    if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(10 + c - 'a');
    if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(10 + c - 'A');
    return 0xFF;
}

static void read_escaped_config_value(const char *value, char *out, size_t out_len) {
    if (out == nullptr || out_len == 0) {
        return;
    }
    out[0] = '\0';
    if (value == nullptr) {
        return;
    }

    size_t pos = 0;
    for (size_t i = 0; value[i] != '\0' && pos + 1U < out_len; ++i) {
        if (value[i] != '\\') {
            out[pos++] = value[i];
            continue;
        }
        const char next = value[++i];
        if (next == '\0') {
            break;
        }
        if (next == 'n') {
            out[pos++] = '\n';
        } else if (next == 'r') {
            out[pos++] = '\r';
        } else if (next == 'x' && value[i + 1] != '\0' && value[i + 2] != '\0') {
            const uint8_t hi = hex_digit_value(value[i + 1]);
            const uint8_t lo = hex_digit_value(value[i + 2]);
            if (hi != 0xFF && lo != 0xFF) {
                out[pos++] = static_cast<char>((hi << 4) | lo);
                i += 2;
            }
        } else {
            out[pos++] = next;
        }
    }
    out[pos] = '\0';
}

static bool load_wifi_profile_file(const char *path,
                                   char *ssid,
                                   size_t ssid_len,
                                   char *password,
                                   size_t password_len,
                                   uint32_t *updated_ms) {
    if (path == nullptr || ssid == nullptr || password == nullptr ||
        ssid_len == 0 || password_len == 0) {
        return false;
    }

    File file = SD.open(path, FILE_READ);
    if (!file || file.isDirectory()) {
        if (file) file.close();
        return false;
    }

    char line[180];
    ssid[0] = '\0';
    password[0] = '\0';
    if (updated_ms != nullptr) {
        *updated_ms = 0;
    }

    while (read_config_line(file, line, sizeof(line))) {
        char *equals = strchr(line, '=');
        if (equals == nullptr) {
            continue;
        }
        *equals = '\0';
        const char *key = line;
        const char *value = equals + 1;
        if (strcmp(key, "ssid") == 0) {
            read_escaped_config_value(value, ssid, ssid_len);
        } else if (strcmp(key, "password") == 0) {
            read_escaped_config_value(value, password, password_len);
        } else if (strcmp(key, "updated_ms") == 0 && updated_ms != nullptr) {
            *updated_ms = strtoul(value, nullptr, 10);
        }
    }

    file.close();
    return ssid[0] != '\0';
}

static void sanitize_legacy_wifi_profiles() {
    constexpr uint8_t MAX_MIGRATIONS_PER_BOOT = 32U;
    for (uint8_t pass = 0U;
         pass < MAX_MIGRATIONS_PER_BOOT;
         ++pass) {
        File root =
            SD.open(WIFI_PROFILE_DIR, FILE_READ);
        if (!root || !root.isDirectory()) {
            if (root) {
                root.close();
            }
            return;
        }

        bool found = false;
        char legacy_path[96] = {};
        char legacy_ssid[WIFI_CREDENTIAL_SSID_BYTES] = {};
        char legacy_password[
            WIFI_CREDENTIAL_PASSWORD_BYTES] = {};
        File entry = root.openNextFile();
        while (entry) {
            if (!entry.isDirectory()) {
                const char *name =
                    ota_portal_basename(entry.name());
                if (strncmp(name, "profile_", 8U) == 0) {
                    char path[96] = {};
                    snprintf(
                        path, sizeof(path), "%s/%s",
                        WIFI_PROFILE_DIR, name);
                    uint32_t updated_ms = 0U;
                    char ssid[
                        WIFI_CREDENTIAL_SSID_BYTES] = {};
                    char password[
                        WIFI_CREDENTIAL_PASSWORD_BYTES] = {};
                    if (load_wifi_profile_file(
                            path,
                            ssid,
                            sizeof(ssid),
                            password,
                            sizeof(password),
                            &updated_ms) &&
                        password[0] != '\0') {
                        snprintf(
                            legacy_path,
                            sizeof(legacy_path),
                            "%s",
                            path);
                        snprintf(
                            legacy_ssid,
                            sizeof(legacy_ssid),
                            "%s",
                            ssid);
                        snprintf(
                            legacy_password,
                            sizeof(legacy_password),
                            "%s",
                            password);
                        found = true;
                    }
                    secure_clear_gui_buffer(
                        password, sizeof(password));
                }
            }
            entry.close();
            if (found) {
                break;
            }
            entry = root.openNextFile();
        }
        root.close();

        if (!found) {
            secure_clear_gui_buffer(
                legacy_password,
                sizeof(legacy_password));
            return;
        }
        const bool migrated =
            save_wifi_profile_to_sd(
                legacy_ssid,
                legacy_password,
                "",
                0,
                legacy_path);
        secure_clear_gui_buffer(
            legacy_password,
            sizeof(legacy_password));
        char verified_ssid[
            WIFI_CREDENTIAL_SSID_BYTES] = {};
        char verified_password[
            WIFI_CREDENTIAL_PASSWORD_BYTES] = {};
        uint32_t verified_updated_ms = 0U;
        const bool sanitized =
            migrated &&
            load_wifi_profile_file(
                legacy_path,
                verified_ssid,
                sizeof(verified_ssid),
                verified_password,
                sizeof(verified_password),
                &verified_updated_ms) &&
            strcmp(verified_ssid, legacy_ssid) == 0 &&
            verified_password[0] == '\0';
        secure_clear_gui_buffer(
            verified_password,
            sizeof(verified_password));
        if (!sanitized) {
            Serial.printf(
                "WIFI_NVS: migration failed for %s\n",
                legacy_path);
            return;
        }
    }
    Serial.println(
        "WIFI_NVS: migration limit reached; "
        "remaining profiles will be sanitized next boot.");
}

static void try_autoconnect_wifi_profile(void) {
    if (cfg.modemSleep || wifi_connected || is_connecting || wifi_ota_active) {
        return;
    }
    const uint32_t now_ms = millis();
    const uint32_t retry_cooldown_ms = wifi_retry_policy.remaining_ms(now_ms);
    if (retry_cooldown_ms > 0U) {
        Serial.printf("WIFI_SD: autoconnect delayed after ASSOC_TOOMANY for %lu ms\n",
                      static_cast<unsigned long>(retry_cooldown_ms));
        return;
    }
    char nvs_ssid[WIFI_CREDENTIAL_SSID_BYTES] = "";
    char nvs_password[WIFI_PASSWORD_MAX_LEN + 1] = "";
    if (wifi_credential_store_load_latest(
            nvs_ssid,
            sizeof(nvs_ssid),
            nvs_password,
            sizeof(nvs_password))) {
        snprintf(selected_ssid, sizeof(selected_ssid), "%s", nvs_ssid);
        snprintf(
            pending_wifi_password,
            sizeof(pending_wifi_password),
            "%s",
            nvs_password);
        secure_clear_gui_buffer(
            nvs_password, sizeof(nvs_password));
        pending_wifi_password_valid = true;
        begin_sta_connection(selected_ssid, pending_wifi_password);
        Serial.printf(
            "WIFI_NVS: autoconnect profile SSID=%s\n",
            selected_ssid);
        return;
    }
    if (!ota_portal_sd_ready()) {
        return;
    }

    File root = SD.open(WIFI_PROFILE_DIR, FILE_READ);
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        return;
    }

    char best_ssid[64] = "";
    char best_password[WIFI_PASSWORD_MAX_LEN + 1] = "";
    uint32_t best_updated = 0;

    File entry = root.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            const char *name = ota_portal_basename(entry.name());
            if (strncmp(name, "profile_", 8) == 0) {
                char path[96];
                snprintf(path, sizeof(path), "%s/%s", WIFI_PROFILE_DIR, name);
                char ssid[64];
                char password[WIFI_PASSWORD_MAX_LEN + 1];
                uint32_t updated = 0;
                const bool profile_loaded =
                    load_wifi_profile_file(
                        path,
                        ssid,
                        sizeof(ssid),
                        password,
                        sizeof(password),
                        &updated);
                // Version 1 profiles are migrated once. The rewrite removes
                // the password field after directory enumeration is closed.
                if (profile_loaded && password[0] != '\0') {
                    wifi_credential_store_save(
                        ssid, password);
                }
                char secure_password[WIFI_PASSWORD_MAX_LEN + 1] = "";
                const bool secret_loaded =
                    profile_loaded &&
                    wifi_credential_store_load(
                        ssid,
                        secure_password,
                        sizeof(secure_password));
                secure_clear_gui_buffer(
                    password, sizeof(password));
                if (secret_loaded &&
                    (best_ssid[0] == '\0' || updated >= best_updated)) {
                    snprintf(best_ssid, sizeof(best_ssid), "%s", ssid);
                    snprintf(
                        best_password,
                        sizeof(best_password),
                        "%s",
                        secure_password);
                    best_updated = updated;
                }
                secure_clear_gui_buffer(
                    secure_password,
                    sizeof(secure_password));
            }
        }
        entry.close();
        entry = root.openNextFile();
    }
    root.close();
    sanitize_legacy_wifi_profiles();

    if (best_ssid[0] == '\0') {
        secure_clear_gui_buffer(
            best_password, sizeof(best_password));
        return;
    }
    if (!wifi_credential_store_save(
            best_ssid, best_password)) {
        secure_clear_gui_buffer(
            best_password, sizeof(best_password));
        Serial.println(
            "WIFI_NVS: cannot select migrated profile.");
        return;
    }

    snprintf(selected_ssid, sizeof(selected_ssid), "%s", best_ssid);
    snprintf(pending_wifi_password, sizeof(pending_wifi_password), "%s", best_password);
    secure_clear_gui_buffer(
        best_password, sizeof(best_password));
    pending_wifi_password_valid = true;
    begin_sta_connection(selected_ssid, pending_wifi_password);
    Serial.printf("WIFI_SD: autoconnect profile SSID=%s\n", selected_ssid);
}

static const char OTA_PORTAL_FALLBACK_INDEX[] PROGMEM = R"rawliteral(
<!doctype html><html lang="pl"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>cydAkwarium OTA</title><style>
body{margin:0;font-family:Arial,sans-serif;background:#f1f5f9;color:#0f172a}main{max-width:760px;margin:0 auto;padding:20px}
.card{background:#fff;border:1px solid #cbd5e1;border-radius:10px;padding:16px;margin:12px 0;box-shadow:0 8px 24px rgba(15,23,42,.08)}
h1{margin:0 0 4px;font-size:26px}.muted{color:#64748b}.row{display:flex;gap:10px;flex-wrap:wrap}.btn{border:0;border-radius:8px;padding:10px 14px;background:#0ea5e9;color:#fff;font-weight:700;text-decoration:none;display:inline-block}
input{width:100%;box-sizing:border-box;border:1px solid #cbd5e1;border-radius:8px;padding:10px}progress{width:100%;height:18px}
</style></head><body><main><h1>cydAkwarium OTA</h1><p class="muted">Awaryjna strona firmware. Wlasciwy plik powinien byc na SD: /aq/ota/index.html</p>
<section class="card"><h2>Aktualizacja firmware</h2><form id="f"><label>PIN administratora</label><input id="pin" type="password" inputmode="numeric" required><p><input id="pkg" name="firmware" type="file" accept=".aqfw" required></p><p><progress id="p" max="100" value="0"></progress></p><button class="btn" type="submit">Zweryfikuj i wgraj .aqfw</button></form><p id="msg" class="muted"></p></section>
<section class="card"><h2>Dane</h2><a class="btn" href="/api/history.csv">Pobierz aktualna historie CSV</a></section>
</main><script>
f.onsubmit=async function(e){e.preventDefault();var file=pkg.files[0];if(!file||!file.name.toLowerCase().endsWith('.aqfw')||file.size>1966592){msg.textContent='Wybierz poprawny pakiet .aqfw';return}try{var auth=await fetch('/api/v2/auth',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({pin:pin.value})});var body=await auth.json();var token=body&&body.data&&body.data.sessionToken;if(!auth.ok||!token){throw new Error(body.message||'Logowanie odrzucone')}var x=new XMLHttpRequest();x.open('POST','/update');x.setRequestHeader('X-AquaCYD-Session',token);x.upload.onprogress=function(ev){if(ev.lengthComputable)p.value=(ev.loaded*100/ev.total)|0};x.onload=function(){msg.textContent=x.responseText};x.onerror=function(){msg.textContent='Przerwane polaczenie OTA'};var d=new FormData();d.append('firmware',file,file.name);x.send(d)}catch(error){msg.textContent=error.message||'Blad autoryzacji'}};
</script></body></html>
)rawliteral";

static void ota_portal_no_cache() {
    ota_http_server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    ota_http_server.sendHeader("Pragma", "no-cache");
}

static bool ota_portal_client_accepts_gzip() {
    return ota_http_server.hasHeader("Accept-Encoding") &&
           ota_http_server.header("Accept-Encoding").indexOf("gzip") >= 0;
}

static const char *ota_portal_content_type(const char *path) {
    if (path == nullptr) {
        return "application/octet-stream";
    }
    const char *ext = strrchr(path, '.');
    if (ext == nullptr) {
        return "application/octet-stream";
    }
    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) return "text/html; charset=utf-8";
    if (strcmp(ext, ".css") == 0) return "text/css; charset=utf-8";
    if (strcmp(ext, ".js") == 0) return "application/javascript; charset=utf-8";
    if (strcmp(ext, ".json") == 0) return "application/json; charset=utf-8";
    if (strcmp(ext, ".csv") == 0) return "text/csv; charset=utf-8";
    if (strcmp(ext, ".txt") == 0 || strcmp(ext, ".log") == 0 || strcmp(ext, ".cfg") == 0) return "text/plain; charset=utf-8";
    if (strcmp(ext, ".bin") == 0 || strcmp(ext, ".aqbin") == 0 ||
        strcmp(ext, ".aqfw") == 0) return "application/octet-stream";
    if (strcmp(ext, ".gz") == 0) return "application/gzip";
    return "application/octet-stream";
}

static const char *ota_portal_basename(const char *path) {
    if (path == nullptr) {
        return "download.bin";
    }
    const char *slash = strrchr(path, '/');
    return slash != nullptr && slash[1] != '\0' ? slash + 1 : path;
}

static bool ota_portal_has_dotdot(const char *path) {
    return path == nullptr || strstr(path, "..") != nullptr;
}

static bool ota_portal_starts_with_dir(const char *path, const char *dir) {
    if (path == nullptr || dir == nullptr) {
        return false;
    }
    const size_t dir_len = strlen(dir);
    if (strncmp(path, dir, dir_len) != 0) {
        return false;
    }
    return path[dir_len] == '\0' || path[dir_len] == '/';
}

static bool ota_portal_allowed_data_path(const char *path) {
    if (ota_portal_has_dotdot(path) || path[0] != '/') {
        return false;
    }
    return ota_portal_starts_with_dir(path, OTA_PORTAL_HISTORY_DIR) ||
           ota_portal_starts_with_dir(path, OTA_PORTAL_LOG_DIR) ||
           ota_portal_starts_with_dir(path, OTA_PORTAL_DIAG_DIR);
}

static bool ota_portal_sd_ready() {
    return hal_sd_is_mounted() || hal_sd_init();
}

static bool history_archive_ensure_dirs() {
    return ensure_sd_directory("/aq") &&
           ensure_sd_directory("/aq/data") &&
           ensure_sd_directory(OTA_PORTAL_HISTORY_DIR);
}

static bool history_archive_name_to_month(const char *name, uint16_t *year, uint8_t *month) {
    if (name == nullptr || strlen(name) != 13U) {
        return false;
    }
    if (name[4] != '-' || strcmp(name + 7, ".aqbin") != 0) {
        return false;
    }
    for (uint8_t i = 0; i < 7; ++i) {
        if (i == 4) {
            continue;
        }
        if (name[i] < '0' || name[i] > '9') {
            return false;
        }
    }

    const uint16_t parsed_year = static_cast<uint16_t>((name[0] - '0') * 1000 +
                                                       (name[1] - '0') * 100 +
                                                       (name[2] - '0') * 10 +
                                                       (name[3] - '0'));
    const uint8_t parsed_month = static_cast<uint8_t>((name[5] - '0') * 10 + (name[6] - '0'));
    if (parsed_year < 1970U || parsed_year > 2099U || parsed_month < 1U || parsed_month > 12U) {
        return false;
    }

    if (year != nullptr) {
        *year = parsed_year;
    }
    if (month != nullptr) {
        *month = parsed_month;
    }
    return true;
}

static bool history_archive_is_older(uint16_t year_a, uint8_t month_a, uint16_t year_b, uint8_t month_b) {
    return year_a < year_b || (year_a == year_b && month_a < month_b);
}

static void history_archive_build_path(char *out, size_t out_len, uint16_t year, uint8_t month) {
    if (out == nullptr || out_len == 0) {
        return;
    }
    snprintf(out, out_len, "%s/%04u-%02u.aqbin",
             OTA_PORTAL_HISTORY_DIR,
             static_cast<unsigned>(year),
             static_cast<unsigned>(month));
}

static uint64_t history_archive_free_bytes() {
    const uint64_t total = SD.totalBytes();
    const uint64_t used = SD.usedBytes();
    if (total == 0ULL || used >= total) {
        return 0ULL;
    }
    return total - used;
}

static bool history_archive_find_oldest(char *out, size_t out_len, const char *skip_path) {
    if (out == nullptr || out_len == 0) {
        return false;
    }
    out[0] = '\0';

    File root = SD.open(OTA_PORTAL_HISTORY_DIR, FILE_READ);
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        return false;
    }

    bool found = false;
    uint16_t oldest_year = 0;
    uint8_t oldest_month = 0;
    File entry = root.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            const char *name = ota_portal_basename(entry.name());
            uint16_t year = 0;
            uint8_t month = 0;
            if (history_archive_name_to_month(name, &year, &month)) {
                char candidate[96];
                snprintf(candidate, sizeof(candidate), "%s/%s", OTA_PORTAL_HISTORY_DIR, name);
                const bool skip = skip_path != nullptr && strcmp(candidate, skip_path) == 0;
                if (!skip && (!found || history_archive_is_older(year, month, oldest_year, oldest_month))) {
                    snprintf(out, out_len, "%s", candidate);
                    oldest_year = year;
                    oldest_month = month;
                    found = true;
                }
            }
        }
        entry.close();
        entry = root.openNextFile();
    }
    root.close();
    return found;
}

static bool history_archive_delete_oldest(const char *current_path) {
    char oldest[96];
    if (!history_archive_find_oldest(oldest, sizeof(oldest), current_path)) {
        return false;
    }
    const bool removed = SD.remove(oldest);
    Serial.printf("HISTORY_SD: retention removed %s: %s\n", oldest, removed ? "ok" : "failed");
    return removed;
}

static bool history_archive_read_header(File &file, HistoryArchiveHeader *header) {
    if (!file || header == nullptr || file.size() < sizeof(HistoryArchiveHeader)) {
        return false;
    }
    if (!file.seek(0)) {
        return false;
    }
    return file.read(reinterpret_cast<uint8_t *>(header), sizeof(HistoryArchiveHeader)) ==
           sizeof(HistoryArchiveHeader);
}

static bool history_archive_header_valid(const HistoryArchiveHeader &header, uint16_t year, uint8_t month) {
    return header.magic == HISTORY_ARCHIVE_MAGIC &&
           header.version == HISTORY_ARCHIVE_VERSION &&
           header.headerSize == sizeof(HistoryArchiveHeader) &&
           header.recordSize == sizeof(HistoryArchiveRecord) &&
           header.year == year &&
           header.month == month;
}

static bool history_archive_create_file(const char *path, uint16_t year, uint8_t month) {
    if (path == nullptr) {
        return false;
    }
    if (SD.exists(path) && !SD.remove(path)) {
        Serial.printf("HISTORY_SD: failed to replace invalid archive %s\n", path);
        return false;
    }

    File file = SD.open(path, FILE_WRITE);
    if (!file) {
        Serial.printf("HISTORY_SD: failed to create %s\n", path);
        return false;
    }

    HistoryArchiveHeader header = {};
    header.magic = HISTORY_ARCHIVE_MAGIC;
    header.version = HISTORY_ARCHIVE_VERSION;
    header.headerSize = sizeof(HistoryArchiveHeader);
    header.recordSize = sizeof(HistoryArchiveRecord);
    header.year = year;
    header.month = month;
    header.createdEpoch = controller_unix_time();
    const size_t written = file.write(reinterpret_cast<const uint8_t *>(&header), sizeof(header));
    file.close();
    return written == sizeof(header);
}

static bool history_archive_ensure_file(const char *path, uint16_t year, uint8_t month) {
    if (path == nullptr || path[0] == '\0') {
        return false;
    }
    if (!SD.exists(path)) {
        return history_archive_create_file(path, year, month);
    }

    File file = SD.open(path, FILE_READ);
    HistoryArchiveHeader header = {};
    const bool valid = history_archive_read_header(file, &header) &&
                       history_archive_header_valid(header, year, month);
    if (file) {
        file.close();
    }
    return valid || history_archive_create_file(path, year, month);
}

static bool history_archive_compact_current(const char *path) {
    if (path == nullptr || !SD.exists(path)) {
        return false;
    }

    File source = SD.open(path, FILE_READ);
    HistoryArchiveHeader header = {};
    if (!history_archive_read_header(source, &header) ||
        header.recordSize != sizeof(HistoryArchiveRecord) ||
        header.headerSize != sizeof(HistoryArchiveHeader)) {
        if (source) source.close();
        return SD.remove(path);
    }

    const uint32_t source_size = source.size();
    const uint32_t payload_size = source_size > header.headerSize ? source_size - header.headerSize : 0U;
    const uint32_t record_count = payload_size / header.recordSize;
    if (record_count <= 1U) {
        source.close();
        return false;
    }

    const uint32_t drop_count = (record_count - 1U) < HISTORY_ARCHIVE_COMPACT_DROP_RECORDS
                                    ? (record_count - 1U)
                                    : HISTORY_ARCHIVE_COMPACT_DROP_RECORDS;
    const uint32_t keep_bytes = (record_count - drop_count) * header.recordSize;
    const char temp_path[] = "/aq/data/history/.compact.tmp";
    if (SD.exists(temp_path) && !SD.remove(temp_path)) {
        source.close();
        return false;
    }

    File target = SD.open(temp_path, FILE_WRITE);
    if (!target) {
        source.close();
        const bool recreated = SD.remove(path) && history_archive_create_file(path, header.year, header.month);
        Serial.printf("HISTORY_SD: compact fallback recreated %s: %s\n", path, recreated ? "ok" : "failed");
        return recreated;
    }

    bool ok = target.write(reinterpret_cast<const uint8_t *>(&header), sizeof(header)) == sizeof(header);
    ok = ok && source.seek(header.headerSize + drop_count * header.recordSize);

    uint8_t buffer[HISTORY_ARCHIVE_COPY_BUFFER_BYTES];
    uint32_t remaining = keep_bytes;
    while (ok && remaining > 0U) {
        const size_t chunk = remaining < sizeof(buffer) ? static_cast<size_t>(remaining) : sizeof(buffer);
        const size_t bytes_read = source.read(buffer, chunk);
        if (bytes_read == 0U) {
            ok = false;
            break;
        }
        const size_t written = target.write(buffer, bytes_read);
        if (written != bytes_read) {
            ok = false;
            break;
        }
        remaining -= static_cast<uint32_t>(bytes_read);
        taskYIELD();
    }

    source.close();
    target.close();
    if (!ok) {
        SD.remove(temp_path);
        const bool recreated = SD.remove(path) && history_archive_create_file(path, header.year, header.month);
        Serial.printf("HISTORY_SD: compact copy failed, recreated %s: %s\n", path, recreated ? "ok" : "failed");
        return recreated;
    }
    if (!SD.remove(path)) {
        SD.remove(temp_path);
        return false;
    }
    const bool renamed = SD.rename(temp_path, path);
    if (!renamed) {
        SD.remove(temp_path);
    }
    Serial.printf("HISTORY_SD: compacted %s, dropped %lu records\n",
                  path,
                  static_cast<unsigned long>(drop_count));
    return renamed;
}

static bool history_archive_prepare_space(const char *current_path, size_t bytes_needed) {
    if (current_path == nullptr) {
        return false;
    }
    for (uint8_t attempt = 0; attempt < 8U; ++attempt) {
        const uint64_t free_bytes = history_archive_free_bytes();
        if (free_bytes >= HISTORY_ARCHIVE_MIN_FREE_BYTES + static_cast<uint64_t>(bytes_needed)) {
            return true;
        }
        if (history_archive_delete_oldest(current_path)) {
            continue;
        }
        if (history_archive_compact_current(current_path)) {
            continue;
        }
        return false;
    }
    return history_archive_free_bytes() >= static_cast<uint64_t>(bytes_needed);
}

static int16_t history_archive_scaled_i16(float value, float scale) {
    if (!isfinite(value)) {
        return INT16_MIN;
    }
    long scaled = lroundf(value * scale);
    if (scaled < static_cast<long>(INT16_MIN + 1)) {
        scaled = static_cast<long>(INT16_MIN + 1);
    } else if (scaled > static_cast<long>(INT16_MAX)) {
        scaled = static_cast<long>(INT16_MAX);
    }
    return static_cast<int16_t>(scaled);
}

static void history_archive_append_sample(float temp, bool heater_on, float ph, int ldr, uint32_t heap_bytes) {
    const uint32_t now_ms = millis();
    if (history_archive_has_written &&
        static_cast<uint32_t>(now_ms - history_archive_last_write_ms) < HISTORY_ARCHIVE_INTERVAL_MS) {
        return;
    }
    history_archive_has_written = true;
    history_archive_last_write_ms = now_ms;

    if (!ota_portal_sd_ready() || !history_archive_ensure_dirs()) {
        return;
    }

    uint16_t archive_year = static_cast<uint16_t>(clock_year);
    uint8_t archive_month = static_cast<uint8_t>(clock_month);
    if (!calendar_date_valid(clock_day, clock_month, clock_year)) {
        archive_year = 1970;
        archive_month = 1;
    }

    char path[96];
    history_archive_build_path(path, sizeof(path), archive_year, archive_month);
    if (!history_archive_prepare_space(path, sizeof(HistoryArchiveHeader) + sizeof(HistoryArchiveRecord))) {
        Serial.println("HISTORY_SD: archive skipped, retention could not free space.");
        return;
    }
    if (!history_archive_ensure_file(path, archive_year, archive_month)) {
        return;
    }
    if (!history_archive_prepare_space(path, sizeof(HistoryArchiveRecord))) {
        return;
    }

    HistoryArchiveRecord record = {};
    record.epoch = controller_unix_time();
    if (record.epoch == 0U) {
        record.epoch = now_ms / 1000UL;
    }
    record.tempCx100 = history_archive_scaled_i16(temp, 100.0f);
    record.phX1000 = history_archive_scaled_i16(ph, 1000.0f);
    record.ldr = ldr >= LDR_ADC_MIN && ldr <= LDR_ADC_MAX ? static_cast<int16_t>(ldr) : static_cast<int16_t>(-1);
    record.heapBytes = heap_bytes;
    if (isfinite(temp)) record.flags |= 0x01U;
    if (isfinite(ph)) record.flags |= 0x02U;
    if (record.ldr >= 0) record.flags |= 0x04U;
    if (heater_on) record.flags |= 0x08U;

    File file = SD.open(path, FILE_APPEND);
    if (!file) {
        if (history_archive_delete_oldest(path)) {
            file = SD.open(path, FILE_APPEND);
        }
    }
    if (!file) {
        return;
    }
    const size_t written = file.write(reinterpret_cast<const uint8_t *>(&record), sizeof(record));
    file.close();
    if (written != sizeof(record)) {
        Serial.printf("HISTORY_SD: short write %s (%u/%u)\n",
                      path,
                      static_cast<unsigned>(written),
                      static_cast<unsigned>(sizeof(record)));
    }
}

static void ota_portal_send_json_escaped(const char *text) {
    ota_http_server.sendContent("\"");
    if (text != nullptr) {
        for (size_t i = 0; text[i] != '\0'; ++i) {
            const uint8_t c = static_cast<uint8_t>(text[i]);
            char out[8];
            if (c == '"' || c == '\\') {
                out[0] = '\\';
                out[1] = static_cast<char>(c);
                out[2] = '\0';
                ota_http_server.sendContent(out);
            } else if (c == '\n') {
                ota_http_server.sendContent("\\n");
            } else if (c == '\r') {
                ota_http_server.sendContent("\\r");
            } else if (c < 32U || c > 126U) {
                snprintf(out, sizeof(out), "\\u%04x", static_cast<unsigned>(c));
                ota_http_server.sendContent(out);
            } else {
                out[0] = static_cast<char>(c);
                out[1] = '\0';
                ota_http_server.sendContent(out);
            }
        }
    }
    ota_http_server.sendContent("\"");
}

static void ota_portal_set_status(const char *text, lv_color_t color) {
    if (wifi_status_message_lbl != nullptr) {
        lv_label_set_text(wifi_status_message_lbl, text != nullptr ? text : "");
        lv_obj_set_style_text_color(wifi_status_message_lbl, color, 0);
    }
}

static const char *ota_portal_schedule_mode_name(uint8_t mode) {
    if (mode == static_cast<uint8_t>(ScheduleMode::AlwaysOn)) {
        return "always_on";
    }
    if (mode == static_cast<uint8_t>(ScheduleMode::AlwaysOff)) {
        return "always_off";
    }
    return "schedule";
}

static void ota_portal_send_bool(bool value) {
    ota_http_server.sendContent(value ? "true" : "false");
}

static void ota_portal_send_time_json(uint8_t hour, uint8_t minute) {
    char buf[8];
    snprintf(buf, sizeof(buf), "\"%02u:%02u\"", static_cast<unsigned>(hour), static_cast<unsigned>(minute));
    ota_http_server.sendContent(buf);
}

static void ota_portal_send_schedule_json(const char *key,
                                          uint8_t mode,
                                          uint8_t start_hour,
                                          uint8_t start_minute,
                                          uint8_t end_hour,
                                          uint8_t end_minute,
                                          int8_t aquael_profile = -1,
                                          bool profile_cycle = false) {
    ota_http_server.sendContent("\"");
    ota_http_server.sendContent(key);
    ota_http_server.sendContent("\":{\"mode\":\"");
    ota_http_server.sendContent(ota_portal_schedule_mode_name(mode));
    ota_http_server.sendContent("\",\"start\":");
    ota_portal_send_time_json(start_hour, start_minute);
    ota_http_server.sendContent(",\"end\":");
    ota_portal_send_time_json(end_hour, end_minute);
    if (aquael_profile >= 0) {
        const uint8_t profile = normalize_aquael_profile(static_cast<uint8_t>(aquael_profile));
        ota_http_server.sendContent(",\"profile\":\"");
        ota_http_server.sendContent(aquael_profile_code(profile));
        ota_http_server.sendContent("\",\"profileLabel\":\"");
        ota_http_server.sendContent(light_color_mode_label(profile));
        ota_http_server.sendContent("\",\"profileDescription\":");
        ota_portal_send_json_escaped(aquael_profile_description(profile));
        ota_http_server.sendContent(",\"supportedProfiles\":[\"day\",\"daybreak\",\"night\"],\"profileCycle\":");
        ota_portal_send_bool(profile_cycle);
    }
    ota_http_server.sendContent("}");
}

static void ota_portal_handle_root() {
    ota_portal_mark_web_activity();
    ota_portal_no_cache();
    if (ota_portal_sd_ready()) {
        const bool use_gzip = ota_portal_client_accepts_gzip() && SD.exists("/aq/ota/index.html.gz");
        const char *served_path = use_gzip ? "/aq/ota/index.html.gz" : OTA_PORTAL_INDEX_PATH;
        File file = SD.open(served_path, FILE_READ);
        if (file && !file.isDirectory()) {
            if (use_gzip) {
                ota_http_server.sendHeader("Content-Encoding", "gzip");
                ota_http_server.sendHeader("Vary", "Accept-Encoding");
            }
            ota_http_server.streamFile(file, "text/html; charset=utf-8");
            file.close();
            return;
        }
        if (file) {
            file.close();
        }
    }
    ota_http_server.send_P(200, "text/html; charset=utf-8", OTA_PORTAL_FALLBACK_INDEX);
}

static const char *i2c_device_type(uint8_t address) {
    if (address == HwConfig::MCP23017_ADDR) return "mcp23017";
    if (address >= 0x21U && address <= 0x27U) return "gpio_expander";
    if (address == HwConfig::ADS1115_ADDR) return "ads1115";
    if (address == 0x3CU || address == 0x3DU) return "oled";
    if (address == 0x40U) return "current_pwm_sensor";
    if (address == 0x44U || address == 0x45U) return "sht3x";
    if (address >= 0x50U && address <= 0x57U) return "eeprom";
    if (address == 0x68U) return "rtc_or_imu";
    if (address == 0x76U || address == 0x77U) return "environmental";
    return "unknown";
}

static bool i2c_device_is_configured(uint8_t address) {
    return address == HwConfig::MCP23017_ADDR || address == HwConfig::ADS1115_ADDR;
}

static const char *onewire_device_type(uint8_t family) {
    switch (family) {
        case 0x01U: return "ds1990_serial";
        case 0x10U: return "ds18s20";
        case 0x22U: return "ds1822";
        case 0x28U: return "ds18b20";
        case 0x1DU: return "ds2423_counter";
        case 0x26U: return "ds2438_battery";
        case 0x2DU: return "ds2431_eeprom";
        case 0x3BU: return "max31850";
        default: return "unknown";
    }
}

static void ota_portal_handle_i2c_scan() {
    ota_portal_mark_web_activity();
    if (!ota_portal_require_pin()) {
        return;
    }

    const uint32_t started_ms = millis();
    HalI2cScanResult result = {};
    if (!hal_i2c_bus_scan(&result)) {
        ota_portal_no_cache();
        ota_http_server.send(503, "application/json",
                             "{\"ok\":false,\"success\":false,\"code\":\"i2c_busy\",\"message\":\"Magistrala I2C jest zajeta.\"}");
        return;
    }
    const uint32_t i2c_scan_ms = millis() - started_ms;

    const uint32_t onewire_started_ms = millis();
    HalOneWireScanResult onewire_result = {};
    if (!hal_onewire_bus_scan(&onewire_result)) {
        ota_portal_no_cache();
        ota_http_server.send(503, "application/json",
                             "{\"ok\":false,\"success\":false,\"code\":\"onewire_busy\",\"message\":\"Magistrala OneWire jest zajęta.\"}");
        return;
    }
    const uint32_t onewire_scan_ms = millis() - onewire_started_ms;

    ota_portal_no_cache();
    ota_http_server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    ota_http_server.send(200, "application/json", "");

    char line[256];
    snprintf(line, sizeof(line),
             "{\"ok\":true,\"simulated\":false,\"sda\":%u,\"scl\":%u,\"frequencyHz\":%lu,"
             "\"scanMs\":%lu,\"count\":%u,\"truncated\":%s,\"devices\":[",
             static_cast<unsigned>(HwConfig::I2C_SDA_PIN),
             static_cast<unsigned>(HwConfig::I2C_SCL_PIN),
             static_cast<unsigned long>(HwConfig::I2C_FREQUENCY_HZ),
             static_cast<unsigned long>(i2c_scan_ms),
             static_cast<unsigned>(result.count),
             result.truncated ? "true" : "false");
    ota_http_server.sendContent(line);

    for (uint8_t index = 0; index < result.count; ++index) {
        const uint8_t address = result.addresses[index];
        snprintf(line, sizeof(line),
                 "%s{\"address\":%u,\"hex\":\"0x%02X\",\"type\":\"%s\",\"configured\":%s}",
                 index == 0U ? "" : ",",
                 static_cast<unsigned>(address),
                 static_cast<unsigned>(address),
                 i2c_device_type(address),
                 i2c_device_is_configured(address) ? "true" : "false");
        ota_http_server.sendContent(line);
    }

    ota_http_server.sendContent("],\"uart\":{\"ports\":[");
    snprintf(line, sizeof(line),
             "{\"port\":%u,\"active\":true,\"role\":\"console\",\"tx\":%d,\"rx\":%d,"
             "\"baud\":%lu,\"format\":\"%s\"}",
             static_cast<unsigned>(HwConfig::UartConsole::PORT),
             static_cast<int>(HwConfig::UartConsole::TX_PIN),
             static_cast<int>(HwConfig::UartConsole::RX_PIN),
             static_cast<unsigned long>(HwConfig::UartConsole::BAUD),
             HwConfig::UartConsole::FORMAT);
    ota_http_server.sendContent(line);
    ota_http_server.sendContent("],\"discoverySupported\":false},\"oneWire\":{");
    snprintf(line, sizeof(line),
             "\"dataPin\":%u,\"scanMs\":%lu,\"count\":%u,\"truncated\":%s,\"devices\":[",
             static_cast<unsigned>(HwConfig::OneWireBus::DATA_PIN),
             static_cast<unsigned long>(onewire_scan_ms),
             static_cast<unsigned>(onewire_result.count),
             onewire_result.truncated ? "true" : "false");
    ota_http_server.sendContent(line);

    for (uint8_t index = 0; index < onewire_result.count; ++index) {
        const HalOneWireDevice &device = onewire_result.devices[index];
        char rom[24];
        snprintf(rom, sizeof(rom),
                 "%02X-%02X%02X%02X%02X%02X%02X-%02X",
                 device.rom[0], device.rom[1], device.rom[2], device.rom[3],
                 device.rom[4], device.rom[5], device.rom[6], device.rom[7]);
        snprintf(line, sizeof(line),
                 "%s{\"rom\":\"%s\",\"family\":%u,\"type\":\"%s\",\"crcValid\":%s}",
                 index == 0U ? "" : ",",
                 rom,
                 static_cast<unsigned>(device.rom[0]),
                 onewire_device_type(device.rom[0]),
                 device.crc_valid ? "true" : "false");
        ota_http_server.sendContent(line);
    }

    ota_http_server.sendContent("]}}");
    ota_http_server.sendContent("");
}

static void ota_portal_handle_v2_capabilities() {
    ota_portal_mark_web_activity();
    char response[3072];
    if (!gui_app_v2_capabilities_json(response, sizeof(response))) {
        ota_http_server.send(
            503,
            "application/json",
            "{\"type\":\"capabilities\",\"v\":2,\"ok\":false,"
            "\"code\":\"capabilities_unavailable\"}");
        return;
    }
    ota_portal_no_cache();
    ota_http_server.send(200, "application/json", response);
}

static void ota_portal_handle_v2_auth() {
    ota_portal_mark_web_activity();
    char pin[16] = {};
    if (ota_http_server.hasArg("pin")) {
        ota_http_server.arg("pin").toCharArray(pin, sizeof(pin));
    } else if (ota_http_server.hasArg("plain")) {
        char request_json[256] = {};
        const String &body = ota_http_server.arg("plain");
        if (body.length() < sizeof(request_json)) {
            body.toCharArray(request_json, sizeof(request_json));
            ble_json_read_string(request_json, "pin", pin, sizeof(pin));
        }
    }
    char token[aquarium::AdminSessionManager::kTokenBytes] = {};
    const GuiV2AuthResult result =
        gui_app_v2_auth(pin, token, sizeof(token));
    const uint32_t timestamp = controller_unix_time();
    char body[512];
    if (result.success) {
        snprintf(
            body,
            sizeof(body),
            "{\"type\":\"auth\",\"v\":2,\"ok\":true,"
            "\"code\":\"authenticated\",\"ts\":%lu,"
            "\"data\":{\"sessionToken\":\"%s\",\"expiresInSec\":%lu}}",
            static_cast<unsigned long>(timestamp),
            token,
            static_cast<unsigned long>(result.expires_in_seconds));
        ota_portal_no_cache();
        ota_http_server.send(200, "application/json", body);
        return;
    }
    snprintf(
        body,
        sizeof(body),
        "{\"type\":\"auth\",\"v\":2,\"ok\":false,\"code\":\"%s\","
        "\"message\":\"%s\",\"ts\":%lu,\"retryAfterSec\":%lu}",
        result.code,
        result.message,
        static_cast<unsigned long>(timestamp),
        static_cast<unsigned long>(result.retry_after_seconds));
    ota_portal_no_cache();
    int http_status = 401;
    if (strcmp(result.code, "auth_rate_limited") == 0) {
        http_status = 429;
    } else if (strcmp(result.code, "controller_busy") == 0) {
        http_status = 503;
    } else if (strcmp(result.code, "token_generation_failed") == 0) {
        http_status = 500;
    }
    ota_http_server.send(http_status, "application/json", body);
}

static void ota_portal_handle_status() {
    ota_portal_mark_web_activity();
    const bool include_history = ota_http_server.hasArg("history") &&
                                 ota_http_server.arg("history") == "1";
    char ip_buf[24];
    const IPAddress ip = wifi_ota_active ? WiFi.softAPIP() : WiFi.localIP();
    snprintf(ip_buf, sizeof(ip_buf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    char portal_url[64];
    char portal_domain[64];
    snprintf(portal_url, sizeof(portal_url), "http://%s/", ip_buf);
    snprintf(portal_domain, sizeof(portal_domain), "http://%s.local/", Secrets::OTA_HOSTNAME);

    char temp_json[16];
    char ph_json[16];
    char ec_json[16];
    char ldr_json[16];
    char battery_voltage_json[16];
    char battery_percent_json[8];
    char supply_voltage_json[16];
    const aquarium::DevSnapshot &dev_snapshot = aquarium::dev_simulator().latest();
    const float api_temp_value = isfinite(runtime.lastTemp)
                                     ? runtime.lastTemp
                                     : (cfg.devMode ? dev_snapshot.temperatureC : NAN);
    const float api_ph_value = isfinite(runtime.lastPh)
                                   ? runtime.lastPh
                                   : (cfg.devMode ? dev_snapshot.ph : NAN);
    const bool api_temp_valid = isfinite(api_temp_value);
    const bool api_ph_valid = isfinite(api_ph_value);
    const bool api_ec_valid = sensor_debug.ecValid || cfg.devMode;
    const bool api_ldr_valid = last_ldr_valid || cfg.devMode;
    const float api_ec_mv = sensor_debug.ecValid
                                ? sensor_debug.ecValue
                                : dev_snapshot.ecConductivity;
    const int api_ldr_value = last_ldr_valid
                                  ? last_ldr_value
                                  : dev_snapshot.ldr;
    snprintf(temp_json, sizeof(temp_json), api_temp_valid ? "%.2f" : "null", api_temp_value);
    snprintf(ph_json, sizeof(ph_json), api_ph_valid ? "%.3f" : "null", api_ph_value);
    if (api_ec_valid) {
        snprintf(ec_json, sizeof(ec_json), "%.1f", api_ec_mv);
    } else {
        snprintf(ec_json, sizeof(ec_json), "null");
    }
    if (api_ldr_valid) {
        snprintf(ldr_json, sizeof(ldr_json), "%d", api_ldr_value);
    } else {
        snprintf(ldr_json, sizeof(ldr_json), "null");
    }
    if (cfg.devMode) {
        snprintf(battery_voltage_json, sizeof(battery_voltage_json), "%.2f", dev_snapshot.batteryVoltage);
        snprintf(battery_percent_json, sizeof(battery_percent_json), "%u",
                 static_cast<unsigned>(dev_snapshot.batteryPercent));
        snprintf(supply_voltage_json, sizeof(supply_voltage_json), "%.2f", dev_snapshot.supplyVoltage);
    } else {
        snprintf(battery_voltage_json, sizeof(battery_voltage_json), "null");
        snprintf(battery_percent_json, sizeof(battery_percent_json), "null");
        snprintf(supply_voltage_json, sizeof(supply_voltage_json), "null");
    }
    const bool api_mcp_present = sensor_debug.mcpPresent || cfg.devMode;
    const bool mcp_status_valid = (sensor_debug.mcpPresent && sensor_debug.mcpValid) || cfg.devMode;
    const uint16_t water_level_mask = static_cast<uint16_t>(1U << static_cast<uint8_t>(HwConfig::CH_WATER_LEVEL));
    const uint16_t leak_mask = static_cast<uint16_t>(1U << static_cast<uint8_t>(HwConfig::CH_LEAK));
    const uint16_t flow_mask = static_cast<uint16_t>(1U << static_cast<uint8_t>(HwConfig::CH_FLOW_PULSE));
    const bool water_level_high = cfg.devMode
                                      ? dev_snapshot.waterLevelHigh
                                      : (mcp_status_valid && ((sensor_debug.mcpState & water_level_mask) != 0U));
    const bool leak_detected = cfg.devMode
                                   ? dev_snapshot.leakDetected
                                   : (mcp_status_valid && ((sensor_debug.mcpState & leak_mask) != 0U));
    const bool flow_active = cfg.devMode
                                 ? dev_snapshot.flowActive
                                 : (mcp_status_valid && ((sensor_debug.mcpState & flow_mask) != 0U));
    const char *water_level_json = mcp_status_valid ? (water_level_high ? "true" : "false") : "null";
    const char *leak_json = mcp_status_valid ? (leak_detected ? "true" : "false") : "null";
    const char *flow_json = mcp_status_valid ? (flow_active ? "true" : "false") : "null";
    const unsigned int alarm_flags = current_alarm_flags;
    const EcoRuntimeStatus eco = eco_collect_status();
    const bool sd_ready_for_status = ota_portal_sd_ready();
    const uint64_t sd_total_bytes = sd_ready_for_status ? SD.totalBytes() : 0ULL;
    const uint64_t sd_used_bytes = sd_ready_for_status ? SD.usedBytes() : 0ULL;
    const uint64_t sd_free_bytes = (sd_total_bytes > sd_used_bytes) ? (sd_total_bytes - sd_used_bytes) : 0ULL;
    const uint32_t status_now_ms = millis();
    const uint8_t active_web_clients = web_ui_active_client_count(status_now_ms);
    const uint32_t web_idle_ms = web_ui_last_request_ms == 0U
                                     ? 0U
                                     : static_cast<uint32_t>(status_now_ms - web_ui_last_request_ms);
    const uint32_t wifi_retry_cooldown_ms = wifi_retry_policy.remaining_ms(status_now_ms);

    char line[512];
    ota_portal_no_cache();
    ota_http_server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    ota_http_server.send(200, "application/json", "");

    snprintf(line, sizeof(line),
             "{\"device\":\"cydAkwarium\",\"mode\":\"%s\",\"portal_ip\":\"%s\",\"ip\":\"%s\","
             "\"portal_url\":\"%s\",\"portal_domain\":\"%s\",\"hostname\":\"%s\","
             "\"theme\":\"%s\",\"theme_light\":%s,\"ldr_auto\":%s,\"manual_light_theme\":%s,"
             "\"clients\":%u,\"heap_free\":%lu,\"heap_largest\":%lu,\"sd_mounted\":%s,"
             "\"sd_total_bytes\":%llu,\"sd_used_bytes\":%llu,\"sd_free_bytes\":%llu,"
             "\"history_points\":%u,\"uptime_ms\":%lu,\"ota_active\":%s,",
             wifi_ota_active ? "OTA_AP" : "STA_SERVICE",
             ip_buf,
             ip_buf,
             portal_url,
             portal_domain,
             Secrets::OTA_HOSTNAME,
             ui_light_theme ? "light" : "dark",
             ui_light_theme ? "true" : "false",
             cfg.ldrThemeEnabled ? "true" : "false",
             cfg.manualLightTheme ? "true" : "false",
             static_cast<unsigned>(wifi_ota_active ? WiFi.softAPgetStationNum() : 0),
             static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_8BIT)),
             static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
             sd_ready_for_status ? "true" : "false",
             static_cast<unsigned long long>(sd_total_bytes),
             static_cast<unsigned long long>(sd_used_bytes),
             static_cast<unsigned long long>(sd_free_bytes),
             static_cast<unsigned>(history_count),
             static_cast<unsigned long>(millis()),
             wifi_ota_active ? "true" : "false");
    ota_http_server.sendContent(line);

    snprintf(line, sizeof(line),
             "\"sensors\":{\"temp_c\":%s,\"temp_valid\":%s,\"ph\":%s,\"ph_valid\":%s,\"ec\":%s,\"ec_valid\":%s,"
             "\"ldr\":%s,\"ldr_valid\":%s,"
             "\"mcp_present\":%s,\"mcp_valid\":%s,\"mcp_ok\":%s,\"water_level_high\":%s,\"water_level_valid\":%s,"
             "\"leak_detected\":%s,\"leak_valid\":%s,\"flow_active\":%s,\"flow_valid\":%s,"
             "\"supply_voltage\":%s,\"supply_valid\":%s},",
             temp_json,
             api_temp_valid ? "true" : "false",
             ph_json,
             api_ph_valid ? "true" : "false",
             ec_json,
             api_ec_valid ? "true" : "false",
             ldr_json,
             api_ldr_valid ? "true" : "false",
             api_mcp_present ? "true" : "false",
             mcp_status_valid ? "true" : "false",
             mcp_status_valid ? "true" : "false",
             water_level_json,
             mcp_status_valid ? "true" : "false",
             leak_json,
             mcp_status_valid ? "true" : "false",
             flow_json,
             mcp_status_valid ? "true" : "false",
             supply_voltage_json,
             cfg.devMode ? "true" : "false");
    ota_http_server.sendContent(line);

    snprintf(line, sizeof(line),
             "\"alarms\":{\"flags\":%u,\"activeCount\":%u,\"temperatureHigh\":%s,\"temperatureLow\":%s,"
             "\"phOutOfRange\":%s,\"waterLevelLow\":%s,\"leak\":%s,\"supplyLow\":%s,"
             "\"sensorMissing\":%s,\"sensorStale\":%s,\"sensorBusFault\":%s,"
             "\"actuatorWriteFailed\":%s},",
             alarm_flags,
             aquarium::alarm_count(alarm_flags),
             (alarm_flags & aquarium::AlarmTemperatureHigh) != 0U ? "true" : "false",
             (alarm_flags & aquarium::AlarmTemperatureLow) != 0U ? "true" : "false",
             (alarm_flags & aquarium::AlarmPhOutOfRange) != 0U ? "true" : "false",
             (alarm_flags & aquarium::AlarmWaterLevelLow) != 0U ? "true" : "false",
             (alarm_flags & aquarium::AlarmLeak) != 0U ? "true" : "false",
             (alarm_flags & aquarium::AlarmSupplyLow) != 0U ? "true" : "false",
             (alarm_flags & aquarium::AlarmSensorMissing) != 0U ? "true" : "false",
             (alarm_flags & aquarium::AlarmSensorStale) != 0U ? "true" : "false",
             (alarm_flags & aquarium::AlarmSensorBusFault) != 0U ? "true" : "false",
             (alarm_flags & aquarium::AlarmActuatorWriteFailed) != 0U ? "true" : "false");
    ota_http_server.sendContent(line);

    const aquarium::SensorCalibration calibration =
        sensor_calibration_store_snapshot();
    snprintf(
        line, sizeof(line),
        "\"calibration\":{\"version\":%u,"
        "\"ph\":{\"lowRaw\":%d,\"lowReference\":%.3f,"
        "\"highRaw\":%d,\"highReference\":%.3f},"
        "\"ec\":{\"referenceRaw\":%d,\"referenceUsCm\":%.2f,"
        "\"temperatureCoefficient\":%.5f,"
        "\"referenceTemperatureC\":%.2f}},",
        static_cast<unsigned>(calibration.version),
        static_cast<int>(calibration.ph_low_raw),
        static_cast<double>(calibration.ph_low_reference),
        static_cast<int>(calibration.ph_high_raw),
        static_cast<double>(calibration.ph_high_reference),
        static_cast<int>(calibration.ec_reference_raw),
        static_cast<double>(calibration.ec_reference_us_cm),
        static_cast<double>(
            calibration.ec_temperature_coefficient),
        static_cast<double>(
            calibration.ec_reference_temperature_c));
    ota_http_server.sendContent(line);

    const RemoteAlarmRelayStatus remote_gateway =
        remote_alarm_relay_status();
    snprintf(
        line, sizeof(line),
        "\"remoteGateway\":{\"enabled\":%s,"
        "\"provisioned\":%s,\"taskRunning\":%s,"
        "\"caCertificateLoaded\":%s,"
        "\"deliveredEvents\":%lu,\"failedAttempts\":%lu,"
        "\"lastSuccessEpoch\":%lu,\"nextRetryMs\":%lu,"
        "\"lastError\":\"%s\",\"baseUrl\":",
        remote_gateway.enabled ? "true" : "false",
        remote_gateway.provisioned ? "true" : "false",
        remote_gateway.task_running ? "true" : "false",
        remote_gateway.ca_certificate_loaded
            ? "true"
            : "false",
        static_cast<unsigned long>(
            remote_gateway.delivered_events),
        static_cast<unsigned long>(
            remote_gateway.failed_attempts),
        static_cast<unsigned long>(
            remote_gateway.last_success_epoch),
        static_cast<unsigned long>(
            remote_gateway.next_retry_ms),
        remote_alarm_relay_error_code(
            remote_gateway.last_error));
    ota_http_server.sendContent(line);
    ota_portal_send_json_escaped(
        remote_gateway.base_url);
    ota_http_server.sendContent(",\"deviceId\":");
    ota_portal_send_json_escaped(
        remote_gateway.device_id);
    ota_http_server.sendContent("},");

    snprintf(line, sizeof(line),
             "\"config\":{\"target_temp\":%.2f,\"temp_hysteresis\":%.2f,\"co2TargetPh\":%.2f,\"co2MaxTimeMin\":%u,\"dev_mode\":%s,"
             "\"modem_sleep\":%s,\"always_screen_on\":%s,\"sound_enabled\":%s,\"quiet_hours_enabled\":%s,",
             cfg.targetTemp,
             cfg.tempHysteresis,
             static_cast<double>(co2_target_ph),
             static_cast<unsigned>(co2_max_time_minutes),
             cfg.devMode ? "true" : "false",
             cfg.modemSleep ? "true" : "false",
             cfg.alwaysScreenOn ? "true" : "false",
             cfg.soundEnabled ? "true" : "false",
             cfg.quietHoursEnabled ? "true" : "false");
    ota_http_server.sendContent(line);
    ota_http_server.sendContent("\"quiet_start\":");
    ota_portal_send_time_json(cfg.quietStartHour, cfg.quietStartMinute);
    ota_http_server.sendContent(",\"quiet_end\":");
    ota_portal_send_time_json(cfg.quietEndHour, cfg.quietEndMinute);
    ota_http_server.sendContent("},");

    const uint32_t ato_runtime_seconds = runtime.waterFillOn && ato_started_ms != 0U
                                             ? static_cast<uint32_t>(millis() - ato_started_ms) / 1000UL
                                             : 0U;
    snprintf(line, sizeof(line),
             "\"display\":{\"autoBrightness\":%s,\"profile\":\"%s\",\"brightness\":%u,\"appliedBrightness\":%u},"
             "\"water\":{\"timeoutSec\":%u,\"active\":%s,\"timeoutLatched\":%s,\"runtimeSec\":%lu},"
             "\"leak\":{\"action\":\"%s\"},",
             display_auto_brightness ? "true" : "false",
             display_profile_code(display_power_profile),
             static_cast<unsigned>(display_max_brightness),
             static_cast<unsigned>(hal_display_get_brightness()),
             static_cast<unsigned>(water_timeout_seconds),
             runtime.waterFillOn ? "true" : "false",
             ato_timeout_latched ? "true" : "false",
             static_cast<unsigned long>(ato_runtime_seconds),
             leak_action_code(leak_action));
    ota_http_server.sendContent(line);

    const bool api_ph_sensor_enabled = cfg.devMode || cfg.showPhSensor;
    const bool api_ec_enabled = cfg.devMode || cfg.enableEc;
    const bool api_water_level_enabled = cfg.devMode || cfg.enableWaterLevel;
    const bool api_leak_enabled = cfg.devMode || cfg.enableLeak;
    const bool api_flow_enabled = cfg.devMode || cfg.enableFlow;

    snprintf(line, sizeof(line),
             "\"modules\":{\"light_on\":%s,\"plant_light_on\":%s,\"light1_on\":%s,\"light2_on\":%s,\"filter_on\":%s,\"air_on\":%s,"
             "\"co2_on\":%s,\"heater_on\":%s,\"heater_enabled\":%s,\"ph_sensor_enabled\":%s,\"co2_enabled\":%s,"
             "\"ec_enabled\":%s,\"water_level_enabled\":%s,\"water_dosing_on\":%s,\"leak_enabled\":%s,\"flow_enabled\":%s,"
             "\"feeder_enabled\":%s},",
             runtime.lightOn ? "true" : "false",
             runtime.plantLightOn ? "true" : "false",
             runtime.lightOn ? "true" : "false",
             runtime.plantLightOn ? "true" : "false",
             runtime.filterOn ? "true" : "false",
             runtime.airOn ? "true" : "false",
             runtime.co2On ? "true" : "false",
             runtime.heaterOn ? "true" : "false",
             cfg.enableHeater ? "true" : "false",
             api_ph_sensor_enabled ? "true" : "false",
             cfg.enableCo2 ? "true" : "false",
             api_ec_enabled ? "true" : "false",
             api_water_level_enabled ? "true" : "false",
             runtime.waterFillOn ? "true" : "false",
             api_leak_enabled ? "true" : "false",
             api_flow_enabled ? "true" : "false",
             cfg.feedEnabled ? "true" : "false");
    ota_http_server.sendContent(line);

    ota_http_server.sendContent("\"schedules\":{");
    ota_portal_send_schedule_json("light1", cfg.lightMode, cfg.lightStartHour, cfg.lightStartMinute,
                                  cfg.lightEndHour, cfg.lightEndMinute,
                                  static_cast<int8_t>(runtime.lightActiveMode),
                                  cfg.lightMode == static_cast<uint8_t>(ScheduleMode::Schedule) && config_uses_factory_light_window());
    ota_http_server.sendContent(",");
    ota_portal_send_schedule_json("light2", cfg.plantLightMode, cfg.plantStartHour, cfg.plantStartMinute,
                                  cfg.plantEndHour, cfg.plantEndMinute,
                                  static_cast<int8_t>(runtime.plantLightActiveMode),
                                  cfg.plantLightMode == static_cast<uint8_t>(ScheduleMode::Schedule) && config_uses_factory_light2_window());
    ota_http_server.sendContent(",");
    ota_portal_send_schedule_json("light", cfg.lightMode, cfg.lightStartHour, cfg.lightStartMinute,
                                  cfg.lightEndHour, cfg.lightEndMinute,
                                  static_cast<int8_t>(runtime.lightActiveMode),
                                  cfg.lightMode == static_cast<uint8_t>(ScheduleMode::Schedule) && config_uses_factory_light_window());
    ota_http_server.sendContent(",");
    ota_portal_send_schedule_json("plant_light", cfg.plantLightMode, cfg.plantStartHour, cfg.plantStartMinute,
                                  cfg.plantEndHour, cfg.plantEndMinute,
                                  static_cast<int8_t>(runtime.plantLightActiveMode),
                                  cfg.plantLightMode == static_cast<uint8_t>(ScheduleMode::Schedule) && config_uses_factory_light2_window());
    ota_http_server.sendContent(",");
    ota_portal_send_schedule_json("filter", cfg.filterMode, cfg.filterStartHour, cfg.filterStartMinute, cfg.filterEndHour, cfg.filterEndMinute);
    ota_http_server.sendContent(",");
    ota_portal_send_schedule_json("air", cfg.airMode, cfg.airStartHour, cfg.airStartMinute, cfg.airEndHour, cfg.airEndMinute);
    ota_http_server.sendContent(",\"feeder\":{\"enabled\":");
    ota_portal_send_bool(cfg.feedEnabled);
    snprintf(line, sizeof(line),
             ",\"count\":%u,\"time1\":\"%02u:%02u\",\"time2\":\"%02u:%02u\"}},",
             static_cast<unsigned>(cfg.feedCount),
             static_cast<unsigned>(cfg.feedHour1),
             static_cast<unsigned>(cfg.feedMinute1),
             static_cast<unsigned>(cfg.feedHour2),
             static_cast<unsigned>(cfg.feedMinute2));
    ota_http_server.sendContent(line);

    snprintf(line, sizeof(line),
             "\"eco\":{\"safe_active\":%s,\"quiet_window\":%s,\"deep_ready\":%s,"
             "\"rtc_ready\":%s,\"wake_after_sec\":%lu,\"last_wake_cause\":%u,\"blockers\":",
             eco.safeEcoActive ? "true" : "false",
             eco.quietWindow ? "true" : "false",
             eco.deepReady ? "true" : "false",
             eco.rtcReady ? "true" : "false",
             static_cast<unsigned long>(eco.plannedWakeAfterSec),
             static_cast<unsigned>(eco_rtc_state.lastWakeCause));
    ota_http_server.sendContent(line);
    ota_portal_send_eco_blockers_json(eco.blockers);
    ota_http_server.sendContent("},");

    snprintf(line, sizeof(line),
             "\"clock\":{\"year\":%d,\"month\":%d,\"day\":%d,\"hour\":%d,\"minute\":%d,\"second\":%d,"
             "\"valid\":%s,\"source\":",
             clock_year,
             clock_month,
             clock_day,
             clock_hour,
             clock_minute,
             clock_second,
             controller_clock_reliable ? "true" : "false");
    ota_http_server.sendContent(line);
    ota_portal_send_json_escaped(controller_clock_source);
    snprintf(line, sizeof(line), ",\"staRetryCooldownMs\":%lu},",
             static_cast<unsigned long>(wifi_retry_cooldown_ms));
    ota_http_server.sendContent(line);

    snprintf(line, sizeof(line),
             "\"temperature\":{\"current\":%s,\"target\":%.2f,\"hysteresis\":%.2f,"
             "\"historyCapacity\":%u,\"historyIntervalMinutes\":1",
             temp_json,
             cfg.targetTemp,
             cfg.tempHysteresis,
             static_cast<unsigned>(TEMP_HISTORY_POINTS));
    ota_http_server.sendContent(line);
    if (include_history) {
        ota_http_server.sendContent(",\"history\":[");
        for (uint8_t i = 0; i < history_count; ++i) {
            if (i > 0) ota_http_server.sendContent(",");
            char sample[40];
            char value_json[16];
            if (isfinite(temp_history[i])) {
                snprintf(value_json, sizeof(value_json), "%.2f", temp_history[i]);
            } else {
                snprintf(value_json, sizeof(value_json), "null");
            }
            snprintf(sample, sizeof(sample), "{\"value\":%s,\"epoch\":%lu}",
                     value_json,
                     static_cast<unsigned long>(history_epoch[i] > 0 ? history_epoch[i] : controller_unix_time()));
            ota_http_server.sendContent(sample);
        }
        ota_http_server.sendContent("]");
    }
    ota_http_server.sendContent(",\"heaterMode\":");
    ota_http_server.sendContent(cfg.heaterMode == static_cast<uint8_t>(HeaterMode::Off) ? "1" : "0");
    ota_http_server.sendContent("},");

    snprintf(line, sizeof(line),
             "\"battery\":{\"voltage\":%s,\"percent\":%s},"
             "\"firmware\":{\"version\":\"%s\",\"apiVersion\":%u,"
             "\"buildDate\":\"%s\",\"buildTime\":\"%s\"},"
             "\"network\":{\"staConnected\":%s,\"staConnecting\":%s,\"apMode\":%s,"
             "\"serviceMode\":%s,\"serviceModePending\":false,\"staSsid\":",
             battery_voltage_json,
             battery_percent_json,
             FirmwareInfo::VERSION,
             static_cast<unsigned>(FirmwareInfo::API_VERSION),
             __DATE__,
             __TIME__,
             wifi_connected ? "true" : "false",
             is_connecting ? "true" : "false",
             wifi_ota_active ? "true" : "false",
             ota_portal_sta_running ? "true" : "false");
    ota_http_server.sendContent(line);
    ota_portal_send_json_escaped(WiFi.SSID().c_str());
    snprintf(line, sizeof(line),
             ",\"configuredStaSsid\":");
    ota_http_server.sendContent(line);
    ota_portal_send_json_escaped(selected_ssid);
    snprintf(line, sizeof(line),
             ",\"configuredApSsid\":");
    ota_http_server.sendContent(line);
    ota_portal_send_json_escaped(Secrets::OTA_AP_SSID);
    snprintf(line, sizeof(line),
             ",\"ssid\":");
    ota_http_server.sendContent(line);
    ota_portal_send_json_escaped(wifi_ota_active ? WiFi.softAPSSID().c_str() : WiFi.SSID().c_str());
    snprintf(line, sizeof(line),
             ",\"ip\":\"%s\",\"rssi\":%d,\"clients\":%u,\"lastTimeSyncOk\":%s,"
             "\"lastTimeSyncStatus\":",
             ip_buf,
             wifi_rssi,
             static_cast<unsigned>(wifi_ota_active ? WiFi.softAPgetStationNum() : 0),
             controller_clock_reliable ? "true" : "false");
    ota_http_server.sendContent(line);
    ota_portal_send_json_escaped(controller_clock_source);
    ota_http_server.sendContent("},");

    snprintf(line, sizeof(line),
             "\"web\":{\"focus\":%s,\"activeClients\":%u,\"lastSeenMs\":%lu,\"timeoutMs\":%lu,"
             "\"cpuProfile\":\"%s\",\"localUiDeferred\":%s,\"sensorControlIntervalMs\":1000},",
             gui_web_focus_blocks_local_ui() ? "true" : "false",
             static_cast<unsigned>(active_web_clients),
             static_cast<unsigned long>(web_idle_ms),
             static_cast<unsigned long>(WEB_UI_CLIENT_TIMEOUT_MS),
             gui_web_focus_blocks_local_ui() ? "web_sensor_control" : "local_ui",
             gui_web_focus_blocks_local_ui() ? "true" : "false");
    ota_http_server.sendContent(line);

    const RuntimeSafetyStatus safety =
        runtime_safety_status();
    snprintf(
        line, sizeof(line),
        "\"system\":{\"uptime\":%lu,\"powerMode\":\"%s\",\"resetReason\":\"%u\","
        "\"freeHeap\":%lu,\"largestHeap\":%lu,\"minimumFreeHeap\":%lu,"
        "\"bootId\":%lu,\"bootCount\":%lu,\"faultCount\":%lu,"
        "\"lastFaultReason\":\"%s\",\"lastFaultUptimeMs\":%lu,"
        "\"lastFailSafeConfirmed\":%s,\"actuatorWriteErrors\":%lu},",
        static_cast<unsigned long>(millis() / 1000UL),
        cfg.modemSleep ? "modem_sleep" : "normal",
        static_cast<unsigned>(esp_reset_reason()),
        static_cast<unsigned long>(
            heap_caps_get_free_size(MALLOC_CAP_8BIT)),
        static_cast<unsigned long>(
            heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
        static_cast<unsigned long>(
            safety.current_minimum_free_heap),
        static_cast<unsigned long>(safety.boot_id),
        static_cast<unsigned long>(safety.boot_count),
        static_cast<unsigned long>(safety.fault_count),
        runtime_fault_reason_code(
            safety.last_reset.fault_reason),
        static_cast<unsigned long>(
            safety.last_reset.fault_uptime_ms),
        safety.last_reset.fail_safe_confirmed
            ? "true"
            : "false",
        static_cast<unsigned long>(
            actuator_write_error_count));
    ota_http_server.sendContent(line);

    snprintf(line, sizeof(line),
             "\"relays\":{\"light\":%s,\"plantLight\":%s,\"light1\":%s,\"light2\":%s,\"pump\":%s,\"heater\":%s,"
             "\"co2\":%s,\"aeration\":%s,\"waterDosing\":%s,\"aerationPercent\":%u},",
             runtime.lightOn ? "true" : "false",
             runtime.plantLightOn ? "true" : "false",
             runtime.lightOn ? "true" : "false",
             runtime.plantLightOn ? "true" : "false",
             runtime.filterOn ? "true" : "false",
             runtime.heaterOn ? "true" : "false",
             runtime.co2On ? "true" : "false",
             runtime.airOn ? "true" : "false",
             runtime.waterFillOn ? "true" : "false",
             runtime.airOn ? 100U : 0U);
    ota_http_server.sendContent(line);

    const uint32_t light_status_ms = millis();
    const aquarium::AquaelLightSnapshot front_light =
        mcp_outputs.frontLight.snapshot(light_status_ms);
    const aquarium::AquaelLightSnapshot rear_light =
        mcp_outputs.rearLight.snapshot(light_status_ms);
    const aquarium::AquaelProfile front_reported_profile =
        front_light.known
            ? front_light.profile
            : aquael_domain_profile(runtime.lightActiveMode);
    const aquarium::AquaelProfile rear_reported_profile =
        rear_light.known
            ? rear_light.profile
            : aquael_domain_profile(runtime.plantLightActiveMode);
    ota_http_server.sendContent("\"lights\":{");
    snprintf(
        line, sizeof(line),
        "\"front\":{\"label\":\"Przednia\",\"relay\":\"light1\","
        "\"on\":%s,\"profile\":\"%s\",\"profileName\":\"%s\","
        "\"profileCycle\":%s,\"transitioning\":%s,\"known\":%s},",
        (cfg.devMode ? runtime.lightOn : front_light.relay_on) ? "true" : "false",
        aquarium::AquaelLightController::profile_code(front_reported_profile),
        aquarium::AquaelLightController::profile_name(front_reported_profile),
        cfg.lightMode == static_cast<uint8_t>(ScheduleMode::Schedule) &&
                config_uses_factory_light_window()
            ? "true"
            : "false",
        front_light.transitioning ? "true" : "false",
        (cfg.devMode || front_light.known) ? "true" : "false");
    ota_http_server.sendContent(line);
    snprintf(
        line, sizeof(line),
        "\"rear\":{\"label\":\"Tylna\",\"relay\":\"light2\","
        "\"on\":%s,\"profile\":\"%s\",\"profileName\":\"%s\","
        "\"profileCycle\":%s,\"transitioning\":%s,\"known\":%s},",
        (cfg.devMode ? runtime.plantLightOn : rear_light.relay_on) ? "true" : "false",
        aquarium::AquaelLightController::profile_code(rear_reported_profile),
        aquarium::AquaelLightController::profile_name(rear_reported_profile),
        cfg.plantLightMode == static_cast<uint8_t>(ScheduleMode::Schedule) &&
                config_uses_factory_light2_window()
            ? "true"
            : "false",
        rear_light.transitioning ? "true" : "false",
        (cfg.devMode || rear_light.known) ? "true" : "false");
    ota_http_server.sendContent(line);
    snprintf(
        line, sizeof(line),
        "\"light1\":{\"on\":%s,\"profile\":\"%s\",\"profileName\":\"%s\","
        "\"transitioning\":%s,\"known\":%s},",
        (cfg.devMode ? runtime.lightOn : front_light.relay_on) ? "true" : "false",
        aquarium::AquaelLightController::profile_code(front_reported_profile),
        aquarium::AquaelLightController::profile_name(front_reported_profile),
        front_light.transitioning ? "true" : "false",
        (cfg.devMode || front_light.known) ? "true" : "false");
    ota_http_server.sendContent(line);
    snprintf(
        line, sizeof(line),
        "\"light2\":{\"on\":%s,\"profile\":\"%s\",\"profileName\":\"%s\","
        "\"transitioning\":%s,\"known\":%s},"
        "\"supportedProfiles\":[\"day\",\"daybreak\",\"night\"]},",
        (cfg.devMode ? runtime.plantLightOn : rear_light.relay_on) ? "true" : "false",
        aquarium::AquaelLightController::profile_code(rear_reported_profile),
        aquarium::AquaelLightController::profile_name(rear_reported_profile),
        rear_light.transitioning ? "true" : "false",
        (cfg.devMode || rear_light.known) ? "true" : "false");
    ota_http_server.sendContent(line);

    snprintf(line, sizeof(line),
             "\"schedule\":{\"lightMode\":%u,\"dayStartHour\":%u,\"dayStartMin\":%u,"
             "\"dayEndHour\":%u,\"dayEndMin\":%u,\"airMode\":%u,\"airStartHour\":%u,"
             "\"airStartMin\":%u,\"airEndHour\":%u,\"airEndMin\":%u,\"filterMode\":%u,"
             "\"filterStartHour\":%u,\"filterStartMin\":%u,\"filterEndHour\":%u,"
             "\"filterEndMin\":%u,\"heaterMode\":%u",
             static_cast<unsigned>(cfg.lightMode),
             static_cast<unsigned>(cfg.lightStartHour),
             static_cast<unsigned>(cfg.lightStartMinute),
             static_cast<unsigned>(cfg.lightEndHour),
             static_cast<unsigned>(cfg.lightEndMinute),
             static_cast<unsigned>(cfg.airMode),
             static_cast<unsigned>(cfg.airStartHour),
             static_cast<unsigned>(cfg.airStartMinute),
             static_cast<unsigned>(cfg.airEndHour),
             static_cast<unsigned>(cfg.airEndMinute),
             static_cast<unsigned>(cfg.filterMode),
             static_cast<unsigned>(cfg.filterStartHour),
             static_cast<unsigned>(cfg.filterStartMinute),
             static_cast<unsigned>(cfg.filterEndHour),
             static_cast<unsigned>(cfg.filterEndMinute),
             static_cast<unsigned>(cfg.heaterMode));
    ota_http_server.sendContent(line);
    snprintf(line, sizeof(line),
             ",\"lightProfile\":%u,\"lightProfileName\":\"%s\",\"plantLightMode\":%u,"
             "\"plantStartHour\":%u,\"plantStartMin\":%u,\"plantEndHour\":%u,\"plantEndMin\":%u,"
             "\"plantLightProfile\":%u,\"plantLightProfileName\":\"%s\"},",
             static_cast<unsigned>(runtime.lightActiveMode),
             light_color_mode_label(runtime.lightActiveMode),
             static_cast<unsigned>(cfg.plantLightMode),
             static_cast<unsigned>(cfg.plantStartHour),
             static_cast<unsigned>(cfg.plantStartMinute),
             static_cast<unsigned>(cfg.plantEndHour),
             static_cast<unsigned>(cfg.plantEndMinute),
             static_cast<unsigned>(runtime.plantLightActiveMode),
             light_color_mode_label(runtime.plantLightActiveMode));
    ota_http_server.sendContent(line);

    snprintf(line, sizeof(line),
             "\"feeding\":{\"active\":%s,\"freq\":%u,\"hour\":%u,\"minute\":%u,"
             "\"lastFeedEpoch\":%lu,\"lastResult\":",
             feeder_pulse_active ? "true" : "false",
             cfg.feedEnabled ? 1U : 0U,
             static_cast<unsigned>(cfg.feedHour1),
             static_cast<unsigned>(cfg.feedMinute1),
             static_cast<unsigned long>(last_feed_epoch));
    ota_http_server.sendContent(line);
    ota_portal_send_json_escaped(last_feed_result);
    const uint32_t control_now_ms = millis();
    const aquarium::OperatingModeSnapshot modes =
        control_modes.mode_snapshot(control_now_ms);
    snprintf(line, sizeof(line),
             "},\"controlState\":{\"feedingMode\":{\"active\":%s,"
             "\"remainingSec\":%lu},\"serviceMode\":{\"active\":%s,"
             "\"remainingSec\":%lu},\"overrides\":[",
             modes.feeding_active ? "true" : "false",
             static_cast<unsigned long>(modes.feeding_remaining_seconds),
             modes.service_active ? "true" : "false",
             static_cast<unsigned long>(modes.service_remaining_seconds));
    ota_http_server.sendContent(line);
    bool first_override = true;
    for (uint8_t index = 0U;
         index < static_cast<uint8_t>(aquarium::OutputTarget::Count);
         ++index) {
        const aquarium::OutputTarget target =
            static_cast<aquarium::OutputTarget>(index);
        const aquarium::TimedOverrideSnapshot override_state =
            control_modes.override_snapshot(target, control_now_ms);
        if (!override_state.active) {
            continue;
        }
        snprintf(line, sizeof(line),
                 "%s{\"target\":\"%s\",\"state\":%s,\"remainingSec\":%lu}",
                 first_override ? "" : ",",
                 aquarium::ControlModeManager::target_name(target),
                 override_state.state ? "true" : "false",
                 static_cast<unsigned long>(override_state.remaining_seconds));
        ota_http_server.sendContent(line);
        first_override = false;
    }
    ota_http_server.sendContent("]}}");
    ota_http_server.sendContent("");
}

static void ota_portal_send_log_array(const GuiLogEntry *entries, uint8_t count, const char *level) {
    ota_http_server.sendContent("[");
    for (uint8_t i = 0; i < count; ++i) {
        if (i > 0) {
            ota_http_server.sendContent(",");
        }
        char meta[64];
        snprintf(meta, sizeof(meta), "{\"ts\":%lu,\"level\":",
                 static_cast<unsigned long>(entries[i].ts));
        ota_http_server.sendContent(meta);
        ota_portal_send_json_escaped(level);
        ota_http_server.sendContent(",\"code\":");
        ota_portal_send_json_escaped(entries[i].critical ? "wazne" : "info");
        ota_http_server.sendContent(",\"message\":");
        ota_portal_send_json_escaped(entries[i].message);
        ota_http_server.sendContent("}");
    }
    ota_http_server.sendContent("]");
}

static void ota_portal_handle_logs() {
    ota_portal_mark_web_activity();
    if (!ota_portal_require_pin()) {
        return;
    }

    if (ota_http_server.hasArg("format") && ota_http_server.arg("format") == "text") {
        const bool critical = ota_http_server.hasArg("type") && ota_http_server.arg("type") == "critical";
        const GuiLogEntry *entries = critical ? gui_logs_important : gui_logs_normal;
        const uint8_t count = critical ? gui_logs_important_count : gui_logs_normal_count;
        ota_portal_no_cache();
        ota_http_server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        ota_http_server.send(200, "text/plain; charset=utf-8", "");
        for (uint8_t i = 0; i < count; ++i) {
            char line[144];
            snprintf(line, sizeof(line), "%lu %s %s\n",
                     static_cast<unsigned long>(entries[i].ts),
                     entries[i].critical ? "WAZNE" : "INFO",
                     entries[i].message);
            ota_http_server.sendContent(line);
        }
        ota_http_server.sendContent("");
        return;
    }

    ota_portal_no_cache();
    ota_http_server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    ota_http_server.send(200, "application/json", "");
    ota_http_server.sendContent("{\"normal\":");
    ota_portal_send_log_array(gui_logs_normal, gui_logs_normal_count, "info");
    ota_http_server.sendContent(",\"critical\":");
    ota_portal_send_log_array(gui_logs_important, gui_logs_important_count, "error");
    char counts[64];
    snprintf(counts, sizeof(counts), ",\"counts\":{\"normal\":%u,\"critical\":%u}}",
             static_cast<unsigned>(gui_logs_normal_count),
             static_cast<unsigned>(gui_logs_important_count));
    ota_http_server.sendContent(counts);
    ota_http_server.sendContent("");
}

static bool parse_time_text(const char *text, uint8_t *hour, uint8_t *minute) {
    if (text == nullptr || hour == nullptr || minute == nullptr) {
        return false;
    }
    int h = -1;
    int m = -1;
    if (sscanf(text, "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59) {
        return false;
    }
    *hour = static_cast<uint8_t>(h);
    *minute = snap_minute(m);
    return true;
}

static bool parse_time_arg(const char *name, uint8_t *hour, uint8_t *minute) {
    if (name == nullptr || !ota_http_server.hasArg(name)) {
        return false;
    }
    String value = ota_http_server.arg(name);
    return parse_time_text(value.c_str(), hour, minute);
}

static bool parse_strict_long_arg(
    const char *name,
    long minimum,
    long maximum,
    long *out) {
    if (name == nullptr ||
        out == nullptr ||
        !ota_http_server.hasArg(name)) {
        return false;
    }
    String value = ota_http_server.arg(name);
    value.trim();
    if (value.length() == 0U) {
        return false;
    }
    char *end = nullptr;
    const long parsed =
        strtol(value.c_str(), &end, 10);
    if (end == value.c_str() ||
        *end != '\0' ||
        parsed < minimum ||
        parsed > maximum) {
        return false;
    }
    *out = parsed;
    return true;
}

static bool parse_strict_float_arg(
    const char *name,
    float minimum,
    float maximum,
    float *out) {
    if (name == nullptr ||
        out == nullptr ||
        !ota_http_server.hasArg(name)) {
        return false;
    }
    String value = ota_http_server.arg(name);
    value.trim();
    if (value.length() == 0U) {
        return false;
    }
    char *end = nullptr;
    const float parsed =
        strtof(value.c_str(), &end);
    if (end == value.c_str() ||
        *end != '\0' ||
        !isfinite(parsed) ||
        parsed < minimum ||
        parsed > maximum) {
        return false;
    }
    *out = parsed;
    return true;
}

static uint8_t parse_mode_arg(const char *name, uint8_t fallback) {
    if (name == nullptr || !ota_http_server.hasArg(name)) {
        return fallback;
    }
    const int value = ota_http_server.arg(name).toInt();
    return is_schedule_mode(static_cast<uint8_t>(value))
               ? static_cast<uint8_t>(value)
               : fallback;
}

static bool parse_light_profile_arg(const char *name, uint8_t *out_profile) {
    if (name == nullptr || out_profile == nullptr || !ota_http_server.hasArg(name)) {
        return false;
    }
    String value = ota_http_server.arg(name);
    value.trim();
    value.toLowerCase();
    if (value == "day" || value == "0") {
        *out_profile = 0U;
        return true;
    }
    if (value == "daybreak" || value == "dawn" || value == "sunrise" || value == "1") {
        *out_profile = 1U;
        return true;
    }
    if (value == "night" || value == "moon" || value == "2") {
        *out_profile = 2U;
        return true;
    }
    return false;
}

static bool parse_bool_arg(const char *name, bool fallback) {
    if (name == nullptr || !ota_http_server.hasArg(name)) {
        return fallback;
    }
    String value = ota_http_server.arg(name);
    value.toLowerCase();
    return value == "1" || value == "true" || value == "on" || value == "tak";
}

static void json_escape_to_buffer(const char *source, char *destination, size_t destination_size) {
    if (destination == nullptr || destination_size == 0U) {
        return;
    }
    size_t output = 0U;
    if (source != nullptr) {
        for (size_t input = 0U; source[input] != '\0' && output + 1U < destination_size; ++input) {
            const unsigned char c = static_cast<unsigned char>(source[input]);
            if (c == '"' || c == '\\') {
                if (output + 2U >= destination_size) {
                    break;
                }
                destination[output++] = '\\';
                destination[output++] = static_cast<char>(c);
            } else if (c >= 32U && c <= 126U) {
                destination[output++] = static_cast<char>(c);
            } else {
                destination[output++] = '?';
            }
        }
    }
    destination[output] = '\0';
}

static void ota_portal_send_action_result(bool success, const char *code, const char *message) {
    ota_portal_no_cache();
    char escaped_code[72];
    char escaped_message[192];
    char body[352];
    json_escape_to_buffer(code != nullptr ? code : (success ? "ok" : "error"),
                          escaped_code, sizeof(escaped_code));
    json_escape_to_buffer(message != nullptr ? message : "",
                          escaped_message, sizeof(escaped_message));
    snprintf(body, sizeof(body),
             "{\"success\":%s,\"ok\":%s,\"code\":\"%s\",\"message\":\"%s\"}",
             success ? "true" : "false",
             success ? "true" : "false",
             escaped_code,
             escaped_message);
    ota_http_server.send(success ? 200 : 400, "application/json", body);
}

static bool relay_profile_has_valid_shape(const String &data) {
    if (data.length() < RELAY_PROFILE_MIN_BYTES || data.length() > RELAY_PROFILE_MAX_BYTES) {
        return false;
    }

    size_t first = 0U;
    while (first < data.length() && isspace(static_cast<unsigned char>(data[first]))) {
        ++first;
    }
    size_t last = data.length();
    while (last > first && isspace(static_cast<unsigned char>(data[last - 1U]))) {
        --last;
    }
    if (first >= last || data[first] != '{' || data[last - 1U] != '}') {
        return false;
    }
    if (data.indexOf("\"relayBoard\"") < 0 || data.indexOf("\"relays\"") < 0) {
        return false;
    }
    for (uint8_t channel = 1U; channel <= 8U; ++channel) {
        char marker[18];
        snprintf(marker, sizeof(marker), "\"channel\":%u", static_cast<unsigned>(channel));
        if (data.indexOf(marker) < 0) {
            return false;
        }
    }
    return true;
}

static bool save_relay_profile_to_sd(const String &data) {
    if (!relay_profile_has_valid_shape(data) || !ota_portal_sd_ready()) {
        return false;
    }
    if (!ensure_sd_directory("/aq") || !ensure_sd_directory("/aq/config")) {
        return false;
    }

    if (SD.exists(RELAY_PROFILE_TEMP_PATH) && !SD.remove(RELAY_PROFILE_TEMP_PATH)) {
        return false;
    }
    File profile = SD.open(RELAY_PROFILE_TEMP_PATH, FILE_WRITE);
    if (!profile) {
        return false;
    }
    const size_t written = profile.print(data);
    profile.flush();
    profile.close();
    if (written != data.length()) {
        SD.remove(RELAY_PROFILE_TEMP_PATH);
        return false;
    }

    if (SD.exists(RELAY_PROFILE_BACKUP_PATH)) {
        SD.remove(RELAY_PROFILE_BACKUP_PATH);
    }
    const bool had_previous = SD.exists(RELAY_PROFILE_PATH);
    if (had_previous && !SD.rename(RELAY_PROFILE_PATH, RELAY_PROFILE_BACKUP_PATH)) {
        SD.remove(RELAY_PROFILE_TEMP_PATH);
        return false;
    }
    if (!SD.rename(RELAY_PROFILE_TEMP_PATH, RELAY_PROFILE_PATH)) {
        if (had_previous) {
            SD.rename(RELAY_PROFILE_BACKUP_PATH, RELAY_PROFILE_PATH);
        }
        SD.remove(RELAY_PROFILE_TEMP_PATH);
        return false;
    }
    if (had_previous) {
        SD.remove(RELAY_PROFILE_BACKUP_PATH);
    }
    return true;
}

static void ota_portal_handle_action() {
    ota_portal_mark_web_activity();
    char request_json[768] = {};
    bool json_request = false;
    if (ota_http_server.hasArg("plain")) {
        const String &body = ota_http_server.arg("plain");
        if (body.length() >= sizeof(request_json)) {
            ota_portal_send_action_result(
                false,
                "request_too_large",
                "Ladunek JSON przekracza bezpieczny limit 767 bajtow.");
            return;
        }
        body.toCharArray(request_json, sizeof(request_json));
        json_request = request_json[0] != '\0';
    }

    char action_name[48] = {};
    if (ota_http_server.hasArg("action")) {
        ota_http_server.arg("action").toCharArray(
            action_name, sizeof(action_name));
    } else if (ota_http_server.hasArg("name")) {
        ota_http_server.arg("name").toCharArray(
            action_name, sizeof(action_name));
    } else if (json_request) {
        if (!ble_json_read_string(
                request_json, "name", action_name, sizeof(action_name))) {
            ble_json_read_string(
                request_json, "action", action_name, sizeof(action_name));
        }
    }
    if (action_name[0] == '\0') {
        ota_portal_send_action_result(false, "missing_action", "Brak parametru action.");
        return;
    }
    if (strcmp(
            action_name,
            "save_remote_gateway") == 0 ||
        strcmp(
            action_name,
            "set_remote_gateway_enabled") == 0 ||
        strcmp(
            action_name,
            "clear_remote_gateway") == 0 ||
        strcmp(
            action_name,
            "save_espnow_link") == 0 ||
        strcmp(
            action_name,
            "clear_espnow_link") == 0) {
        secure_clear_gui_buffer(
            request_json, sizeof(request_json));
        ota_portal_no_cache();
        ota_http_server.send(
            403,
            "application/json",
            "{\"ok\":false,\"success\":false,"
            "\"code\":\"secure_transport_required\","
            "\"message\":\"Konfiguracja sekretow lacza jest dostepna "
            "wylacznie przez zaszyfrowane i uwierzytelnione BLE.\"}");
        return;
    }

    String action(action_name);
    if (is_v2_control_action(action_name)) {
        char command_id[aquarium::IdempotencyLedger::kCommandIdBytes] = {};
        char token[aquarium::AdminSessionManager::kTokenBytes] = {};
        char pin[16] = {};
        char target[24] = {};
        char profile[16] = {};
        if (ota_http_server.hasArg("commandId")) {
            ota_http_server.arg("commandId").toCharArray(
                command_id, sizeof(command_id));
        } else if (json_request) {
            ble_json_read_string(
                request_json,
                "commandId",
                command_id,
                sizeof(command_id));
        }
        if (ota_http_server.hasArg("token")) {
            ota_http_server.arg("token").toCharArray(token, sizeof(token));
        } else if (json_request) {
            ble_json_read_string(
                request_json, "token", token, sizeof(token));
        }
        if (ota_http_server.hasArg("pin")) {
            ota_http_server.arg("pin").toCharArray(pin, sizeof(pin));
        } else if (json_request) {
            ble_json_read_string(request_json, "pin", pin, sizeof(pin));
        }
        if (ota_http_server.hasArg("target")) {
            ota_http_server.arg("target").toCharArray(target, sizeof(target));
        } else if (json_request) {
            ble_json_read_string(
                request_json, "target", target, sizeof(target));
        }
        if (ota_http_server.hasArg("profile")) {
            ota_http_server.arg("profile").toCharArray(profile, sizeof(profile));
        } else if (json_request) {
            ble_json_read_string(
                request_json, "profile", profile, sizeof(profile));
        }
        bool state = parse_bool_arg("state", false);
        bool dispense = parse_bool_arg("dispense", false);
        if (json_request) {
            ble_json_read_bool(request_json, "state", &state);
            ble_json_read_bool(request_json, "dispense", &dispense);
        }
        const long default_duration =
            action == "start_feeding_mode"
                ? 600L
                : (action == "start_service_mode" ? 1800L : 30L);
        long duration_seconds = ota_http_server.hasArg("durationSec")
                                    ? ota_http_server.arg("durationSec").toInt()
                                    : default_duration;
        if (json_request) {
            ble_json_read_long(
                request_json, "durationSec", 0L, 86400L, &duration_seconds);
        }
        char command_json[384];
        snprintf(
            command_json,
            sizeof(command_json),
            "{\"v\":2,\"commandId\":\"%s\",\"name\":\"%s\","
            "\"args\":{\"target\":\"%s\",\"state\":%s,"
            "\"durationSec\":%ld,\"dispense\":%s,\"profile\":\"%s\"}}",
            command_id,
            action_name,
            target,
            state ? "true" : "false",
            duration_seconds,
            dispense ? "true" : "false",
            profile);
        bool duplicate = false;
        char replay_code[40] = {};
        char replay_message[128] = {};
        const GuiBleCommandResult result = gui_app_v2_action(
            action_name,
            json_request ? request_json : command_json,
            pin,
            token,
            command_id,
            &duplicate,
            replay_code,
            sizeof(replay_code),
            replay_message,
            sizeof(replay_message));
        char escaped_code[72];
        char escaped_message[192];
        char escaped_command_id[112];
        json_escape_to_buffer(
            result.code != nullptr ? result.code : "internal_error",
            escaped_code,
            sizeof(escaped_code));
        json_escape_to_buffer(
            result.message != nullptr ? result.message : "",
            escaped_message,
            sizeof(escaped_message));
        json_escape_to_buffer(
            command_id,
            escaped_command_id,
            sizeof(escaped_command_id));
        char response[480];
        snprintf(
            response,
            sizeof(response),
            "{\"type\":\"response\",\"v\":2,\"ok\":%s,"
            "\"code\":\"%s\",\"message\":\"%s\",\"commandId\":\"%s\","
            "\"ts\":%lu,\"duplicate\":%s}",
            result.success ? "true" : "false",
            escaped_code,
            escaped_message,
            escaped_command_id,
            static_cast<unsigned long>(controller_unix_time()),
            duplicate ? "true" : "false");
        int http_status = result.success ? 200 : 400;
        if (strcmp(escaped_code, "session_required") == 0 ||
            strcmp(escaped_code, "session_expired") == 0) {
            http_status = 401;
        } else if (strcmp(escaped_code, "command_id_conflict") == 0 ||
                   strcmp(escaped_code, "mode_conflict") == 0) {
            http_status = 409;
        } else if (strcmp(escaped_code, "controller_busy") == 0 ||
                   strcmp(escaped_code, "output_unavailable") == 0) {
            http_status = 503;
        }
        ota_portal_no_cache();
        ota_http_server.send(http_status, "application/json", response);
        return;
    }
    if (action == "auth_check") {
        if (!ota_portal_require_pin()) {
            return;
        }
        ota_portal_send_action_result(true, "admin_authenticated", "Admin authenticated.");
        return;
    }

    if (action == "feed_now") {
        if (!ota_portal_require_pin()) {
            return;
        }
        const bool ok = run_feeder_pulse("Karmienie", "Dawka z WWW", false);
        ota_portal_send_action_result(ok,
                                      ok ? (cfg.devMode ? "dev_simulated" : "feed_ok") : last_feed_result,
                                      ok ? (cfg.devMode ? "Karmienie zasymulowane w trybie DEV." : "Karmienie uruchomione.")
                                         : "Nie uruchomiono karmnika.");
        return;
    }

    const bool action_requires_admin =
        action == "set_light" ||
        action == "set_light1" ||
        action == "set_light2" ||
        action == "set_filter" ||
        action == "set_plant" ||
        action == "set_heater" ||
        action == "set_aeration" ||
        action == "save_schedule" ||
        action == "save_temperature" ||
        action == "save_calibration" ||
        action == "save_network" ||
        action == "save_display" ||
        action == "save_co2" ||
        action == "save_water" ||
        action == "save_leak" ||
        action == "save_relays" ||
        action == "test_relay" ||
        action == "wifi_session_start" ||
        action == "wifi_session_stop" ||
        action == "sync_time_ntp";
    if (action_requires_admin && !ota_portal_require_pin()) {
        return;
    }

    if (action == "set_light" || action == "set_light1" || action == "set_light2" ||
        action == "set_filter" || action == "set_plant" ||
        action == "set_heater" || action == "set_aeration") {
        if (!cfg.devMode && !hal_mcp_is_present()) {
            ota_portal_send_action_result(false, "output_unavailable", "Wyjscia fizyczne sa niedostepne w tym trybie.");
            return;
        }
        const bool state = parse_bool_arg("state", false);
        if (action == "set_light" || action == "set_light1") {
            cfg.lightMode = state ? static_cast<uint8_t>(ScheduleMode::AlwaysOn)
                                  : static_cast<uint8_t>(ScheduleMode::AlwaysOff);
            runtime.lightOn = state;
        } else if (action == "set_filter") {
            cfg.filterMode = state ? static_cast<uint8_t>(ScheduleMode::AlwaysOn)
                                   : static_cast<uint8_t>(ScheduleMode::AlwaysOff);
            runtime.filterOn = state;
        } else if (action == "set_plant" || action == "set_light2") {
            cfg.plantLightMode = state ? static_cast<uint8_t>(ScheduleMode::AlwaysOn)
                                       : static_cast<uint8_t>(ScheduleMode::AlwaysOff);
            runtime.plantLightOn = state;
        } else if (action == "set_heater") {
            cfg.heaterMode = state ? static_cast<uint8_t>(HeaterMode::Threshold)
                                   : static_cast<uint8_t>(HeaterMode::Off);
            cfg.enableHeater = state;
            if (!state) {
                runtime.heaterOn = false;
            }
        } else {
            cfg.airMode = state ? static_cast<uint8_t>(ScheduleMode::AlwaysOn)
                                : static_cast<uint8_t>(ScheduleMode::AlwaysOff);
            runtime.airOn = state;
        }
        gui_app_save_settings();
        apply_mcp_outputs();
        if (!gui_web_focus_blocks_local_ui()) {
            gui_sync_widgets_to_state();
        }
        ota_portal_send_action_result(true, "ok", "Stan zapisany.");
        return;
    }

    if (action == "save_schedule") {
        cfg.lightMode = parse_mode_arg("light1Mode", parse_mode_arg("lightMode", cfg.lightMode));
        uint8_t parsed_profile = 0U;
        if (parse_light_profile_arg("light1Profile", &parsed_profile) ||
            parse_light_profile_arg("lightProfile", &parsed_profile)) {
            cfg.lightColorMode = parsed_profile;
            cfg.lightSchedColorMode = aquael_profile_to_schedule(parsed_profile);
        }
        if ((ota_http_server.hasArg("light1ProfileCycle") && parse_bool_arg("light1ProfileCycle", false)) ||
            (ota_http_server.hasArg("lightProfileCycle") && parse_bool_arg("lightProfileCycle", false))) {
            cfg.lightSchedColorMode = 0U;
        }
        if (!parse_time_arg("light1Start", &cfg.lightStartHour, &cfg.lightStartMinute)) {
            parse_time_arg("dayStart", &cfg.lightStartHour, &cfg.lightStartMinute);
        }
        if (!parse_time_arg("light1End", &cfg.lightEndHour, &cfg.lightEndMinute)) {
            parse_time_arg("dayEnd", &cfg.lightEndHour, &cfg.lightEndMinute);
        }
        cfg.plantLightMode = parse_mode_arg("light2Mode", parse_mode_arg("plantLightMode", cfg.plantLightMode));
        if (parse_light_profile_arg("light2Profile", &parsed_profile) ||
            parse_light_profile_arg("plantLightProfile", &parsed_profile)) {
            cfg.plantLightColorMode = parsed_profile;
            cfg.plantSchedColorMode = aquael_profile_to_schedule(parsed_profile);
        }
        if ((ota_http_server.hasArg("light2ProfileCycle") && parse_bool_arg("light2ProfileCycle", false)) ||
            (ota_http_server.hasArg("plantLightProfileCycle") && parse_bool_arg("plantLightProfileCycle", false))) {
            cfg.plantSchedColorMode = 0U;
        }
        if (!parse_time_arg("light2Start", &cfg.plantStartHour, &cfg.plantStartMinute)) {
            parse_time_arg("plantLightStart", &cfg.plantStartHour, &cfg.plantStartMinute);
        }
        if (!parse_time_arg("light2End", &cfg.plantEndHour, &cfg.plantEndMinute)) {
            parse_time_arg("plantLightEnd", &cfg.plantEndHour, &cfg.plantEndMinute);
        }
        cfg.airMode = parse_mode_arg("aerationMode", cfg.airMode);
        parse_time_arg("airOn", &cfg.airStartHour, &cfg.airStartMinute);
        parse_time_arg("airOff", &cfg.airEndHour, &cfg.airEndMinute);
        cfg.filterMode = parse_mode_arg("filterMode", cfg.filterMode);
        parse_time_arg("filterOn", &cfg.filterStartHour, &cfg.filterStartMinute);
        parse_time_arg("filterOff", &cfg.filterEndHour, &cfg.filterEndMinute);
        if (ota_http_server.hasArg("heaterMode")) {
            const int heater_mode = ota_http_server.arg("heaterMode").toInt();
            cfg.heaterMode = heater_mode == static_cast<int>(HeaterMode::Off)
                                 ? static_cast<uint8_t>(HeaterMode::Off)
                                 : static_cast<uint8_t>(HeaterMode::Threshold);
            cfg.enableHeater = cfg.heaterMode != static_cast<uint8_t>(HeaterMode::Off);
        }
        const int feed_freq = ota_http_server.hasArg("feedFreq") ? ota_http_server.arg("feedFreq").toInt() : (cfg.feedEnabled ? 1 : 0);
        cfg.feedEnabled = feed_freq > 0;
        if (parse_time_arg("feedTime", &cfg.feedHour1, &cfg.feedMinute1)) {
            cfg.feedCount = 1;
        }
        sanitize_config(cfg);
        gui_app_save_settings();
        if (!gui_web_focus_blocks_local_ui()) {
            gui_sync_widgets_to_state();
        }
        ota_portal_send_action_result(true, "ok", "Harmonogramy zapisane.");
        return;
    }

    if (action == "save_temperature") {
        const int mode = ota_http_server.hasArg("heaterMode") ? ota_http_server.arg("heaterMode").toInt() : cfg.heaterMode;
        cfg.heaterMode = mode == static_cast<int>(HeaterMode::Off)
                             ? static_cast<uint8_t>(HeaterMode::Off)
                             : static_cast<uint8_t>(HeaterMode::Threshold);
        cfg.enableHeater = cfg.heaterMode != static_cast<uint8_t>(HeaterMode::Off);
        if (ota_http_server.hasArg("target")) {
            cfg.targetTemp = ota_http_server.arg("target").toFloat();
        }
        if (ota_http_server.hasArg("hysteresis")) {
            cfg.tempHysteresis = ota_http_server.arg("hysteresis").toFloat();
        }
        sanitize_config(cfg);
        gui_app_save_settings();
        if (!gui_web_focus_blocks_local_ui()) {
            gui_sync_widgets_to_state();
        }
        ota_portal_send_action_result(true, "ok", "Ustawienia temperatury zapisane.");
        return;
    }

    if (action == "save_calibration") {
        char type[8] = {};
        if (ota_http_server.hasArg("type")) {
            ota_http_server.arg("type").toCharArray(type, sizeof(type));
        } else if (json_request) {
            ble_json_read_string(
                request_json, "type", type, sizeof(type));
        }
        bool saved = false;
        if (strcmp(type, "ph") == 0) {
            long low_raw = 0L;
            long high_raw = 0L;
            float low_reference = 4.01f;
            float high_reference = 6.86f;
            bool low_raw_valid = false;
            bool high_raw_valid = false;
            bool low_reference_valid = true;
            bool high_reference_valid = true;
            if (json_request) {
                low_raw_valid = ble_json_read_long(
                    request_json,
                    "lowRaw",
                    INT16_MIN,
                    INT16_MAX,
                    &low_raw);
                high_raw_valid = ble_json_read_long(
                    request_json,
                    "highRaw",
                    INT16_MIN,
                    INT16_MAX,
                    &high_raw);
                if (strstr(
                        request_json,
                        "\"lowReference\"") != nullptr) {
                    low_reference_valid =
                        ble_json_read_float(
                            request_json,
                            "lowReference",
                            0.0f,
                            14.0f,
                            &low_reference);
                }
                if (strstr(
                        request_json,
                        "\"highReference\"") != nullptr) {
                    high_reference_valid =
                        ble_json_read_float(
                            request_json,
                            "highReference",
                            0.0f,
                            14.0f,
                            &high_reference);
                }
            } else {
                low_raw_valid =
                    parse_strict_long_arg(
                        "lowRaw",
                        INT16_MIN,
                        INT16_MAX,
                        &low_raw);
                high_raw_valid =
                    parse_strict_long_arg(
                        "highRaw",
                        INT16_MIN,
                        INT16_MAX,
                        &high_raw);
                if (ota_http_server.hasArg(
                        "lowReference")) {
                    low_reference_valid =
                        parse_strict_float_arg(
                            "lowReference",
                            0.0f,
                            14.0f,
                            &low_reference);
                }
                if (ota_http_server.hasArg(
                        "highReference")) {
                    high_reference_valid =
                        parse_strict_float_arg(
                            "highReference",
                            0.0f,
                            14.0f,
                            &high_reference);
                }
            }
            saved =
                low_raw_valid &&
                high_raw_valid &&
                low_reference_valid &&
                high_reference_valid &&
                sensor_calibration_store_save_ph(
                    static_cast<int16_t>(low_raw),
                    low_reference,
                    static_cast<int16_t>(high_raw),
                    high_reference);
        } else if (strcmp(type, "ec") == 0) {
            long reference_raw = 0L;
            float reference = 1413.0f;
            float coefficient = 0.019f;
            float reference_temperature = 25.0f;
            bool raw_valid = false;
            bool reference_valid = true;
            bool coefficient_valid = true;
            bool temperature_valid = true;
            if (json_request) {
                raw_valid = ble_json_read_long(
                    request_json,
                    "referenceRaw",
                    1L,
                    INT16_MAX,
                    &reference_raw);
                if (strstr(
                        request_json,
                        "\"referenceUsCm\"") != nullptr) {
                    reference_valid =
                        ble_json_read_float(
                            request_json,
                            "referenceUsCm",
                            1.0f,
                            100000.0f,
                            &reference);
                }
                if (strstr(
                        request_json,
                        "\"temperatureCoefficient\"") != nullptr) {
                    coefficient_valid =
                        ble_json_read_float(
                            request_json,
                            "temperatureCoefficient",
                            0.0f,
                            0.1f,
                            &coefficient);
                }
                if (strstr(
                        request_json,
                        "\"referenceTemperatureC\"") != nullptr) {
                    temperature_valid =
                        ble_json_read_float(
                            request_json,
                            "referenceTemperatureC",
                            0.0f,
                            50.0f,
                            &reference_temperature);
                }
            } else {
                raw_valid =
                    parse_strict_long_arg(
                        "referenceRaw",
                        1L,
                        INT16_MAX,
                        &reference_raw);
                if (ota_http_server.hasArg(
                        "referenceUsCm")) {
                    reference_valid =
                        parse_strict_float_arg(
                            "referenceUsCm",
                            1.0f,
                            100000.0f,
                            &reference);
                }
                if (ota_http_server.hasArg(
                        "temperatureCoefficient")) {
                    coefficient_valid =
                        parse_strict_float_arg(
                            "temperatureCoefficient",
                            0.0f,
                            0.1f,
                            &coefficient);
                }
                if (ota_http_server.hasArg(
                        "referenceTemperatureC")) {
                    temperature_valid =
                        parse_strict_float_arg(
                            "referenceTemperatureC",
                            0.0f,
                            50.0f,
                            &reference_temperature);
                }
            }
            saved =
                raw_valid &&
                reference_valid &&
                coefficient_valid &&
                temperature_valid &&
                sensor_calibration_store_save_ec(
                    static_cast<int16_t>(
                        reference_raw),
                    reference,
                    coefficient,
                    reference_temperature);
        }
        ota_portal_send_action_result(
            saved,
            saved ? "calibration_saved" : "invalid_calibration",
            saved ? "Kalibracja zostala zapisana w NVS."
                  : "Kalibracja jest niepelna lub niepoprawna.");
        return;
    }

    if (action == "save_network") {
        char ssid[64] = "";
        char password[WIFI_PASSWORD_MAX_LEN + 1] = "";
        if (ota_http_server.hasArg("staSsid")) {
            ota_http_server.arg("staSsid").toCharArray(ssid, sizeof(ssid));
        }
        if (ota_http_server.hasArg("staPassword")) {
            ota_http_server.arg("staPassword").toCharArray(password, sizeof(password));
        }
        const bool ok =
            ssid[0] != '\0' &&
            save_wifi_profile_to_sd(ssid, password, "", 0);
        secure_clear_gui_buffer(
            password, sizeof(password));
        if (ok) {
            snprintf(selected_ssid, sizeof(selected_ssid), "%s", ssid);
        }
        ota_portal_send_action_result(
            ok,
            ok ? "ok" : "wifi_profile_error",
            ok ? "Profil WiFi zapisany bezpiecznie w NVS."
               : "Nie zapisano profilu WiFi.");
        return;
    }

    if (action == "wifi_session_start") {
        try_autoconnect_wifi_profile();
        ota_portal_send_action_result(true, "ok", "Sesja WiFi uruchomiona.");
        return;
    }

    if (action == "wifi_session_stop") {
        ota_portal_send_action_result(true, "ok", "Sesja WiFi zatrzymana.");
        wifi_disconnect_pending = true;
        wifi_disconnect_at_ms = millis() + 250UL;
        return;
    }

    if (action == "sync_time_ntp") {
        if (!wifi_connected) {
            ota_portal_send_action_result(false, "wifi_required", "Brak polaczenia WiFi.");
            return;
        }
        const bool accepted = sync_clock_from_ntp(5000U);
        ota_portal_send_action_result(
            accepted,
            accepted ? "ntp_started" : "ntp_start_failed",
            accepted ? "Synchronizacja NTP uruchomiona."
                     : "Nie mozna uruchomic synchronizacji NTP.");
        return;
    }

    if (action == "clear_critical_logs") {
        if (!ota_portal_require_pin()) {
            return;
        }
        gui_logs_important_count = 0;
        if (log_list_important != nullptr) {
            lv_obj_clean(log_list_important);
        }
        ota_portal_send_action_result(true, "ok", "Wyczyszczono wazne logi.");
        return;
    }

    if (action == "restart_device") {
        if (!ota_portal_require_pin()) {
            return;
        }
        ota_reboot_reason =
            RuntimeFaultReason::ManualRestart;
        ota_reboot_pending = true;
        ota_reboot_at_ms = millis() + 1000UL;
        ota_portal_send_action_result(true, "ok", "Restart za chwile.");
        return;
    }

    if (action == "factory_reset") {
        if (!ota_portal_require_pin()) {
            return;
        }
        if (prefs.begin("aquarium", false)) {
            prefs.clear();
            prefs.end();
        }
        device_credentials_factory_reset();
        wifi_credential_store_clear();
        sensor_calibration_store_reset_defaults();
        remote_alarm_relay_clear();
        admin_sessions.clear();
        load_default_config(cfg);
        display_auto_brightness = true;
        display_max_brightness = 100U;
        display_power_profile = DisplayPowerProfile::AlwaysOn;
        co2_target_ph = FACTORY_CO2_TARGET_PH;
        co2_max_time_minutes = 540U;
        water_timeout_seconds = 120U;
        leak_action = LeakAction::DisableAll;
        runtime.waterFillOn = false;
        ato_started_ms = 0U;
        ato_timeout_latched = false;
        gui_app_save_settings();
        apply_mcp_outputs();
        ota_reboot_reason =
            RuntimeFaultReason::FactoryReset;
        ota_reboot_pending = true;
        ota_reboot_at_ms = millis() + 1200UL;
        ota_portal_send_action_result(true, "ok", "Konfiguracja wyczyszczona. Restart za chwile.");
        return;
    }

    if (action == "save_display") {
        display_auto_brightness = parse_bool_arg("autoBrightness", display_auto_brightness);
        if (ota_http_server.hasArg("brightness")) {
            display_max_brightness = clamp_u8(ota_http_server.arg("brightness").toInt(), 10, 100);
        }
        if (ota_http_server.hasArg("profile")) {
            DisplayPowerProfile parsed_profile = display_power_profile;
            if (!parse_display_profile(ota_http_server.arg("profile"), &parsed_profile)) {
                ota_portal_send_action_result(false, "invalid_display_profile", "Nieprawidlowy profil ekranu.");
                return;
            }
            display_power_profile = parsed_profile;
        }
        gui_app_save_settings();
        apply_display_backlight(last_ldr_value, last_ldr_valid);
        ota_portal_send_action_result(true, "ok", "Ustawienia ekranu zapisane i zastosowane.");
        return;
    }

    if (action == "save_co2") {
        cfg.enableCo2 = parse_bool_arg("co2Enabled", cfg.enableCo2);
        if (ota_http_server.hasArg("targetPh")) {
            co2_target_ph = constrain(ota_http_server.arg("targetPh").toFloat(), 5.0f, 8.5f);
        }
        if (ota_http_server.hasArg("co2Limit")) {
            co2_max_time_minutes = static_cast<uint16_t>(
                constrain(ota_http_server.arg("co2Limit").toInt(), 1, 1440));
        }
        gui_app_save_settings();
        if (!gui_web_focus_blocks_local_ui()) {
            gui_sync_widgets_to_state();
        }
        ota_portal_send_action_result(true, "ok", "Ustawienia CO2 zapisane.");
        return;
    }

    if (action == "save_water") {
        cfg.enableWaterLevel = parse_bool_arg("waterEnabled", cfg.enableWaterLevel);
        if (ota_http_server.hasArg("waterTimeout")) {
            water_timeout_seconds = static_cast<uint16_t>(
                constrain(ota_http_server.arg("waterTimeout").toInt(), 5, 300));
        }
        if (!cfg.enableWaterLevel) {
            runtime.waterFillOn = false;
            ato_started_ms = 0U;
            ato_timeout_latched = false;
        }
        gui_app_save_settings();
        apply_mcp_outputs();
        if (!gui_web_focus_blocks_local_ui()) {
            gui_sync_widgets_to_state();
        }
        ota_portal_send_action_result(true, "ok", "Ustawienia ATO zapisane.");
        return;
    }

    if (action == "save_leak") {
        cfg.enableLeak = parse_bool_arg("leakEnabled", cfg.enableLeak);
        if (ota_http_server.hasArg("leakAction")) {
            LeakAction parsed_action = leak_action;
            if (!parse_leak_action(ota_http_server.arg("leakAction"), &parsed_action)) {
                ota_portal_send_action_result(false, "invalid_leak_action", "Nieprawidlowa akcja wycieku.");
                return;
            }
            leak_action = parsed_action;
        }
        gui_app_save_settings();
        if (!gui_web_focus_blocks_local_ui()) {
            gui_sync_widgets_to_state();
        }
        ota_portal_send_action_result(true, "ok", "Ustawienia wycieku zapisane.");
        return;
    }

    if (action == "save_relays") {
        if (!ota_http_server.hasArg("data")) {
            ota_portal_send_action_result(false, "missing_relay_profile", "Brak profilu mapowania.");
            return;
        }
        const String profile = ota_http_server.arg("data");
        if (!relay_profile_has_valid_shape(profile)) {
            ota_portal_send_action_result(false, "invalid_relay_profile", "Profil musi zawierac kompletna mape osmiu kanalow.");
            return;
        }
        if (!save_relay_profile_to_sd(profile)) {
            ota_portal_send_action_result(false, "relay_profile_write_failed", "Nie zapisano profilu mapowania na karcie SD.");
            return;
        }
        ota_portal_send_action_result(true, "ok", "Profil mapowania zapisany na karcie SD.");
        return;
    }

    if (action == "test_relay") {
        const int channel = ota_http_server.hasArg("channel") ? ota_http_server.arg("channel").toInt() : 0;
        if (channel < 1 || channel > 8) {
            ota_portal_send_action_result(false, "invalid_relay_channel", "Kanal testowy musi byc w zakresie 1-8.");
            return;
        }
        const bool state = parse_bool_arg("state", false);
        const uint32_t duration_ms = state
            ? static_cast<uint32_t>(constrain(ota_http_server.hasArg("duration")
                                                  ? ota_http_server.arg("duration").toInt() * 1000
                                                  : static_cast<int>(RELAY_TEST_MAX_DURATION_MS),
                                              100,
                                              static_cast<int>(RELAY_TEST_MAX_DURATION_MS)))
            : 0U;
        if (!start_relay_test(static_cast<uint8_t>(channel), state, duration_ms)) {
            ota_portal_send_action_result(false, "relay_test_unavailable", "Nie mozna sterowac wybranym kanalem.");
            return;
        }
        ota_portal_send_action_result(true, "ok", state ? "Test kanalu uruchomiony na maksymalnie 3 sekundy." : "Test kanalu zatrzymany.");
        return;
    }

    ota_portal_send_action_result(false, "unknown_action", "Nieznana akcja.");
}

static void ota_portal_handle_web_session() {
    ota_portal_mark_web_activity();
    const uint32_t now_ms = millis();

    char session_id[WEB_UI_SESSION_ID_MAX_LEN + 1U] = "";
    if (ota_http_server.hasArg("sid")) {
        ota_http_server.arg("sid").toCharArray(session_id, sizeof(session_id));
    }

    bool closing = false;
    if (ota_http_server.hasArg("state")) {
        String state = ota_http_server.arg("state");
        state.trim();
        state.toLowerCase();
        closing = state == "close" || state == "closed" || state == "release" || state == "inactive";
    }

    if (closing) {
        web_ui_release_client(session_id);
    } else {
        web_ui_track_client(session_id, now_ms);
    }

    const uint8_t active_clients = web_ui_active_client_count(now_ms);
    if (active_clients == 0U && closing) {
        web_ui_last_request_ms = 0;
    }

    ota_portal_no_cache();
    char body[128];
    snprintf(body, sizeof(body),
             "{\"ok\":true,\"activeClients\":%u,\"focus\":%s,\"timeoutMs\":%lu}",
             static_cast<unsigned>(active_clients),
             ota_portal_has_recent_web_activity(now_ms) ? "true" : "false",
             static_cast<unsigned long>(WEB_UI_CLIENT_TIMEOUT_MS));
    ota_http_server.send(200, "application/json", body);
}

static void ota_portal_handle_settime() {
    ota_portal_mark_web_activity();
    if (!ota_portal_require_pin()) {
        return;
    }

    if (!ota_http_server.hasArg("epoch")) {
        ota_http_server.send(400, "text/plain", "missing_epoch");
        return;
    }

    const uint32_t epoch = strtoul(ota_http_server.arg("epoch").c_str(), nullptr, 10);
    if (epoch < 1704067200UL) {
        ota_http_server.send(400, "text/plain", "invalid_epoch");
        return;
    }

    setenv("TZ", NTP_TZ_POLAND, 1);
    tzset();
    const time_t raw = static_cast<time_t>(epoch);
    struct tm local_tm = {};
    if (localtime_r(&raw, &local_tm) == nullptr) {
        ota_http_server.send(500, "text/plain", "time_convert_failed");
        return;
    }

    clock_year = local_tm.tm_year + 1900;
    clock_month = local_tm.tm_mon + 1;
    clock_day = local_tm.tm_mday;
    clock_hour = local_tm.tm_hour;
    clock_minute = local_tm.tm_min;
    clock_second = local_tm.tm_sec;
    if (!gui_save_clock_settings(true, "browser")) {
        ota_http_server.send(500, "text/plain", "save_failed");
        return;
    }
    ota_http_server.send(200, "text/plain", "ok");
}

static void ota_portal_handle_events() {
    ota_portal_mark_web_activity();
    ota_portal_no_cache();
    ota_http_server.sendHeader("Connection", "close");
    ota_http_server.send(200, "text/event-stream", "event: ready\ndata: {}\n\n");
}

static void ota_portal_handle_alarm_events() {
    ota_portal_mark_web_activity();
    AlarmTransitionEvent events[
        ALARM_EVENT_QUEUE_CAPACITY] = {};
    const size_t count =
        alarm_event_queue_snapshot(
            events, ALARM_EVENT_QUEUE_CAPACITY);

    ota_portal_no_cache();
    ota_http_server.setContentLength(
        CONTENT_LENGTH_UNKNOWN);
    ota_http_server.send(
        200, "application/json", "");
    char line[320];
    snprintf(
        line, sizeof(line),
        "{\"schemaVersion\":1,\"activeFlags\":%u,"
        "\"count\":%u,\"events\":[",
        alarm_event_queue_active_flags(),
        static_cast<unsigned>(count));
    ota_http_server.sendContent(line);
    for (size_t index = 0U; index < count; ++index) {
        const AlarmTransitionEvent &event = events[index];
        snprintf(
            line, sizeof(line),
            "%s{\"eventId\":\"%lu-%lu-%08lx\","
            "\"bootId\":%lu,\"sequence\":%lu,"
            "\"timestamp\":%lu,\"timestampReliable\":%s,"
            "\"nonce\":%lu,\"activeFlags\":%u,"
            "\"raisedFlags\":%u,\"clearedFlags\":%u}",
            index == 0U ? "" : ",",
            static_cast<unsigned long>(event.boot_id),
            static_cast<unsigned long>(
                event.event_sequence),
            static_cast<unsigned long>(event.nonce),
            static_cast<unsigned long>(event.boot_id),
            static_cast<unsigned long>(
                event.event_sequence),
            static_cast<unsigned long>(
                event.timestamp),
            event.timestamp_reliable
                ? "true"
                : "false",
            static_cast<unsigned long>(event.nonce),
            event.active_flags,
            event.raised_flags,
            event.cleared_flags);
        ota_http_server.sendContent(line);
    }
    ota_http_server.sendContent("]}");
}

static void ota_portal_handle_current_history_csv() {
    ota_portal_mark_web_activity();
    ota_http_server.sendHeader("Content-Disposition", "attachment; filename=\"cydAkwarium-current-history.csv\"");
    ota_portal_no_cache();
    ota_http_server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    ota_http_server.send(200, "text/csv", "");
    ota_http_server.sendContent("schema_version,generated_epoch,index,epoch,temp_c,temp_valid,ph,ph_valid,ldr,ldr_valid,heap_bytes,heater_on\n");
    const uint32_t generated_epoch = controller_unix_time();
    for (uint8_t i = 0; i < history_count; ++i) {
        char temp_buf[16];
        char ph_buf[16];
        char ldr_buf[16];
        const bool temp_valid = isfinite(temp_history[i]);
        const bool ph_valid = isfinite(ph_history[i]);
        const bool ldr_valid = ldr_history[i] >= 0;
        if (isfinite(temp_history[i])) {
            snprintf(temp_buf, sizeof(temp_buf), "%.2f", temp_history[i]);
        } else {
            temp_buf[0] = '\0';
        }
        if (isfinite(ph_history[i])) {
            snprintf(ph_buf, sizeof(ph_buf), "%.3f", ph_history[i]);
        } else {
            ph_buf[0] = '\0';
        }
        if (ldr_valid) {
            snprintf(ldr_buf, sizeof(ldr_buf), "%d", ldr_history[i]);
        } else {
            ldr_buf[0] = '\0';
        }
        char line[128];
        snprintf(line, sizeof(line), "1,%lu,%u,%lu,%s,%u,%s,%u,%s,%u,%lu,%u\n",
                 static_cast<unsigned long>(generated_epoch),
                 static_cast<unsigned>(i),
                 static_cast<unsigned long>(history_epoch[i] > 0 ? history_epoch[i] : generated_epoch),
                 temp_buf,
                 temp_valid ? 1U : 0U,
                 ph_buf,
                 ph_valid ? 1U : 0U,
                 ldr_buf,
                 ldr_valid ? 1U : 0U,
                 static_cast<unsigned long>(heap_history[i]),
                 heater_history[i] ? 1U : 0U);
        ota_http_server.sendContent(line);
    }
    ota_http_server.sendContent("");
}

static void ota_portal_handle_files() {
    ota_portal_mark_web_activity();
    char dir[96];
    snprintf(dir, sizeof(dir), "%s", OTA_PORTAL_HISTORY_DIR);
    if (ota_http_server.hasArg("dir")) {
        String arg = ota_http_server.arg("dir");
        arg.toCharArray(dir, sizeof(dir));
    }

    if (!ota_portal_allowed_data_path(dir)) {
        ota_http_server.send(403, "application/json", "{\"ok\":false,\"error\":\"forbidden\"}");
        return;
    }
    if (!ota_portal_sd_ready()) {
        ota_http_server.send(503, "application/json", "{\"ok\":false,\"error\":\"sd_unavailable\"}");
        return;
    }

    File root = SD.open(dir, FILE_READ);
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        ota_http_server.send(404, "application/json", "{\"ok\":false,\"error\":\"directory_not_found\"}");
        return;
    }

    ota_portal_no_cache();
    ota_http_server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    ota_http_server.send(200, "application/json", "");
    ota_http_server.sendContent("{\"ok\":true,\"dir\":");
    ota_portal_send_json_escaped(dir);
    ota_http_server.sendContent(",\"files\":[");

    bool first = true;
    File entry = root.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            const char *entry_name = ota_portal_basename(entry.name());
            char path[144];
            snprintf(path, sizeof(path), "%s/%s", dir, entry_name);
            if (!first) {
                ota_http_server.sendContent(",");
            }
            first = false;
            ota_http_server.sendContent("{\"name\":");
            ota_portal_send_json_escaped(entry_name);
            ota_http_server.sendContent(",\"path\":");
            ota_portal_send_json_escaped(path);
            char meta[48];
            snprintf(meta, sizeof(meta), ",\"size\":%lu}", static_cast<unsigned long>(entry.size()));
            ota_http_server.sendContent(meta);
        }
        entry.close();
        entry = root.openNextFile();
    }
    root.close();
    ota_http_server.sendContent("]}");
    ota_http_server.sendContent("");
}

static void ota_portal_handle_download() {
    ota_portal_mark_web_activity();
    if (!ota_http_server.hasArg("path")) {
        ota_http_server.send(400, "text/plain", "Missing path");
        return;
    }

    char path[144];
    String arg = ota_http_server.arg("path");
    arg.toCharArray(path, sizeof(path));
    if (!ota_portal_allowed_data_path(path)) {
        ota_http_server.send(403, "text/plain", "Forbidden");
        return;
    }
    if (!ota_portal_sd_ready()) {
        ota_http_server.send(503, "text/plain", "SD unavailable");
        return;
    }

    File file = SD.open(path, FILE_READ);
    if (!file || file.isDirectory()) {
        if (file) file.close();
        ota_http_server.send(404, "text/plain", "File not found");
        return;
    }

    char disposition[180];
    snprintf(disposition, sizeof(disposition), "attachment; filename=\"%s\"", ota_portal_basename(path));
    ota_http_server.sendHeader("Content-Disposition", disposition);
    ota_http_server.streamFile(file, ota_portal_content_type(path));
    file.close();
}

static bool ota_portal_require_pin() {
    return ota_portal_require_admin_session();
}

static bool ota_portal_request_has_admin_session() {
    if (!ota_http_server.hasHeader("X-AquaCYD-Session")) {
        return false;
    }
    const String &header = ota_http_server.header("X-AquaCYD-Session");
    if (header.length() + 1U !=
        aquarium::AdminSessionManager::kTokenBytes) {
        return false;
    }
    char token[aquarium::AdminSessionManager::kTokenBytes] = {};
    header.toCharArray(token, sizeof(token));
    return admin_sessions.validate(token, millis());
}

static bool ota_portal_require_admin_session() {
    if (ota_portal_request_has_admin_session()) {
        return true;
    }
    ota_portal_no_cache();
    ota_http_server.send(
        401,
        "application/json",
        "{\"ok\":false,\"success\":false,\"code\":\"session_required\","
        "\"message\":\"Wymagana aktywna sesja administratora.\"}");
    return false;
}

static void ota_portal_handle_v2_logout() {
    ota_portal_mark_web_activity();
    if (!ota_portal_request_has_admin_session()) {
        ota_portal_no_cache();
        ota_http_server.send(
            401,
            "application/json",
            "{\"ok\":false,\"success\":false,\"code\":\"session_required\","
            "\"message\":\"Sesja administratora wygasla.\"}");
        return;
    }
    char token[aquarium::AdminSessionManager::kTokenBytes] = {};
    ota_http_server.header("X-AquaCYD-Session")
        .toCharArray(token, sizeof(token));
    admin_sessions.revoke(token);
    memset(token, 0, sizeof(token));
    ota_portal_no_cache();
    ota_http_server.send(
        200,
        "application/json",
        "{\"ok\":true,\"success\":true,\"code\":\"logged_out\","
        "\"message\":\"Sesja administratora zostala uniewazniona.\"}");
}

static void ota_portal_handle_stop_ota() {
    ota_portal_mark_web_activity();
    if (!ota_portal_require_admin_session()) {
        return;
    }
    ota_portal_no_cache();
    ota_http_server.sendHeader("Connection", "close");
    ota_http_server.send(200, "application/json", "{\"ok\":true,\"message\":\"Portal HTTP zostanie zamkniety.\"}");
    ota_shutdown_pending = true;
    ota_shutdown_at_ms = millis() + 650UL;
    ota_portal_set_status("HTTP: zamykanie portalu...", lv_color_make(245, 158, 11));
}

static void ota_portal_handle_update_finish() {
    ota_portal_mark_web_activity();
    // A successful upload was authenticated again immediately before
    // secure_ota_finish(). Do not turn that committed result into a misleading
    // 401 if the five-minute token expires between the upload and response
    // callbacks.
    if (!ota_http_update_ok &&
        !ota_portal_request_has_admin_session()) {
        ota_http_update_ok = false;
        ota_http_update_failed = true;
        ota_portal_no_cache();
        ota_http_server.send(
            401,
            "application/json",
            "{\"ok\":false,\"code\":\"session_required\","
            "\"message\":\"Wymagana aktywna sesja administratora.\"}");
        return;
    }
    ota_portal_no_cache();
    ota_http_server.sendHeader("Connection", "close");
    if (ota_http_update_ok) {
        ota_http_server.send(200, "application/json", "{\"ok\":true,\"message\":\"Firmware zapisany. Restart za chwile.\"}");
        ota_reboot_reason =
            RuntimeFaultReason::OtaUpdate;
        ota_reboot_pending = true;
        ota_reboot_at_ms = millis() + 1400UL;
        ota_portal_set_status("HTTP OTA: zapis OK, restart...", lv_color_make(16, 185, 129));
    } else {
        char body[160];
        snprintf(body, sizeof(body), "{\"ok\":false,\"message\":\"%s\"}",
                 ota_http_update_msg[0] != '\0' ? ota_http_update_msg : "Blad aktualizacji");
        ota_http_server.send(500, "application/json", body);
        ota_portal_set_status("HTTP OTA: blad aktualizacji", lv_color_make(239, 68, 68));
    }
    ota_http_update_ok = false;
    ota_http_update_failed = false;
}

static void ota_portal_set_update_error(const char *message) {
    if (secure_ota_status().active) {
        secure_ota_abort(
            message != nullptr ? message : "secure_ota_failed");
    }
    if (ota_http_service_mode_owned) {
        control_modes.stop_service();
        ota_http_service_mode_owned = false;
    }
    ota_http_upload_active = false;
    ota_http_upload_bytes = 0;
    ota_http_upload_total = 0;
    ota_http_upload_percent = -1;
    ota_http_update_failed = true;
    ota_http_update_ok = false;
    snprintf(ota_http_update_msg, sizeof(ota_http_update_msg), "%s", message != nullptr ? message : "Blad aktualizacji");
}

static void ota_portal_refresh_upload_screen(bool force) {
    if (!web_ui_focus_active) {
        gui_web_focus_enter();
    } else {
        gui_web_client_screen_update(force);
    }
}

static void ota_portal_handle_update_upload() {
    ota_portal_mark_web_activity();
    HTTPUpload &upload = ota_http_server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        ota_http_update_ok = false;
        ota_http_update_failed = false;
        ota_http_upload_active = false;
        ota_http_upload_bytes = 0;
        ota_http_upload_total = 0;
        ota_http_upload_percent = -1;
        ota_http_update_msg[0] = '\0';
        if (!ota_portal_request_has_admin_session()) {
            ota_portal_set_update_error("Wymagana aktywna sesja");
            ota_portal_refresh_upload_screen(true);
            return;
        }
        String normalized_filename = upload.filename;
        normalized_filename.toLowerCase();
        if (!normalized_filename.endsWith(".aqfw")) {
            ota_portal_set_update_error("Wybierz podpisany plik .aqfw");
            ota_portal_refresh_upload_screen(true);
            return;
        }
        const bool backup_ok = backup_configuration_for_ota();
        char secure_begin_code[48] = {};
        if (!secure_ota_begin(
                feeder_pulse_active,
                backup_ok,
                ESP.getFreeHeap(),
                secure_begin_code,
                sizeof(secure_begin_code))) {
            char message[96];
            snprintf(
                message,
                sizeof(message),
                "OTA niedostepne: %s",
                secure_begin_code[0] != '\0'
                    ? secure_begin_code
                    : "initialization_failed");
            ota_portal_set_update_error(message);
            ota_portal_refresh_upload_screen(true);
            return;
        }
        ota_http_service_mode_owned =
            control_modes.start_service(
                aquarium::ControlModeManager::kServiceMaxSeconds,
                millis()) == aquarium::ControlModeResult::Applied;
        force_safe_service_outputs();
        Serial.printf("HTTP_OTA: upload start %s\n", upload.filename.c_str());
        ota_http_upload_active = true;
        ota_http_upload_total = 0U;
        ota_portal_set_status(
            "HTTP OTA: weryfikacja pakietu...",
            lv_color_make(245, 158, 11));
        ota_portal_refresh_upload_screen(true);
        return;
    }

    if (upload.status == UPLOAD_FILE_WRITE) {
        if (ota_http_update_failed) {
            return;
        }
        char secure_code[48] = {};
        if (!secure_ota_write(
                upload.buf,
                upload.currentSize,
                secure_code,
                sizeof(secure_code))) {
            char message[96];
            snprintf(
                message,
                sizeof(message),
                "OTA odrzucone: %s",
                secure_code[0] != '\0'
                    ? secure_code
                    : "verification_failed");
            ota_portal_set_update_error(message);
            ota_portal_refresh_upload_screen(true);
            return;
        }
        const SecureOtaStatus secure_status = secure_ota_status();
        ota_http_upload_bytes = secure_status.package_bytes_received;
        ota_http_upload_total =
            secure_status.expected_image_bytes > 0U
                ? secure_status.expected_image_bytes +
                      aquarium::kOtaPackageHeaderBytes
                : 0U;
        if (ota_http_upload_total > 0U) {
            const uint32_t percent = static_cast<uint32_t>(
                (static_cast<uint64_t>(ota_http_upload_bytes) * 100ULL) / ota_http_upload_total);
            ota_http_upload_percent = static_cast<int>(percent > 99U ? 99U : percent);
        } else {
            ota_http_upload_percent = -1;
        }
        ota_portal_refresh_upload_screen(false);
        return;
    }

    if (upload.status == UPLOAD_FILE_END) {
        ota_http_upload_bytes = static_cast<uint32_t>(upload.totalSize);
        if (ota_http_update_failed) {
            ota_http_upload_active = false;
            ota_portal_refresh_upload_screen(true);
            return;
        }
        if (!ota_portal_request_has_admin_session()) {
            secure_ota_abort("session_expired");
            ota_portal_set_update_error(
                "Sesja administratora wygasla podczas uploadu");
            ota_portal_refresh_upload_screen(true);
            return;
        }
        char secure_code[48] = {};
        if (secure_ota_finish(secure_code, sizeof(secure_code))) {
            ota_http_update_ok = true;
            ota_http_upload_active = false;
            ota_http_upload_percent = 100;
            const aquarium::OtaPackageMetadata *metadata =
                secure_ota_metadata();
            snprintf(ota_http_update_msg, sizeof(ota_http_update_msg),
                     "Zweryfikowano %s (%lu B)",
                     metadata != nullptr
                         ? metadata->firmware_version
                         : "firmware",
                     static_cast<unsigned long>(upload.totalSize));
            Serial.printf(
                "HTTP_OTA: signed package verified, %lu bytes\n",
                static_cast<unsigned long>(upload.totalSize));
        } else {
            char message[96];
            snprintf(
                message,
                sizeof(message),
                "OTA odrzucone: %s",
                secure_code[0] != '\0'
                    ? secure_code
                    : "verification_failed");
            ota_portal_set_update_error(message);
        }
        ota_portal_refresh_upload_screen(true);
        return;
    }

    if (upload.status == UPLOAD_FILE_ABORTED) {
        secure_ota_abort("upload_aborted");
        ota_http_upload_active = false;
        ota_portal_set_update_error("Upload przerwany");
        ota_portal_refresh_upload_screen(true);
    }
}

static void ota_portal_handle_not_found() {
    if (ota_http_server.method() == HTTP_OPTIONS) {
        ota_http_server.send(204, "text/plain", "");
        return;
    }

    if (ota_http_server.method() == HTTP_GET) {
        String uri = ota_http_server.uri();
        if (uri.length() > 0 && uri.length() < 72 && uri.indexOf("..") < 0) {
            char path[112];
            snprintf(path, sizeof(path), "/aq/ota%s", uri.c_str());
            if (ota_portal_sd_ready() && SD.exists(path)) {
                char served_path[116];
                snprintf(served_path, sizeof(served_path), "%s", path);
                bool use_gzip = false;
                if (ota_portal_client_accepts_gzip()) {
                    char gzip_path[116];
                    snprintf(gzip_path, sizeof(gzip_path), "%s.gz", path);
                    if (SD.exists(gzip_path)) {
                        snprintf(served_path, sizeof(served_path), "%s", gzip_path);
                        use_gzip = true;
                    }
                }
                File file = SD.open(served_path, FILE_READ);
                if (file && !file.isDirectory()) {
                    ota_portal_mark_web_activity();
                    ota_http_server.sendHeader("Cache-Control", "public, max-age=31536000, immutable");
                    if (use_gzip) {
                        ota_http_server.sendHeader("Content-Encoding", "gzip");
                        ota_http_server.sendHeader("Vary", "Accept-Encoding");
                    }
                    ota_http_server.streamFile(file, ota_portal_content_type(path));
                    file.close();
                    return;
                }
                if (file) file.close();
            }
        }
    }

    const IPAddress ip = wifi_ota_active ? WiFi.softAPIP() : WiFi.localIP();
    char redirect[64];
    snprintf(redirect, sizeof(redirect), "http://%u.%u.%u.%u/", ip[0], ip[1], ip[2], ip[3]);
    ota_http_server.sendHeader("Location", redirect, true);
    ota_http_server.send(302, "text/plain", "");
}

static void register_ota_portal_routes() {
    static bool routes_registered = false;
    if (routes_registered) {
        return;
    }
    static const char *collect_headers[] = {
        "Content-Length",
        "Accept-Encoding",
        "X-AquaCYD-Session"
    };
    ota_http_server.collectHeaders(collect_headers, 3);
    ota_http_server.on("/", HTTP_GET, ota_portal_handle_root);
    ota_http_server.on("/index.html", HTTP_GET, ota_portal_handle_root);
    ota_http_server.on("/api/status", HTTP_GET, ota_portal_handle_status);
    ota_http_server.on("/api/v2/capabilities", HTTP_GET, ota_portal_handle_v2_capabilities);
    ota_http_server.on("/api/v2/auth", HTTP_POST, ota_portal_handle_v2_auth);
    ota_http_server.on("/api/v2/logout", HTTP_POST, ota_portal_handle_v2_logout);
    ota_http_server.on("/api/v2/action", HTTP_POST, ota_portal_handle_action);
    ota_http_server.on("/api/bus-diagnostics", HTTP_GET, ota_portal_handle_i2c_scan);
    ota_http_server.on("/api/i2c-scan", HTTP_GET, ota_portal_handle_i2c_scan);
    ota_http_server.on("/api/logs", HTTP_GET, ota_portal_handle_logs);
    ota_http_server.on("/api/action", HTTP_POST, ota_portal_handle_action);
    ota_http_server.on("/api/web-session", HTTP_GET, ota_portal_handle_web_session);
    ota_http_server.on("/api/web-session", HTTP_POST, ota_portal_handle_web_session);
    ota_http_server.on("/api/events", HTTP_GET, ota_portal_handle_events);
    ota_http_server.on("/api/alarm-events", HTTP_GET, ota_portal_handle_alarm_events);
    ota_http_server.on("/api/v2/alarm-events", HTTP_GET, ota_portal_handle_alarm_events);
    ota_http_server.on("/settime", HTTP_POST, ota_portal_handle_settime);
    ota_http_server.on("/api/history.csv", HTTP_GET, ota_portal_handle_current_history_csv);
    ota_http_server.on("/history.csv", HTTP_GET, ota_portal_handle_current_history_csv);
    ota_http_server.on("/api/files", HTTP_GET, ota_portal_handle_files);
    ota_http_server.on("/download", HTTP_GET, ota_portal_handle_download);
    ota_http_server.on("/api/ota/stop", HTTP_POST, ota_portal_handle_stop_ota);
    ota_http_server.on("/generate_204", HTTP_GET, ota_portal_handle_not_found);
    ota_http_server.on("/gen_204", HTTP_GET, ota_portal_handle_not_found);
    ota_http_server.on("/hotspot-detect.html", HTTP_GET, ota_portal_handle_not_found);
    ota_http_server.on("/connecttest.txt", HTTP_GET, ota_portal_handle_not_found);
    ota_http_server.on("/ncsi.txt", HTTP_GET, ota_portal_handle_not_found);
    ota_http_server.on("/update", HTTP_POST, ota_portal_handle_update_finish, ota_portal_handle_update_upload);
    ota_http_server.onNotFound(ota_portal_handle_not_found);
    routes_registered = true;
}

static void stop_mdns_service() {
    if (!ota_mdns_running) {
        return;
    }
    MDNS.end();
    ota_mdns_running = false;
}

static bool start_mdns_service() {
    stop_mdns_service();
    if (!MDNS.begin(Secrets::OTA_HOSTNAME)) {
        Serial.printf("STA_PORTAL: mDNS start failed for %s.local\n", Secrets::OTA_HOSTNAME);
        return false;
    }
    MDNS.addService("http", "tcp", OTA_PORTAL_HTTP_PORT);
    ota_mdns_running = true;
    Serial.printf("STA_PORTAL: mDNS http://%s.local/ ready\n", Secrets::OTA_HOSTNAME);
    return true;
}

static void start_http_portal_common(bool captive_dns) {
    register_ota_portal_routes();
    ota_reboot_pending = false;
    ota_reboot_reason =
        RuntimeFaultReason::ManualRestart;
    ota_shutdown_pending = false;
    ota_http_update_ok = false;
    ota_http_update_failed = false;
    ota_http_update_msg[0] = '\0';

    if (!ota_portal_running) {
        ota_http_server.begin();
    }
    ota_portal_running = true;

    if (captive_dns) {
        if (!ota_portal_dns_running) {
            ota_dns_server.setTTL(0);
            ota_dns_server.setErrorReplyCode(DNSReplyCode::NoError);
            ota_portal_dns_running = ota_dns_server.start(OTA_PORTAL_DNS_PORT, "*", ota_portal_ip);
        }
    } else if (ota_portal_dns_running) {
        ota_dns_server.stop();
        ota_portal_dns_running = false;
    }

    Serial.printf("HTTP_PORTAL: HTTP on %u, DNS %s, SD %s\n",
                  static_cast<unsigned>(OTA_PORTAL_HTTP_PORT),
                  ota_portal_dns_running ? "OK" : "OFF",
                  hal_sd_is_mounted() ? "mounted" : "not mounted");
}

static void start_ota_portal() {
    ota_portal_sta_running = false;
    stop_mdns_service();
    start_http_portal_common(true);
}

static void start_sta_service_portal() {
    if (cfg.modemSleep || WiFi.status() != WL_CONNECTED) {
        return;
    }
    start_mdns_service();
    ota_portal_sta_running = true;
    start_http_portal_common(false);
}

static void stop_ota_portal() {
    if (ota_http_upload_active) {
        secure_ota_abort("portal_stopped");
        ota_http_upload_active = false;
    }
    if (ota_http_service_mode_owned) {
        control_modes.stop_service();
        ota_http_service_mode_owned = false;
    }
    web_ui_clear_clients();
    web_ui_last_request_ms = 0;
    if (!ota_portal_running) {
        if (ota_portal_dns_running) {
            ota_dns_server.stop();
            ota_portal_dns_running = false;
        }
        stop_mdns_service();
        ota_portal_sta_running = false;
        return;
    }
    if (ota_portal_dns_running) {
        ota_dns_server.stop();
        ota_portal_dns_running = false;
    }
    ota_http_server.stop();
    stop_mdns_service();
    ota_portal_running = false;
    ota_portal_sta_running = false;
    ota_shutdown_pending = false;
    ota_http_update_ok = false;
    ota_http_update_failed = false;
}

static void free_wifi_scan_user_data(void) {
    if (sta_list_obj == nullptr || !lv_obj_is_valid(sta_list_obj)) {
        return;
    }
    const uint32_t cnt = lv_obj_get_child_cnt(sta_list_obj);
    for (uint32_t i = 0; i < cnt; ++i) {
        lv_obj_t *child = lv_obj_get_child(sta_list_obj, i);
        if (child == nullptr) {
            continue;
        }
        void *ud = lv_obj_get_user_data(child);
        if (ud != nullptr) {
            free(ud);
            lv_obj_set_user_data(child, nullptr);
        }
    }
}

static void wifi_check_timer_cb(lv_timer_t *timer) {
    LV_UNUSED(timer);

    if (cfg.modemSleep) {
        if (WiFi.getMode() != WIFI_OFF) {
            WiFi.disconnect(true);
            stop_ota_portal();
            WiFi.mode(WIFI_OFF);
            wifi_connected = false;
            wifi_rssi = 0;
            gui_app_update_wifi(0, 0);
        }
        return;
    }

    // 1. Scan handling
    if (is_scanning) {
        if (!scan_started) {
            // Wait 300ms for WiFi driver to initialize before starting scan
            if (millis() - scan_start_ms >= 300) {
                int16_t res = WiFi.scanNetworks(true, false);
                if (res == -1) {
                    scan_started = true;
                } else {
                    static uint8_t scan_fail_retries = 0;
                    scan_fail_retries++;
                    if (scan_fail_retries > 4) {
                        scan_fail_retries = 0;
                        is_scanning = false;
                        if (sta_list_obj != nullptr) {
                            lv_obj_clean(sta_list_obj);
                            lv_obj_t *list_btn = lv_list_add_btn(sta_list_obj, LV_SYMBOL_WARNING, "Skanowanie nieudane. Sprobuj ponownie");
                            style_wifi_list_item(list_btn, lv_color_make(239, 68, 68));
                            lv_obj_add_event_cb(list_btn, btn_sta_handler, LV_EVENT_CLICKED, nullptr);
                        }
                    }
                }
            }
        } else {
            int n = WiFi.scanComplete();
            if (n >= 0) {
                is_scanning = false;
                scan_started = false;
                if (sta_list_obj != nullptr) {
                    uint32_t cnt = lv_obj_get_child_cnt(sta_list_obj);
                    for (uint32_t i = 0; i < cnt; i++) {
                        void *ud = lv_obj_get_user_data(lv_obj_get_child(sta_list_obj, i));
                        if (ud != nullptr) {
                            free(ud);
                        }
                    }
                    lv_obj_clean(sta_list_obj);

                    if (n == 0) {
                        lv_obj_t *list_btn = lv_list_add_btn(sta_list_obj, LV_SYMBOL_WARNING, "Brak sieci. Szukaj ponownie");
                        style_wifi_list_item(list_btn, lv_color_make(239, 68, 68));
                        lv_obj_add_event_cb(list_btn, btn_sta_handler, LV_EVENT_CLICKED, nullptr);
                    } else {
                        for (int i = 0; i < n; i++) {
                            String ssid = WiFi.SSID(i);
                            int32_t rssi = WiFi.RSSI(i);
                            char item_text[96];
                            snprintf(item_text, sizeof(item_text), "%s (%d dBm)", ssid.c_str(), rssi);

                            lv_obj_t *list_btn = lv_list_add_btn(sta_list_obj, LV_SYMBOL_WIFI, item_text);
                            style_wifi_list_item(list_btn, theme_text_main());
                            char *ssid_copy = strdup(ssid.c_str());
                            lv_obj_set_user_data(list_btn, ssid_copy);
                            lv_obj_add_event_cb(list_btn, select_network_cb, LV_EVENT_CLICKED, nullptr);
                        }
                    }
                }
                WiFi.scanDelete();
            } else if (n == WIFI_SCAN_FAILED) {
                is_scanning = false;
                scan_started = false;
                if (sta_list_obj != nullptr) {
                    uint32_t cnt = lv_obj_get_child_cnt(sta_list_obj);
                    for (uint32_t i = 0; i < cnt; i++) {
                        void *ud = lv_obj_get_user_data(lv_obj_get_child(sta_list_obj, i));
                        if (ud != nullptr) {
                            free(ud);
                        }
                    }
                    lv_obj_clean(sta_list_obj);
                    lv_obj_t *list_btn = lv_list_add_btn(sta_list_obj, LV_SYMBOL_WARNING, "Skanowanie nieudane. Sprobuj ponownie");
                    style_wifi_list_item(list_btn, lv_color_make(239, 68, 68));
                    lv_obj_add_event_cb(list_btn, btn_sta_handler, LV_EVENT_CLICKED, nullptr);
                }
            }
        }
    }

    // 2. Connection handling
    if (is_connecting) {
        wl_status_t status = WiFi.status();
        if (status == WL_CONNECTED) {
            is_connecting = false;
            wifi_connected = true;
            wifi_rssi = WiFi.RSSI();
            wifi_retry_policy.on_connected();
            String ip_str = WiFi.localIP().toString();
            start_sta_service_portal();

            gui_app_update_wifi(1, wifi_rssi);
            const bool profile_save_attempted = pending_wifi_password_valid;
            const bool profile_saved = profile_save_attempted &&
                                       save_wifi_profile_to_sd(WiFi.SSID().c_str(),
                                                               pending_wifi_password,
                                                               ip_str.c_str(),
                                                               wifi_rssi);
            clear_pending_wifi_password();

            if (wifi_status_message_lbl != nullptr) {
                const char *status_text = "Panel HTTP gotowy";
                lv_color_t status_color = lv_color_make(16, 185, 129);
                if (profile_saved) {
                    status_text = "Profil WiFi zapisany na SD";
                } else if (profile_save_attempted) {
                    status_text = "STA online, blad zapisu profilu";
                    status_color = lv_color_make(245, 158, 11);
                }
                lv_label_set_text(wifi_status_message_lbl, status_text);
                lv_obj_set_style_text_color(wifi_status_message_lbl, status_color, 0);
            }
            if (wifi_ssid_lbl != nullptr) {
                lv_label_set_text_fmt(wifi_ssid_lbl, LV_SYMBOL_WIFI "  SSID: %s", WiFi.SSID().c_str());
            }
            if (wifi_ip_lbl != nullptr) {
                lv_label_set_text_fmt(wifi_ip_lbl, LV_SYMBOL_RIGHT "  IP: %s", ip_str.c_str());
            }
            if (btn_disconnect != nullptr) {
                lv_obj_clear_flag(btn_disconnect, LV_OBJ_FLAG_HIDDEN);
            }
            if (btn_sta != nullptr) {
                lv_obj_add_flag(btn_sta, LV_OBJ_FLAG_HIDDEN);
            }
            if (btn_ota != nullptr) {
                lv_obj_add_flag(btn_ota, LV_OBJ_FLAG_HIDDEN);
            }
        } else {
            const uint8_t disconnect_reason = wifi_last_disconnect_reason;
            const bool ap_capacity_rejected = wifi_disconnect_reason_is_ap_capacity(disconnect_reason);
            if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL || ap_capacity_rejected ||
                (millis() - conn_start_ms > WIFI_PROFILE_CONNECT_TIMEOUT_MS)) {
                const char *reason_name = wifi_disconnect_reason_name(disconnect_reason);
                Serial.printf("WIFI_STA: connect failed status=%d reason=%u:%s elapsed=%lu ms\n",
                              static_cast<int>(status),
                              static_cast<unsigned>(disconnect_reason),
                              reason_name,
                              static_cast<unsigned long>(millis() - conn_start_ms));
                is_connecting = false;
                wifi_connected = false;
                wifi_rssi = 0;
                WiFi.setAutoReconnect(false);
                WiFi.disconnect(false, false);
                clear_pending_wifi_password();

                gui_app_update_wifi(0, 0);

                if (ap_capacity_rejected) {
                    wifi_retry_policy.on_disconnect(disconnect_reason, millis());
                    add_gui_log("WiFi: router odrzuca STA - zbyt wielu klientow", true);
                }

                if (wifi_status_message_lbl != nullptr) {
                    if (ap_capacity_rejected) {
                        lv_label_set_text_fmt(wifi_status_message_lbl, "Status: Router pelny (%u %s)",
                                              static_cast<unsigned>(disconnect_reason),
                                              reason_name);
                    } else if (status == WL_CONNECT_FAILED ||
                        disconnect_reason == WIFI_REASON_AUTH_FAIL ||
                        disconnect_reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
                        disconnect_reason == WIFI_REASON_HANDSHAKE_TIMEOUT) {
                        lv_label_set_text_fmt(wifi_status_message_lbl, "Status: Blad WPA (%u %s)",
                                              static_cast<unsigned>(disconnect_reason),
                                              reason_name);
                    } else if (status == WL_NO_SSID_AVAIL || disconnect_reason == WIFI_REASON_NO_AP_FOUND) {
                        lv_label_set_text_fmt(wifi_status_message_lbl, "Status: Siec niedostepna (%u %s)",
                                              static_cast<unsigned>(disconnect_reason),
                                              reason_name);
                    } else {
                        lv_label_set_text_fmt(wifi_status_message_lbl, "Status: Timeout (%u %s)",
                                              static_cast<unsigned>(disconnect_reason),
                                              reason_name);
                    }
                    lv_obj_set_style_text_color(wifi_status_message_lbl, lv_color_make(239, 68, 68), 0);
                }
                if (btn_disconnect != nullptr) {
                    lv_obj_add_flag(btn_disconnect, LV_OBJ_FLAG_HIDDEN);
                }
                if (btn_sta != nullptr) {
                    lv_obj_clear_flag(btn_sta, LV_OBJ_FLAG_HIDDEN);
                }
                if (btn_ota != nullptr) {
                    lv_obj_clear_flag(btn_ota, LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
    } else if (wifi_connected && !wifi_ota_active) {
        // Periodically monitor connection stability
        if (WiFi.status() != WL_CONNECTED) {
            wifi_connected = false;
            wifi_rssi = 0;
            stop_ota_portal();
            gui_app_update_wifi(0, 0);
            if (wifi_status_message_lbl != nullptr) {
                lv_label_set_text(wifi_status_message_lbl, "Status: Rozlaczono");
                lv_obj_set_style_text_color(wifi_status_message_lbl, lv_color_make(239, 68, 68), 0);
            }
            if (btn_disconnect != nullptr) {
                lv_obj_add_flag(btn_disconnect, LV_OBJ_FLAG_HIDDEN);
            }
            if (btn_sta != nullptr) {
                lv_obj_clear_flag(btn_sta, LV_OBJ_FLAG_HIDDEN);
            }
            if (btn_ota != nullptr) {
                lv_obj_clear_flag(btn_ota, LV_OBJ_FLAG_HIDDEN);
            }
        } else {
            // Update RSSI periodically
            int current_rssi = WiFi.RSSI();
            if (current_rssi != wifi_rssi) {
                wifi_rssi = current_rssi;
                gui_app_update_wifi(1, wifi_rssi);
            }
        }
    }
}

static void btn_wifi_disc_handler(lv_event_t *e) {
    LV_UNUSED(e);
    play_system_sound(SoundType::Click);

    WiFi.disconnect(true);
    wifi_connected = false;
    wifi_rssi = 0;
    stop_ota_portal();

    gui_app_update_wifi(0, 0);

    if (wifi_status_message_lbl != nullptr) {
        lv_label_set_text(wifi_status_message_lbl, "Status: Rozlaczono");
        lv_obj_set_style_text_color(wifi_status_message_lbl, lv_color_make(239, 68, 68), 0);
    }
    if (wifi_ssid_lbl != nullptr) {
        lv_label_set_text(wifi_ssid_lbl, LV_SYMBOL_WIFI "  SSID: Rozlaczono");
    }
    if (wifi_ip_lbl != nullptr) {
        lv_label_set_text(wifi_ip_lbl, LV_SYMBOL_RIGHT "  IP: 0.0.0.0");
    }
    if (btn_disconnect != nullptr) {
        lv_obj_add_flag(btn_disconnect, LV_OBJ_FLAG_HIDDEN);
    }
    if (btn_sta != nullptr) {
        lv_obj_clear_flag(btn_sta, LV_OBJ_FLAG_HIDDEN);
    }
    if (btn_ota != nullptr) {
        lv_obj_clear_flag(btn_ota, LV_OBJ_FLAG_HIDDEN);
    }
}

static void prepare_wifi_sta_radio() {
    WiFi.persistent(false);
    WiFi.scanDelete();

    const wifi_mode_t mode = WiFi.getMode();
    if (mode == WIFI_AP || mode == WIFI_AP_STA) {
        WiFi.softAPdisconnect(true);
    }

    WiFi.mode(WIFI_STA);
    vTaskDelay(pdMS_TO_TICKS(50U));
    WiFi.disconnect(false, false);
    vTaskDelay(pdMS_TO_TICKS(50U));
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(false);
    WiFi.setHostname(Secrets::OTA_HOSTNAME);
    WiFi.setMinSecurity(WIFI_AUTH_WPA_PSK);
    WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
    WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);

    const esp_err_t ps_result = esp_wifi_set_ps(WIFI_PS_NONE);
    if (ps_result != ESP_OK) {
        Serial.printf("WIFI_STA: modem-sleep disable failed: %d\n", static_cast<int>(ps_result));
    }
}

static void register_wifi_event_handlers() {
    if (wifi_events_registered) {
        return;
    }

    WiFi.onEvent(
        [](WiFiEvent_t event, WiFiEventInfo_t info) {
            LV_UNUSED(event);
            wifi_last_disconnect_reason = info.wifi_sta_disconnected.reason;
            wifi_last_disconnect_ms = millis();
            Serial.printf("WIFI_STA: disconnected reason=%u\n",
                          static_cast<unsigned>(wifi_last_disconnect_reason));
        },
        ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    wifi_events_registered = true;
}

static const char *wifi_disconnect_reason_name(uint8_t reason) {
    if (reason == 0U) {
        return "brak";
    }
    const char *name = WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(reason));
    return (name != nullptr && name[0] != '\0') ? name : "UNKNOWN";
}

static bool wifi_disconnect_reason_is_ap_capacity(uint8_t reason) {
    return wifi_retry_policy.is_capacity_rejection(reason);
}

static void btn_sta_handler(lv_event_t *e) {
    LV_UNUSED(e);
    play_system_sound(SoundType::Click);
    if (gui_web_focus_blocks_local_ui()) {
        gui_web_focus_show_wifi_main_panel();
        gui_web_focus_apply_wifi_controls(true);
        return;
    }

    is_connecting = false;
    wifi_connected = false;
    wifi_rssi = 0;
    wifi_last_disconnect_reason = 0;
    wifi_last_disconnect_ms = 0;
    clear_pending_wifi_password();

    if (wifi_main_panel != nullptr) lv_obj_add_flag(wifi_main_panel, LV_OBJ_FLAG_HIDDEN);
    if (wifi_sta_panel != nullptr) lv_obj_clear_flag(wifi_sta_panel, LV_OBJ_FLAG_HIDDEN);

    if (sta_list_obj != nullptr) {
        uint32_t cnt = lv_obj_get_child_cnt(sta_list_obj);
        for (uint32_t i = 0; i < cnt; i++) {
            void *ud = lv_obj_get_user_data(lv_obj_get_child(sta_list_obj, i));
            if (ud != nullptr) {
                free(ud);
            }
        }
        lv_obj_clean(sta_list_obj);
        lv_obj_t *scan_item = lv_list_add_btn(sta_list_obj, LV_SYMBOL_LOOP, "Skanowanie sieci...");
        style_wifi_list_item(scan_item, lv_color_make(14, 165, 233));
    }

    is_scanning = true;
    scan_started = false;
    wifi_scan_prepare_pending = true;
}

static void btn_ota_handler(lv_event_t *e) {
    LV_UNUSED(e);
    if (gui_web_focus_blocks_local_ui()) {
        gui_web_focus_show_wifi_main_panel();
        gui_web_focus_apply_wifi_controls(true);
        return;
    }
    pin_guard_execute_or_prompt(PinAction::StartOta, 0, false);
}

static void start_ota_authorized() {
    play_system_sound(SoundType::Warning);
    if (ota_start_pending || wifi_ota_active) {
        return;
    }
    ota_start_pending = true;
    ota_portal_set_status("OTA: oczekiwanie na zadanie sieciowe...",
                          lv_color_make(245, 158, 11));
}

static void start_ota_background() {
    const char *ota_password = device_credentials_ota_ap_password();
    if (ota_password == nullptr || strlen(ota_password) < 8U) {
        wifi_ota_active = false;
        gui_app_update_wifi(0, 0);
        ota_portal_set_status("Status: Blad OTA - haslo AP min. 8 znakow", lv_color_make(239, 68, 68));
        Serial.println("OTA: SoftAP password too short. ESP32 requires at least 8 characters.");
        return;
    }

    is_scanning = false;
    scan_started = false;
    is_connecting = false;
    wifi_connected = false;
    wifi_rssi = 0;
    stop_ota_portal();
    WiFi.scanDelete();

    gui_app_update_wifi(0, 0);
    ota_portal_set_status("OTA: uruchamiam punkt dostepowy...", lv_color_make(245, 158, 11));

    WiFi.persistent(false);
    WiFi.disconnect(true, true);
    vTaskDelay(pdMS_TO_TICKS(120U));
    WiFi.mode(WIFI_OFF);
    vTaskDelay(pdMS_TO_TICKS(80U));
    WiFi.mode(WIFI_AP);
    WiFi.setSleep(false);
    const bool config_ok = WiFi.softAPConfig(ota_portal_ip, ota_portal_gateway, ota_portal_subnet);
    if (!config_ok) {
        Serial.println("OTA: SoftAP IP configuration failed, continuing with default AP config.");
    }
    vTaskDelay(pdMS_TO_TICKS(100U));
    bool ap_ok = WiFi.softAP(Secrets::OTA_AP_SSID, ota_password);
    vTaskDelay(pdMS_TO_TICKS(100U));
    Serial.printf("OTA: SoftAP start: %s, SSID: %s, IP: %s\n", 
                  ap_ok ? "OK" : "FAILED", 
                  WiFi.softAPSSID().c_str(), 
                  WiFi.softAPIP().toString().c_str());

#if AQUARIUM_ALLOW_UNSIGNED_ARDUINO_OTA
    ArduinoOTA.setHostname(Secrets::OTA_HOSTNAME);
    ArduinoOTA.setPassword(ota_password);
    ArduinoOTA.onStart([]() {
        Serial.println(
            "OTA_DEV: unsigned ArduinoOTA started; never enable this profile in production.");
        backup_configuration_for_ota();
        arduino_ota_service_mode_owned =
            control_modes.start_service(
                aquarium::ControlModeManager::kServiceMaxSeconds,
                millis()) == aquarium::ControlModeResult::Applied;
        force_safe_service_outputs();
        if (wifi_status_message_lbl != nullptr) {
            lv_label_set_text(wifi_status_message_lbl, "OTA: Trwa flashowanie...");
            lv_obj_set_style_text_color(wifi_status_message_lbl, lv_color_make(6, 182, 212), 0);
        }
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("\nOTA: Gotowe!");
        runtime_safety_record_restart(
            RuntimeFaultReason::OtaUpdate,
            hal_mcp_latch_all_relays_safe());
        if (wifi_status_message_lbl != nullptr) {
            lv_label_set_text(wifi_status_message_lbl, "OTA: Gotowe! Restart...");
            lv_obj_set_style_text_color(wifi_status_message_lbl, lv_color_make(16, 185, 129), 0);
        }
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        const unsigned int percent = total > 0U ? static_cast<unsigned int>((progress * 100ULL) / total) : 0U;
        Serial.printf("OTA: Postep: %u%%\r", percent);
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("OTA Blad[%u]\n", error);
        if (arduino_ota_service_mode_owned) {
            control_modes.stop_service();
            arduino_ota_service_mode_owned = false;
        }
        if (wifi_status_message_lbl != nullptr) {
            lv_label_set_text_fmt(wifi_status_message_lbl, "OTA Blad: %u", error);
            lv_obj_set_style_text_color(wifi_status_message_lbl, lv_color_make(239, 68, 68), 0);
        }
    });
#endif

    if (!ap_ok) {
        wifi_ota_active = false;
#if AQUARIUM_ALLOW_UNSIGNED_ARDUINO_OTA
        ArduinoOTA.end();
#endif
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_OFF);
        gui_app_update_wifi(0, 0);
        ota_portal_set_status("Status: Blad startu AP OTA", lv_color_make(239, 68, 68));
        return;
    }

#if AQUARIUM_ALLOW_UNSIGNED_ARDUINO_OTA
    ArduinoOTA.begin();
#else
    Serial.println(
        "OTA: raw ArduinoOTA disabled; only signed .aqfw packages are accepted.");
#endif
    start_ota_portal();
    wifi_ota_active = true;
    gui_app_update_wifi(2, 0);
    ota_portal_set_status("OTA: AP aktywny, portal gotowy", lv_color_make(16, 185, 129));

    if (wifi_main_panel != nullptr) lv_obj_add_flag(wifi_main_panel, LV_OBJ_FLAG_HIDDEN);
    if (wifi_ota_panel != nullptr) lv_obj_clear_flag(wifi_ota_panel, LV_OBJ_FLAG_HIDDEN);
}

static void stop_ota_runtime(bool play_sound) {
    if (play_sound) {
        play_system_sound(SoundType::Click);
    }
#if AQUARIUM_ALLOW_UNSIGNED_ARDUINO_OTA
    ArduinoOTA.end();
#endif
    stop_ota_portal();
    WiFi.softAPdisconnect(true);
    wifi_ota_active = false;

    if (WiFi.status() == WL_CONNECTED) {
        wifi_connected = true;
        wifi_rssi = WiFi.RSSI();
        WiFi.mode(WIFI_STA);
        gui_app_update_wifi(1, wifi_rssi);
        if (wifi_mac_lbl != nullptr) {
            lv_label_set_text(wifi_mac_lbl, "Portal: zatrzymany");
        }
    } else {
        wifi_connected = false;
        wifi_rssi = 0;
        WiFi.mode(WIFI_OFF);
        gui_app_update_wifi(0, 0);
        if (wifi_status_message_lbl != nullptr) {
            lv_label_set_text(wifi_status_message_lbl, "Status: Rozlaczono");
            lv_obj_set_style_text_color(wifi_status_message_lbl, lv_color_make(239, 68, 68), 0);
        }
    }

    if (wifi_ota_panel != nullptr) lv_obj_add_flag(wifi_ota_panel, LV_OBJ_FLAG_HIDDEN);
    if (wifi_main_panel != nullptr) lv_obj_clear_flag(wifi_main_panel, LV_OBJ_FLAG_HIDDEN);
}

static void stop_ota_cb(lv_event_t *e) {
    LV_UNUSED(e);
    stop_ota_runtime(true);
}

static void cancel_sta_cb(lv_event_t *e) {
    LV_UNUSED(e);
    play_system_sound(SoundType::Click);

    is_scanning = false;
    wifi_scan_prepare_pending = false;
    WiFi.scanDelete();

    if (sta_list_obj != nullptr) {
        uint32_t cnt = lv_obj_get_child_cnt(sta_list_obj);
        for (uint32_t i = 0; i < cnt; i++) {
            void *ud = lv_obj_get_user_data(lv_obj_get_child(sta_list_obj, i));
            if (ud != nullptr) {
                free(ud);
            }
        }
        lv_obj_clean(sta_list_obj);
    }

    if (wifi_sta_panel != nullptr) lv_obj_add_flag(wifi_sta_panel, LV_OBJ_FLAG_HIDDEN);
    if (wifi_main_panel != nullptr) lv_obj_clear_flag(wifi_main_panel, LV_OBJ_FLAG_HIDDEN);
}

static void select_network_cb(lv_event_t *e) {
    play_system_sound(SoundType::Click);
    lv_obj_t *btn = lv_event_get_target(e);

    char *ssid_ptr = (char *)lv_obj_get_user_data(btn);
    if (ssid_ptr != nullptr) {
        strncpy(selected_ssid, ssid_ptr, sizeof(selected_ssid) - 1);
        selected_ssid[sizeof(selected_ssid) - 1] = '\0';
    } else {
        lv_obj_t *list = lv_obj_get_parent(btn);
        const char *text = lv_list_get_btn_text(list, btn);
        if (text != nullptr) {
            strncpy(selected_ssid, text, sizeof(selected_ssid) - 1);
            selected_ssid[sizeof(selected_ssid) - 1] = '\0';
            char *paren = strrchr(selected_ssid, '(');
            if (paren != nullptr && paren > selected_ssid) {
                *(paren - 1) = '\0';
            }
        }
    }

    if (wifi_pwd_title_lbl != nullptr) {
        lv_label_set_text_fmt(wifi_pwd_title_lbl, "Haslo do: %s", selected_ssid);
    }

    if (wifi_pwd_ta != nullptr) {
        lv_textarea_set_text(wifi_pwd_ta, "");
    }
    clear_pending_wifi_password();

    if (wifi_sta_panel != nullptr) lv_obj_add_flag(wifi_sta_panel, LV_OBJ_FLAG_HIDDEN);
    if (wifi_pwd_panel != nullptr) lv_obj_clear_flag(wifi_pwd_panel, LV_OBJ_FLAG_HIDDEN);
}

static void cancel_pwd_cb(lv_event_t *e) {
    LV_UNUSED(e);
    play_system_sound(SoundType::Click);
    if (wifi_pwd_panel != nullptr) lv_obj_add_flag(wifi_pwd_panel, LV_OBJ_FLAG_HIDDEN);
    if (wifi_sta_panel != nullptr) lv_obj_clear_flag(wifi_sta_panel, LV_OBJ_FLAG_HIDDEN);
}

static bool begin_sta_connection(const char *ssid, const char *password) {
    if (ssid == nullptr || ssid[0] == '\0') {
        Serial.println("WIFI_STA: empty SSID, connection not started.");
        return false;
    }
    if (xPortGetCoreID() != 0) {
        wifi_connect_pending = true;
        is_connecting = true;
        wifi_connected = false;
        wifi_rssi = 0;
        return true;
    }

    WiFi.scanDelete();
    is_scanning = false;
    scan_started = false;

    if (wifi_ota_active) {
        stop_ota_runtime(false);
    } else {
        stop_ota_portal();
    }

    prepare_wifi_sta_radio();
    wifi_last_disconnect_reason = 0;
    wifi_last_disconnect_ms = 0;

    wl_status_t begin_status = WiFi.begin(ssid, password != nullptr ? password : "");
    WiFi.setAutoReconnect(true);
    is_connecting = true;
    wifi_connected = false;
    wifi_rssi = 0;
    conn_start_ms = millis();
    Serial.printf("WIFI_STA: begin SSID=%s status=%d\n", ssid, static_cast<int>(begin_status));
    return begin_status != WL_CONNECT_FAILED;
}

static void keyboard_ready_cb(lv_event_t *e) {
    LV_UNUSED(e);
    play_system_sound(SoundType::Click);

    const char *pwd = "";
    if (wifi_pwd_ta != nullptr) {
        pwd = lv_textarea_get_text(wifi_pwd_ta);
    }
    strncpy(pending_wifi_password, pwd != nullptr ? pwd : "", sizeof(pending_wifi_password) - 1);
    pending_wifi_password[sizeof(pending_wifi_password) - 1] = '\0';
    pending_wifi_password_valid = selected_ssid[0] != '\0';

    if (wifi_pwd_panel != nullptr) lv_obj_add_flag(wifi_pwd_panel, LV_OBJ_FLAG_HIDDEN);
    if (wifi_main_panel != nullptr) lv_obj_clear_flag(wifi_main_panel, LV_OBJ_FLAG_HIDDEN);

    if (wifi_status_message_lbl != nullptr) {
        lv_label_set_text(wifi_status_message_lbl, "Status: Laczenie...");
        lv_obj_set_style_text_color(wifi_status_message_lbl, lv_color_make(245, 158, 11), 0);
    }

    if (!begin_sta_connection(selected_ssid, pending_wifi_password)) {
        is_connecting = false;
        clear_pending_wifi_password();
        if (wifi_status_message_lbl != nullptr) {
            lv_label_set_text(wifi_status_message_lbl, "Status: nie uruchomiono WiFi");
            lv_obj_set_style_text_color(wifi_status_message_lbl, lv_color_make(239, 68, 68), 0);
        }
    }
}


static void ntp_sync_restore_cb(lv_timer_t *timer) {
    lv_obj_t *btn = timer != nullptr ? static_cast<lv_obj_t *>(timer->user_data) : nullptr;
    if (btn != nullptr) {
        lv_obj_set_style_bg_color(btn, resolve_bg_color(lv_color_make(35, 41, 55)), 0);
    }
    if (btn_sync_ntp_lbl_global != nullptr) {
        lv_label_set_text(btn_sync_ntp_lbl_global, "Synchronizuj NTP");
    }
    if (timer != nullptr) {
        lv_timer_del(timer);
    }
}

static void btn_sync_ntp_handler(lv_event_t *e) {
    play_system_sound(SoundType::Click);
    lv_obj_t *btn = lv_event_get_target(e);
    if (!wifi_connected) {
        lv_obj_set_style_bg_color(btn, lv_color_make(239, 68, 68), 0);
        set_label_text(btn_sync_ntp_lbl_global, "Brak WiFi");
        lv_timer_create(ntp_sync_restore_cb, 2000, btn);
        return;
    }

    set_label_text(btn_sync_ntp_lbl_global, "Pobieram czas...");
    const bool accepted = sync_clock_from_ntp(5000U);
    lv_obj_set_style_bg_color(
        btn,
        accepted ? lv_color_make(245, 158, 11) : lv_color_make(239, 68, 68),
        0);
    if (!accepted) {
        set_label_text(btn_sync_ntp_lbl_global, "Blad NTP");
        lv_timer_create(ntp_sync_restore_cb, 2000, btn);
    }
}

static void adjust_clock_cb(lv_event_t *e) {
    play_system_sound(SoundType::Click);
    const int val = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    if (val == 1) { // H+
        clock_hour = (clock_hour + 1) % 24;
    } else if (val == -1) { // H-
        clock_hour = (clock_hour == 0) ? 23 : clock_hour - 1;
    } else if (val == 2) { // M+
        clock_minute = (clock_minute + 1) % 60;
    } else if (val == -2) { // M-
        clock_minute = (clock_minute == 0) ? 59 : clock_minute - 1;
    } else if (val == 3) { // D+
        int days_in_month = 31;
        if (clock_month == 4 || clock_month == 6 || clock_month == 9 || clock_month == 11) {
            days_in_month = 30;
        } else if (clock_month == 2) {
            bool is_leap = (clock_year % 4 == 0 && (clock_year % 100 != 0 || clock_year % 400 == 0));
            days_in_month = is_leap ? 29 : 28;
        }
        clock_day = (clock_day % days_in_month) + 1;
    } else if (val == -3) { // D-
        int days_in_month = 31;
        if (clock_month == 4 || clock_month == 6 || clock_month == 9 || clock_month == 11) {
            days_in_month = 30;
        } else if (clock_month == 2) {
            bool is_leap = (clock_year % 4 == 0 && (clock_year % 100 != 0 || clock_year % 400 == 0));
            days_in_month = is_leap ? 29 : 28;
        }
        clock_day = (clock_day == 1) ? days_in_month : clock_day - 1;
    } else if (val == 4) { // Month+
        clock_month = (clock_month % 12) + 1;
        int days_in_month = 31;
        if (clock_month == 4 || clock_month == 6 || clock_month == 9 || clock_month == 11) {
            days_in_month = 30;
        } else if (clock_month == 2) {
            bool is_leap = (clock_year % 4 == 0 && (clock_year % 100 != 0 || clock_year % 400 == 0));
            days_in_month = is_leap ? 29 : 28;
        }
        if (clock_day > days_in_month) {
            clock_day = days_in_month;
        }
    } else if (val == -4) { // Month-
        clock_month = (clock_month == 1) ? 12 : clock_month - 1;
        int days_in_month = 31;
        if (clock_month == 4 || clock_month == 6 || clock_month == 9 || clock_month == 11) {
            days_in_month = 30;
        } else if (clock_month == 2) {
            bool is_leap = (clock_year % 4 == 0 && (clock_year % 100 != 0 || clock_year % 400 == 0));
            days_in_month = is_leap ? 29 : 28;
        }
        if (clock_day > days_in_month) {
            clock_day = days_in_month;
        }
    } else if (val == 5) { // Year+
        clock_year++;
    } else if (val == -5) { // Year-
        if (clock_year > 2000) clock_year--;
    } else if (val == 6) { // S+
        clock_second = (clock_second + 1) % 60;
    } else if (val == -6) { // S-
        clock_second = (clock_second == 0) ? 59 : clock_second - 1;
    }
}

static void save_clock_settings_cb(lv_event_t *e) {
    LV_UNUSED(e);
    play_system_sound(SoundType::Click);
    const bool saved = gui_save_clock_settings(true, "manual");
    if (saved && is_clock_changed()) {
        show_save_toast("Zapisano czas");
        capture_clock_snapshot();
    } else if (!saved) {
        show_top_notification("Nie zapisano czasu", false);
    }
    delete_runtime_subpages(true);
    Serial.printf("System: Clock manually set to: %02d:%02d:%02d on %02d/%02d/%04d\n",
                  clock_hour, clock_minute, clock_second, clock_day, clock_month, clock_year);
}

static void diag_dev_mode_handler(lv_event_t *e) {
    play_system_sound(SoundType::Click);
    lv_obj_t *obj = lv_event_get_target(e);
    const bool requested = lv_obj_has_state(obj, LV_STATE_CHECKED);
    apply_dev_mode_authorized(requested);
}

static void apply_dev_mode_authorized(bool enabled) {
    cfg.devMode = FORCE_DEVELOPER_MODE ? true : enabled;
    gui_app_save_settings();
    if (FORCE_DEVELOPER_MODE && !enabled) {
        add_gui_log("Tryb deweloperski wymuszony przez firmware", true);
    } else if (cfg.devMode) {
        add_gui_log("Tryb deweloperski wlaczony", false);
    } else {
        add_gui_log("Tryb deweloperski wylaczony", true);
    }
    gui_sync_widgets_to_state();
}

static void screen_always_on_handler(lv_event_t *e) {
    play_system_sound(SoundType::Click);
    lv_obj_t *obj = lv_event_get_target(e);
    display_power_profile = lv_obj_has_state(obj, LV_STATE_CHECKED)
                                ? DisplayPowerProfile::AlwaysOn
                                : DisplayPowerProfile::Timeout60Seconds;
    gui_app_save_settings();
    apply_display_backlight(last_ldr_value, last_ldr_valid);
}

static void screen_ldr_enable_handler(lv_event_t *e) {
    play_system_sound(SoundType::Click);
    lv_obj_t *obj = lv_event_get_target(e);
    cfg.ldrThemeEnabled = lv_obj_has_state(obj, LV_STATE_CHECKED);
    
    if (cfg.ldrThemeEnabled && !cfg.devMode) {
        const int ldr_val = analogRead(HwConfig::LDR_PIN);
        bool should_be_light = ui_light_theme;
        last_ldr_value = ldr_val;
        last_ldr_valid = true;
        
        if (ldr_value_to_light_theme(ldr_val, &should_be_light) &&
            should_be_light != ui_light_theme) {
            ui_light_theme = should_be_light;
            rebuild_gui_tree_for_theme();
        }
    } else {
        last_ldr_valid = false;
        if (ui_light_theme != cfg.manualLightTheme) {
            ui_light_theme = cfg.manualLightTheme;
            rebuild_gui_tree_for_theme();
        }
    }
    
    gui_sync_widgets_to_state();
    gui_app_save_settings();
}

static void screen_manual_theme_handler(lv_event_t *e) {
    play_system_sound(SoundType::Click);
    lv_obj_t *obj = lv_event_get_target(e);
    cfg.manualLightTheme = lv_obj_has_state(obj, LV_STATE_CHECKED);
    if (!cfg.ldrThemeEnabled) {
        ui_light_theme = cfg.manualLightTheme;
        rebuild_gui_tree_for_theme();
    }
    gui_app_save_settings();
}

static void screen_ph_enable_handler(lv_event_t *e) {
    play_system_sound(SoundType::Click);
    lv_obj_t *obj = lv_event_get_target(e);
    const bool requested = lv_obj_has_state(obj, LV_STATE_CHECKED);
    if (!pin_guard_execute_or_prompt(PinAction::TogglePhSensor, 0, requested)) {
        set_checked(obj, cfg.showPhSensor);
    }
}

static void apply_ph_sensor_authorized(bool enabled) {
    cfg.showPhSensor = enabled;
    request_gui_rebuild_async();
    gui_app_save_settings();
}

static void save_screen_settings_cb(lv_event_t *e) {
    LV_UNUSED(e);
    play_system_sound(SoundType::Click);
    if (is_screen_changed()) {
        sanitize_config(cfg);
        gui_app_save_settings();
        show_save_toast("Zapisano ekran");
        capture_screen_snapshot();
        Serial.println("GUI: Screen settings saved.");
    } else {
        Serial.println("GUI: Screen settings unchanged.");
    }
}

static void append_gui_log_entry(GuiLogEntry *entries, uint8_t &count, const char *msg, bool critical) {
    if (entries == nullptr || msg == nullptr) {
        return;
    }
    if (count < GUI_LOG_CAPACITY) {
        ++count;
    } else {
        memmove(&entries[0], &entries[1], sizeof(GuiLogEntry) * (GUI_LOG_CAPACITY - 1U));
    }

    GuiLogEntry &entry = entries[count - 1U];
    entry.ts = controller_clock_reliable ? controller_unix_time() : 0U;
    entry.critical = critical;
    snprintf(entry.message, sizeof(entry.message), "%s", msg);
}

static void add_gui_log(const char *msg, bool is_important) {
    Serial.printf("LOG_GUI [%s]: %s\n", is_important ? "IMPORTANT" : "NORMAL", msg);
    if (is_important) {
        append_gui_log_entry(gui_logs_important, gui_logs_important_count, msg != nullptr ? msg : "", true);
    } else {
        append_gui_log_entry(gui_logs_normal, gui_logs_normal_count, msg != nullptr ? msg : "", false);
    }
    lv_obj_t *target_list = is_important ? log_list_important : log_list_normal;
    if (target_list != nullptr) {
        uint32_t cnt = lv_obj_get_child_cnt(target_list);
        if (cnt >= 15) {
            lv_obj_t *oldest = lv_obj_get_child(target_list, 0);
            if (oldest != nullptr) {
                lv_obj_del(oldest);
            }
        }
        const char *icon = is_important ? LV_SYMBOL_WARNING : LV_SYMBOL_LIST;
        lv_obj_t *list_btn = lv_list_add_btn(target_list, icon, msg);
        lv_obj_set_style_text_font(list_btn, &lv_font_montserrat_12, 0);
        if (is_important) {
            lv_obj_set_style_text_color(list_btn, lv_color_make(239, 68, 68), 0);
        } else {
            lv_obj_set_style_text_color(list_btn, theme_text_main(), 0);
        }
    }
}

static void btn_log_normal_cb(lv_event_t *e) {
    LV_UNUSED(e);
    play_system_sound(SoundType::Click);
    showing_important_logs = false;
    if (btn_log_normal != nullptr) {
        lv_obj_set_style_bg_color(btn_log_normal, lv_color_make(59, 130, 246), 0);
    }
    if (btn_log_important != nullptr) {
        lv_obj_set_style_bg_color(btn_log_important, resolve_bg_color(lv_color_make(35, 41, 55)), 0);
    }
    if (log_list_normal != nullptr) {
        lv_obj_clear_flag(log_list_normal, LV_OBJ_FLAG_HIDDEN);
    }
    if (log_list_important != nullptr) {
        lv_obj_add_flag(log_list_important, LV_OBJ_FLAG_HIDDEN);
    }
}

static void btn_log_important_cb(lv_event_t *e) {
    LV_UNUSED(e);
    play_system_sound(SoundType::Click);
    showing_important_logs = true;
    if (btn_log_normal != nullptr) {
        lv_obj_set_style_bg_color(btn_log_normal, resolve_bg_color(lv_color_make(35, 41, 55)), 0);
    }
    if (btn_log_important != nullptr) {
        lv_obj_set_style_bg_color(btn_log_important, lv_color_make(239, 68, 68), 0);
    }
    if (log_list_normal != nullptr) {
        lv_obj_add_flag(log_list_normal, LV_OBJ_FLAG_HIDDEN);
    }
    if (log_list_important != nullptr) {
        lv_obj_clear_flag(log_list_important, LV_OBJ_FLAG_HIDDEN);
    }
}

static void clear_logs_cb(lv_event_t *e) {
    LV_UNUSED(e);
    play_system_sound(SoundType::Click);
    if (log_list_normal != nullptr) {
        lv_obj_clean(log_list_normal);
        lv_list_add_btn(log_list_normal, LV_SYMBOL_LIST, "Brak logow");
    }
    if (log_list_important != nullptr) {
        lv_obj_clean(log_list_important);
        lv_list_add_btn(log_list_important, LV_SYMBOL_WARNING, "Brak waznych logow");
    }
}

static void sound_enable_handler(lv_event_t *e) {
    play_system_sound(SoundType::Click);
    lv_obj_t *obj = lv_event_get_target(e);
    cfg.soundEnabled = lv_obj_has_state(obj, LV_STATE_CHECKED);
}

static void sound_quiet_enable_handler(lv_event_t *e) {
    play_system_sound(SoundType::Click);
    lv_obj_t *obj = lv_event_get_target(e);
    cfg.quietHoursEnabled = lv_obj_has_state(obj, LV_STATE_CHECKED);
}

// adjust_quiet_hours_cb is deleted since we use standard schedule editor

static void save_sound_settings_cb(lv_event_t *e) {
    LV_UNUSED(e);
    play_system_sound(SoundType::Click);
    if (is_sound_changed()) {
        sanitize_config(cfg);
        gui_app_save_settings();
        show_save_toast("Zapisano dzwiek");
        capture_sound_snapshot();
        Serial.println("GUI: Sound settings saved.");
    } else {
        Serial.println("GUI: Sound settings unchanged.");
    }
    delete_runtime_subpages(true);
}

static void test_speaker_cb(lv_event_t *e) {
    LV_UNUSED(e);
    play_mario_tune();
}

static void back_service_cb(lv_event_t *e) {
    LV_UNUSED(e);
    play_system_sound(SoundType::Click);
    delete_runtime_subpages(true);
}

static void service_tile_cb(lv_event_t *e) {
    LV_UNUSED(e);
    log_subpage_enter_request(ActiveSubpage::Service, "service_tile");
    play_system_sound(SoundType::Click);

    // Silence active music
    musicPlaying = false;

    // Global override: shut down all systems except lights
    cfg.filterMode = static_cast<uint8_t>(ScheduleMode::AlwaysOff);
    cfg.airMode = static_cast<uint8_t>(ScheduleMode::AlwaysOff);
    cfg.heaterMode = static_cast<uint8_t>(HeaterMode::Off);
    cfg.feedEnabled = false;

    runtime.filterOn = false;
    runtime.airOn = false;
    runtime.heaterOn = false;

    // Save changes to NVS
    gui_app_save_settings();

    // Sync all widgets
    apply_mcp_outputs();
    gui_sync_widgets_to_state();

    open_or_build_subpage(ActiveSubpage::Service);
}

static void service_light_sw_cb(lv_event_t *e) {
    play_system_sound(SoundType::Click);
    lv_obj_t *sw = lv_event_get_target(e);
    bool checked = lv_obj_has_state(sw, LV_STATE_CHECKED);
    cfg.lightMode = checked ? static_cast<uint8_t>(ScheduleMode::AlwaysOn) : static_cast<uint8_t>(ScheduleMode::AlwaysOff);
    runtime.lightOn = checked;
    gui_app_save_settings();
    apply_mcp_outputs();
    gui_sync_widgets_to_state();
}

static void service_filter_sw_cb(lv_event_t *e) {
    play_system_sound(SoundType::Click);
    lv_obj_t *sw = lv_event_get_target(e);
    bool checked = lv_obj_has_state(sw, LV_STATE_CHECKED);
    cfg.filterMode = checked ? static_cast<uint8_t>(ScheduleMode::AlwaysOn) : static_cast<uint8_t>(ScheduleMode::AlwaysOff);
    runtime.filterOn = checked;
    gui_app_save_settings();
    apply_mcp_outputs();
    gui_sync_widgets_to_state();
}

static void service_song_dd_cb(lv_event_t *e) {
    play_system_sound(SoundType::Click);
    lv_obj_t *dd = lv_event_get_target(e);
    selectedSongIndex = lv_dropdown_get_selected(dd);
}

static void service_volume_slider_cb(lv_event_t *e) {
    lv_obj_t *slider = lv_event_get_target(e);
    musicVolume = lv_slider_get_value(slider);
    if (service_vol_lbl != nullptr) {
        lv_label_set_text_fmt(service_vol_lbl, "Vol: %d0%%", musicVolume.load());
    }
}

static void service_play_cb(lv_event_t *e) {
    LV_UNUSED(e);
    play_system_sound(SoundType::Click);
    musicPlaying = true;
}

static void service_stop_cb(lv_event_t *e) {
    LV_UNUSED(e);
    play_system_sound(SoundType::Click);
    musicPlaying = false;
}

static void open_heater_subpage_cb(lv_event_t *e) {
    LV_UNUSED(e);
    pin_guard_execute_or_prompt(PinAction::OpenHeater, 0, false);
}

static void open_heater_subpage_authorized() {
    log_subpage_enter_request(ActiveSubpage::Heater, "protected_tile");
    play_system_sound(SoundType::Click);
    capture_heater_snapshot();
    open_or_build_subpage(ActiveSubpage::Heater);
}

static void open_ph_subpage_cb(lv_event_t *e) {
    LV_UNUSED(e);
    pin_guard_execute_or_prompt(PinAction::OpenPh, 0, false);
}

static void open_ph_subpage_authorized() {
    log_subpage_enter_request(ActiveSubpage::Ph, "protected_tile");
    play_system_sound(SoundType::Click);
    capture_ph_snapshot();
    open_or_build_subpage(ActiveSubpage::Ph);
}

static void open_hardware_subpage_cb(lv_event_t *e) {
    LV_UNUSED(e);
    log_subpage_enter_request(ActiveSubpage::Hardware, "module_tile");
    play_system_sound(SoundType::Click);
    open_or_build_subpage(ActiveSubpage::Hardware);
}

static void open_co2_subpage_cb(lv_event_t *e) {
    LV_UNUSED(e);
    log_subpage_enter_request(ActiveSubpage::Co2, "module_tile");
    play_system_sound(SoundType::Click);
    open_or_build_subpage(ActiveSubpage::Co2);
}

static void open_ec_subpage_cb(lv_event_t *e) {
    LV_UNUSED(e);
    log_subpage_enter_request(ActiveSubpage::Ec, "module_tile");
    play_system_sound(SoundType::Click);
    open_or_build_subpage(ActiveSubpage::Ec);
}

static void open_water_subpage_cb(lv_event_t *e) {
    LV_UNUSED(e);
    log_subpage_enter_request(ActiveSubpage::WaterLevel, "module_tile");
    play_system_sound(SoundType::Click);
    open_or_build_subpage(ActiveSubpage::WaterLevel);
}

static void open_leak_subpage_cb(lv_event_t *e) {
    LV_UNUSED(e);
    log_subpage_enter_request(ActiveSubpage::Leak, "module_tile");
    play_system_sound(SoundType::Click);
    open_or_build_subpage(ActiveSubpage::Leak);
}

static void open_flow_subpage_cb(lv_event_t *e) {
    LV_UNUSED(e);
    log_subpage_enter_request(ActiveSubpage::Flow, "module_tile");
    play_system_sound(SoundType::Click);
    open_or_build_subpage(ActiveSubpage::Flow);
}

static lv_obj_t *create_menu_item(lv_obj_t *parent, const char *title,
                                  lv_event_cb_t event_cb, void *userData,
                                  lv_obj_t **title_label = nullptr) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 300, 34);
    lv_obj_set_ext_click_area(btn, 3);
    lv_obj_set_style_bg_color(btn, resolve_bg_color(lv_color_make(20, 26, 40)), 0);
    lv_obj_set_style_bg_color(btn, resolve_bg_color(lv_color_make(30, 38, 56)), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, ui_light_theme ? theme_card_border() : lv_color_make(35, 41, 55), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(btn, 0, 0);
    if (event_cb != nullptr) {
        lv_obj_add_event_cb(btn, event_cb, LV_EVENT_CLICKED, userData);
    }

    lv_obj_t *lbl = create_label(btn, title, lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 10, 0);
    if (title_label != nullptr) {
        *title_label = lbl;
    }

    lv_obj_t *chevron = create_label(btn, ">", lv_color_make(100, 116, 139), &lv_font_montserrat_12);
    lv_obj_align(chevron, LV_ALIGN_RIGHT_MID, -10, 0);
    return btn;
}

static lv_obj_t *create_subpage(const char *title, lv_event_cb_t back_cb = nullptr, void *back_user_data = nullptr) {
    lv_obj_t *sub = lv_obj_create(lv_scr_act());
    lv_obj_set_size(sub, 320, 240);
    lv_obj_set_pos(sub, 0, 0);
    lv_obj_add_flag(sub, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_pad_all(sub, 0, 0);
    style_panel(sub, lv_color_make(3, 7, 18), lv_color_make(3, 7, 18), 0);

    lv_obj_t *header = lv_obj_create(sub);
    lv_obj_set_size(header, 320, 30);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    style_panel(header, lv_color_make(20, 26, 40), lv_color_make(20, 26, 40), 0);

    lv_obj_t *back = create_button(header, LV_SYMBOL_LEFT, 32, 22, lv_color_make(30, 41, 59), nullptr, nullptr);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 6, 0);
    if (back_cb != nullptr) {
        lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, back_user_data);
    } else {
        lv_obj_add_event_cb(back, [](lv_event_t *e) {
            LV_UNUSED(e);
            play_system_sound(SoundType::Click);
            if (gui_web_focus_blocks_local_ui()) {
                gui_web_focus_show_wifi_main_panel();
                gui_web_focus_apply_wifi_controls(true);
                return;
            }
            delete_runtime_subpages(true);
        }, LV_EVENT_CLICKED, sub);
    }

    lv_obj_t *title_lbl = create_label(header, title, lv_color_white(), &lv_font_montserrat_14);
    lv_obj_align(title_lbl, LV_ALIGN_CENTER, 0, 0);
    return sub;
}

static void add_page_base(uint8_t index) {
    pages[index] = lv_obj_create(lv_scr_act());
    lv_obj_set_size(pages[index], 320, 180);
    lv_obj_set_pos(pages[index], 0, 25);
    lv_obj_set_style_pad_all(pages[index], index == 0 ? 0 : 4, 0);
    style_panel(pages[index], lv_color_make(3, 7, 18), lv_color_make(3, 7, 18), 0);
}

static void build_status_bar() {
    lv_obj_t *status_bar = lv_obj_create(lv_scr_act());
    lv_obj_set_size(status_bar, 320, 25);
    lv_obj_set_pos(status_bar, 0, 0);
    lv_obj_set_style_pad_all(status_bar, 0, 0);
    style_panel(status_bar, lv_color_make(8, 13, 24), lv_color_make(6, 182, 212), 0);
    lv_obj_set_style_border_side(status_bar, LV_BORDER_SIDE_BOTTOM, 0);

    lv_obj_t *brand = create_label(status_bar, "AQ", lv_color_make(6, 182, 212), &lv_font_montserrat_12);
    lv_obj_set_width(brand, 24);
    lv_obj_set_style_text_align(brand, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(brand, LV_ALIGN_LEFT_MID, 6, 0);

    label_power_mode = create_label(status_bar, "T --.-*C", lv_color_make(56, 189, 248), &lv_font_montserrat_12);
    lv_obj_set_width(label_power_mode, 62);
    lv_label_set_long_mode(label_power_mode, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(label_power_mode, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(label_power_mode, resolve_bg_color(lv_color_make(15, 23, 42)), 0);
    lv_obj_set_style_bg_opa(label_power_mode, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(label_power_mode, 4, 0);
    lv_obj_set_style_pad_top(label_power_mode, 2, 0);
    lv_obj_set_style_pad_bottom(label_power_mode, 2, 0);
    lv_obj_align(label_power_mode, LV_ALIGN_LEFT_MID, 34, 0);

    label_date = create_label(status_bar, "--:-- .--- --s", lv_color_make(226, 232, 240), &lv_font_montserrat_12);
    lv_obj_set_width(label_date, 116);
    lv_label_set_long_mode(label_date, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(label_date, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label_date, LV_ALIGN_CENTER, 0, 0);

    label_wifi_state = create_label(status_bar, "OFF", lv_color_make(148, 163, 184), &lv_font_montserrat_12);
    lv_obj_set_width(label_wifi_state, 56);
    lv_label_set_long_mode(label_wifi_state, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(label_wifi_state, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(label_wifi_state, resolve_bg_color(lv_color_make(15, 23, 42)), 0);
    lv_obj_set_style_bg_opa(label_wifi_state, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(label_wifi_state, 4, 0);
    lv_obj_set_style_pad_top(label_wifi_state, 2, 0);
    lv_obj_set_style_pad_bottom(label_wifi_state, 2, 0);
    lv_obj_align(label_wifi_state, LV_ALIGN_RIGHT_MID, -42, 0);

    label_rtc_bat = create_label(status_bar, "--", lv_color_make(245, 158, 11), &lv_font_montserrat_12);
    lv_obj_set_width(label_rtc_bat, 34);
    lv_label_set_long_mode(label_rtc_bat, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(label_rtc_bat, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(label_rtc_bat, resolve_bg_color(lv_color_make(6, 78, 59)), 0);
    lv_obj_set_style_bg_opa(label_rtc_bat, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(label_rtc_bat, 4, 0);
    lv_obj_set_style_pad_top(label_rtc_bat, 2, 0);
    lv_obj_set_style_pad_bottom(label_rtc_bat, 2, 0);
    lv_obj_align(label_rtc_bat, LV_ALIGN_RIGHT_MID, -4, 0);
}

static lv_obj_t *create_accent_bar(lv_obj_t *parent, lv_color_t color, lv_coord_t h) {
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, 3, h);
    lv_obj_align(bar, LV_ALIGN_LEFT_MID, -6, 0);
    lv_obj_set_style_bg_color(bar, color, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 2, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    return bar;
}

static void make_home_card_clickable(lv_obj_t *card, lv_event_cb_t cb, void *user_data) {
    if (card == nullptr || cb == nullptr) {
        return;
    }
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    // A pressed card must look interactive immediately; relying on color alone
    // gives poor feedback on a small resistive touch panel.
    lv_obj_set_style_bg_color(card, theme_matrix_pressed_bg(), LV_STATE_PRESSED);
    lv_obj_set_style_translate_y(card, -1, LV_STATE_PRESSED);
    lv_obj_add_event_cb(card, cb, LV_EVENT_CLICKED, user_data);
}

static lv_obj_t *create_home_action_card(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                                         lv_coord_t w, lv_coord_t h, const char *title,
                                         const char *value, lv_color_t accent,
                                         lv_event_cb_t cb, void *user_data,
                                         lv_obj_t **value_label) {
    lv_obj_t *card = create_card(parent, w, h, x, y);
    lv_obj_set_style_pad_all(card, 5, 0);
    create_accent_bar(card, accent, static_cast<lv_coord_t>(h - 14));
    make_home_card_clickable(card, cb, user_data);

    lv_obj_t *title_lbl = create_label(card, title, lv_color_make(100, 116, 139), &lv_font_montserrat_12);
    lv_obj_set_width(title_lbl, static_cast<lv_coord_t>(w - 16));
    lv_label_set_long_mode(title_lbl, LV_LABEL_LONG_CLIP);
    lv_obj_align(title_lbl, LV_ALIGN_TOP_LEFT, 6, -1);

    lv_obj_t *val_lbl = create_label(card, value, theme_text_main(), &lv_font_montserrat_12);
    lv_obj_set_width(val_lbl, static_cast<lv_coord_t>(w - 16));
    lv_label_set_long_mode(val_lbl, LV_LABEL_LONG_CLIP);
    lv_obj_align(val_lbl, LV_ALIGN_BOTTOM_LEFT, 6, 1);
    if (value_label != nullptr) {
        *value_label = val_lbl;
    }
    return card;
}

static lv_obj_t *create_home_feed_button(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                                         lv_coord_t w, lv_coord_t h, const char *title,
                                         const char *value, lv_event_cb_t cb, void *user_data,
                                         lv_obj_t **value_label) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    // Subtle button styling
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, ui_light_theme ? lv_color_make(203, 213, 225) : lv_color_make(51, 65, 85), 0);
    lv_obj_set_style_bg_color(btn, ui_light_theme ? lv_color_make(241, 245, 249) : lv_color_make(30, 41, 59), 0);
    lv_obj_set_style_bg_color(btn, ui_light_theme ? lv_color_make(226, 232, 240) : lv_color_make(47, 58, 82), LV_STATE_PRESSED);

    if (cb != nullptr) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    }

    // Centered labels inside the button
    lv_obj_t *title_lbl = create_label(btn, title, lv_color_make(100, 116, 139), &lv_font_montserrat_12);
    lv_obj_set_width(title_lbl, static_cast<lv_coord_t>(w - 8));
    lv_label_set_long_mode(title_lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(title_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(title_lbl, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t *val_lbl = create_label(btn, value, theme_text_main(), &lv_font_montserrat_12);
    lv_obj_set_width(val_lbl, static_cast<lv_coord_t>(w - 8));
    lv_label_set_long_mode(val_lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(val_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(val_lbl, LV_ALIGN_BOTTOM_MID, 0, -4);

    if (value_label != nullptr) {
        *value_label = val_lbl;
    }

    return btn;
}

static lv_obj_t *create_home_device_card(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                                         lv_coord_t w, const char *icon,
                                         const char *title, lv_color_t accent,
                                         lv_event_cb_t cb, void *user_data,
                                         lv_obj_t **state_label,
                                         lv_obj_t **detail_label) {
    lv_obj_t *card = create_card(parent, w, 42, x, y);
    lv_obj_set_style_pad_all(card, 4, 0);
    create_accent_bar(card, accent, 26);
    make_home_card_clickable(card, cb, user_data);

    lv_obj_t *icon_lbl = create_label(card, icon, accent, &lv_font_montserrat_14);
    lv_obj_align(icon_lbl, LV_ALIGN_TOP_LEFT, 4, 0);

    lv_obj_t *title_lbl = create_label(card, title, theme_text_main(), &lv_font_montserrat_12);
    lv_obj_set_width(title_lbl, static_cast<lv_coord_t>(w - 34));
    lv_label_set_long_mode(title_lbl, LV_LABEL_LONG_DOT);
    lv_obj_align(title_lbl, LV_ALIGN_TOP_LEFT, 24, 1);

    lv_obj_t *state_lbl = create_label(card, "OFF", theme_text_muted(), &lv_font_montserrat_12);
    lv_obj_align(state_lbl, LV_ALIGN_BOTTOM_LEFT, 6, 1);

    lv_obj_t *detail_lbl = create_label(card, "AUTO", theme_text_muted(), &lv_font_montserrat_12);
    lv_obj_set_width(detail_lbl, static_cast<lv_coord_t>(w - 56));
    lv_label_set_long_mode(detail_lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(detail_lbl, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(detail_lbl, LV_ALIGN_BOTTOM_RIGHT, -5, 1);

    if (state_label != nullptr) {
        *state_label = state_lbl;
    }
    if (detail_label != nullptr) {
        *detail_label = detail_lbl;
    }
    return card;
}

static void build_home_page() {
    home_temp_current = nullptr;
    home_ph_current = nullptr;
    home_temp_target_lbl = nullptr;
    home_temp_trend_lbl = nullptr;
    home_feed_time_lbl = nullptr;
    home_light_state_lbl = nullptr;
    home_light_mode_lbl = nullptr;
    home_light_color_lbl = nullptr;
    home_plant_state_lbl = nullptr;
    home_plant_mode_lbl = nullptr;
    home_plant_color_lbl = nullptr;
    home_filter_state_lbl = nullptr;
    home_filter_mode_lbl = nullptr;
    home_heater_state_lbl = nullptr;
    home_heater_mode_lbl = nullptr;
    home_air_state_lbl = nullptr;
    home_air_mode_lbl = nullptr;

    lv_obj_t *temp_card = create_card(pages[0], 150, 86, 4, 4);
    lv_obj_set_style_pad_all(temp_card, 7, 0);
    create_accent_bar(temp_card, lv_color_make(6, 182, 212), 54);

    lv_obj_t *temp_title = create_label(temp_card, "WODA", lv_color_make(100, 116, 139), &lv_font_montserrat_12);
    lv_obj_align(temp_title, LV_ALIGN_TOP_LEFT, 6, -1);

    home_temp_target_lbl = create_label(temp_card, "Cel 25.0*C", lv_color_make(148, 163, 184), &lv_font_montserrat_12);
    lv_obj_align(home_temp_target_lbl, LV_ALIGN_TOP_RIGHT, -5, -1);

    home_temp_current = create_label(temp_card, "--.-", theme_text_main(), &lv_font_montserrat_24);
    lv_obj_align(home_temp_current, LV_ALIGN_LEFT_MID, 7, 7);

    lv_obj_t *temp_unit = create_label(temp_card, "*C", lv_color_make(6, 182, 212), &lv_font_montserrat_12);
    lv_obj_align_to(temp_unit, home_temp_current, LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -2);

    home_temp_trend_lbl = create_label(temp_card, "Brak danych", theme_text_muted(), &lv_font_montserrat_12);
    lv_obj_align(home_temp_trend_lbl, LV_ALIGN_BOTTOM_LEFT, 7, 1);

    if (cfg.showPhSensor) {
        lv_obj_t *ph_card = create_home_action_card(pages[0], 160, 4, 74, 41, "pH", "--",
                                                    lv_color_make(16, 185, 129), open_ph_subpage_cb,
                                                    nullptr, &home_ph_current);
        lv_obj_t *ph_lbl = lv_obj_get_child(ph_card, 2);
        if (ph_lbl != nullptr) {
            lv_obj_set_style_text_font(ph_lbl, &lv_font_montserrat_14, 0);
        }

        create_home_feed_button(pages[0], 240, 4, 76, 41, "KARMIJ", "--:--",
                                feed_now_event_handler, nullptr, &home_feed_time_lbl);
    } else {
        create_home_feed_button(pages[0], 160, 4, 156, 41, "KARMIJ TERAZ", "--:--",
                                feed_now_event_handler, nullptr, &home_feed_time_lbl);
    }

    create_home_action_card(pages[0], 160, 49, 156, 41, "SERWIS", "Sterowanie reczne",
                            lv_color_make(239, 68, 68), service_tile_cb, nullptr, nullptr);

    const bool show_air = cfg.enableAerator;
    if (show_air) {
        create_home_device_card(pages[0], 4, 94, 100, LV_SYMBOL_IMAGE, "Przednia",
                                lv_color_make(14, 165, 233), open_sched_editor_cb,
                                reinterpret_cast<void *>(static_cast<intptr_t>(ScheduleDevice::Light)),
                                &home_light_state_lbl, &home_light_mode_lbl);
        create_home_device_card(pages[0], 110, 94, 100, LV_SYMBOL_IMAGE, "Tylna",
                                lv_color_make(34, 197, 94), open_sched_editor_cb,
                                reinterpret_cast<void *>(static_cast<intptr_t>(ScheduleDevice::PlantLight)),
                                &home_plant_state_lbl, &home_plant_mode_lbl);
        create_home_device_card(pages[0], 216, 94, 100, LV_SYMBOL_LOOP, "Filtr",
                                lv_color_make(6, 182, 212), open_sched_editor_cb,
                                reinterpret_cast<void *>(static_cast<intptr_t>(ScheduleDevice::Filter)),
                                &home_filter_state_lbl, &home_filter_mode_lbl);
        create_home_device_card(pages[0], 4, 138, 153, LV_SYMBOL_CHARGE, "Grzalka",
                                lv_color_make(249, 115, 22), open_heater_subpage_cb,
                                nullptr, &home_heater_state_lbl, &home_heater_mode_lbl);
        create_home_device_card(pages[0], 163, 138, 153, LV_SYMBOL_REFRESH, "Powietrze",
                                lv_color_make(168, 85, 247), open_sched_editor_cb,
                                reinterpret_cast<void *>(static_cast<intptr_t>(ScheduleDevice::Air)),
                                &home_air_state_lbl, &home_air_mode_lbl);
    } else {
        create_home_device_card(pages[0], 4, 94, 153, LV_SYMBOL_IMAGE, "Przednia",
                                lv_color_make(14, 165, 233), open_sched_editor_cb,
                                reinterpret_cast<void *>(static_cast<intptr_t>(ScheduleDevice::Light)),
                                &home_light_state_lbl, &home_light_mode_lbl);
        create_home_device_card(pages[0], 163, 94, 153, LV_SYMBOL_IMAGE, "Tylna",
                                lv_color_make(34, 197, 94), open_sched_editor_cb,
                                reinterpret_cast<void *>(static_cast<intptr_t>(ScheduleDevice::PlantLight)),
                                &home_plant_state_lbl, &home_plant_mode_lbl);
        create_home_device_card(pages[0], 4, 138, 153, LV_SYMBOL_LOOP, "Filtr",
                                lv_color_make(6, 182, 212), open_sched_editor_cb,
                                reinterpret_cast<void *>(static_cast<intptr_t>(ScheduleDevice::Filter)),
                                &home_filter_state_lbl, &home_filter_mode_lbl);
        create_home_device_card(pages[0], 163, 138, 153, LV_SYMBOL_CHARGE, "Grzalka",
                                lv_color_make(249, 115, 22), open_heater_subpage_cb,
                                nullptr, &home_heater_state_lbl, &home_heater_mode_lbl);
    }
}

static void set_device_mode_cb(lv_event_t *e) {
    play_system_sound(SoundType::Save);
    intptr_t user_data = reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
    int device = user_data / 10;
    int mode = user_data % 10;

    if (device == 0) { // FrontLight
        cfg.lightMode = mode;
    } else if (device == 1) { // RearLight
        cfg.plantLightMode = mode;
    } else if (device == 2) { // Filter
        cfg.filterMode = mode;
    } else if (device == 3) { // Heater
        cfg.heaterMode = mode;
    } else if (device == 4) { // Air
        cfg.airMode = mode;
    }
    
    // Save configuration
    gui_app_save_settings();
    
    // Sync widgets
    gui_sync_widgets_to_state();
}

static void show_top_notification(const char *text, bool success) {
    play_system_sound(success ? SoundType::Click : SoundType::Click);
    if (!ensure_runtime_ui_heap("TopNotification", UI_RUNTIME_MODAL_MIN_FREE, UI_RUNTIME_BIGGEST_MIN)) {
        return;
    }
    lv_obj_t *notif = lv_obj_create(lv_scr_act());
    lv_obj_set_size(notif, 280, 32);
    lv_obj_set_pos(notif, 20, -40); // Start off-screen
    style_panel(notif, ui_light_theme ? lv_color_make(240, 240, 240) : lv_color_make(20, 26, 40), 
                      success ? lv_color_make(16, 185, 129) : lv_color_make(245, 158, 11), 6);
    lv_obj_set_style_pad_all(notif, 0, 0);
    lv_obj_clear_flag(notif, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = create_label(notif, text, ui_light_theme ? lv_color_make(15, 23, 42) : lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);

    // Slide down animation
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, notif);
    lv_anim_set_values(&a, -40, 32); // slide down to 32px
    lv_anim_set_time(&a, 300);
    lv_anim_set_exec_cb(&a, [](void *var, int32_t val) {
        lv_obj_set_y(static_cast<lv_obj_t *>(var), val);
    });
    lv_anim_start(&a);

    // Slide up and delete after 2 seconds
    lv_timer_t *t = lv_timer_create([](lv_timer_t *timer) {
        lv_obj_t *n_obj = static_cast<lv_obj_t *>(timer->user_data);
        if (n_obj != nullptr) {
            lv_anim_t a_up;
            lv_anim_init(&a_up);
            lv_anim_set_var(&a_up, n_obj);
            lv_anim_set_values(&a_up, 32, -40);
            lv_anim_set_time(&a_up, 300);
            lv_anim_set_exec_cb(&a_up, [](void *var, int32_t val) {
                lv_obj_set_y(static_cast<lv_obj_t *>(var), val);
            });
            lv_anim_set_ready_cb(&a_up, [](lv_anim_t *anim) {
                lv_obj_del(static_cast<lv_obj_t *>(anim->var));
            });
            lv_anim_start(&a_up);
        }
    }, 2000, notif);
    lv_timer_set_repeat_count(t, 1);
}

static void make_3d_tile(lv_obj_t *obj) {
    if (obj == nullptr) return;
    lv_obj_set_style_radius(obj, 8, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, ui_light_theme ? lv_color_make(203, 213, 225) : lv_color_make(51, 65, 85), 0);
    lv_obj_set_style_bg_color(obj, ui_light_theme ? lv_color_make(255, 255, 255) : lv_color_make(30, 41, 59), 0);
    
    // Default Shadow
    lv_obj_set_style_shadow_width(obj, 6, LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_ofs_x(obj, 3, LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_ofs_y(obj, 3, LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(obj, ui_light_theme ? lv_color_make(100, 116, 139) : lv_color_make(0, 0, 0), LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(obj, ui_light_theme ? LV_OPA_30 : LV_OPA_50, LV_STATE_DEFAULT);
    
    // Pressed State Shadow & Translate
    lv_obj_set_style_shadow_width(obj, 2, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_ofs_x(obj, 1, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_ofs_y(obj, 1, LV_STATE_PRESSED);
    lv_obj_set_style_translate_x(obj, 2, LV_STATE_PRESSED);
    lv_obj_set_style_translate_y(obj, 2, LV_STATE_PRESSED);
}

static void apply_3d_button_properties(lv_obj_t *obj) {
    if (obj == nullptr) return;
    make_3d_tile(obj);
    // Background color when pressed
    lv_obj_set_style_bg_color(obj, ui_light_theme ? lv_color_make(224, 242, 254) : lv_color_make(30, 41, 59), LV_STATE_PRESSED);
}

static void style_tile_3d(lv_obj_t *tile, bool active) {
    if (tile == nullptr) return;
    
    lv_color_t bg = ui_light_theme ? (active ? lv_color_make(255, 255, 255) : lv_color_make(248, 250, 252))
                                   : (active ? lv_color_make(30, 41, 59) : lv_color_make(15, 23, 42));
    lv_color_t border = active ? lv_color_make(6, 182, 212) // Cyan
                               : (ui_light_theme ? lv_color_make(203, 213, 225) : lv_color_make(51, 65, 85));
                               
    lv_obj_set_style_bg_color(tile, bg, 0);
    lv_obj_set_style_border_color(tile, border, 0);
    lv_obj_set_style_border_width(tile, active ? 2 : 1, 0);
    
    // 3D Shadow
    lv_obj_set_style_shadow_width(tile, 6, 0);
    lv_obj_set_style_shadow_ofs_x(tile, 3, 0);
    lv_obj_set_style_shadow_ofs_y(tile, 3, 0);
    lv_obj_set_style_shadow_color(tile, ui_light_theme ? lv_color_make(100, 116, 139) : lv_color_make(0, 0, 0), 0);
    lv_obj_set_style_shadow_opa(tile, ui_light_theme ? LV_OPA_30 : LV_OPA_50, 0);
    
    // Set opacity on child elements if disabled
    uint32_t child_cnt = lv_obj_get_child_cnt(tile);
    for (uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t *child = lv_obj_get_child(tile, i);
        lv_obj_set_style_opa(child, active ? LV_OPA_COVER : LV_OPA_60, 0);
    }
}

static lv_obj_t *create_tile_3d(lv_obj_t *parent, const char *icon, const char *title, lv_event_cb_t cb, void *user_data) {
    lv_obj_t *tile = lv_obj_create(parent);
    lv_obj_set_size(tile, 144, 74);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(tile, 8, 0);
    make_3d_tile(tile);
    
    if (cb != nullptr) {
        lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(tile, cb, LV_EVENT_CLICKED, user_data);
    }
    
    lv_obj_t *ico_lbl = create_label(tile, icon, lv_color_make(6, 182, 212), &lv_font_montserrat_14);
    lv_obj_align(ico_lbl, LV_ALIGN_TOP_LEFT, 4, 4);
    
    lv_obj_t *title_lbl = create_label(tile, title, theme_text_main(), &lv_font_montserrat_12);
    lv_obj_set_width(title_lbl, 106);
    lv_label_set_long_mode(title_lbl, LV_LABEL_LONG_DOT);
    lv_obj_align(title_lbl, LV_ALIGN_TOP_LEFT, 24, 5);
    
    lv_obj_t *status_lbl = create_label(tile, "", theme_text_muted(), &lv_font_montserrat_12);
    lv_obj_set_width(status_lbl, 126);
    lv_label_set_long_mode(status_lbl, LV_LABEL_LONG_DOT);
    lv_obj_align(status_lbl, LV_ALIGN_BOTTOM_LEFT, 4, -4);
    
    return tile;
}

static void build_schedules_page() {
    lv_obj_set_flex_flow(pages[1], LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_all(pages[1], 8, 0);
    lv_obj_set_style_pad_row(pages[1], 8, 0);
    lv_obj_set_style_pad_column(pages[1], 8, 0);
    lv_obj_set_flex_align(pages[1], LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    tile_light = create_tile_3d(pages[1], LV_SYMBOL_IMAGE, "Przednia", open_sched_editor_cb, reinterpret_cast<void *>(static_cast<intptr_t>(ScheduleDevice::Light)));
    sched_light_lbl = lv_obj_get_child(tile_light, 2);

    tile_plant = create_tile_3d(pages[1], LV_SYMBOL_IMAGE, "Tylna", open_sched_editor_cb, reinterpret_cast<void *>(static_cast<intptr_t>(ScheduleDevice::PlantLight)));
    sched_plant_lbl = lv_obj_get_child(tile_plant, 2);

    tile_filter = create_tile_3d(pages[1], LV_SYMBOL_LOOP, "Filtr", open_sched_editor_cb, reinterpret_cast<void *>(static_cast<intptr_t>(ScheduleDevice::Filter)));
    // Rozszerz kafelek Filtr na pelna szerokosc (dwa kafelki)
    lv_obj_set_width(tile_filter, 296);
    lv_obj_t *filter_title_lbl = lv_obj_get_child(tile_filter, 1);
    if (filter_title_lbl != nullptr) {
        lv_obj_set_width(filter_title_lbl, 258);
    }
    sched_filter_lbl = lv_obj_get_child(tile_filter, 2);
    if (sched_filter_lbl != nullptr) {
        lv_obj_set_width(sched_filter_lbl, 276);
    }
}

static void build_optional_page() {
    // The optional module list can grow to nine cards. Keep every module
    // reachable instead of clipping rows below the 180 px page viewport.
    lv_obj_add_flag(pages[2], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(pages[2], LV_DIR_VER);
    lv_obj_set_scrollbar_mode(pages[2], LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(pages[2], LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_all(pages[2], 8, 0);
    lv_obj_set_style_pad_row(pages[2], 8, 0);
    lv_obj_set_style_pad_column(pages[2], 8, 0);
    lv_obj_set_flex_align(pages[2], LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    tile_feeder = create_tile_3d(pages[2], LV_SYMBOL_LIST, "Karmnik", open_sched_editor_cb, reinterpret_cast<void *>(static_cast<intptr_t>(ScheduleDevice::Feed)));
    sched_feed_lbl = lv_obj_get_child(tile_feeder, 2);

    if (cfg.enableHeater) {
        tile_heater = create_tile_3d(pages[2], LV_SYMBOL_CHARGE, "Grzalka", open_heater_subpage_cb, nullptr);
        device_heater_detail_lbl = lv_obj_get_child(tile_heater, 2);
    } else tile_heater = nullptr;

    if (cfg.showPhSensor) {
        tile_ph = create_tile_3d(pages[2], LV_SYMBOL_EDIT, "pH", open_ph_subpage_cb, nullptr);
        device_ph_detail_lbl = lv_obj_get_child(tile_ph, 2);
    } else tile_ph = nullptr;

    if (cfg.enableAerator) {
        tile_air = create_tile_3d(pages[2], LV_SYMBOL_REFRESH, "Powietrze", open_sched_editor_cb, reinterpret_cast<void *>(static_cast<intptr_t>(ScheduleDevice::Air)));
        device_air_detail_lbl = lv_obj_get_child(tile_air, 2);
    } else tile_air = nullptr;

    if (cfg.enableCo2) {
        tile_co2 = create_tile_3d(pages[2], LV_SYMBOL_SETTINGS, "CO2", open_co2_subpage_cb, nullptr);
        device_co2_detail_lbl = lv_obj_get_child(tile_co2, 2);
    } else tile_co2 = nullptr;

    if (cfg.enableEc) {
        tile_ec = create_tile_3d(pages[2], LV_SYMBOL_EDIT, "EC", open_ec_subpage_cb, nullptr);
        device_ec_detail_lbl = lv_obj_get_child(tile_ec, 2);
    } else tile_ec = nullptr;

    if (cfg.enableWaterLevel) {
        tile_water = create_tile_3d(pages[2], LV_SYMBOL_UPLOAD, "Poziom", open_water_subpage_cb, nullptr);
        device_water_detail_lbl = lv_obj_get_child(tile_water, 2);
    } else tile_water = nullptr;

    if (cfg.enableLeak) {
        tile_leak = create_tile_3d(pages[2], LV_SYMBOL_WARNING, "Wyciek", open_leak_subpage_cb, nullptr);
        device_leak_detail_lbl = lv_obj_get_child(tile_leak, 2);
    } else tile_leak = nullptr;

    if (cfg.enableFlow) {
        tile_flow = create_tile_3d(pages[2], LV_SYMBOL_LOOP, "Przeplyw", open_flow_subpage_cb, nullptr);
        device_flow_detail_lbl = lv_obj_get_child(tile_flow, 2);
    } else tile_flow = nullptr;
}

static lv_color_t active_chart_color() {
    switch (active_chart) {
    case ActiveChart::Temp:
        return lv_color_make(6, 182, 212);
    case ActiveChart::Ph:
        return lv_color_make(168, 85, 247);
    case ActiveChart::Ldr:
        return lv_color_make(234, 179, 8);
    case ActiveChart::Heap:
        return lv_color_make(14, 165, 233);
    default:
        return lv_color_make(6, 182, 212);
    }
}

static void chart_draw_event_cb(lv_event_t *e) {
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
    if (dsc->part == LV_PART_ITEMS) {
        if (dsc->sub_part_ptr == chart_temp_series) {
            const lv_color_t main_color = active_chart_color();
            dsc->line_dsc->color = main_color;
            dsc->line_dsc->width = 2;
            if (dsc->rect_dsc != nullptr) {
                dsc->rect_dsc->bg_color = main_color;
                dsc->rect_dsc->bg_opa = LV_OPA_10;
            }
        } else if (dsc->sub_part_ptr == chart_temp_target_series) {
            dsc->line_dsc->color = lv_color_make(16, 185, 129); // Green
            dsc->line_dsc->width = 1;
            if (dsc->rect_dsc != nullptr) {
                dsc->rect_dsc->bg_opa = LV_OPA_0;
            }
        } else if (dsc->sub_part_ptr == chart_temp_upper_series) {
            dsc->line_dsc->color = lv_color_make(239, 68, 68); // Red
            dsc->line_dsc->width = 1;
            if (dsc->rect_dsc != nullptr) {
                dsc->rect_dsc->bg_opa = LV_OPA_0;
            }
        } else if (dsc->sub_part_ptr == chart_temp_lower_series) {
            dsc->line_dsc->color = lv_color_make(59, 130, 246); // Blue
            dsc->line_dsc->width = 1;
            if (dsc->rect_dsc != nullptr) {
                dsc->rect_dsc->bg_opa = LV_OPA_0;
            }
        } else if (dsc->sub_part_ptr == chart_temp_heater_series) {
            dsc->line_dsc->color = lv_color_make(249, 115, 22); // Orange
            dsc->line_dsc->width = 1;
            dsc->line_dsc->opa = LV_OPA_10;
            if (dsc->rect_dsc != nullptr) {
                dsc->rect_dsc->bg_color = lv_color_make(249, 115, 22);
                dsc->rect_dsc->bg_opa = LV_OPA_40; // Orange columns
            }
        } else if (dsc->sub_part_ptr == chart_ph_series) {
            dsc->line_dsc->color = lv_color_make(168, 85, 247); // Purple
            dsc->line_dsc->width = 2;
            if (dsc->rect_dsc != nullptr) {
                dsc->rect_dsc->bg_color = lv_color_make(168, 85, 247);
                dsc->rect_dsc->bg_opa = LV_OPA_10;
            }
        } else if (dsc->sub_part_ptr == chart_ldr_series) {
            dsc->line_dsc->color = lv_color_make(234, 179, 8); // Yellow
            dsc->line_dsc->width = 2;
            if (dsc->rect_dsc != nullptr) {
                dsc->rect_dsc->bg_color = lv_color_make(234, 179, 8);
                dsc->rect_dsc->bg_opa = LV_OPA_10;
            }
        } else if (dsc->sub_part_ptr == chart_heap_series) {
            dsc->line_dsc->color = lv_color_make(14, 165, 233); // Sky Blue
            dsc->line_dsc->width = 2;
            if (dsc->rect_dsc != nullptr) {
                dsc->rect_dsc->bg_color = lv_color_make(14, 165, 233);
                dsc->rect_dsc->bg_opa = LV_OPA_10;
            }
        }
    }
}

static void build_charts_page() {
    lv_obj_t *panel = create_card(pages[3], 312, 172, 4, 4);
    lv_obj_set_style_pad_all(panel, 6, 0);

    // Wybór wykresu (TEMP, pH, LDR, HEAP)
    btn_chart_temp = create_button(panel, "TEMP", 50, 22, lv_color_make(35, 41, 55), select_chart_cb, reinterpret_cast<void *>(static_cast<intptr_t>(ActiveChart::Temp)));
    lv_obj_align(btn_chart_temp, LV_ALIGN_TOP_LEFT, 0, -2);
    style_chart_btn(btn_chart_temp);
    lv_obj_add_state(btn_chart_temp, LV_STATE_CHECKED);
    
    btn_chart_ph = create_button(panel, "pH", 42, 22, lv_color_make(35, 41, 55), select_chart_cb, reinterpret_cast<void *>(static_cast<intptr_t>(ActiveChart::Ph)));
    lv_obj_align(btn_chart_ph, LV_ALIGN_TOP_LEFT, 53, -2);
    style_chart_btn(btn_chart_ph);
    
    btn_chart_ldr = create_button(panel, "LDR", 42, 22, lv_color_make(35, 41, 55), select_chart_cb, reinterpret_cast<void *>(static_cast<intptr_t>(ActiveChart::Ldr)));
    lv_obj_align(btn_chart_ldr, LV_ALIGN_TOP_LEFT, 98, -2);
    style_chart_btn(btn_chart_ldr);

    btn_chart_heap = create_button(panel, "HEAP", 45, 22, lv_color_make(35, 41, 55), select_chart_cb, reinterpret_cast<void *>(static_cast<intptr_t>(ActiveChart::Heap)));
    lv_obj_align(btn_chart_heap, LV_ALIGN_TOP_LEFT, 143, -2);
    style_chart_btn(btn_chart_heap);

    // Bufor RAM zawiera 32 ostatnie próbki; etykieta nie udaje zakresu 1H/24H.
    lv_obj_t *range_btn = create_button(panel, "LIVE", 44, 22, lv_color_make(35, 41, 55), nullptr, nullptr);
    lv_obj_align(range_btn, LV_ALIGN_TOP_RIGHT, 0, -2);
    lv_obj_clear_flag(range_btn, LV_OBJ_FLAG_CLICKABLE);
    chart_range_lbl = lv_obj_get_child(range_btn, 0);

    // Etykieta temperatury docelowej
    chart_target_lbl = create_label(panel, "Target 25.0*C  H 0.5", lv_color_make(148, 163, 184), &lv_font_montserrat_12);
    lv_obj_align(chart_target_lbl, LV_ALIGN_TOP_LEFT, 0, 22);

    // 1. Wykres Temperatury
    chart_temp = lv_chart_create(panel);
    lv_obj_set_size(chart_temp, 300, 92);
    lv_obj_align(chart_temp, LV_ALIGN_TOP_MID, 0, 42);
    lv_obj_set_style_bg_color(chart_temp, resolve_bg_color(lv_color_make(8, 13, 24)), 0);
    lv_obj_set_style_border_color(chart_temp, ui_light_theme ? theme_card_border() : lv_color_make(35, 41, 55), 0);
    lv_obj_set_style_border_width(chart_temp, 1, 0);
    lv_obj_set_style_radius(chart_temp, 4, 0);
    lv_obj_set_style_line_width(chart_temp, 2, LV_PART_ITEMS);
    lv_chart_set_type(chart_temp, LV_CHART_TYPE_LINE);
    lv_chart_set_update_mode(chart_temp, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_point_count(chart_temp, TEMP_HISTORY_POINTS);
    lv_chart_set_range(chart_temp, LV_CHART_AXIS_PRIMARY_Y, 180, 300);
    lv_chart_set_div_line_count(chart_temp, 4, 6);
    
    chart_temp_series = lv_chart_add_series(chart_temp, lv_color_make(6, 182, 212), LV_CHART_AXIS_PRIMARY_Y);
    chart_temp_target_series = lv_chart_add_series(chart_temp, lv_color_make(16, 185, 129), LV_CHART_AXIS_PRIMARY_Y);
    chart_temp_upper_series = lv_chart_add_series(chart_temp, lv_color_make(239, 68, 68), LV_CHART_AXIS_PRIMARY_Y);
    chart_temp_lower_series = lv_chart_add_series(chart_temp, lv_color_make(59, 130, 246), LV_CHART_AXIS_PRIMARY_Y);
    chart_temp_heater_series = lv_chart_add_series(chart_temp, lv_color_make(249, 115, 22), LV_CHART_AXIS_PRIMARY_Y);
    
    lv_chart_set_all_value(chart_temp, chart_temp_series, LV_CHART_POINT_NONE);
    lv_chart_set_all_value(chart_temp, chart_temp_target_series, LV_CHART_POINT_NONE);
    lv_chart_set_all_value(chart_temp, chart_temp_upper_series, LV_CHART_POINT_NONE);
    lv_chart_set_all_value(chart_temp, chart_temp_lower_series, LV_CHART_POINT_NONE);
    lv_chart_set_all_value(chart_temp, chart_temp_heater_series, LV_CHART_POINT_NONE);

    // 2. Wykres pH
    chart_ph = nullptr;
    chart_ph_series = nullptr;

    // 3. Wykres LDR (Jasności)
    chart_ldr = nullptr;
    chart_ldr_series = nullptr;

    // 4. Wykres HEAP (Pamięci)
    chart_heap = nullptr;
    chart_heap_series = nullptr;

    // Stylizacja wykresów
    lv_obj_t *all_charts[4] = {chart_temp, chart_ph, chart_ldr, chart_heap};
    for (int i = 0; i < 4; ++i) {
        if (all_charts[i] != nullptr) {
            lv_obj_set_style_size(all_charts[i], 0, LV_PART_INDICATOR);
            lv_obj_set_style_bg_opa(all_charts[i], LV_OPA_20, LV_PART_ITEMS);
            lv_obj_set_style_line_color(all_charts[i], resolve_bg_color(lv_color_make(20, 26, 40)), LV_PART_MAIN);
            lv_obj_set_style_line_width(all_charts[i], 1, LV_PART_MAIN);
            lv_obj_add_event_cb(all_charts[i], chart_draw_event_cb, LV_EVENT_DRAW_PART_BEGIN, nullptr);
        }
    }

    lv_obj_t *stats = lv_obj_create(panel);
    lv_obj_set_size(stats, 300, 24);
    lv_obj_align(stats, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_pad_all(stats, 0, 0);
    lv_obj_set_style_bg_color(stats, resolve_bg_color(lv_color_make(8, 13, 24)), 0);
    lv_obj_set_style_border_width(stats, 0, 0);
    lv_obj_set_style_radius(stats, 4, 0);
    lv_obj_clear_flag(stats, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *min_title = create_label(stats, "MIN", lv_color_make(100, 116, 139), &lv_font_montserrat_12);
    lv_obj_align(min_title, LV_ALIGN_LEFT_MID, 10, -6);
    chart_min_lbl = create_label(stats, "--.-", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(chart_min_lbl, LV_ALIGN_LEFT_MID, 10, 6);

    lv_obj_t *max_title = create_label(stats, "MAX", lv_color_make(100, 116, 139), &lv_font_montserrat_12);
    lv_obj_align(max_title, LV_ALIGN_CENTER, 0, -6);
    chart_max_lbl = create_label(stats, "--.-", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(chart_max_lbl, LV_ALIGN_CENTER, 0, 6);

    lv_obj_t *cur_title = create_label(stats, "CUR", lv_color_make(100, 116, 139), &lv_font_montserrat_12);
    lv_obj_align(cur_title, LV_ALIGN_RIGHT_MID, -10, -6);
    chart_cur_lbl = create_label(stats, "--.-", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(chart_cur_lbl, LV_ALIGN_RIGHT_MID, -10, 6);
}


static lv_obj_t *create_system_hub_item(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                                        const char *icon, const char *title,
                                        const char *subtitle, lv_color_t accent,
                                        ActiveSubpage target) {
    lv_obj_t *tile = create_card(parent, 150, 38, x, y);
    lv_obj_set_style_pad_all(tile, 4, 0);
    void *user_data = reinterpret_cast<void *>(static_cast<intptr_t>(target));
    make_object_clickable(tile, open_system_subpage, user_data);

    lv_obj_t *icon_lbl = create_label(tile, icon, accent, &lv_font_montserrat_14);
    lv_obj_align(icon_lbl, LV_ALIGN_LEFT_MID, 5, -1);
    make_object_clickable(icon_lbl, open_system_subpage, user_data);

    lv_obj_t *title_lbl = create_label(tile, title, theme_text_main(), &lv_font_montserrat_12);
    lv_obj_set_width(title_lbl, 108);
    lv_label_set_long_mode(title_lbl, LV_LABEL_LONG_DOT);
    lv_obj_align(title_lbl, LV_ALIGN_TOP_LEFT, 28, 0);
    make_object_clickable(title_lbl, open_system_subpage, user_data);

    lv_obj_t *subtitle_lbl = create_label(tile, subtitle, theme_text_muted(), &lv_font_montserrat_12);
    lv_obj_set_width(subtitle_lbl, 108);
    lv_label_set_long_mode(subtitle_lbl, LV_LABEL_LONG_DOT);
    lv_obj_align(subtitle_lbl, LV_ALIGN_BOTTOM_LEFT, 28, 1);
    make_object_clickable(subtitle_lbl, open_system_subpage, user_data);
    return tile;
}

static void build_system_page() {
    lv_obj_set_style_pad_all(pages[4], 0, 0);
    create_system_hub_item(pages[4], 4, 4, LV_SYMBOL_WIFI, "WiFi", "STA / OTA",
                           lv_color_make(14, 165, 233), ActiveSubpage::Wifi);
    create_system_hub_item(pages[4], 166, 4, LV_SYMBOL_IMAGE, "Ekran", "Motyw / LDR",
                           lv_color_make(6, 182, 212), ActiveSubpage::Screen);
    create_system_hub_item(pages[4], 4, 48, LV_SYMBOL_LIST, "Logi", "Zdarzenia",
                           lv_color_make(245, 158, 11), ActiveSubpage::Logs);
    create_system_hub_item(pages[4], 166, 48, LV_SYMBOL_KEYBOARD, "Czas", "RTC / NTP",
                           lv_color_make(34, 197, 94), ActiveSubpage::Clock);
    create_system_hub_item(pages[4], 4, 92, LV_SYMBOL_SETTINGS, "Diag", "Heap / CPU",
                           lv_color_make(168, 85, 247), ActiveSubpage::Diagnostics);
    create_system_hub_item(pages[4], 166, 92, LV_SYMBOL_POWER, "Zasilanie", "Sleep / reset",
                           lv_color_make(239, 68, 68), ActiveSubpage::Power);
    create_system_hub_item(pages[4], 4, 136, LV_SYMBOL_AUDIO, "Audio", "Dzwieki",
                           lv_color_make(20, 184, 166), ActiveSubpage::Sounds);
    create_system_hub_item(pages[4], 166, 136, LV_SYMBOL_PLUS, "Sprzet", "Moduly",
                           lv_color_make(100, 116, 139), ActiveSubpage::Hardware);
}

static void reset_page_object_refs(uint8_t index) {
    if (index >= PAGE_COUNT) {
        return;
    }

    pages[index] = nullptr;
    switch (index) {
    case 0:
        home_temp_current = nullptr;
        home_ph_current = nullptr;
        home_temp_target_lbl = nullptr;
        home_temp_trend_lbl = nullptr;
        home_feed_time_lbl = nullptr;
        home_light_state_lbl = nullptr;
        home_light_mode_lbl = nullptr;
        home_light_color_lbl = nullptr;
        home_plant_state_lbl = nullptr;
        home_plant_mode_lbl = nullptr;
        home_plant_color_lbl = nullptr;
        home_filter_state_lbl = nullptr;
        home_filter_mode_lbl = nullptr;
        home_heater_state_lbl = nullptr;
        home_heater_mode_lbl = nullptr;
        home_air_state_lbl = nullptr;
        home_air_mode_lbl = nullptr;
        break;
    case 1:
        tile_light = nullptr;
        tile_plant = nullptr;
        tile_filter = nullptr;
        sched_light_lbl = nullptr;
        sched_plant_lbl = nullptr;
        sched_filter_lbl = nullptr;
        sched_air_lbl = nullptr;
        break;
    case 2:
        tile_feeder = nullptr;
        tile_heater = nullptr;
        tile_ph = nullptr;
        tile_air = nullptr;
        tile_co2 = nullptr;
        tile_ec = nullptr;
        tile_water = nullptr;
        tile_leak = nullptr;
        tile_flow = nullptr;
        sched_feed_lbl = nullptr;
        device_heater_detail_lbl = nullptr;
        device_air_detail_lbl = nullptr;
        device_ph_detail_lbl = nullptr;
        device_co2_detail_lbl = nullptr;
        device_ec_detail_lbl = nullptr;
        device_water_detail_lbl = nullptr;
        device_leak_detail_lbl = nullptr;
        device_flow_detail_lbl = nullptr;
        break;
    case 3:
        chart_temp = nullptr;
        chart_temp_series = nullptr;
        chart_min_lbl = nullptr;
        chart_max_lbl = nullptr;
        chart_cur_lbl = nullptr;
        chart_target_lbl = nullptr;
        chart_range_lbl = nullptr;
        btn_chart_temp = nullptr;
        btn_chart_ph = nullptr;
        btn_chart_ldr = nullptr;
        btn_chart_heap = nullptr;
        chart_ph = nullptr;
        chart_ph_series = nullptr;
        chart_ldr = nullptr;
        chart_ldr_series = nullptr;
        chart_heap = nullptr;
        chart_heap_series = nullptr;
        chart_temp_target_series = nullptr;
        chart_temp_upper_series = nullptr;
        chart_temp_lower_series = nullptr;
        chart_temp_heater_series = nullptr;
        break;
    default:
        break;
    }
}

static bool build_page_by_index(uint8_t index) {
    if (index >= PAGE_COUNT) {
        Serial.printf("UI_NAV: invalid page index=%u\n", static_cast<unsigned>(index));
        return false;
    }

    if (pages[index] != nullptr && lv_obj_is_valid(pages[index])) {
        return true;
    }

    add_page_base(index);
    if (pages[index] == nullptr || !lv_obj_is_valid(pages[index])) {
        Serial.printf("UI_NAV: page allocation failed index=%u\n", static_cast<unsigned>(index));
        reset_page_object_refs(index);
        return false;
    }

    switch (index) {
    case 0:
        build_home_page();
        break;
    case 1:
        build_schedules_page();
        break;
    case 2:
        build_optional_page();
        break;
    case 3:
        build_charts_page();
        break;
    case 4:
        build_system_page();
        break;
    default:
        reset_page_object_refs(index);
        return false;
    }

    log_page_ram("page_enter", index);
    return true;
}

static void delete_active_page() {
    if (current_page_index < 0 || current_page_index >= PAGE_COUNT) {
        return;
    }

    lv_obj_t *page = pages[current_page_index];
    if (page != nullptr && lv_obj_is_valid(page)) {
        lv_obj_del(page);
    }
    reset_page_object_refs(static_cast<uint8_t>(current_page_index));
    log_page_ram("page_deleted", current_page_index);
}

static void switch_to_page(uint8_t index) {
    if (index >= PAGE_COUNT) {
        return;
    }

    if (index == current_page_index && pages[index] != nullptr && lv_obj_is_valid(pages[index])) {
        for (uint8_t i = 0; i < PAGE_COUNT; ++i) {
            if (nav_btns[i] == nullptr) {
                continue;
            }
            if (i == index) {
                lv_obj_add_state(nav_btns[i], LV_STATE_CHECKED);
            } else {
                lv_obj_clear_state(nav_btns[i], LV_STATE_CHECKED);
            }
        }
        sync_nav_bar_visuals();
        return;
    }

    delete_runtime_subpages(false);
    current_subpage = ActiveSubpage::None;
    delete_active_page();
    current_page_index = index;

    if (!build_page_by_index(index)) {
        current_page_index = 0;
        if (!build_page_by_index(0)) {
            Serial.println("UI_NAV: failed to restore Start page.");
            return;
        }
    }

    for (uint8_t i = 0; i < PAGE_COUNT; ++i) {
        if (nav_btns[i] == nullptr) {
            continue;
        }
        if (i == current_page_index) {
            lv_obj_add_state(nav_btns[i], LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(nav_btns[i], LV_STATE_CHECKED);
        }
    }

    sync_nav_bar_visuals();
    gui_sync_widgets_to_state();
    gui_app_update_wifi(wifi_connected ? 1 : 0, wifi_rssi);
    redraw_charts();
    update_chart_stats();
}

static void sync_nav_bar_visuals() {
    for (uint8_t i = 0; i < PAGE_COUNT; ++i) {
        if (nav_btns[i] == nullptr) {
            continue;
        }
        const bool active = lv_obj_has_state(nav_btns[i], LV_STATE_CHECKED);
        
        lv_color_t fg;
        lv_color_t bg;
        lv_opa_t bg_opa;
        lv_color_t border_color;
        lv_coord_t border_width;

        if (ui_light_theme) {
            if (active) {
                fg = lv_color_make(3, 105, 161); // sky-700
                bg = lv_color_make(224, 242, 254); // sky-100
                bg_opa = LV_OPA_COVER;
                border_color = lv_color_make(3, 105, 161); // sky-700
                border_width = 1;
            } else {
                fg = lv_color_make(100, 116, 139); // slate-500
                bg = theme_nav_bg();
                bg_opa = LV_OPA_TRANSP;
                border_color = theme_nav_bg();
                border_width = 0;
            }
        } else {
            fg = active ? lv_color_make(6, 182, 212) : lv_color_make(100, 116, 139);
            bg = active ? resolve_bg_color(lv_color_make(15, 23, 42)) : theme_nav_bg();
            bg_opa = active ? LV_OPA_80 : LV_OPA_TRANSP;
            border_color = active ? lv_color_make(6, 182, 212) : theme_nav_bg();
            border_width = active ? 1 : 0;
        }

        lv_obj_set_style_bg_color(nav_btns[i], bg, 0);
        lv_obj_set_style_bg_opa(nav_btns[i], bg_opa, 0);
        lv_obj_set_style_border_color(nav_btns[i], border_color, 0);
        lv_obj_set_style_border_width(nav_btns[i], border_width, 0);

        const uint32_t child_count = lv_obj_get_child_cnt(nav_btns[i]);
        for (uint32_t child_index = 0; child_index < child_count; ++child_index) {
            lv_obj_t *child = lv_obj_get_child(nav_btns[i], child_index);
            lv_obj_set_style_text_color(child, fg, 0);
        }
    }
}

static void build_nav_bar() {
    lv_obj_t *nav = lv_obj_create(lv_scr_act());
    lv_obj_set_size(nav, 320, 35);
    lv_obj_set_pos(nav, 0, 205);
    lv_obj_set_style_pad_all(nav, 0, 0);
    style_panel(nav, lv_color_make(5, 8, 17), lv_color_make(30, 41, 59), 0);

    const char *symbols[PAGE_COUNT] = {
        LV_SYMBOL_HOME,
        LV_SYMBOL_LOOP,
        LV_SYMBOL_PLUS,
        LV_SYMBOL_IMAGE,
        LV_SYMBOL_SETTINGS
    };

    const char *captions[PAGE_COUNT] = {
        "Start",
        "Plan",
        "Moduly",
        "Wykres",
        "System"
    };

    for (uint8_t i = 0; i < PAGE_COUNT; ++i) {
        const lv_coord_t btn_w = 64;
        nav_btns[i] = lv_btn_create(nav);
        lv_obj_set_size(nav_btns[i], 62, 33);
        lv_obj_set_pos(nav_btns[i], static_cast<lv_coord_t>(1 + i * btn_w), 1);
        lv_obj_set_style_bg_opa(nav_btns[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_radius(nav_btns[i], 7, 0);
        lv_obj_set_style_border_width(nav_btns[i], 0, 0);
        lv_obj_set_style_pad_all(nav_btns[i], 0, 0);
        lv_obj_clear_flag(nav_btns[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(nav_btns[i], nav_btn_event_handler, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(static_cast<intptr_t>(i)));

        lv_obj_t *lbl = create_label(nav_btns[i], symbols[i], lv_color_make(100, 116, 139), &lv_font_montserrat_14);
        lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 1);

        lv_obj_t *caption = create_label(nav_btns[i], captions[i], lv_color_make(100, 116, 139), &lv_font_montserrat_12);
        lv_obj_align(caption, LV_ALIGN_BOTTOM_MID, 0, 1);
        if (i == 0) {
            lv_obj_add_state(nav_btns[i], LV_STATE_CHECKED);
        }
    }
    sync_nav_bar_visuals();
}

static const char *hours_options = "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23";
static const char *minutes_seconds_options = 
    "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n"
    "30\n31\n32\n33\n34\n35\n36\n37\n38\n39\n40\n41\n42\n43\n44\n45\n46\n47\n48\n49\n50\n51\n52\n53\n54\n55\n56\n57\n58\n59";

struct TimePickerData {
    lv_obj_t *bg_overlay;
    lv_obj_t *hour_lbl;
    lv_obj_t *minute_lbl;
    lv_obj_t *second_lbl;
    int hour;
    int minute;
    int second;
};

struct DatePickerData {
    lv_obj_t *bg_overlay;
    lv_obj_t *day_lbl;
    lv_obj_t *month_lbl;
    lv_obj_t *year_lbl;
    int day;
    int month;
    int year;
};

static TimePickerData time_picker_state = {};
static DatePickerData date_picker_state = {};

static int days_in_month_for(int month, int year) {
    if (month == 4 || month == 6 || month == 9 || month == 11) {
        return 30;
    }
    if (month == 2) {
        const bool leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
        return leap ? 29 : 28;
    }
    return 31;
}

static void update_time_picker_labels() {
    if (time_picker_state.hour_lbl != nullptr) {
        lv_label_set_text_fmt(time_picker_state.hour_lbl, "%02d", constrain(time_picker_state.hour, 0, 23));
    }
    if (time_picker_state.minute_lbl != nullptr) {
        lv_label_set_text_fmt(time_picker_state.minute_lbl, "%02d", constrain(time_picker_state.minute, 0, 59));
    }
    if (time_picker_state.second_lbl != nullptr) {
        lv_label_set_text_fmt(time_picker_state.second_lbl, "%02d", constrain(time_picker_state.second, 0, 59));
    }
}

static void update_date_picker_labels() {
    if (date_picker_state.day_lbl != nullptr) {
        lv_label_set_text_fmt(date_picker_state.day_lbl, "%02d", date_picker_state.day);
    }
    if (date_picker_state.month_lbl != nullptr) {
        lv_label_set_text_fmt(date_picker_state.month_lbl, "%02d", date_picker_state.month);
    }
    if (date_picker_state.year_lbl != nullptr) {
        lv_label_set_text_fmt(date_picker_state.year_lbl, "%04d", date_picker_state.year);
    }
}

static void time_picker_ok_cb(lv_event_t *e) {
    LV_UNUSED(e);
    play_system_sound(SoundType::Click);
    clock_hour = constrain(time_picker_state.hour, 0, 23);
    clock_minute = constrain(time_picker_state.minute, 0, 59);
    clock_second = constrain(time_picker_state.second, 0, 59);
    lv_obj_t *overlay = time_picker_state.bg_overlay;
    memset(&time_picker_state, 0, sizeof(time_picker_state));
    delete_obj_async(overlay);
    gui_sync_widgets_to_state();
    show_top_notification("Czas zapisany", true);
}

static void time_picker_cancel_cb(lv_event_t *e) {
    LV_UNUSED(e);
    play_system_sound(SoundType::Click);
    lv_obj_t *overlay = time_picker_state.bg_overlay;
    memset(&time_picker_state, 0, sizeof(time_picker_state));
    delete_obj_async(overlay);
}

static void open_time_picker_cb(lv_event_t *e) {
    LV_UNUSED(e);
    open_time_picker_authorized(); // Bezposrednio, bez PIN
}

static void time_step_cb(lv_event_t *e) {
    play_system_sound(SoundType::Click);
    const int code = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    const int field = code / 10;
    const int delta = (code % 10) == 1 ? 1 : -1;

    if (field == 0) {
        time_picker_state.hour += delta;
        if (time_picker_state.hour < 0) time_picker_state.hour = 23;
        if (time_picker_state.hour > 23) time_picker_state.hour = 0;
    } else if (field == 1) {
        time_picker_state.minute += delta;
        if (time_picker_state.minute < 0) time_picker_state.minute = 59;
        if (time_picker_state.minute > 59) time_picker_state.minute = 0;
    } else {
        time_picker_state.second += delta;
        if (time_picker_state.second < 0) time_picker_state.second = 59;
        if (time_picker_state.second > 59) time_picker_state.second = 0;
    }
    update_time_picker_labels();
}

static void add_stepper_group(lv_obj_t *parent, lv_coord_t x, const char *caption,
                              lv_obj_t **value_lbl, int field) {
    lv_obj_t *caption_lbl = create_label(parent, caption, theme_text_muted(), &lv_font_montserrat_12);
    lv_obj_align(caption_lbl, LV_ALIGN_TOP_LEFT, x + 12, 0);

    lv_obj_t *plus = create_button(parent, LV_SYMBOL_UP, 54, 28, lv_color_make(35, 41, 55),
                                   time_step_cb, reinterpret_cast<void *>(static_cast<intptr_t>(field * 10 + 1)));
    lv_obj_set_pos(plus, x, 22);
    apply_3d_button_properties(plus);

    lv_obj_t *box = create_card(parent, 54, 38, x, 54);
    lv_obj_set_style_pad_all(box, 0, 0);
    *value_lbl = create_label(box, "00", lv_color_make(6, 182, 212), &lv_font_montserrat_24);
    lv_obj_align(*value_lbl, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *minus = create_button(parent, LV_SYMBOL_DOWN, 54, 28, lv_color_make(35, 41, 55),
                                    time_step_cb, reinterpret_cast<void *>(static_cast<intptr_t>(field * 10 + 9)));
    lv_obj_set_pos(minus, x, 98);
    apply_3d_button_properties(minus);
}

static void open_time_picker_authorized() {
    // Sprawdz heap, ale nie blokuj — pokaz notyfikacje jesli brakuje pamieci
    const uint32_t heap_free = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    if (heap_free < 6000UL) {
        show_top_notification("Brak pamieci RAM", false);
        return;
    }
    play_system_sound(SoundType::Click);
    lv_obj_t *bg_overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(bg_overlay, 320, 240);
    lv_obj_set_pos(bg_overlay, 0, 0);
    lv_obj_set_style_bg_color(bg_overlay, theme_screen_bg(), 0);
    lv_obj_set_style_border_width(bg_overlay, 0, 0);
    lv_obj_set_style_radius(bg_overlay, 0, 0);
    lv_obj_clear_flag(bg_overlay, LV_OBJ_FLAG_SCROLLABLE);

    memset(&time_picker_state, 0, sizeof(time_picker_state));
    time_picker_state.bg_overlay = bg_overlay;
    time_picker_state.hour = constrain(clock_hour, 0, 23);
    time_picker_state.minute = constrain(clock_minute, 0, 59);
    time_picker_state.second = constrain(clock_second, 0, 59);

    lv_obj_t *header = lv_obj_create(bg_overlay);
    lv_obj_set_size(header, 320, 35);
    lv_obj_set_pos(header, 0, 0);
    style_panel(header, theme_header_bg(), theme_card_border(), 0);
    lv_obj_set_style_pad_all(header, 0, 0);

    lv_obj_t *title = create_label(header, "Ustaw Czas", theme_text_main(), &lv_font_montserrat_14);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *btn_back = create_button(header, LV_SYMBOL_LEFT " Wstecz", 70, 26, lv_color_make(35, 41, 55), time_picker_cancel_cb, nullptr);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 5, 0);
    apply_3d_button_properties(btn_back);

    lv_obj_t *main_area = lv_obj_create(bg_overlay);
    lv_obj_set_size(main_area, 320, 205);
    lv_obj_set_pos(main_area, 0, 35);
    lv_obj_set_style_bg_opa(main_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(main_area, 0, 0);
    lv_obj_set_style_pad_all(main_area, 0, 0);
    lv_obj_clear_flag(main_area, LV_OBJ_FLAG_SCROLLABLE);

    add_stepper_group(main_area, 48, "Godz", &time_picker_state.hour_lbl, 0);
    add_stepper_group(main_area, 132, "Min", &time_picker_state.minute_lbl, 1);
    add_stepper_group(main_area, 216, "Sek", &time_picker_state.second_lbl, 2);
    update_time_picker_labels();

    lv_obj_t *btn_ok = create_button(main_area, "Zapisz", 150, 32, lv_color_make(16, 185, 129), time_picker_ok_cb, nullptr);
    lv_obj_align(btn_ok, LV_ALIGN_BOTTOM_MID, 0, -15);
    apply_3d_button_properties(btn_ok);
}

static void date_picker_ok_cb(lv_event_t *e) {
    LV_UNUSED(e);
    play_system_sound(SoundType::Click);
    clock_year = constrain(date_picker_state.year, 2020, 2099);
    clock_month = constrain(date_picker_state.month, 1, 12);
    clock_day = constrain(date_picker_state.day, 1, days_in_month_for(clock_month, clock_year));
    lv_obj_t *overlay = date_picker_state.bg_overlay;
    memset(&date_picker_state, 0, sizeof(date_picker_state));
    delete_obj_async(overlay);
    gui_sync_widgets_to_state();
    show_top_notification("Data zapisana", true);
}

static void date_picker_cancel_cb(lv_event_t *e) {
    LV_UNUSED(e);
    play_system_sound(SoundType::Click);
    lv_obj_t *overlay = date_picker_state.bg_overlay;
    memset(&date_picker_state, 0, sizeof(date_picker_state));
    delete_obj_async(overlay);
}

static void date_step_cb(lv_event_t *e) {
    play_system_sound(SoundType::Click);
    const int code = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    const int field = code / 10;
    const int delta = (code % 10) == 1 ? 1 : -1;

    if (field == 0) {
        date_picker_state.day += delta;
    } else if (field == 1) {
        date_picker_state.month += delta;
        if (date_picker_state.month < 1) date_picker_state.month = 12;
        if (date_picker_state.month > 12) date_picker_state.month = 1;
    } else {
        date_picker_state.year += delta;
        date_picker_state.year = constrain(date_picker_state.year, 2020, 2099);
    }

    const int max_day = days_in_month_for(date_picker_state.month, date_picker_state.year);
    if (date_picker_state.day < 1) date_picker_state.day = max_day;
    if (date_picker_state.day > max_day) date_picker_state.day = 1;
    update_date_picker_labels();
}

static void add_date_stepper_group(lv_obj_t *parent, lv_coord_t x, const char *caption,
                                   lv_obj_t **value_lbl, int field) {
    lv_obj_t *caption_lbl = create_label(parent, caption, theme_text_muted(), &lv_font_montserrat_12);
    lv_obj_align(caption_lbl, LV_ALIGN_TOP_LEFT, x + 12, 0);

    lv_obj_t *plus = create_button(parent, LV_SYMBOL_UP, 66, 28, lv_color_make(35, 41, 55),
                                   date_step_cb, reinterpret_cast<void *>(static_cast<intptr_t>(field * 10 + 1)));
    lv_obj_set_pos(plus, x, 22);
    apply_3d_button_properties(plus);

    lv_obj_t *box = create_card(parent, 66, 38, x, 54);
    lv_obj_set_style_pad_all(box, 0, 0);
    *value_lbl = create_label(box, field == 2 ? "2026" : "00", lv_color_make(6, 182, 212), &lv_font_montserrat_16);
    lv_obj_align(*value_lbl, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *minus = create_button(parent, LV_SYMBOL_DOWN, 66, 28, lv_color_make(35, 41, 55),
                                    date_step_cb, reinterpret_cast<void *>(static_cast<intptr_t>(field * 10 + 9)));
    lv_obj_set_pos(minus, x, 98);
    apply_3d_button_properties(minus);
}

static void open_calendar_picker_cb(lv_event_t *e) {
    LV_UNUSED(e);
    open_date_picker_authorized(); // Bezposrednio, bez PIN
}

static void open_date_picker_authorized() {
    // Sprawdz heap, ale nie blokuj — pokaz notyfikacje jesli brakuje pamieci
    const uint32_t heap_free = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    if (heap_free < 6000UL) {
        show_top_notification("Brak pamieci RAM", false);
        return;
    }
    play_system_sound(SoundType::Click);
    lv_obj_t *bg_overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(bg_overlay, 320, 240);
    lv_obj_set_pos(bg_overlay, 0, 0);
    lv_obj_set_style_bg_color(bg_overlay, theme_screen_bg(), 0);
    lv_obj_set_style_border_width(bg_overlay, 0, 0);
    lv_obj_set_style_radius(bg_overlay, 0, 0);
    lv_obj_clear_flag(bg_overlay, LV_OBJ_FLAG_SCROLLABLE);

    memset(&date_picker_state, 0, sizeof(date_picker_state));
    date_picker_state.bg_overlay = bg_overlay;
    date_picker_state.year = constrain(clock_year, 2020, 2099);
    date_picker_state.month = constrain(clock_month, 1, 12);
    date_picker_state.day = constrain(clock_day, 1, days_in_month_for(date_picker_state.month, date_picker_state.year));

    lv_obj_t *header = lv_obj_create(bg_overlay);
    lv_obj_set_size(header, 320, 35);
    lv_obj_set_pos(header, 0, 0);
    style_panel(header, theme_header_bg(), theme_card_border(), 0);
    lv_obj_set_style_pad_all(header, 0, 0);

    lv_obj_t *title = create_label(header, "Wybierz date", theme_text_main(), &lv_font_montserrat_14);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *btn_back = create_button(header, LV_SYMBOL_LEFT " Wstecz", 70, 26, lv_color_make(35, 41, 55), date_picker_cancel_cb, nullptr);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 5, 0);
    apply_3d_button_properties(btn_back);

    lv_obj_t *main_area = lv_obj_create(bg_overlay);
    lv_obj_set_size(main_area, 320, 205);
    lv_obj_set_pos(main_area, 0, 35);
    lv_obj_set_style_bg_opa(main_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(main_area, 0, 0);
    lv_obj_set_style_pad_all(main_area, 0, 0);
    lv_obj_clear_flag(main_area, LV_OBJ_FLAG_SCROLLABLE);

    add_date_stepper_group(main_area, 35, "Dzien", &date_picker_state.day_lbl, 0);
    add_date_stepper_group(main_area, 127, "Mies", &date_picker_state.month_lbl, 1);
    add_date_stepper_group(main_area, 219, "Rok", &date_picker_state.year_lbl, 2);
    update_date_picker_labels();

    lv_obj_t *btn_ok = create_button(main_area, "Zapisz", 150, 32, lv_color_make(16, 185, 129), date_picker_ok_cb, nullptr);
    lv_obj_align(btn_ok, LV_ALIGN_BOTTOM_MID, 0, -15);
    apply_3d_button_properties(btn_ok);
}

static void execute_pin_action(const PendingPinAction &action) {
    switch (action.action) {
    case PinAction::OpenScheduleEditor:
        open_sched_editor_authorized(static_cast<ScheduleDevice>(action.value));
        break;
    case PinAction::OpenHeater:
        open_heater_subpage_authorized();
        break;
    case PinAction::OpenPh:
        open_ph_subpage_authorized();
        break;
    case PinAction::OpenTimePicker:
        open_time_picker_authorized();
        break;
    case PinAction::OpenDatePicker:
        open_date_picker_authorized();
        break;
    case PinAction::StartOta:
        start_ota_authorized();
        break;
    case PinAction::Restart:
        restart_authorized();
        break;
    case PinAction::LightSleep:
        light_sleep_authorized();
        break;
    case PinAction::DeepSleep:
        deep_sleep_authorized();
        break;
    case PinAction::Hibernation:
        hibernation_authorized();
        break;
    case PinAction::FactoryReset:
        factory_reset_authorized();
        break;
    case PinAction::ToggleModemSleep:
        apply_modem_sleep_authorized(action.state);
        break;
    case PinAction::ToggleHardware:
        apply_hardware_toggle_authorized(static_cast<HardwareToggle>(action.value), action.state);
        break;
    case PinAction::ToggleDevMode:
        apply_dev_mode_authorized(action.state);
        break;
    case PinAction::TogglePhSensor:
        apply_ph_sensor_authorized(action.state);
        break;
    case PinAction::StartCalibration:
        open_calibration_wizard_authorized(static_cast<int>(action.value));
        break;
    default:
        break;
    }
}


static void rebuild_gui_tree_async_cb(void *user_data) {
    LV_UNUSED(user_data);
    gui_rebuild_pending = false;
    rebuild_gui_tree_for_theme();
}

static void request_gui_rebuild_async() {
    if (gui_rebuild_pending) {
        return;
    }
    gui_rebuild_pending = true;
    if (lv_async_call(rebuild_gui_tree_async_cb, nullptr) != LV_RES_OK) {
        gui_rebuild_pending = false;
        rebuild_gui_tree_for_theme();
    }
}

static void make_object_clickable(lv_obj_t *obj, lv_event_cb_t cb, void *user_data) {
    if (obj == nullptr || cb == nullptr) {
        return;
    }
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, user_data);
}

static bool hardware_toggle_metadata_for_switch(lv_obj_t *sw, HardwareToggle &toggle, bool &old_state) {
    if (sw == nullptr) {
        return false;
    }

    if (sw == hw_aerator_sw) {
        toggle = HardwareToggle::Aerator;
        old_state = cfg.enableAerator;
    } else if (sw == hw_ec_sw) {
        toggle = HardwareToggle::Ec;
        old_state = cfg.enableEc;
    } else if (sw == hw_co2_sw) {
        toggle = HardwareToggle::Co2;
        old_state = cfg.enableCo2;
    } else if (sw == hw_water_level_sw) {
        toggle = HardwareToggle::WaterLevel;
        old_state = cfg.enableWaterLevel;
    } else if (sw == hw_leak_sw) {
        toggle = HardwareToggle::Leak;
        old_state = cfg.enableLeak;
    } else if (sw == hw_flow_sw) {
        toggle = HardwareToggle::Flow;
        old_state = cfg.enableFlow;
    } else if (sw == hw_heater_sw) {
        toggle = HardwareToggle::Heater;
        old_state = cfg.enableHeater;
    } else {
        return false;
    }

    return true;
}

static lv_event_cb_t hardware_detail_cb_for_switch(lv_obj_t *sw) {
    if (sw == hw_heater_sw) {
        return open_heater_subpage_cb;
    }
    if (sw == hw_co2_sw) {
        return open_co2_subpage_cb;
    }
    if (sw == hw_ec_sw) {
        return open_ec_subpage_cb;
    }
    if (sw == hw_water_level_sw) {
        return open_water_subpage_cb;
    }
    if (sw == hw_leak_sw) {
        return open_leak_subpage_cb;
    }
    if (sw == hw_flow_sw) {
        return open_flow_subpage_cb;
    }
    return nullptr;
}

static HardwareToggle hardware_toggle_for_row(uint8_t row) {
    switch (row) {
    case 0: return HardwareToggle::Heater;
    case 1: return HardwareToggle::Aerator;
    case 2: return HardwareToggle::Co2;
    case 3: return HardwareToggle::Ec;
    case 4: return HardwareToggle::WaterLevel;
    case 5: return HardwareToggle::Leak;
    default: return HardwareToggle::Flow;
    }
}

static bool hardware_toggle_current_state(HardwareToggle toggle) {
    switch (toggle) {
    case HardwareToggle::Heater: return cfg.enableHeater;
    case HardwareToggle::Aerator: return cfg.enableAerator;
    case HardwareToggle::Co2: return cfg.enableCo2;
    case HardwareToggle::Ec: return cfg.enableEc;
    case HardwareToggle::WaterLevel: return cfg.enableWaterLevel;
    case HardwareToggle::Leak: return cfg.enableLeak;
    case HardwareToggle::Flow: return cfg.enableFlow;
    }
    return false;
}

static char hw_state_texts[7][4] = {};
static const char *hw_matrix_map[] = {
    "Grzalka", hw_state_texts[0], "\n",
    "Aerator", hw_state_texts[1], "\n",
    "CO2", hw_state_texts[2], "\n",
    "EC", hw_state_texts[3], "\n",
    "Poziom", hw_state_texts[4], "\n",
    "Wyciek", hw_state_texts[5], "\n",
    "Przeplyw", hw_state_texts[6],
    ""
};

static void update_hardware_matrix_labels() {
    uint8_t active = 0;
    for (uint8_t row = 0; row < 7; ++row) {
        const bool enabled = hardware_toggle_current_state(hardware_toggle_for_row(row));
        if (enabled) {
            ++active;
        }
        snprintf(hw_state_texts[row], sizeof(hw_state_texts[row]), "%s", enabled ? "ON" : "OFF");
    }
    if (hw_summary_lbl != nullptr && lv_obj_is_valid(hw_summary_lbl)) {
        lv_label_set_text_fmt(hw_summary_lbl, "Aktywne: %u/7 | nazwa = szczegoly, ON/OFF = PIN", static_cast<unsigned>(active));
    }
    if (hw_matrix != nullptr && lv_obj_is_valid(hw_matrix)) {
        lv_btnmatrix_set_map(hw_matrix, hw_matrix_map);
    }
}

static void open_hardware_detail_for_row(uint8_t row, lv_event_t *e) {
    switch (row) {
    case 0:
        open_heater_subpage_cb(e);
        break;
    case 2:
        open_co2_subpage_cb(e);
        break;
    case 3:
        open_ec_subpage_cb(e);
        break;
    case 4:
        open_water_subpage_cb(e);
        break;
    case 5:
        open_leak_subpage_cb(e);
        break;
    case 6:
        open_flow_subpage_cb(e);
        break;
    default:
        pin_guard_execute_or_prompt(PinAction::ToggleHardware, static_cast<intptr_t>(hardware_toggle_for_row(row)),
                                    !hardware_toggle_current_state(hardware_toggle_for_row(row)));
        break;
    }
}

static void hardware_matrix_cb(lv_event_t *e) {
    lv_obj_t *matrix = lv_event_get_target(e);
    if (matrix == nullptr || !lv_obj_is_valid(matrix)) {
        return;
    }
    const uint16_t btn_id = lv_btnmatrix_get_selected_btn(matrix);
    lv_btnmatrix_set_selected_btn(matrix, LV_BTNMATRIX_BTN_NONE);
    if (btn_id == LV_BTNMATRIX_BTN_NONE || btn_id >= 14) {
        return;
    }

    play_system_sound(SoundType::Click);
    const uint8_t row = static_cast<uint8_t>(btn_id / 2);
    const bool state_column = (btn_id % 2) == 1;
    if (!state_column) {
        open_hardware_detail_for_row(row, e);
        return;
    }

    const HardwareToggle toggle = hardware_toggle_for_row(row);
    pin_guard_execute_or_prompt(PinAction::ToggleHardware, static_cast<intptr_t>(toggle),
                                !hardware_toggle_current_state(toggle));
}

static void hardware_toggle_request(lv_obj_t *sw, bool state) {
    HardwareToggle toggle = HardwareToggle::Heater;
    bool old_state = false;
    if (!hardware_toggle_metadata_for_switch(sw, toggle, old_state)) {
        return;
    }

    if (!pin_guard_execute_or_prompt(PinAction::ToggleHardware, static_cast<intptr_t>(toggle), state)) {
        set_checked(sw, old_state);
    }
}

static void hardware_card_click_handler(lv_event_t *e) {
    lv_obj_t *sw = static_cast<lv_obj_t *>(lv_event_get_user_data(e));
    if (sw == nullptr || !lv_obj_is_valid(sw)) {
        return;
    }

    lv_event_cb_t detail_cb = hardware_detail_cb_for_switch(sw);
    if (detail_cb != nullptr) {
        detail_cb(e);
        return;
    }

    play_system_sound(SoundType::Click);
    hardware_toggle_request(sw, !lv_obj_has_state(sw, LV_STATE_CHECKED));
}

static void hw_switch_handler(lv_event_t *e) {
    play_system_sound(SoundType::Click);
    lv_obj_t *sw = lv_event_get_target(e);
    const bool state = lv_obj_has_state(sw, LV_STATE_CHECKED);
    hardware_toggle_request(sw, state);
}

static void apply_hardware_toggle_authorized(HardwareToggle toggle, bool enabled) {
    switch (toggle) {
    case HardwareToggle::Heater:
        cfg.enableHeater = enabled;
        if (!enabled) {
            cfg.heaterMode = static_cast<uint8_t>(HeaterMode::Off);
            runtime.heaterOn = false;
        }
        break;
    case HardwareToggle::Aerator:
        cfg.enableAerator = enabled;
        if (!enabled) {
            cfg.airMode = static_cast<uint8_t>(ScheduleMode::AlwaysOff);
            runtime.airOn = false;
        }
        break;
    case HardwareToggle::Co2:
        cfg.enableCo2 = enabled;
        break;
    case HardwareToggle::Ec:
        cfg.enableEc = enabled;
        break;
    case HardwareToggle::WaterLevel:
        cfg.enableWaterLevel = enabled;
        break;
    case HardwareToggle::Leak:
        cfg.enableLeak = enabled;
        break;
    case HardwareToggle::Flow:
        cfg.enableFlow = enabled;
        break;
    }
    
    gui_app_save_settings();
    apply_mcp_outputs();
    update_hardware_matrix_labels();
    current_subpage = ActiveSubpage::Hardware;
    request_gui_rebuild_async();
}

struct CalibWizardData {
    lv_obj_t *bg_overlay;
    lv_obj_t *step_lbl;
    lv_obj_t *desc_lbl;
    lv_obj_t *btn_next;
    lv_obj_t *val_lbl;
    int step;
    int type; // 0 = pH, 1 = EC
    int16_t ph_low_raw;
    int16_t ph_high_raw;
    int16_t ec_reference_raw;
};

static void update_calibration_value_label() {
    if (calib_value_lbl == nullptr) {
        return;
    }
    if (calib_active_type == 0) {
        if (sensor_debug.adcPresent && sensor_debug.phValid) {
            lv_label_set_text_fmt(calib_value_lbl, "ADS A0: %d  %.3f V",
                                  static_cast<int>(sensor_debug.phRaw),
                                  sensor_debug.phVoltage);
        } else {
            lv_label_set_text(calib_value_lbl, "ADS A0: brak odczytu");
        }
    } else if (calib_active_type == 1) {
        if (sensor_debug.adcPresent && sensor_debug.ecValid) {
            lv_label_set_text_fmt(calib_value_lbl, "ADS A1: %d  %.3f V",
                                  static_cast<int>(sensor_debug.ecRaw),
                                  sensor_debug.ecVoltage);
        } else {
            lv_label_set_text(calib_value_lbl, "ADS A1: brak odczytu");
        }
    }
}

static void calib_wizard_delete_data_cb(lv_event_t *e) {
    CalibWizardData *data = static_cast<CalibWizardData*>(lv_event_get_user_data(e));
    delete data;
}

static void close_calib_wizard(CalibWizardData *data) {
    if (data == nullptr || data->bg_overlay == nullptr) {
        return;
    }
    if (calib_value_lbl == data->val_lbl) {
        calib_value_lbl = nullptr;
        calib_active_type = -1;
    }
    lv_obj_t *overlay = data->bg_overlay;
    data->bg_overlay = nullptr;
    if (overlay != nullptr && lv_obj_is_valid(overlay)) {
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
        delete_obj_async(overlay);
    }
}

static void calib_wizard_close_cb(lv_event_t *e) {
    play_system_sound(SoundType::Click);
    CalibWizardData *data = static_cast<CalibWizardData*>(lv_event_get_user_data(e));
    close_calib_wizard(data);
}

static void calib_wizard_next_cb(lv_event_t *e) {
    play_system_sound(SoundType::Click);
    CalibWizardData *data = static_cast<CalibWizardData*>(lv_event_get_user_data(e));
    if (data == nullptr || data->bg_overlay == nullptr) {
        return;
    }
    data->step++;

    if (data->type == 0) { // pH
        if (data->step == 1) {
            lv_label_set_text(data->step_lbl, "Krok 1/2: pH 4.01");
            lv_label_set_text(data->desc_lbl, "Umiesc sonde w buforze 4.01.\nPoczekaj na odczyt i kliknij Dalej.");
            update_calibration_value_label();
        } else if (data->step == 2) {
            if (!sensor_debug.phValid) {
                data->step = 1;
                lv_label_set_text(
                    data->desc_lbl,
                    "Brak stabilnego odczytu pH 4.01.\nSprawdz sonde i sprobuj ponownie.");
                lv_obj_set_style_text_color(
                    data->desc_lbl, lv_color_make(239, 68, 68), 0);
                return;
            }
            data->ph_low_raw = sensor_debug.phRaw;
            lv_label_set_text(data->step_lbl, "Krok 2/2: pH 6.86");
            lv_label_set_text(data->desc_lbl, "Umiesc sonde w buforze 6.86.\nPoczekaj na odczyt i kliknij Zapisz.");
            lv_obj_set_style_text_color(
                data->desc_lbl, theme_text_main(), 0);
            update_calibration_value_label();
            lv_label_set_text(lv_obj_get_child(data->btn_next, 0), "Zapisz");
        } else if (data->step == 3) {
            if (!sensor_debug.phValid) {
                data->step = 2;
                lv_label_set_text(
                    data->desc_lbl,
                    "Brak stabilnego odczytu pH 6.86.\nSprawdz sonde i sprobuj ponownie.");
                lv_obj_set_style_text_color(
                    data->desc_lbl, lv_color_make(239, 68, 68), 0);
                return;
            }
            data->ph_high_raw = sensor_debug.phRaw;
            if (!sensor_calibration_store_save_ph(
                    data->ph_low_raw,
                    4.01f,
                    data->ph_high_raw,
                    6.86f)) {
                data->step = 2;
                lv_label_set_text(
                    data->desc_lbl,
                    "Punkty sa zbyt blisko lub zapis NVS nie powiodl sie.");
                lv_obj_set_style_text_color(
                    data->desc_lbl, lv_color_make(239, 68, 68), 0);
                add_gui_log("Kalibracja pH: odrzucone punkty lub blad NVS", true);
                return;
            }
            add_gui_log("Kalibracja pH zapisana", false);
            show_save_toast("Kalibracja pH zapisana");
            close_calib_wizard(data);
        }
    } else { // EC
        if (data->step == 1) {
            lv_label_set_text(data->step_lbl, "Krok 1/1: EC 1413");
            lv_label_set_text(data->desc_lbl, "Umiesc sonde w plynie 1413.\nPoczekaj na odczyt i kliknij Zapisz.");
            update_calibration_value_label();
            lv_label_set_text(lv_obj_get_child(data->btn_next, 0), "Zapisz");
        } else if (data->step == 2) {
            if (!sensor_debug.ecValid) {
                data->step = 1;
                lv_label_set_text(
                    data->desc_lbl,
                    "Brak stabilnego odczytu EC.\nSprawdz sonde i sprobuj ponownie.");
                lv_obj_set_style_text_color(
                    data->desc_lbl, lv_color_make(239, 68, 68), 0);
                return;
            }
            data->ec_reference_raw = sensor_debug.ecRaw;
            if (!sensor_calibration_store_save_ec(
                    data->ec_reference_raw,
                    1413.0f,
                    0.019f,
                    25.0f)) {
                data->step = 1;
                lv_label_set_text(
                    data->desc_lbl,
                    "Niepoprawny punkt EC lub blad zapisu NVS.");
                lv_obj_set_style_text_color(
                    data->desc_lbl, lv_color_make(239, 68, 68), 0);
                add_gui_log("Kalibracja EC: punkt odrzucony lub blad NVS", true);
                return;
            }
            add_gui_log("Kalibracja EC zapisana", false);
            show_save_toast("Kalibracja EC zapisana");
            close_calib_wizard(data);
        }
    }
}

static void open_calibration_wizard_cb(lv_event_t *e) {
    LV_UNUSED(e);
    int type = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    pin_guard_execute_or_prompt(PinAction::StartCalibration, static_cast<intptr_t>(type), false);
}

static void open_calibration_wizard_authorized(int type) {
    if (!ensure_runtime_ui_heap(type == 0 ? "CalibrationPH" : "CalibrationEC",
                                UI_RUNTIME_MODAL_MIN_FREE,
                                UI_RUNTIME_BIGGEST_MIN)) {
        return;
    }
    play_system_sound(SoundType::Click);
    lv_obj_t *bg_overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(bg_overlay, 320, 240);
    lv_obj_set_pos(bg_overlay, 0, 0);
    lv_obj_set_style_bg_color(bg_overlay, theme_screen_bg(), 0);
    lv_obj_set_style_border_width(bg_overlay, 0, 0);
    lv_obj_set_style_radius(bg_overlay, 0, 0);
    lv_obj_clear_flag(bg_overlay, LV_OBJ_FLAG_SCROLLABLE);

    CalibWizardData *data = new (std::nothrow) CalibWizardData();
    if (data == nullptr) {
        lv_obj_del(bg_overlay);
        show_save_toast("Brak pamieci RAM");
        return;
    }
    data->bg_overlay = bg_overlay;
    data->step = 0;
    data->type = type;
    data->ph_low_raw = 0;
    data->ph_high_raw = 0;
    data->ec_reference_raw = 0;
    lv_obj_add_event_cb(bg_overlay, calib_wizard_delete_data_cb, LV_EVENT_DELETE, data);

    lv_obj_t *header = lv_obj_create(bg_overlay);
    lv_obj_set_size(header, 320, 35);
    lv_obj_set_pos(header, 0, 0);
    style_panel(header, theme_header_bg(), theme_card_border(), 0);
    lv_obj_set_style_pad_all(header, 0, 0);

    lv_obj_t *title = create_label(header, type == 0 ? "Kalibracja pH" : "Kalibracja EC", theme_text_main(), &lv_font_montserrat_14);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *btn_cancel = create_button(header, "Anuluj", 70, 26, lv_color_make(239, 68, 68), calib_wizard_close_cb, data);
    lv_obj_align(btn_cancel, LV_ALIGN_LEFT_MID, 5, 0);
    apply_3d_button_properties(btn_cancel);

    lv_obj_t *card = create_card(bg_overlay, 290, 140, 15, 45);
    
    data->step_lbl = create_label(card, "Krok 0: Przygotowanie", lv_color_make(6, 182, 212), &lv_font_montserrat_14);
    lv_obj_align(data->step_lbl, LV_ALIGN_TOP_MID, 0, 5);
    
    data->desc_lbl = create_label(card, "Wyplucz sonde w wodzie demineralizowanej.\nNacisnij Dalej aby rozpoczac.", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_set_style_text_align(data->desc_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(data->desc_lbl, LV_ALIGN_CENTER, 0, -10);

    data->val_lbl = create_label(card, "", lv_color_make(16, 185, 129), &lv_font_montserrat_16);
    lv_obj_align(data->val_lbl, LV_ALIGN_CENTER, 0, 20);
    calib_value_lbl = data->val_lbl;
    calib_active_type = type;
    update_calibration_value_label();

    data->btn_next = create_button(bg_overlay, "Dalej", 120, 32, lv_color_make(16, 185, 129), calib_wizard_next_cb, data);
    lv_obj_align(data->btn_next, LV_ALIGN_BOTTOM_MID, 0, -15);
    apply_3d_button_properties(data->btn_next);
}

static void build_hardware_subpage() {
    subpage_hardware = create_subpage("Sprzet / Moduly", nullptr, nullptr);

    hw_summary_lbl = create_label(subpage_hardware, "", theme_text_muted(), &lv_font_montserrat_12);
    lv_obj_set_width(hw_summary_lbl, 300);
    lv_label_set_long_mode(hw_summary_lbl, LV_LABEL_LONG_CLIP);
    lv_obj_align(hw_summary_lbl, LV_ALIGN_TOP_MID, 0, 38);

    // Runtime heap on CYD is heavily fragmented when this page is opened from
    // Sys. A single btnmatrix replaces seven switch cards and avoids the
    // lv_btn_create crash seen with ~3 KB largest free block.
    hw_matrix = lv_btnmatrix_create(subpage_hardware);
    if (hw_matrix == nullptr) {
        Serial.println("UI_HW: matrix allocation failed");
        return;
    }
    update_hardware_matrix_labels();
    lv_obj_set_size(hw_matrix, 300, 160);
    lv_obj_align(hw_matrix, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_btnmatrix_set_one_checked(hw_matrix, false);
    for (uint8_t row = 0; row < 7; ++row) {
        lv_btnmatrix_set_btn_width(hw_matrix, static_cast<uint16_t>(row * 2), 3);
        lv_btnmatrix_set_btn_width(hw_matrix, static_cast<uint16_t>(row * 2 + 1), 1);
    }
    lv_obj_add_event_cb(hw_matrix, hardware_matrix_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_set_style_bg_color(hw_matrix, resolve_bg_color(lv_color_make(20, 26, 40)), LV_PART_MAIN);
    lv_obj_set_style_border_color(hw_matrix, theme_card_border(), LV_PART_MAIN);
    lv_obj_set_style_border_width(hw_matrix, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(hw_matrix, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(hw_matrix, 4, LV_PART_MAIN);
    lv_obj_set_style_text_font(hw_matrix, &lv_font_montserrat_12, LV_PART_ITEMS);
    lv_obj_set_style_text_color(hw_matrix, theme_text_main(), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(hw_matrix, theme_matrix_item_bg(), LV_PART_ITEMS);
    const lv_style_selector_t hw_matrix_pressed_selector =
        static_cast<lv_style_selector_t>(static_cast<uint32_t>(LV_PART_ITEMS) | static_cast<uint32_t>(LV_STATE_PRESSED));
    lv_obj_set_style_bg_color(hw_matrix, theme_matrix_pressed_bg(), hw_matrix_pressed_selector);
    lv_obj_set_style_border_width(hw_matrix, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(hw_matrix, theme_card_border(), LV_PART_ITEMS);
    lv_obj_set_style_radius(hw_matrix, 5, LV_PART_ITEMS);
    Serial.printf("UI_HW: hardware matrix ready obj=%p heap_free=%lu heap_largest=%lu\n",
                  static_cast<void *>(hw_matrix),
                  static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_8BIT)),
                  static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
}



static void build_co2_subpage() {
    subpage_co2 = create_subpage("CO2");
    
    lv_obj_t *list = lv_obj_create(subpage_co2);
    lv_obj_set_size(list, 312, 196);
    lv_obj_set_pos(list, 4, 34);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    
    lv_obj_t *state_card = create_card(list, 300, 62, 0, 0);
    lv_obj_t *state_title = create_label(state_card, "Elektrozawor CO2", lv_color_make(20, 184, 166), &lv_font_montserrat_12);
    lv_obj_align(state_title, LV_ALIGN_TOP_LEFT, 6, 2);
    co2_state_lbl = create_label(state_card, "Stan: --", theme_text_main(), &lv_font_montserrat_14);
    lv_obj_align(co2_state_lbl, LV_ALIGN_LEFT_MID, 6, 8);
    co2_mcp_lbl = create_label(state_card, "MCP: --", theme_text_muted(), &lv_font_montserrat_12);
    lv_obj_align(co2_mcp_lbl, LV_ALIGN_BOTTOM_LEFT, 6, -4);

    lv_obj_t *ph_card = create_card(list, 300, 56, 0, 0);
    lv_obj_t *ph_title = create_label(ph_card, "Warunek pH", lv_color_make(168, 85, 247), &lv_font_montserrat_12);
    lv_obj_align(ph_title, LV_ALIGN_TOP_LEFT, 6, 2);
    co2_ph_lbl = create_label(ph_card, "pH: --", theme_text_main(), &lv_font_montserrat_14);
    lv_obj_align(co2_ph_lbl, LV_ALIGN_LEFT_MID, 6, 8);

    lv_obj_t *info_card = create_card(list, 300, 52, 0, 0);
    lv_obj_t *info = create_label(info_card, "Kanal: MCP23017 CH_CO2.\nBrak odczytu pH wymusza stan bezpieczny OFF.", theme_text_muted(), &lv_font_montserrat_12);
    lv_obj_set_width(info, 286);
    lv_label_set_long_mode(info, LV_LABEL_LONG_WRAP);
    lv_obj_align(info, LV_ALIGN_LEFT_MID, 6, 0);
}

static void build_ec_subpage() {
    subpage_ec = create_subpage("Czujnik EC");
    
    lv_obj_t *card = create_card(subpage_ec, 300, 92, 10, 42);
    lv_obj_t *lbl = create_label(card, "Sonda EC", lv_color_make(6, 182, 212), &lv_font_montserrat_14);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 10, 8);
    
    ec_value_lbl = create_label(card, "EC: --", lv_color_make(6, 182, 212), &lv_font_montserrat_24);
    lv_obj_align(ec_value_lbl, LV_ALIGN_LEFT_MID, 10, 8);

    ec_raw_lbl = create_label(card, "ADS1115 A1: --", theme_text_muted(), &lv_font_montserrat_12);
    lv_obj_align(ec_raw_lbl, LV_ALIGN_BOTTOM_LEFT, 10, -8);

    lv_obj_t *ec_calib_btn = create_button(subpage_ec, "Kalibruj EC", 120, 32, lv_color_make(6, 182, 212), open_calibration_wizard_cb, reinterpret_cast<void*>(static_cast<intptr_t>(1)));
    lv_obj_align(ec_calib_btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    apply_3d_button_properties(ec_calib_btn);
}

static void build_water_subpage() {
    subpage_water = create_subpage("Poziom wody");

    lv_obj_t *card = create_card(subpage_water, 300, 100, 10, 50);
    lv_obj_t *title = create_label(card, "Czujnik poziomu", lv_color_make(14, 165, 233), &lv_font_montserrat_14);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 8);
    water_state_lbl = create_label(card, "Stan: --", theme_text_main(), &lv_font_montserrat_24);
    lv_obj_align(water_state_lbl, LV_ALIGN_LEFT_MID, 10, 8);
    lv_obj_t *detail = create_label(card, "MCP23017 CH_WATER_LEVEL", theme_text_muted(), &lv_font_montserrat_12);
    lv_obj_align(detail, LV_ALIGN_BOTTOM_LEFT, 10, -8);
}

static void build_leak_subpage() {
    subpage_leak = create_subpage("Czujnik wycieku");

    lv_obj_t *card = create_card(subpage_leak, 300, 100, 10, 50);
    lv_obj_t *title = create_label(card, "Alarm wycieku", lv_color_make(239, 68, 68), &lv_font_montserrat_14);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 8);
    leak_state_lbl = create_label(card, "Stan: --", theme_text_main(), &lv_font_montserrat_24);
    lv_obj_align(leak_state_lbl, LV_ALIGN_LEFT_MID, 10, 8);
    lv_obj_t *detail = create_label(card, "MCP23017 CH_LEAK", theme_text_muted(), &lv_font_montserrat_12);
    lv_obj_align(detail, LV_ALIGN_BOTTOM_LEFT, 10, -8);
}

static void build_flow_subpage() {
    subpage_flow = create_subpage("Przeplyw");

    lv_obj_t *card = create_card(subpage_flow, 300, 100, 10, 50);
    lv_obj_t *title = create_label(card, "Czujnik przeplywu", lv_color_make(34, 197, 94), &lv_font_montserrat_14);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 8);
    flow_state_lbl = create_label(card, "Stan: --", theme_text_main(), &lv_font_montserrat_24);
    lv_obj_align(flow_state_lbl, LV_ALIGN_LEFT_MID, 10, 8);
    lv_obj_t *detail = create_label(card, "MCP23017 CH_FLOW_PULSE", theme_text_muted(), &lv_font_montserrat_12);
    lv_obj_align(detail, LV_ALIGN_BOTTOM_LEFT, 10, -8);
}

static void build_subpages(ActiveSubpage target) {
    if (target == ActiveSubpage::Wifi) {
        subpage_wifi = create_subpage("WiFi");

        wifi_main_panel = lv_obj_create(subpage_wifi);
        lv_obj_set_size(wifi_main_panel, 320, 210);
        lv_obj_set_pos(wifi_main_panel, 0, 30);
        lv_obj_set_style_bg_opa(wifi_main_panel, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(wifi_main_panel, 0, 0);
        lv_obj_set_style_pad_all(wifi_main_panel, 0, 0);
        lv_obj_clear_flag(wifi_main_panel, LV_OBJ_FLAG_SCROLLABLE);

        wifi_info_card = create_card(wifi_main_panel, 304, 150, 8, 4);
        lv_obj_set_style_pad_all(wifi_info_card, 0, 0);
        lv_obj_clear_flag(wifi_info_card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(wifi_info_card, theme_card_bg(), 0);
        lv_obj_set_style_border_color(wifi_info_card, lv_color_make(239, 68, 68), 0);
        lv_obj_set_style_border_width(wifi_info_card, 2, 0);

        lv_obj_t *wifi_title_lbl = create_label(wifi_info_card, LV_SYMBOL_WIFI "  WiFi", lv_color_make(14, 165, 233), &lv_font_montserrat_14);
        lv_obj_set_width(wifi_title_lbl, 110);
        lv_label_set_long_mode(wifi_title_lbl, LV_LABEL_LONG_DOT);
        lv_obj_align(wifi_title_lbl, LV_ALIGN_TOP_LEFT, 12, 10);

        wifi_mode_lbl = create_label(wifi_info_card, "ROZLACZONY", lv_color_make(239, 68, 68), &lv_font_montserrat_14);
        lv_obj_set_width(wifi_mode_lbl, 160);
        lv_label_set_long_mode(wifi_mode_lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(wifi_mode_lbl, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(wifi_mode_lbl, LV_ALIGN_TOP_RIGHT, -12, 10);

        wifi_status_message_lbl = create_label(wifi_info_card, "Brak polaczenia WiFi", theme_text_muted(), &lv_font_montserrat_12);
        lv_obj_set_width(wifi_status_message_lbl, 280);
        lv_label_set_long_mode(wifi_status_message_lbl, LV_LABEL_LONG_DOT);
        lv_obj_align(wifi_status_message_lbl, LV_ALIGN_TOP_LEFT, 12, 34);

        wifi_ssid_lbl = create_label(wifi_info_card, LV_SYMBOL_WIFI "  SSID: --", theme_text_main(), &lv_font_montserrat_12);
        lv_obj_set_width(wifi_ssid_lbl, 280);
        lv_label_set_long_mode(wifi_ssid_lbl, LV_LABEL_LONG_DOT);
        lv_obj_align(wifi_ssid_lbl, LV_ALIGN_TOP_LEFT, 12, 62);

        wifi_ip_lbl = create_label(wifi_info_card, LV_SYMBOL_RIGHT "  IP: --", theme_text_muted(), &lv_font_montserrat_12);
        lv_obj_set_width(wifi_ip_lbl, 172);
        lv_label_set_long_mode(wifi_ip_lbl, LV_LABEL_LONG_DOT);
        lv_obj_align(wifi_ip_lbl, LV_ALIGN_TOP_LEFT, 12, 88);

        wifi_rssi_lbl = create_label(wifi_info_card, "RSSI: --", theme_text_muted(), &lv_font_montserrat_12);
        lv_obj_set_width(wifi_rssi_lbl, 96);
        lv_label_set_long_mode(wifi_rssi_lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(wifi_rssi_lbl, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(wifi_rssi_lbl, LV_ALIGN_TOP_RIGHT, -12, 88);

        wifi_mac_lbl = create_label(wifi_info_card, "Portal: --", lv_color_make(100, 116, 139), &lv_font_montserrat_12);
        lv_obj_set_width(wifi_mac_lbl, 280);
        lv_label_set_long_mode(wifi_mac_lbl, LV_LABEL_LONG_DOT);
        lv_obj_align(wifi_mac_lbl, LV_ALIGN_BOTTOM_LEFT, 12, -12);

        btn_sta = create_button(wifi_main_panel, LV_SYMBOL_WIFI "  Sieci", 146, 36, lv_color_make(14, 165, 233), btn_sta_handler, nullptr);
        lv_obj_set_pos(btn_sta, 8, 164);
        apply_colored_3d_button(btn_sta, lv_color_make(14, 165, 233), 126);

        btn_ota = create_button(wifi_main_panel, LV_SYMBOL_UPLOAD "  OTA", 146, 36, lv_color_make(20, 184, 166), btn_ota_handler, nullptr);
        lv_obj_set_pos(btn_ota, 166, 164);
        apply_colored_3d_button(btn_ota, lv_color_make(20, 184, 166), 126);

        btn_disconnect = create_button(wifi_main_panel, LV_SYMBOL_CLOSE "  Rozlacz", 146, 36, lv_color_make(239, 68, 68), btn_wifi_disc_handler, nullptr);
        lv_obj_set_pos(btn_disconnect, 87, 164);
        lv_obj_add_flag(btn_disconnect, LV_OBJ_FLAG_HIDDEN);
        apply_colored_3d_button(btn_disconnect, lv_color_make(239, 68, 68), 126);

        wifi_sta_panel = lv_obj_create(subpage_wifi);
        lv_obj_set_size(wifi_sta_panel, 320, 210);
        lv_obj_set_pos(wifi_sta_panel, 0, 30);
        style_panel(wifi_sta_panel, theme_screen_bg(), theme_screen_bg(), 0);
        lv_obj_set_style_pad_all(wifi_sta_panel, 0, 0);
        lv_obj_add_flag(wifi_sta_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(wifi_sta_panel, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *sta_header = create_heading_card(wifi_sta_panel, 304, 44, 8, 4,
                                                   "Sieci WiFi",
                                                   "Skanuj i wybierz SSID",
                                                   lv_color_make(14, 165, 233));
        LV_UNUSED(sta_header);

        sta_list_obj = lv_list_create(wifi_sta_panel);
        lv_obj_set_size(sta_list_obj, 304, 116);
        lv_obj_set_pos(sta_list_obj, 8, 52);
        lv_obj_set_style_bg_color(sta_list_obj, theme_card_bg(), 0);
        lv_obj_set_style_border_color(sta_list_obj, theme_card_border(), 0);
        lv_obj_set_style_border_width(sta_list_obj, 1, 0);
        lv_obj_set_style_radius(sta_list_obj, 8, 0);
        lv_obj_set_style_pad_all(sta_list_obj, 4, 0);

        lv_obj_t *list_btn = lv_list_add_btn(sta_list_obj, LV_SYMBOL_WIFI, "Skanuj sieci");
        style_wifi_list_item(list_btn, lv_color_make(14, 165, 233));
        lv_obj_add_event_cb(list_btn, btn_sta_handler, LV_EVENT_CLICKED, nullptr);

        lv_obj_t *back_sta_btn = create_button(wifi_sta_panel, LV_SYMBOL_LEFT "  Wstecz", 146, 30, lv_color_make(30, 41, 59), cancel_sta_cb, nullptr);
        lv_obj_set_pos(back_sta_btn, 8, 174);
        apply_colored_3d_button(back_sta_btn, lv_color_make(30, 41, 59), 126);

        lv_obj_t *rescan_btn = create_button(wifi_sta_panel, LV_SYMBOL_REFRESH "  Skanuj", 146, 30, lv_color_make(14, 165, 233), btn_sta_handler, nullptr);
        lv_obj_set_pos(rescan_btn, 166, 174);
        apply_colored_3d_button(rescan_btn, lv_color_make(14, 165, 233), 126);

        wifi_pwd_panel = lv_obj_create(subpage_wifi);
        lv_obj_set_size(wifi_pwd_panel, 320, 210);
        lv_obj_set_pos(wifi_pwd_panel, 0, 30);
        style_panel(wifi_pwd_panel, theme_screen_bg(), theme_screen_bg(), 0);
        lv_obj_set_style_pad_all(wifi_pwd_panel, 0, 0);
        lv_obj_add_flag(wifi_pwd_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(wifi_pwd_panel, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *pwd_header = create_card(wifi_pwd_panel, 304, 78, 8, 4);
        lv_obj_set_style_pad_all(pwd_header, 0, 0);
        lv_obj_set_style_border_color(pwd_header, lv_color_make(14, 165, 233), 0);
        lv_obj_clear_flag(pwd_header, LV_OBJ_FLAG_SCROLLABLE);

        wifi_pwd_title_lbl = create_label(pwd_header, "Haslo WiFi: --", theme_text_main(), &lv_font_montserrat_12);
        lv_obj_set_width(wifi_pwd_title_lbl, 238);
        lv_label_set_long_mode(wifi_pwd_title_lbl, LV_LABEL_LONG_DOT);
        lv_obj_align(wifi_pwd_title_lbl, LV_ALIGN_TOP_LEFT, 12, 10);

        lv_obj_t *back_pwd_btn = create_button(pwd_header, LV_SYMBOL_CLOSE, 30, 28, lv_color_make(71, 85, 105), cancel_pwd_cb, nullptr);
        lv_obj_align(back_pwd_btn, LV_ALIGN_TOP_RIGHT, -10, 7);
        apply_colored_3d_button(back_pwd_btn, lv_color_make(71, 85, 105), 22);

        wifi_pwd_ta = lv_textarea_create(pwd_header);
        lv_obj_set_size(wifi_pwd_ta, 284, 30);
        lv_obj_align(wifi_pwd_ta, LV_ALIGN_BOTTOM_MID, 0, -8);
        lv_textarea_set_one_line(wifi_pwd_ta, true);
        lv_textarea_set_password_mode(wifi_pwd_ta, true);
        lv_textarea_set_max_length(wifi_pwd_ta, WIFI_PASSWORD_MAX_LEN);
        lv_obj_set_style_bg_color(wifi_pwd_ta, resolve_bg_color(lv_color_make(15, 23, 42)), 0);
        lv_obj_set_style_border_color(wifi_pwd_ta, lv_color_make(14, 165, 233), 0);
        lv_obj_set_style_border_width(wifi_pwd_ta, 2, 0);
        lv_obj_set_style_text_color(wifi_pwd_ta, theme_text_main(), 0);

        wifi_pwd_kb = lv_keyboard_create(wifi_pwd_panel);
        lv_obj_set_size(wifi_pwd_kb, 320, 126);
        lv_obj_align(wifi_pwd_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_keyboard_set_textarea(wifi_pwd_kb, wifi_pwd_ta);
        lv_obj_add_event_cb(wifi_pwd_kb, [](lv_event_t *e) {
            lv_event_code_t code = lv_event_get_code(e);
            if (code == LV_EVENT_READY) {
                keyboard_ready_cb(e);
            } else if (code == LV_EVENT_CANCEL) {
                cancel_pwd_cb(e);
            }
        }, LV_EVENT_ALL, nullptr);

        wifi_ota_panel = lv_obj_create(subpage_wifi);
        lv_obj_set_size(wifi_ota_panel, 320, 210);
        lv_obj_set_pos(wifi_ota_panel, 0, 30);
        style_panel(wifi_ota_panel, theme_screen_bg(), theme_screen_bg(), 0);
        lv_obj_set_style_pad_all(wifi_ota_panel, 0, 0);
        lv_obj_add_flag(wifi_ota_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(wifi_ota_panel, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *ota_banner = create_heading_card(wifi_ota_panel, 304, 44, 8, 4,
                                                   "Tryb OTA",
                                                   "AP gotowy do aktualizacji",
                                                   lv_color_make(20, 184, 166));
        LV_UNUSED(ota_banner);

        lv_obj_t *ota_card = create_card(wifi_ota_panel, 304, 108, 8, 54);
        lv_obj_set_style_pad_all(ota_card, 0, 0);
        lv_obj_set_style_border_color(ota_card, lv_color_make(20, 184, 166), 0);
        lv_obj_clear_flag(ota_card, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *ota_state_lbl = create_label(ota_card, LV_SYMBOL_UPLOAD "  Portal: http://192.168.4.1/", lv_color_make(20, 184, 166), &lv_font_montserrat_12);
        lv_obj_set_width(ota_state_lbl, 280);
        lv_label_set_long_mode(ota_state_lbl, LV_LABEL_LONG_DOT);
        lv_obj_align(ota_state_lbl, LV_ALIGN_TOP_LEFT, 12, 12);

        lv_obj_t *ota_ssid_lbl = create_label(ota_card, LV_SYMBOL_WIFI "  SSID: cydAkwarium-OTA", theme_text_main(), &lv_font_montserrat_12);
        lv_obj_set_width(ota_ssid_lbl, 280);
        lv_label_set_long_mode(ota_ssid_lbl, LV_LABEL_LONG_DOT);
        lv_obj_align(ota_ssid_lbl, LV_ALIGN_TOP_LEFT, 12, 36);

        char ota_pass_text[96];
        snprintf(ota_pass_text, sizeof(ota_pass_text), LV_SYMBOL_EYE_OPEN "  Haslo: %s", device_credentials_ota_ap_password());
        lv_obj_t *ota_pass_lbl = create_label(ota_card, ota_pass_text, theme_text_main(), &lv_font_montserrat_12);
        lv_obj_set_width(ota_pass_lbl, 280);
        lv_label_set_long_mode(ota_pass_lbl, LV_LABEL_LONG_DOT);
        lv_obj_align(ota_pass_lbl, LV_ALIGN_TOP_LEFT, 12, 60);

        lv_obj_t *ota_ip_lbl = create_label(ota_card, LV_SYMBOL_RIGHT "  IP: 192.168.4.1", theme_text_main(), &lv_font_montserrat_12);
        lv_obj_set_width(ota_ip_lbl, 280);
        lv_label_set_long_mode(ota_ip_lbl, LV_LABEL_LONG_DOT);
        lv_obj_align(ota_ip_lbl, LV_ALIGN_TOP_LEFT, 12, 84);

        lv_obj_t *ota_stop_btn = create_button(wifi_ota_panel, LV_SYMBOL_CLOSE "  Zatrzymaj OTA", 180, 32, lv_color_make(239, 68, 68), stop_ota_cb, nullptr);
        lv_obj_align(ota_stop_btn, LV_ALIGN_BOTTOM_MID, 0, -10);
        apply_colored_3d_button(ota_stop_btn, lv_color_make(239, 68, 68), 160);
        return;
    }

    if (target == ActiveSubpage::Screen) {
    subpage_screen = create_subpage("Ekran LCD");
    
    // Scrollable lista opcji ekranu
    lv_obj_t *scr_list = lv_obj_create(subpage_screen);
    lv_obj_set_size(scr_list, 312, 172);
    lv_obj_set_pos(scr_list, 4, 32);
    lv_obj_set_style_bg_opa(scr_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scr_list, 0, 0);
    lv_obj_set_style_pad_all(scr_list, 0, 0);
    lv_obj_set_flex_flow(scr_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scr_list, 5, 0);
    lv_obj_set_scrollbar_mode(scr_list, LV_SCROLLBAR_MODE_AUTO);

    // --- Karta: Motyw ekranu ---
    lv_obj_t *theme_group = create_card(scr_list, 300, 94, 0, 0);
    lv_obj_set_style_pad_all(theme_group, 0, 0);
    lv_obj_clear_flag(theme_group, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *theme_title = create_label(theme_group, LV_SYMBOL_IMAGE "  MOTYW EKRANU", lv_color_make(14, 165, 233), &lv_font_montserrat_12);
    lv_obj_set_width(theme_title, 280);
    lv_obj_align(theme_title, LV_ALIGN_TOP_LEFT, 8, 6);

    // Jasny motyw (reczny)
    lv_obj_t *manual_theme_lbl = create_label(theme_group, "Jasny motyw (manual)", theme_text_main(), &lv_font_montserrat_12);
    lv_obj_set_width(manual_theme_lbl, 200); // Ograniczona szerokosc - nie zakrywa switcha
    lv_obj_align(manual_theme_lbl, LV_ALIGN_TOP_LEFT, 8, 28);
    screen_manual_theme_sw = lv_switch_create(theme_group);
    lv_obj_set_size(screen_manual_theme_sw, 40, 20);
    lv_obj_align(screen_manual_theme_sw, LV_ALIGN_TOP_RIGHT, -8, 26);
    style_switch_cyd(screen_manual_theme_sw);
    lv_obj_add_event_cb(screen_manual_theme_sw, screen_manual_theme_handler, LV_EVENT_VALUE_CHANGED, nullptr);

    // Czujnik LDR - auto motyw
    lv_obj_t *ldr_enable_lbl = create_label(theme_group, "Auto LDR (<200 jasny)", theme_text_main(), &lv_font_montserrat_12);
    lv_obj_set_width(ldr_enable_lbl, 200); // Ograniczona szerokosc - nie zakrywa switcha
    lv_obj_align(ldr_enable_lbl, LV_ALIGN_TOP_LEFT, 8, 54);
    screen_ldr_enable_sw = lv_switch_create(theme_group);
    lv_obj_set_size(screen_ldr_enable_sw, 40, 20);
    lv_obj_align(screen_ldr_enable_sw, LV_ALIGN_TOP_RIGHT, -8, 52);
    style_switch_cyd(screen_ldr_enable_sw);
    lv_obj_add_event_cb(screen_ldr_enable_sw, screen_ldr_enable_handler, LV_EVENT_VALUE_CHANGED, nullptr);

    // --- Karta: Ustawienia systemowe ---
    lv_obj_t *sys_group = create_card(scr_list, 300, 52, 0, 0);
    lv_obj_set_style_pad_all(sys_group, 0, 0);
    lv_obj_clear_flag(sys_group, LV_OBJ_FLAG_SCROLLABLE);

    // Always on display
    lv_obj_t *screen_mode_lbl = create_label(sys_group, LV_SYMBOL_POWER "  Ekran zawsze aktywny", theme_text_main(), &lv_font_montserrat_12);
    lv_obj_set_width(screen_mode_lbl, 200); // Ograniczona szerokosc
    lv_obj_align(screen_mode_lbl, LV_ALIGN_TOP_LEFT, 8, 6);
    screen_always_on_sw = lv_switch_create(sys_group);
    lv_obj_set_size(screen_always_on_sw, 40, 20);
    lv_obj_align(screen_always_on_sw, LV_ALIGN_TOP_RIGHT, -8, 4);
    style_switch_cyd(screen_always_on_sw);
    lv_obj_add_event_cb(screen_always_on_sw, screen_always_on_handler, LV_EVENT_VALUE_CHANGED, nullptr);

    // Tryb deweloperski
    lv_obj_t *dev_mode_lbl = create_label(sys_group, LV_SYMBOL_SETTINGS "  Tryb deweloperski", theme_text_main(), &lv_font_montserrat_12);
    lv_obj_set_width(dev_mode_lbl, 200); // Ograniczona szerokosc
    lv_obj_align(dev_mode_lbl, LV_ALIGN_TOP_LEFT, 8, 28);
    diag_dev_mode_sw = lv_switch_create(sys_group);
    lv_obj_set_size(diag_dev_mode_sw, 40, 20);
    lv_obj_align(diag_dev_mode_sw, LV_ALIGN_TOP_RIGHT, -8, 26);
    style_switch_cyd(diag_dev_mode_sw);
    lv_obj_add_event_cb(diag_dev_mode_sw, diag_dev_mode_handler, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t *screen_save = create_button(subpage_screen, LV_SYMBOL_SAVE "  ZAPISZ USTAWIENIA", 200, 26, lv_color_make(16, 185, 129), save_screen_settings_cb, nullptr);
    lv_obj_align(screen_save, LV_ALIGN_BOTTOM_MID, 0, -2);
    return;
    }

    if (target == ActiveSubpage::Logs) {
    subpage_logs = create_subpage("Logi");

    btn_log_normal = create_button(subpage_logs, "Zwykle", 130, 28, lv_color_make(59, 130, 246), btn_log_normal_cb, nullptr);
    lv_obj_align(btn_log_normal, LV_ALIGN_TOP_LEFT, 20, 36);
    apply_3d_button_properties(btn_log_normal);

    btn_log_important = create_button(subpage_logs, "Wazne", 130, 28, lv_color_make(35, 41, 55), btn_log_important_cb, nullptr);
    lv_obj_align(btn_log_important, LV_ALIGN_TOP_RIGHT, -20, 36);
    apply_3d_button_properties(btn_log_important);

    log_list_normal = lv_list_create(subpage_logs);
    lv_obj_set_size(log_list_normal, 280, 110);
    lv_obj_align(log_list_normal, LV_ALIGN_TOP_MID, 0, 70);
    lv_obj_set_style_bg_color(log_list_normal, resolve_bg_color(lv_color_make(20, 26, 40)), 0);
    lv_obj_set_style_border_color(log_list_normal, theme_card_border(), 0);
    lv_obj_set_style_pad_all(log_list_normal, 4, 0);

    log_list_important = lv_list_create(subpage_logs);
    lv_obj_set_size(log_list_important, 280, 110);
    lv_obj_align(log_list_important, LV_ALIGN_TOP_MID, 0, 70);
    lv_obj_set_style_bg_color(log_list_important, resolve_bg_color(lv_color_make(20, 26, 40)), 0);
    lv_obj_set_style_border_color(log_list_important, theme_card_border(), 0);
    lv_obj_set_style_pad_all(log_list_important, 4, 0);
    lv_obj_add_flag(log_list_important, LV_OBJ_FLAG_HIDDEN); // Initially hidden

    // Populate initial system startup logs
    add_gui_log("System uruchomiony poprawnie", false);
    add_gui_log("Zaladowano konfiguracje NVS", false);
    add_gui_log("Panel dotykowy gotowy", false);
    add_gui_log("Brak polaczenia Wi-Fi przy starcie", true);

    lv_obj_t *clear_logs = create_button(subpage_logs, "Wyczysc", 120, 28, lv_color_make(35, 41, 55), clear_logs_cb, nullptr);
    lv_obj_align(clear_logs, LV_ALIGN_BOTTOM_MID, 0, -12);
    apply_3d_button_properties(clear_logs);
    return;
    }

    if (target == ActiveSubpage::Clock) {
    subpage_clock = create_subpage("Zegar", back_clock_cb, nullptr);
    
    lv_obj_t *clock_list = lv_obj_create(subpage_clock);
    lv_obj_set_size(clock_list, 312, 196);
    lv_obj_set_pos(clock_list, 4, 34);
    lv_obj_set_style_bg_opa(clock_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(clock_list, 0, 0);
    lv_obj_set_style_pad_all(clock_list, 0, 0);
    lv_obj_set_flex_flow(clock_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(clock_list, 6, 0);
    lv_obj_set_scrollbar_mode(clock_list, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t *clock_box = create_card(clock_list, 300, 70, 0, 0);
    lv_obj_set_style_pad_all(clock_box, 0, 0);
    label_clock_time = create_label(clock_box, "20:30:00", lv_color_white(), &lv_font_montserrat_24);
    lv_obj_align(label_clock_time, LV_ALIGN_CENTER, 0, -10);
    label_clock_date = create_label(clock_box, "31 May 2026", lv_color_make(148, 163, 184), &lv_font_montserrat_12);
    lv_obj_align(label_clock_date, LV_ALIGN_CENTER, 0, 14);

    // Karta 2: Ręczne ustawianie godziny (roller modal)
    lv_obj_t *adj_time_row = create_card(clock_list, 300, 46, 0, 0);
    lv_obj_set_style_pad_all(adj_time_row, 0, 0);
    lv_obj_t *adj_time_lbl = create_label(adj_time_row, "Ustaw godzine", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(adj_time_lbl, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_t *btn_adjust_time = create_button(adj_time_row, "Dostosuj", 80, 28, lv_color_make(35, 41, 55), open_time_picker_cb, nullptr);
    lv_obj_align(btn_adjust_time, LV_ALIGN_RIGHT_MID, -10, 0);
    apply_3d_button_properties(btn_adjust_time);

    // Karta 3: Wybór daty w kalendarzu
    lv_obj_t *adj_date_row = create_card(clock_list, 300, 46, 0, 0);
    lv_obj_set_style_pad_all(adj_date_row, 0, 0);
    lv_obj_t *adj_date_lbl = create_label(adj_date_row, "Ustaw date", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(adj_date_lbl, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_t *btn_adjust_date = create_button(adj_date_row, "Kalendarz", 80, 28, lv_color_make(35, 41, 55), open_calendar_picker_cb, nullptr);
    lv_obj_align(btn_adjust_date, LV_ALIGN_RIGHT_MID, -10, 0);
    apply_3d_button_properties(btn_adjust_date);

    // Karta 4: NTP Sync Button inside scrollable list
    clock_ntp_row = create_card(clock_list, 300, 46, 0, 0);
    lv_obj_set_style_pad_all(clock_ntp_row, 0, 0);
    btn_sync_ntp_global = create_button(
        clock_ntp_row,
        "Synchronizuj NTP",
        280,
        32,
        lv_color_make(35, 41, 55),
        btn_sync_ntp_handler,
        nullptr);
    lv_obj_align(btn_sync_ntp_global, LV_ALIGN_CENTER, 0, 0);
    btn_sync_ntp_lbl_global = lv_obj_get_child(btn_sync_ntp_global, 0);

    // Initial NTP Sync row visibility based on connection status
    if (!wifi_connected) {
        lv_obj_add_flag(clock_ntp_row, LV_OBJ_FLAG_HIDDEN);
    }
    return;
    }

    if (target == ActiveSubpage::Diagnostics) {
    subpage_diagnostics = create_subpage("Diag");

    lv_obj_t *diag_panel = create_card(subpage_diagnostics, 304, 190, 8, 36);
    lv_obj_set_style_pad_all(diag_panel, 0, 0);
    lv_obj_set_style_border_color(diag_panel, lv_color_make(6, 182, 212), 0);
    create_accent_bar(diag_panel, lv_color_make(6, 182, 212), 172);

    create_fixed_label(diag_panel, "LIVE / RAM / BUS", lv_color_make(6, 182, 212),
                       &lv_font_montserrat_12, 272, 14, 5, LV_LABEL_LONG_CLIP);

    diag_heap_lbl = create_fixed_label(diag_panel, "RAM wolne: -- KB | blok: -- KB",
                                       theme_text_main(), &lv_font_montserrat_12,
                                       278, 14, 24);
    diag_uptime_lbl = create_fixed_label(diag_panel, "Czas: -- | Boot: --",
                                         theme_text_main(), &lv_font_montserrat_12,
                                         278, 14, 42);
    diag_reset_reason_lbl = create_fixed_label(diag_panel, "Reset: --",
                                               theme_text_muted(), &lv_font_montserrat_12,
                                               278, 14, 60);
    diag_cpu_temp_lbl = create_fixed_label(diag_panel, "CPU: --.-*C / -- MHz | Flash -- MB",
                                           theme_text_main(), &lv_font_montserrat_12,
                                           278, 14, 78);
    diag_adc_lbl = create_fixed_label(diag_panel, "ADS: -- | pH -- | EC --",
                                      theme_text_main(), &lv_font_montserrat_12,
                                      278, 14, 96);
    diag_mcp_lbl = create_fixed_label(diag_panel, "MCP: --",
                                      theme_text_main(), &lv_font_montserrat_12,
                                      278, 14, 114);
    diag_queue_lbl = create_fixed_label(diag_panel, "EVT overflow: 0 | LDR: --",
                                        theme_text_muted(), &lv_font_montserrat_12,
                                        278, 14, 132);
    diag_eco_lbl = create_fixed_label(diag_panel, "ECO: --",
                                      theme_text_main(), &lv_font_montserrat_12,
                                      278, 14, 150);
    diag_rtc_lbl = create_fixed_label(diag_panel, "RTC sleep: --",
                                      theme_text_muted(), &lv_font_montserrat_12,
                                      278, 14, 168);
    return;
    }

    if (target == ActiveSubpage::Power) {
    subpage_power = create_subpage("Zasilanie");

    lv_obj_t *state_card = create_card(subpage_power, 304, 42, 8, 36);
    lv_obj_set_style_pad_all(state_card, 0, 0);
    lv_obj_set_style_border_color(state_card, lv_color_make(239, 68, 68), 0);
    create_accent_bar(state_card, lv_color_make(239, 68, 68), 26);
    create_fixed_label(state_card, "ENERGIA", lv_color_make(239, 68, 68),
                       &lv_font_montserrat_12, 268, 14, 5, LV_LABEL_LONG_CLIP);
    power_state_lbl = create_fixed_label(state_card, "LCD auto | WiFi gotowe",
                                         theme_text_main(), &lv_font_montserrat_12,
                                         278, 14, 23);

    lv_obj_t *modem_card = create_card(subpage_power, 304, 40, 8, 84);
    lv_obj_set_style_pad_all(modem_card, 0, 0);
    create_accent_bar(modem_card, lv_color_make(14, 165, 233), 24);
    create_fixed_label(modem_card, "Modem Sleep", theme_text_main(),
                       &lv_font_montserrat_12, 210, 14, 5);
    create_fixed_label(modem_card, "wylacza radio WiFi",
                       theme_text_muted(), &lv_font_montserrat_12,
                       210, 14, 22);
    power_modem_sleep_sw = lv_switch_create(modem_card);
    lv_obj_set_size(power_modem_sleep_sw, 42, 22);
    lv_obj_align(power_modem_sleep_sw, LV_ALIGN_RIGHT_MID, -10, 0);
    style_switch_cyd(power_modem_sleep_sw);
    lv_obj_add_event_cb(power_modem_sleep_sw, power_modem_sleep_handler, LV_EVENT_VALUE_CHANGED, nullptr);

    power_warning_lbl_global = create_fixed_label(subpage_power, "PIN wymagany dla akcji krytycznych",
                                                  theme_text_muted(), &lv_font_montserrat_12,
                                                  304, 8, 128, LV_LABEL_LONG_DOT);

    lv_obj_t *btn_restart = create_button(subpage_power, LV_SYMBOL_REFRESH " Restart", 146, 28,
                                          lv_color_make(239, 68, 68), btn_restart_event_handler, nullptr);
    lv_obj_set_pos(btn_restart, 8, 146);
    apply_colored_3d_button(btn_restart, lv_color_make(239, 68, 68), 130);

    lv_obj_t *btn_light = create_button(subpage_power, "Sen lekki 10s", 146, 28,
                                        lv_color_make(59, 130, 246), btn_light_sleep_handler, nullptr);
    lv_obj_set_pos(btn_light, 166, 146);
    apply_colored_3d_button(btn_light, lv_color_make(59, 130, 246), 130);

    lv_obj_t *btn_deep = create_button(subpage_power, "Sen gleboki", 146, 28,
                                       lv_color_make(71, 85, 105), btn_deep_sleep_handler, nullptr);
    lv_obj_set_pos(btn_deep, 8, 178);
    apply_colored_3d_button(btn_deep, lv_color_make(71, 85, 105), 130);

    lv_obj_t *btn_hib = create_button(subpage_power, "Hibernacja", 146, 28,
                                      lv_color_make(30, 41, 59), btn_hibernation_handler, nullptr);
    lv_obj_set_pos(btn_hib, 166, 178);
    apply_colored_3d_button(btn_hib, lv_color_make(30, 41, 59), 130);

    lv_obj_t *btn_reset = create_button(subpage_power, LV_SYMBOL_WARNING " Reset cfg", 304, 28,
                                        lv_color_make(185, 28, 28), btn_factory_reset_handler, nullptr);
    lv_obj_set_pos(btn_reset, 8, 210);
    apply_colored_3d_button(btn_reset, lv_color_make(185, 28, 28), 286);
    return;
    }

    if (target == ActiveSubpage::FeedEditor) {
    subpage_feed_editor = create_subpage("Karmienie", back_feed_editor_cb, nullptr);
    
    lv_obj_t *feed_list = lv_obj_create(subpage_feed_editor);
    lv_obj_set_size(feed_list, 312, 196);
    lv_obj_set_pos(feed_list, 4, 34);
    lv_obj_set_style_bg_opa(feed_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(feed_list, 0, 0);
    lv_obj_set_style_pad_all(feed_list, 0, 0);
    lv_obj_set_flex_flow(feed_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(feed_list, 6, 0);
    lv_obj_set_scrollbar_mode(feed_list, LV_SCROLLBAR_MODE_AUTO);

    // Card 1: Auto Feeding (switch)
    lv_obj_t *feed_enabled_row = create_card(feed_list, 300, 46, 0, 0);
    lv_obj_set_style_pad_all(feed_enabled_row, 0, 0);
    lv_obj_clear_flag(feed_enabled_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *feed_enabled_lbl = create_label(feed_enabled_row, "Auto karmienie", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(feed_enabled_lbl, LV_ALIGN_LEFT_MID, 10, 0);
    
    feed_enable_sw = lv_switch_create(feed_enabled_row);
    lv_obj_set_size(feed_enable_sw, 40, 20);
    lv_obj_align(feed_enable_sw, LV_ALIGN_RIGHT_MID, -10, 0);
    style_switch_cyd(feed_enable_sw);
    lv_obj_add_event_cb(feed_enable_sw, [](lv_event_t *e) {
        play_system_sound(SoundType::Click);
        cfg.feedEnabled = lv_obj_has_state(feed_enable_sw, LV_STATE_CHECKED);
        gui_sync_widgets_to_state();
    }, LV_EVENT_VALUE_CHANGED, nullptr);

    // Card 2: Weekday grid (Mon-Sun)
    lv_obj_t *feed_days_row = create_card(feed_list, 300, 64, 0, 0);
    lv_obj_set_style_pad_all(feed_days_row, 0, 0);
    lv_obj_clear_flag(feed_days_row, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *feed_days_lbl = create_label(feed_days_row, "Dni tygodnia", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(feed_days_lbl, LV_ALIGN_TOP_LEFT, 10, 6);

    lv_obj_t *feed_days_container = lv_obj_create(feed_days_row);
    lv_obj_set_size(feed_days_container, 288, 28);
    lv_obj_align(feed_days_container, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_flex_flow(feed_days_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(feed_days_container, 0, 0);
    lv_obj_set_style_pad_column(feed_days_container, 4, 0);
    lv_obj_set_style_bg_opa(feed_days_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(feed_days_container, 0, 0);
    lv_obj_clear_flag(feed_days_container, LV_OBJ_FLAG_SCROLLABLE);

    const char *day_names[] = {"Pn", "Wt", "Sr", "Cz", "Pt", "Sb", "Nd"};
    for (int i = 0; i < 7; i++) {
        feed_day_btns[i] = create_button(feed_days_container, day_names[i], 36, 24, lv_color_make(35, 41, 55), feed_day_click_cb, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
        lv_obj_set_style_radius(feed_day_btns[i], 4, 0);
        lv_obj_set_style_pad_all(feed_day_btns[i], 0, 0);
    }

    // Card 3: Frequency (button)
    lv_obj_t *feed_freq_row = create_card(feed_list, 300, 46, 0, 0);
    lv_obj_set_style_pad_all(feed_freq_row, 0, 0);
    lv_obj_clear_flag(feed_freq_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *feed_freq_lbl = create_label(feed_freq_row, "Czestosc", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(feed_freq_lbl, LV_ALIGN_LEFT_MID, 10, 0);

    feed_freq_btn = create_button(feed_freq_row, "1 raz dziennie", 110, 26, lv_color_make(35, 41, 55), feed_freq_click_cb, nullptr);
    lv_obj_align(feed_freq_btn, LV_ALIGN_RIGHT_MID, -10, 0);

    // Card 4: Time 1 Row
    lv_obj_t *feed_time1_row = create_card(feed_list, 300, 46, 0, 0);
    lv_obj_set_style_pad_all(feed_time1_row, 0, 0);
    lv_obj_clear_flag(feed_time1_row, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *time1_lbl = create_label(feed_time1_row, "Godz. 1", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(time1_lbl, LV_ALIGN_LEFT_MID, 10, 0);

    lv_obj_t *t1_h_minus = create_button(feed_time1_row, "-", 24, 20, lv_color_make(35, 41, 55), adjust_feed_time_new, reinterpret_cast<void *>(static_cast<intptr_t>(112)));
    lv_obj_align(t1_h_minus, LV_ALIGN_LEFT_MID, 60, 0);
    feed_time1_h_lbl = create_label(feed_time1_row, "10", lv_color_make(6, 182, 212), &lv_font_montserrat_12);
    lv_obj_align(feed_time1_h_lbl, LV_ALIGN_LEFT_MID, 90, 0);
    lv_obj_set_size(feed_time1_h_lbl, 20, 20);
    lv_obj_set_style_text_align(feed_time1_h_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *t1_h_plus = create_button(feed_time1_row, "+", 24, 20, lv_color_make(35, 41, 55), adjust_feed_time_new, reinterpret_cast<void *>(static_cast<intptr_t>(111)));
    lv_obj_align(t1_h_plus, LV_ALIGN_LEFT_MID, 116, 0);

    lv_obj_t *t1_colon = create_label(feed_time1_row, ":", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(t1_colon, LV_ALIGN_LEFT_MID, 146, -1);

    lv_obj_t *t1_m_minus = create_button(feed_time1_row, "-", 24, 20, lv_color_make(35, 41, 55), adjust_feed_time_new, reinterpret_cast<void *>(static_cast<intptr_t>(122)));
    lv_obj_align(t1_m_minus, LV_ALIGN_LEFT_MID, 156, 0);
    feed_time1_m_lbl = create_label(feed_time1_row, "00", lv_color_make(6, 182, 212), &lv_font_montserrat_12);
    lv_obj_align(feed_time1_m_lbl, LV_ALIGN_LEFT_MID, 186, 0);
    lv_obj_set_size(feed_time1_m_lbl, 20, 20);
    lv_obj_set_style_text_align(feed_time1_m_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *t1_m_plus = create_button(feed_time1_row, "+", 24, 20, lv_color_make(35, 41, 55), adjust_feed_time_new, reinterpret_cast<void *>(static_cast<intptr_t>(121)));
    lv_obj_align(t1_m_plus, LV_ALIGN_LEFT_MID, 212, 0);

    // Card 5: Time 2 Row
    feed_time2_row = create_card(feed_list, 300, 46, 0, 0);
    lv_obj_set_style_pad_all(feed_time2_row, 0, 0);
    lv_obj_clear_flag(feed_time2_row, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *time2_lbl = create_label(feed_time2_row, "Godz. 2", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(time2_lbl, LV_ALIGN_LEFT_MID, 10, 0);

    lv_obj_t *t2_h_minus = create_button(feed_time2_row, "-", 24, 20, lv_color_make(35, 41, 55), adjust_feed_time_new, reinterpret_cast<void *>(static_cast<intptr_t>(212)));
    lv_obj_align(t2_h_minus, LV_ALIGN_LEFT_MID, 60, 0);
    feed_time2_h_lbl = create_label(feed_time2_row, "18", lv_color_make(6, 182, 212), &lv_font_montserrat_12);
    lv_obj_align(feed_time2_h_lbl, LV_ALIGN_LEFT_MID, 90, 0);
    lv_obj_set_size(feed_time2_h_lbl, 20, 20);
    lv_obj_set_style_text_align(feed_time2_h_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *t2_h_plus = create_button(feed_time2_row, "+", 24, 20, lv_color_make(35, 41, 55), adjust_feed_time_new, reinterpret_cast<void *>(static_cast<intptr_t>(211)));
    lv_obj_align(t2_h_plus, LV_ALIGN_LEFT_MID, 116, 0);

    lv_obj_t *t2_colon = create_label(feed_time2_row, ":", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(t2_colon, LV_ALIGN_LEFT_MID, 146, -1);

    lv_obj_t *t2_m_minus = create_button(feed_time2_row, "-", 24, 20, lv_color_make(35, 41, 55), adjust_feed_time_new, reinterpret_cast<void *>(static_cast<intptr_t>(222)));
    lv_obj_align(t2_m_minus, LV_ALIGN_LEFT_MID, 156, 0);
    feed_time2_m_lbl = create_label(feed_time2_row, "00", lv_color_make(6, 182, 212), &lv_font_montserrat_12);
    lv_obj_align(feed_time2_m_lbl, LV_ALIGN_LEFT_MID, 186, 0);
    lv_obj_set_size(feed_time2_m_lbl, 20, 20);
    lv_obj_set_style_text_align(feed_time2_m_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *t2_m_plus = create_button(feed_time2_row, "+", 24, 20, lv_color_make(35, 41, 55), adjust_feed_time_new, reinterpret_cast<void *>(static_cast<intptr_t>(221)));
    lv_obj_align(t2_m_plus, LV_ALIGN_LEFT_MID, 212, 0);
    return;
    }

    if (target == ActiveSubpage::SchedEditor) {
    subpage_sched_editor = create_subpage("Harmonogram", back_sched_editor_cb, nullptr);
    editor_title_lbl = create_label(subpage_sched_editor, "Przednia", lv_color_white(), &lv_font_montserrat_14);
    lv_obj_align(editor_title_lbl, LV_ALIGN_TOP_MID, 0, 36);
    sched_editor_mode_btn = create_button(subpage_sched_editor, "Tryb", 100, 28, lv_color_make(35, 41, 55), cycle_editor_mode_cb, nullptr);
    lv_obj_align(sched_editor_mode_btn, LV_ALIGN_TOP_MID, 0, 58);
    editor_mode_lbl = lv_obj_get_child(sched_editor_mode_btn, 0);

    // Editor controls for 24h timeline
    lv_obj_t *timeline_container = lv_obj_create(subpage_sched_editor);
    lv_obj_set_size(timeline_container, 312, 26);
    lv_obj_align(timeline_container, LV_ALIGN_TOP_MID, 0, 88);
    lv_obj_set_flex_flow(timeline_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(timeline_container, 0, 0);
    lv_obj_set_style_pad_column(timeline_container, 1, 0);
    lv_obj_set_style_bg_color(timeline_container, resolve_bg_color(lv_color_make(15, 23, 42)), 0);
    lv_obj_set_style_border_width(timeline_container, 0, 0);
    lv_obj_clear_flag(timeline_container, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 24; i++) {
        timeline_blocks[i] = lv_obj_create(timeline_container);
        lv_obj_set_size(timeline_blocks[i], 12, 26);
        lv_obj_set_style_radius(timeline_blocks[i], 2, 0);
        lv_obj_set_style_pad_all(timeline_blocks[i], 0, 0);
        lv_obj_set_style_border_width(timeline_blocks[i], 0, 0);
        lv_obj_clear_flag(timeline_blocks[i], LV_OBJ_FLAG_SCROLLABLE);
    }

    // Add hour markers under the timeline
    lv_obj_t *marker_container = lv_obj_create(subpage_sched_editor);
    lv_obj_set_size(marker_container, 312, 12);
    lv_obj_align(marker_container, LV_ALIGN_TOP_MID, 0, 115);
    lv_obj_set_style_bg_opa(marker_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(marker_container, 0, 0);
    lv_obj_set_style_pad_all(marker_container, 0, 0);
    lv_obj_clear_flag(marker_container, LV_OBJ_FLAG_SCROLLABLE);

    const char *hours[] = {"0h", "6h", "12h", "18h", "23h"};
    int positions[] = {0, 78, 156, 234, 296};
    for (int i = 0; i < 5; i++) {
        lv_obj_t *m_lbl = create_label(marker_container, hours[i], lv_color_make(100, 116, 139), &lv_font_montserrat_12);
        lv_obj_align(m_lbl, LV_ALIGN_LEFT_MID, positions[i], 0);
    }

    lv_obj_t *editor_controls = create_card(subpage_sched_editor, 300, 82, 10, 126);
    lv_obj_set_style_pad_all(editor_controls, 0, 0);

    // Row 1: Start time
    lv_obj_t *row_start = lv_obj_create(editor_controls);
    lv_obj_set_size(row_start, 290, 24);
    lv_obj_align(row_start, LV_ALIGN_TOP_MID, 0, 2);
    lv_obj_set_style_bg_opa(row_start, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row_start, 0, 0);
    lv_obj_clear_flag(row_start, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_start = create_label(row_start, "START", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(lbl_start, LV_ALIGN_LEFT_MID, 6, 0);

    lv_obj_t *sh_minus = create_button(row_start, "-", 24, 20, lv_color_make(35, 41, 55), adjust_schedule_time_cb, reinterpret_cast<void *>(static_cast<intptr_t>(19)));
    lv_obj_align(sh_minus, LV_ALIGN_LEFT_MID, 60, 0);
    sched_editor_start_h_lbl = create_label(row_start, "10", lv_color_make(6, 182, 212), &lv_font_montserrat_12);
    lv_obj_align(sched_editor_start_h_lbl, LV_ALIGN_LEFT_MID, 90, 0);
    lv_obj_set_size(sched_editor_start_h_lbl, 20, 20);
    lv_obj_set_style_text_align(sched_editor_start_h_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *sh_plus = create_button(row_start, "+", 24, 20, lv_color_make(35, 41, 55), adjust_schedule_time_cb, reinterpret_cast<void *>(static_cast<intptr_t>(11)));
    lv_obj_align(sh_plus, LV_ALIGN_LEFT_MID, 116, 0);

    lv_obj_t *colon1 = create_label(row_start, ":", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(colon1, LV_ALIGN_LEFT_MID, 146, -1);

    lv_obj_t *sm_minus = create_button(row_start, "-", 24, 20, lv_color_make(35, 41, 55), adjust_schedule_time_cb, reinterpret_cast<void *>(static_cast<intptr_t>(29)));
    lv_obj_align(sm_minus, LV_ALIGN_LEFT_MID, 156, 0);
    sched_editor_start_m_lbl = create_label(row_start, "00", lv_color_make(6, 182, 212), &lv_font_montserrat_12);
    lv_obj_align(sched_editor_start_m_lbl, LV_ALIGN_LEFT_MID, 186, 0);
    lv_obj_set_size(sched_editor_start_m_lbl, 20, 20);
    lv_obj_set_style_text_align(sched_editor_start_m_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *sm_plus = create_button(row_start, "+", 24, 20, lv_color_make(35, 41, 55), adjust_schedule_time_cb, reinterpret_cast<void *>(static_cast<intptr_t>(21)));
    lv_obj_align(sm_plus, LV_ALIGN_LEFT_MID, 212, 0);

    // Row 2: End time
    lv_obj_t *row_end = lv_obj_create(editor_controls);
    lv_obj_set_size(row_end, 290, 24);
    lv_obj_align(row_end, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_set_style_bg_opa(row_end, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row_end, 0, 0);
    lv_obj_clear_flag(row_end, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_end = create_label(row_end, "END", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(lbl_end, LV_ALIGN_LEFT_MID, 6, 0);

    lv_obj_t *eh_minus = create_button(row_end, "-", 24, 20, lv_color_make(35, 41, 55), adjust_schedule_time_cb, reinterpret_cast<void *>(static_cast<intptr_t>(39)));
    lv_obj_align(eh_minus, LV_ALIGN_LEFT_MID, 60, 0);
    sched_editor_end_h_lbl = create_label(row_end, "21", lv_color_make(6, 182, 212), &lv_font_montserrat_12);
    lv_obj_align(sched_editor_end_h_lbl, LV_ALIGN_LEFT_MID, 90, 0);
    lv_obj_set_size(sched_editor_end_h_lbl, 20, 20);
    lv_obj_set_style_text_align(sched_editor_end_h_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *eh_plus = create_button(row_end, "+", 24, 20, lv_color_make(35, 41, 55), adjust_schedule_time_cb, reinterpret_cast<void *>(static_cast<intptr_t>(31)));
    lv_obj_align(eh_plus, LV_ALIGN_LEFT_MID, 116, 0);

    lv_obj_t *colon2 = create_label(row_end, ":", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(colon2, LV_ALIGN_LEFT_MID, 146, -1);

    lv_obj_t *em_minus = create_button(row_end, "-", 24, 20, lv_color_make(35, 41, 55), adjust_schedule_time_cb, reinterpret_cast<void *>(static_cast<intptr_t>(49)));
    lv_obj_align(em_minus, LV_ALIGN_LEFT_MID, 156, 0);
    sched_editor_end_m_lbl = create_label(row_end, "00", lv_color_make(6, 182, 212), &lv_font_montserrat_12);
    lv_obj_align(sched_editor_end_m_lbl, LV_ALIGN_LEFT_MID, 186, 0);
    lv_obj_set_size(sched_editor_end_m_lbl, 20, 20);
    lv_obj_set_style_text_align(sched_editor_end_m_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *em_plus = create_button(row_end, "+", 24, 20, lv_color_make(35, 41, 55), adjust_schedule_time_cb, reinterpret_cast<void *>(static_cast<intptr_t>(41)));
    lv_obj_align(em_plus, LV_ALIGN_LEFT_MID, 212, 0);

    // Row 3: Color selection (Lights only)
    sched_editor_color_row = lv_obj_create(editor_controls);
    lv_obj_set_size(sched_editor_color_row, 290, 24);
    lv_obj_align(sched_editor_color_row, LV_ALIGN_TOP_MID, 0, 54);
    lv_obj_set_style_bg_opa(sched_editor_color_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sched_editor_color_row, 0, 0);
    lv_obj_clear_flag(sched_editor_color_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_color = create_label(sched_editor_color_row, "COLOR", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(lbl_color, LV_ALIGN_LEFT_MID, 6, 0);

    lv_obj_t *btn_container = lv_obj_create(sched_editor_color_row);
    lv_obj_set_size(btn_container, 220, 24);
    lv_obj_align(btn_container, LV_ALIGN_LEFT_MID, 60, 0);
    lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(btn_container, 0, 0);
    lv_obj_set_style_pad_column(btn_container, 6, 0);
    lv_obj_set_style_bg_opa(btn_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_container, 0, 0);
    lv_obj_clear_flag(btn_container, LV_OBJ_FLAG_SCROLLABLE);

    const char* light_modes[] = {"DAY", "DAYBR", "NIGHT"};
    for (int i = 0; i < 3; i++) {
        editor_mode_btns[i] = create_button(btn_container, light_modes[i], 64, 22, lv_color_make(35, 41, 55), editor_mode_btn_cb, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
        lv_obj_set_style_radius(editor_mode_btns[i], 4, 0);
        lv_obj_set_style_pad_all(editor_mode_btns[i], 0, 0);
    }
    return;
    }

    if (target == ActiveSubpage::Sounds) {
    subpage_sounds = create_subpage("Dzwiek");

    lv_obj_t *snd_list = lv_obj_create(subpage_sounds);
    lv_obj_set_size(snd_list, 312, 168);
    lv_obj_set_pos(snd_list, 4, 34);
    lv_obj_set_style_bg_opa(snd_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(snd_list, 0, 0);
    lv_obj_set_style_pad_all(snd_list, 0, 0);
    lv_obj_set_flex_flow(snd_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(snd_list, 6, 0);
    lv_obj_set_scrollbar_mode(snd_list, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t *sound_enable_row = create_card(snd_list, 300, 46, 0, 0);
    lv_obj_set_style_pad_all(sound_enable_row, 0, 0);
    lv_obj_t *sound_enable_lbl = create_label(sound_enable_row, "Dzwieki systemowe", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(sound_enable_lbl, LV_ALIGN_LEFT_MID, 10, 0);
    sound_enable_sw = lv_switch_create(sound_enable_row);
    lv_obj_set_size(sound_enable_sw, 42, 22);
    lv_obj_align(sound_enable_sw, LV_ALIGN_RIGHT_MID, -10, 0);
    style_switch_cyd(sound_enable_sw);
    lv_obj_add_event_cb(sound_enable_sw, sound_enable_handler, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t *quiet_enable_row = create_card(snd_list, 300, 46, 0, 0);
    lv_obj_set_style_pad_all(quiet_enable_row, 0, 0);
    lv_obj_t *quiet_enable_lbl = create_label(quiet_enable_row, "Cisza nocna", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(quiet_enable_lbl, LV_ALIGN_LEFT_MID, 10, 0);
    sound_quiet_enable_sw = lv_switch_create(quiet_enable_row);
    lv_obj_set_size(sound_quiet_enable_sw, 42, 22);
    lv_obj_align(sound_quiet_enable_sw, LV_ALIGN_RIGHT_MID, -10, 0);
    style_switch_cyd(sound_quiet_enable_sw);
    lv_obj_add_event_cb(sound_quiet_enable_sw, sound_quiet_enable_handler, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t *quiet_sched_row = create_card(snd_list, 300, 46, 0, 0);
    lv_obj_set_style_pad_all(quiet_sched_row, 0, 0);
    lv_obj_clear_flag(quiet_sched_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *quiet_sched_lbl = create_label(quiet_sched_row, "Harmonogram", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(quiet_sched_lbl, LV_ALIGN_LEFT_MID, 10, 0);

    sound_quiet_sched_lbl = create_label(quiet_sched_row, "20:00 - 10:00", lv_color_make(148, 163, 184), &lv_font_montserrat_12);
    lv_obj_align(sound_quiet_sched_lbl, LV_ALIGN_RIGHT_MID, -100, 0);

    lv_obj_t *quiet_sched_btn = create_button(quiet_sched_row, "Dostosuj", 80, 28, lv_color_make(35, 41, 55), open_sched_editor_cb, reinterpret_cast<void *>(static_cast<intptr_t>(ScheduleDevice::QuietHours)));
    lv_obj_align(quiet_sched_btn, LV_ALIGN_RIGHT_MID, -10, 0);
    apply_3d_button_properties(quiet_sched_btn);

    lv_obj_t *speaker_test_row = create_card(snd_list, 300, 46, 0, 0);
    lv_obj_set_style_pad_all(speaker_test_row, 0, 0);
    lv_obj_t *speaker_test_lbl = create_label(speaker_test_row, "Test glosnika", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(speaker_test_lbl, LV_ALIGN_LEFT_MID, 10, 0);

    lv_obj_t *speaker_test_btn = create_button(speaker_test_row, "TEST", 70, 26, lv_color_make(6, 182, 212), test_speaker_cb, nullptr);
    lv_obj_align(speaker_test_btn, LV_ALIGN_RIGHT_MID, -10, 0);

    lv_obj_t *sound_save = create_button(subpage_sounds, "ZAPISZ DZWIEK", 180, 26, lv_color_make(16, 185, 129), save_sound_settings_cb, nullptr);
    lv_obj_align(sound_save, LV_ALIGN_BOTTOM_MID, 0, -2);
    return;
    }

    if (target == ActiveSubpage::Heater) {
    subpage_heater = create_subpage("Grzalka", back_heater_cb, nullptr);
    
    lv_obj_t *heat_list = lv_obj_create(subpage_heater);
    lv_obj_set_size(heat_list, 312, 196);
    lv_obj_set_pos(heat_list, 4, 34);
    lv_obj_set_style_bg_opa(heat_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(heat_list, 0, 0);
    lv_obj_set_style_pad_all(heat_list, 0, 0);
    lv_obj_set_flex_flow(heat_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(heat_list, 6, 0);
    lv_obj_set_scrollbar_mode(heat_list, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t *heater_row = create_card(heat_list, 300, 46, 0, 0);
    lv_obj_set_style_pad_all(heater_row, 0, 0);
    lv_obj_clear_flag(heater_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *heater_lbl = create_label(heater_row, "Prog grzalki", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(heater_lbl, LV_ALIGN_LEFT_MID, 10, 0);
    temp_auto_sw = lv_switch_create(heater_row);
    lv_obj_set_size(temp_auto_sw, 40, 20);
    lv_obj_align(temp_auto_sw, LV_ALIGN_RIGHT_MID, -10, 0);
    style_switch_cyd(temp_auto_sw);
    lv_obj_add_event_cb(temp_auto_sw, toggle_heater_auto_handler, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t *target_row = create_card(heat_list, 300, 46, 0, 0);
    lv_obj_set_style_pad_all(target_row, 0, 0);
    lv_obj_t *target_title = create_label(target_row, "Temp. docelowa", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(target_title, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_t *btn_t_minus = create_button(target_row, "-", 30, 26, lv_color_make(35, 41, 55), adjust_target_temp_cb, reinterpret_cast<void *>(static_cast<intptr_t>(-1)));
    lv_obj_align(btn_t_minus, LV_ALIGN_RIGHT_MID, -92, 0);
    temp_target_val_lbl = create_label(target_row, "25.0*C", lv_color_make(6, 182, 212), &lv_font_montserrat_12);
    lv_obj_align(temp_target_val_lbl, LV_ALIGN_RIGHT_MID, -48, 0);
    lv_obj_t *btn_t_plus = create_button(target_row, "+", 30, 26, lv_color_make(35, 41, 55), adjust_target_temp_cb, reinterpret_cast<void *>(static_cast<intptr_t>(1)));
    lv_obj_align(btn_t_plus, LV_ALIGN_RIGHT_MID, -10, 0);

    // Card 3: Hysteresis
    lv_obj_t *hyst_row = create_card(heat_list, 300, 46, 0, 0);
    lv_obj_set_style_pad_all(hyst_row, 0, 0);
    lv_obj_t *hyst_title = create_label(hyst_row, "Hysteresis", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(hyst_title, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_t *btn_h_minus = create_button(hyst_row, "-", 30, 26, lv_color_make(35, 41, 55), adjust_hysteresis_cb, reinterpret_cast<void *>(static_cast<intptr_t>(-1)));
    lv_obj_align(btn_h_minus, LV_ALIGN_RIGHT_MID, -92, 0);
    temp_hysteresis_val_lbl = create_label(hyst_row, "0.5*C", lv_color_make(6, 182, 212), &lv_font_montserrat_12);
    lv_obj_align(temp_hysteresis_val_lbl, LV_ALIGN_RIGHT_MID, -48, 0);
    lv_obj_t *btn_h_plus = create_button(hyst_row, "+", 30, 26, lv_color_make(35, 41, 55), adjust_hysteresis_cb, reinterpret_cast<void *>(static_cast<intptr_t>(1)));
    lv_obj_align(btn_h_plus, LV_ALIGN_RIGHT_MID, -10, 0);
    return;
    }

    if (target == ActiveSubpage::Ph) {
    subpage_ph = create_subpage("Ustawienia pH", back_ph_cb, nullptr);
    
    lv_obj_t *ph_list = lv_obj_create(subpage_ph);
    lv_obj_set_size(ph_list, 312, 196);
    lv_obj_set_pos(ph_list, 4, 34);
    lv_obj_set_style_bg_opa(ph_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ph_list, 0, 0);
    lv_obj_set_style_pad_all(ph_list, 0, 0);
    lv_obj_set_flex_flow(ph_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ph_list, 6, 0);
    lv_obj_set_scrollbar_mode(ph_list, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t *ph_enable_row = create_card(ph_list, 300, 46, 0, 0);
    lv_obj_set_style_pad_all(ph_enable_row, 0, 0);
    lv_obj_t *ph_enable_lbl = create_label(ph_enable_row, "Pokazuj pH", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(ph_enable_lbl, LV_ALIGN_LEFT_MID, 10, 0);
    screen_ph_enable_sw = lv_switch_create(ph_enable_row);
    lv_obj_set_size(screen_ph_enable_sw, 42, 22);
    lv_obj_align(screen_ph_enable_sw, LV_ALIGN_RIGHT_MID, -10, 0);
    style_switch_cyd(screen_ph_enable_sw);
    lv_obj_add_event_cb(screen_ph_enable_sw, screen_ph_enable_handler, LV_EVENT_VALUE_CHANGED, nullptr);
    return;
    }

    if (target == ActiveSubpage::Service) {
    subpage_service = create_subpage("Tryb serwisowy", back_service_cb, nullptr);
    
    lv_obj_t *service_list = lv_obj_create(subpage_service);
    lv_obj_set_size(service_list, 312, 196);
    lv_obj_set_pos(service_list, 4, 34);
    lv_obj_set_style_bg_opa(service_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(service_list, 0, 0);
    lv_obj_set_style_pad_all(service_list, 0, 0);
    lv_obj_set_flex_flow(service_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(service_list, 6, 0);
    lv_obj_set_scrollbar_mode(service_list, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t *timer_card = create_card(service_list, 300, 40, 0, 0);
    lv_obj_t *timer_lbl = create_label(timer_card, "Service mode ends in: 30:00", lv_color_make(239, 68, 68), &lv_font_montserrat_14);
    lv_obj_align(timer_lbl, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *row_grid = lv_obj_create(service_list);
    lv_obj_set_size(row_grid, 300, 52);
    lv_obj_set_style_pad_all(row_grid, 0, 0);
    lv_obj_set_style_pad_column(row_grid, 8, 0);
    lv_obj_set_style_bg_opa(row_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row_grid, 0, 0);
    lv_obj_set_flex_flow(row_grid, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(row_grid, LV_OBJ_FLAG_SCROLLABLE);

    // Card 1: Light Switch Card
    lv_obj_t *light_sw_card = create_card(row_grid, 146, 52, 0, 0);
    lv_obj_set_style_pad_all(light_sw_card, 0, 0);
    lv_obj_clear_flag(light_sw_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *light_sw_lbl = create_label(light_sw_card, "Light", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(light_sw_lbl, LV_ALIGN_LEFT_MID, 8, 0);

    service_light_sw = lv_switch_create(light_sw_card);
    lv_obj_set_size(service_light_sw, 40, 20);
    lv_obj_align(service_light_sw, LV_ALIGN_RIGHT_MID, -8, 0);
    style_switch_cyd(service_light_sw);
    lv_obj_add_event_cb(service_light_sw, service_light_sw_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    // Card 2: Filter Switch Card
    lv_obj_t *filter_sw_card = create_card(row_grid, 146, 52, 0, 0);
    lv_obj_set_style_pad_all(filter_sw_card, 0, 0);
    lv_obj_clear_flag(filter_sw_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *filter_sw_lbl = create_label(filter_sw_card, "Filter", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(filter_sw_lbl, LV_ALIGN_LEFT_MID, 8, 0);

    service_filter_sw = lv_switch_create(filter_sw_card);
    lv_obj_set_size(service_filter_sw, 40, 20);
    lv_obj_align(service_filter_sw, LV_ALIGN_RIGHT_MID, -8, 0);
    style_switch_cyd(service_filter_sw);
    lv_obj_add_event_cb(service_filter_sw, service_filter_sw_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    // Music Card
    lv_obj_t *music_card = create_card(service_list, 300, 115, 0, 0);
    lv_obj_set_style_pad_all(music_card, 0, 0);
    lv_obj_clear_flag(music_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *music_title = create_label(music_card, "Play music", lv_color_make(100, 116, 139), &lv_font_montserrat_12);
    lv_obj_align(music_title, LV_ALIGN_TOP_LEFT, 10, 4);

    // Dropdown (Song Select)
    lv_obj_t *song_dd = lv_dropdown_create(music_card);
    lv_obj_set_size(song_dd, 140, 28);
    lv_obj_align(song_dd, LV_ALIGN_TOP_LEFT, 10, 20);
    lv_dropdown_set_options(song_dd, "Popcorn Song\nDuckTales Theme\nContra Theme");
    lv_dropdown_set_selected(song_dd, static_cast<uint16_t>(selectedSongIndex.load()));
    lv_obj_set_style_bg_color(song_dd, resolve_bg_color(lv_color_make(35, 41, 55)), 0);
    lv_obj_set_style_text_color(song_dd, lv_color_white(), 0);
    lv_obj_set_style_text_font(song_dd, &lv_font_montserrat_12, 0);
    lv_obj_set_style_border_color(song_dd, lv_color_make(6, 182, 212), 0);
    lv_obj_add_event_cb(song_dd, service_song_dd_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    // Volume label
    service_vol_lbl = create_label(music_card, "Vol: 50%", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(service_vol_lbl, LV_ALIGN_TOP_LEFT, 160, 26);

    // Volume slider
    lv_obj_t *vol_slider = lv_slider_create(music_card);
    lv_obj_set_size(vol_slider, 75, 10);
    lv_obj_set_ext_click_area(vol_slider, 15);
    lv_obj_align(vol_slider, LV_ALIGN_TOP_LEFT, 215, 28);
    lv_slider_set_range(vol_slider, 0, 10);
    lv_slider_set_value(vol_slider, musicVolume.load(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(vol_slider, resolve_bg_color(lv_color_make(30, 41, 59)), LV_PART_MAIN);
    lv_obj_set_style_bg_color(vol_slider, lv_color_make(6, 182, 212), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(vol_slider, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_pad_all(vol_slider, 0, LV_PART_KNOB);
    lv_obj_add_event_cb(vol_slider, service_volume_slider_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    // Buttons
    lv_obj_t *play_btn = create_button(music_card, "GRAJ", 135, 30, lv_color_make(16, 185, 129), service_play_cb, nullptr);
    lv_obj_align(play_btn, LV_ALIGN_TOP_LEFT, 10, 68);
    apply_3d_button_properties(play_btn);
    lv_obj_t *play_lbl = lv_obj_get_child(play_btn, 0);
    if (play_lbl) lv_obj_set_style_text_font(play_lbl, &lv_font_montserrat_12, 0);

    lv_obj_t *stop_btn = create_button(music_card, "STOP", 135, 30, lv_color_make(239, 68, 68), service_stop_cb, nullptr);
    lv_obj_align(stop_btn, LV_ALIGN_TOP_LEFT, 155, 68);
    apply_3d_button_properties(stop_btn);
    lv_obj_t *stop_lbl = lv_obj_get_child(stop_btn, 0);
    if (stop_lbl) lv_obj_set_style_text_font(stop_lbl, &lv_font_montserrat_12, 0);
    return;
    }
}

static void gui_sync_widgets_to_state() {
    if (home_temp_target_lbl != nullptr) {
        lv_label_set_text_fmt(home_temp_target_lbl, "Cel %.1f*C", cfg.targetTemp);
    }
    if (home_feed_time_lbl != nullptr) {
        int wday = get_weekday(clock_day, clock_month, clock_year);
        int bit_idx = (wday == 0) ? 6 : (wday - 1);
        bool day_active = (cfg.feedDays & (1 << bit_idx)) != 0;

        if (!cfg.feedEnabled) {
            if (day_active) {
                if (cfg.feedCount == 2) {
                    if (cfg.showPhSensor) {
                        lv_label_set_text_fmt(home_feed_time_lbl, "(%02u/%02u)",
                                              static_cast<unsigned>(cfg.feedHour1),
                                              static_cast<unsigned>(cfg.feedHour2));
                    } else {
                        lv_label_set_text_fmt(home_feed_time_lbl, "(%02u:%02u/%02u:%02u)",
                                              static_cast<unsigned>(cfg.feedHour1),
                                              static_cast<unsigned>(cfg.feedMinute1),
                                              static_cast<unsigned>(cfg.feedHour2),
                                              static_cast<unsigned>(cfg.feedMinute2));
                    }
                } else {
                    lv_label_set_text_fmt(home_feed_time_lbl, "(%02u:%02u)",
                                          static_cast<unsigned>(cfg.feedHour1),
                                          static_cast<unsigned>(cfg.feedMinute1));
                }
            } else {
                lv_label_set_text(home_feed_time_lbl, "off");
            }
        } else {
            if (day_active) {
                if (cfg.feedCount == 2) {
                    lv_label_set_text_fmt(home_feed_time_lbl, "%02u:%02u/%02u:%02u",
                                          static_cast<unsigned>(cfg.feedHour1),
                                          static_cast<unsigned>(cfg.feedMinute1),
                                          static_cast<unsigned>(cfg.feedHour2),
                                          static_cast<unsigned>(cfg.feedMinute2));
                } else {
                    lv_label_set_text_fmt(home_feed_time_lbl, "%02u:%02u",
                                          static_cast<unsigned>(cfg.feedHour1),
                                          static_cast<unsigned>(cfg.feedMinute1));
                }
            } else {
                lv_label_set_text(home_feed_time_lbl, "off");
            }
        }
    }

    auto set_binary_state = [](lv_obj_t *state_lbl, bool on, const char *on_text,
                               const char *off_text, lv_color_t on_color) {
        if (state_lbl == nullptr) {
            return;
        }
        lv_label_set_text(state_lbl, on ? on_text : off_text);
        lv_obj_set_style_text_color(state_lbl, on ? on_color : theme_text_muted(), 0);
    };

    if (home_light_state_lbl != nullptr) {
        set_binary_state(home_light_state_lbl, runtime.lightOn, "ON", "OFF", lv_color_make(16, 185, 129));
    }
    if (home_light_mode_lbl != nullptr) {
        if (cfg.lightMode == static_cast<uint8_t>(ScheduleMode::Schedule)) {
            lv_label_set_text_fmt(home_light_mode_lbl, "%s %s", mode_label(cfg.lightMode), light_color_mode_label(runtime.lightActiveMode));
        } else {
            lv_label_set_text_fmt(home_light_mode_lbl, "%s %s", mode_label(cfg.lightMode), light_color_mode_label(runtime.lightActiveMode));
        }
    }

    if (home_plant_state_lbl != nullptr) {
        set_binary_state(home_plant_state_lbl, runtime.plantLightOn, "ON", "OFF", lv_color_make(16, 185, 129));
    }
    if (home_plant_mode_lbl != nullptr) {
        if (cfg.plantLightMode == static_cast<uint8_t>(ScheduleMode::Schedule)) {
            lv_label_set_text_fmt(home_plant_mode_lbl, "%s %s", mode_label(cfg.plantLightMode), light_color_mode_label(runtime.plantLightActiveMode));
        } else {
            lv_label_set_text_fmt(home_plant_mode_lbl, "%s %s", mode_label(cfg.plantLightMode), light_color_mode_label(runtime.plantLightActiveMode));
        }
    }

    if (home_filter_state_lbl != nullptr) {
        set_binary_state(home_filter_state_lbl, runtime.filterOn, "ON", "OFF", lv_color_make(16, 185, 129));
    }
    if (home_filter_mode_lbl != nullptr) {
        lv_label_set_text(home_filter_mode_lbl, mode_label(cfg.filterMode));
    }

    if (home_heater_state_lbl != nullptr) {
        if (cfg.heaterMode == static_cast<uint8_t>(HeaterMode::Off)) {
            lv_label_set_text(home_heater_state_lbl, "OFF");
            lv_obj_set_style_text_color(home_heater_state_lbl, theme_text_muted(), 0);
        } else {
            lv_label_set_text(home_heater_state_lbl, runtime.heaterOn ? "HEAT" : "STBY");
            lv_obj_set_style_text_color(home_heater_state_lbl, 
                                        runtime.heaterOn ? lv_color_make(249, 115, 22) : theme_text_muted(), 0);
        }
    }
    if (home_heater_mode_lbl != nullptr) {
        if (cfg.enableHeater) {
            lv_label_set_text_fmt(home_heater_mode_lbl, "%.1f*C", cfg.targetTemp);
        } else {
            lv_label_set_text(home_heater_mode_lbl, "DIS");
        }
    }
    if (home_air_state_lbl != nullptr) {
        set_binary_state(home_air_state_lbl, runtime.airOn, "ON", "OFF", lv_color_make(16, 185, 129));
    }
    if (home_air_mode_lbl != nullptr) {
        lv_label_set_text(home_air_mode_lbl, mode_label(cfg.airMode));
    }


    // Sync Devices tab button active states
    for (int d = 0; d < 5; ++d) {
        for (int m = 0; m < 3; ++m) {
            if (device_btns[d][m] != nullptr) {
                lv_obj_clear_state(device_btns[d][m], LV_STATE_CHECKED);
            }
        }
        
        int active_m = 0;
        if (d == 0) active_m = cfg.lightMode;
        else if (d == 1) active_m = cfg.plantLightMode;
        else if (d == 2) active_m = cfg.filterMode;
        else if (d == 3) active_m = (cfg.heaterMode == 0) ? 0 : 2; // Map Heater 1 (Off) to OFF button at index 2
        else if (d == 4) active_m = cfg.airMode;

        if (active_m >= 0 && active_m < 3 && device_btns[d][active_m] != nullptr) {
            lv_obj_add_state(device_btns[d][active_m], LV_STATE_CHECKED);
        }
    }

    if (device_light_detail_lbl != nullptr) {
        lv_label_set_text_fmt(device_light_detail_lbl, "%02u:%02u-%02u:%02u | %s | %s",
                              cfg.lightStartHour, cfg.lightStartMinute,
                              cfg.lightEndHour, cfg.lightEndMinute,
                              light_color_mode_label(runtime.lightActiveMode),
                              runtime.lightOn ? "ON" : "OFF");
    }
    if (device_plant_detail_lbl != nullptr) {
        lv_label_set_text_fmt(device_plant_detail_lbl, "%02u:%02u-%02u:%02u | %s | %s",
                              cfg.plantStartHour, cfg.plantStartMinute,
                              cfg.plantEndHour, cfg.plantEndMinute,
                              light_color_mode_label(runtime.plantLightActiveMode),
                              runtime.plantLightOn ? "ON" : "OFF");
    }
    if (device_filter_detail_lbl != nullptr) {
        lv_label_set_text_fmt(device_filter_detail_lbl, "%02u:%02u-%02u:%02u | %s",
                              cfg.filterStartHour, cfg.filterStartMinute,
                              cfg.filterEndHour, cfg.filterEndMinute,
                              runtime.filterOn ? "ON" : "OFF");
    }
    if (device_air_detail_lbl != nullptr) {
        lv_label_set_text_fmt(device_air_detail_lbl, "%02u:%02u-%02u:%02u | %s",
                              cfg.airStartHour, cfg.airStartMinute,
                              cfg.airEndHour, cfg.airEndMinute,
                              runtime.airOn ? "ON" : "OFF");
    }
    if (device_heater_detail_lbl != nullptr) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Cel: %.1f*C", cfg.targetTemp);
        lv_label_set_text(device_heater_detail_lbl, buf);
    }
    if (device_ph_detail_lbl != nullptr) {
        lv_label_set_text(device_ph_detail_lbl, cfg.showPhSensor ? "Aktywny" : "OFF");
    }
    if (device_co2_detail_lbl != nullptr) {
        lv_label_set_text(device_co2_detail_lbl, cfg.enableCo2 ? "Modul aktywny" : "OFF");
    }
    if (device_ec_detail_lbl != nullptr) {
        lv_label_set_text(device_ec_detail_lbl, cfg.enableEc ? "Sensor aktywny" : "OFF");
    }
    if (device_water_detail_lbl != nullptr) {
        lv_label_set_text(device_water_detail_lbl, cfg.enableWaterLevel ? "MCP wejscie" : "OFF");
    }
    if (device_leak_detail_lbl != nullptr) {
        lv_label_set_text(device_leak_detail_lbl, cfg.enableLeak ? "Alarm wejscie" : "OFF");
    }
    if (device_flow_detail_lbl != nullptr) {
        lv_label_set_text(device_flow_detail_lbl, cfg.enableFlow ? "Puls wejscie" : "OFF");
    }

    set_checked(hw_heater_sw, cfg.enableHeater);
    set_checked(hw_aerator_sw, cfg.enableAerator);
    set_checked(hw_co2_sw, cfg.enableCo2);
    set_checked(hw_ec_sw, cfg.enableEc);
    set_checked(hw_water_level_sw, cfg.enableWaterLevel);
    set_checked(hw_leak_sw, cfg.enableLeak);
    set_checked(hw_flow_sw, cfg.enableFlow);
    update_hardware_matrix_labels();

    auto format_sched_lbl = [](lv_obj_t *lbl, uint8_t mode,
                               uint8_t startH, uint8_t startM, uint8_t endH, uint8_t endM) {
        if (lbl == nullptr) return;
        if (mode == static_cast<uint8_t>(ScheduleMode::AlwaysOn)) {
            lv_label_set_text(lbl, "Stale WL");
        } else if (mode == static_cast<uint8_t>(ScheduleMode::AlwaysOff)) {
            lv_label_set_text(lbl, "Stale WYL");
        } else {
            lv_label_set_text_fmt(lbl, "%02u:%02u - %02u:%02u", startH, startM, endH, endM);
        }
    };

    format_sched_lbl(sched_light_lbl, cfg.lightMode, cfg.lightStartHour, cfg.lightStartMinute, cfg.lightEndHour, cfg.lightEndMinute);
    format_sched_lbl(sched_plant_lbl, cfg.plantLightMode, cfg.plantStartHour, cfg.plantStartMinute, cfg.plantEndHour, cfg.plantEndMinute);
    format_sched_lbl(sched_filter_lbl, cfg.filterMode, cfg.filterStartHour, cfg.filterStartMinute, cfg.filterEndHour, cfg.filterEndMinute);
    format_sched_lbl(device_air_detail_lbl, cfg.airMode, cfg.airStartHour, cfg.airStartMinute, cfg.airEndHour, cfg.airEndMinute);
    
    if (sched_feed_lbl != nullptr) {
        if (!cfg.feedEnabled) {
            lv_label_set_text(sched_feed_lbl, "OFF");
        } else {
            if (cfg.feedCount == 2) {
                lv_label_set_text_fmt(sched_feed_lbl, "2x/d: %02u:%02u, %02u:%02u",
                                      cfg.feedHour1, cfg.feedMinute1,
                                      cfg.feedHour2, cfg.feedMinute2);
            } else {
                lv_label_set_text_fmt(sched_feed_lbl, "1x/d: %02u:%02u",
                                      cfg.feedHour1, cfg.feedMinute1);
            }
        }
    }

    // Apply 3D Tile Active Styles
    style_tile_3d(tile_light, cfg.lightMode != static_cast<uint8_t>(ScheduleMode::AlwaysOff));
    style_tile_3d(tile_plant, cfg.plantLightMode != static_cast<uint8_t>(ScheduleMode::AlwaysOff));
    style_tile_3d(tile_filter, cfg.filterMode != static_cast<uint8_t>(ScheduleMode::AlwaysOff));
    style_tile_3d(tile_air, cfg.airMode != static_cast<uint8_t>(ScheduleMode::AlwaysOff));
    style_tile_3d(tile_feeder, cfg.feedEnabled);
    style_tile_3d(tile_heater, cfg.heaterMode != static_cast<uint8_t>(HeaterMode::Off));
    style_tile_3d(tile_ph, cfg.showPhSensor);
    style_tile_3d(tile_co2, cfg.enableCo2);
    style_tile_3d(tile_ec, cfg.enableEc);
    style_tile_3d(tile_water, cfg.enableWaterLevel);
    style_tile_3d(tile_leak, cfg.enableLeak);
    style_tile_3d(tile_flow, cfg.enableFlow);

    // Sync feeder subpage widgets
    if (feed_enable_sw != nullptr) {
        set_checked(feed_enable_sw, cfg.feedEnabled);
    }
    if (feed_freq_btn != nullptr) {
        lv_obj_t *freq_lbl = lv_obj_get_child(feed_freq_btn, 0);
        if (freq_lbl != nullptr) {
            lv_label_set_text(freq_lbl, cfg.feedCount == 2 ? "2 razy dziennie" : "1 raz dziennie");
        }
    }
    if (feed_time2_row != nullptr) {
        if (cfg.feedCount == 2) {
            lv_obj_clear_flag(feed_time2_row, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(feed_time2_row, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (feed_time1_h_lbl != nullptr) {
        lv_label_set_text_fmt(feed_time1_h_lbl, "%02u", cfg.feedHour1);
    }
    if (feed_time1_m_lbl != nullptr) {
        lv_label_set_text_fmt(feed_time1_m_lbl, "%02u", cfg.feedMinute1);
    }
    if (feed_time2_h_lbl != nullptr) {
        lv_label_set_text_fmt(feed_time2_h_lbl, "%02u", cfg.feedHour2);
    }
    if (feed_time2_m_lbl != nullptr) {
        lv_label_set_text_fmt(feed_time2_m_lbl, "%02u", cfg.feedMinute2);
    }
    for (int i = 0; i < 7; i++) {
        if (feed_day_btns[i] != nullptr) {
            bool day_active = (cfg.feedDays & (1 << i)) != 0;
            if (day_active) {
                lv_obj_add_state(feed_day_btns[i], LV_STATE_CHECKED);
            } else {
                lv_obj_clear_state(feed_day_btns[i], LV_STATE_CHECKED);
            }
        }
    }

    set_checked(temp_auto_sw, cfg.heaterMode == static_cast<uint8_t>(HeaterMode::Threshold));
    if (temp_target_val_lbl != nullptr) {
        lv_label_set_text_fmt(temp_target_val_lbl, "%.1f*C", cfg.targetTemp);
    }
    if (temp_hysteresis_val_lbl != nullptr) {
        lv_label_set_text_fmt(temp_hysteresis_val_lbl, "%.1f*C", cfg.tempHysteresis);
    }

    set_checked(diag_dev_mode_sw, cfg.devMode);
    set_checked(screen_always_on_sw, cfg.alwaysScreenOn);
    set_checked(screen_manual_theme_sw, cfg.manualLightTheme);
    set_checked(screen_ph_enable_sw, cfg.showPhSensor);
    
    if (screen_ldr_enable_sw != nullptr) {
        if (cfg.ldrThemeEnabled) {
            lv_obj_add_state(screen_ldr_enable_sw, LV_STATE_CHECKED);
            lv_obj_add_state(screen_manual_theme_sw, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(screen_ldr_enable_sw, LV_STATE_CHECKED);
            lv_obj_clear_state(screen_manual_theme_sw, LV_STATE_DISABLED);
        }
    }
    if (chart_target_lbl != nullptr) {
        lv_label_set_text_fmt(chart_target_lbl, "Target %.1f*C  H %.1f", cfg.targetTemp, cfg.tempHysteresis);
    }

    set_checked(sound_enable_sw, cfg.soundEnabled);
    set_checked(sound_quiet_enable_sw, cfg.quietHoursEnabled);
    if (power_modem_sleep_sw != nullptr) {
        set_checked(power_modem_sleep_sw, cfg.modemSleep);
    }
    if (power_state_lbl != nullptr) {
        lv_label_set_text_fmt(power_state_lbl, "LCD %s | WiFi %s",
                              cfg.alwaysScreenOn ? "stale" : "auto",
                              cfg.modemSleep ? "OFF" : (wifi_connected ? "STA" : "gotowe"));
    }
    if (service_light_sw != nullptr) {
        set_checked(service_light_sw, cfg.lightMode != static_cast<uint8_t>(ScheduleMode::AlwaysOff));
    }
    if (service_filter_sw != nullptr) {
        set_checked(service_filter_sw, cfg.filterMode != static_cast<uint8_t>(ScheduleMode::AlwaysOff));
    }
    if (service_vol_lbl != nullptr) {
        lv_label_set_text_fmt(service_vol_lbl, "Vol: %d0%%", musicVolume.load());
    }
    if (sound_quiet_sched_lbl != nullptr) {
        lv_label_set_text_fmt(sound_quiet_sched_lbl, "%02u:%02u - %02u:%02u",
                              static_cast<unsigned>(cfg.quietStartHour),
                              static_cast<unsigned>(cfg.quietStartMinute),
                              static_cast<unsigned>(cfg.quietEndHour),
                              static_cast<unsigned>(cfg.quietEndMinute));
    }

    if (clock_ntp_row != nullptr) {
        if (wifi_connected) {
            lv_obj_clear_flag(clock_ntp_row, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(clock_ntp_row, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void style_chart_btn(lv_obj_t *btn) {
    lv_obj_set_style_bg_color(btn, resolve_bg_color(lv_color_make(35, 41, 55)), 0);
    lv_obj_set_style_bg_color(btn, lv_color_make(6, 182, 212), LV_STATE_CHECKED);
    lv_obj_t *label = lv_obj_get_child(btn, 0);
    if (label != nullptr) {
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
    }
}

static void update_chart_stats() {
    if (history_count == 0) return;
    
    if (active_chart == ActiveChart::Temp) {
        float min_val = temp_history[0];
        float max_val = temp_history[0];
        float cur_val = temp_history[history_count - 1];
        for (uint8_t i = 1; i < history_count; ++i) {
            if (temp_history[i] < min_val) min_val = temp_history[i];
            if (temp_history[i] > max_val) max_val = temp_history[i];
        }
        if (chart_min_lbl != nullptr) lv_label_set_text_fmt(chart_min_lbl, "%.1f*C", min_val);
        if (chart_max_lbl != nullptr) lv_label_set_text_fmt(chart_max_lbl, "%.1f*C", max_val);
        if (chart_cur_lbl != nullptr) lv_label_set_text_fmt(chart_cur_lbl, "%.1f*C", cur_val);
    } else if (active_chart == ActiveChart::Ph) {
        bool found = false;
        float min_val = 0.0f;
        float max_val = 0.0f;
        float cur_val = 0.0f;
        for (uint8_t i = 0; i < history_count; ++i) {
            if (!isfinite(ph_history[i])) {
                continue;
            }
            if (!found) {
                min_val = ph_history[i];
                max_val = ph_history[i];
                found = true;
            }
            if (ph_history[i] < min_val) min_val = ph_history[i];
            if (ph_history[i] > max_val) max_val = ph_history[i];
            cur_val = ph_history[i];
        }
        if (!found) {
            if (chart_min_lbl != nullptr) lv_label_set_text(chart_min_lbl, "--");
            if (chart_max_lbl != nullptr) lv_label_set_text(chart_max_lbl, "--");
            if (chart_cur_lbl != nullptr) lv_label_set_text(chart_cur_lbl, "--");
            return;
        }
        if (chart_min_lbl != nullptr) lv_label_set_text_fmt(chart_min_lbl, "%.2f", min_val);
        if (chart_max_lbl != nullptr) lv_label_set_text_fmt(chart_max_lbl, "%.2f", max_val);
        if (chart_cur_lbl != nullptr) lv_label_set_text_fmt(chart_cur_lbl, "%.2f", cur_val);
    } else if (active_chart == ActiveChart::Ldr) {
        bool found = false;
        int min_val = 0;
        int max_val = 0;
        int cur_val = 0;
        for (uint8_t i = 0; i < history_count; ++i) {
            if (ldr_history[i] < 0) {
                continue;
            }
            if (!found) {
                min_val = ldr_history[i];
                max_val = ldr_history[i];
                found = true;
            }
            if (ldr_history[i] < min_val) min_val = ldr_history[i];
            if (ldr_history[i] > max_val) max_val = ldr_history[i];
            cur_val = ldr_history[i];
        }
        if (!found) {
            if (chart_min_lbl != nullptr) lv_label_set_text(chart_min_lbl, "--");
            if (chart_max_lbl != nullptr) lv_label_set_text(chart_max_lbl, "--");
            if (chart_cur_lbl != nullptr) lv_label_set_text(chart_cur_lbl, "--");
            return;
        }
        if (chart_min_lbl != nullptr) lv_label_set_text_fmt(chart_min_lbl, "%d", min_val);
        if (chart_max_lbl != nullptr) lv_label_set_text_fmt(chart_max_lbl, "%d", max_val);
        if (chart_cur_lbl != nullptr) lv_label_set_text_fmt(chart_cur_lbl, "%d", cur_val);
    } else if (active_chart == ActiveChart::Heap) {
        uint32_t min_val = heap_history[0];
        uint32_t max_val = heap_history[0];
        uint32_t cur_val = heap_history[history_count - 1];
        for (uint8_t i = 1; i < history_count; ++i) {
            if (heap_history[i] < min_val) min_val = heap_history[i];
            if (heap_history[i] > max_val) max_val = heap_history[i];
        }
        if (chart_min_lbl != nullptr) lv_label_set_text_fmt(chart_min_lbl, "%u KB", min_val / 1024);
        if (chart_max_lbl != nullptr) lv_label_set_text_fmt(chart_max_lbl, "%u KB", max_val / 1024);
        if (chart_cur_lbl != nullptr) lv_label_set_text_fmt(chart_cur_lbl, "%u KB", cur_val / 1024);
    }
}


static void append_history_point_at(float temp, bool heater_on, float ph, int ldr, uint32_t epoch) {
    uint32_t current_heap = ESP.getFreeHeap();
    if (history_count < TEMP_HISTORY_POINTS) {
        temp_history[history_count] = temp;
        heater_history[history_count] = heater_on;
        ph_history[history_count] = ph;
        ldr_history[history_count] = ldr;
        heap_history[history_count] = current_heap;
        history_epoch[history_count] = epoch;
        history_count++;
    } else {
        for (uint8_t i = 1; i < TEMP_HISTORY_POINTS; ++i) {
            temp_history[i - 1] = temp_history[i];
            heater_history[i - 1] = heater_history[i];
            ph_history[i - 1] = ph_history[i];
            ldr_history[i - 1] = ldr_history[i];
            heap_history[i - 1] = heap_history[i];
            history_epoch[i - 1] = history_epoch[i];
        }
        temp_history[TEMP_HISTORY_POINTS - 1] = temp;
        heater_history[TEMP_HISTORY_POINTS - 1] = heater_on;
        ph_history[TEMP_HISTORY_POINTS - 1] = ph;
        ldr_history[TEMP_HISTORY_POINTS - 1] = ldr;
        heap_history[TEMP_HISTORY_POINTS - 1] = current_heap;
        history_epoch[TEMP_HISTORY_POINTS - 1] = epoch;
    }
}

static void add_history_point(float temp, bool heater_on, float ph, int ldr) {
    append_history_point_at(temp, heater_on, ph, ldr, controller_unix_time());
}

static void seed_dev_history(float temp, float ph, int ldr) {
    if (!cfg.devMode || history_count != 0 || !isfinite(temp)) {
        return;
    }

    const uint32_t now_epoch = controller_unix_time();
    const uint8_t seed_count = TEMP_HISTORY_POINTS > 0 ? static_cast<uint8_t>(TEMP_HISTORY_POINTS - 1U) : 0U;
    for (uint8_t i = 0; i < seed_count; ++i) {
        const uint8_t age = static_cast<uint8_t>(seed_count - i);
        const float phase = static_cast<float>(age);
        const float seeded_temp = temp + 0.30f * sinf(phase * 0.43f) - 0.10f * cosf(phase * 0.19f);
        const float seeded_ph = isfinite(ph) ? ph + 0.055f * sinf(phase * 0.37f) : NAN;
        int seeded_ldr = -1;
        if (ldr >= 0) {
            seeded_ldr = clamp_ldr_value(ldr + static_cast<int>(lroundf(80.0f * sinf(phase * 0.31f))));
        }
        const bool seeded_heater = seeded_temp < (cfg.targetTemp - cfg.tempHysteresis);
        const uint32_t age_seconds = static_cast<uint32_t>(age) * 60UL;
        const uint32_t sample_epoch = now_epoch > age_seconds ? now_epoch - age_seconds : 0UL;
        append_history_point_at(seeded_temp, seeded_heater, seeded_ph, seeded_ldr, sample_epoch);
    }
}


static void redraw_charts() {
    if (chart_temp == nullptr || chart_temp_series == nullptr) {
        return;
    }

    auto clear_aux_series = []() {
        for (uint8_t i = 0; i < TEMP_HISTORY_POINTS; ++i) {
            if (chart_temp_target_series != nullptr) {
                lv_chart_set_value_by_id(chart_temp, chart_temp_target_series, i, LV_CHART_POINT_NONE);
            }
            if (chart_temp_upper_series != nullptr) {
                lv_chart_set_value_by_id(chart_temp, chart_temp_upper_series, i, LV_CHART_POINT_NONE);
            }
            if (chart_temp_lower_series != nullptr) {
                lv_chart_set_value_by_id(chart_temp, chart_temp_lower_series, i, LV_CHART_POINT_NONE);
            }
            if (chart_temp_heater_series != nullptr) {
                lv_chart_set_value_by_id(chart_temp, chart_temp_heater_series, i, LV_CHART_POINT_NONE);
            }
        }
    };

    if (history_count == 0) {
        for (uint8_t i = 0; i < TEMP_HISTORY_POINTS; ++i) {
            lv_chart_set_value_by_id(chart_temp, chart_temp_series, i, LV_CHART_POINT_NONE);
        }
        clear_aux_series();
        lv_chart_refresh(chart_temp);
        return;
    }

    clear_aux_series();

    if (active_chart == ActiveChart::Temp) {
        float min_temp = temp_history[0];
        float max_temp = temp_history[0];
        for (uint8_t i = 1; i < history_count; ++i) {
            if (temp_history[i] < min_temp) min_temp = temp_history[i];
            if (temp_history[i] > max_temp) max_temp = temp_history[i];
        }

        int low_temp = static_cast<int>(floorf((min(min_temp, cfg.targetTemp - cfg.tempHysteresis) - 0.5f) * 10.0f));
        int high_temp = static_cast<int>(ceilf((max(max_temp, cfg.targetTemp + cfg.tempHysteresis) + 0.5f) * 10.0f));
        if (high_temp - low_temp < 10) high_temp = low_temp + 10;
        lv_chart_set_range(chart_temp, LV_CHART_AXIS_PRIMARY_Y, low_temp, high_temp);

        for (uint8_t i = 0; i < TEMP_HISTORY_POINTS; ++i) {
            if (i < history_count) {
                lv_chart_set_value_by_id(chart_temp, chart_temp_series, i, static_cast<lv_coord_t>(roundf(temp_history[i] * 10.0f)));
                if (chart_temp_target_series != nullptr) {
                    lv_chart_set_value_by_id(chart_temp, chart_temp_target_series, i, static_cast<lv_coord_t>(roundf(cfg.targetTemp * 10.0f)));
                }
                if (chart_temp_upper_series != nullptr) {
                    lv_chart_set_value_by_id(chart_temp, chart_temp_upper_series, i, static_cast<lv_coord_t>(roundf((cfg.targetTemp + cfg.tempHysteresis) * 10.0f)));
                }
                if (chart_temp_lower_series != nullptr) {
                    lv_chart_set_value_by_id(chart_temp, chart_temp_lower_series, i, static_cast<lv_coord_t>(roundf((cfg.targetTemp - cfg.tempHysteresis) * 10.0f)));
                }
                if (chart_temp_heater_series != nullptr) {
                    lv_chart_set_value_by_id(chart_temp, chart_temp_heater_series, i, heater_history[i] ? (low_temp + (high_temp - low_temp) * 0.15) : low_temp);
                }
            } else {
                lv_chart_set_value_by_id(chart_temp, chart_temp_series, i, LV_CHART_POINT_NONE);
            }
        }
    } else if (active_chart == ActiveChart::Ph) {
        bool found_ph = false;
        float min_ph = 0.0f;
        float max_ph = 0.0f;
        for (uint8_t i = 0; i < history_count; ++i) {
            if (!isfinite(ph_history[i])) {
                continue;
            }
            if (!found_ph) {
                min_ph = ph_history[i];
                max_ph = ph_history[i];
                found_ph = true;
            }
            if (ph_history[i] < min_ph) min_ph = ph_history[i];
            if (ph_history[i] > max_ph) max_ph = ph_history[i];
        }
        if (!found_ph) {
            lv_chart_set_range(chart_temp, LV_CHART_AXIS_PRIMARY_Y, 0, 1);
            for (uint8_t i = 0; i < TEMP_HISTORY_POINTS; ++i) {
                lv_chart_set_value_by_id(chart_temp, chart_temp_series, i, LV_CHART_POINT_NONE);
            }
            lv_chart_refresh(chart_temp);
            return;
        }
        int low_ph = static_cast<int>(floorf((min_ph - 0.2f) * 100.0f));
        int high_ph = static_cast<int>(ceilf((max_ph + 0.2f) * 100.0f));
        if (high_ph - low_ph < 20) high_ph = low_ph + 20;
        lv_chart_set_range(chart_temp, LV_CHART_AXIS_PRIMARY_Y, low_ph, high_ph);

        for (uint8_t i = 0; i < TEMP_HISTORY_POINTS; ++i) {
            lv_chart_set_value_by_id(chart_temp, chart_temp_series, i,
                                     i < history_count && isfinite(ph_history[i])
                                         ? static_cast<lv_coord_t>(roundf(ph_history[i] * 100.0f))
                                         : LV_CHART_POINT_NONE);
        }
    } else if (active_chart == ActiveChart::Ldr) {
        bool found_ldr = false;
        int min_ldr = 0;
        int max_ldr = 0;
        for (uint8_t i = 0; i < history_count; ++i) {
            if (ldr_history[i] < 0) {
                continue;
            }
            if (!found_ldr) {
                min_ldr = ldr_history[i];
                max_ldr = ldr_history[i];
                found_ldr = true;
            }
            if (ldr_history[i] < min_ldr) min_ldr = ldr_history[i];
            if (ldr_history[i] > max_ldr) max_ldr = ldr_history[i];
        }
        if (!found_ldr) {
            lv_chart_set_range(chart_temp, LV_CHART_AXIS_PRIMARY_Y, 0, 1);
            for (uint8_t i = 0; i < TEMP_HISTORY_POINTS; ++i) {
                lv_chart_set_value_by_id(chart_temp, chart_temp_series, i, LV_CHART_POINT_NONE);
            }
            lv_chart_refresh(chart_temp);
            return;
        }
        int low_ldr = max(0, min_ldr - 20);
        int high_ldr = min(LDR_ADC_MAX, max_ldr + 20);
        if (high_ldr - low_ldr < 50) {
            high_ldr = min(LDR_ADC_MAX, low_ldr + 50);
            if (high_ldr - low_ldr < 50) {
                low_ldr = max(0, high_ldr - 50);
            }
        }
        lv_chart_set_range(chart_temp, LV_CHART_AXIS_PRIMARY_Y, low_ldr, high_ldr);

        for (uint8_t i = 0; i < TEMP_HISTORY_POINTS; ++i) {
            lv_chart_set_value_by_id(chart_temp, chart_temp_series, i,
                                     i < history_count && ldr_history[i] >= 0
                                         ? static_cast<lv_coord_t>(ldr_history[i])
                                         : LV_CHART_POINT_NONE);
        }
    } else {
        uint32_t min_heap = heap_history[0];
        uint32_t max_heap = heap_history[0];
        for (uint8_t i = 1; i < history_count; ++i) {
            if (heap_history[i] < min_heap) min_heap = heap_history[i];
            if (heap_history[i] > max_heap) max_heap = heap_history[i];
        }
        int low_heap = static_cast<int>(min_heap / 1024U) - 5;
        int high_heap = static_cast<int>(max_heap / 1024U) + 5;
        if (low_heap < 0) low_heap = 0;
        if (high_heap - low_heap < 10) high_heap = low_heap + 10;
        lv_chart_set_range(chart_temp, LV_CHART_AXIS_PRIMARY_Y, low_heap, high_heap);

        for (uint8_t i = 0; i < TEMP_HISTORY_POINTS; ++i) {
            lv_chart_set_value_by_id(chart_temp, chart_temp_series, i,
                                     i < history_count ? static_cast<lv_coord_t>(heap_history[i] / 1024U) : LV_CHART_POINT_NONE);
        }
    }

    lv_chart_refresh(chart_temp);
}


static void select_chart_cb(lv_event_t *e) {
    play_system_sound(SoundType::Click);
    ActiveChart selection = static_cast<ActiveChart>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    active_chart = selection;

    if (btn_chart_temp != nullptr) lv_obj_clear_state(btn_chart_temp, LV_STATE_CHECKED);
    if (btn_chart_ph != nullptr) lv_obj_clear_state(btn_chart_ph, LV_STATE_CHECKED);
    if (btn_chart_ldr != nullptr) lv_obj_clear_state(btn_chart_ldr, LV_STATE_CHECKED);
    if (btn_chart_heap != nullptr) lv_obj_clear_state(btn_chart_heap, LV_STATE_CHECKED);

    if (selection == ActiveChart::Temp) {
        if (btn_chart_temp != nullptr) lv_obj_add_state(btn_chart_temp, LV_STATE_CHECKED);
        if (chart_target_lbl != nullptr) lv_obj_clear_flag(chart_target_lbl, LV_OBJ_FLAG_HIDDEN);
    } else if (selection == ActiveChart::Ph) {
        if (btn_chart_ph != nullptr) lv_obj_add_state(btn_chart_ph, LV_STATE_CHECKED);
        if (chart_target_lbl != nullptr) lv_obj_add_flag(chart_target_lbl, LV_OBJ_FLAG_HIDDEN);
    } else if (selection == ActiveChart::Ldr) {
        if (btn_chart_ldr != nullptr) lv_obj_add_state(btn_chart_ldr, LV_STATE_CHECKED);
        if (chart_target_lbl != nullptr) lv_obj_add_flag(chart_target_lbl, LV_OBJ_FLAG_HIDDEN);
    } else if (selection == ActiveChart::Heap) {
        if (btn_chart_heap != nullptr) lv_obj_add_state(btn_chart_heap, LV_STATE_CHECKED);
        if (chart_target_lbl != nullptr) lv_obj_add_flag(chart_target_lbl, LV_OBJ_FLAG_HIDDEN);
    }

    redraw_charts();
    update_chart_stats();
}


static void update_charts_data(float temp, float ph) {
    const int ldr_sample = last_ldr_valid ? last_ldr_value : -1;
    if (!cfg.devMode) {
        history_archive_append_sample(temp,
                                      runtime.heaterOn,
                                      ph,
                                      ldr_sample,
                                      ESP.getFreeHeap());
    }
    if (!isfinite(temp)) {
        return;
    }
    seed_dev_history(temp, ph, ldr_sample);
    add_history_point(temp, runtime.heaterOn, ph, ldr_sample);
    if (!gui_web_focus_blocks_local_ui()) {
        redraw_charts();
        update_chart_stats();
    }
}

static void reset_gui_object_refs() {
    memset(pages, 0, sizeof(pages));
    memset(nav_btns, 0, sizeof(nav_btns));
    log_list_normal = nullptr;
    log_list_important = nullptr;
    btn_log_normal = nullptr;
    btn_log_important = nullptr;
    memset(device_btns, 0, sizeof(device_btns));

    label_date = nullptr;
    label_power_mode = nullptr;
    label_rtc_bat = nullptr;
    label_wifi_state = nullptr;
    label_clock_time = nullptr;
    label_clock_date = nullptr;
    home_temp_current = nullptr;
    home_ph_current = nullptr;
    home_temp_target_lbl = nullptr;
    home_temp_trend_lbl = nullptr;
    home_feed_time_lbl = nullptr;
    home_light_state_lbl = nullptr;
    home_light_mode_lbl = nullptr;
    home_light_color_lbl = nullptr;
    home_plant_state_lbl = nullptr;
    home_plant_mode_lbl = nullptr;
    home_plant_color_lbl = nullptr;
    home_filter_state_lbl = nullptr;
    home_filter_mode_lbl = nullptr;
    home_air_state_lbl = nullptr;
    home_heater_state_lbl = nullptr;
    home_heater_mode_lbl = nullptr;
    home_air_mode_lbl = nullptr;
    device_light_mode_lbl = nullptr;
    device_light_detail_lbl = nullptr;
    device_plant_mode_lbl = nullptr;
    device_plant_detail_lbl = nullptr;
    device_filter_mode_lbl = nullptr;
    device_filter_detail_lbl = nullptr;
    device_heater_mode_lbl = nullptr;
    device_heater_detail_lbl = nullptr;
    device_air_mode_lbl = nullptr;
    device_air_detail_lbl = nullptr;
    device_ph_detail_lbl = nullptr;
    sched_light_lbl = nullptr;
    sched_plant_lbl = nullptr;
    sched_filter_lbl = nullptr;
    sched_air_lbl = nullptr;
    sched_feed_lbl = nullptr;
    temp_auto_sw = nullptr;
    temp_target_val_lbl = nullptr;
    temp_hysteresis_val_lbl = nullptr;
    temp_pump_power_lbl = nullptr;
    temp_pump_power_slider = nullptr;
    feed_mode_val_lbl = nullptr;
    subpage_wifi = nullptr;
    subpage_clock = nullptr;
    clock_ntp_row = nullptr;
    tile_light = nullptr;
    tile_plant = nullptr;
    tile_filter = nullptr;
    tile_feeder = nullptr;
    tile_heater = nullptr;
    tile_ph = nullptr;
    tile_air = nullptr;
    tile_co2 = nullptr;
    tile_ec = nullptr;
    tile_water = nullptr;
    tile_leak = nullptr;
    tile_flow = nullptr;
    device_co2_detail_lbl = nullptr;
    device_ec_detail_lbl = nullptr;
    device_water_detail_lbl = nullptr;
    device_leak_detail_lbl = nullptr;
    device_flow_detail_lbl = nullptr;
    memset(feed_day_btns, 0, sizeof(feed_day_btns));
    feed_enable_sw = nullptr;
    feed_freq_btn = nullptr;
    feed_time2_row = nullptr;
    feed_time1_h_lbl = nullptr;
    feed_time1_m_lbl = nullptr;
    feed_time2_h_lbl = nullptr;
    feed_time2_m_lbl = nullptr;
    subpage_diagnostics = nullptr;
    subpage_power = nullptr;
    subpage_screen = nullptr;
    subpage_logs = nullptr;
    subpage_sched_editor = nullptr;
    subpage_feed_editor = nullptr;
    editor_start_h_lbl = nullptr;
    editor_start_m_lbl = nullptr;
    feed_editor_mode_lbl = nullptr;
    modal_feeder = nullptr;
    wifi_ssid_lbl = nullptr;
    wifi_ip_lbl = nullptr;
    wifi_status_message_lbl = nullptr;
    wifi_mode_lbl = nullptr;
    wifi_rssi_lbl = nullptr;
    wifi_mac_lbl = nullptr;
    diag_dev_mode_sw = nullptr;
    diag_uptime_lbl = nullptr;
    diag_heap_lbl = nullptr;
    diag_reset_reason_lbl = nullptr;
    diag_restarts_lbl = nullptr;
    diag_cpu_temp_lbl = nullptr;
    diag_cpu_freq_lbl = nullptr;
    diag_flash_lbl = nullptr;
    diag_adc_lbl = nullptr;
    diag_mcp_lbl = nullptr;
    diag_queue_lbl = nullptr;
    diag_ldr_lbl = nullptr;
    diag_eco_lbl = nullptr;
    diag_rtc_lbl = nullptr;
    power_warning_lbl_global = nullptr;
    power_state_lbl = nullptr;
    pin_overlay = nullptr;
    pin_value_lbl = nullptr;
    pin_status_lbl = nullptr;
    pin_matrix = nullptr;
    pin_entry[0] = '\0';
    pin_last_key_ms = 0;
    memset(&time_picker_state, 0, sizeof(time_picker_state));
    memset(&date_picker_state, 0, sizeof(date_picker_state));
    screen_always_on_sw = nullptr;
    screen_manual_theme_sw = nullptr;
    screen_ph_enable_sw = nullptr;
    screen_ldr_enable_sw = nullptr;
    btn_sync_ntp_global = nullptr;
    btn_sync_ntp_lbl_global = nullptr;
    modal_feeder_title_lbl = nullptr;
    modal_feeder_msg_lbl = nullptr;
    chart_temp = nullptr;
    chart_temp_series = nullptr;
    chart_min_lbl = nullptr;
    chart_max_lbl = nullptr;
    chart_cur_lbl = nullptr;
    chart_target_lbl = nullptr;
    chart_range_lbl = nullptr;
    editor_title_lbl = nullptr;
    editor_mode_lbl = nullptr;
    editor_start_hour_lbl = nullptr;
    editor_start_min_lbl = nullptr;
    editor_hour_lbl = nullptr;
    editor_hourly_mode_lbl = nullptr;
    sched_editor_start_h_lbl = nullptr;
    sched_editor_start_m_lbl = nullptr;
    sched_editor_end_h_lbl = nullptr;
    sched_editor_end_m_lbl = nullptr;
    sched_editor_color_row = nullptr;
    subpage_sounds = nullptr;
    sound_enable_sw = nullptr;
    sound_quiet_enable_sw = nullptr;
    sound_quiet_sched_lbl = nullptr;
    power_modem_sleep_sw = nullptr;
    subpage_heater = nullptr;
    subpage_ph = nullptr;
    subpage_hardware = nullptr;
    subpage_co2 = nullptr;
    subpage_ec = nullptr;
    subpage_water = nullptr;
    subpage_leak = nullptr;
    subpage_flow = nullptr;
    co2_state_lbl = nullptr;
    co2_ph_lbl = nullptr;
    co2_mcp_lbl = nullptr;
    ec_value_lbl = nullptr;
    ec_raw_lbl = nullptr;
    water_state_lbl = nullptr;
    leak_state_lbl = nullptr;
    flow_state_lbl = nullptr;
    calib_value_lbl = nullptr;
    calib_active_type = -1;
    hw_heater_sw = nullptr;
    hw_aerator_sw = nullptr;
    hw_co2_sw = nullptr;
    hw_ec_sw = nullptr;
    hw_water_level_sw = nullptr;
    hw_leak_sw = nullptr;
    hw_flow_sw = nullptr;
    hw_matrix = nullptr;
    hw_summary_lbl = nullptr;
    subpage_service = nullptr;
    service_light_sw = nullptr;
    service_filter_sw = nullptr;
    service_vol_lbl = nullptr;
    for (int i=0; i<4; i++) editor_mode_btns[i] = nullptr;
    for (int i=0; i<24; i++) timeline_blocks[i] = nullptr;
    btn_chart_temp = nullptr;
    btn_chart_ph = nullptr;
    btn_chart_ldr = nullptr;
    btn_chart_heap = nullptr;
    chart_ph = nullptr;
    chart_ph_series = nullptr;
    chart_ldr = nullptr;
    chart_ldr_series = nullptr;
    chart_heap = nullptr;
    chart_heap_series = nullptr;
    chart_temp_target_series = nullptr;
    chart_temp_upper_series = nullptr;
    chart_temp_lower_series = nullptr;
    chart_temp_heater_series = nullptr;

    // Reset WiFi Panels & State variables
    wifi_main_panel = nullptr;
    wifi_sta_panel = nullptr;
    wifi_pwd_panel = nullptr;
    wifi_ota_panel = nullptr;
    btn_sta = nullptr;
    btn_ota = nullptr;
    btn_disconnect = nullptr;
    wifi_pwd_ta = nullptr;
    wifi_pwd_kb = nullptr;
    wifi_pwd_title_lbl = nullptr;
    web_client_screen = nullptr;
    web_client_state_lbl = nullptr;
    web_client_url_lbl = nullptr;
    web_client_status_lbl = nullptr;
    web_client_ssid_lbl = nullptr;
    web_client_ip_lbl = nullptr;
    web_client_rssi_lbl = nullptr;
    web_client_uptime_lbl = nullptr;
    web_client_progress_bar = nullptr;
    web_client_progress_lbl = nullptr;
    web_ui_last_screen_ms = 0;
}


static void apply_lvgl_theme() {
    lv_disp_t *disp = lv_disp_get_default();
    lv_theme_t *theme = lv_theme_default_init(
        disp,
        lv_palette_main(LV_PALETTE_CYAN),
        lv_palette_main(LV_PALETTE_GREY),
        !ui_light_theme,
        &lv_font_montserrat_14
    );
    lv_disp_set_theme(disp, theme);
    lv_obj_set_style_bg_color(lv_scr_act(), theme_screen_bg(), 0);
    lv_obj_set_style_text_font(lv_scr_act(), &lv_font_montserrat_14, 0);
    lv_obj_clear_flag(lv_scr_act(), LV_OBJ_FLAG_SCROLLABLE);
}

static void build_gui_tree() {
    apply_lvgl_theme();

    // Splash screen removed — insufficient heap after GUI tree construction.

    log_ram_checkpoint("gui_tree_start");
    build_status_bar();
    build_nav_bar();
    if (!build_page_by_index(static_cast<uint8_t>(current_page_index))) {
        current_page_index = 0;
        build_page_by_index(0);
    }

    const esp_reset_reason_t reason = esp_reset_reason();
    if (diag_reset_reason_lbl != nullptr) {
        lv_label_set_text_fmt(diag_reset_reason_lbl, "Reset: %u", static_cast<unsigned>(reason));
    }
    log_ram_checkpoint("gui_tree_ready");
}





// build_splash_screen() removed — caused LoadProhibited crash due to heap exhaustion
// after build_gui_tree() leaves only ~44 bytes of contiguous free heap.




static void rebuild_gui_tree_for_theme() {
    const int restore_page = current_page_index;
    const ActiveSubpage restore_subpage = current_subpage;
    // SSID-y z listy mają ręcznie zarządzane user_data; trzeba je zwolnić
    // zanim LVGL usunie obiekty nadrzędne.
    free_wifi_scan_user_data();
    lv_obj_clean(lv_scr_act());
    reset_gui_object_refs();
    current_page_index = (restore_page >= 0 && restore_page < PAGE_COUNT) ? restore_page : 0;
    current_subpage = ActiveSubpage::None;
    build_gui_tree();
    prime_pin_guard_modal();

    // Przywrócenie aktywnej strony (zakładki)
    sync_nav_bar_visuals();

    // Przywrócenie aktywnej podstrony
    if (restore_subpage != ActiveSubpage::None) {
        open_or_build_subpage(restore_subpage);
    }

    gui_sync_widgets_to_state();
    gui_app_update_wifi(wifi_connected ? 1 : 0, wifi_rssi);
    redraw_charts();
    update_chart_stats();
}

} // namespace

bool gui_app_sync_init(void) {
    if (gui_mutex != nullptr) {
        return true;
    }
    gui_mutex = xSemaphoreCreateRecursiveMutexStatic(&gui_mutex_storage);
    return gui_mutex != nullptr;
}

bool gui_app_lock(uint32_t timeout_ms) {
    return gui_mutex != nullptr &&
           xSemaphoreTakeRecursive(gui_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void gui_app_unlock(void) {
    if (gui_mutex != nullptr) {
        xSemaphoreGiveRecursive(gui_mutex);
    }
}

void gui_app_init(void) {
    if (!gui_app_sync_init()) {
        Serial.println("GUI: nie można utworzyć blokady synchronizacji.");
        return;
    }
    GuiMutexGuard guard(2000U);
    if (!guard.locked()) {
        Serial.println("GUI: timeout blokady podczas inicjalizacji.");
        return;
    }
    gui_ready = false;
    device_credentials_initialize();
    gui_app_load_settings();
    const RuntimeSafetyStatus safety_status =
        runtime_safety_status();
    if (!alarm_event_queue_initialize(safety_status.boot_id)) {
        Serial.println(
            "ALARMS: persistent transition queue unavailable.");
    }
    register_wifi_event_handlers();
    if (cfg.modemSleep) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        wifi_connected = false;
        wifi_rssi = 0;
        Serial.println("GUI: Modem Sleep active on boot, Wi-Fi radio disabled.");
    }
    if (cfg.ldrThemeEnabled && !cfg.devMode) {
        const int ldr_val = analogRead(HwConfig::LDR_PIN);
        bool light_theme = cfg.manualLightTheme;
        if (ldr_value_to_light_theme(ldr_val, &light_theme)) {
            ui_light_theme = light_theme;
        } else {
            ui_light_theme = cfg.manualLightTheme;
        }
        last_ldr_value = ldr_val;
        last_ldr_valid = true;
    } else {
        ui_light_theme = cfg.manualLightTheme;
        last_ldr_valid = false;
    }

    reset_gui_object_refs();
    build_gui_tree();
    gui_sync_widgets_to_state();
    wifi_autoconnect_pending = !cfg.modemSleep;
    gui_app_update_wifi(wifi_connected ? 1 : 0, wifi_rssi);
    prime_pin_guard_modal();

    lv_mem_monitor_t mem_mon;
    lv_mem_monitor(&mem_mon);
    Serial.printf(
        "GUI: LVGL heap used: %lu/%lu B (%u%%), largest free block: %lu B, fragmentation: %u%%\n",
        static_cast<unsigned long>(mem_mon.total_size - mem_mon.free_size),
        static_cast<unsigned long>(mem_mon.total_size),
        static_cast<unsigned>(mem_mon.used_pct),
        static_cast<unsigned long>(mem_mon.free_biggest_size),
        static_cast<unsigned>(mem_mon.frag_pct)
    );



    if (audio_queue == nullptr) {
        audio_queue = xQueueCreateStatic(
            6U,
            sizeof(AudioEffect),
            audio_queue_buffer,
            &audio_queue_storage);
    }
    if (musicTaskHandle == nullptr && audio_queue != nullptr) {
        xTaskCreatePinnedToCore(
            music_player_task,
            "music_player_task",
            4096,
            nullptr,
            1,
            &musicTaskHandle,
            0
        );
    }
    gui_ready = true;
}

void gui_app_service_background(void) {
    GuiMutexGuard guard(50U);
    if (!guard.locked() || !gui_ready) {
        return;
    }

    static uint32_t last_wifi_service_ms = 0U;
    if (ota_start_pending) {
        ota_start_pending = false;
        start_ota_background();
    }
    if (wifi_autoconnect_pending) {
        wifi_autoconnect_pending = false;
        try_autoconnect_wifi_profile();
    }
    if (wifi_scan_prepare_pending) {
        wifi_scan_prepare_pending = false;
        prepare_wifi_sta_radio();
        scan_started = false;
        scan_start_ms = millis();
    }
    if (wifi_connect_pending) {
        wifi_connect_pending = false;
        if (!begin_sta_connection(selected_ssid, pending_wifi_password)) {
            is_connecting = false;
            clear_pending_wifi_password();
            if (wifi_status_message_lbl != nullptr) {
                lv_label_set_text(wifi_status_message_lbl,
                                  "Status: nie uruchomiono WiFi");
                lv_obj_set_style_text_color(
                    wifi_status_message_lbl,
                    lv_color_make(239, 68, 68),
                    0);
            }
        }
    }
    const uint32_t now_ms = millis();
    if (static_cast<uint32_t>(now_ms - last_wifi_service_ms) >= 500U) {
        last_wifi_service_ms = now_ms;
        wifi_check_timer_cb(nullptr);
    }
    service_ntp_sync(now_ms);
    service_feeder_pulse(now_ms);

    if (wifi_ota_active) {
#if AQUARIUM_ALLOW_UNSIGNED_ARDUINO_OTA
        ArduinoOTA.handle();
#endif
    }
    gui_app_handle_ota_portal();
    apply_mcp_outputs();
}

void gui_app_handle_ota_portal(void) {
    GuiMutexGuard guard(50U);
    if (!guard.locked() || !gui_ready) {
        return;
    }
    update_relay_tests();
    if (ota_reboot_pending &&
        static_cast<int32_t>(millis() - ota_reboot_at_ms) >= 0) {
        Serial.println("SYSTEM: restarting after an authorized request.");
        const bool outputs_safe =
            hal_mcp_latch_all_relays_safe();
        runtime_safety_record_restart(
            ota_reboot_reason,
            outputs_safe);
        ESP.restart();
    }
    if (wifi_disconnect_pending && static_cast<int32_t>(millis() - wifi_disconnect_at_ms) >= 0) {
        wifi_disconnect_pending = false;
        stop_ota_portal();
        stop_mdns_service();
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        wifi_connected = false;
        wifi_rssi = 0;
        is_connecting = false;
        gui_app_update_wifi(0, 0);
        return;
    }
    if (!ota_portal_running) {
        gui_web_focus_update();
        return;
    }

    if (ota_portal_dns_running) {
        ota_dns_server.processNextRequest();
    }
    ota_http_server.handleClient();
    gui_web_focus_update();

    if (ota_shutdown_pending && static_cast<int32_t>(millis() - ota_shutdown_at_ms) >= 0) {
        Serial.println("HTTP_OTA: stopping OTA portal by web request.");
        stop_ota_runtime(false);
    }
}

static uint8_t wifi_signal_bars(int rssi) {
    if (rssi >= -55) {
        return 4U;
    }
    if (rssi >= -67) {
        return 3U;
    }
    if (rssi >= -75) {
        return 2U;
    }
    return 1U;
}

void gui_app_update_ble_pairing(uint32_t passkey, uint8_t state) {
    const uint32_t now_ms = millis();
    uint32_t visible_for_ms = 0U;
    if (state == 1U) {
        visible_for_ms = 60000U;
    } else if (state == 2U) {
        visible_for_ms = 5000U;
    } else if (state == 3U) {
        visible_for_ms = 8000U;
    }
    portENTER_CRITICAL(&ble_pairing_mux);
    ble_pairing_passkey = state == 1U ? passkey : 0U;
    ble_pairing_state = state <= 3U ? state : 0U;
    ble_pairing_until_ms =
        visible_for_ms > 0U ? now_ms + visible_for_ms : 0U;
    portEXIT_CRITICAL(&ble_pairing_mux);
}

void gui_app_update_wifi(int state, int rssi) {
    uint8_t pairing_state = 0U;
    uint32_t pairing_passkey = 0U;
    uint32_t pairing_until_ms = 0U;
    portENTER_CRITICAL(&ble_pairing_mux);
    pairing_state = ble_pairing_state;
    pairing_passkey = ble_pairing_passkey;
    pairing_until_ms = ble_pairing_until_ms;
    portEXIT_CRITICAL(&ble_pairing_mux);

    if (pairing_state != 0U &&
        static_cast<int32_t>(millis() - pairing_until_ms) >= 0) {
        gui_app_update_ble_pairing(0U, 0U);
        pairing_state = 0U;
    }
    if (pairing_state != 0U) {
        const lv_color_t pairing_color =
            pairing_state == 2U
                ? lv_color_make(16, 185, 129)
                : (pairing_state == 3U
                       ? lv_color_make(239, 68, 68)
                       : lv_color_make(6, 182, 212));
        if (label_wifi_state != nullptr) {
            if (pairing_state == 1U) {
                lv_label_set_text_fmt(
                    label_wifi_state,
                    "%06lu",
                    static_cast<unsigned long>(pairing_passkey));
            } else {
                lv_label_set_text(
                    label_wifi_state,
                    pairing_state == 2U ? "BLE OK" : "BLE ERR");
            }
            lv_obj_set_style_text_color(label_wifi_state, pairing_color, 0);
        }
        if (wifi_status_message_lbl != nullptr) {
            if (pairing_state == 1U) {
                lv_label_set_text_fmt(
                    wifi_status_message_lbl,
                    "BLE: wpisz kod %06lu w telefonie",
                    static_cast<unsigned long>(pairing_passkey));
            } else {
                lv_label_set_text(
                    wifi_status_message_lbl,
                    pairing_state == 2U
                        ? "BLE: telefon bezpiecznie sparowany"
                        : "BLE: parowanie odrzucone lub nieudane");
            }
            lv_obj_set_style_text_color(
                wifi_status_message_lbl, pairing_color, 0);
        }
        return;
    }

    if (gui_web_focus_blocks_local_ui()) {
        gui_web_client_screen_update(false);
        return;
    }

    if (label_wifi_state == nullptr) {
        return;
    }

    if (clock_ntp_row != nullptr) {
        if (state == 1) {
            lv_obj_clear_flag(clock_ntp_row, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(clock_ntp_row, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (state == 0) {
        snprintf(status_ip_address, sizeof(status_ip_address), "0.0.0.0");
        lv_label_set_text(label_wifi_state, is_connecting ? "JOIN" : "OFF");
        lv_obj_set_style_text_color(label_wifi_state,
                                    is_connecting ? lv_color_make(245, 158, 11) : lv_color_make(239, 68, 68),
                                    0);
        if (wifi_info_card != nullptr) {
            lv_obj_set_style_border_color(wifi_info_card, is_connecting ? lv_color_make(245, 158, 11) : lv_color_make(239, 68, 68), 0);
        }
        if (wifi_mode_lbl != nullptr) {
            lv_label_set_text(wifi_mode_lbl, is_connecting ? "LACZENIE..." : "ROZLACZONY");
            lv_obj_set_style_text_color(wifi_mode_lbl, is_connecting ? lv_color_make(245, 158, 11) : lv_color_make(239, 68, 68), 0);
        }
        if (wifi_rssi_lbl != nullptr) {
            lv_label_set_text(wifi_rssi_lbl, "RSSI: --");
        }
        if (wifi_mac_lbl != nullptr) {
            lv_label_set_text(wifi_mac_lbl, "Portal: --");
        }

        if (is_connecting) {
            if (wifi_ssid_lbl != nullptr) {
                char temp_ssid_buf[96];
                snprintf(temp_ssid_buf, sizeof(temp_ssid_buf), LV_SYMBOL_WIFI "  SSID: %s", selected_ssid[0] != '\0' ? selected_ssid : "Laczenie...");
                lv_label_set_text(wifi_ssid_lbl, temp_ssid_buf);
            }
            if (wifi_ip_lbl != nullptr) {
                lv_label_set_text(wifi_ip_lbl, LV_SYMBOL_RIGHT "  IP: pobieranie...");
            }
            if (wifi_status_message_lbl != nullptr) {
                lv_label_set_text(wifi_status_message_lbl, "Laczenie z siecia...");
                lv_obj_set_style_text_color(wifi_status_message_lbl, lv_color_make(245, 158, 11), 0);
            }
        } else {
            if (wifi_ssid_lbl != nullptr) {
                lv_label_set_text(wifi_ssid_lbl, LV_SYMBOL_WIFI "  SSID: --");
            }
            if (wifi_ip_lbl != nullptr) {
                lv_label_set_text(wifi_ip_lbl, LV_SYMBOL_RIGHT "  IP: --");
            }
            if (wifi_status_message_lbl != nullptr) {
                const char *curr_txt = lv_label_get_text(wifi_status_message_lbl);
                if (strncmp(curr_txt, "Status: Blad", 12) != 0 &&
                    strncmp(curr_txt, "Status: Bledne", 14) != 0 &&
                    strncmp(curr_txt, "Status: Timeout", 15) != 0 &&
                    strncmp(curr_txt, "Status: Przekroczono", 20) != 0 &&
                    strncmp(curr_txt, "Status: Siec", 12) != 0) {
                    lv_label_set_text(wifi_status_message_lbl, "Brak polaczenia WiFi");
                    lv_obj_set_style_text_color(wifi_status_message_lbl, theme_text_muted(), 0);
                }
            }
        }

        if (btn_sta != nullptr && !is_connecting) lv_obj_clear_flag(btn_sta, LV_OBJ_FLAG_HIDDEN);
        if (btn_ota != nullptr && !is_connecting) lv_obj_clear_flag(btn_ota, LV_OBJ_FLAG_HIDDEN);
        if (btn_disconnect != nullptr) lv_obj_add_flag(btn_disconnect, LV_OBJ_FLAG_HIDDEN);
    } else if (state == 1) {
        const uint8_t bars = wifi_signal_bars(rssi);
        lv_label_set_text_fmt(label_wifi_state, "%u %d", static_cast<unsigned>(bars), rssi);
        lv_obj_set_style_text_color(
            label_wifi_state,
            bars >= 3U ? lv_color_make(16, 185, 129)
                       : (bars == 2U ? lv_color_make(245, 158, 11)
                                    : lv_color_make(239, 68, 68)),
            0);
        if (wifi_info_card != nullptr) {
            lv_obj_set_style_border_color(wifi_info_card, lv_color_make(16, 185, 129), 0);
        }
        if (wifi_mode_lbl != nullptr) {
            lv_label_set_text(wifi_mode_lbl, "ONLINE");
            lv_obj_set_style_text_color(wifi_mode_lbl, lv_color_make(16, 185, 129), 0);
        }

        wifi_config_t station_config = {};
        const bool station_config_ok =
            esp_wifi_get_config(WIFI_IF_STA, &station_config) == ESP_OK;
        const char *current_ssid = station_config_ok
                                       ? reinterpret_cast<const char *>(station_config.sta.ssid)
                                       : "";
        const IPAddress current_ip = WiFi.localIP();
        snprintf(status_ip_address, sizeof(status_ip_address), "%u.%u.%u.%u",
                 current_ip[0], current_ip[1], current_ip[2], current_ip[3]);

        if (wifi_ssid_lbl != nullptr) {
            char temp_ssid_buf[96];
            snprintf(temp_ssid_buf, sizeof(temp_ssid_buf), LV_SYMBOL_WIFI "  SSID: %s",
                     current_ssid[0] != '\0'
                         ? current_ssid
                         : (selected_ssid[0] != '\0' ? selected_ssid : "Aquarium_STA"));
            lv_label_set_text(wifi_ssid_lbl, temp_ssid_buf);
        }

        if (wifi_ip_lbl != nullptr) {
            char temp_ip_buf[64];
            snprintf(temp_ip_buf, sizeof(temp_ip_buf), LV_SYMBOL_RIGHT "  IP: %s",
                     status_ip_address);
            lv_label_set_text(wifi_ip_lbl, temp_ip_buf);
        }
        if (wifi_rssi_lbl != nullptr) {
            lv_label_set_text_fmt(wifi_rssi_lbl, "RSSI: %d dBm", rssi);
        }
        if (wifi_mac_lbl != nullptr) {
            if (ota_portal_running) {
                lv_label_set_text_fmt(wifi_mac_lbl, "Portal: %s.local | %s",
                                      Secrets::OTA_HOSTNAME,
                                      status_ip_address);
            } else {
                lv_label_set_text(wifi_mac_lbl, "Portal: zatrzymany");
            }
        }

        if (btn_sta != nullptr) lv_obj_add_flag(btn_sta, LV_OBJ_FLAG_HIDDEN);
        if (btn_ota != nullptr) lv_obj_add_flag(btn_ota, LV_OBJ_FLAG_HIDDEN);
        if (btn_disconnect != nullptr) lv_obj_clear_flag(btn_disconnect, LV_OBJ_FLAG_HIDDEN);

        if (wifi_status_message_lbl != nullptr) {
            lv_label_set_text(wifi_status_message_lbl,
                              ota_portal_running ? "Panel HTTP gotowy" : "STA aktywne, portal zatrzymany");
            lv_obj_set_style_text_color(wifi_status_message_lbl, lv_color_make(16, 185, 129), 0);
        }
    } else {
        lv_label_set_text(label_wifi_state, "AP");
        lv_obj_set_style_text_color(label_wifi_state, lv_color_make(6, 182, 212), 0);
        if (wifi_mode_lbl != nullptr) {
            lv_label_set_text(wifi_mode_lbl, "AP");
            lv_obj_set_style_text_color(wifi_mode_lbl, lv_color_make(6, 182, 212), 0);
        }

        wifi_config_t access_point_config = {};
        const bool access_point_config_ok =
            esp_wifi_get_config(WIFI_IF_AP, &access_point_config) == ESP_OK;
        const char *soft_ssid = access_point_config_ok
                                    ? reinterpret_cast<const char *>(access_point_config.ap.ssid)
                                    : "";
        const IPAddress soft_ip = WiFi.softAPIP();
        snprintf(status_ip_address, sizeof(status_ip_address), "%u.%u.%u.%u",
                 soft_ip[0], soft_ip[1], soft_ip[2], soft_ip[3]);

        if (wifi_ssid_lbl != nullptr) {
            char temp_ssid_buf[96];
            snprintf(temp_ssid_buf, sizeof(temp_ssid_buf), "SSID: %s",
                     soft_ssid[0] != '\0' ? soft_ssid : Secrets::OTA_AP_SSID);
            lv_label_set_text(wifi_ssid_lbl, temp_ssid_buf);
        }

        if (wifi_ip_lbl != nullptr) {
            char temp_ip_buf[64];
            snprintf(temp_ip_buf, sizeof(temp_ip_buf), "IP: %s", status_ip_address);
            lv_label_set_text(wifi_ip_lbl, temp_ip_buf);
        }
        if (wifi_rssi_lbl != nullptr) {
            lv_label_set_text(wifi_rssi_lbl, "Sygnal: AP");
        }
        if (wifi_mac_lbl != nullptr) {
            lv_label_set_text(wifi_mac_lbl, "Portal: http://192.168.4.1/");
        }

        if (btn_sta != nullptr) lv_obj_add_flag(btn_sta, LV_OBJ_FLAG_HIDDEN);
        if (btn_ota != nullptr) lv_obj_add_flag(btn_ota, LV_OBJ_FLAG_HIDDEN);
        if (btn_disconnect != nullptr) lv_obj_add_flag(btn_disconnect, LV_OBJ_FLAG_HIDDEN);

        if (wifi_status_message_lbl != nullptr) {
            const char *curr_txt = lv_label_get_text(wifi_status_message_lbl);
            if (strncmp(curr_txt, "OTA:", 4) != 0) {
                lv_label_set_text(wifi_status_message_lbl, "Tryb OTA aktywny");
                lv_obj_set_style_text_color(wifi_status_message_lbl, lv_color_make(6, 182, 212), 0);
            }
        }
    }

    gui_web_focus_apply_wifi_controls(true);
}


void gui_update_metrics(float temp, float ph, uint32_t free_heap, const char *time_str) {
    if (cfg.devMode) {
        const float phase = static_cast<float>((millis() / 1000UL) % 3600UL) / 60.0f;
        if (!isfinite(temp)) {
            temp = 25.8f + 0.45f * sinf(phase * 0.17f) + 0.10f * cosf(phase * 0.07f);
        }
        if (!isfinite(ph)) {
            ph = 6.92f + 0.12f * sinf(phase * 0.11f);
        }
    }

    const bool temp_valid = isfinite(temp);
    const bool ph_valid = isfinite(ph);
    status_last_sample_ms = millis();
    status_temperature_ok = temp_valid;
    if (temp_valid) {
        runtime.lastTemp = temp;
    }
    if (ph_valid) {
        runtime.lastPh = ph;
    }

    int hr = clock_hour;
    int mn = clock_minute;
    int sc = clock_second;
    if (time_str != nullptr) {
        sscanf(time_str, "%d:%d:%d", &hr, &mn, &sc);
    }
    const uint16_t nowMins = static_cast<uint16_t>(constrain(hr, 0, 23)) * 60U +
                             static_cast<uint16_t>(constrain(mn, 0, 59));
    const bool output_hardware_available =
        !cfg.devMode && sensor_debug.mcpPresent && sensor_debug.mcpValid;
    const bool output_runtime_available = cfg.devMode || output_hardware_available;

    uint8_t factory_light_profile = 0U;
    const bool factory_light_active = factory_light_profile_at(nowMins, &factory_light_profile);
    runtime.lightOn = output_runtime_available &&
                      (cfg.lightMode == static_cast<uint8_t>(ScheduleMode::Schedule) && config_uses_factory_light_window()
                           ? factory_light_active
                           : schedule_active(cfg.lightMode, nowMins, cfg.lightStartHour, cfg.lightStartMinute, cfg.lightEndHour, cfg.lightEndMinute));
    runtime.lightActiveMode = cfg.lightMode == static_cast<uint8_t>(ScheduleMode::Schedule)
                                  ? (config_uses_factory_light_window()
                                         ? factory_light_profile
                                         : schedule_profile_to_aquael(cfg.lightSchedColorMode))
                                  : normalize_aquael_profile(cfg.lightColorMode);
    runtime.plantLightOn = output_runtime_available &&
                           (cfg.plantLightMode == static_cast<uint8_t>(ScheduleMode::Schedule) && config_uses_factory_light2_window()
                                ? factory_light_active
                                : schedule_active(cfg.plantLightMode, nowMins, cfg.plantStartHour, cfg.plantStartMinute, cfg.plantEndHour, cfg.plantEndMinute));
    runtime.plantLightActiveMode = cfg.plantLightMode == static_cast<uint8_t>(ScheduleMode::Schedule)
                                       ? (config_uses_factory_light2_window()
                                              ? factory_light_profile
                                              : schedule_profile_to_aquael(cfg.plantSchedColorMode))
                                       : normalize_aquael_profile(cfg.plantLightColorMode);
    runtime.filterOn = output_runtime_available &&
                       schedule_active(cfg.filterMode, nowMins, cfg.filterStartHour, cfg.filterStartMinute, cfg.filterEndHour, cfg.filterEndMinute);
    const bool aerator_window_active = schedule_active(cfg.airMode,
                                                       nowMins,
                                                       cfg.airStartHour,
                                                       cfg.airStartMinute,
                                                       cfg.airEndHour,
                                                       cfg.airEndMinute);

    const uint16_t water_level_mask = static_cast<uint16_t>(1U << static_cast<uint8_t>(HwConfig::CH_WATER_LEVEL));
    const uint16_t leak_mask = static_cast<uint16_t>(1U << static_cast<uint8_t>(HwConfig::CH_LEAK));
    const bool water_level_valid = sensor_debug.mcpValid;
    const bool water_level_high = water_level_valid && ((sensor_debug.mcpState & water_level_mask) != 0U);
    const bool leak_detected = sensor_debug.mcpValid && ((sensor_debug.mcpState & leak_mask) != 0U);
    const bool leak_valve_interlock = cfg.enableLeak && leak_detected &&
                                      leak_action != LeakAction::AlarmOnly;
    const bool co2_window_active = is_within_window(nowMins,
                                                    FACTORY_CO2_AIR_START_HOUR,
                                                    FACTORY_CO2_AIR_START_MINUTE,
                                                    FACTORY_CO2_AIR_END_HOUR,
                                                    FACTORY_CO2_AIR_END_MINUTE);
    aquarium::GasControlOutput gas_control = aquarium::evaluate_gas_control({
        output_runtime_available,
        cfg.enableCo2,
        cfg.enableAerator,
        co2_window_active,
        aerator_window_active,
        ph_valid,
        ph,
        co2_target_ph,
        leak_valve_interlock
    });

    static uint32_t co2_started_ms = 0U;
    static bool co2_limit_latched = false;
    if (gas_control.co2On) {
        const uint32_t now_ms = millis();
        if (co2_started_ms == 0U) {
            co2_started_ms = now_ms;
        }
        const uint32_t limit_ms = static_cast<uint32_t>(co2_max_time_minutes) * 60000UL;
        if (static_cast<uint32_t>(now_ms - co2_started_ms) >= limit_ms) {
            gas_control.co2On = false;
            gas_control.aeratorOn = cfg.enableAerator && aerator_window_active && !leak_valve_interlock;
            if (!co2_limit_latched) {
                add_gui_log("CO2: przekroczono limit czasu dozowania", true);
                co2_limit_latched = true;
            }
        }
    } else {
        co2_started_ms = 0U;
        co2_limit_latched = false;
    }
    runtime.co2On = gas_control.co2On;
    runtime.airOn = gas_control.aeratorOn;

    if (water_level_high) {
        ato_timeout_latched = false;
    }
    const bool old_water_fill_on = runtime.waterFillOn;
    runtime.waterFillOn = aquarium::evaluate_ato_control({
        output_runtime_available,
        cfg.enableWaterLevel,
        water_level_valid,
        water_level_high,
        cfg.enableLeak && leak_detected,
        ato_timeout_latched
    });
    if (runtime.waterFillOn) {
        const uint32_t now_ms = millis();
        if (ato_started_ms == 0U) {
            ato_started_ms = now_ms == 0U ? 1U : now_ms;
        }
        const uint32_t timeout_ms = static_cast<uint32_t>(water_timeout_seconds) * 1000UL;
        if (static_cast<uint32_t>(now_ms - ato_started_ms) >= timeout_ms) {
            runtime.waterFillOn = false;
            ato_timeout_latched = true;
            ato_started_ms = 0U;
            add_gui_log("ATO: przekroczono limit pracy pompy", true);
        }
    } else {
        ato_started_ms = 0U;
    }
    if (runtime.waterFillOn != old_water_fill_on) {
        add_gui_log(runtime.waterFillOn ? "ATO: rozpoczęto dolewanie" : "ATO: zatrzymano dolewanie",
                    ato_timeout_latched);
    }

    bool old_heater_on = runtime.heaterOn;
    runtime.heaterOn = aquarium::thermostat_next_state({
        output_runtime_available,
        cfg.enableHeater,
        cfg.heaterMode == static_cast<uint8_t>(HeaterMode::Threshold),
        temp_valid,
        temp,
        cfg.targetTemp,
        cfg.tempHysteresis,
        runtime.heaterOn
    });

    const uint32_t control_now_ms = millis();
    runtime.lightOn = control_modes.resolve(
        aquarium::OutputTarget::Light1, runtime.lightOn, control_now_ms);
    runtime.plantLightOn = control_modes.resolve(
        aquarium::OutputTarget::Light2, runtime.plantLightOn, control_now_ms);
    runtime.filterOn = control_modes.resolve(
        aquarium::OutputTarget::Filter, runtime.filterOn, control_now_ms);
    runtime.airOn = control_modes.resolve(
        aquarium::OutputTarget::Aeration, runtime.airOn, control_now_ms);
    runtime.heaterOn =
        control_modes.resolve(
            aquarium::OutputTarget::Heater, runtime.heaterOn, control_now_ms) &&
        temp_valid;
    runtime.co2On =
        control_modes.resolve(
            aquarium::OutputTarget::Co2, runtime.co2On, control_now_ms) &&
        ph_valid && !leak_valve_interlock;
    runtime.waterFillOn =
        control_modes.resolve(
            aquarium::OutputTarget::WaterDosing,
            runtime.waterFillOn,
            control_now_ms) &&
        water_level_valid && !water_level_high &&
        !(cfg.enableLeak && leak_detected) && !ato_timeout_latched;
    if (!runtime.waterFillOn) {
        ato_started_ms = 0U;
    }

    const bool disable_all_for_leak = cfg.enableLeak && leak_detected &&
                                      leak_action == LeakAction::DisableAll;
    if (disable_all_for_leak) {
        runtime.lightOn = false;
        runtime.plantLightOn = false;
        runtime.filterOn = false;
        runtime.airOn = false;
        runtime.co2On = false;
        runtime.heaterOn = false;
        runtime.waterFillOn = false;
    }

    static bool leak_interlock_latched = false;
    if ((leak_valve_interlock || disable_all_for_leak) != leak_interlock_latched) {
        leak_interlock_latched = leak_valve_interlock || disable_all_for_leak;
        add_gui_log(leak_interlock_latched
                        ? "Wyciek: zastosowano zapisana akcje bezpieczenstwa"
                        : "Wyciek: blokada bezpieczenstwa zwolniona",
                    leak_interlock_latched);
    }

    if (runtime.heaterOn != old_heater_on) {
        if (runtime.heaterOn) {
            add_gui_log("Ogrzewanie: Wlaczone", false);
        } else {
            add_gui_log("Ogrzewanie: Wylaczone", false);
        }
    }

    static bool high_temp_warn = false;
    static bool low_temp_warn = false;
    if (isfinite(temp)) {
        if (temp > 28.0f && !high_temp_warn) {
            high_temp_warn = true;
            add_gui_log("Wysoka temperatura wody!", true);
        } else if (temp <= 28.0f && high_temp_warn) {
            high_temp_warn = false;
            add_gui_log("Temperatura wody w normie", false);
        }
        
        if (temp < 20.0f && !low_temp_warn) {
            low_temp_warn = true;
            add_gui_log("Niska temperatura wody!", true);
        } else if (temp >= 20.0f && low_temp_warn) {
            low_temp_warn = false;
            add_gui_log("Temperatura wody w normie", false);
        }
    }

    static bool ph_warn = false;
    if (ph_valid) {
        if ((ph < 6.0f || ph > 8.0f) && !ph_warn) {
            ph_warn = true;
            char ph_buf[64];
            snprintf(ph_buf, sizeof(ph_buf), "Niepoprawne pH: %.2f", ph);
            add_gui_log(ph_buf, true);
        } else if (ph >= 6.0f && ph <= 8.0f && ph_warn) {
            ph_warn = false;
            add_gui_log("Poziom pH ustabilizowany", false);
        }
    }

    const unsigned int previous_alarm_flags =
        current_alarm_flags;
    const unsigned int observed_alarm_flags =
        cfg.devMode
            ? aquarium::dev_simulator()
                  .latest()
                  .alarmFlags
            : evaluate_live_alarm_flags(
                  temp,
                  temp_valid,
                  ph,
                  ph_valid);
    current_alarm_flags =
        alarm_stability_filter.update(
            observed_alarm_flags);
    const uint32_t alarm_timestamp =
        controller_clock_reliable
            ? controller_unix_time()
            : control_now_ms / 1000UL;
    const bool alarm_queue_saved =
        alarm_event_queue_update(
            current_alarm_flags,
            alarm_timestamp,
            controller_clock_reliable);
    if (previous_alarm_flags != current_alarm_flags) {
        Serial.printf(
            "ALARMS: flags 0x%04x -> 0x%04x queue=%s.\n",
            static_cast<unsigned>(previous_alarm_flags),
            static_cast<unsigned>(current_alarm_flags),
            alarm_queue_saved ? "ok" : "volatile");
        constexpr unsigned int health_alarm_mask =
            aquarium::AlarmSensorMissing |
            aquarium::AlarmSensorStale |
            aquarium::AlarmSensorBusFault |
            aquarium::AlarmActuatorWriteFailed;
        const unsigned int raised_health =
            current_alarm_flags &
            ~previous_alarm_flags &
            health_alarm_mask;
        const unsigned int cleared_health =
            previous_alarm_flags &
            ~current_alarm_flags &
            health_alarm_mask;
        if (raised_health != 0U) {
            add_gui_log(
                "Sprzet: wykryto brak, stale dane lub blad wyjscia",
                true);
        }
        if (cleared_health != 0U &&
            (current_alarm_flags & health_alarm_mask) == 0U) {
            add_gui_log(
                "Sprzet: czujniki i wyjscia ponownie sprawne",
                false);
        }
    }

    int wday = get_weekday(clock_day, clock_month, clock_year);
    int bit_idx = (wday == 0) ? 6 : (wday - 1);
    bool day_active = (cfg.feedDays & (1 << bit_idx)) != 0;

    if (cfg.feedEnabled && day_active && sc == 0) {
        bool time_match = (hr == cfg.feedHour1 && mn == cfg.feedMinute1) ||
                          (cfg.feedCount == 2 && hr == cfg.feedHour2 && mn == cfg.feedMinute2);
        if (time_match) {
            const uint32_t nowMs = millis();
            if (runtime.lastAutoFeedMs == 0 || nowMs - runtime.lastAutoFeedMs > 60000UL) {
                if (run_feeder_pulse("Karmienie", "Dawka z harmonogramu", true)) {
                    Serial.println("GUI: Scheduled feeding triggered.");
                }
            }
        }
    }

    // History belongs to the control path, not the local display path. Core 0
    // applies the desired output state after this calculation releases the
    // shared mutex.
    update_charts_data(temp, ph);
    if (gui_web_focus_blocks_local_ui()) {
        return;
    }

    if (label_power_mode != nullptr) {
        if (temp_valid) {
            lv_label_set_text_fmt(label_power_mode, "T %.1f*C", temp);
        } else {
            lv_label_set_text(label_power_mode, "T --.-*C");
        }
    }
    if (label_clock_time != nullptr && time_str != nullptr) {
        lv_label_set_text(label_clock_time, time_str);
    }
    if (label_clock_date != nullptr) {
        char date_str[32];
        static const char *months[] = {
            "Jan", "Feb", "Mar", "Apr", "May", "Jun",
            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
        };
        const char *month_name = (clock_month >= 1 && clock_month <= 12) ? months[clock_month - 1] : "May";
        snprintf(date_str, sizeof(date_str), "%d %s %d", clock_day, month_name, clock_year);
        lv_label_set_text(label_clock_date, date_str);
    }

    if (home_temp_current != nullptr) {
        if (temp_valid) {
            lv_label_set_text_fmt(home_temp_current, "%.1f", temp);
        } else {
            lv_label_set_text(home_temp_current, "--");
        }
    }
    if (home_ph_current != nullptr) {
        if (ph_valid) {
            lv_label_set_text_fmt(home_ph_current, "%.2f", ph);
        } else {
            lv_label_set_text(home_ph_current, "--");
        }
    }
    if (home_temp_trend_lbl != nullptr) {
        if (!temp_valid) {
            lv_label_set_text(home_temp_trend_lbl, "Brak danych");
            lv_obj_set_style_text_color(home_temp_trend_lbl, theme_text_muted(), 0);
        } else if (temp > runtime.previousTemp + 0.05f) {
            lv_label_set_text(home_temp_trend_lbl, "Rosnie");
            lv_obj_set_style_text_color(home_temp_trend_lbl, lv_color_make(239, 68, 68), 0);
        } else if (temp < runtime.previousTemp - 0.05f) {
            lv_label_set_text(home_temp_trend_lbl, "Spada");
            lv_obj_set_style_text_color(home_temp_trend_lbl, lv_color_make(6, 182, 212), 0);
        } else {
            lv_label_set_text(home_temp_trend_lbl, "Stabilna");
            lv_obj_set_style_text_color(home_temp_trend_lbl, lv_color_make(16, 185, 129), 0);
        }
    }
    if (temp_valid) {
        runtime.previousTemp = temp;
    }

    gui_sync_widgets_to_state();
    gui_app_update_system_info(free_heap, millis() / 1000UL);
}

void gui_app_update_status_bar(uint32_t uptime_sec) {
    const uint32_t now_ms = millis();
    const uint32_t measured_age_sec =
        status_last_sample_ms == 0U
            ? 99U
            : static_cast<uint32_t>(now_ms - status_last_sample_ms) / 1000UL;
    const uint32_t age_sec = measured_age_sec > 99U ? 99U : measured_age_sec;

    const char *ip_tail = strrchr(status_ip_address, '.');
    ip_tail = ip_tail != nullptr ? ip_tail + 1 : "--";
    if (strcmp(status_ip_address, "0.0.0.0") == 0) {
        ip_tail = "--";
    }
    if (label_date != nullptr) {
        lv_label_set_text_fmt(
            label_date,
            "%02d:%02d .%s %lus",
            constrain(clock_hour, 0, 23),
            constrain(clock_minute, 0, 59),
            ip_tail,
            static_cast<unsigned long>(age_sec));
    }

    if (label_rtc_bat == nullptr) {
        return;
    }

    char uptime_text[8];
    if (uptime_sec >= 86400UL) {
        snprintf(uptime_text, sizeof(uptime_text), "%lud",
                 static_cast<unsigned long>(uptime_sec / 86400UL));
    } else if (uptime_sec >= 3600UL) {
        snprintf(uptime_text, sizeof(uptime_text), "%luh",
                 static_cast<unsigned long>(uptime_sec / 3600UL));
    } else if (uptime_sec >= 60UL) {
        snprintf(uptime_text, sizeof(uptime_text), "%lum",
                 static_cast<unsigned long>(uptime_sec / 60UL));
    } else {
        snprintf(uptime_text, sizeof(uptime_text), "%lus",
                 static_cast<unsigned long>(uptime_sec));
    }

    const bool stale = status_last_sample_ms == 0U || age_sec > 5U;
    const bool healthy = status_sensor_bus_ok && status_temperature_ok && !stale;
    char badge[8];
    snprintf(badge, sizeof(badge), "%c%s",
             cfg.devMode ? 'D' : (healthy ? ' ' : (stale ? '~' : '!')),
             uptime_text);
    lv_label_set_text(label_rtc_bat, badge);
    lv_obj_set_style_text_color(
        label_rtc_bat,
        cfg.devMode ? lv_color_make(245, 158, 11)
                    : (healthy ? lv_color_make(16, 185, 129)
                               : (stale ? lv_color_make(245, 158, 11)
                                        : lv_color_make(248, 113, 113))),
        0);
    lv_obj_set_style_bg_color(
        label_rtc_bat,
        resolve_bg_color(
            cfg.devMode ? lv_color_make(69, 26, 3)
                        : (healthy ? lv_color_make(6, 78, 59)
                                   : (stale ? lv_color_make(69, 45, 3)
                                            : lv_color_make(69, 10, 10)))),
        0);
}

void gui_app_update_system_info(uint32_t free_heap, uint32_t uptime_sec) {
    if (gui_web_focus_blocks_local_ui()) {
        return;
    }
    const RuntimeSafetyStatus safety =
        runtime_safety_status();

    if (diag_heap_lbl != nullptr) {
        const uint32_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
        lv_label_set_text_fmt(diag_heap_lbl, "RAM wolne: %lu KB | blok: %lu KB",
                              static_cast<unsigned long>((free_heap + 512UL) / 1024UL),
                              static_cast<unsigned long>((largest_block + 512UL) / 1024UL));
    }

    if (diag_restarts_lbl != nullptr) {
        lv_label_set_text_fmt(
            diag_restarts_lbl,
            "Boot: %lu | awarie: %lu",
            static_cast<unsigned long>(
                safety.boot_count),
            static_cast<unsigned long>(
                safety.fault_count));
    }

    if (diag_reset_reason_lbl != nullptr) {
        esp_reset_reason_t reason = esp_reset_reason();
        const char *reason_str = "Unknown";
        switch (reason) {
            case ESP_RST_POWERON: reason_str = "Power On Reset"; break;
            case ESP_RST_EXT: reason_str = "External Pin Reset"; break;
            case ESP_RST_SW: reason_str = "Software Reset"; break;
            case ESP_RST_PANIC: reason_str = "Exception/Panic"; break;
            case ESP_RST_INT_WDT: reason_str = "Interrupt WDT"; break;
            case ESP_RST_TASK_WDT: reason_str = "Task WDT"; break;
            case ESP_RST_WDT: reason_str = "Other Watchdog"; break;
            case ESP_RST_DEEPSLEEP: reason_str = "Deep Sleep Reset"; break;
            case ESP_RST_BROWNOUT: reason_str = "Brownout Reset"; break;
            case ESP_RST_SDIO: reason_str = "SDIO Reset"; break;
            default: break;
        }
        const char *fault_code =
            runtime_fault_reason_code(
                safety.last_reset.fault_reason);
        if (safety.last_reset.fault_reason !=
            RuntimeFaultReason::None) {
            lv_label_set_text_fmt(
                diag_reset_reason_lbl,
                "Reset: %s (%s)",
                fault_code,
                safety.last_reset.fail_safe_confirmed
                    ? "safe"
                    : "unsafe");
        } else {
            lv_label_set_text_fmt(
                diag_reset_reason_lbl,
                "Reset: %s",
                reason_str);
        }
    }

    if (diag_uptime_lbl != nullptr) {
        const uint32_t days = uptime_sec / 86400UL;
        const uint32_t hours = (uptime_sec % 86400UL) / 3600UL;
        const uint32_t minutes = (uptime_sec % 3600UL) / 60UL;
        const uint32_t seconds = uptime_sec % 60UL;
        if (days > 0) {
            lv_label_set_text_fmt(diag_uptime_lbl, "Czas: %lud %luh %lum | Boot: %lu",
                                  static_cast<unsigned long>(days),
                                  static_cast<unsigned long>(hours),
                                  static_cast<unsigned long>(minutes),
                                  static_cast<unsigned long>(safety.boot_count));
        } else if (hours > 0) {
            lv_label_set_text_fmt(diag_uptime_lbl, "Czas: %luh %lum %lus | Boot: %lu",
                                  static_cast<unsigned long>(hours),
                                  static_cast<unsigned long>(minutes),
                                  static_cast<unsigned long>(seconds),
                                  static_cast<unsigned long>(safety.boot_count));
        } else {
            lv_label_set_text_fmt(diag_uptime_lbl, "Czas: %lum %lus | Boot: %lu",
                                  static_cast<unsigned long>(minutes),
                                  static_cast<unsigned long>(seconds),
                                  static_cast<unsigned long>(safety.boot_count));
        }
    }

    if (diag_cpu_temp_lbl != nullptr) {
        #ifdef ESP32
        const float cpu_temp = temperatureRead();
        if (cpu_temp > 0.0f) {
            const int temp_x10 = static_cast<int>(lroundf(cpu_temp * 10.0f));
            lv_label_set_text_fmt(diag_cpu_temp_lbl, "CPU: %d.%d*C / %d MHz | Flash %d MB",
                                  temp_x10 / 10,
                                  abs(temp_x10 % 10),
                                  ESP.getCpuFreqMHz(),
                                  ESP.getFlashChipSize() / (1024 * 1024));
        } else {
            lv_label_set_text_fmt(diag_cpu_temp_lbl, "CPU: --.-*C / %d MHz | Flash %d MB",
                                  ESP.getCpuFreqMHz(),
                                  ESP.getFlashChipSize() / (1024 * 1024));
        }
        #else
        lv_label_set_text(diag_cpu_temp_lbl, "CPU: --.-*C / -- MHz | Flash -- MB");
        #endif
    }

    if (diag_cpu_freq_lbl != nullptr) {
        lv_label_set_text_fmt(diag_cpu_freq_lbl, "CPU Freq: %d MHz", ESP.getCpuFreqMHz());
    }

    if (diag_flash_lbl != nullptr) {
        lv_label_set_text_fmt(diag_flash_lbl, "Flash size: %d MB", ESP.getFlashChipSize() / (1024 * 1024));
    }

    if (diag_eco_lbl != nullptr || diag_rtc_lbl != nullptr) {
        const EcoRuntimeStatus eco = eco_collect_status();
        char blockers[72];
        eco_blockers_to_csv(eco.blockers, blockers, sizeof(blockers));

        if (diag_eco_lbl != nullptr) {
            lv_label_set_text_fmt(diag_eco_lbl, "ECO: %s | Deep %s",
                                  eco.safeEcoActive ? "okno" : "standby",
                                  eco.deepReady ? "READY" : "LOCK");
            lv_obj_set_style_text_color(diag_eco_lbl,
                                        eco.deepReady ? lv_color_make(16, 185, 129) :
                                        (eco.safeEcoActive ? lv_color_make(245, 158, 11) : theme_text_main()),
                                        0);
        }

        if (diag_rtc_lbl != nullptr) {
            const uint32_t wake_min = (eco.plannedWakeAfterSec + 59UL) / 60UL;
            lv_label_set_text_fmt(diag_rtc_lbl, "RTC: %s | wake %lum | %s",
                                  eco.rtcReady ? "OK" : "--",
                                  static_cast<unsigned long>(wake_min),
                                  blockers);
            lv_obj_set_style_text_color(diag_rtc_lbl,
                                        eco.deepReady ? lv_color_make(16, 185, 129) : theme_text_muted(),
                                        0);
        }
    }
}

void gui_app_update_sensor_debug(int ldr_value,
                                 bool temperature_present,
                                 bool temperature_stale,
                                 uint32_t temperature_age_ms,
                                 uint32_t temperature_error_count,
                                 bool adc_present,
                                 bool ph_valid,
                                 int16_t ph_raw,
                                 float ph_voltage,
                                 float ph_value,
                                 bool ec_valid,
                                 int16_t ec_raw,
                                 float ec_voltage,
                                 float ec_value,
                                 bool mcp_present,
                                 bool mcp_valid,
                                 uint16_t mcp_state) {
    sensor_debug.ldrValue = ldr_value;
    sensor_debug.temperaturePresent = temperature_present;
    sensor_debug.temperatureStale = temperature_stale;
    sensor_debug.temperatureAgeMs = temperature_age_ms;
    sensor_debug.temperatureErrorCount = temperature_error_count;
    sensor_debug.adcPresent = adc_present;
    sensor_debug.phValid = ph_valid;
    sensor_debug.phRaw = ph_raw;
    sensor_debug.phVoltage = ph_voltage;
    sensor_debug.phValue = ph_value;
    sensor_debug.ecValid = ec_valid;
    sensor_debug.ecRaw = ec_raw;
    sensor_debug.ecVoltage = ec_voltage;
    sensor_debug.ecValue = ec_value;
    sensor_debug.mcpPresent = mcp_present;
    sensor_debug.mcpValid = mcp_valid;
    sensor_debug.mcpState = mcp_state;
    sensor_debug.updatedMs = millis();
    const bool adc_health_ok =
        (!cfg.showPhSensor || (adc_present && ph_valid)) &&
        (!cfg.enableEc || (adc_present && ec_valid));
    status_sensor_bus_ok =
        cfg.devMode || (mcp_present && mcp_valid && adc_health_ok);

    const auto mcp_bit = [mcp_valid, mcp_state](HwConfig::McpChannel channel) {
        if (!mcp_valid) {
            return false;
        }
        const uint16_t bit = static_cast<uint16_t>(1U << static_cast<uint8_t>(channel));
        return (mcp_state & bit) != 0;
    };
    const bool water_raw = mcp_bit(HwConfig::CH_WATER_LEVEL);
    const bool leak_raw = mcp_bit(HwConfig::CH_LEAK);
    const bool flow_raw = mcp_bit(HwConfig::CH_FLOW_PULSE);

    if (cfg.devMode && mcp_valid) {
        static bool dev_inputs_initialized = false;
        static bool previous_water_high = true;
        static bool previous_leak = false;
        static bool previous_supply_low = false;
        const bool supply_low = (aquarium::dev_simulator().latest().alarmFlags & aquarium::AlarmSupplyLow) != 0U;

        if (dev_inputs_initialized) {
            if (water_raw != previous_water_high) {
                add_gui_log(water_raw ? "DEV: poziom wody przywrocony" : "DEV: niski poziom wody", !water_raw);
            }
            if (leak_raw != previous_leak) {
                add_gui_log(leak_raw ? "DEV: wykryto wyciek" : "DEV: wyciek ustapil", leak_raw);
            }
            if (supply_low != previous_supply_low) {
                add_gui_log(supply_low ? "DEV: niskie napiecie zasilania" : "DEV: zasilanie stabilne", supply_low);
            }
        }
        previous_water_high = water_raw;
        previous_leak = leak_raw;
        previous_supply_low = supply_low;
        dev_inputs_initialized = true;
    }

    if (gui_web_focus_blocks_local_ui()) {
        return;
    }

    update_calibration_value_label();

    if (diag_adc_lbl != nullptr) {
        if (adc_present) {
            lv_label_set_text_fmt(diag_adc_lbl, "ADS: OK | pH %s | EC %s",
                                  ph_valid ? "OK" : "--",
                                  ec_valid ? "OK" : "--");
            lv_obj_set_style_text_color(diag_adc_lbl, theme_text_main(), 0);
        } else {
            lv_label_set_text(diag_adc_lbl, "ADS: brak | pH -- | EC --");
            lv_obj_set_style_text_color(diag_adc_lbl, lv_color_make(239, 68, 68), 0);
        }
    }
    if (diag_mcp_lbl != nullptr) {
        if (mcp_present && mcp_valid) {
            lv_label_set_text_fmt(diag_mcp_lbl, "MCP: OK | maska 0x%04X", static_cast<unsigned>(mcp_state));
            lv_obj_set_style_text_color(diag_mcp_lbl, theme_text_main(), 0);
        } else if (mcp_present) {
            lv_label_set_text(diag_mcp_lbl, "MCP: blad odczytu");
            lv_obj_set_style_text_color(diag_mcp_lbl, lv_color_make(245, 158, 11), 0);
        } else {
            lv_label_set_text(diag_mcp_lbl, "MCP: brak");
            lv_obj_set_style_text_color(diag_mcp_lbl, lv_color_make(239, 68, 68), 0);
        }
    }
    if (diag_queue_lbl != nullptr) {
        if (ldr_value >= 0) {
            lv_label_set_text_fmt(diag_queue_lbl, "EVT overflow: %lu | LDR: %d",
                                  static_cast<unsigned long>(events_sample_overflow_count()),
                                  ldr_value);
        } else {
            lv_label_set_text_fmt(diag_queue_lbl, "EVT overflow: %lu | LDR: --",
                                  static_cast<unsigned long>(events_sample_overflow_count()));
        }
    }
    if (diag_ldr_lbl != nullptr) {
        if (ldr_value >= 0) {
            lv_label_set_text_fmt(diag_ldr_lbl, "LDR GPIO34: %d", ldr_value);
        } else {
            lv_label_set_text(diag_ldr_lbl, "LDR GPIO34: --");
        }
    }

    if (co2_state_lbl != nullptr) {
        const bool co2_on = cfg.devMode ? runtime.co2On : mcp_bit(HwConfig::CH_CO2);
        lv_label_set_text(co2_state_lbl, (mcp_valid || cfg.devMode) ? (co2_on ? "Stan: ON" : "Stan: OFF") : "Stan: --");
        lv_obj_set_style_text_color(co2_state_lbl,
                                    co2_on ? lv_color_make(16, 185, 129) : theme_text_main(), 0);
    }
    if (co2_mcp_lbl != nullptr) {
        lv_label_set_text_fmt(co2_mcp_lbl, "MCP: %s", mcp_valid ? "OK" : "--");
    }
    if (co2_ph_lbl != nullptr) {
        if (ph_valid) {
            lv_label_set_text_fmt(co2_ph_lbl, "pH: %.2f V: %.3f", runtime.lastPh, ph_voltage);
        } else {
            lv_label_set_text(co2_ph_lbl, "pH: brak odczytu");
        }
    }

    if (ec_value_lbl != nullptr) {
        if (ec_valid) {
            lv_label_set_text_fmt(ec_value_lbl, "%.3f V", ec_voltage);
        } else {
            lv_label_set_text(ec_value_lbl, "--");
        }
    }
    if (ec_raw_lbl != nullptr) {
        if (ec_valid) {
            lv_label_set_text_fmt(ec_raw_lbl, "ADS1115 A1 raw: %d", static_cast<int>(ec_raw));
        } else {
            lv_label_set_text(ec_raw_lbl, "ADS1115 A1: brak odczytu");
        }
    }

    if (water_state_lbl != nullptr) {
        lv_label_set_text(water_state_lbl, mcp_valid ? (water_raw ? "RAW HIGH" : "RAW LOW") : "--");
    }
    if (leak_state_lbl != nullptr) {
        lv_label_set_text(leak_state_lbl, mcp_valid ? (leak_raw ? "RAW HIGH" : "RAW LOW") : "--");
        lv_obj_set_style_text_color(leak_state_lbl,
                                    (mcp_valid && leak_raw) ? lv_color_make(239, 68, 68) : theme_text_main(), 0);
    }
    if (flow_state_lbl != nullptr) {
        lv_label_set_text(flow_state_lbl, mcp_valid ? (flow_raw ? "RAW HIGH" : "RAW LOW") : "--");
    }
    if (device_water_detail_lbl != nullptr) {
        lv_label_set_text(device_water_detail_lbl, mcp_valid ? (water_raw ? "HIGH" : "LOW") : "MCP --");
    }
    if (device_leak_detail_lbl != nullptr) {
        lv_label_set_text(device_leak_detail_lbl, mcp_valid ? (leak_raw ? "HIGH" : "LOW") : "MCP --");
    }
    if (device_flow_detail_lbl != nullptr) {
        lv_label_set_text(device_flow_detail_lbl, mcp_valid ? (flow_raw ? "HIGH" : "LOW") : "MCP --");
    }
    if (device_ec_detail_lbl != nullptr && cfg.enableEc) {
        if (ec_valid) {
            lv_label_set_text_fmt(device_ec_detail_lbl, "A1 %.3f V", ec_voltage);
        } else {
            lv_label_set_text(device_ec_detail_lbl, "ADS --");
        }
    }
    if (device_co2_detail_lbl != nullptr && cfg.enableCo2) {
        const bool co2_on = cfg.devMode ? runtime.co2On : mcp_bit(HwConfig::CH_CO2);
        lv_label_set_text(device_co2_detail_lbl,
                          (mcp_valid || cfg.devMode) ? (co2_on ? "Zawor ON" : "Zawor OFF") : "MCP --");
    }
}

void gui_app_update_ldr(int ldr_value, bool valid) {
    last_ldr_valid = valid;
    if (!valid) {
        apply_display_backlight(last_ldr_value, false);
        return;
    }

    last_ldr_value = clamp_ldr_value(ldr_value);
    apply_display_backlight(last_ldr_value, true);

    if (gui_web_focus_blocks_local_ui()) {
        return;
    }

    if (!cfg.ldrThemeEnabled) return;
    
    bool should_be_light = ui_light_theme;
    
    if (!ldr_value_to_light_theme(last_ldr_value, &should_be_light)) {
        return;
    }
    
    static int consecutive_diff_count = 0;
    static bool pending_state = false;
    
    if (should_be_light != ui_light_theme) {
        if (consecutive_diff_count == 0) {
            pending_state = should_be_light;
            consecutive_diff_count = 1;
        } else if (should_be_light == pending_state) {
            consecutive_diff_count++;
            if (consecutive_diff_count >= LDR_THEME_CONFIRM_READS) {
                ui_light_theme = should_be_light;
                rebuild_gui_tree_for_theme();
                consecutive_diff_count = 0;
            }
        } else {
            pending_state = should_be_light;
            consecutive_diff_count = 1;
        }
    } else {
        consecutive_diff_count = 0;
    }
}

bool gui_app_is_dev_mode(void) {
    GuiMutexGuard guard(20U);
    return guard.locked() && gui_ready && cfg.devMode;
}

bool gui_app_is_web_focus_active(void) {
    GuiMutexGuard guard(20U);
    return guard.locked() && gui_ready && web_ui_focus_active;
}

bool gui_app_runtime_ready(void) {
    GuiMutexGuard guard(50U);
    return guard.locked() &&
           gui_ready &&
           cfg.magic == UI_CONFIG_MAGIC &&
           lv_scr_act() != nullptr &&
           label_wifi_state != nullptr;
}

bool gui_app_ota_health_ready(void) {
    GuiMutexGuard guard(50U);
    if (!guard.locked() ||
        !gui_ready ||
        cfg.magic != UI_CONFIG_MAGIC) {
        return false;
    }
    if (cfg.devMode) {
        return true;
    }

    const uint32_t now_ms = millis();
    const bool frame_fresh =
        sensor_debug.updatedMs != 0U &&
        static_cast<uint32_t>(
            now_ms - sensor_debug.updatedMs) <=
            SENSOR_FRAME_STALE_MS;
    const bool temperature_ready =
        sensor_debug.temperaturePresent &&
        !sensor_debug.temperatureStale &&
        status_temperature_ok &&
        sensor_debug.temperatureAgeMs <=
            SENSOR_FRAME_STALE_MS;
    const bool ph_ready =
        !(cfg.showPhSensor || cfg.enableCo2) ||
        (sensor_debug.adcPresent &&
         sensor_debug.phValid);
    const bool ec_ready =
        !cfg.enableEc ||
        (sensor_debug.adcPresent &&
         sensor_debug.ecValid);
    const bool mcp_ready =
        sensor_debug.mcpPresent &&
        sensor_debug.mcpValid;
    constexpr unsigned int blocking_health_alarms =
        aquarium::AlarmSensorMissing |
        aquarium::AlarmSensorStale |
        aquarium::AlarmSensorBusFault |
        aquarium::AlarmActuatorWriteFailed;
    return frame_fresh &&
           temperature_ready &&
           ph_ready &&
           ec_ready &&
           mcp_ready &&
           !actuator_write_failed &&
           (current_alarm_flags &
            blocking_health_alarms) == 0U;
}

bool gui_app_ble_snapshot(GuiBleSnapshot *out) {
    GuiMutexGuard guard(200U);
    if (!guard.locked() || !gui_ready || out == nullptr || cfg.magic != UI_CONFIG_MAGIC) {
        return false;
    }

    const aquarium::DevSnapshot &dev = aquarium::dev_simulator().latest();
    const bool dev_mode = cfg.devMode;
    const float temperature = isfinite(runtime.lastTemp)
                                  ? runtime.lastTemp
                                  : (dev_mode ? dev.temperatureC : NAN);
    const float ph = isfinite(runtime.lastPh)
                         ? runtime.lastPh
                         : (dev_mode ? dev.ph : NAN);
    const bool ec_valid = sensor_debug.ecValid || dev_mode;
    const float ec = sensor_debug.ecValid
                         ? sensor_debug.ecValue
                         : (dev_mode ? dev.ecConductivity : NAN);
    const bool ldr_valid = last_ldr_valid || dev_mode;
    const int ldr = last_ldr_valid ? last_ldr_value : (dev_mode ? dev.ldr : 0);
    const uint16_t water_mask = static_cast<uint16_t>(
        1U << static_cast<uint8_t>(HwConfig::CH_WATER_LEVEL));
    const uint16_t leak_mask = static_cast<uint16_t>(
        1U << static_cast<uint8_t>(HwConfig::CH_LEAK));
    const bool mcp_valid = (sensor_debug.mcpPresent && sensor_debug.mcpValid) || dev_mode;
    const bool water_high = dev_mode
                                ? dev.waterLevelHigh
                                : (mcp_valid && (sensor_debug.mcpState & water_mask) != 0U);
    const bool leak_detected = dev_mode
                                   ? dev.leakDetected
                                   : (mcp_valid && (sensor_debug.mcpState & leak_mask) != 0U);

    out->protocol_version = 1U;
    out->developer_mode = dev_mode;
    out->uptime_seconds = millis() / 1000UL;
    out->free_heap_bytes = ESP.getFreeHeap();
    out->configuration_revision = cfg.crc32;
    out->temperature = isfinite(temperature) ? temperature : 0.0f;
    out->temperature_valid = isfinite(temperature);
    out->target_temperature = cfg.targetTemp;
    out->ph = isfinite(ph) ? ph : 0.0f;
    out->ph_valid = isfinite(ph);
    out->ec = isfinite(ec) ? ec : 0.0f;
    out->ec_valid = ec_valid && isfinite(ec);
    out->ldr = ldr;
    out->ldr_valid = ldr_valid;
    out->alarm_flags =
        static_cast<uint16_t>(current_alarm_flags);
    out->water_level_high = water_high;
    out->leak_detected = leak_detected;
    out->light_on = runtime.lightOn;
    out->plant_light_on = runtime.plantLightOn;
    out->filter_on = runtime.filterOn;
    out->heater_on = runtime.heaterOn;
    out->aeration_on = runtime.airOn;
    return true;
}

static bool gui_ble_pin_valid(const char *pin) {
    return device_credentials_admin_pin_matches(pin);
}

GuiBleCommandResult gui_app_ble_set_output(const char *target, bool state, const char *pin) {
    GuiMutexGuard guard(500U);
    if (!guard.locked() || !gui_ready) {
        return {false, "controller_busy", "Sterownik jest chwilowo zajety."};
    }
    if (!gui_ble_pin_valid(pin)) {
        return {false, "pin_invalid", "Nieprawidlowy PIN administratora."};
    }
    if (cfg.magic != UI_CONFIG_MAGIC) {
        return {false, "not_ready", "Sterownik nie zakonczyl inicjalizacji."};
    }
    if (target == nullptr) {
        return {false, "invalid_target", "Brak nazwy modulu."};
    }
    if (!cfg.devMode && !hal_mcp_is_present()) {
        return {false, "output_unavailable", "Wyjscia fizyczne sa niedostepne."};
    }

    if (strcmp(target, "light") == 0 || strcmp(target, "light1") == 0) {
        cfg.lightMode = state ? static_cast<uint8_t>(ScheduleMode::AlwaysOn)
                              : static_cast<uint8_t>(ScheduleMode::AlwaysOff);
        runtime.lightOn = state;
    } else if (strcmp(target, "plant") == 0 || strcmp(target, "light2") == 0) {
        cfg.plantLightMode = state ? static_cast<uint8_t>(ScheduleMode::AlwaysOn)
                                   : static_cast<uint8_t>(ScheduleMode::AlwaysOff);
        runtime.plantLightOn = state;
    } else if (strcmp(target, "filter") == 0) {
        cfg.filterMode = state ? static_cast<uint8_t>(ScheduleMode::AlwaysOn)
                               : static_cast<uint8_t>(ScheduleMode::AlwaysOff);
        runtime.filterOn = state;
    } else if (strcmp(target, "heater") == 0) {
        cfg.heaterMode = state ? static_cast<uint8_t>(HeaterMode::Threshold)
                               : static_cast<uint8_t>(HeaterMode::Off);
        cfg.enableHeater = state;
        if (!state) {
            runtime.heaterOn = false;
        }
    } else if (strcmp(target, "aeration") == 0) {
        cfg.airMode = state ? static_cast<uint8_t>(ScheduleMode::AlwaysOn)
                            : static_cast<uint8_t>(ScheduleMode::AlwaysOff);
        runtime.airOn = state;
    } else {
        return {false, "invalid_target", "Nieznany modul BLE."};
    }

    gui_app_save_settings();
    apply_mcp_outputs();
    return {true, cfg.devMode ? "dev_simulated" : "ok",
            cfg.devMode ? "Stan zasymulowany w trybie DEV." : "Stan modulu zapisany."};
}

GuiBleCommandResult gui_app_ble_feed(const char *pin) {
    GuiMutexGuard guard(500U);
    if (!guard.locked() || !gui_ready) {
        return {false, "controller_busy", "Sterownik jest chwilowo zajety."};
    }
    if (!gui_ble_pin_valid(pin)) {
        return {false, "pin_invalid", "Nieprawidlowy PIN administratora."};
    }
    if (cfg.magic != UI_CONFIG_MAGIC) {
        return {false, "not_ready", "Sterownik nie zakonczyl inicjalizacji."};
    }
    const bool started = run_feeder_pulse("Karmienie", "Dawka z BLE", false);
    if (!started) {
        return {false, "feed_busy", "Nie uruchomiono karmnika."};
    }
    return {true, cfg.devMode ? "dev_simulated" : "ok",
            cfg.devMode ? "Karmienie zasymulowane w trybie DEV." : "Karmienie uruchomione."};
}

namespace {

template <typename... Args>
bool ble_json_append(char *out, size_t out_size, size_t *used,
                     const char *format, Args... args) {
    if (out == nullptr || used == nullptr || format == nullptr || *used >= out_size) {
        return false;
    }
    const int written = snprintf(out + *used, out_size - *used, format, args...);
    if (written < 0 || static_cast<size_t>(written) >= out_size - *used) {
        out[out_size - 1U] = '\0';
        return false;
    }
    *used += static_cast<size_t>(written);
    return true;
}

const char *ble_json_find_value(const char *json, const char *key) {
    if (json == nullptr || key == nullptr) {
        return nullptr;
    }
    char pattern[48];
    const int length = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(pattern)) {
        return nullptr;
    }
    const char *cursor = json;
    while ((cursor = strstr(cursor, pattern)) != nullptr) {
        cursor += length;
        while (*cursor != '\0' && isspace(static_cast<unsigned char>(*cursor))) {
            ++cursor;
        }
        if (*cursor != ':') {
            continue;
        }
        ++cursor;
        while (*cursor != '\0' && isspace(static_cast<unsigned char>(*cursor))) {
            ++cursor;
        }
        return cursor;
    }
    return nullptr;
}

bool ble_json_value_has_valid_terminator(const char *cursor) {
    if (cursor == nullptr) {
        return false;
    }
    while (*cursor != '\0' &&
           isspace(static_cast<unsigned char>(*cursor))) {
        ++cursor;
    }
    return *cursor == '\0' || *cursor == ',' || *cursor == '}' ||
           *cursor == ']';
}

bool ble_json_read_bool(const char *json, const char *key, bool *out) {
    const char *value = ble_json_find_value(json, key);
    if (value == nullptr || out == nullptr) {
        return false;
    }
    if (strncmp(value, "true", 4U) == 0 &&
        ble_json_value_has_valid_terminator(value + 4U)) {
        *out = true;
        return true;
    }
    if (strncmp(value, "false", 5U) == 0 &&
        ble_json_value_has_valid_terminator(value + 5U)) {
        *out = false;
        return true;
    }
    if (*value == '1' && ble_json_value_has_valid_terminator(value + 1U)) {
        *out = true;
        return true;
    }
    if (*value == '0' && ble_json_value_has_valid_terminator(value + 1U)) {
        *out = false;
        return true;
    }
    return false;
}

bool ble_json_read_long(const char *json, const char *key, long minimum,
                        long maximum, long *out) {
    const char *value = ble_json_find_value(json, key);
    if (value == nullptr || out == nullptr) {
        return false;
    }
    const bool quoted = *value == '"';
    if (quoted) {
        ++value;
    }
    char *end = nullptr;
    const long parsed = strtol(value, &end, 10);
    if (end == value || parsed < minimum || parsed > maximum) {
        return false;
    }
    if (quoted) {
        if (*end != '"') {
            return false;
        }
        ++end;
    }
    if (!ble_json_value_has_valid_terminator(end)) {
        return false;
    }
    *out = parsed;
    return true;
}

bool ble_json_read_float(const char *json, const char *key, float minimum,
                         float maximum, float *out) {
    const char *value = ble_json_find_value(json, key);
    if (value == nullptr || out == nullptr) {
        return false;
    }
    const bool quoted = *value == '"';
    if (quoted) {
        ++value;
    }
    char *end = nullptr;
    const float parsed = strtof(value, &end);
    if (end == value || !isfinite(parsed) || parsed < minimum || parsed > maximum) {
        return false;
    }
    if (quoted) {
        if (*end != '"') {
            return false;
        }
        ++end;
    }
    if (!ble_json_value_has_valid_terminator(end)) {
        return false;
    }
    *out = parsed;
    return true;
}

bool ble_json_read_string(const char *json, const char *key, char *out, size_t out_size) {
    const char *value = ble_json_find_value(json, key);
    if (value == nullptr || out == nullptr || out_size < 2U || *value != '"') {
        return false;
    }
    ++value;
    size_t used = 0U;
    while (*value != '\0' && *value != '"') {
        unsigned char decoded = static_cast<unsigned char>(*value++);
        if (decoded == '\\') {
            const char escaped = *value++;
            switch (escaped) {
            case '"': decoded = '"'; break;
            case '\\': decoded = '\\'; break;
            case '/': decoded = '/'; break;
            case 'b': decoded = '\b'; break;
            case 'f': decoded = '\f'; break;
            case 'n': decoded = '\n'; break;
            case 'r': decoded = '\r'; break;
            case 't': decoded = '\t'; break;
            default: return false;
            }
        }
        if (decoded < 0x20U || used + 1U >= out_size) {
            return false;
        }
        out[used++] = static_cast<char>(decoded);
    }
    if (*value != '"' ||
        !ble_json_value_has_valid_terminator(value + 1U)) {
        return false;
    }
    out[used] = '\0';
    return true;
}

bool ble_json_read_time(const char *json, const char *key,
                        uint8_t *hour, uint8_t *minute) {
    char value[8];
    return ble_json_read_string(json, key, value, sizeof(value)) &&
           parse_time_text(value, hour, minute);
}

const char *ble_schedule_mode_name(uint8_t mode) {
    if (mode == static_cast<uint8_t>(ScheduleMode::AlwaysOn)) {
        return "always_on";
    }
    if (mode == static_cast<uint8_t>(ScheduleMode::AlwaysOff)) {
        return "always_off";
    }
    return "schedule";
}

const char *ble_light_profile_name(uint8_t profile) {
    switch (profile) {
    case 1U: return "daybreak";
    case 2U: return "night";
    default: return "day";
    }
}

bool ble_parse_light_profile(const char *json, const char *key, uint8_t *out) {
    char value[16];
    if (!ble_json_read_string(json, key, value, sizeof(value)) || out == nullptr) {
        return false;
    }
    if (strcmp(value, "day") == 0 || strcmp(value, "0") == 0) {
        *out = 0U;
        return true;
    }
    if (strcmp(value, "daybreak") == 0 || strcmp(value, "dawn") == 0 ||
        strcmp(value, "sunrise") == 0 || strcmp(value, "1") == 0) {
        *out = 1U;
        return true;
    }
    if (strcmp(value, "night") == 0 || strcmp(value, "moon") == 0 ||
        strcmp(value, "2") == 0) {
        *out = 2U;
        return true;
    }
    return false;
}

} // namespace

bool gui_app_ble_full_status_json(char *out, size_t out_size) {
    GuiMutexGuard guard(500U);
    if (!guard.locked() || !gui_ready ||
        out == nullptr || out_size < 1024U || cfg.magic != UI_CONFIG_MAGIC) {
        return false;
    }

    GuiBleSnapshot snapshot = {};
    if (!gui_app_ble_snapshot(&snapshot)) {
        return false;
    }
    const bool mcp_valid = (sensor_debug.mcpPresent && sensor_debug.mcpValid) || cfg.devMode;
    const uint16_t flow_mask = static_cast<uint16_t>(1U << static_cast<uint8_t>(HwConfig::CH_FLOW_PULSE));
    const aquarium::DevSnapshot &dev = aquarium::dev_simulator().latest();
    const bool flow_active = cfg.devMode
                                 ? dev.flowActive
                                 : (mcp_valid && (sensor_debug.mcpState & flow_mask) != 0U);
    const uint32_t water_runtime = runtime.waterFillOn && ato_started_ms != 0U
                                       ? (millis() - ato_started_ms) / 1000UL
                                       : 0U;
    const IPAddress ip = wifi_ota_active ? WiFi.softAPIP() : WiFi.localIP();
    char ssid[96];
    char configured_ssid[96];
    const RemoteAlarmRelayStatus remote_gateway =
        remote_alarm_relay_status();
    char remote_base_url[
        REMOTE_ALARM_RELAY_URL_BYTES * 2U] = {};
    char remote_device_id[
        REMOTE_ALARM_RELAY_DEVICE_ID_BYTES * 2U] = {};
    json_escape_to_buffer(wifi_ota_active ? WiFi.softAPSSID().c_str() : WiFi.SSID().c_str(),
                          ssid, sizeof(ssid));
    json_escape_to_buffer(selected_ssid, configured_ssid, sizeof(configured_ssid));
    json_escape_to_buffer(
        remote_gateway.base_url,
        remote_base_url,
        sizeof(remote_base_url));
    json_escape_to_buffer(
        remote_gateway.device_id,
        remote_device_id,
        sizeof(remote_device_id));
    const char *temperature = snapshot.temperature_valid ? nullptr : "null";
    const char *ph = snapshot.ph_valid ? nullptr : "null";
    const char *ec = snapshot.ec_valid ? nullptr : "null";
    const char *ldr = snapshot.ldr_valid ? nullptr : "null";
    char temperature_value[20];
    char ph_value[20];
    char ec_value[20];
    char ldr_value[20];
    if (temperature != nullptr) {
        snprintf(temperature_value, sizeof(temperature_value), "%s", temperature);
    } else {
        snprintf(temperature_value, sizeof(temperature_value), "%.2f",
                 static_cast<double>(snapshot.temperature));
    }
    if (ph != nullptr) {
        snprintf(ph_value, sizeof(ph_value), "%s", ph);
    } else {
        snprintf(ph_value, sizeof(ph_value), "%.3f", static_cast<double>(snapshot.ph));
    }
    if (ec != nullptr) {
        snprintf(ec_value, sizeof(ec_value), "%s", ec);
    } else {
        snprintf(ec_value, sizeof(ec_value), "%.1f", static_cast<double>(snapshot.ec));
    }
    if (ldr != nullptr) {
        snprintf(ldr_value, sizeof(ldr_value), "%s", ldr);
    } else {
        snprintf(ldr_value, sizeof(ldr_value), "%d", snapshot.ldr);
    }

    size_t used = 0U;
    bool ok = ble_json_append(out, out_size, &used,
        "{\"type\":\"full_status\",\"data\":{\"device\":\"cydAkwarium\",\"mode\":\"BLE_V2\","
        "\"ip\":\"%u.%u.%u.%u\",\"hostname\":\"cydAkwarium\",\"heap_free\":%lu,"
        "\"heap_largest\":%lu,\"sd_mounted\":%s,\"history_points\":%u,\"uptime_ms\":%lu,"
        "\"ota_active\":%s,",
        ip[0], ip[1], ip[2], ip[3],
        static_cast<unsigned long>(ESP.getFreeHeap()),
        static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
        ota_portal_sd_ready() ? "true" : "false",
        static_cast<unsigned>(history_count),
        static_cast<unsigned long>(millis()),
        wifi_ota_active ? "true" : "false");
    ok = ok && ble_json_append(out, out_size, &used,
        "\"sensors\":{\"temp_c\":%s,\"temp_valid\":%s,\"ph\":%s,\"ph_valid\":%s,"
        "\"ec\":%s,\"ec_valid\":%s,\"ldr\":%s,\"ldr_valid\":%s,"
        "\"mcp_present\":%s,\"mcp_valid\":%s,\"mcp_ok\":%s,"
        "\"water_level_high\":%s,\"water_level_valid\":%s,"
        "\"leak_detected\":%s,\"leak_valid\":%s,\"flow_active\":%s,\"flow_valid\":%s},",
        temperature_value, snapshot.temperature_valid ? "true" : "false",
        ph_value, snapshot.ph_valid ? "true" : "false",
        ec_value, snapshot.ec_valid ? "true" : "false",
        ldr_value, snapshot.ldr_valid ? "true" : "false",
        (sensor_debug.mcpPresent || cfg.devMode) ? "true" : "false",
        mcp_valid ? "true" : "false", mcp_valid ? "true" : "false",
        snapshot.water_level_high ? "true" : "false", mcp_valid ? "true" : "false",
        snapshot.leak_detected ? "true" : "false", mcp_valid ? "true" : "false",
        flow_active ? "true" : "false", mcp_valid ? "true" : "false");
    ok = ok && ble_json_append(out, out_size, &used,
        "\"alarms\":{\"flags\":%u,\"activeCount\":%u,\"temperatureHigh\":%s,"
        "\"temperatureLow\":%s,\"phOutOfRange\":%s,\"waterLevelLow\":%s,"
        "\"leak\":%s,\"supplyLow\":%s,\"sensorMissing\":%s,"
        "\"sensorStale\":%s,\"sensorBusFault\":%s,"
        "\"actuatorWriteFailed\":%s},",
        static_cast<unsigned>(snapshot.alarm_flags),
        static_cast<unsigned>(aquarium::alarm_count(snapshot.alarm_flags)),
        (snapshot.alarm_flags & aquarium::AlarmTemperatureHigh) ? "true" : "false",
        (snapshot.alarm_flags & aquarium::AlarmTemperatureLow) ? "true" : "false",
        (snapshot.alarm_flags & aquarium::AlarmPhOutOfRange) ? "true" : "false",
        (snapshot.alarm_flags & aquarium::AlarmWaterLevelLow) ? "true" : "false",
        (snapshot.alarm_flags & aquarium::AlarmLeak) ? "true" : "false",
        (snapshot.alarm_flags & aquarium::AlarmSupplyLow) ? "true" : "false",
        (snapshot.alarm_flags & aquarium::AlarmSensorMissing) ? "true" : "false",
        (snapshot.alarm_flags & aquarium::AlarmSensorStale) ? "true" : "false",
        (snapshot.alarm_flags & aquarium::AlarmSensorBusFault) ? "true" : "false",
        (snapshot.alarm_flags & aquarium::AlarmActuatorWriteFailed) ? "true" : "false");
    const aquarium::SensorCalibration calibration =
        sensor_calibration_store_snapshot();
    ok = ok && ble_json_append(
        out, out_size, &used,
        "\"calibration\":{\"version\":%u,"
        "\"ph\":{\"lowRaw\":%d,\"lowReference\":%.3f,"
        "\"highRaw\":%d,\"highReference\":%.3f},"
        "\"ec\":{\"referenceRaw\":%d,\"referenceUsCm\":%.2f,"
        "\"temperatureCoefficient\":%.5f,"
        "\"referenceTemperatureC\":%.2f}},",
        static_cast<unsigned>(calibration.version),
        static_cast<int>(calibration.ph_low_raw),
        static_cast<double>(calibration.ph_low_reference),
        static_cast<int>(calibration.ph_high_raw),
        static_cast<double>(calibration.ph_high_reference),
        static_cast<int>(calibration.ec_reference_raw),
        static_cast<double>(calibration.ec_reference_us_cm),
        static_cast<double>(
            calibration.ec_temperature_coefficient),
        static_cast<double>(
            calibration.ec_reference_temperature_c));
    ok = ok && ble_json_append(
        out, out_size, &used,
        "\"remoteGateway\":{\"enabled\":%s,"
        "\"provisioned\":%s,\"taskRunning\":%s,"
        "\"caCertificateLoaded\":%s,"
        "\"deliveredEvents\":%lu,\"failedAttempts\":%lu,"
        "\"lastSuccessEpoch\":%lu,\"nextRetryMs\":%lu,"
        "\"lastError\":\"%s\",\"baseUrl\":\"%s\","
        "\"deviceId\":\"%s\"},",
        remote_gateway.enabled ? "true" : "false",
        remote_gateway.provisioned ? "true" : "false",
        remote_gateway.task_running ? "true" : "false",
        remote_gateway.ca_certificate_loaded
            ? "true"
            : "false",
        static_cast<unsigned long>(
            remote_gateway.delivered_events),
        static_cast<unsigned long>(
            remote_gateway.failed_attempts),
        static_cast<unsigned long>(
            remote_gateway.last_success_epoch),
        static_cast<unsigned long>(
            remote_gateway.next_retry_ms),
        remote_alarm_relay_error_code(
            remote_gateway.last_error),
        remote_base_url,
        remote_device_id);
    ok = ok && ble_json_append(out, out_size, &used,
        "\"config\":{\"target_temp\":%.2f,\"temp_hysteresis\":%.2f,\"co2TargetPh\":%.2f,"
        "\"co2MaxTimeMin\":%u,\"dev_mode\":%s,\"modem_sleep\":%s,"
        "\"always_screen_on\":%s,\"sound_enabled\":%s},"
        "\"display\":{\"autoBrightness\":%s,\"profile\":\"%s\",\"brightness\":%u,"
        "\"appliedBrightness\":%u},\"water\":{\"timeoutSec\":%u,\"active\":%s,"
        "\"timeoutLatched\":%s,\"runtimeSec\":%lu},\"leak\":{\"action\":\"%s\"},",
        static_cast<double>(cfg.targetTemp), static_cast<double>(cfg.tempHysteresis),
        static_cast<double>(co2_target_ph), static_cast<unsigned>(co2_max_time_minutes),
        cfg.devMode ? "true" : "false", cfg.modemSleep ? "true" : "false",
        cfg.alwaysScreenOn ? "true" : "false", cfg.soundEnabled ? "true" : "false",
        display_auto_brightness ? "true" : "false", display_profile_code(display_power_profile),
        static_cast<unsigned>(display_max_brightness),
        static_cast<unsigned>(hal_display_get_brightness()),
        static_cast<unsigned>(water_timeout_seconds), runtime.waterFillOn ? "true" : "false",
        ato_timeout_latched ? "true" : "false", static_cast<unsigned long>(water_runtime),
        leak_action_code(leak_action));
    ok = ok && ble_json_append(out, out_size, &used,
        "\"modules\":{\"light_on\":%s,\"plant_light_on\":%s,\"light1_on\":%s,\"light2_on\":%s,\"filter_on\":%s,"
        "\"air_on\":%s,\"co2_on\":%s,\"heater_on\":%s,\"heater_enabled\":%s,"
        "\"ph_sensor_enabled\":%s,\"co2_enabled\":%s,\"ec_enabled\":%s,"
        "\"water_level_enabled\":%s,\"water_dosing_on\":%s,\"leak_enabled\":%s,"
        "\"flow_enabled\":%s,\"feeder_enabled\":%s},",
        runtime.lightOn ? "true" : "false", runtime.plantLightOn ? "true" : "false",
        runtime.lightOn ? "true" : "false", runtime.plantLightOn ? "true" : "false",
        runtime.filterOn ? "true" : "false", runtime.airOn ? "true" : "false",
        runtime.co2On ? "true" : "false", runtime.heaterOn ? "true" : "false",
        cfg.enableHeater ? "true" : "false", (cfg.showPhSensor || cfg.devMode) ? "true" : "false",
        cfg.enableCo2 ? "true" : "false", (cfg.enableEc || cfg.devMode) ? "true" : "false",
        (cfg.enableWaterLevel || cfg.devMode) ? "true" : "false",
        runtime.waterFillOn ? "true" : "false", (cfg.enableLeak || cfg.devMode) ? "true" : "false",
        (cfg.enableFlow || cfg.devMode) ? "true" : "false", cfg.feedEnabled ? "true" : "false");
    ok = ok && ble_json_append(out, out_size, &used,
        "\"schedules\":{" 
        "\"light\":{\"mode\":\"%s\",\"start\":\"%02u:%02u\",\"end\":\"%02u:%02u\",\"profile\":\"%s\",\"profileCycle\":%s},"
        "\"plant_light\":{\"mode\":\"%s\",\"start\":\"%02u:%02u\",\"end\":\"%02u:%02u\",\"profile\":\"%s\",\"profileCycle\":%s},"
        "\"filter\":{\"mode\":\"%s\",\"start\":\"%02u:%02u\",\"end\":\"%02u:%02u\"},"
        "\"air\":{\"mode\":\"%s\",\"start\":\"%02u:%02u\",\"end\":\"%02u:%02u\"},"
        "\"feeder\":{\"enabled\":%s,\"count\":%u,\"time1\":\"%02u:%02u\",\"time2\":\"%02u:%02u\"}},",
        ble_schedule_mode_name(cfg.lightMode), cfg.lightStartHour, cfg.lightStartMinute,
        cfg.lightEndHour, cfg.lightEndMinute, ble_light_profile_name(runtime.lightActiveMode),
        cfg.lightMode == static_cast<uint8_t>(ScheduleMode::Schedule) && config_uses_factory_light_window() ? "true" : "false",
        ble_schedule_mode_name(cfg.plantLightMode), cfg.plantStartHour, cfg.plantStartMinute,
        cfg.plantEndHour, cfg.plantEndMinute, ble_light_profile_name(runtime.plantLightActiveMode),
        cfg.plantLightMode == static_cast<uint8_t>(ScheduleMode::Schedule) && config_uses_factory_light2_window() ? "true" : "false",
        ble_schedule_mode_name(cfg.filterMode), cfg.filterStartHour, cfg.filterStartMinute,
        cfg.filterEndHour, cfg.filterEndMinute, ble_schedule_mode_name(cfg.airMode),
        cfg.airStartHour, cfg.airStartMinute, cfg.airEndHour, cfg.airEndMinute,
        cfg.feedEnabled ? "true" : "false", static_cast<unsigned>(cfg.feedCount),
        cfg.feedHour1, cfg.feedMinute1, cfg.feedHour2, cfg.feedMinute2);
    ok = ok && ble_json_append(out, out_size, &used,
        "\"clock\":{\"year\":%d,\"month\":%d,\"day\":%d,\"hour\":%d,\"minute\":%d,"
        "\"second\":%d,\"valid\":%s,\"source\":\"%s\"},"
        "\"temperature\":{\"current\":%s,\"target\":%.2f,\"hysteresis\":%.2f,"
        "\"historyCapacity\":%u,\"historyIntervalMinutes\":1,\"history\":[",
        clock_year, clock_month, clock_day, clock_hour, clock_minute, clock_second,
        controller_clock_reliable ? "true" : "false", controller_clock_source,
        temperature_value, static_cast<double>(cfg.targetTemp), static_cast<double>(cfg.tempHysteresis),
        static_cast<unsigned>(TEMP_HISTORY_POINTS));
    constexpr uint8_t BLE_HISTORY_LIMIT = 48U;
    const uint8_t history_start = history_count > BLE_HISTORY_LIMIT
                                      ? static_cast<uint8_t>(history_count - BLE_HISTORY_LIMIT)
                                      : 0U;
    for (uint8_t index = history_start; ok && index < history_count; ++index) {
        char history_temperature[20];
        if (isfinite(temp_history[index])) {
            snprintf(history_temperature, sizeof(history_temperature), "%.2f",
                     static_cast<double>(temp_history[index]));
        } else {
            snprintf(history_temperature, sizeof(history_temperature), "null");
        }
        const uint32_t epoch = history_epoch[index] > 0U
                                   ? history_epoch[index]
                                   : controller_unix_time();
        ok = ble_json_append(out, out_size, &used,
                             "%s{\"value\":%s,\"epoch\":%lu}",
                             index == history_start ? "" : ",",
                             history_temperature,
                             static_cast<unsigned long>(epoch));
    }
    ok = ok && ble_json_append(out, out_size, &used,
        "],\"heaterMode\":%u},\"battery\":{\"voltage\":null,\"percent\":null},"
        "\"firmware\":{\"version\":\"%s\",\"apiVersion\":%u,"
        "\"buildDate\":\"%s\",\"buildTime\":\"%s\"},",
        static_cast<unsigned>(cfg.heaterMode),
        FirmwareInfo::VERSION,
        static_cast<unsigned>(FirmwareInfo::API_VERSION),
        __DATE__,
        __TIME__);
    const RuntimeSafetyStatus safety =
        runtime_safety_status();
    ok = ok && ble_json_append(out, out_size, &used,
        "\"network\":{\"staConnected\":%s,\"staConnecting\":%s,\"apMode\":%s,"
        "\"serviceMode\":%s,\"staSsid\":\"%s\",\"configuredStaSsid\":\"%s\","
        "\"configuredApSsid\":\"cydAkwarium_AP\",\"ssid\":\"%s\","
        "\"ip\":\"%u.%u.%u.%u\",\"rssi\":%d,\"clients\":%u},"
        "\"system\":{\"uptime\":%lu,\"powerMode\":\"%s\",\"resetReason\":\"%d\","
        "\"freeHeap\":%lu,\"largestHeap\":%lu,\"minimumFreeHeap\":%lu,"
        "\"bootId\":%lu,\"bootCount\":%lu,\"faultCount\":%lu,"
        "\"lastFaultReason\":\"%s\",\"lastFaultUptimeMs\":%lu,"
        "\"lastFailSafeConfirmed\":%s,\"actuatorWriteErrors\":%lu},",
        wifi_connected ? "true" : "false", is_connecting ? "true" : "false",
        wifi_ota_active ? "true" : "false", ota_portal_sta_running ? "true" : "false",
        ssid, configured_ssid, ssid, ip[0], ip[1], ip[2], ip[3], wifi_rssi,
        static_cast<unsigned>(wifi_ota_active ? WiFi.softAPgetStationNum() : 0U),
        static_cast<unsigned long>(millis() / 1000UL), cfg.modemSleep ? "eco" : "normal",
        static_cast<int>(esp_reset_reason()), static_cast<unsigned long>(ESP.getFreeHeap()),
        static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
        static_cast<unsigned long>(safety.current_minimum_free_heap),
        static_cast<unsigned long>(safety.boot_id),
        static_cast<unsigned long>(safety.boot_count),
        static_cast<unsigned long>(safety.fault_count),
        runtime_fault_reason_code(safety.last_reset.fault_reason),
        static_cast<unsigned long>(safety.last_reset.fault_uptime_ms),
        safety.last_reset.fail_safe_confirmed ? "true" : "false",
        static_cast<unsigned long>(actuator_write_error_count));
    const uint32_t light_status_ms = millis();
    const aquarium::AquaelLightSnapshot front_light =
        mcp_outputs.frontLight.snapshot(light_status_ms);
    const aquarium::AquaelLightSnapshot rear_light =
        mcp_outputs.rearLight.snapshot(light_status_ms);
    const aquarium::AquaelProfile front_profile =
        front_light.known
            ? front_light.profile
            : aquael_domain_profile(runtime.lightActiveMode);
    const aquarium::AquaelProfile rear_profile =
        rear_light.known
            ? rear_light.profile
            : aquael_domain_profile(runtime.plantLightActiveMode);
    ok = ok && ble_json_append(
        out, out_size, &used,
        "\"lights\":{\"front\":{\"label\":\"Przednia\",\"relay\":\"light1\","
        "\"on\":%s,\"profile\":\"%s\","
        "\"profileName\":\"%s\",\"transitioning\":%s,\"known\":%s},"
        "\"rear\":{\"label\":\"Tylna\",\"relay\":\"light2\","
        "\"on\":%s,\"profile\":\"%s\",\"profileName\":\"%s\","
        "\"transitioning\":%s,\"known\":%s},"
        "\"light1\":{\"on\":%s,\"profile\":\"%s\",\"transitioning\":%s,"
        "\"known\":%s},\"light2\":{\"on\":%s,\"profile\":\"%s\","
        "\"transitioning\":%s,\"known\":%s},"
        "\"supportedProfiles\":[\"day\",\"daybreak\",\"night\"]},",
        (cfg.devMode ? runtime.lightOn : front_light.relay_on) ? "true" : "false",
        aquarium::AquaelLightController::profile_code(front_profile),
        aquarium::AquaelLightController::profile_name(front_profile),
        front_light.transitioning ? "true" : "false",
        (cfg.devMode || front_light.known) ? "true" : "false",
        (cfg.devMode ? runtime.plantLightOn : rear_light.relay_on) ? "true" : "false",
        aquarium::AquaelLightController::profile_code(rear_profile),
        aquarium::AquaelLightController::profile_name(rear_profile),
        rear_light.transitioning ? "true" : "false",
        (cfg.devMode || rear_light.known) ? "true" : "false",
        (cfg.devMode ? runtime.lightOn : front_light.relay_on) ? "true" : "false",
        aquarium::AquaelLightController::profile_code(front_profile),
        front_light.transitioning ? "true" : "false",
        (cfg.devMode || front_light.known) ? "true" : "false",
        (cfg.devMode ? runtime.plantLightOn : rear_light.relay_on) ? "true" : "false",
        aquarium::AquaelLightController::profile_code(rear_profile),
        rear_light.transitioning ? "true" : "false",
        (cfg.devMode || rear_light.known) ? "true" : "false");
    ok = ok && ble_json_append(out, out_size, &used,
        "\"relays\":{\"light\":%s,\"plantLight\":%s,\"light1\":%s,\"light2\":%s,\"pump\":%s,\"heater\":%s,"
        "\"co2\":%s,\"aeration\":%s,\"waterDosing\":%s,\"aerationPercent\":%u},"
        "\"schedule\":{\"lightMode\":%u,\"dayStartHour\":%u,\"dayStartMin\":%u,"
        "\"dayEndHour\":%u,\"dayEndMin\":%u,\"plantLightMode\":%u,"
        "\"plantStartHour\":%u,\"plantStartMin\":%u,\"plantEndHour\":%u,"
        "\"plantEndMin\":%u,\"filterMode\":%u,\"filterStartHour\":%u,"
        "\"filterStartMin\":%u,\"filterEndHour\":%u,\"filterEndMin\":%u,"
        "\"airMode\":%u,\"airStartHour\":%u,\"airStartMin\":%u,"
        "\"airEndHour\":%u,\"airEndMin\":%u,\"heaterMode\":%u},"
        "\"feeding\":{\"active\":%s,\"freq\":%u,\"hour\":%u,\"minute\":%u,"
        "\"lastFeedEpoch\":%lu,\"lastResult\":\"%s\"},"
        "\"eco\":{\"safe_active\":false,\"quiet_window\":false,\"deep_ready\":false,"
        "\"rtc_ready\":%s,\"wake_after_sec\":0,\"last_wake_cause\":0,\"blockers\":[]},",
        runtime.lightOn ? "true" : "false", runtime.plantLightOn ? "true" : "false",
        runtime.lightOn ? "true" : "false", runtime.plantLightOn ? "true" : "false",
        runtime.filterOn ? "true" : "false", runtime.heaterOn ? "true" : "false",
        runtime.co2On ? "true" : "false", runtime.airOn ? "true" : "false",
        runtime.waterFillOn ? "true" : "false", runtime.airOn ? 100U : 0U,
        cfg.lightMode, cfg.lightStartHour, cfg.lightStartMinute, cfg.lightEndHour, cfg.lightEndMinute,
        cfg.plantLightMode, cfg.plantStartHour, cfg.plantStartMinute, cfg.plantEndHour, cfg.plantEndMinute,
        cfg.filterMode, cfg.filterStartHour, cfg.filterStartMinute, cfg.filterEndHour, cfg.filterEndMinute,
        cfg.airMode, cfg.airStartHour, cfg.airStartMinute, cfg.airEndHour, cfg.airEndMinute,
        cfg.heaterMode, feeder_pulse_active ? "true" : "false",
        cfg.feedEnabled ? static_cast<unsigned>(cfg.feedCount) : 0U,
        cfg.feedHour1, cfg.feedMinute1, static_cast<unsigned long>(last_feed_epoch),
        last_feed_result, controller_clock_reliable ? "true" : "false");
    const uint32_t control_now_ms = millis();
    const aquarium::OperatingModeSnapshot modes =
        control_modes.mode_snapshot(control_now_ms);
    ok = ok && ble_json_append(
        out, out_size, &used,
        "\"controlState\":{\"feedingMode\":{\"active\":%s,\"remainingSec\":%lu},"
        "\"serviceMode\":{\"active\":%s,\"remainingSec\":%lu},\"overrides\":[",
        modes.feeding_active ? "true" : "false",
        static_cast<unsigned long>(modes.feeding_remaining_seconds),
        modes.service_active ? "true" : "false",
        static_cast<unsigned long>(modes.service_remaining_seconds));
    bool first_override = true;
    for (uint8_t index = 0U;
         ok && index < static_cast<uint8_t>(aquarium::OutputTarget::Count);
         ++index) {
        const aquarium::OutputTarget target =
            static_cast<aquarium::OutputTarget>(index);
        const aquarium::TimedOverrideSnapshot override_state =
            control_modes.override_snapshot(target, control_now_ms);
        if (!override_state.active) {
            continue;
        }
        ok = ble_json_append(
            out, out_size, &used,
            "%s{\"target\":\"%s\",\"state\":%s,\"remainingSec\":%lu}",
            first_override ? "" : ",",
            aquarium::ControlModeManager::target_name(target),
            override_state.state ? "true" : "false",
            static_cast<unsigned long>(override_state.remaining_seconds));
        first_override = false;
    }
    ok = ok && ble_json_append(out, out_size, &used, "]}}}");
    if (!ok) {
        out[0] = '\0';
    }
    return ok;
}

bool gui_app_ble_logs_json(char *out, size_t out_size, const char *pin) {
    GuiMutexGuard guard(500U);
    if (!guard.locked() || !gui_ready ||
        !gui_ble_pin_valid(pin) || out == nullptr || out_size < 256U) {
        return false;
    }
    size_t used = 0U;
    bool ok = ble_json_append(out, out_size, &used, "{\"type\":\"logs\",\"data\":{\"normal\":[");
    for (uint8_t i = 0U; ok && i < gui_logs_normal_count; ++i) {
        char escaped[GUI_LOG_MESSAGE_LEN * 2U + 1U];
        json_escape_to_buffer(gui_logs_normal[i].message, escaped, sizeof(escaped));
        ok = ble_json_append(out, out_size, &used,
                             "%s{\"ts\":%lu,\"level\":\"info\",\"code\":\"info\",\"message\":\"%s\"}",
                             i == 0U ? "" : ",", static_cast<unsigned long>(gui_logs_normal[i].ts), escaped);
    }
    ok = ok && ble_json_append(out, out_size, &used, "],\"critical\":[");
    for (uint8_t i = 0U; ok && i < gui_logs_important_count; ++i) {
        char escaped[GUI_LOG_MESSAGE_LEN * 2U + 1U];
        json_escape_to_buffer(gui_logs_important[i].message, escaped, sizeof(escaped));
        ok = ble_json_append(out, out_size, &used,
                             "%s{\"ts\":%lu,\"level\":\"error\",\"code\":\"wazne\",\"message\":\"%s\"}",
                             i == 0U ? "" : ",", static_cast<unsigned long>(gui_logs_important[i].ts), escaped);
    }
    ok = ok && ble_json_append(out, out_size, &used,
                               "],\"counts\":{\"normal\":%u,\"critical\":%u}}}",
                               static_cast<unsigned>(gui_logs_normal_count),
                               static_cast<unsigned>(gui_logs_important_count));
    return ok;
}

bool gui_app_ble_diagnostics_json(char *out, size_t out_size, const char *pin) {
    GuiMutexGuard guard(500U);
    if (!guard.locked() || !gui_ready ||
        !gui_ble_pin_valid(pin) || out == nullptr || out_size < 512U) {
        return false;
    }
    const RuntimeSafetyStatus safety =
        runtime_safety_status();
    const RemoteAlarmRelayStatus remote_gateway =
        remote_alarm_relay_status();
    const int written = snprintf(
        out, out_size,
        "{\"type\":\"diagnostics\",\"data\":{\"ok\":true,\"simulated\":%s,"
        "\"sda\":%u,\"scl\":%u,\"frequencyHz\":%lu,\"scanMs\":0,"
        "\"count\":%u,\"truncated\":false,\"devices\":[],"
        "\"runtime\":{\"bootId\":%lu,\"bootCount\":%lu,"
        "\"faultCount\":%lu,\"minimumFreeHeap\":%lu,"
        "\"lastFaultReason\":\"%s\",\"lastFaultUptimeMs\":%lu,"
        "\"lastFailSafeConfirmed\":%s,"
        "\"actuatorWriteErrors\":%lu},"
        "\"remoteGateway\":{\"enabled\":%s,"
        "\"provisioned\":%s,\"taskRunning\":%s,"
        "\"deliveredEvents\":%lu,\"failedAttempts\":%lu,"
        "\"lastError\":\"%s\"},"
        "\"uart\":{\"ports\":[],\"discoverySupported\":false},"
        "\"oneWire\":{\"dataPin\":%u,\"scanMs\":0,\"count\":0,"
        "\"truncated\":false,\"devices\":[]}}}",
        cfg.devMode ? "true" : "false",
        static_cast<unsigned>(HwConfig::I2C_SDA_PIN),
        static_cast<unsigned>(HwConfig::I2C_SCL_PIN),
        static_cast<unsigned long>(HwConfig::I2C_FREQUENCY_HZ),
        static_cast<unsigned>((sensor_debug.mcpPresent ? 1U : 0U) +
                              (sensor_debug.adcPresent ? 1U : 0U)),
        static_cast<unsigned long>(safety.boot_id),
        static_cast<unsigned long>(safety.boot_count),
        static_cast<unsigned long>(safety.fault_count),
        static_cast<unsigned long>(
            safety.current_minimum_free_heap),
        runtime_fault_reason_code(
            safety.last_reset.fault_reason),
        static_cast<unsigned long>(
            safety.last_reset.fault_uptime_ms),
        safety.last_reset.fail_safe_confirmed
            ? "true"
            : "false",
        static_cast<unsigned long>(
            actuator_write_error_count),
        remote_gateway.enabled ? "true" : "false",
        remote_gateway.provisioned ? "true" : "false",
        remote_gateway.task_running ? "true" : "false",
        static_cast<unsigned long>(
            remote_gateway.delivered_events),
        static_cast<unsigned long>(
            remote_gateway.failed_attempts),
        remote_alarm_relay_error_code(
            remote_gateway.last_error),
        static_cast<unsigned>(HwConfig::OneWireBus::DATA_PIN));
    return written > 0 && static_cast<size_t>(written) < out_size;
}

static bool is_remote_gateway_action(
    const char *action) {
    return action != nullptr &&
           (strcmp(
                action,
                "save_remote_gateway") == 0 ||
            strcmp(
                action,
                "set_remote_gateway_enabled") == 0 ||
            strcmp(
                action,
                "clear_remote_gateway") == 0);
}

static GuiBleCommandResult execute_remote_gateway_action(
    const char *action,
    const char *command_json) {
    if (strcmp(action, "save_remote_gateway") == 0) {
        char base_url[
            REMOTE_ALARM_RELAY_URL_BYTES] = {};
        char device_id[
            REMOTE_ALARM_RELAY_DEVICE_ID_BYTES] = {};
        char hmac_secret[136] = {};
        bool enabled = true;
        const bool parsed =
            ble_json_read_string(
                command_json,
                "baseUrl",
                base_url,
                sizeof(base_url)) &&
            ble_json_read_string(
                command_json,
                "deviceId",
                device_id,
                sizeof(device_id)) &&
            ble_json_read_string(
                command_json,
                "hmacSecret",
                hmac_secret,
                sizeof(hmac_secret));
        ble_json_read_bool(
            command_json, "enabled", &enabled);
        const bool saved =
            parsed &&
            remote_alarm_relay_configure(
                base_url,
                device_id,
                hmac_secret,
                enabled);
        secure_clear_gui_buffer(
            hmac_secret, sizeof(hmac_secret));
        return saved
                   ? GuiBleCommandResult{
                         true,
                         "remote_gateway_saved",
                         "Bezpieczna bramka alarmowa zostala zapisana."}
                   : GuiBleCommandResult{
                         false,
                         "invalid_remote_gateway",
                         "Sprawdz adres HTTPS, identyfikator i sekret Base64."};
    }
    if (strcmp(
            action,
            "set_remote_gateway_enabled") == 0) {
        bool enabled = false;
        if (!ble_json_read_bool(
                command_json,
                "enabled",
                &enabled)) {
            return {
                false,
                "invalid_remote_gateway_state",
                "Brak poprawnego pola enabled."};
        }
        const bool saved =
            remote_alarm_relay_set_enabled(enabled);
        return saved
                   ? GuiBleCommandResult{
                         true,
                         enabled
                             ? "remote_gateway_enabled"
                             : "remote_gateway_disabled",
                         enabled
                             ? "Wysylanie alarmow zostalo wlaczone."
                             : "Wysylanie alarmow zostalo wylaczone."}
                   : GuiBleCommandResult{
                         false,
                         "remote_gateway_not_provisioned",
                         "Najpierw zapisz konfiguracje bramki."};
    }
    if (strcmp(action, "clear_remote_gateway") == 0) {
        const bool cleared =
            remote_alarm_relay_clear();
        return cleared
                   ? GuiBleCommandResult{
                         true,
                         "remote_gateway_cleared",
                         "Konfiguracja bramki zostala usunieta."}
                   : GuiBleCommandResult{
                         false,
                         "remote_gateway_clear_failed",
                         "Nie udalo sie wyczyscic konfiguracji bramki."};
    }
    return {
        false,
        "invalid_remote_gateway_action",
        "Nieznana akcja bramki alarmowej."};
}

GuiBleCommandResult gui_app_ble_action(const char *action,
                                       const char *command_json,
                                       const char *pin) {
    GuiMutexGuard guard(1000U);
    if (!guard.locked() || !gui_ready) {
        return {false, "controller_busy", "Sterownik jest chwilowo zajety."};
    }
    if (!gui_ble_pin_valid(pin)) {
        return {false, "pin_invalid", "Nieprawidlowy PIN administratora."};
    }
    if (action == nullptr || command_json == nullptr || cfg.magic != UI_CONFIG_MAGIC) {
        return {false, "invalid_action", "Nieprawidlowa komenda BLE."};
    }
    if (strcmp(action, "auth_check") == 0) {
        return {true, "admin_authenticated", "PIN administratora jest poprawny."};
    }
    if (strcmp(action, "feed_now") == 0) {
        return gui_app_ble_feed(pin);
    }
    if (is_remote_gateway_action(action)) {
        return execute_remote_gateway_action(
            action, command_json);
    }

    if (strcmp(action, "save_schedule") == 0) {
        long value = 0L;
        if (ble_json_read_long(command_json, "light1Mode", 0L, 2L, &value) ||
            ble_json_read_long(command_json, "lightMode", 0L, 2L, &value)) {
            cfg.lightMode = static_cast<uint8_t>(value);
        }
        if (ble_json_read_long(command_json, "light2Mode", 0L, 2L, &value) ||
            ble_json_read_long(command_json, "plantLightMode", 0L, 2L, &value)) {
            cfg.plantLightMode = static_cast<uint8_t>(value);
        }
        if (ble_json_read_long(command_json, "filterMode", 0L, 2L, &value)) cfg.filterMode = static_cast<uint8_t>(value);
        if (ble_json_read_long(command_json, "aerationMode", 0L, 2L, &value)) cfg.airMode = static_cast<uint8_t>(value);
        if (ble_json_read_long(command_json, "heaterMode", 0L, 1L, &value)) {
            cfg.heaterMode = static_cast<uint8_t>(value);
            cfg.enableHeater = value == 0L;
        }
        if (!ble_json_read_time(command_json, "light1Start", &cfg.lightStartHour, &cfg.lightStartMinute)) {
            ble_json_read_time(command_json, "dayStart", &cfg.lightStartHour, &cfg.lightStartMinute);
        }
        if (!ble_json_read_time(command_json, "light1End", &cfg.lightEndHour, &cfg.lightEndMinute)) {
            ble_json_read_time(command_json, "dayEnd", &cfg.lightEndHour, &cfg.lightEndMinute);
        }
        if (!ble_json_read_time(command_json, "light2Start", &cfg.plantStartHour, &cfg.plantStartMinute)) {
            ble_json_read_time(command_json, "plantLightStart", &cfg.plantStartHour, &cfg.plantStartMinute);
        }
        if (!ble_json_read_time(command_json, "light2End", &cfg.plantEndHour, &cfg.plantEndMinute)) {
            ble_json_read_time(command_json, "plantLightEnd", &cfg.plantEndHour, &cfg.plantEndMinute);
        }
        ble_json_read_time(command_json, "filterOn", &cfg.filterStartHour, &cfg.filterStartMinute);
        ble_json_read_time(command_json, "filterOff", &cfg.filterEndHour, &cfg.filterEndMinute);
        ble_json_read_time(command_json, "airOn", &cfg.airStartHour, &cfg.airStartMinute);
        ble_json_read_time(command_json, "airOff", &cfg.airEndHour, &cfg.airEndMinute);
        uint8_t profile = 0U;
        if (ble_parse_light_profile(command_json, "light1Profile", &profile) ||
            ble_parse_light_profile(command_json, "lightProfile", &profile)) {
            cfg.lightColorMode = profile;
            cfg.lightSchedColorMode = aquael_profile_to_schedule(profile);
        }
        bool profile_cycle = false;
        if ((ble_json_read_bool(command_json, "light1ProfileCycle", &profile_cycle) ||
             ble_json_read_bool(command_json, "lightProfileCycle", &profile_cycle)) && profile_cycle) {
            cfg.lightSchedColorMode = 0U;
        }
        if (ble_parse_light_profile(command_json, "light2Profile", &profile) ||
            ble_parse_light_profile(command_json, "plantLightProfile", &profile)) {
            cfg.plantLightColorMode = profile;
            cfg.plantSchedColorMode = aquael_profile_to_schedule(profile);
        }
        profile_cycle = false;
        if ((ble_json_read_bool(command_json, "light2ProfileCycle", &profile_cycle) ||
             ble_json_read_bool(command_json, "plantLightProfileCycle", &profile_cycle)) && profile_cycle) {
            cfg.plantSchedColorMode = 0U;
        }
        if (ble_json_read_long(command_json, "feedFreq", 0L, 2L, &value)) {
            cfg.feedEnabled = value > 0L;
            cfg.feedCount = cfg.feedEnabled ? 1U : 0U;
        }
        ble_json_read_time(command_json, "feedTime", &cfg.feedHour1, &cfg.feedMinute1);
        sanitize_config(cfg);
        gui_app_save_settings();
        return {true, "ok", "Harmonogramy zapisane."};
    }

    if (strcmp(action, "save_temperature") == 0) {
        long mode = cfg.heaterMode;
        float target = cfg.targetTemp;
        float hysteresis = cfg.tempHysteresis;
        ble_json_read_long(command_json, "heaterMode", 0L, 1L, &mode);
        if (!ble_json_read_float(command_json, "target", 18.0f, 30.0f, &target) ||
            !ble_json_read_float(command_json, "hysteresis", 0.1f, 5.0f, &hysteresis)) {
            return {false, "invalid_temperature", "Temperatura lub histereza jest poza zakresem."};
        }
        cfg.heaterMode = static_cast<uint8_t>(mode);
        cfg.enableHeater = mode == 0L;
        cfg.targetTemp = target;
        cfg.tempHysteresis = hysteresis;
        gui_app_save_settings();
        apply_mcp_outputs();
        return {true, "ok", "Ustawienia temperatury zapisane."};
    }

    if (strcmp(action, "save_calibration") == 0) {
        char type[8] = {};
        if (!ble_json_read_string(
                command_json, "type", type, sizeof(type))) {
            return {
                false,
                "invalid_calibration",
                "Brak typu kalibracji ph/ec."
            };
        }
        if (strcmp(type, "ph") == 0) {
            long low_raw = 0L;
            long high_raw = 0L;
            float low_reference = 4.01f;
            float high_reference = 6.86f;
            if (!ble_json_read_long(
                    command_json,
                    "lowRaw",
                    INT16_MIN,
                    INT16_MAX,
                    &low_raw) ||
                !ble_json_read_long(
                    command_json,
                    "highRaw",
                    INT16_MIN,
                    INT16_MAX,
                    &high_raw) ||
                !ble_json_read_float(
                    command_json,
                    "lowReference",
                    0.0f,
                    14.0f,
                    &low_reference) ||
                !ble_json_read_float(
                    command_json,
                    "highReference",
                    0.0f,
                    14.0f,
                    &high_reference) ||
                !sensor_calibration_store_save_ph(
                    static_cast<int16_t>(low_raw),
                    low_reference,
                    static_cast<int16_t>(high_raw),
                    high_reference)) {
                return {
                    false,
                    "invalid_calibration",
                    "Punkty pH sa niepoprawne lub zbyt blisko."
                };
            }
            return {
                true,
                "calibration_saved",
                "Kalibracja pH zapisana."
            };
        }
        if (strcmp(type, "ec") == 0) {
            long reference_raw = 0L;
            float reference = 1413.0f;
            float coefficient = 0.019f;
            float reference_temperature = 25.0f;
            if (!ble_json_read_long(
                    command_json,
                    "referenceRaw",
                    1L,
                    INT16_MAX,
                    &reference_raw) ||
                !ble_json_read_float(
                    command_json,
                    "referenceUsCm",
                    1.0f,
                    100000.0f,
                    &reference) ||
                !ble_json_read_float(
                    command_json,
                    "temperatureCoefficient",
                    0.0f,
                    0.1f,
                    &coefficient) ||
                !ble_json_read_float(
                    command_json,
                    "referenceTemperatureC",
                    0.0f,
                    50.0f,
                    &reference_temperature) ||
                !sensor_calibration_store_save_ec(
                    static_cast<int16_t>(reference_raw),
                    reference,
                    coefficient,
                    reference_temperature)) {
                return {
                    false,
                    "invalid_calibration",
                    "Punkt EC lub kompensacja sa niepoprawne."
                };
            }
            return {
                true,
                "calibration_saved",
                "Kalibracja EC zapisana."
            };
        }
        return {
            false,
            "invalid_calibration",
            "Typ kalibracji musi miec wartosc ph albo ec."
        };
    }

    if (strcmp(action, "save_co2") == 0) {
        bool enabled = cfg.enableCo2;
        float target = co2_target_ph;
        long limit = co2_max_time_minutes;
        ble_json_read_bool(command_json, "co2Enabled", &enabled);
        if (!ble_json_read_float(command_json, "targetPh", 5.0f, 8.5f, &target) ||
            !ble_json_read_long(command_json, "co2Limit", 1L, 1440L, &limit)) {
            return {false, "invalid_co2", "Parametry CO2 sa poza zakresem."};
        }
        cfg.enableCo2 = enabled;
        co2_target_ph = target;
        co2_max_time_minutes = static_cast<uint16_t>(limit);
        gui_app_save_settings();
        return {true, "ok", "Ustawienia CO2 zapisane."};
    }

    if (strcmp(action, "save_water") == 0) {
        bool enabled = cfg.enableWaterLevel;
        long timeout = water_timeout_seconds;
        ble_json_read_bool(command_json, "waterEnabled", &enabled);
        if (!ble_json_read_long(command_json, "waterTimeout", 5L, 300L, &timeout)) {
            return {false, "invalid_water_timeout", "Limit ATO musi wynosic 5-300 sekund."};
        }
        cfg.enableWaterLevel = enabled;
        water_timeout_seconds = static_cast<uint16_t>(timeout);
        if (!enabled) {
            runtime.waterFillOn = false;
            ato_started_ms = 0U;
            ato_timeout_latched = false;
        }
        gui_app_save_settings();
        apply_mcp_outputs();
        return {true, "ok", "Ustawienia ATO zapisane."};
    }

    if (strcmp(action, "save_leak") == 0) {
        bool enabled = cfg.enableLeak;
        char value[24];
        ble_json_read_bool(command_json, "leakEnabled", &enabled);
        if (!ble_json_read_string(command_json, "leakAction", value, sizeof(value))) {
            return {false, "invalid_leak_action", "Brak poprawnej akcji wycieku."};
        }
        LeakAction parsed = leak_action;
        if (!parse_leak_action(String(value), &parsed)) {
            return {false, "invalid_leak_action", "Nieprawidlowa akcja wycieku."};
        }
        cfg.enableLeak = enabled;
        leak_action = parsed;
        gui_app_save_settings();
        return {true, "ok", "Ustawienia wycieku zapisane."};
    }

    if (strcmp(action, "save_display") == 0) {
        bool automatic = display_auto_brightness;
        long brightness = display_max_brightness;
        char profile[24];
        ble_json_read_bool(command_json, "autoBrightness", &automatic);
        if (!ble_json_read_long(command_json, "brightness", 10L, 100L, &brightness) ||
            !ble_json_read_string(command_json, "profile", profile, sizeof(profile))) {
            return {false, "invalid_display", "Nieprawidlowe ustawienia ekranu."};
        }
        DisplayPowerProfile parsed = display_power_profile;
        if (!parse_display_profile(String(profile), &parsed)) {
            return {false, "invalid_display_profile", "Nieprawidlowy profil ekranu."};
        }
        display_auto_brightness = automatic;
        display_max_brightness = static_cast<uint8_t>(brightness);
        display_power_profile = parsed;
        gui_app_save_settings();
        apply_display_backlight(last_ldr_value, last_ldr_valid);
        return {true, "ok", "Ustawienia ekranu zapisane."};
    }

    if (strcmp(action, "save_network") == 0) {
        char ssid[33];
        char password[WIFI_PASSWORD_MAX_LEN + 1U] = {};
        if (!ble_json_read_string(command_json, "staSsid", ssid, sizeof(ssid)) || ssid[0] == '\0') {
            return {false, "wifi_profile_error", "SSID musi zawierac od 1 do 32 znakow."};
        }
        ble_json_read_string(command_json, "staPassword", password, sizeof(password));
        if (password[0] != '\0' && strlen(password) < 8U) {
            secure_clear_gui_buffer(
                password, sizeof(password));
            return {false, "wifi_profile_error", "Haslo WPA musi miec co najmniej 8 znakow."};
        }
        const bool saved =
            save_wifi_profile_to_sd(ssid, password, "", 0);
        secure_clear_gui_buffer(
            password, sizeof(password));
        if (!saved) {
            return {false, "wifi_profile_error", "Nie zapisano profilu WiFi na karcie SD."};
        }
        snprintf(selected_ssid, sizeof(selected_ssid), "%s", ssid);
        return {true, "ok", "Profil WiFi zapisany."};
    }

    if (strcmp(action, "wifi_session_start") == 0) {
        try_autoconnect_wifi_profile();
        return {true, "ok", "Sesja WiFi uruchomiona."};
    }
    if (strcmp(action, "wifi_session_stop") == 0) {
        wifi_disconnect_pending = true;
        wifi_disconnect_at_ms = millis() + 250UL;
        return {true, "ok", "Sesja WiFi zostanie zatrzymana."};
    }
    if (strcmp(action, "sync_time_ntp") == 0) {
        if (!wifi_connected) {
            return {false, "wifi_required", "Synchronizacja NTP wymaga WiFi."};
        }
        return sync_clock_from_ntp(5000U)
                   ? GuiBleCommandResult{true, "ntp_started", "Synchronizacja NTP uruchomiona."}
                   : GuiBleCommandResult{false, "ntp_start_failed", "Nie mozna uruchomic synchronizacji NTP."};
    }
    if (strcmp(action, "set_time") == 0) {
        long epoch = 0L;
        if (!ble_json_read_long(command_json, "epoch", 1704067200L, 2147483647L, &epoch)) {
            return {false, "invalid_epoch", "Nieprawidlowy czas telefonu."};
        }
        setenv("TZ", NTP_TZ_POLAND, 1);
        tzset();
        const time_t raw = static_cast<time_t>(epoch);
        struct tm local_time = {};
        if (localtime_r(&raw, &local_time) == nullptr) {
            return {false, "time_convert_failed", "Nie mozna przeliczyc czasu."};
        }
        clock_year = local_time.tm_year + 1900;
        clock_month = local_time.tm_mon + 1;
        clock_day = local_time.tm_mday;
        clock_hour = local_time.tm_hour;
        clock_minute = local_time.tm_min;
        clock_second = local_time.tm_sec;
        return gui_save_clock_settings(true, "mobile_ble")
                   ? GuiBleCommandResult{true, "ok", "Czas sterownika ustawiony."}
                   : GuiBleCommandResult{false, "save_failed", "Nie zapisano czasu w NVS."};
    }
    if (strcmp(action, "clear_critical_logs") == 0) {
        gui_logs_important_count = 0U;
        return {true, "ok", "Wyczyszczono wazne logi."};
    }
    if (strcmp(action, "test_relay") == 0) {
        long channel = 0L;
        long duration = 3L;
        bool state = false;
        if (!ble_json_read_long(command_json, "channel", 1L, 8L, &channel) ||
            !ble_json_read_bool(command_json, "state", &state)) {
            return {false, "invalid_relay_channel", "Kanal musi byc w zakresie 1-8."};
        }
        ble_json_read_long(command_json, "duration", 1L, 3L, &duration);
        return start_relay_test(static_cast<uint8_t>(channel), state,
                                state ? static_cast<uint32_t>(duration) * 1000UL : 0U)
                   ? GuiBleCommandResult{true, "ok", "Test przekaznika wykonany."}
                   : GuiBleCommandResult{false, "relay_test_unavailable", "Kanal jest niedostepny."};
    }
    if (strcmp(action, "restart_device") == 0) {
        ota_reboot_reason =
            RuntimeFaultReason::ManualRestart;
        ota_reboot_pending = true;
        ota_reboot_at_ms = millis() + 1000UL;
        return {true, "ok", "Restart sterownika za chwile."};
    }
    if (strcmp(action, "factory_reset") == 0) {
        if (prefs.begin("aquarium", false)) {
            prefs.clear();
            prefs.end();
        }
        device_credentials_factory_reset();
        wifi_credential_store_clear();
        sensor_calibration_store_reset_defaults();
        remote_alarm_relay_clear();
        admin_sessions.clear();
        load_default_config(cfg);
        display_auto_brightness = true;
        display_max_brightness = 100U;
        display_power_profile = DisplayPowerProfile::AlwaysOn;
        co2_target_ph = FACTORY_CO2_TARGET_PH;
        co2_max_time_minutes = 540U;
        water_timeout_seconds = 120U;
        leak_action = LeakAction::DisableAll;
        runtime.waterFillOn = false;
        ato_started_ms = 0U;
        ato_timeout_latched = false;
        gui_app_save_settings();
        apply_mcp_outputs();
        ota_reboot_reason =
            RuntimeFaultReason::FactoryReset;
        ota_reboot_pending = true;
        ota_reboot_at_ms = millis() + 1200UL;
        return {true, "ok", "Konfiguracja fabryczna przywrocona."};
    }
    if (strcmp(action, "save_relays") == 0) {
        return {false, "transport_unsupported",
                "Profil przekaznikow jest zbyt duzy dla bezpiecznej edycji BLE; uzyj WiFi."};
    }
    return {false, "unknown_action", "Nieznana akcja BLE."};
}

GuiV2AuthResult gui_app_v2_auth(const char *pin,
                                char *out_token,
                                size_t out_token_size) {
    GuiMutexGuard guard(500U);
    if (!guard.locked() || !gui_ready) {
        return {false, "controller_busy", "Sterownik jest chwilowo zajety.", 0U, 0U};
    }
    const uint32_t entropy[4] = {
        esp_random(), esp_random(), esp_random(), esp_random()
    };
    const aquarium::AuthenticationStatus status = admin_sessions.authenticate(
        gui_ble_pin_valid(pin),
        millis(),
        entropy,
        out_token,
        out_token_size);
    switch (status.result) {
    case aquarium::AuthenticationResult::Authenticated:
        device_credentials_acknowledge_setup_pin();
        return {
            true,
            "authenticated",
            "Sesja administratora jest aktywna.",
            aquarium::AdminSessionManager::kSessionTtlMs / 1000U,
            0U
        };
    case aquarium::AuthenticationResult::RateLimited:
        return {
            false,
            "auth_rate_limited",
            "Zbyt wiele blednych prob PIN. Sprobuj ponownie pozniej.",
            0U,
            status.retry_after_seconds
        };
    case aquarium::AuthenticationResult::InvalidPin:
        return {false, "pin_invalid", "Nieprawidlowy PIN administratora.", 0U, 0U};
    default:
        return {false, "token_generation_failed", "Nie mozna utworzyc sesji.", 0U, 0U};
    }
}

namespace {

GuiBleCommandResult control_mode_result(aquarium::ControlModeResult result) {
    switch (result) {
    case aquarium::ControlModeResult::Applied:
        return {true, "ok", "Polecenie zostalo zastosowane."};
    case aquarium::ControlModeResult::InvalidTarget:
        return {false, "invalid_target", "Nieznane wyjscie sterownika."};
    case aquarium::ControlModeResult::InvalidDuration:
        return {false, "invalid_duration", "Czas dzialania jest poza dozwolonym zakresem."};
    case aquarium::ControlModeResult::ModeConflict:
    default:
        return {false, "mode_conflict", "Aktywny tryb bezpieczenstwa blokuje polecenie."};
    }
}

void force_safe_service_outputs() {
    runtime.lightOn = false;
    runtime.plantLightOn = false;
    runtime.filterOn = false;
    runtime.heaterOn = false;
    runtime.airOn = false;
    runtime.co2On = false;
    runtime.waterFillOn = false;
    ato_started_ms = 0U;
    apply_mcp_outputs();
}

GuiBleCommandResult execute_v2_control_action_locked(const char *action,
                                                     const char *command_json) {
    if (strcmp(action, "set_light_profile") == 0) {
        char target[16] = {};
        char profile_text[16] = {};
        aquarium::AquaelProfile profile = aquarium::AquaelProfile::Day;
        if (!ble_json_read_string(command_json, "target", target, sizeof(target)) ||
            !ble_json_read_string(
                command_json, "profile", profile_text, sizeof(profile_text)) ||
            !aquarium::AquaelLightController::parse_profile(
                profile_text, &profile)) {
            return {
                false,
                "invalid_light_profile",
                "Wymagane: target front/rear i profile day/daybreak/night."
            };
        }
        const uint8_t encoded = static_cast<uint8_t>(profile);
        bool active = false;
        if (strcmp(target, "front") == 0 || strcmp(target, "light1") == 0 ||
            strcmp(target, "light") == 0) {
            cfg.lightColorMode = encoded;
            cfg.lightSchedColorMode = aquael_profile_to_schedule(encoded);
            runtime.lightActiveMode = encoded;
            active = runtime.lightOn;
        } else if (strcmp(target, "rear") == 0 ||
                   strcmp(target, "light2") == 0 ||
                   strcmp(target, "plant") == 0 ||
                   strcmp(target, "plant_light") == 0) {
            cfg.plantLightColorMode = encoded;
            cfg.plantSchedColorMode = aquael_profile_to_schedule(encoded);
            runtime.plantLightActiveMode = encoded;
            active = runtime.plantLightOn;
        } else {
            return {
                false,
                "invalid_target",
                "Lampa musi miec target front albo rear."
            };
        }
        gui_app_save_settings();
        apply_mcp_outputs();
        return active
                   ? GuiBleCommandResult{
                         true,
                         "light_profile_transition_started",
                         "Zmiana profilu lampy zostala uruchomiona."}
                   : GuiBleCommandResult{
                         true,
                         "light_profile_saved",
                         "Profil zapisany i zostanie ustawiony przy wlaczeniu lampy."};
    }
    if (strcmp(action, "set_timed_override") == 0) {
        char target_name[24] = {};
        bool state = false;
        long duration_seconds = 0L;
        if (!ble_json_read_string(command_json, "target", target_name, sizeof(target_name)) ||
            !ble_json_read_bool(command_json, "state", &state) ||
            !ble_json_read_long(
                command_json,
                "durationSec",
                aquarium::ControlModeManager::kOverrideMinSeconds,
                aquarium::ControlModeManager::kOverrideMaxSeconds,
                &duration_seconds)) {
            return {
                false,
                "invalid_arguments",
                "Wymagane: target, state i durationSec 30-86400."
            };
        }
        if (!cfg.devMode && !hal_mcp_is_present()) {
            return {false, "output_unavailable", "Wyjscia fizyczne sa niedostepne."};
        }
        const aquarium::OutputTarget target =
            aquarium::ControlModeManager::parse_target(target_name);
        const GuiBleCommandResult result = control_mode_result(
            control_modes.set_override(
                target,
                state,
                static_cast<uint32_t>(duration_seconds),
                millis()));
        if (result.success && !state) {
            switch (target) {
            case aquarium::OutputTarget::Light1:
                runtime.lightOn = false;
                break;
            case aquarium::OutputTarget::Light2:
                runtime.plantLightOn = false;
                break;
            case aquarium::OutputTarget::Filter:
                runtime.filterOn = false;
                break;
            case aquarium::OutputTarget::Heater:
                runtime.heaterOn = false;
                break;
            case aquarium::OutputTarget::Aeration:
                runtime.airOn = false;
                break;
            case aquarium::OutputTarget::Co2:
                runtime.co2On = false;
                break;
            case aquarium::OutputTarget::WaterDosing:
                runtime.waterFillOn = false;
                ato_started_ms = 0U;
                break;
            default:
                break;
            }
            apply_mcp_outputs();
        }
        return result.success
                   ? GuiBleCommandResult{
                         true,
                         "override_active",
                         "Tymczasowe sterowanie aktywne; po czasie wroci AUTO."}
                   : result;
    }
    if (strcmp(action, "clear_timed_override") == 0) {
        char target_name[24] = {};
        if (!ble_json_read_string(command_json, "target", target_name, sizeof(target_name))) {
            return {false, "invalid_arguments", "Brak poprawnego target."};
        }
        const GuiBleCommandResult result = control_mode_result(
            control_modes.clear_override(
                aquarium::ControlModeManager::parse_target(target_name)));
        return result.success
                   ? GuiBleCommandResult{
                         true,
                         "automatic_restored",
                         "Dla wyjscia przywrocono tryb AUTO."}
                   : result;
    }
    if (strcmp(action, "start_feeding_mode") == 0) {
        long duration_seconds = 600L;
        bool dispense = false;
        ble_json_read_long(
            command_json,
            "durationSec",
            aquarium::ControlModeManager::kFeedingMinSeconds,
            aquarium::ControlModeManager::kFeedingMaxSeconds,
            &duration_seconds);
        ble_json_read_bool(command_json, "dispense", &dispense);
        const GuiBleCommandResult result = control_mode_result(
            control_modes.start_feeding(
                static_cast<uint32_t>(duration_seconds), millis()));
        if (!result.success) {
            return result;
        }
        runtime.filterOn = false;
        runtime.co2On = false;
        runtime.waterFillOn = false;
        ato_started_ms = 0U;
        apply_mcp_outputs();
        if (dispense && !run_feeder_pulse(
                            "Karmienie", "Dawka z trybu karmienia", false)) {
            return {
                true,
                "feeding_mode_started_no_dispense",
                "Tryb karmienia aktywny, ale karmnik byl zajety."
            };
        }
        return {true, "feeding_mode_active", "Tryb karmienia aktywny."};
    }
    if (strcmp(action, "stop_feeding_mode") == 0) {
        control_modes.stop_feeding();
        return {true, "automatic_restored", "Tryb karmienia zakonczony; powrot do AUTO."};
    }
    if (strcmp(action, "start_service_mode") == 0) {
        long duration_seconds = 1800L;
        ble_json_read_long(
            command_json,
            "durationSec",
            aquarium::ControlModeManager::kServiceMinSeconds,
            aquarium::ControlModeManager::kServiceMaxSeconds,
            &duration_seconds);
        const GuiBleCommandResult result = control_mode_result(
            control_modes.start_service(
                static_cast<uint32_t>(duration_seconds), millis()));
        if (!result.success) {
            return result;
        }
        force_safe_service_outputs();
        return {
            true,
            "service_mode_active",
            "Tryb serwisowy aktywny; wyjscia sa bezpiecznie wylaczone."
        };
    }
    if (strcmp(action, "stop_service_mode") == 0) {
        control_modes.stop_service();
        return {true, "automatic_restored", "Tryb serwisowy zakonczony; powrot do AUTO."};
    }
    if (strcmp(action, "save_espnow_link") == 0) {
        char peer_mac[18] = {};
        char pmk_hex[33] = {};
        char lmk_hex[33] = {};
        long channel = 0L;
        const bool parsed =
            ble_json_read_string(
                command_json, "peerMac", peer_mac, sizeof(peer_mac)) &&
            ble_json_read_string(
                command_json, "pmk", pmk_hex, sizeof(pmk_hex)) &&
            ble_json_read_string(
                command_json, "lmk", lmk_hex, sizeof(lmk_hex)) &&
            ble_json_read_long(command_json, "channel", 1L, 13L, &channel);
        const bool saved =
            parsed &&
            espnow_link_configure(
                peer_mac,
                pmk_hex,
                lmk_hex,
                static_cast<uint8_t>(channel));
        secure_clear_gui_buffer(pmk_hex, sizeof(pmk_hex));
        secure_clear_gui_buffer(lmk_hex, sizeof(lmk_hex));
        return saved
                   ? GuiBleCommandResult{
                         true,
                         "espnow_saved_restart_required",
                         "Szyfrowane lacze zapisane; uruchom sterownik ponownie."}
                   : GuiBleCommandResult{
                         false,
                         "invalid_espnow_configuration",
                         "Sprawdz MAC, kanal oraz 32-znakowe klucze PMK i LMK."};
    }
    if (strcmp(action, "clear_espnow_link") == 0) {
        return espnow_link_clear_configuration()
                   ? GuiBleCommandResult{
                         true,
                         "espnow_cleared_restart_required",
                         "Konfiguracja lacza usunieta; uruchom sterownik ponownie."}
                   : GuiBleCommandResult{
                         false,
                         "espnow_clear_failed",
                         "Nie mozna usunac konfiguracji lacza z NVS."};
    }
    if (is_remote_gateway_action(action)) {
        return execute_remote_gateway_action(
            action, command_json);
    }
    if (strcmp(action, "forget_ble_bonds") == 0) {
        return ble_controller_request_forget_bonds()
                   ? GuiBleCommandResult{
                         true,
                         "ble_bonds_reset_scheduled",
                         "Zapisane telefony BLE zostana usuniete; polaczenie zostanie zamkniete."}
                   : GuiBleCommandResult{
                         false,
                         "ble_unavailable",
                         "Stos BLE nie jest jeszcze gotowy."};
    }
    return {false, "unknown_action", "Nieznana akcja protokolu v2."};
}

bool is_v2_control_action(const char *action) {
    return action != nullptr &&
           (strcmp(action, "set_light_profile") == 0 ||
            strcmp(action, "set_timed_override") == 0 ||
            strcmp(action, "clear_timed_override") == 0 ||
            strcmp(action, "start_feeding_mode") == 0 ||
            strcmp(action, "stop_feeding_mode") == 0 ||
             strcmp(action, "start_service_mode") == 0 ||
             strcmp(action, "stop_service_mode") == 0 ||
             strcmp(action, "save_espnow_link") == 0 ||
             strcmp(action, "clear_espnow_link") == 0 ||
             is_remote_gateway_action(action) ||
             strcmp(action, "forget_ble_bonds") == 0);
}

uint32_t v2_action_fingerprint(const char *action, const char *command_json) {
    char canonical[160] = {};
    if (strcmp(action, "set_light_profile") == 0) {
        char target[24] = {};
        char profile[16] = {};
        ble_json_read_string(command_json, "target", target, sizeof(target));
        ble_json_read_string(command_json, "profile", profile, sizeof(profile));
        snprintf(canonical, sizeof(canonical), "%s|%s|%s",
                 action, target, profile);
    } else if (strcmp(action, "set_timed_override") == 0) {
        char target[24] = {};
        bool state = false;
        long duration = 0L;
        ble_json_read_string(command_json, "target", target, sizeof(target));
        ble_json_read_bool(command_json, "state", &state);
        ble_json_read_long(
            command_json,
            "durationSec",
            0L,
            aquarium::ControlModeManager::kOverrideMaxSeconds,
            &duration);
        snprintf(canonical, sizeof(canonical), "%s|%s|%u|%ld",
                 action, target, state ? 1U : 0U, duration);
    } else if (strcmp(action, "clear_timed_override") == 0) {
        char target[24] = {};
        ble_json_read_string(command_json, "target", target, sizeof(target));
        snprintf(canonical, sizeof(canonical), "%s|%s", action, target);
    } else if (strcmp(action, "start_feeding_mode") == 0) {
        long duration = 600L;
        bool dispense = false;
        ble_json_read_long(
            command_json,
            "durationSec",
            aquarium::ControlModeManager::kFeedingMinSeconds,
            aquarium::ControlModeManager::kFeedingMaxSeconds,
            &duration);
        ble_json_read_bool(command_json, "dispense", &dispense);
        snprintf(canonical, sizeof(canonical), "%s|%ld|%u",
                 action, duration, dispense ? 1U : 0U);
    } else if (strcmp(action, "start_service_mode") == 0) {
        long duration = 1800L;
        ble_json_read_long(
            command_json,
            "durationSec",
            aquarium::ControlModeManager::kServiceMinSeconds,
            aquarium::ControlModeManager::kServiceMaxSeconds,
            &duration);
        snprintf(canonical, sizeof(canonical), "%s|%ld", action, duration);
    } else if (strcmp(action, "save_espnow_link") == 0) {
        char peer_mac[18] = {};
        char pmk_hex[33] = {};
        char lmk_hex[33] = {};
        long channel = 0L;
        ble_json_read_string(
            command_json, "peerMac", peer_mac, sizeof(peer_mac));
        ble_json_read_string(
            command_json, "pmk", pmk_hex, sizeof(pmk_hex));
        ble_json_read_string(
            command_json, "lmk", lmk_hex, sizeof(lmk_hex));
        ble_json_read_long(
            command_json, "channel", 1L, 13L, &channel);
        snprintf(
            canonical,
            sizeof(canonical),
            "%s|%s|%08lx|%08lx|%ld",
            action,
            peer_mac,
            static_cast<unsigned long>(
                aquarium::IdempotencyLedger::fingerprint(pmk_hex)),
            static_cast<unsigned long>(
                aquarium::IdempotencyLedger::fingerprint(lmk_hex)),
            channel);
        secure_clear_gui_buffer(pmk_hex, sizeof(pmk_hex));
        secure_clear_gui_buffer(lmk_hex, sizeof(lmk_hex));
    } else if (strcmp(
                   action,
                   "save_remote_gateway") == 0) {
        char base_url[
            REMOTE_ALARM_RELAY_URL_BYTES] = {};
        char device_id[
            REMOTE_ALARM_RELAY_DEVICE_ID_BYTES] = {};
        char hmac_secret[136] = {};
        bool enabled = true;
        ble_json_read_string(
            command_json,
            "baseUrl",
            base_url,
            sizeof(base_url));
        ble_json_read_string(
            command_json,
            "deviceId",
            device_id,
            sizeof(device_id));
        ble_json_read_string(
            command_json,
            "hmacSecret",
            hmac_secret,
            sizeof(hmac_secret));
        ble_json_read_bool(
            command_json, "enabled", &enabled);
        snprintf(
            canonical,
            sizeof(canonical),
            "%s|%08lx|%08lx|%08lx|%u",
            action,
            static_cast<unsigned long>(
                aquarium::IdempotencyLedger::
                    fingerprint(base_url)),
            static_cast<unsigned long>(
                aquarium::IdempotencyLedger::
                    fingerprint(device_id)),
            static_cast<unsigned long>(
                aquarium::IdempotencyLedger::
                    fingerprint(hmac_secret)),
            enabled ? 1U : 0U);
        secure_clear_gui_buffer(
            hmac_secret, sizeof(hmac_secret));
    } else if (strcmp(
                   action,
                   "set_remote_gateway_enabled") == 0) {
        bool enabled = false;
        ble_json_read_bool(
            command_json, "enabled", &enabled);
        snprintf(
            canonical,
            sizeof(canonical),
            "%s|%u",
            action,
            enabled ? 1U : 0U);
    } else {
        snprintf(canonical, sizeof(canonical), "%s", action);
    }
    return aquarium::IdempotencyLedger::fingerprint(canonical);
}

} // namespace

GuiBleCommandResult gui_app_v2_action(const char *action,
                                      const char *command_json,
                                      const char *pin,
                                      const char *token,
                                      const char *command_id,
                                      bool *out_duplicate,
                                      char *out_replay_code,
                                      size_t out_replay_code_size,
                                      char *out_replay_message,
                                      size_t out_replay_message_size) {
    GuiMutexGuard guard(1000U);
    if (out_duplicate != nullptr) {
        *out_duplicate = false;
    }
    if (out_replay_code != nullptr && out_replay_code_size > 0U) {
        out_replay_code[0] = '\0';
    }
    if (out_replay_message != nullptr && out_replay_message_size > 0U) {
        out_replay_message[0] = '\0';
    }
    if (!guard.locked() || !gui_ready) {
        return {false, "controller_busy", "Sterownik jest chwilowo zajety."};
    }
    if (action == nullptr || command_json == nullptr) {
        return {false, "invalid_action", "Nieprawidlowa komenda v2."};
    }
    (void)pin;
    const bool token_valid =
        token != nullptr && token[0] != '\0' &&
        admin_sessions.validate(token, millis());
    const bool v2_control_action = is_v2_control_action(action);
    if (!v2_control_action) {
        return {
            false,
            "token_action_unsupported",
            "Ta starsza akcja wymaga osobnego, zgodnego wywolania v1."
        };
    }
    if (!token_valid) {
        return {
            false,
            token != nullptr && token[0] != '\0'
                ? "session_expired"
                : "session_required",
            token != nullptr && token[0] != '\0'
                ? "Sesja administratora wygasla lub jest nieprawidlowa."
                : "Wymagana aktywna sesja administratora."
        };
    }
    if (!aquarium::IdempotencyLedger::valid_command_id(command_id)) {
        return {
            false,
            "invalid_command_id",
            "commandId musi miec 8-48 bezpiecznych znakow ASCII."
        };
    }

    const uint32_t fingerprint =
        v2_action_fingerprint(action, command_json);
    aquarium::CachedCommandResult cached = {};
    const aquarium::CommandLookup lookup =
        command_ledger.lookup(command_id, fingerprint, millis(), &cached);
    if (lookup == aquarium::CommandLookup::Duplicate) {
        if (out_duplicate != nullptr) {
            *out_duplicate = true;
        }
        if (out_replay_code == nullptr || out_replay_code_size == 0U ||
            out_replay_message == nullptr || out_replay_message_size == 0U) {
            return {
                cached.success,
                "duplicate_replayed",
                "Polecenie bylo juz wykonane; zwrocono zapisany wynik."
            };
        }
        snprintf(
            out_replay_code, out_replay_code_size, "%s", cached.code);
        snprintf(
            out_replay_message,
            out_replay_message_size,
            "%s",
            cached.message);
        return {cached.success, out_replay_code, out_replay_message};
    }
    if (lookup == aquarium::CommandLookup::Conflict) {
        return {
            false,
            "command_id_conflict",
            "commandId byl juz uzyty z innym ladunkiem."
        };
    }
    if (lookup == aquarium::CommandLookup::InvalidId) {
        return {false, "invalid_command_id", "Nieprawidlowy commandId."};
    }

    const GuiBleCommandResult result =
        execute_v2_control_action_locked(action, command_json);
    aquarium::CachedCommandResult stored = {};
    stored.success = result.success;
    snprintf(stored.code, sizeof(stored.code), "%s",
             result.code != nullptr ? result.code : "internal_error");
    snprintf(stored.message, sizeof(stored.message), "%s",
             result.message != nullptr ? result.message : "");
    command_ledger.remember(command_id, fingerprint, stored, millis());
    return result;
}

GuiBleCommandResult gui_app_trusted_link_action(const char *action,
                                                const char *command_json,
                                                const char *command_id,
                                                bool *out_duplicate) {
    GuiMutexGuard guard(1000U);
    if (out_duplicate != nullptr) {
        *out_duplicate = false;
    }
    if (!guard.locked() || !gui_ready) {
        return {false, "controller_busy", "Sterownik jest chwilowo zajety."};
    }
    const bool action_allowed =
        action != nullptr &&
        (strcmp(action, "set_timed_override") == 0 ||
         strcmp(action, "start_feeding_mode") == 0);
    if (!action_allowed || command_json == nullptr) {
        return {
            false,
            "trusted_action_unsupported",
            "Polecenie nie nalezy do bezpiecznego podzbioru lacza."
        };
    }
    if (!aquarium::IdempotencyLedger::valid_command_id(command_id)) {
        return {
            false,
            "invalid_command_id",
            "Nieprawidlowy identyfikator polecenia lacza."
        };
    }

    const uint32_t fingerprint =
        v2_action_fingerprint(action, command_json);
    aquarium::CachedCommandResult cached = {};
    const aquarium::CommandLookup lookup =
        command_ledger.lookup(command_id, fingerprint, millis(), &cached);
    if (lookup == aquarium::CommandLookup::Duplicate) {
        if (out_duplicate != nullptr) {
            *out_duplicate = true;
        }
        return {
            cached.success,
            "duplicate_replayed",
            "Polecenie lacza bylo juz wykonane."
        };
    }
    if (lookup == aquarium::CommandLookup::Conflict) {
        return {
            false,
            "command_id_conflict",
            "Identyfikator byl juz uzyty z innym poleceniem."
        };
    }
    if (lookup == aquarium::CommandLookup::InvalidId) {
        return {
            false,
            "invalid_command_id",
            "Nieprawidlowy identyfikator polecenia lacza."
        };
    }

    const GuiBleCommandResult result =
        execute_v2_control_action_locked(action, command_json);
    aquarium::CachedCommandResult stored = {};
    stored.success = result.success;
    snprintf(
        stored.code,
        sizeof(stored.code),
        "%s",
        result.code != nullptr ? result.code : "internal_error");
    snprintf(
        stored.message,
        sizeof(stored.message),
        "%s",
        result.message != nullptr ? result.message : "");
    command_ledger.remember(command_id, fingerprint, stored, millis());
    return result;
}

bool gui_app_v2_capabilities_json(char *out, size_t out_size) {
    if (out == nullptr || out_size < 512U) {
        return false;
    }
    const OtaGuardStatus ota = ota_guard_status();
    const int written = snprintf(
        out,
        out_size,
        "{\"type\":\"capabilities\",\"v\":2,\"ts\":%lu,\"data\":{"
        "\"device\":\"cydAkwarium\",\"firmwareVersion\":\"%s\","
        "\"apiVersions\":[1,2],"
        "\"transports\":[\"http\",\"ble\"],"
        "\"features\":{\"timedOverrides\":true,\"feedingMode\":true,"
        "\"serviceMode\":true,\"safeOta\":true,\"idempotency\":true,"
        "\"adminSessions\":true,\"aquaelLightProfiles\":true,"
        "\"signedFirmwarePackages\":true,\"sensorCalibration\":true,"
        "\"persistentAlarmTransitions\":true,\"runtimeWatchdog\":true,"
        "\"remoteAlarmGateway\":true},"
        "\"security\":{\"ble\":{\"linkEncryption\":true,\"bonding\":true,"
        "\"mitmProtection\":true,\"secureConnections\":true,"
        "\"minimumKeySize\":16,\"bondCount\":%d,"
        "\"applicationAuth\":\"short_lived_token\"},"
        "\"ota\":{\"algorithm\":\"rsa-pss-sha256\","
        "\"trustedKeyId\":\"%s\",\"unsignedOtaAllowed\":%s},"
        "\"remoteGateway\":{\"provisioningTransport\":\"ble\","
        "\"linkEncryptionRequired\":true,\"bondRequired\":true,"
        "\"adminSessionRequired\":true}},"
        "\"endpoints\":{\"status\":{\"method\":\"GET\",\"path\":\"/api/status\"},"
        "\"capabilities\":{\"method\":\"GET\",\"path\":\"/api/v2/capabilities\"},"
        "\"auth\":{\"method\":\"POST\",\"path\":\"/api/v2/auth\","
        "\"contentType\":\"application/json\"},"
        "\"alarmEvents\":{\"method\":\"GET\","
        "\"path\":\"/api/v2/alarm-events\"},"
        "\"action\":{\"method\":\"POST\",\"path\":\"/api/action\","
        "\"contentType\":\"application/x-www-form-urlencoded\","
        "\"jsonAlias\":\"/api/v2/action\"}},"
        "\"auth\":{\"scheme\":\"short_lived_token\",\"ttlSec\":300,"
        "\"maxSessions\":2,\"maxFailedPinAttempts\":5,\"lockoutSec\":60,"
        "\"pinFallbackV1\":false,\"logoutPath\":\"/api/v2/logout\"},"
        "\"limits\":{\"commandIdMin\":8,\"commandIdMax\":48,"
        "\"overrideMinSec\":30,\"overrideMaxSec\":86400,"
        "\"feedingMinSec\":60,\"feedingMaxSec\":3600,"
        "\"serviceMinSec\":60,\"serviceMaxSec\":7200,"
        "\"lightCycleOffMs\":1000,\"lightProfileToggleMaxOffMs\":5000,"
        "\"lightResetThresholdMs\":5000,\"lightCalibrationOffMs\":6000},"
        "\"targets\":[\"light1\",\"light2\",\"filter\",\"heater\","
        "\"aeration\",\"co2\",\"water_dosing\"],"
        "\"actions\":[\"set_light_profile\",\"set_timed_override\","
        "\"clear_timed_override\",\"start_feeding_mode\","
        "\"stop_feeding_mode\",\"start_service_mode\",\"stop_service_mode\","
        "\"forget_ble_bonds\",\"save_calibration\","
        "\"save_remote_gateway\",\"set_remote_gateway_enabled\","
        "\"clear_remote_gateway\",\"save_espnow_link\","
        "\"clear_espnow_link\"],"
        "\"lights\":{\"front\":{\"label\":\"Przednia\",\"relay\":\"light1\"},"
        "\"rear\":{\"label\":\"Tylna\",\"relay\":\"light2\"},"
        "\"profiles\":[\"day\",\"daybreak\",\"night\"]},"
        "\"ota\":{\"format\":\"aqfw-v1\",\"productId\":\"aquacyd-cyd\","
        "\"target\":\"%s\",\"keyId\":\"%s\","
        "\"bootloaderVersion\":%u,\"securityVersion\":%lu,"
        "\"minimumSecurityVersion\":%lu,\"rollbackAvailable\":%s,"
        "\"updatePartitionBytes\":%lu,\"pendingVerify\":%s,"
        "\"state\":\"%s\"}}}",
        static_cast<unsigned long>(controller_unix_time()),
        FirmwareInfo::VERSION,
        ble_controller_bond_count(),
        FirmwareTrust::KEY_ID,
        AQUARIUM_ALLOW_UNSIGNED_ARDUINO_OTA ? "true" : "false",
#if CYD_PANEL_ST7789
        "st7789",
#else
        "ili9341",
#endif
        FirmwareTrust::KEY_ID,
        static_cast<unsigned>(FirmwareInfo::BOOTLOADER_COMPATIBILITY_VERSION),
        static_cast<unsigned long>(FirmwareInfo::SECURITY_VERSION),
        static_cast<unsigned long>(ota.minimum_security_version),
        ota.rollback_available ? "true" : "false",
        static_cast<unsigned long>(ota.update_partition_bytes),
        ota.pending_verify ? "true" : "false",
        ota.state != nullptr ? ota.state : "unknown");
    return written > 0 && static_cast<size_t>(written) < out_size;
}
