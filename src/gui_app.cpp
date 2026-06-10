#include "gui_app.h"

#include "config.h"
#include "events.h"
#include "hal_adc.h"
#include "hal_mcp23017.h"

#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <lvgl.h>
#include <math.h>
#include <string.h>
#include <WiFi.h>
#include <ArduinoOTA.h>

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

constexpr uint32_t UI_CONFIG_MAGIC = 0x43594441UL;
constexpr uint16_t UI_CONFIG_VERSION = 12; // Bumped: nowe domyslne (jasny motyw, LDR off)
constexpr uint8_t MINUTE_STEP = 5;
constexpr uint8_t PAGE_COUNT = 5;
constexpr uint8_t TEMP_HISTORY_POINTS = 32;
constexpr int LDR_ADC_MIN = 0;
constexpr int LDR_ADC_MAX = 4095;
constexpr int LDR_THEME_HYSTERESIS = 15;
constexpr uint32_t UI_RUNTIME_SUBPAGE_MIN_FREE = 18000UL;
constexpr uint32_t UI_RUNTIME_MODAL_MIN_FREE = 12000UL;
constexpr uint32_t UI_RUNTIME_BIGGEST_MIN = 4096UL;
constexpr uint32_t UI_RUNTIME_HARDWARE_MIN_FREE = 6000UL;
constexpr uint32_t UI_RUNTIME_HARDWARE_MIN_LARGEST = 2048UL;

enum class ScheduleMode : uint8_t {
    Schedule = 0,
    AlwaysOn = 1,
    AlwaysOff = 2
};

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

#define SPEAKER_PIN 26
constexpr uint8_t SPEAKER_LEDC_CHANNEL = 0;

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
    float lastTemp;
    float previousTemp;
    float lastPh;
    uint32_t lastAutoFeedMs;
};

static Preferences prefs;
static AquariumUiConfig cfg;
static UiRuntimeState runtime = {
    0,     // lightActiveMode
    0,     // plantLightActiveMode
    false, // lightOn
    false, // plantLightOn
    false, // filterOn
    false, // airOn
    false, // heaterOn
    24.5f, // lastTemp
    24.5f, // previousTemp
    7.20f, // lastPh
    0      // lastAutoFeedMs
};

struct SensorDebugSnapshot {
    int ldrValue;
    bool adcPresent;
    bool phValid;
    int16_t phRaw;
    float phVoltage;
    bool ecValid;
    int16_t ecRaw;
    float ecVoltage;
    bool mcpPresent;
    bool mcpValid;
    uint16_t mcpState;
    uint32_t updatedMs;
};

static SensorDebugSnapshot sensor_debug = {
    0,
    false,
    false,
    0,
    0.0f,
    false,
    0,
    0.0f,
    false,
    false,
    0,
    0
};

static lv_obj_t *pages[PAGE_COUNT];
static lv_obj_t *nav_btns[PAGE_COUNT];

static lv_obj_t *label_date;
static lv_obj_t *label_power_mode;
static lv_obj_t *label_rtc_bat;
static lv_obj_t *label_wifi_state;
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
static uint32_t boot_count_val = 0;
static lv_obj_t *power_warning_lbl_global;
static lv_obj_t *power_state_lbl = nullptr;
static lv_obj_t *screen_always_on_sw;
static lv_obj_t *screen_dev_mode_sw = nullptr;
static lv_obj_t *screen_manual_theme_sw;
static lv_obj_t *screen_ldr_enable_sw;
static lv_obj_t *screen_ldr_slider;
static lv_obj_t *screen_ldr_value_lbl;
static lv_obj_t *screen_ldr_raw_lbl;
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
static volatile bool musicPlaying = false;
static int musicVolume = 5; // default 50%
static int selectedSongIndex = 0;
static TaskHandle_t musicTaskHandle = nullptr;
static lv_obj_t *device_ph_detail_lbl = nullptr;
static lv_obj_t *btn_sync_ntp_lbl_global;
static lv_obj_t *clock_ntp_row = nullptr;
static lv_obj_t *modal_feeder_title_lbl;
static lv_obj_t *modal_feeder_msg_lbl;

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
    case ScheduleDevice::Light: return "Light";
    case ScheduleDevice::PlantLight: return "PlantLight";
    case ScheduleDevice::Filter: return "Filter";
    case ScheduleDevice::Air: return "Air";
    case ScheduleDevice::Feed: return "Feed";
    case ScheduleDevice::QuietHours: return "QuietHours";
    default: return "Unknown";
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
constexpr char PIN_KEY_BACK[] = "DEL";
constexpr char PIN_KEY_OK[] = "OK";
static bool pin_authenticated = false;
static uint32_t pin_auth_until_ms = 0;
static PendingPinAction pending_pin_action = {PinAction::None, 0, false};
static lv_obj_t *pin_overlay = nullptr;
static lv_obj_t *pin_value_lbl = nullptr;
static lv_obj_t *pin_status_lbl = nullptr;
static lv_obj_t *pin_matrix = nullptr;
static char pin_entry[5] = "";

static bool is_scanning = false;
static unsigned long conn_start_ms = 0;
static bool is_connecting = false;
static char selected_ssid[64] = "";
static lv_timer_t *wifi_check_timer = nullptr;
static bool scan_started = false;
static unsigned long scan_start_ms = 0;

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
static uint8_t chart_range_index = 0;
static float temp_history[TEMP_HISTORY_POINTS];
static bool heater_history[TEMP_HISTORY_POINTS];
static float ph_history[TEMP_HISTORY_POINTS];
static int ldr_history[TEMP_HISTORY_POINTS];
static uint32_t heap_history[TEMP_HISTORY_POINTS];
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

static void add_gui_log(const char *msg, bool is_important);
static void chart_draw_event_cb(lv_event_t *e);
static void btn_sta_handler(lv_event_t *e);
static void btn_ota_handler(lv_event_t *e);
static void select_network_cb(lv_event_t *e);
static void keyboard_ready_cb(lv_event_t *e);
static void cancel_sta_cb(lv_event_t *e);
static void cancel_pwd_cb(lv_event_t *e);
static void stop_ota_cb(lv_event_t *e);


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
static void request_gui_rebuild_async();
static void execute_pin_action(const PendingPinAction &action);
static void open_sched_editor_authorized(ScheduleDevice device);
static void open_heater_subpage_authorized();
static void open_ph_subpage_authorized();
static void open_time_picker_authorized();
static void open_date_picker_authorized();
static void start_ota_authorized();
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

static uint8_t clamp_u8(int value, int low, int high) {
    if (value < low) {
        return static_cast<uint8_t>(low);
    }
    if (value > high) {
        return static_cast<uint8_t>(high);
    }
    return static_cast<uint8_t>(value);
}

static uint8_t snap_minute(int minute) {
    const int bounded = constrain(minute, 0, 59);
    const int snapped = ((bounded + (MINUTE_STEP / 2)) / MINUTE_STEP) * MINUTE_STEP;
    return static_cast<uint8_t>(min(snapped, 55));
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
        return color;
    }
    if (same_color(color, 255, 255, 255) || same_color(color, 226, 232, 240)) {
        return theme_text_main();
    }
    if (same_color(color, 148, 163, 184) || same_color(color, 100, 116, 139)) {
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

static void load_default_config(AquariumUiConfig &out) {
    memset(&out, 0, sizeof(out));
    out.magic = UI_CONFIG_MAGIC;
    out.version = 11;
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
    out.plantLightColorMode = 2;
    out.filterMode = static_cast<uint8_t>(ScheduleMode::Schedule);
    out.airMode = static_cast<uint8_t>(ScheduleMode::AlwaysOff); // OFF by default
    out.lightStartHour = 10;
    out.lightStartMinute = 0;
    out.lightEndHour = 21;
    out.lightEndMinute = 0;
    out.lightSchedColorMode = 1; // MIX
    
    out.plantStartHour = 12;
    out.plantStartMinute = 0;
    out.plantEndHour = 18;
    out.plantEndMinute = 0;
    out.plantSchedColorMode = 3; // PLANT
    
    out.filterStartHour = 10;
    out.filterStartMinute = 30;
    out.filterEndHour = 20;
    out.filterEndMinute = 30;
    
    out.airStartHour = 10;
    out.airStartMinute = 0;
    out.airEndHour = 19;
    out.airEndMinute = 0;
    out.heaterMode = static_cast<uint8_t>(HeaterMode::Off); // OFF by default
    out.targetTemp = 25.0f;
    out.tempHysteresis = 0.5f;
    out.feedEnabled = false; // OFF by default
    out.feedDays = 0x7F; // Mon-Sun enabled
    out.feedCount = 1; // 1 time per day
    out.feedHour1 = 18;
    out.feedMinute1 = 0;
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
    out.devMode = false; // OFF by default — GPIO 36 (was DEV_PH_ADC_PIN) conflicts with XPT2046 T_IRQ
    out.modemSleep = false;
    out.crc32 = config_crc(out);
}

static void sanitize_config(AquariumUiConfig &value) {
    value.magic = UI_CONFIG_MAGIC;
    value.version = UI_CONFIG_VERSION;

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

    value.lightColorMode = clamp_u8(value.lightColorMode, 0, 2);
    value.plantLightColorMode = clamp_u8(value.plantLightColorMode, 0, 2);

    value.lightStartHour = clamp_u8(value.lightStartHour, 0, 23);
    value.lightStartMinute = snap_minute(value.lightStartMinute);
    value.lightEndHour = clamp_u8(value.lightEndHour, 0, 23);
    value.lightEndMinute = snap_minute(value.lightEndMinute);
    value.lightSchedColorMode = clamp_u8(value.lightSchedColorMode, 1, 3);
    
    value.plantStartHour = clamp_u8(value.plantStartHour, 0, 23);
    value.plantStartMinute = snap_minute(value.plantStartMinute);
    value.plantEndHour = clamp_u8(value.plantEndHour, 0, 23);
    value.plantEndMinute = snap_minute(value.plantEndMinute);
    value.plantSchedColorMode = clamp_u8(value.plantSchedColorMode, 1, 3);
    
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
    value.crc32 = config_crc(value);
}

static void gui_app_save_settings() {
    sanitize_config(cfg);
    if (!prefs.begin("aquarium", false)) {
        Serial.println("GUI: NVS open failed while saving settings.");
        return;
    }
    const size_t written = prefs.putBytes("uiCfg", &cfg, sizeof(cfg));
    prefs.end();
    if (written != sizeof(cfg)) {
        Serial.printf("GUI: NVS write failed, written=%u expected=%u.\n",
                      static_cast<unsigned>(written),
                      static_cast<unsigned>(sizeof(cfg)));
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
    load_default_config(cfg);
    if (!prefs.begin("aquarium", false)) {
        Serial.println("GUI: NVS open failed, defaults loaded.");
        return;
    }

    // Increment boot count
    boot_count_val = prefs.getUInt("bootCount", 0);
    boot_count_val++;
    prefs.putUInt("bootCount", boot_count_val);

    bool loaded = false;
    if (prefs.isKey("uiCfg")) {
        AquariumUiConfig stored = {};
        const size_t bytes = prefs.getBytes("uiCfg", &stored, sizeof(stored));
        if (bytes == sizeof(stored) &&
            stored.magic == UI_CONFIG_MAGIC &&
            stored.version == UI_CONFIG_VERSION &&
            stored.crc32 == config_crc(stored)) {
            cfg = stored;
            loaded = true;
        }
    }
    prefs.end();

    sanitize_config(cfg);
    if (!loaded) {
        gui_app_save_settings();
        Serial.println("GUI: NVS defaults initialized.");
    } else {
        Serial.println("GUI: NVS settings loaded.");
    }
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
        return "Every 2 days";
    case 2:
        return "Every 3 days";
    case 3:
        return "Weekly";
    case 4:
        return "Manual only";
    default:
        return "Daily";
    }
}

static const char *light_color_mode_label(uint8_t mode) {
    switch (mode) {
    case 1:
        return "MIX";
    case 2:
        return "ROSL";
    default:
        return "NORM";
    }
}

static uint16_t to_minutes(uint8_t hour, uint8_t minute) {
    return static_cast<uint16_t>(hour) * 60U + static_cast<uint16_t>(minute);
}

static bool is_within_window(uint16_t now, uint8_t startHour, uint8_t startMinute,
                             uint8_t endHour, uint8_t endMinute) {
    const uint16_t start = to_minutes(startHour, startMinute);
    const uint16_t end = to_minutes(endHour, endMinute);
    if (start == end) {
        return false;
    }
    if (start < end) {
        return now >= start && now < end;
    }
    return now >= start || now < end;
}

static bool schedule_active(uint8_t mode, uint16_t now, uint8_t startHour,
                            uint8_t startMinute, uint8_t endHour,
                            uint8_t endMinute) {
    if (mode == static_cast<uint8_t>(ScheduleMode::AlwaysOn)) {
        return true;
    }
    if (mode == static_cast<uint8_t>(ScheduleMode::AlwaysOff)) {
        return false;
    }
    return is_within_window(now, startHour, startMinute, endHour, endMinute);
}

static bool is_quiet_hours() {
    if (!cfg.quietHoursEnabled) {
        return false;
    }
    uint16_t current_min = static_cast<uint16_t>(clock_hour) * 60U + static_cast<uint16_t>(clock_minute);
    return is_within_window(current_min, cfg.quietStartHour, cfg.quietStartMinute, cfg.quietEndHour, cfg.quietEndMinute);
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
    delay(durationMs);
    speaker_ledc_write(0);
}

static void play_system_sound(SoundType type) {
    if (!cfg.soundEnabled) return;
    if (is_quiet_hours()) return;

    switch (type) {
    case SoundType::Click:
        play_beep(3800, 8, 2); // Tiny, high-pitched mechanical click (2% volume)
        break;
    case SoundType::Save:
        play_beep(2800, 20, 3);
        delay(15);
        play_beep(3500, 35, 3);
        break;
    case SoundType::Warning:
        play_beep(1200, 80, 2);
        break;
    }
}

static void play_mario_tune() {
    uint16_t E5 = 659;
    uint16_t C5 = 523;
    uint16_t G5 = 784;
    uint16_t G4 = 392;

    play_beep(E5, 100, 3);
    delay(50);
    play_beep(E5, 100, 3);
    delay(100);
    play_beep(E5, 100, 3);
    delay(100);
    play_beep(C5, 100, 3);
    delay(50);
    play_beep(E5, 100, 3);
    delay(150);
    play_beep(G5, 100, 3);
    delay(300);
    play_beep(G4, 100, 3);
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

    while (*p && musicPlaying) {
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
            while (elapsed < note_duration && musicPlaying) {
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

            if (freq > 0 && musicPlaying) {
                uint32_t duty = ((musicVolume * 10) * 128) / 100;
                if (duty == 0) duty = 1;

                speaker_ledc_write_tone(freq);
                speaker_ledc_write(duty);

                uint32_t sound_duration = note_duration * 9 / 10;
                uint32_t gap_duration = note_duration - sound_duration;

                uint32_t elapsed = 0;
                while (elapsed < sound_duration && musicPlaying) {
                    uint32_t sleep_time = (sound_duration - elapsed > 10) ? 10 : (sound_duration - elapsed);
                    vTaskDelay(pdMS_TO_TICKS(sleep_time));
                    elapsed += sleep_time;
                }

                speaker_ledc_write(0);

                elapsed = 0;
                while (elapsed < gap_duration && musicPlaying) {
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
        if (musicPlaying) {
            int idx = selectedSongIndex;
            if (idx >= 0 && idx < 3) {
                play_rtttl(RTTTL_SONGS[idx]);
            }
            musicPlaying = false;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
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

static lv_obj_t *create_button(lv_obj_t *parent, const char *text, lv_coord_t w,
                               lv_coord_t h, lv_color_t bg,
                               lv_event_cb_t cb, void *userData) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, resolve_bg_color(bg), 0);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *label = create_label(btn, text, lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    if (cb != nullptr) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, userData);
    }
    return btn;
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
    char masked[5] = "";
    const size_t len = strlen(pin_entry);
    for (size_t i = 0; i < len && i < 4; ++i) {
        masked[i] = '*';
    }
    masked[len < 4 ? len : 4] = '\0';
    lv_label_set_text(pin_value_lbl, masked[0] != '\0' ? masked : "----");
}

static void pin_matrix_cb(lv_event_t *e);
static void build_pin_guard_modal();

static void pin_set_status(const char *text, lv_color_t color) {
    if (pin_status_lbl == nullptr) {
        return;
    }
    lv_obj_set_style_text_color(pin_status_lbl, color, 0);
    lv_label_set_text(pin_status_lbl, text != nullptr ? text : "");
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
        return;
    }
    if (!lv_obj_is_valid(pin_overlay)) {
        pin_overlay = nullptr;
        pin_value_lbl = nullptr;
        pin_status_lbl = nullptr;
        pin_matrix = nullptr;
        pin_entry[0] = '\0';
        return;
    }

    // Keep the modal alive and hidden instead of deleting it. Reallocating the
    // keypad tree later is what caused the fragmented-heap failures in practice.
    pin_entry[0] = '\0';
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

    lv_obj_t *title = create_label(pin_overlay, "PIN Guard", theme_text_main(), &lv_font_montserrat_14);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    pin_value_lbl = create_label(pin_overlay, "----", lv_color_make(6, 182, 212), &lv_font_montserrat_24);
    lv_obj_align(pin_value_lbl, LV_ALIGN_TOP_MID, 0, 28);

    pin_status_lbl = create_label(pin_overlay, "PIN: OK potwierdza, DEL cofa/anuluje.", theme_text_muted(), &lv_font_montserrat_12);
    lv_obj_set_width(pin_status_lbl, 300);
    lv_label_set_long_mode(pin_status_lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(pin_status_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(pin_status_lbl, LV_ALIGN_TOP_MID, 0, 62);

    // Three rows keep touch targets about twice as tall as the old 5-row
    // keypad while preserving all digits. DEL on an empty PIN acts as cancel.
    static const char *pin_map[] = {
        "1", "2", "3", PIN_KEY_BACK,
        "\n",
        "4", "5", "6", PIN_KEY_OK,
        "\n",
        "7", "8", "9", "0",
        ""
    };

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
    lv_obj_set_style_text_color(pin_matrix, lv_color_white(), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(pin_matrix, lv_color_make(35, 41, 55), LV_PART_ITEMS);
    const lv_style_selector_t pin_matrix_checked_selector =
        static_cast<lv_style_selector_t>(static_cast<uint32_t>(LV_PART_ITEMS) | static_cast<uint32_t>(LV_STATE_CHECKED));
    lv_obj_set_style_bg_color(pin_matrix, lv_color_make(16, 185, 129), pin_matrix_checked_selector);
    lv_obj_set_style_border_width(pin_matrix, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(pin_matrix, lv_color_make(55, 65, 81), LV_PART_ITEMS);
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

    play_system_sound(SoundType::Click);

    if (strcmp(key, PIN_KEY_BACK) == 0) {
        const size_t len = strlen(pin_entry);
        if (len > 0) {
            pin_entry[len - 1] = '\0';
            pin_update_label();
            pin_set_status("PIN: OK potwierdza, DEL cofa/anuluje.", theme_text_muted());
        } else {
            pending_pin_action = {PinAction::None, 0, false};
            close_pin_overlay();
        }
        return;
    }

    if (strcmp(key, PIN_KEY_OK) == 0) {
        if (strcmp(pin_entry, Secrets::DEFAULT_PIN) == 0) {
            pin_authenticated = true;
            pin_auth_until_ms = millis() + PIN_AUTH_WINDOW_MS;
            PendingPinAction action = pending_pin_action;
            pending_pin_action = {PinAction::None, 0, false};
            close_pin_overlay();
            execute_pin_action(action);
        } else {
            pin_entry[0] = '\0';
            pin_update_label();
            pin_set_status("Bledny PIN", lv_color_make(239, 68, 68));
            Serial.println("UI_PIN: invalid PIN");
            play_system_sound(SoundType::Warning);
        }
        return;
    }

    if (key[0] >= '0' && key[0] <= '9' && key[1] == '\0') {
        const size_t len = strlen(pin_entry);
        if (len < 4) {
            pin_entry[len] = key[0];
            pin_entry[len + 1] = '\0';
        }
        pin_update_label();
        pin_set_status("PIN: OK potwierdza, DEL cofa/anuluje.", theme_text_muted());
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
        pin_update_label();
        pin_set_status("PIN: OK potwierdza, DEL cofa/anuluje.", theme_text_muted());
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
    pin_update_label();
    pin_set_status("PIN: OK potwierdza, DEL cofa/anuluje.", theme_text_muted());
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
static void show_feeder_modal(const char *line1, const char *line2) {
    if (modal_feeder == nullptr) {
        return;
    }
    lv_obj_clear_flag(modal_feeder, LV_OBJ_FLAG_HIDDEN);
    if (modal_feeder_title_lbl != nullptr) {
        lv_label_set_text(modal_feeder_title_lbl, line1 != nullptr ? line1 : "Feeding");
    }
    if (modal_feeder_msg_lbl != nullptr) {
        lv_label_set_text(modal_feeder_msg_lbl, line2 != nullptr ? line2 : "Motor active");
    }
}

static void close_feeder_modal_cb(lv_timer_t *timer) {
    if (modal_feeder != nullptr) {
        lv_obj_add_flag(modal_feeder, LV_OBJ_FLAG_HIDDEN);
    }
    if (timer != nullptr) {
        lv_timer_del(timer);
    }
}

static void feed_now_event_handler(lv_event_t *e) {
    LV_UNUSED(e);
    play_system_sound(SoundType::Save);
    runtime.lastAutoFeedMs = millis();
    show_feeder_modal("Feeding", "Manual dose requested");
    lv_timer_create(close_feeder_modal_cb, 3000, nullptr);
    Serial.println("GUI: Manual feeding requested.");
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
    mode = static_cast<uint8_t>((mode + 1U) % 3U);
}

static void cycle_heater_mode(lv_event_t *e) {
    LV_UNUSED(e);
    if (cfg.heaterMode == static_cast<uint8_t>(HeaterMode::Threshold)) {
        cfg.heaterMode = static_cast<uint8_t>(HeaterMode::Off);
        runtime.heaterOn = false;
    } else {
        cfg.heaterMode = static_cast<uint8_t>(HeaterMode::Threshold);
    }
    gui_sync_widgets_to_state();
    gui_app_save_settings();
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
            case 1: return "MIX";
            case 2: return "WHITE";
            case 3: return "PLANT";
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
        if (val == 1) return lv_color_make(16, 185, 129); // MIX - Green
        if (val == 2) return lv_color_white();
        if (val == 3) return lv_color_make(239, 68, 68); // PLANT - Red
    }
    return lv_color_make(16, 185, 129); // ON - Green
}

static void update_editor_fields() {
    if (editor_title_lbl != nullptr) {
        switch (current_editor_device) {
        case ScheduleDevice::Light:
            lv_label_set_text(editor_title_lbl, "FrontLight");
            break;
        case ScheduleDevice::PlantLight:
            lv_label_set_text(editor_title_lbl, "RearLight");
            break;
        case ScheduleDevice::Filter:
            lv_label_set_text(editor_title_lbl, "Water filter");
            break;
        case ScheduleDevice::Air:
            lv_label_set_text(editor_title_lbl, "Air pump");
            break;
        case ScheduleDevice::Feed:
            lv_label_set_text(editor_title_lbl, "Feeding");
            break;
        case ScheduleDevice::QuietHours:
            lv_label_set_text(editor_title_lbl, "Quiet Hours");
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
    current_subpage = ActiveSubpage::None;
    if (is_sched_changed()) {
        sanitize_config(cfg);
        gui_app_save_settings();
        gui_sync_widgets_to_state();
        show_top_notification("Changes saved", true);
    }
    if (subpage_sched_editor != nullptr) {
        lv_obj_add_flag(subpage_sched_editor, LV_OBJ_FLAG_HIDDEN);
    }
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
    current_subpage = ActiveSubpage::None;
    if (is_feed_changed()) {
        sanitize_config(cfg);
        gui_app_save_settings();
        gui_sync_widgets_to_state();
        show_top_notification("Changes saved", true);
    }
    if (subpage_feed_editor != nullptr) {
        lv_obj_add_flag(subpage_feed_editor, LV_OBJ_FLAG_HIDDEN);
    }
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
    current_subpage = ActiveSubpage::None;
    if (is_heater_changed()) {
        sanitize_config(cfg);
        gui_app_save_settings();
        gui_sync_widgets_to_state();
        show_top_notification("Changes saved", true);
    }
    if (subpage_heater != nullptr) {
        lv_obj_add_flag(subpage_heater, LV_OBJ_FLAG_HIDDEN);
    }
}

static void capture_ph_snapshot() {
    ph_snapshot.showPh = cfg.showPhSensor;
}

static bool is_ph_changed() {
    return (ph_snapshot.showPh != cfg.showPhSensor);
}

static void back_ph_cb(lv_event_t *e) {
    LV_UNUSED(e);
    current_subpage = ActiveSubpage::None;
    if (is_ph_changed()) {
        sanitize_config(cfg);
        gui_app_save_settings();
        rebuild_gui_tree_for_theme();
        show_top_notification("Changes saved", true);
    } else {
        if (subpage_ph != nullptr) {
            lv_obj_add_flag(subpage_ph, LV_OBJ_FLAG_HIDDEN);
        }
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

static void back_clock_cb(lv_event_t *e) {
    LV_UNUSED(e);
    current_subpage = ActiveSubpage::None;
    if (is_clock_changed()) {
        show_top_notification("Changes saved", true);
    }
    if (subpage_clock != nullptr) {
        lv_obj_add_flag(subpage_clock, LV_OBJ_FLAG_HIDDEN);
    }
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
        if (subpage_feed_editor != nullptr) {
            current_subpage = ActiveSubpage::FeedEditor;
            lv_obj_clear_flag(subpage_feed_editor, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        capture_sched_snapshot();
        if (subpage_sched_editor != nullptr) {
            current_subpage = ActiveSubpage::SchedEditor;
            lv_obj_clear_flag(subpage_sched_editor, LV_OBJ_FLAG_HIDDEN);
        }
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
    int mode = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e))) + 1; // 1 = MIX, 2 = WHITE, 3 = PLANT
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

static void hide_runtime_subpages() {
    auto hide_subpage = [](lv_obj_t *subpage) {
        if (subpage != nullptr) {
            lv_obj_add_flag(subpage, LV_OBJ_FLAG_HIDDEN);
        }
    };

    hide_subpage(subpage_wifi);
    hide_subpage(subpage_clock);
    hide_subpage(subpage_diagnostics);
    hide_subpage(subpage_power);
    hide_subpage(subpage_screen);
    hide_subpage(subpage_logs);
    hide_subpage(subpage_sounds);
    hide_subpage(subpage_hardware);
    hide_subpage(subpage_co2);
    hide_subpage(subpage_ec);
    hide_subpage(subpage_water);
    hide_subpage(subpage_leak);
    hide_subpage(subpage_flow);
    hide_subpage(subpage_feed_editor);
    hide_subpage(subpage_sched_editor);
    hide_subpage(subpage_heater);
    hide_subpage(subpage_ph);
    hide_subpage(subpage_service);
}

static void open_system_subpage(lv_event_t *e) {
    const ActiveSubpage target = static_cast<ActiveSubpage>(
        reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    log_subpage_enter_request(target, "system_hub");
    play_system_sound(SoundType::Click);

    hide_runtime_subpages();

    lv_obj_t *target_subpage = nullptr;
    switch (target) {
    case ActiveSubpage::Wifi:
        target_subpage = subpage_wifi;
        break;
    case ActiveSubpage::Screen:
        target_subpage = subpage_screen;
        break;
    case ActiveSubpage::Logs:
        target_subpage = subpage_logs;
        break;
    case ActiveSubpage::Clock:
        capture_clock_snapshot();
        target_subpage = subpage_clock;
        break;
    case ActiveSubpage::Diagnostics:
        target_subpage = subpage_diagnostics;
        break;
    case ActiveSubpage::Power:
        target_subpage = subpage_power;
        break;
    case ActiveSubpage::Sounds:
        target_subpage = subpage_sounds;
        break;
    case ActiveSubpage::Hardware:
        if (subpage_hardware == nullptr) {
            if (!ensure_runtime_ui_heap("Hardware", UI_RUNTIME_HARDWARE_MIN_FREE, UI_RUNTIME_HARDWARE_MIN_LARGEST)) {
                return;
            }
            build_hardware_subpage();
        }
        target_subpage = subpage_hardware;
        break;
    case ActiveSubpage::Co2:
        if (subpage_co2 == nullptr) {
            if (!ensure_runtime_ui_heap("CO2", UI_RUNTIME_HARDWARE_MIN_FREE, UI_RUNTIME_HARDWARE_MIN_LARGEST)) {
                return;
            }
            build_co2_subpage();
        }
        target_subpage = subpage_co2;
        break;
    case ActiveSubpage::Ec:
        if (subpage_ec == nullptr) {
            if (!ensure_runtime_ui_heap("EC", UI_RUNTIME_HARDWARE_MIN_FREE, UI_RUNTIME_HARDWARE_MIN_LARGEST)) {
                return;
            }
            build_ec_subpage();
        }
        target_subpage = subpage_ec;
        break;
    default:
        break;
    }

    if (target_subpage != nullptr) {
        current_subpage = target;
        lv_obj_clear_flag(target_subpage, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(target_subpage);
    }
}

static void nav_btn_event_handler(lv_event_t *e) {
    const int index = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    log_tab_enter_request(index);
    play_system_sound(SoundType::Click);
    hide_runtime_subpages();
    current_subpage = ActiveSubpage::None;
    current_page_index = index;
    for (uint8_t i = 0; i < PAGE_COUNT; ++i) {
        if (pages[i] == nullptr || nav_btns[i] == nullptr) {
            continue;
        }
        if (i == index) {
            lv_obj_clear_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_state(nav_btns[i], LV_STATE_CHECKED);
        } else {
            lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_state(nav_btns[i], LV_STATE_CHECKED);
        }
    }
    sync_nav_bar_visuals();
}

static void btn_restart_event_handler(lv_event_t *e) {
    LV_UNUSED(e);
    pin_guard_execute_or_prompt(PinAction::Restart, 0, false);
}

static void restart_authorized() {
    play_system_sound(SoundType::Warning);
    Serial.println("System: Device restart requested.");
    delay(300);
    ESP.restart();
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

static void light_sleep_authorized() {
    play_system_sound(SoundType::Warning);
    Serial.println("System: Light Sleep requested for 10s.");
    add_gui_log("Uruchamianie Light Sleep (10s)", true);
    delay(200); // Allow logs and sounds to play out
    
    // Turn off LCD Backlight (GPIO 21)
    digitalWrite(21, LOW);
    
    // Configure timer wakeup for 10s
    esp_sleep_enable_timer_wakeup(10ULL * 1000000ULL);
    
    // Start light sleep
    esp_light_sleep_start();
    
    // --- Device is now asleep, and execution resumes here on wake-up ---
    
    // Turn LCD Backlight back on
    digitalWrite(21, HIGH);
    
    Serial.println("System: Woke up from Light Sleep.");
    add_gui_log("Obudzono z Light Sleep", false);
}

static void btn_deep_sleep_handler(lv_event_t *e) {
    LV_UNUSED(e);
    pin_guard_execute_or_prompt(PinAction::DeepSleep, 0, false);
}

static void deep_sleep_authorized() {
    play_system_sound(SoundType::Warning);
    Serial.println("System: Deep Sleep requested for 30s.");
    add_gui_log("Uruchamianie Deep Sleep (30s)", true);
    delay(200);
    
    // Turn off LCD Backlight
    digitalWrite(21, LOW);
    
    // Configure timer wakeup for 30s
    esp_sleep_enable_timer_wakeup(30ULL * 1000000ULL);
    
    // Start deep sleep
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
    delay(200);
    
    // Turn off LCD Backlight
    digitalWrite(21, LOW);
    
    // Configure timer wakeup for 30s
    esp_sleep_enable_timer_wakeup(30ULL * 1000000ULL);
    
    // Power down all RTC memory and peripherals for true Hibernation
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_OFF);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);
    
    // Start deep sleep (now behaving as Hibernation)
    esp_deep_sleep_start();
}

static void btn_factory_reset_handler(lv_event_t *e) {
    LV_UNUSED(e);
    pin_guard_execute_or_prompt(PinAction::FactoryReset, 0, false);
}

static void factory_reset_authorized() {
    play_system_sound(SoundType::Warning);
    if (prefs.begin("aquarium", false)) {
        prefs.clear();
        prefs.end();
    }
    load_default_config(cfg);
    gui_app_save_settings();
    WiFi.disconnect(true, true);
    if (power_warning_lbl_global != nullptr) {
        lv_label_set_text(power_warning_lbl_global, "Ustawienia usuniete. Restart...");
        lv_obj_set_style_text_color(power_warning_lbl_global, lv_color_make(16, 185, 129), 0);
    }
    lv_timer_create(factory_reset_timer_cb, 1500, nullptr);
}

static void wifi_check_timer_cb(lv_timer_t *timer) {
    LV_UNUSED(timer);

    if (cfg.modemSleep) {
        if (WiFi.getMode() != WIFI_OFF) {
            WiFi.disconnect(true);
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
                        lv_obj_add_event_cb(list_btn, btn_sta_handler, LV_EVENT_CLICKED, nullptr);
                    } else {
                        for (int i = 0; i < n; i++) {
                            String ssid = WiFi.SSID(i);
                            int32_t rssi = WiFi.RSSI(i);
                            char item_text[96];
                            snprintf(item_text, sizeof(item_text), "%s (%d dBm)", ssid.c_str(), rssi);

                            lv_obj_t *list_btn = lv_list_add_btn(sta_list_obj, LV_SYMBOL_WIFI, item_text);
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
            String ip_str = WiFi.localIP().toString();

            gui_app_update_wifi(1, wifi_rssi);

            if (wifi_status_message_lbl != nullptr) {
                lv_label_set_text(wifi_status_message_lbl, "Status: Polaczono");
                lv_obj_set_style_text_color(wifi_status_message_lbl, lv_color_make(16, 185, 129), 0);
            }
            if (wifi_ssid_lbl != nullptr) {
                lv_label_set_text_fmt(wifi_ssid_lbl, "SSID: %s", WiFi.SSID().c_str());
            }
            if (wifi_ip_lbl != nullptr) {
                lv_label_set_text_fmt(wifi_ip_lbl, "IP: %s", ip_str.c_str());
            }
            if (btn_disconnect != nullptr) {
                lv_obj_clear_flag(btn_disconnect, LV_OBJ_FLAG_HIDDEN);
            }
            if (btn_sta != nullptr) {
                lv_obj_add_flag(btn_sta, LV_OBJ_FLAG_HIDDEN);
            }
            if (btn_ota != nullptr) {
                lv_obj_clear_flag(btn_ota, LV_OBJ_FLAG_HIDDEN);
            }
        } else if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL || (millis() - conn_start_ms > 15000)) {
            is_connecting = false;
            wifi_connected = false;
            wifi_rssi = 0;
            WiFi.disconnect(true);

            gui_app_update_wifi(0, 0);

            if (wifi_status_message_lbl != nullptr) {
                if (status == WL_CONNECT_FAILED) {
                    lv_label_set_text(wifi_status_message_lbl, "Status: Bledne haslo");
                } else if (status == WL_NO_SSID_AVAIL) {
                    lv_label_set_text(wifi_status_message_lbl, "Status: Siec niedostepna");
                } else {
                    lv_label_set_text(wifi_status_message_lbl, "Status: Przekroczono limit czasu");
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
    } else if (wifi_connected && !wifi_ota_active) {
        // Periodically monitor connection stability
        if (WiFi.status() != WL_CONNECTED) {
            wifi_connected = false;
            wifi_rssi = 0;
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
                if (wifi_status_message_lbl != nullptr) {
                    lv_label_set_text_fmt(wifi_status_message_lbl, "Status: Polaczono (%d dBm)", wifi_rssi);
                }
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

    gui_app_update_wifi(0, 0);

    if (wifi_status_message_lbl != nullptr) {
        lv_label_set_text(wifi_status_message_lbl, "Status: Rozlaczono");
        lv_obj_set_style_text_color(wifi_status_message_lbl, lv_color_make(239, 68, 68), 0);
    }
    if (wifi_ssid_lbl != nullptr) {
        lv_label_set_text(wifi_ssid_lbl, "SSID: Rozlaczono");
    }
    if (wifi_ip_lbl != nullptr) {
        lv_label_set_text(wifi_ip_lbl, "IP: 0.0.0.0");
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

static void btn_sta_handler(lv_event_t *e) {
    LV_UNUSED(e);
    play_system_sound(SoundType::Click);

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
        lv_list_add_btn(sta_list_obj, LV_SYMBOL_LOOP, "Skanowanie sieci...");
    }

    WiFi.disconnect(); // Disconnect now to ensure STA interface is free
    WiFi.mode(WIFI_STA);
    is_scanning = true;
    scan_started = false;
    scan_start_ms = millis();
}

static void btn_ota_handler(lv_event_t *e) {
    LV_UNUSED(e);
    pin_guard_execute_or_prompt(PinAction::StartOta, 0, false);
}

static void start_ota_authorized() {
    play_system_sound(SoundType::Warning);
    WiFi.disconnect();
    WiFi.mode(WIFI_AP); // Solely WIFI_AP for maximum AP and connection stability
    delay(100);
    bool ap_ok = WiFi.softAP("cydAquarium-OTA", Secrets::OTA_PASSWORD);
    delay(100);
    Serial.printf("OTA: SoftAP start: %s, SSID: %s, IP: %s\n", 
                  ap_ok ? "OK" : "FAILED", 
                  WiFi.softAPSSID().c_str(), 
                  WiFi.softAPIP().toString().c_str());

    ArduinoOTA.setHostname(Secrets::OTA_HOSTNAME);
    ArduinoOTA.setPassword(Secrets::OTA_PASSWORD);
    ArduinoOTA.onStart([]() {
        Serial.println("OTA: Start");
        if (wifi_status_message_lbl != nullptr) {
            lv_label_set_text(wifi_status_message_lbl, "OTA: Trwa flashowanie...");
            lv_obj_set_style_text_color(wifi_status_message_lbl, lv_color_make(6, 182, 212), 0);
        }
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("\nOTA: Gotowe!");
        if (wifi_status_message_lbl != nullptr) {
            lv_label_set_text(wifi_status_message_lbl, "OTA: Gotowe! Restart...");
            lv_obj_set_style_text_color(wifi_status_message_lbl, lv_color_make(16, 185, 129), 0);
        }
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("OTA: Postep: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("OTA Blad[%u]\n", error);
        if (wifi_status_message_lbl != nullptr) {
            lv_label_set_text_fmt(wifi_status_message_lbl, "OTA Blad: %u", error);
            lv_obj_set_style_text_color(wifi_status_message_lbl, lv_color_make(239, 68, 68), 0);
        }
    });

    ArduinoOTA.begin();
    wifi_ota_active = true;

    gui_app_update_wifi(2, 0);

    if (wifi_main_panel != nullptr) lv_obj_add_flag(wifi_main_panel, LV_OBJ_FLAG_HIDDEN);
    if (wifi_ota_panel != nullptr) lv_obj_clear_flag(wifi_ota_panel, LV_OBJ_FLAG_HIDDEN);
}

static void stop_ota_cb(lv_event_t *e) {
    LV_UNUSED(e);
    play_system_sound(SoundType::Click);

    ArduinoOTA.end();
    WiFi.softAPdisconnect(true);
    wifi_ota_active = false;

    if (WiFi.status() == WL_CONNECTED) {
        wifi_connected = true;
        wifi_rssi = WiFi.RSSI();
        WiFi.mode(WIFI_STA);
        gui_app_update_wifi(1, wifi_rssi);
        if (wifi_status_message_lbl != nullptr) {
            lv_label_set_text_fmt(wifi_status_message_lbl, "Status: Polaczono (%d dBm)", wifi_rssi);
            lv_obj_set_style_text_color(wifi_status_message_lbl, lv_color_make(16, 185, 129), 0);
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

static void cancel_sta_cb(lv_event_t *e) {
    LV_UNUSED(e);
    play_system_sound(SoundType::Click);

    is_scanning = false;
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

    if (wifi_sta_panel != nullptr) lv_obj_add_flag(wifi_sta_panel, LV_OBJ_FLAG_HIDDEN);
    if (wifi_pwd_panel != nullptr) lv_obj_clear_flag(wifi_pwd_panel, LV_OBJ_FLAG_HIDDEN);
}

static void cancel_pwd_cb(lv_event_t *e) {
    LV_UNUSED(e);
    play_system_sound(SoundType::Click);
    if (wifi_pwd_panel != nullptr) lv_obj_add_flag(wifi_pwd_panel, LV_OBJ_FLAG_HIDDEN);
    if (wifi_sta_panel != nullptr) lv_obj_clear_flag(wifi_sta_panel, LV_OBJ_FLAG_HIDDEN);
}

static void keyboard_ready_cb(lv_event_t *e) {
    LV_UNUSED(e);
    play_system_sound(SoundType::Click);

    const char *pwd = "";
    if (wifi_pwd_ta != nullptr) {
        pwd = lv_textarea_get_text(wifi_pwd_ta);
    }

    if (wifi_pwd_panel != nullptr) lv_obj_add_flag(wifi_pwd_panel, LV_OBJ_FLAG_HIDDEN);
    if (wifi_main_panel != nullptr) lv_obj_clear_flag(wifi_main_panel, LV_OBJ_FLAG_HIDDEN);

    if (wifi_status_message_lbl != nullptr) {
        lv_label_set_text(wifi_status_message_lbl, "Status: Laczenie...");
        lv_obj_set_style_text_color(wifi_status_message_lbl, lv_color_make(245, 158, 11), 0);
    }

    WiFi.begin(selected_ssid, pwd);
    is_connecting = true;
    conn_start_ms = millis();
}


static void ntp_sync_restore_cb(lv_timer_t *timer) {
    lv_obj_t *btn = timer != nullptr ? static_cast<lv_obj_t *>(timer->user_data) : nullptr;
    if (btn != nullptr) {
        lv_obj_set_style_bg_color(btn, resolve_bg_color(lv_color_make(35, 41, 55)), 0);
    }
    if (btn_sync_ntp_lbl_global != nullptr) {
        lv_label_set_text(btn_sync_ntp_lbl_global, "Sync with NTP");
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
        set_label_text(btn_sync_ntp_lbl_global, "WiFi disconnected");
        lv_timer_create(ntp_sync_restore_cb, 2000, btn);
        return;
    }

    clock_hour = 12;
    clock_minute = 0;
    clock_second = 0;
    clock_day = 2;
    clock_month = 6;
    clock_year = 2026;
    lv_obj_set_style_bg_color(btn, lv_color_make(16, 185, 129), 0);
    set_label_text(btn_sync_ntp_lbl_global, "Time synced");
    lv_timer_create(ntp_sync_restore_cb, 2000, btn);
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
    gui_app_save_settings();
    show_save_toast("Time Settings Saved!");
    if (subpage_clock != nullptr) {
        lv_obj_add_flag(subpage_clock, LV_OBJ_FLAG_HIDDEN);
    }
    Serial.printf("System: Clock manually set to: %02d:%02d:%02d on %02d/%02d/%04d\n",
                  clock_hour, clock_minute, clock_second, clock_day, clock_month, clock_year);
}

static void screen_dev_mode_handler(lv_event_t *e) {
    play_system_sound(SoundType::Click);
    lv_obj_t *obj = lv_event_get_target(e);
    const bool requested = lv_obj_has_state(obj, LV_STATE_CHECKED);
    apply_dev_mode_authorized(requested); // Bezposrednio, bez PIN
}

static void apply_dev_mode_authorized(bool enabled) {
    cfg.devMode = enabled;
    gui_app_save_settings();
    if (cfg.devMode) {
        add_gui_log("Tryb deweloperski wlaczony", false);
    } else {
        add_gui_log("Tryb deweloperski wylaczony", true);
    }
    gui_sync_widgets_to_state();
}

static void screen_always_on_handler(lv_event_t *e) {
    play_system_sound(SoundType::Click);
    lv_obj_t *obj = lv_event_get_target(e);
    cfg.alwaysScreenOn = lv_obj_has_state(obj, LV_STATE_CHECKED);
    gui_app_save_settings();
}

static void screen_ldr_enable_handler(lv_event_t *e) {
    play_system_sound(SoundType::Click);
    lv_obj_t *obj = lv_event_get_target(e);
    cfg.ldrThemeEnabled = lv_obj_has_state(obj, LV_STATE_CHECKED);
    
    if (cfg.ldrThemeEnabled) {
        int ldr_val = analogRead(34);
        int threshold = map(cfg.ldrSensitivity, 0, 100, 200, 0);
        bool should_be_light = (ldr_val < threshold); // Odwrócone: jasno = ciemny ekran
        last_ldr_value = ldr_val;
        
        if (should_be_light != ui_light_theme) {
            ui_light_theme = should_be_light;
            rebuild_gui_tree_for_theme();
        }
    } else {
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

static void screen_ldr_slider_cb(lv_event_t *e) {
    lv_obj_t *slider = lv_event_get_target(e);
    cfg.ldrSensitivity = static_cast<uint8_t>(lv_slider_get_value(slider));
    if (screen_ldr_value_lbl != nullptr) {
        lv_label_set_text_fmt(screen_ldr_value_lbl, "%u%%", static_cast<unsigned>(cfg.ldrSensitivity));
    }
}

static void save_screen_settings_cb(lv_event_t *e) {
    LV_UNUSED(e);
    play_system_sound(SoundType::Click);
    gui_app_save_settings();
    show_save_toast("Screen Settings Saved!");
    Serial.println("GUI: Screen settings saved.");
}

static void add_gui_log(const char *msg, bool is_important) {
    Serial.printf("LOG_GUI [%s]: %s\n", is_important ? "IMPORTANT" : "NORMAL", msg);
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
        lv_obj_set_style_bg_color(btn_log_important, lv_color_make(35, 41, 55), 0);
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
        lv_obj_set_style_bg_color(btn_log_normal, lv_color_make(35, 41, 55), 0);
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
    sanitize_config(cfg);
    gui_app_save_settings();
    show_save_toast("Sound Settings Saved!");
    if (subpage_sounds != nullptr) {
        lv_obj_add_flag(subpage_sounds, LV_OBJ_FLAG_HIDDEN);
    }
    Serial.println("GUI: Sound settings saved.");
}

static void test_speaker_cb(lv_event_t *e) {
    LV_UNUSED(e);
    play_mario_tune();
}

static void back_service_cb(lv_event_t *e) {
    LV_UNUSED(e);
    play_system_sound(SoundType::Click);
    current_subpage = ActiveSubpage::None;
    if (subpage_service != nullptr) {
        lv_obj_add_flag(subpage_service, LV_OBJ_FLAG_HIDDEN);
    }
}

static void service_tile_cb(lv_event_t *e) {
    LV_UNUSED(e);
    log_subpage_enter_request(ActiveSubpage::Service, "service_tile");
    play_system_sound(SoundType::Click);

    // Silence active music
    musicPlaying = false;
    speaker_ledc_write(0);

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
    gui_sync_widgets_to_state();

    // Open Service subpage
    if (subpage_service != nullptr) {
        current_subpage = ActiveSubpage::Service;
        lv_obj_clear_flag(subpage_service, LV_OBJ_FLAG_HIDDEN);
    }
}

static void service_light_sw_cb(lv_event_t *e) {
    play_system_sound(SoundType::Click);
    lv_obj_t *sw = lv_event_get_target(e);
    bool checked = lv_obj_has_state(sw, LV_STATE_CHECKED);
    cfg.lightMode = checked ? static_cast<uint8_t>(ScheduleMode::AlwaysOn) : static_cast<uint8_t>(ScheduleMode::AlwaysOff);
    runtime.lightOn = checked;
    gui_app_save_settings();
    gui_sync_widgets_to_state();
}

static void service_filter_sw_cb(lv_event_t *e) {
    play_system_sound(SoundType::Click);
    lv_obj_t *sw = lv_event_get_target(e);
    bool checked = lv_obj_has_state(sw, LV_STATE_CHECKED);
    cfg.filterMode = checked ? static_cast<uint8_t>(ScheduleMode::AlwaysOn) : static_cast<uint8_t>(ScheduleMode::AlwaysOff);
    runtime.filterOn = checked;
    gui_app_save_settings();
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
        lv_label_set_text_fmt(service_vol_lbl, "Vol: %d0%%", musicVolume);
    }
}

static void service_play_cb(lv_event_t *e) {
    LV_UNUSED(e);
    play_system_sound(SoundType::Click);
    if (musicPlaying) {
        musicPlaying = false;
        delay(50);
    }
    musicPlaying = true;
}

static void service_stop_cb(lv_event_t *e) {
    LV_UNUSED(e);
    play_system_sound(SoundType::Click);
    musicPlaying = false;
    speaker_ledc_write(0);
}

static void open_heater_subpage_cb(lv_event_t *e) {
    LV_UNUSED(e);
    pin_guard_execute_or_prompt(PinAction::OpenHeater, 0, false);
}

static void open_heater_subpage_authorized() {
    log_subpage_enter_request(ActiveSubpage::Heater, "protected_tile");
    play_system_sound(SoundType::Click);
    capture_heater_snapshot();
    if (subpage_heater != nullptr) {
        current_subpage = ActiveSubpage::Heater;
        lv_obj_clear_flag(subpage_heater, LV_OBJ_FLAG_HIDDEN);
    }
}

static void open_ph_subpage_cb(lv_event_t *e) {
    LV_UNUSED(e);
    pin_guard_execute_or_prompt(PinAction::OpenPh, 0, false);
}

static void open_ph_subpage_authorized() {
    log_subpage_enter_request(ActiveSubpage::Ph, "protected_tile");
    play_system_sound(SoundType::Click);
    capture_ph_snapshot();
    if (subpage_ph != nullptr) {
        current_subpage = ActiveSubpage::Ph;
        lv_obj_clear_flag(subpage_ph, LV_OBJ_FLAG_HIDDEN);
    }
}

static void open_hardware_subpage_cb(lv_event_t *e) {
    LV_UNUSED(e);
    log_subpage_enter_request(ActiveSubpage::Hardware, "module_tile");
    play_system_sound(SoundType::Click);
    if (subpage_hardware == nullptr) {
        if (!ensure_runtime_ui_heap("Hardware", UI_RUNTIME_HARDWARE_MIN_FREE, UI_RUNTIME_HARDWARE_MIN_LARGEST)) {
            return;
        }
        build_hardware_subpage();
    }
    if (subpage_hardware != nullptr) {
        current_subpage = ActiveSubpage::Hardware;
        lv_obj_clear_flag(subpage_hardware, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(subpage_hardware);
        Serial.printf("UI_NAV: subpage visible target=Hardware obj=%p hidden=%d\n",
                      static_cast<void *>(subpage_hardware),
                      lv_obj_has_flag(subpage_hardware, LV_OBJ_FLAG_HIDDEN) ? 1 : 0);
    }
}

static void open_co2_subpage_cb(lv_event_t *e) {
    LV_UNUSED(e);
    log_subpage_enter_request(ActiveSubpage::Co2, "module_tile");
    play_system_sound(SoundType::Click);
    if (subpage_co2 == nullptr) {
        if (!ensure_runtime_ui_heap("CO2", UI_RUNTIME_HARDWARE_MIN_FREE, UI_RUNTIME_HARDWARE_MIN_LARGEST)) {
            return;
        }
        build_co2_subpage();
    }
    if (subpage_co2 != nullptr) {
        current_subpage = ActiveSubpage::Co2;
        lv_obj_clear_flag(subpage_co2, LV_OBJ_FLAG_HIDDEN);
    }
}

static void open_ec_subpage_cb(lv_event_t *e) {
    LV_UNUSED(e);
    log_subpage_enter_request(ActiveSubpage::Ec, "module_tile");
    play_system_sound(SoundType::Click);
    if (subpage_ec == nullptr) {
        if (!ensure_runtime_ui_heap("EC", UI_RUNTIME_HARDWARE_MIN_FREE, UI_RUNTIME_HARDWARE_MIN_LARGEST)) {
            return;
        }
        build_ec_subpage();
    }
    if (subpage_ec != nullptr) {
        current_subpage = ActiveSubpage::Ec;
        lv_obj_clear_flag(subpage_ec, LV_OBJ_FLAG_HIDDEN);
    }
}

static void open_water_subpage_cb(lv_event_t *e) {
    LV_UNUSED(e);
    log_subpage_enter_request(ActiveSubpage::WaterLevel, "module_tile");
    play_system_sound(SoundType::Click);
    if (subpage_water == nullptr) {
        if (!ensure_runtime_ui_heap("WaterLevel", UI_RUNTIME_HARDWARE_MIN_FREE, UI_RUNTIME_HARDWARE_MIN_LARGEST)) {
            return;
        }
        build_water_subpage();
    }
    if (subpage_water != nullptr) {
        current_subpage = ActiveSubpage::WaterLevel;
        lv_obj_clear_flag(subpage_water, LV_OBJ_FLAG_HIDDEN);
    }
}

static void open_leak_subpage_cb(lv_event_t *e) {
    LV_UNUSED(e);
    log_subpage_enter_request(ActiveSubpage::Leak, "module_tile");
    play_system_sound(SoundType::Click);
    if (subpage_leak == nullptr) {
        if (!ensure_runtime_ui_heap("Leak", UI_RUNTIME_HARDWARE_MIN_FREE, UI_RUNTIME_HARDWARE_MIN_LARGEST)) {
            return;
        }
        build_leak_subpage();
    }
    if (subpage_leak != nullptr) {
        current_subpage = ActiveSubpage::Leak;
        lv_obj_clear_flag(subpage_leak, LV_OBJ_FLAG_HIDDEN);
    }
}

static void open_flow_subpage_cb(lv_event_t *e) {
    LV_UNUSED(e);
    log_subpage_enter_request(ActiveSubpage::Flow, "module_tile");
    play_system_sound(SoundType::Click);
    if (subpage_flow == nullptr) {
        if (!ensure_runtime_ui_heap("Flow", UI_RUNTIME_HARDWARE_MIN_FREE, UI_RUNTIME_HARDWARE_MIN_LARGEST)) {
            return;
        }
        build_flow_subpage();
    }
    if (subpage_flow != nullptr) {
        current_subpage = ActiveSubpage::Flow;
        lv_obj_clear_flag(subpage_flow, LV_OBJ_FLAG_HIDDEN);
    }
}

static void cycle_chart_range_cb(lv_event_t *e) {
    LV_UNUSED(e);
    chart_range_index = static_cast<uint8_t>((chart_range_index + 1U) % 3U);
    const char *ranges[] = {"1H", "24H", "7D"};
    set_label_text(chart_range_lbl, ranges[chart_range_index]);
}

static lv_obj_t *create_menu_item(lv_obj_t *parent, const char *title,
                                  lv_event_cb_t event_cb, void *userData,
                                  lv_obj_t **title_label = nullptr) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 300, 34);
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
            play_system_sound(SoundType::Click);
            current_subpage = ActiveSubpage::None;
            lv_obj_t *subpage = static_cast<lv_obj_t *>(lv_event_get_user_data(e));
            if (subpage != nullptr) {
                lv_obj_add_flag(subpage, LV_OBJ_FLAG_HIDDEN);
            }
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
    if (index > 0) {
        lv_obj_add_flag(pages[index], LV_OBJ_FLAG_HIDDEN);
    }
}

static void build_status_bar() {
    lv_obj_t *status_bar = lv_obj_create(lv_scr_act());
    lv_obj_set_size(status_bar, 320, 25);
    lv_obj_set_pos(status_bar, 0, 0);
    lv_obj_set_style_pad_all(status_bar, 0, 0);
    style_panel(status_bar, lv_color_make(15, 23, 42), lv_color_make(15, 23, 42), 0);

    lv_obj_t *brand = create_label(status_bar, "CYD", lv_color_make(6, 182, 212), &lv_font_montserrat_12);
    lv_obj_align(brand, LV_ALIGN_LEFT_MID, 6, 0);

    label_date = create_label(status_bar, "31 May 20:30", lv_color_make(226, 232, 240), &lv_font_montserrat_12);
    lv_obj_set_width(label_date, 116);
    lv_label_set_long_mode(label_date, LV_LABEL_LONG_CLIP);
    lv_obj_align(label_date, LV_ALIGN_LEFT_MID, 40, 0);

    label_power_mode = create_label(status_bar, "24.5*C", lv_color_make(6, 182, 212), &lv_font_montserrat_12);
    lv_obj_align(label_power_mode, LV_ALIGN_RIGHT_MID, -88, 0);

    label_wifi_state = create_label(status_bar, "OFF", lv_color_make(148, 163, 184), &lv_font_montserrat_12);
    lv_obj_set_width(label_wifi_state, 30);
    lv_label_set_long_mode(label_wifi_state, LV_LABEL_LONG_CLIP);
    lv_obj_align(label_wifi_state, LV_ALIGN_RIGHT_MID, -52, 0);

    label_rtc_bat = create_label(status_bar, LV_SYMBOL_CHARGE " USB", lv_color_make(16, 185, 129), &lv_font_montserrat_12);
    lv_obj_align(label_rtc_bat, LV_ALIGN_RIGHT_MID, -6, 0);
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
    lv_obj_t *card = create_card(parent, w, 39, x, y);
    lv_obj_set_style_pad_all(card, 4, 0);
    create_accent_bar(card, accent, 22);
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

    home_temp_current = create_label(temp_card, "24.5", theme_text_main(), &lv_font_montserrat_24);
    lv_obj_align(home_temp_current, LV_ALIGN_LEFT_MID, 7, 7);

    lv_obj_t *temp_unit = create_label(temp_card, "*C", lv_color_make(6, 182, 212), &lv_font_montserrat_12);
    lv_obj_align_to(temp_unit, home_temp_current, LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -2);

    home_temp_trend_lbl = create_label(temp_card, "Stabilna", lv_color_make(16, 185, 129), &lv_font_montserrat_12);
    lv_obj_align(home_temp_trend_lbl, LV_ALIGN_BOTTOM_LEFT, 7, 1);

    if (cfg.showPhSensor) {
        lv_obj_t *ph_card = create_home_action_card(pages[0], 160, 4, 74, 40, "pH", "7.20",
                                                    lv_color_make(16, 185, 129), open_ph_subpage_cb,
                                                    nullptr, &home_ph_current);
        lv_obj_t *ph_lbl = lv_obj_get_child(ph_card, 2);
        if (ph_lbl != nullptr) {
            lv_obj_set_style_text_font(ph_lbl, &lv_font_montserrat_14, 0);
        }

        create_home_feed_button(pages[0], 240, 4, 76, 40, "FEED", "18:00",
                                feed_now_event_handler, nullptr, &home_feed_time_lbl);
    } else {
        create_home_feed_button(pages[0], 160, 4, 156, 40, "KARMIENIE", "18:00",
                                feed_now_event_handler, nullptr, &home_feed_time_lbl);
    }

    create_home_action_card(pages[0], 160, 50, 156, 40, "SERWIS", "Otworz tryb",
                            lv_color_make(239, 68, 68), service_tile_cb, nullptr, nullptr);

    const bool show_air = cfg.enableAerator;
    if (show_air) {
        create_home_device_card(pages[0], 4, 96, 100, LV_SYMBOL_IMAGE, "Lampa 1",
                                lv_color_make(14, 165, 233), open_sched_editor_cb,
                                reinterpret_cast<void *>(static_cast<intptr_t>(ScheduleDevice::Light)),
                                &home_light_state_lbl, &home_light_mode_lbl);
        create_home_device_card(pages[0], 110, 96, 100, LV_SYMBOL_IMAGE, "Lampa 2",
                                lv_color_make(34, 197, 94), open_sched_editor_cb,
                                reinterpret_cast<void *>(static_cast<intptr_t>(ScheduleDevice::PlantLight)),
                                &home_plant_state_lbl, &home_plant_mode_lbl);
        create_home_device_card(pages[0], 216, 96, 100, LV_SYMBOL_LOOP, "Filtr",
                                lv_color_make(6, 182, 212), open_sched_editor_cb,
                                reinterpret_cast<void *>(static_cast<intptr_t>(ScheduleDevice::Filter)),
                                &home_filter_state_lbl, &home_filter_mode_lbl);
        create_home_device_card(pages[0], 4, 140, 153, LV_SYMBOL_CHARGE, "Grzalka",
                                lv_color_make(249, 115, 22), open_heater_subpage_cb,
                                nullptr, &home_heater_state_lbl, &home_heater_mode_lbl);
        create_home_device_card(pages[0], 163, 140, 153, LV_SYMBOL_REFRESH, "Powietrze",
                                lv_color_make(168, 85, 247), open_sched_editor_cb,
                                reinterpret_cast<void *>(static_cast<intptr_t>(ScheduleDevice::Air)),
                                &home_air_state_lbl, &home_air_mode_lbl);
    } else {
        create_home_device_card(pages[0], 4, 96, 153, LV_SYMBOL_IMAGE, "Lampa 1",
                                lv_color_make(14, 165, 233), open_sched_editor_cb,
                                reinterpret_cast<void *>(static_cast<intptr_t>(ScheduleDevice::Light)),
                                &home_light_state_lbl, &home_light_mode_lbl);
        create_home_device_card(pages[0], 163, 96, 153, LV_SYMBOL_IMAGE, "Lampa 2",
                                lv_color_make(34, 197, 94), open_sched_editor_cb,
                                reinterpret_cast<void *>(static_cast<intptr_t>(ScheduleDevice::PlantLight)),
                                &home_plant_state_lbl, &home_plant_mode_lbl);
        create_home_device_card(pages[0], 4, 140, 153, LV_SYMBOL_LOOP, "Filtr",
                                lv_color_make(6, 182, 212), open_sched_editor_cb,
                                reinterpret_cast<void *>(static_cast<intptr_t>(ScheduleDevice::Filter)),
                                &home_filter_state_lbl, &home_filter_mode_lbl);
        create_home_device_card(pages[0], 163, 140, 153, LV_SYMBOL_CHARGE, "Grzalka",
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

    tile_light = create_tile_3d(pages[1], LV_SYMBOL_IMAGE, "Lampa 1", open_sched_editor_cb, reinterpret_cast<void *>(static_cast<intptr_t>(ScheduleDevice::Light)));
    sched_light_lbl = lv_obj_get_child(tile_light, 2);

    tile_plant = create_tile_3d(pages[1], LV_SYMBOL_IMAGE, "Lampa 2", open_sched_editor_cb, reinterpret_cast<void *>(static_cast<intptr_t>(ScheduleDevice::PlantLight)));
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

static void chart_draw_event_cb(lv_event_t *e) {
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
    if (dsc->part == LV_PART_ITEMS) {
        if (dsc->sub_part_ptr == chart_temp_series) {
            dsc->line_dsc->color = lv_color_make(6, 182, 212); // Cyan
            dsc->line_dsc->width = 2;
            if (dsc->rect_dsc != nullptr) {
                dsc->rect_dsc->bg_color = lv_color_make(6, 182, 212);
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

    // Przycisk zakresu czasu
    lv_obj_t *range_btn = create_button(panel, "1H", 44, 22, lv_color_make(35, 41, 55), cycle_chart_range_cb, nullptr);
    lv_obj_align(range_btn, LV_ALIGN_TOP_RIGHT, 0, -2);
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
    chart_ph = lv_chart_create(panel);
    lv_obj_set_size(chart_ph, 300, 92);
    lv_obj_align(chart_ph, LV_ALIGN_TOP_MID, 0, 42);
    lv_obj_set_style_bg_color(chart_ph, resolve_bg_color(lv_color_make(8, 13, 24)), 0);
    lv_obj_set_style_border_color(chart_ph, ui_light_theme ? theme_card_border() : lv_color_make(35, 41, 55), 0);
    lv_obj_set_style_border_width(chart_ph, 1, 0);
    lv_obj_set_style_radius(chart_ph, 4, 0);
    lv_obj_set_style_line_width(chart_ph, 2, LV_PART_ITEMS);
    lv_chart_set_type(chart_ph, LV_CHART_TYPE_LINE);
    lv_chart_set_update_mode(chart_ph, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_point_count(chart_ph, TEMP_HISTORY_POINTS);
    lv_chart_set_range(chart_ph, LV_CHART_AXIS_PRIMARY_Y, 600, 800);
    lv_chart_set_div_line_count(chart_ph, 4, 6);
    
    chart_ph_series = lv_chart_add_series(chart_ph, lv_color_make(168, 85, 247), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(chart_ph, chart_ph_series, LV_CHART_POINT_NONE);
    lv_obj_add_flag(chart_ph, LV_OBJ_FLAG_HIDDEN);

    // 3. Wykres LDR (Jasności)
    chart_ldr = lv_chart_create(panel);
    lv_obj_set_size(chart_ldr, 300, 92);
    lv_obj_align(chart_ldr, LV_ALIGN_TOP_MID, 0, 42);
    lv_obj_set_style_bg_color(chart_ldr, resolve_bg_color(lv_color_make(8, 13, 24)), 0);
    lv_obj_set_style_border_color(chart_ldr, ui_light_theme ? theme_card_border() : lv_color_make(35, 41, 55), 0);
    lv_obj_set_style_border_width(chart_ldr, 1, 0);
    lv_obj_set_style_radius(chart_ldr, 4, 0);
    lv_obj_set_style_line_width(chart_ldr, 2, LV_PART_ITEMS);
    lv_chart_set_type(chart_ldr, LV_CHART_TYPE_LINE);
    lv_chart_set_update_mode(chart_ldr, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_point_count(chart_ldr, TEMP_HISTORY_POINTS);
    lv_chart_set_range(chart_ldr, LV_CHART_AXIS_PRIMARY_Y, 0, 4095);
    lv_chart_set_div_line_count(chart_ldr, 4, 6);
    
    chart_ldr_series = lv_chart_add_series(chart_ldr, lv_color_make(234, 179, 8), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(chart_ldr, chart_ldr_series, LV_CHART_POINT_NONE);
    lv_obj_add_flag(chart_ldr, LV_OBJ_FLAG_HIDDEN);

    // 4. Wykres HEAP (Pamięci)
    chart_heap = lv_chart_create(panel);
    lv_obj_set_size(chart_heap, 300, 92);
    lv_obj_align(chart_heap, LV_ALIGN_TOP_MID, 0, 42);
    lv_obj_set_style_bg_color(chart_heap, resolve_bg_color(lv_color_make(8, 13, 24)), 0);
    lv_obj_set_style_border_color(chart_heap, ui_light_theme ? theme_card_border() : lv_color_make(35, 41, 55), 0);
    lv_obj_set_style_border_width(chart_heap, 1, 0);
    lv_obj_set_style_radius(chart_heap, 4, 0);
    lv_obj_set_style_line_width(chart_heap, 2, LV_PART_ITEMS);
    lv_chart_set_type(chart_heap, LV_CHART_TYPE_LINE);
    lv_chart_set_update_mode(chart_heap, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_point_count(chart_heap, TEMP_HISTORY_POINTS);
    lv_chart_set_range(chart_heap, LV_CHART_AXIS_PRIMARY_Y, 0, 300);
    lv_chart_set_div_line_count(chart_heap, 4, 6);
    
    chart_heap_series = lv_chart_add_series(chart_heap, lv_color_make(14, 165, 233), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(chart_heap, chart_heap_series, LV_CHART_POINT_NONE);
    lv_obj_add_flag(chart_heap, LV_OBJ_FLAG_HIDDEN);

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
        LV_SYMBOL_HOME,       // Page 0: Home Dashboard
        LV_SYMBOL_LOOP,       // Page 1: Schedules (2-ga opcja to harmonogramy)
        LV_SYMBOL_PLUS,       // Page 2: Optional Items (3-cia zakładka)
        LV_SYMBOL_IMAGE,      // Page 3: Charts / History
        LV_SYMBOL_SETTINGS    // Page 4: System Settings (WiFi, LCD, Logs, Clock, Sounds)
    };

    const char *captions[PAGE_COUNT] = {
        "Start",
        "Plan",
        "Mod",
        "Hist",
        "Sys"
    };

    for (uint8_t i = 0; i < PAGE_COUNT; ++i) {
        const lv_coord_t btn_w = 64;
        nav_btns[i] = lv_btn_create(nav);
        lv_obj_set_size(nav_btns[i], 60, 31);
        lv_obj_set_pos(nav_btns[i], static_cast<lv_coord_t>(2 + i * btn_w), 2);
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
        break;
    case HardwareToggle::Aerator:
        cfg.enableAerator = enabled;
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
            lv_label_set_text(data->step_lbl, "Krok 2/2: pH 6.86");
            lv_label_set_text(data->desc_lbl, "Umiesc sonde w buforze 6.86.\nPoczekaj na odczyt i kliknij Zapisz.");
            update_calibration_value_label();
            lv_label_set_text(lv_obj_get_child(data->btn_next, 0), "Zapisz");
        } else if (data->step == 3) {
            gui_app_save_settings();
            close_calib_wizard(data);
        }
    } else { // EC
        if (data->step == 1) {
            lv_label_set_text(data->step_lbl, "Krok 1/1: EC 1413");
            lv_label_set_text(data->desc_lbl, "Umiesc sonde w plynie 1413.\nPoczekaj na odczyt i kliknij Zapisz.");
            update_calibration_value_label();
            lv_label_set_text(lv_obj_get_child(data->btn_next, 0), "Zapisz");
        } else if (data->step == 2) {
            gui_app_save_settings();
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

    CalibWizardData *data = new CalibWizardData();
    data->bg_overlay = bg_overlay;
    data->step = 0;
    data->type = type;
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
    lv_obj_set_style_text_color(hw_matrix, lv_color_white(), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(hw_matrix, lv_color_make(35, 41, 55), LV_PART_ITEMS);
    lv_obj_set_style_border_width(hw_matrix, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(hw_matrix, lv_color_make(55, 65, 81), LV_PART_ITEMS);
    lv_obj_set_style_radius(hw_matrix, 5, LV_PART_ITEMS);
    Serial.printf("UI_HW: hardware matrix ready obj=%p heap_free=%lu heap_largest=%lu\n",
                  static_cast<void *>(hw_matrix),
                  static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_8BIT)),
                  static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
}



static void build_co2_subpage() {
    subpage_co2 = create_subpage("CO2 System");
    
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
    subpage_ec = create_subpage("EC Sensor");
    
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

static void build_subpages() {
    subpage_wifi = create_subpage("WiFi");

    wifi_main_panel = lv_obj_create(subpage_wifi);
    lv_obj_set_size(wifi_main_panel, 320, 210);
    lv_obj_set_pos(wifi_main_panel, 0, 30);
    lv_obj_set_style_bg_opa(wifi_main_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_main_panel, 0, 0);
    lv_obj_set_style_pad_all(wifi_main_panel, 0, 0);
    lv_obj_clear_flag(wifi_main_panel, LV_OBJ_FLAG_SCROLLABLE);

    wifi_info_card = create_card(wifi_main_panel, 304, 110, 8, 6);
    lv_obj_set_style_pad_all(wifi_info_card, 0, 0);
    lv_obj_clear_flag(wifi_info_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(wifi_info_card, resolve_bg_color(lv_color_make(20, 26, 40)), 0);
    lv_obj_set_style_border_color(wifi_info_card, lv_color_make(239, 68, 68), 0);
    lv_obj_set_style_border_width(wifi_info_card, 2, 0);

    lv_obj_t *wifi_icon_big = create_label(wifi_info_card, LV_SYMBOL_WIFI, lv_color_make(239, 68, 68), &lv_font_montserrat_24);
    lv_obj_align(wifi_icon_big, LV_ALIGN_TOP_LEFT, 10, 8);

    wifi_mode_lbl = create_label(wifi_info_card, "ROZLACZONY", lv_color_make(239, 68, 68), &lv_font_montserrat_14);
    lv_obj_align(wifi_mode_lbl, LV_ALIGN_TOP_LEFT, 46, 8);

    wifi_status_message_lbl = create_label(wifi_info_card, "Brak polaczenia WiFi", theme_text_muted(), &lv_font_montserrat_12);
    lv_obj_set_width(wifi_status_message_lbl, 190);
    lv_label_set_long_mode(wifi_status_message_lbl, LV_LABEL_LONG_DOT);
    lv_obj_align(wifi_status_message_lbl, LV_ALIGN_TOP_LEFT, 46, 28);

    btn_disconnect = create_button(wifi_info_card, LV_SYMBOL_CLOSE " Rozlacz", 90, 26, lv_color_make(239, 68, 68), btn_wifi_disc_handler, nullptr);
    lv_obj_align(btn_disconnect, LV_ALIGN_TOP_RIGHT, -8, 6);
    lv_obj_add_flag(btn_disconnect, LV_OBJ_FLAG_HIDDEN);
    apply_3d_button_properties(btn_disconnect);

    lv_obj_t *wifi_sep = lv_obj_create(wifi_info_card);
    lv_obj_set_size(wifi_sep, 280, 1);
    lv_obj_align(wifi_sep, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_style_bg_color(wifi_sep, theme_card_border(), 0);
    lv_obj_set_style_border_width(wifi_sep, 0, 0);
    lv_obj_clear_flag(wifi_sep, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(wifi_sep, LV_OBJ_FLAG_CLICKABLE);

    wifi_ssid_lbl = create_label(wifi_info_card, LV_SYMBOL_WIFI "  SSID: --", theme_text_main(), &lv_font_montserrat_12);
    lv_obj_set_width(wifi_ssid_lbl, 180);
    lv_label_set_long_mode(wifi_ssid_lbl, LV_LABEL_LONG_DOT);
    lv_obj_align(wifi_ssid_lbl, LV_ALIGN_TOP_LEFT, 10, 56);

    wifi_rssi_lbl = create_label(wifi_info_card, "RSSI: --", theme_text_muted(), &lv_font_montserrat_12);
    lv_obj_align(wifi_rssi_lbl, LV_ALIGN_TOP_RIGHT, -10, 56);

    wifi_ip_lbl = create_label(wifi_info_card, LV_SYMBOL_RIGHT "  IP: --", theme_text_muted(), &lv_font_montserrat_12);
    lv_obj_set_width(wifi_ip_lbl, 180);
    lv_label_set_long_mode(wifi_ip_lbl, LV_LABEL_LONG_DOT);
    lv_obj_align(wifi_ip_lbl, LV_ALIGN_TOP_LEFT, 10, 76);

    wifi_mac_lbl = create_label(wifi_info_card, "MAC: --", lv_color_make(100, 116, 139), &lv_font_montserrat_12);
    lv_obj_set_width(wifi_mac_lbl, 280);
    lv_label_set_long_mode(wifi_mac_lbl, LV_LABEL_LONG_DOT);
    lv_obj_align(wifi_mac_lbl, LV_ALIGN_BOTTOM_LEFT, 10, -6);

    lv_obj_t *wifi_actions_card = create_card(wifi_main_panel, 304, 80, 8, 120);
    lv_obj_set_style_pad_all(wifi_actions_card, 0, 0);
    lv_obj_clear_flag(wifi_actions_card, LV_OBJ_FLAG_SCROLLABLE);
    create_accent_bar(wifi_actions_card, lv_color_make(6, 182, 212), 68);

    lv_obj_t *wifi_actions_title = create_label(wifi_actions_card, "Akcje", lv_color_make(6, 182, 212), &lv_font_montserrat_12);
    lv_obj_align(wifi_actions_title, LV_ALIGN_TOP_LEFT, 10, 4);

    lv_obj_t *wifi_actions_hint = create_label(wifi_actions_card, "Skanuj sieci lub uruchom OTA.", theme_text_muted(), &lv_font_montserrat_12);
    lv_obj_set_width(wifi_actions_hint, 220);
    lv_label_set_long_mode(wifi_actions_hint, LV_LABEL_LONG_CLIP);
    lv_obj_align(wifi_actions_hint, LV_ALIGN_TOP_LEFT, 10, 22);

    btn_sta = create_button(wifi_actions_card, LV_SYMBOL_WIFI "  Skanuj sieci", 140, 30, lv_color_make(14, 165, 233), btn_sta_handler, nullptr);
    lv_obj_align(btn_sta, LV_ALIGN_BOTTOM_LEFT, 8, -8);
    apply_3d_button_properties(btn_sta);
    lv_obj_t *sta_lbl = lv_obj_get_child(btn_sta, 0);
    lv_obj_set_style_text_font(sta_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(sta_lbl, lv_color_white(), 0);

    btn_ota = create_button(wifi_actions_card, LV_SYMBOL_UPLOAD "  Tryb OTA", 140, 30, lv_color_make(20, 184, 166), btn_ota_handler, nullptr);
    lv_obj_align(btn_ota, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
    apply_3d_button_properties(btn_ota);
    lv_obj_t *ota_lbl = lv_obj_get_child(btn_ota, 0);
    lv_obj_set_style_text_font(ota_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(ota_lbl, lv_color_white(), 0);

    wifi_sta_panel = lv_obj_create(subpage_wifi);
    lv_obj_set_size(wifi_sta_panel, 320, 210);
    lv_obj_set_pos(wifi_sta_panel, 0, 30);
    style_panel(wifi_sta_panel, theme_screen_bg(), theme_screen_bg(), 0);
    lv_obj_set_style_pad_all(wifi_sta_panel, 0, 0);
    lv_obj_add_flag(wifi_sta_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(wifi_sta_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *sta_header = create_heading_card(wifi_sta_panel, 304, 42, 8, 4,
                                               "Wybierz siec WiFi",
                                               "Dotknij siec, aby wpisac haslo.",
                                               lv_color_make(14, 165, 233));
    LV_UNUSED(sta_header);

    sta_list_obj = lv_list_create(wifi_sta_panel);
    lv_obj_set_size(sta_list_obj, 300, 114);
    lv_obj_align(sta_list_obj, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_set_style_bg_color(sta_list_obj, resolve_bg_color(lv_color_make(20, 26, 40)), 0);
    lv_obj_set_style_border_color(sta_list_obj, theme_card_border(), 0);
    lv_obj_set_style_pad_all(sta_list_obj, 4, 0);

    lv_obj_t *list_btn = lv_list_add_btn(sta_list_obj, LV_SYMBOL_WIFI, "Skanuj sieci");
    lv_obj_add_event_cb(list_btn, btn_sta_handler, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *back_sta_btn = create_button(wifi_sta_panel, LV_SYMBOL_LEFT "  Wstecz", 110, 30, lv_color_make(30, 41, 59), cancel_sta_cb, nullptr);
    lv_obj_align(back_sta_btn, LV_ALIGN_BOTTOM_LEFT, 10, -6);
    apply_3d_button_properties(back_sta_btn);

    lv_obj_t *rescan_btn = create_button(wifi_sta_panel, LV_SYMBOL_REFRESH "  Skanuj", 110, 30, lv_color_make(14, 165, 233), btn_sta_handler, nullptr);
    lv_obj_align(rescan_btn, LV_ALIGN_BOTTOM_RIGHT, -10, -6);
    apply_3d_button_properties(rescan_btn);

    wifi_pwd_panel = lv_obj_create(subpage_wifi);
    lv_obj_set_size(wifi_pwd_panel, 320, 210);
    lv_obj_set_pos(wifi_pwd_panel, 0, 30);
    style_panel(wifi_pwd_panel, theme_screen_bg(), theme_screen_bg(), 0);
    lv_obj_set_style_pad_all(wifi_pwd_panel, 0, 0);
    lv_obj_add_flag(wifi_pwd_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(wifi_pwd_panel, LV_OBJ_FLAG_SCROLLABLE);

    wifi_pwd_title_lbl = create_label(wifi_pwd_panel, "Haslo do: --", theme_text_main(), &lv_font_montserrat_12);
    lv_obj_set_width(wifi_pwd_title_lbl, 240);
    lv_label_set_long_mode(wifi_pwd_title_lbl, LV_LABEL_LONG_DOT);
    lv_obj_align(wifi_pwd_title_lbl, LV_ALIGN_TOP_LEFT, 10, 4);

    lv_obj_t *pwd_hint_lbl = create_label(wifi_pwd_panel, "Wpisz haslo i potwierdz na klawiaturze.", theme_text_muted(), &lv_font_montserrat_12);
    lv_obj_set_width(pwd_hint_lbl, 248);
    lv_label_set_long_mode(pwd_hint_lbl, LV_LABEL_LONG_CLIP);
    lv_obj_align(pwd_hint_lbl, LV_ALIGN_TOP_LEFT, 10, 20);

    wifi_pwd_ta = lv_textarea_create(wifi_pwd_panel);
    lv_obj_set_size(wifi_pwd_ta, 230, 28);
    lv_obj_align(wifi_pwd_ta, LV_ALIGN_TOP_LEFT, 10, 38);
    lv_textarea_set_one_line(wifi_pwd_ta, true);
    lv_textarea_set_password_mode(wifi_pwd_ta, true);
    lv_obj_set_style_bg_color(wifi_pwd_ta, resolve_bg_color(lv_color_make(20, 26, 40)), 0);
    lv_obj_set_style_border_color(wifi_pwd_ta, lv_color_make(14, 165, 233), 0);
    lv_obj_set_style_border_width(wifi_pwd_ta, 2, 0);
    lv_obj_set_style_text_color(wifi_pwd_ta, theme_text_main(), 0);

    lv_obj_t *back_pwd_btn = create_button(wifi_pwd_panel, LV_SYMBOL_CLOSE, 28, 28, lv_color_make(71, 85, 105), cancel_pwd_cb, nullptr);
    lv_obj_align(back_pwd_btn, LV_ALIGN_TOP_RIGHT, -10, 18);
    apply_3d_button_properties(back_pwd_btn);

    wifi_pwd_kb = lv_keyboard_create(wifi_pwd_panel);
    lv_obj_set_size(wifi_pwd_kb, 320, 155);
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

    lv_obj_t *ota_banner = create_heading_card(wifi_ota_panel, 304, 42, 8, 6,
                                               "Tryb OTA aktywny",
                                               "Polacz sie z AP i wgraj firmware.",
                                               lv_color_make(20, 184, 166));
    LV_UNUSED(ota_banner);

    lv_obj_t *ota_card = create_card(wifi_ota_panel, 298, 112, 11, 54);
    lv_obj_set_style_pad_all(ota_card, 10, 0);
    lv_obj_clear_flag(ota_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ota_instr = create_label(ota_card, "Polacz sie z siecia AP i wyslij firmware.", theme_text_muted(), &lv_font_montserrat_12);
    lv_obj_set_width(ota_instr, 272);
    lv_label_set_long_mode(ota_instr, LV_LABEL_LONG_WRAP);
    lv_obj_align(ota_instr, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *ota_ssid_lbl = create_label(ota_card, LV_SYMBOL_WIFI "  SSID: cydAquarium-OTA", theme_text_main(), &lv_font_montserrat_12);
    lv_obj_align(ota_ssid_lbl, LV_ALIGN_TOP_LEFT, 0, 28);
    lv_obj_t *ota_pass_lbl = create_label(ota_card, LV_SYMBOL_EYE_CLOSE "  Haslo: ukryte", theme_text_main(), &lv_font_montserrat_12);
    lv_obj_align(ota_pass_lbl, LV_ALIGN_TOP_LEFT, 0, 48);
    lv_obj_t *ota_ip_lbl = create_label(ota_card, LV_SYMBOL_RIGHT "  IP: 192.168.4.1", lv_color_make(20, 184, 166), &lv_font_montserrat_12);
    lv_obj_align(ota_ip_lbl, LV_ALIGN_TOP_LEFT, 0, 68);

    lv_obj_t *ota_stop_btn = create_button(wifi_ota_panel, LV_SYMBOL_CLOSE "  Zatrzymaj OTA", 160, 32, lv_color_make(239, 68, 68), stop_ota_cb, nullptr);
    lv_obj_align(ota_stop_btn, LV_ALIGN_BOTTOM_MID, 0, -10);
    apply_3d_button_properties(ota_stop_btn);

    subpage_screen = create_subpage("LCD Screen");
    
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
    lv_obj_t *ldr_enable_lbl = create_label(theme_group, "Auto LDR (jasno=ciemny)", theme_text_main(), &lv_font_montserrat_12);
    lv_obj_set_width(ldr_enable_lbl, 200); // Ograniczona szerokosc - nie zakrywa switcha
    lv_obj_align(ldr_enable_lbl, LV_ALIGN_TOP_LEFT, 8, 54);
    screen_ldr_enable_sw = lv_switch_create(theme_group);
    lv_obj_set_size(screen_ldr_enable_sw, 40, 20);
    lv_obj_align(screen_ldr_enable_sw, LV_ALIGN_TOP_RIGHT, -8, 52);
    style_switch_cyd(screen_ldr_enable_sw);
    lv_obj_add_event_cb(screen_ldr_enable_sw, screen_ldr_enable_handler, LV_EVENT_VALUE_CHANGED, nullptr);

    // --- Karta: Czulosc czujnika LDR ---
    lv_obj_t *ldr_row = create_card(scr_list, 300, 52, 0, 0);
    lv_obj_set_style_pad_all(ldr_row, 0, 0);
    lv_obj_clear_flag(ldr_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ldr_sens_title = create_label(ldr_row, "Czulosc LDR:", theme_text_muted(), &lv_font_montserrat_12);
    lv_obj_align(ldr_sens_title, LV_ALIGN_TOP_LEFT, 8, 6);
    screen_ldr_value_lbl = create_label(ldr_row, "50%", lv_color_make(6, 182, 212), &lv_font_montserrat_12);
    lv_obj_align(screen_ldr_value_lbl, LV_ALIGN_TOP_MID, 20, 6);
    screen_ldr_raw_lbl = create_label(ldr_row, "ADC: --", lv_color_make(100, 116, 139), &lv_font_montserrat_12);
    lv_obj_align(screen_ldr_raw_lbl, LV_ALIGN_TOP_RIGHT, -8, 6);

    screen_ldr_slider = lv_slider_create(ldr_row);
    lv_slider_set_range(screen_ldr_slider, 0, 100);
    lv_obj_set_size(screen_ldr_slider, 280, 10);
    lv_obj_align(screen_ldr_slider, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_color(screen_ldr_slider, resolve_bg_color(lv_color_make(30, 41, 59)), LV_PART_MAIN);
    lv_obj_set_style_bg_color(screen_ldr_slider, lv_color_make(6, 182, 212), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(screen_ldr_slider, lv_color_white(), LV_PART_KNOB);
    lv_obj_add_event_cb(screen_ldr_slider, screen_ldr_slider_cb, LV_EVENT_VALUE_CHANGED, nullptr);

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
    screen_dev_mode_sw = lv_switch_create(sys_group);
    lv_obj_set_size(screen_dev_mode_sw, 40, 20);
    lv_obj_align(screen_dev_mode_sw, LV_ALIGN_TOP_RIGHT, -8, 26);
    style_switch_cyd(screen_dev_mode_sw);
    lv_obj_add_event_cb(screen_dev_mode_sw, screen_dev_mode_handler, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t *screen_save = create_button(subpage_screen, LV_SYMBOL_SAVE "  ZAPISZ USTAWIENIA", 200, 26, lv_color_make(16, 185, 129), save_screen_settings_cb, nullptr);
    lv_obj_align(screen_save, LV_ALIGN_BOTTOM_MID, 0, -2);

    subpage_logs = create_subpage("Logs");

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

    lv_obj_t *clear_logs = create_button(subpage_logs, "CLEAR", 120, 28, lv_color_make(35, 41, 55), clear_logs_cb, nullptr);
    lv_obj_align(clear_logs, LV_ALIGN_BOTTOM_MID, 0, -12);
    apply_3d_button_properties(clear_logs);

    subpage_clock = create_subpage("Time", back_clock_cb, nullptr);
    
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
    lv_obj_t *ntp = create_button(clock_ntp_row, "Sync with NTP", 280, 32, lv_color_make(35, 41, 55), btn_sync_ntp_handler, nullptr);
    lv_obj_align(ntp, LV_ALIGN_CENTER, 0, 0);
    btn_sync_ntp_lbl_global = lv_obj_get_child(ntp, 0);

    // Initial NTP Sync row visibility based on connection status
    if (!wifi_connected) {
        lv_obj_add_flag(clock_ntp_row, LV_OBJ_FLAG_HIDDEN);
    }

    subpage_diagnostics = create_subpage("Diagnostics");

    lv_obj_t *diag_list = lv_obj_create(subpage_diagnostics);
    lv_obj_set_size(diag_list, 312, 196);
    lv_obj_set_pos(diag_list, 4, 34);
    lv_obj_set_style_bg_opa(diag_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(diag_list, 0, 0);
    lv_obj_set_style_pad_all(diag_list, 0, 0);
    lv_obj_set_flex_flow(diag_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(diag_list, 6, 0);
    lv_obj_set_scrollbar_mode(diag_list, LV_SCROLLBAR_MODE_AUTO);

    // Karta 1: System Info
    lv_obj_t *card_sys = create_card(diag_list, 300, 72, 0, 0);
    lv_obj_set_style_pad_all(card_sys, 6, 0);
    
    lv_obj_t *sys_title = create_label(card_sys, "SYSTEM", lv_color_make(6, 182, 212), &lv_font_montserrat_12);
    lv_obj_align(sys_title, LV_ALIGN_TOP_LEFT, 5, 0);

    diag_uptime_lbl = create_label(card_sys, "Uptime: 0m 0s", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(diag_uptime_lbl, LV_ALIGN_TOP_LEFT, 5, 18);

    diag_restarts_lbl = create_label(card_sys, "Boot count: --", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(diag_restarts_lbl, LV_ALIGN_TOP_LEFT, 5, 34);

    diag_reset_reason_lbl = create_label(card_sys, "Reason: --", lv_color_make(148, 163, 184), &lv_font_montserrat_12);
    lv_obj_align(diag_reset_reason_lbl, LV_ALIGN_TOP_LEFT, 5, 50);

    // Karta 2: Hardware Info
    lv_obj_t *card_hw = create_card(diag_list, 300, 72, 0, 0);
    lv_obj_set_style_pad_all(card_hw, 6, 0);

    lv_obj_t *hw_title = create_label(card_hw, "HARDWARE", lv_color_make(6, 182, 212), &lv_font_montserrat_12);
    lv_obj_align(hw_title, LV_ALIGN_TOP_LEFT, 5, 0);

    diag_cpu_temp_lbl = create_label(card_hw, "CPU Temp: -- *C", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(diag_cpu_temp_lbl, LV_ALIGN_TOP_LEFT, 5, 18);

    diag_cpu_freq_lbl = create_label(card_hw, "CPU Freq: -- MHz", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(diag_cpu_freq_lbl, LV_ALIGN_TOP_LEFT, 5, 34);

    diag_flash_lbl = create_label(card_hw, "Flash: -- MB", lv_color_make(148, 163, 184), &lv_font_montserrat_12);
    lv_obj_align(diag_flash_lbl, LV_ALIGN_TOP_LEFT, 5, 50);

    // Karta 3: Memory Info
    lv_obj_t *card_mem = create_card(diag_list, 300, 56, 0, 0);
    lv_obj_set_style_pad_all(card_mem, 6, 0);

    lv_obj_t *mem_title = create_label(card_mem, "MEMORY (RAM)", lv_color_make(6, 182, 212), &lv_font_montserrat_12);
    lv_obj_align(mem_title, LV_ALIGN_TOP_LEFT, 5, 0);

    diag_heap_lbl = create_label(card_mem, "Free heap: -- KB", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(diag_heap_lbl, LV_ALIGN_TOP_LEFT, 5, 18);

    lv_obj_t *card_bus = create_card(diag_list, 300, 88, 0, 0);
    lv_obj_set_style_pad_all(card_bus, 6, 0);

    lv_obj_t *bus_title = create_label(card_bus, "CZUJNIKI / MAGISTRALE", lv_color_make(20, 184, 166), &lv_font_montserrat_12);
    lv_obj_align(bus_title, LV_ALIGN_TOP_LEFT, 5, 0);

    diag_adc_lbl = create_label(card_bus, "ADS1115: --", theme_text_main(), &lv_font_montserrat_12);
    lv_obj_align(diag_adc_lbl, LV_ALIGN_TOP_LEFT, 5, 18);

    diag_mcp_lbl = create_label(card_bus, "MCP23017: --", theme_text_main(), &lv_font_montserrat_12);
    lv_obj_align(diag_mcp_lbl, LV_ALIGN_TOP_LEFT, 5, 34);

    diag_queue_lbl = create_label(card_bus, "Kolejka overflow: 0", theme_text_muted(), &lv_font_montserrat_12);
    lv_obj_align(diag_queue_lbl, LV_ALIGN_TOP_LEFT, 5, 50);

    diag_ldr_lbl = create_label(card_bus, "LDR GPIO34: --", theme_text_muted(), &lv_font_montserrat_12);
    lv_obj_align(diag_ldr_lbl, LV_ALIGN_TOP_LEFT, 5, 66);

    // Karta 4: Reset fabryczny
    lv_obj_t *card_reset = create_card(diag_list, 300, 72, 0, 0);
    lv_obj_set_style_pad_all(card_reset, 6, 0);
    lv_obj_clear_flag(card_reset, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *reset_title = create_label(card_reset, "RESET FABRYCZNY", lv_color_make(239, 68, 68), &lv_font_montserrat_12);
    lv_obj_align(reset_title, LV_ALIGN_TOP_LEFT, 5, 0);

    power_warning_lbl_global = create_label(card_reset, "Reset wykasuje wszystkie ustawienia.", lv_color_make(148, 163, 184), &lv_font_montserrat_12);
    lv_obj_align(power_warning_lbl_global, LV_ALIGN_TOP_LEFT, 5, 18);

    lv_obj_t *btn_reset = create_button(card_reset, "Reset Fabryczny", 150, 24, lv_color_make(239, 68, 68), btn_factory_reset_handler, nullptr);
    lv_obj_align(btn_reset, LV_ALIGN_BOTTOM_MID, 0, -2);
    apply_3d_button_properties(btn_reset);

    subpage_power = create_subpage("Power");

    lv_obj_t *power_list = lv_obj_create(subpage_power);
    lv_obj_set_size(power_list, 312, 196);
    lv_obj_set_pos(power_list, 4, 34);
    lv_obj_set_style_bg_opa(power_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(power_list, 0, 0);
    lv_obj_set_style_pad_all(power_list, 0, 0);
    lv_obj_set_flex_flow(power_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(power_list, 6, 0);
    lv_obj_set_scrollbar_mode(power_list, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t *card_state = create_card(power_list, 300, 58, 0, 0);
    lv_obj_set_style_pad_all(card_state, 6, 0);
    lv_obj_t *state_title = create_label(card_state, "ZASILANIE", lv_color_make(239, 68, 68), &lv_font_montserrat_12);
    lv_obj_align(state_title, LV_ALIGN_TOP_LEFT, 5, 0);
    power_state_lbl = create_label(card_state, "Ekran: ON | WiFi: aktywne", theme_text_main(), &lv_font_montserrat_12);
    lv_obj_set_width(power_state_lbl, 286);
    lv_label_set_long_mode(power_state_lbl, LV_LABEL_LONG_DOT);
    lv_obj_align(power_state_lbl, LV_ALIGN_TOP_LEFT, 5, 20);
    lv_obj_t *state_hint = create_label(card_state, "Akcje krytyczne wymagaja PIN.", theme_text_muted(), &lv_font_montserrat_12);
    lv_obj_align(state_hint, LV_ALIGN_BOTTOM_LEFT, 5, -2);

    // Karta 1: Restart Systemu
    lv_obj_t *card_restart = create_card(power_list, 300, 46, 0, 0);
    lv_obj_set_style_pad_all(card_restart, 0, 0);
    lv_obj_clear_flag(card_restart, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *restart_lbl = create_label(card_restart, "Restart systemu", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(restart_lbl, LV_ALIGN_LEFT_MID, 10, 0);

    lv_obj_t *btn_restart = create_button(card_restart, "Restart", 80, 28, lv_color_make(239, 68, 68), btn_restart_event_handler, nullptr);
    lv_obj_align(btn_restart, LV_ALIGN_RIGHT_MID, -10, 0);
    apply_3d_button_properties(btn_restart);

    // Karta 2: Modem Sleep (Wi-Fi OFF)
    lv_obj_t *card_modem = create_card(power_list, 300, 46, 0, 0);
    lv_obj_set_style_pad_all(card_modem, 0, 0);
    lv_obj_clear_flag(card_modem, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *modem_lbl = create_label(card_modem, "Tryb Modem Sleep", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(modem_lbl, LV_ALIGN_LEFT_MID, 10, -7);
    lv_obj_t *modem_sub = create_label(card_modem, "Wylacza Wi-Fi dla oszczedzania energii", lv_color_make(148, 163, 184), &lv_font_montserrat_12);
    lv_obj_align(modem_sub, LV_ALIGN_LEFT_MID, 10, 8);

    power_modem_sleep_sw = lv_switch_create(card_modem);
    lv_obj_set_size(power_modem_sleep_sw, 42, 22);
    lv_obj_align(power_modem_sleep_sw, LV_ALIGN_RIGHT_MID, -10, 0);
    style_switch_cyd(power_modem_sleep_sw);
    lv_obj_add_event_cb(power_modem_sleep_sw, power_modem_sleep_handler, LV_EVENT_VALUE_CHANGED, nullptr);

    // Karta 3: Light Sleep
    lv_obj_t *card_light = create_card(power_list, 300, 46, 0, 0);
    lv_obj_set_style_pad_all(card_light, 0, 0);
    lv_obj_clear_flag(card_light, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *light_lbl = create_label(card_light, "Tryb Light Sleep", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(light_lbl, LV_ALIGN_LEFT_MID, 10, -7);
    lv_obj_t *light_sub = create_label(card_light, "Wylacza ekran i CPU na 10s", lv_color_make(148, 163, 184), &lv_font_montserrat_12);
    lv_obj_align(light_sub, LV_ALIGN_LEFT_MID, 10, 8);

    lv_obj_t *btn_light = create_button(card_light, "Uspij 10s", 90, 28, lv_color_make(59, 130, 246), btn_light_sleep_handler, nullptr);
    lv_obj_align(btn_light, LV_ALIGN_RIGHT_MID, -10, 0);
    apply_3d_button_properties(btn_light);

    // Karta 4: Deep Sleep
    lv_obj_t *card_deep = create_card(power_list, 300, 46, 0, 0);
    lv_obj_set_style_pad_all(card_deep, 0, 0);
    lv_obj_clear_flag(card_deep, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *deep_lbl = create_label(card_deep, "Tryb Deep Sleep", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(deep_lbl, LV_ALIGN_LEFT_MID, 10, -7);
    lv_obj_t *deep_sub = create_label(card_deep, "Wylacza CPU/RAM, restart po 30s", lv_color_make(148, 163, 184), &lv_font_montserrat_12);
    lv_obj_align(deep_sub, LV_ALIGN_LEFT_MID, 10, 8);

    lv_obj_t *btn_deep = create_button(card_deep, "Uspij 30s", 90, 28, lv_color_make(71, 85, 105), btn_deep_sleep_handler, nullptr);
    lv_obj_align(btn_deep, LV_ALIGN_RIGHT_MID, -10, 0);
    apply_3d_button_properties(btn_deep);

    // Karta 5: Hibernacja
    lv_obj_t *card_hib = create_card(power_list, 300, 46, 0, 0);
    lv_obj_set_style_pad_all(card_hib, 0, 0);
    lv_obj_clear_flag(card_hib, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *hib_lbl = create_label(card_hib, "Tryb Hibernacji", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(hib_lbl, LV_ALIGN_LEFT_MID, 10, -7);
    lv_obj_t *hib_sub = create_label(card_hib, "Wylacza rtc, restart po 30s", lv_color_make(148, 163, 184), &lv_font_montserrat_12);
    lv_obj_align(hib_sub, LV_ALIGN_LEFT_MID, 10, 8);

    lv_obj_t *btn_hib = create_button(card_hib, "Uspij 30s", 90, 28, lv_color_make(30, 41, 59), btn_hibernation_handler, nullptr);
    lv_obj_align(btn_hib, LV_ALIGN_RIGHT_MID, -10, 0);
    apply_3d_button_properties(btn_hib);

    subpage_feed_editor = create_subpage("Auto Feed", back_feed_editor_cb, nullptr);
    
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
    lv_obj_t *feed_enabled_lbl = create_label(feed_enabled_row, "Auto feeding", lv_color_white(), &lv_font_montserrat_12);
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
    
    lv_obj_t *feed_days_lbl = create_label(feed_days_row, "Days of week", lv_color_white(), &lv_font_montserrat_12);
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
    lv_obj_t *feed_freq_lbl = create_label(feed_freq_row, "Frequency", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(feed_freq_lbl, LV_ALIGN_LEFT_MID, 10, 0);

    feed_freq_btn = create_button(feed_freq_row, "1 time a day", 110, 26, lv_color_make(35, 41, 55), feed_freq_click_cb, nullptr);
    lv_obj_align(feed_freq_btn, LV_ALIGN_RIGHT_MID, -10, 0);

    // Card 4: Time 1 Row
    lv_obj_t *feed_time1_row = create_card(feed_list, 300, 46, 0, 0);
    lv_obj_set_style_pad_all(feed_time1_row, 0, 0);
    lv_obj_clear_flag(feed_time1_row, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *time1_lbl = create_label(feed_time1_row, "Time 1", lv_color_white(), &lv_font_montserrat_12);
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
    
    lv_obj_t *time2_lbl = create_label(feed_time2_row, "Time 2", lv_color_white(), &lv_font_montserrat_12);
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

    subpage_sched_editor = create_subpage("Schedule", back_sched_editor_cb, nullptr);
    editor_title_lbl = create_label(subpage_sched_editor, "FrontLight", lv_color_white(), &lv_font_montserrat_14);
    lv_obj_align(editor_title_lbl, LV_ALIGN_TOP_MID, 0, 36);
    sched_editor_mode_btn = create_button(subpage_sched_editor, "Mode", 100, 28, lv_color_make(35, 41, 55), cycle_editor_mode_cb, nullptr);
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

    const char* light_modes[] = {"MIX", "WHITE", "PLANT"};
    for (int i = 0; i < 3; i++) {
        editor_mode_btns[i] = create_button(btn_container, light_modes[i], 58, 22, lv_color_make(35, 41, 55), editor_mode_btn_cb, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
        lv_obj_set_style_radius(editor_mode_btns[i], 4, 0);
        lv_obj_set_style_pad_all(editor_mode_btns[i], 0, 0);
    }

    // --- SOUND SETTINGS SUBPAGE ---
    subpage_sounds = create_subpage("Sound Settings");

    lv_obj_t *snd_list = lv_obj_create(subpage_sounds);
    lv_obj_set_size(snd_list, 312, 168);
    lv_obj_set_pos(snd_list, 4, 34);
    lv_obj_set_style_bg_opa(snd_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(snd_list, 0, 0);
    lv_obj_set_style_pad_all(snd_list, 0, 0);
    lv_obj_set_flex_flow(snd_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(snd_list, 6, 0);
    lv_obj_set_scrollbar_mode(snd_list, LV_SCROLLBAR_MODE_AUTO);

    // Card 1: Enable audio feedback
    lv_obj_t *sound_enable_row = create_card(snd_list, 300, 46, 0, 0);
    lv_obj_set_style_pad_all(sound_enable_row, 0, 0);
    lv_obj_t *sound_enable_lbl = create_label(sound_enable_row, "Enable audio feedback", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(sound_enable_lbl, LV_ALIGN_LEFT_MID, 10, 0);
    sound_enable_sw = lv_switch_create(sound_enable_row);
    lv_obj_set_size(sound_enable_sw, 42, 22);
    lv_obj_align(sound_enable_sw, LV_ALIGN_RIGHT_MID, -10, 0);
    style_switch_cyd(sound_enable_sw);
    lv_obj_add_event_cb(sound_enable_sw, sound_enable_handler, LV_EVENT_VALUE_CHANGED, nullptr);

    // Card 2: Enable quiet hours
    lv_obj_t *quiet_enable_row = create_card(snd_list, 300, 46, 0, 0);
    lv_obj_set_style_pad_all(quiet_enable_row, 0, 0);
    lv_obj_t *quiet_enable_lbl = create_label(quiet_enable_row, "Quiet hours (silent)", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(quiet_enable_lbl, LV_ALIGN_LEFT_MID, 10, 0);
    sound_quiet_enable_sw = lv_switch_create(quiet_enable_row);
    lv_obj_set_size(sound_quiet_enable_sw, 42, 22);
    lv_obj_align(sound_quiet_enable_sw, LV_ALIGN_RIGHT_MID, -10, 0);
    style_switch_cyd(sound_quiet_enable_sw);
    lv_obj_add_event_cb(sound_quiet_enable_sw, sound_quiet_enable_handler, LV_EVENT_VALUE_CHANGED, nullptr);

    // Card 3: Quiet Hours Schedule Card
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

    // Card 5: Test speaker
    lv_obj_t *speaker_test_row = create_card(snd_list, 300, 46, 0, 0);
    lv_obj_set_style_pad_all(speaker_test_row, 0, 0);
    lv_obj_t *speaker_test_lbl = create_label(speaker_test_row, "Test speaker", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(speaker_test_lbl, LV_ALIGN_LEFT_MID, 10, 0);

    lv_obj_t *speaker_test_btn = create_button(speaker_test_row, "TEST", 70, 26, lv_color_make(6, 182, 212), test_speaker_cb, nullptr);
    lv_obj_align(speaker_test_btn, LV_ALIGN_RIGHT_MID, -10, 0);

    lv_obj_t *sound_save = create_button(subpage_sounds, "SAVE SOUNDS", 180, 26, lv_color_make(16, 185, 129), save_sound_settings_cb, nullptr);
    lv_obj_align(sound_save, LV_ALIGN_BOTTOM_MID, 0, -2);

    modal_feeder = lv_obj_create(lv_scr_act());
    lv_obj_set_size(modal_feeder, 240, 140);
    lv_obj_align(modal_feeder, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(modal_feeder, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_pad_all(modal_feeder, 0, 0);
    style_panel(modal_feeder, lv_color_make(20, 26, 40), lv_color_make(6, 182, 212), 12);
    lv_obj_t *spinner = lv_spinner_create(modal_feeder, 1000, 60);
    lv_obj_set_size(spinner, 40, 40);
    lv_obj_align(spinner, LV_ALIGN_TOP_MID, 0, 15);
    lv_obj_set_style_arc_color(spinner, lv_color_make(6, 182, 212), LV_PART_INDICATOR);
    modal_feeder_title_lbl = create_label(modal_feeder, "Feeding", lv_color_white(), &lv_font_montserrat_14);
    lv_obj_align(modal_feeder_title_lbl, LV_ALIGN_BOTTOM_MID, 0, -35);
    modal_feeder_msg_lbl = create_label(modal_feeder, "Starting motor", lv_color_make(100, 116, 139), &lv_font_montserrat_12);
    lv_obj_align(modal_feeder_msg_lbl, LV_ALIGN_BOTTOM_MID, 0, -15);

    // --- HEATER SETTINGS SUBPAGE ---
    subpage_heater = create_subpage("Heater Settings", back_heater_cb, nullptr);
    
    lv_obj_t *heat_list = lv_obj_create(subpage_heater);
    lv_obj_set_size(heat_list, 312, 196);
    lv_obj_set_pos(heat_list, 4, 34);
    lv_obj_set_style_bg_opa(heat_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(heat_list, 0, 0);
    lv_obj_set_style_pad_all(heat_list, 0, 0);
    lv_obj_set_flex_flow(heat_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(heat_list, 6, 0);
    lv_obj_set_scrollbar_mode(heat_list, LV_SCROLLBAR_MODE_AUTO);

    // Card 1: Heater threshold
    lv_obj_t *heater_row = create_card(heat_list, 300, 46, 0, 0);
    lv_obj_set_style_pad_all(heater_row, 0, 0);
    lv_obj_clear_flag(heater_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *heater_lbl = create_label(heater_row, "Heater threshold", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(heater_lbl, LV_ALIGN_LEFT_MID, 10, 0);
    temp_auto_sw = lv_switch_create(heater_row);
    lv_obj_set_size(temp_auto_sw, 40, 20);
    lv_obj_align(temp_auto_sw, LV_ALIGN_RIGHT_MID, -10, 0);
    style_switch_cyd(temp_auto_sw);
    lv_obj_add_event_cb(temp_auto_sw, toggle_heater_auto_handler, LV_EVENT_VALUE_CHANGED, nullptr);

    // Card 2: Target temp
    lv_obj_t *target_row = create_card(heat_list, 300, 46, 0, 0);
    lv_obj_set_style_pad_all(target_row, 0, 0);
    lv_obj_t *target_title = create_label(target_row, "Target temp", lv_color_white(), &lv_font_montserrat_12);
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

    // --- pH SETTINGS SUBPAGE ---
    subpage_ph = create_subpage("pH Settings", back_ph_cb, nullptr);
    
    lv_obj_t *ph_list = lv_obj_create(subpage_ph);
    lv_obj_set_size(ph_list, 312, 196);
    lv_obj_set_pos(ph_list, 4, 34);
    lv_obj_set_style_bg_opa(ph_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ph_list, 0, 0);
    lv_obj_set_style_pad_all(ph_list, 0, 0);
    lv_obj_set_flex_flow(ph_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ph_list, 6, 0);
    lv_obj_set_scrollbar_mode(ph_list, LV_SCROLLBAR_MODE_AUTO);

    // Card 1: pH enable
    lv_obj_t *ph_enable_row = create_card(ph_list, 300, 46, 0, 0);
    lv_obj_set_style_pad_all(ph_enable_row, 0, 0);
    lv_obj_t *ph_enable_lbl = create_label(ph_enable_row, "Show pH level", lv_color_white(), &lv_font_montserrat_12);
    lv_obj_align(ph_enable_lbl, LV_ALIGN_LEFT_MID, 10, 0);
    screen_ph_enable_sw = lv_switch_create(ph_enable_row);
    lv_obj_set_size(screen_ph_enable_sw, 42, 22);
    lv_obj_align(screen_ph_enable_sw, LV_ALIGN_RIGHT_MID, -10, 0);
    style_switch_cyd(screen_ph_enable_sw);
    lv_obj_add_event_cb(screen_ph_enable_sw, screen_ph_enable_handler, LV_EVENT_VALUE_CHANGED, nullptr);

    // --- SERVICE MODE SUBPAGE ---
    subpage_service = create_subpage("Service Mode", back_service_cb, nullptr);
    
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
    lv_dropdown_set_selected(song_dd, selectedSongIndex);
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
    lv_obj_align(vol_slider, LV_ALIGN_TOP_LEFT, 215, 28);
    lv_slider_set_range(vol_slider, 0, 10);
    lv_slider_set_value(vol_slider, musicVolume, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(vol_slider, resolve_bg_color(lv_color_make(30, 41, 59)), LV_PART_MAIN);
    lv_obj_set_style_bg_color(vol_slider, lv_color_make(6, 182, 212), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(vol_slider, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_pad_all(vol_slider, 0, LV_PART_KNOB);
    lv_obj_add_event_cb(vol_slider, service_volume_slider_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    // Buttons
    lv_obj_t *play_btn = create_button(music_card, "PLAY", 135, 30, lv_color_make(16, 185, 129), service_play_cb, nullptr);
    lv_obj_align(play_btn, LV_ALIGN_TOP_LEFT, 10, 68);
    apply_3d_button_properties(play_btn);
    lv_obj_t *play_lbl = lv_obj_get_child(play_btn, 0);
    if (play_lbl) lv_obj_set_style_text_font(play_lbl, &lv_font_montserrat_12, 0);

    lv_obj_t *stop_btn = create_button(music_card, "STOP", 135, 30, lv_color_make(239, 68, 68), service_stop_cb, nullptr);
    lv_obj_align(stop_btn, LV_ALIGN_TOP_LEFT, 155, 68);
    apply_3d_button_properties(stop_btn);
    lv_obj_t *stop_lbl = lv_obj_get_child(stop_btn, 0);
    if (stop_lbl) lv_obj_set_style_text_font(stop_lbl, &lv_font_montserrat_12, 0);
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
            lv_label_set_text_fmt(home_light_mode_lbl, "%s %s", mode_label(cfg.lightMode), light_color_mode_label(cfg.lightColorMode));
        } else {
            lv_label_set_text(home_light_mode_lbl, mode_label(cfg.lightMode));
        }
    }

    if (home_plant_state_lbl != nullptr) {
        set_binary_state(home_plant_state_lbl, runtime.plantLightOn, "ON", "OFF", lv_color_make(16, 185, 129));
    }
    if (home_plant_mode_lbl != nullptr) {
        if (cfg.plantLightMode == static_cast<uint8_t>(ScheduleMode::Schedule)) {
            lv_label_set_text_fmt(home_plant_mode_lbl, "%s %s", mode_label(cfg.plantLightMode), light_color_mode_label(cfg.plantLightColorMode));
        } else {
            lv_label_set_text(home_plant_mode_lbl, mode_label(cfg.plantLightMode));
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
                              light_color_mode_label(cfg.lightColorMode),
                              runtime.lightOn ? "ON" : "OFF");
    }
    if (device_plant_detail_lbl != nullptr) {
        lv_label_set_text_fmt(device_plant_detail_lbl, "%02u:%02u-%02u:%02u | %s | %s",
                              cfg.plantStartHour, cfg.plantStartMinute,
                              cfg.plantEndHour, cfg.plantEndMinute,
                              light_color_mode_label(cfg.plantLightColorMode),
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
        snprintf(buf, sizeof(buf), "Target: %.1f*C", cfg.targetTemp);
        lv_label_set_text(device_heater_detail_lbl, buf);
    }
    if (device_ph_detail_lbl != nullptr) {
        lv_label_set_text(device_ph_detail_lbl, cfg.showPhSensor ? "Active" : "OFF");
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
            lv_label_set_text(lbl, "Always ON");
        } else if (mode == static_cast<uint8_t>(ScheduleMode::AlwaysOff)) {
            lv_label_set_text(lbl, "Always OFF");
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
            lv_label_set_text(freq_lbl, cfg.feedCount == 2 ? "2 times a day" : "1 time a day");
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

    set_checked(screen_dev_mode_sw, cfg.devMode);
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
    if (screen_ldr_slider != nullptr) {
        lv_slider_set_value(screen_ldr_slider, cfg.ldrSensitivity, LV_ANIM_OFF);
    }
    if (screen_ldr_value_lbl != nullptr) {
        lv_label_set_text_fmt(screen_ldr_value_lbl, "%u%%", static_cast<unsigned>(cfg.ldrSensitivity));
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
        lv_label_set_text_fmt(power_state_lbl, "Ekran: %s | WiFi: %s",
                              cfg.alwaysScreenOn ? "stale ON" : "auto",
                              cfg.modemSleep ? "OFF" : (wifi_connected ? "STA" : "gotowe"));
    }
    if (service_light_sw != nullptr) {
        set_checked(service_light_sw, cfg.lightMode != static_cast<uint8_t>(ScheduleMode::AlwaysOff));
    }
    if (service_filter_sw != nullptr) {
        set_checked(service_filter_sw, cfg.filterMode != static_cast<uint8_t>(ScheduleMode::AlwaysOff));
    }
    if (service_vol_lbl != nullptr) {
        lv_label_set_text_fmt(service_vol_lbl, "Vol: %d0%%", musicVolume);
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
        float min_val = ph_history[0];
        float max_val = ph_history[0];
        float cur_val = ph_history[history_count - 1];
        for (uint8_t i = 1; i < history_count; ++i) {
            if (ph_history[i] < min_val) min_val = ph_history[i];
            if (ph_history[i] > max_val) max_val = ph_history[i];
        }
        if (chart_min_lbl != nullptr) lv_label_set_text_fmt(chart_min_lbl, "%.2f", min_val);
        if (chart_max_lbl != nullptr) lv_label_set_text_fmt(chart_max_lbl, "%.2f", max_val);
        if (chart_cur_lbl != nullptr) lv_label_set_text_fmt(chart_cur_lbl, "%.2f", cur_val);
    } else if (active_chart == ActiveChart::Ldr) {
        int min_val = ldr_history[0];
        int max_val = ldr_history[0];
        int cur_val = ldr_history[history_count - 1];
        for (uint8_t i = 1; i < history_count; ++i) {
            if (ldr_history[i] < min_val) min_val = ldr_history[i];
            if (ldr_history[i] > max_val) max_val = ldr_history[i];
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


static void add_history_point(float temp, bool heater_on, float ph, int ldr) {
    uint32_t current_heap = ESP.getFreeHeap();
    if (history_count < TEMP_HISTORY_POINTS) {
        temp_history[history_count] = temp;
        heater_history[history_count] = heater_on;
        ph_history[history_count] = ph;
        ldr_history[history_count] = ldr;
        heap_history[history_count] = current_heap;
        history_count++;
    } else {
        for (uint8_t i = 1; i < TEMP_HISTORY_POINTS; ++i) {
            temp_history[i - 1] = temp_history[i];
            heater_history[i - 1] = heater_history[i];
            ph_history[i - 1] = ph_history[i];
            ldr_history[i - 1] = ldr_history[i];
            heap_history[i - 1] = heap_history[i];
        }
        temp_history[TEMP_HISTORY_POINTS - 1] = temp;
        heater_history[TEMP_HISTORY_POINTS - 1] = heater_on;
        ph_history[TEMP_HISTORY_POINTS - 1] = ph;
        ldr_history[TEMP_HISTORY_POINTS - 1] = ldr;
        heap_history[TEMP_HISTORY_POINTS - 1] = current_heap;
    }
}


static void redraw_charts() {
    if (chart_temp == nullptr) return;
    
    // 1. Update Temperature Chart
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
            lv_chart_set_value_by_id(chart_temp, chart_temp_target_series, i, static_cast<lv_coord_t>(roundf(cfg.targetTemp * 10.0f)));
            lv_chart_set_value_by_id(chart_temp, chart_temp_upper_series, i, static_cast<lv_coord_t>(roundf((cfg.targetTemp + cfg.tempHysteresis) * 10.0f)));
            lv_chart_set_value_by_id(chart_temp, chart_temp_lower_series, i, static_cast<lv_coord_t>(roundf((cfg.targetTemp - cfg.tempHysteresis) * 10.0f)));
            lv_chart_set_value_by_id(chart_temp, chart_temp_heater_series, i, heater_history[i] ? (low_temp + (high_temp - low_temp) * 0.15) : low_temp);
        } else {
            lv_chart_set_value_by_id(chart_temp, chart_temp_series, i, LV_CHART_POINT_NONE);
            lv_chart_set_value_by_id(chart_temp, chart_temp_target_series, i, LV_CHART_POINT_NONE);
            lv_chart_set_value_by_id(chart_temp, chart_temp_upper_series, i, LV_CHART_POINT_NONE);
            lv_chart_set_value_by_id(chart_temp, chart_temp_lower_series, i, LV_CHART_POINT_NONE);
            lv_chart_set_value_by_id(chart_temp, chart_temp_heater_series, i, LV_CHART_POINT_NONE);
        }
    }
    
    // 2. Update pH Chart
    if (chart_ph != nullptr && chart_ph_series != nullptr) {
        float min_ph = ph_history[0];
        float max_ph = ph_history[0];
        for (uint8_t i = 1; i < history_count; ++i) {
            if (ph_history[i] < min_ph) min_ph = ph_history[i];
            if (ph_history[i] > max_ph) max_ph = ph_history[i];
        }
        int low_ph = static_cast<int>(floorf((min_ph - 0.2f) * 100.0f));
        int high_ph = static_cast<int>(ceilf((max_ph + 0.2f) * 100.0f));
        if (high_ph - low_ph < 20) high_ph = low_ph + 20;
        lv_chart_set_range(chart_ph, LV_CHART_AXIS_PRIMARY_Y, low_ph, high_ph);
        
        for (uint8_t i = 0; i < TEMP_HISTORY_POINTS; ++i) {
            if (i < history_count) {
                lv_chart_set_value_by_id(chart_ph, chart_ph_series, i, static_cast<lv_coord_t>(roundf(ph_history[i] * 100.0f)));
            } else {
                lv_chart_set_value_by_id(chart_ph, chart_ph_series, i, LV_CHART_POINT_NONE);
            }
        }
    }
    
    // 3. Update LDR Chart
    if (chart_ldr != nullptr && chart_ldr_series != nullptr) {
        int min_ldr = ldr_history[0];
        int max_ldr = ldr_history[0];
        for (uint8_t i = 1; i < history_count; ++i) {
            if (ldr_history[i] < min_ldr) min_ldr = ldr_history[i];
            if (ldr_history[i] > max_ldr) max_ldr = ldr_history[i];
        }
        int low_ldr = max(0, min_ldr - 20);
        int high_ldr = min(LDR_ADC_MAX, max_ldr + 20);
        if (high_ldr - low_ldr < 50) {
            high_ldr = min(LDR_ADC_MAX, low_ldr + 50);
            if (high_ldr - low_ldr < 50) {
                low_ldr = max(0, high_ldr - 50);
            }
        }
        lv_chart_set_range(chart_ldr, LV_CHART_AXIS_PRIMARY_Y, low_ldr, high_ldr);
        
        for (uint8_t i = 0; i < TEMP_HISTORY_POINTS; ++i) {
            if (i < history_count) {
                lv_chart_set_value_by_id(chart_ldr, chart_ldr_series, i, static_cast<lv_coord_t>(ldr_history[i]));
            } else {
                lv_chart_set_value_by_id(chart_ldr, chart_ldr_series, i, LV_CHART_POINT_NONE);
            }
        }
    }

    // 4. Update Heap Chart
    if (chart_heap != nullptr && chart_heap_series != nullptr) {
        uint32_t min_heap = heap_history[0];
        uint32_t max_heap = heap_history[0];
        for (uint8_t i = 1; i < history_count; ++i) {
            if (heap_history[i] < min_heap) min_heap = heap_history[i];
            if (heap_history[i] > max_heap) max_heap = heap_history[i];
        }
        int low_heap = static_cast<int>(min_heap / 1024) - 5;
        int high_heap = static_cast<int>(max_heap / 1024) + 5;
        if (low_heap < 0) low_heap = 0;
        if (high_heap - low_heap < 10) high_heap = low_heap + 10;
        lv_chart_set_range(chart_heap, LV_CHART_AXIS_PRIMARY_Y, low_heap, high_heap);
        
        for (uint8_t i = 0; i < TEMP_HISTORY_POINTS; ++i) {
            if (i < history_count) {
                lv_chart_set_value_by_id(chart_heap, chart_heap_series, i, static_cast<lv_coord_t>(heap_history[i] / 1024));
            } else {
                lv_chart_set_value_by_id(chart_heap, chart_heap_series, i, LV_CHART_POINT_NONE);
            }
        }
    }
    
    lv_chart_refresh(chart_temp);
    if (chart_ph != nullptr) lv_chart_refresh(chart_ph);
    if (chart_ldr != nullptr) lv_chart_refresh(chart_ldr);
    if (chart_heap != nullptr) lv_chart_refresh(chart_heap);
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
        if (chart_temp != nullptr) lv_obj_clear_flag(chart_temp, LV_OBJ_FLAG_HIDDEN);
        if (chart_ph != nullptr) lv_obj_add_flag(chart_ph, LV_OBJ_FLAG_HIDDEN);
        if (chart_ldr != nullptr) lv_obj_add_flag(chart_ldr, LV_OBJ_FLAG_HIDDEN);
        if (chart_heap != nullptr) lv_obj_add_flag(chart_heap, LV_OBJ_FLAG_HIDDEN);
        if (chart_target_lbl != nullptr) lv_obj_clear_flag(chart_target_lbl, LV_OBJ_FLAG_HIDDEN);
    } else if (selection == ActiveChart::Ph) {
        if (btn_chart_ph != nullptr) lv_obj_add_state(btn_chart_ph, LV_STATE_CHECKED);
        if (chart_temp != nullptr) lv_obj_add_flag(chart_temp, LV_OBJ_FLAG_HIDDEN);
        if (chart_ph != nullptr) lv_obj_clear_flag(chart_ph, LV_OBJ_FLAG_HIDDEN);
        if (chart_ldr != nullptr) lv_obj_add_flag(chart_ldr, LV_OBJ_FLAG_HIDDEN);
        if (chart_heap != nullptr) lv_obj_add_flag(chart_heap, LV_OBJ_FLAG_HIDDEN);
        if (chart_target_lbl != nullptr) lv_obj_add_flag(chart_target_lbl, LV_OBJ_FLAG_HIDDEN);
    } else if (selection == ActiveChart::Ldr) {
        if (btn_chart_ldr != nullptr) lv_obj_add_state(btn_chart_ldr, LV_STATE_CHECKED);
        if (chart_temp != nullptr) lv_obj_add_flag(chart_temp, LV_OBJ_FLAG_HIDDEN);
        if (chart_ph != nullptr) lv_obj_add_flag(chart_ph, LV_OBJ_FLAG_HIDDEN);
        if (chart_ldr != nullptr) lv_obj_clear_flag(chart_ldr, LV_OBJ_FLAG_HIDDEN);
        if (chart_heap != nullptr) lv_obj_add_flag(chart_heap, LV_OBJ_FLAG_HIDDEN);
        if (chart_target_lbl != nullptr) lv_obj_add_flag(chart_target_lbl, LV_OBJ_FLAG_HIDDEN);
    } else if (selection == ActiveChart::Heap) {
        if (btn_chart_heap != nullptr) lv_obj_add_state(btn_chart_heap, LV_STATE_CHECKED);
        if (chart_temp != nullptr) lv_obj_add_flag(chart_temp, LV_OBJ_FLAG_HIDDEN);
        if (chart_ph != nullptr) lv_obj_add_flag(chart_ph, LV_OBJ_FLAG_HIDDEN);
        if (chart_ldr != nullptr) lv_obj_add_flag(chart_ldr, LV_OBJ_FLAG_HIDDEN);
        if (chart_heap != nullptr) lv_obj_clear_flag(chart_heap, LV_OBJ_FLAG_HIDDEN);
        if (chart_target_lbl != nullptr) lv_obj_add_flag(chart_target_lbl, LV_OBJ_FLAG_HIDDEN);
    }
    
    update_chart_stats();
}


static void update_charts_data(float temp, float ph) {
    if (!isfinite(temp)) {
        return;
    }
    const float chart_ph = isfinite(ph) ? ph : (isfinite(runtime.lastPh) ? runtime.lastPh : 7.20f);
    add_history_point(temp, runtime.heaterOn, chart_ph, last_ldr_value);
    redraw_charts();
    update_chart_stats();
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
    screen_dev_mode_sw = nullptr;
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
    power_warning_lbl_global = nullptr;
    power_state_lbl = nullptr;
    pin_overlay = nullptr;
    pin_value_lbl = nullptr;
    pin_status_lbl = nullptr;
    pin_matrix = nullptr;
    pin_entry[0] = '\0';
    memset(&time_picker_state, 0, sizeof(time_picker_state));
    memset(&date_picker_state, 0, sizeof(date_picker_state));
    screen_always_on_sw = nullptr;
    screen_manual_theme_sw = nullptr;
    screen_ph_enable_sw = nullptr;
    screen_ldr_enable_sw = nullptr;
    screen_ldr_slider = nullptr;
    screen_ldr_value_lbl = nullptr;
    screen_ldr_raw_lbl = nullptr;
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

    build_status_bar();
    for (uint8_t i = 0; i < PAGE_COUNT; ++i) {
        add_page_base(i);
    }
    Serial.println("DEBUG: build_home_page"); build_home_page();
    Serial.println("DEBUG: build_schedules_page"); build_schedules_page();
    Serial.println("DEBUG: build_optional_page"); build_optional_page();
    Serial.println("DEBUG: build_charts_page"); build_charts_page();
    Serial.println("DEBUG: build_system_page"); build_system_page();
    Serial.println("DEBUG: build_nav_bar"); build_nav_bar();
    Serial.println("DEBUG: build_subpages"); build_subpages();

    const esp_reset_reason_t reason = esp_reset_reason();
    if (diag_reset_reason_lbl != nullptr) {
        lv_label_set_text_fmt(diag_reset_reason_lbl, "Reset reason: %u", static_cast<unsigned>(reason));
    }
}





// build_splash_screen() removed — caused LoadProhibited crash due to heap exhaustion
// after build_gui_tree() leaves only ~44 bytes of contiguous free heap.




static void rebuild_gui_tree_for_theme() {
    lv_obj_clean(lv_scr_act());
    reset_gui_object_refs();
    build_gui_tree();
    prime_pin_guard_modal();

    // Przywrócenie aktywnej strony (zakładki)
    for (uint8_t i = 0; i < PAGE_COUNT; ++i) {
        if (pages[i] != nullptr && nav_btns[i] != nullptr) {
            if (i == current_page_index) {
                lv_obj_clear_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_state(nav_btns[i], LV_STATE_CHECKED);
            } else {
                lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_state(nav_btns[i], LV_STATE_CHECKED);
            }
        }
    }

    // Przywrócenie aktywnej podstrony
    sync_nav_bar_visuals();

    if (current_subpage != ActiveSubpage::None) {
        lv_obj_t *target_subpage = nullptr;
        switch (current_subpage) {
            case ActiveSubpage::Wifi: target_subpage = subpage_wifi; break;
            case ActiveSubpage::Screen: target_subpage = subpage_screen; break;
            case ActiveSubpage::Logs: target_subpage = subpage_logs; break;
            case ActiveSubpage::Clock: target_subpage = subpage_clock; break;
            case ActiveSubpage::Diagnostics: target_subpage = subpage_diagnostics; break;
            case ActiveSubpage::Power: target_subpage = subpage_power; break;
            case ActiveSubpage::Sounds: target_subpage = subpage_sounds; break;
            case ActiveSubpage::FeedEditor: target_subpage = subpage_feed_editor; break;
            case ActiveSubpage::SchedEditor: target_subpage = subpage_sched_editor; break;
            case ActiveSubpage::Heater: target_subpage = subpage_heater; break;
            case ActiveSubpage::Ph: target_subpage = subpage_ph; break;
            case ActiveSubpage::Service: target_subpage = subpage_service; break;
            case ActiveSubpage::Hardware:
                if (subpage_hardware == nullptr) {
                    if (!ensure_runtime_ui_heap("Hardware", UI_RUNTIME_HARDWARE_MIN_FREE, UI_RUNTIME_HARDWARE_MIN_LARGEST)) {
                        break;
                    }
                    build_hardware_subpage();
                }
                target_subpage = subpage_hardware;
                break;
            case ActiveSubpage::Co2:
                if (subpage_co2 == nullptr) {
                    if (!ensure_runtime_ui_heap("CO2", UI_RUNTIME_HARDWARE_MIN_FREE, UI_RUNTIME_HARDWARE_MIN_LARGEST)) {
                        break;
                    }
                    build_co2_subpage();
                }
                target_subpage = subpage_co2;
                break;
            case ActiveSubpage::Ec:
                if (subpage_ec == nullptr) {
                    if (!ensure_runtime_ui_heap("EC", UI_RUNTIME_HARDWARE_MIN_FREE, UI_RUNTIME_HARDWARE_MIN_LARGEST)) {
                        break;
                    }
                    build_ec_subpage();
                }
                target_subpage = subpage_ec;
                break;
            case ActiveSubpage::WaterLevel:
                if (subpage_water == nullptr) {
                    if (!ensure_runtime_ui_heap("WaterLevel", UI_RUNTIME_HARDWARE_MIN_FREE, UI_RUNTIME_HARDWARE_MIN_LARGEST)) {
                        break;
                    }
                    build_water_subpage();
                }
                target_subpage = subpage_water;
                break;
            case ActiveSubpage::Leak:
                if (subpage_leak == nullptr) {
                    if (!ensure_runtime_ui_heap("Leak", UI_RUNTIME_HARDWARE_MIN_FREE, UI_RUNTIME_HARDWARE_MIN_LARGEST)) {
                        break;
                    }
                    build_leak_subpage();
                }
                target_subpage = subpage_leak;
                break;
            case ActiveSubpage::Flow:
                if (subpage_flow == nullptr) {
                    if (!ensure_runtime_ui_heap("Flow", UI_RUNTIME_HARDWARE_MIN_FREE, UI_RUNTIME_HARDWARE_MIN_LARGEST)) {
                        break;
                    }
                    build_flow_subpage();
                }
                target_subpage = subpage_flow;
                break;
            default: break;
        }
        if (target_subpage != nullptr) {
            lv_obj_clear_flag(target_subpage, LV_OBJ_FLAG_HIDDEN);
        }
    }

    gui_sync_widgets_to_state();
    gui_app_update_wifi(wifi_connected ? 1 : 0, wifi_rssi);
    redraw_charts();
    update_chart_stats();
}

} // namespace

void gui_app_init(void) {
    gui_app_load_settings();
    if (cfg.modemSleep) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        wifi_connected = false;
        wifi_rssi = 0;
        Serial.println("GUI: Modem Sleep active on boot, Wi-Fi radio disabled.");
    }
    if (cfg.ldrThemeEnabled) {
        // Read LDR value immediately to set the correct theme before building UI
        // High LDR value = bright ambient light = dark display theme (reduce glare)
        // Low LDR value = dark ambient = light display theme (easier to see)
        int ldr_val = analogRead(34);
        int threshold = map(cfg.ldrSensitivity, 0, 100, 200, 0);
        ui_light_theme = (ldr_val < threshold); // Inverted: dark room = light screen
        last_ldr_value = ldr_val;
    } else {
        ui_light_theme = cfg.manualLightTheme;
    }

    reset_gui_object_refs();
    build_gui_tree();
    gui_sync_widgets_to_state();
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



    if (wifi_check_timer == nullptr) {
        wifi_check_timer = lv_timer_create(wifi_check_timer_cb, 500, nullptr);
    }

    if (musicTaskHandle == nullptr) {
        xTaskCreate(
            music_player_task,
            "music_player_task",
            4096,
            nullptr,
            1,
            &musicTaskHandle
        );
    }
}

void gui_app_update_wifi(int state, int rssi) {
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
        lv_label_set_text(label_wifi_state, "OFF");
        lv_obj_set_style_text_color(label_wifi_state, lv_color_make(239, 68, 68), 0);
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
            lv_label_set_text_fmt(wifi_mac_lbl, "MAC: %s", WiFi.macAddress().c_str());
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
        lv_label_set_text(label_wifi_state, "STA");
        lv_obj_set_style_text_color(label_wifi_state, lv_color_make(16, 185, 129), 0);
        if (wifi_info_card != nullptr) {
            lv_obj_set_style_border_color(wifi_info_card, lv_color_make(16, 185, 129), 0);
        }
        if (wifi_mode_lbl != nullptr) {
            lv_label_set_text(wifi_mode_lbl, "POLACZONY");
            lv_obj_set_style_text_color(wifi_mode_lbl, lv_color_make(16, 185, 129), 0);
        }

        String current_ssid = WiFi.SSID();
        String current_ip = WiFi.localIP().toString();

        if (wifi_ssid_lbl != nullptr) {
            char temp_ssid_buf[96];
            snprintf(temp_ssid_buf, sizeof(temp_ssid_buf), LV_SYMBOL_WIFI "  SSID: %s", current_ssid.length() > 0 ? current_ssid.c_str() : (selected_ssid[0] != '\0' ? selected_ssid : "Aquarium_STA"));
            lv_label_set_text(wifi_ssid_lbl, temp_ssid_buf);
        }

        if (wifi_ip_lbl != nullptr) {
            char temp_ip_buf[64];
            snprintf(temp_ip_buf, sizeof(temp_ip_buf), LV_SYMBOL_RIGHT "  IP: %s", current_ip.c_str());
            lv_label_set_text(wifi_ip_lbl, temp_ip_buf);
        }
        if (wifi_rssi_lbl != nullptr) {
            lv_label_set_text_fmt(wifi_rssi_lbl, "RSSI: %d dBm", rssi);
        }
        if (wifi_mac_lbl != nullptr) {
            lv_label_set_text_fmt(wifi_mac_lbl, "MAC: %s", WiFi.macAddress().c_str());
        }

        if (btn_sta != nullptr) lv_obj_add_flag(btn_sta, LV_OBJ_FLAG_HIDDEN);
        if (btn_ota != nullptr) lv_obj_add_flag(btn_ota, LV_OBJ_FLAG_HIDDEN);
        if (btn_disconnect != nullptr) lv_obj_clear_flag(btn_disconnect, LV_OBJ_FLAG_HIDDEN);

        if (wifi_status_message_lbl != nullptr) {
            lv_label_set_text(wifi_status_message_lbl, "Polaczono z siecia");
            lv_obj_set_style_text_color(wifi_status_message_lbl, lv_color_make(16, 185, 129), 0);
        }
    } else {
        lv_label_set_text(label_wifi_state, "AP");
        lv_obj_set_style_text_color(label_wifi_state, lv_color_make(6, 182, 212), 0);
        if (wifi_mode_lbl != nullptr) {
            lv_label_set_text(wifi_mode_lbl, "AP");
            lv_obj_set_style_text_color(wifi_mode_lbl, lv_color_make(6, 182, 212), 0);
        }

        String soft_ssid = WiFi.softAPSSID();
        String soft_ip = WiFi.softAPIP().toString();

        if (wifi_ssid_lbl != nullptr) {
            char temp_ssid_buf[96];
            snprintf(temp_ssid_buf, sizeof(temp_ssid_buf), "SSID: %s", soft_ssid.length() > 0 ? soft_ssid.c_str() : "cydAquarium-OTA");
            lv_label_set_text(wifi_ssid_lbl, temp_ssid_buf);
        }

        if (wifi_ip_lbl != nullptr) {
            char temp_ip_buf[64];
            snprintf(temp_ip_buf, sizeof(temp_ip_buf), "IP: %s", soft_ip.c_str());
            lv_label_set_text(wifi_ip_lbl, temp_ip_buf);
        }
        if (wifi_rssi_lbl != nullptr) {
            lv_label_set_text(wifi_rssi_lbl, "Sygnal: AP");
        }
        if (wifi_mac_lbl != nullptr) {
            lv_label_set_text_fmt(wifi_mac_lbl, "MAC: %s", WiFi.softAPmacAddress().c_str());
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
}


void gui_update_metrics(float temp, float ph, uint32_t free_heap, const char *time_str) {
    const bool temp_valid = isfinite(temp);
    const bool ph_valid = isfinite(ph);
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

    runtime.lightOn = schedule_active(cfg.lightMode, nowMins, cfg.lightStartHour, cfg.lightStartMinute, cfg.lightEndHour, cfg.lightEndMinute);
    runtime.lightActiveMode = cfg.lightMode == static_cast<uint8_t>(ScheduleMode::Schedule) ? cfg.lightSchedColorMode : cfg.lightColorMode;
    runtime.plantLightOn = schedule_active(cfg.plantLightMode, nowMins, cfg.plantStartHour, cfg.plantStartMinute, cfg.plantEndHour, cfg.plantEndMinute);
    runtime.plantLightActiveMode = cfg.plantLightMode == static_cast<uint8_t>(ScheduleMode::Schedule) ? cfg.plantSchedColorMode : cfg.plantLightColorMode;
    runtime.filterOn = schedule_active(cfg.filterMode, nowMins, cfg.filterStartHour, cfg.filterStartMinute, cfg.filterEndHour, cfg.filterEndMinute);
    runtime.airOn = schedule_active(cfg.airMode, nowMins, cfg.airStartHour, cfg.airStartMinute, cfg.airEndHour, cfg.airEndMinute);

    bool old_heater_on = runtime.heaterOn;
    if (cfg.heaterMode == static_cast<uint8_t>(HeaterMode::Off) || !isfinite(temp)) {
        runtime.heaterOn = false;
    } else if (temp < cfg.targetTemp - cfg.tempHysteresis) {
        runtime.heaterOn = true;
    } else if (temp >= cfg.targetTemp) {
        runtime.heaterOn = false;
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

    int wday = get_weekday(clock_day, clock_month, clock_year);
    int bit_idx = (wday == 0) ? 6 : (wday - 1);
    bool day_active = (cfg.feedDays & (1 << bit_idx)) != 0;

    if (cfg.feedEnabled && day_active && sc == 0) {
        bool time_match = (hr == cfg.feedHour1 && mn == cfg.feedMinute1) ||
                          (cfg.feedCount == 2 && hr == cfg.feedHour2 && mn == cfg.feedMinute2);
        if (time_match) {
            const uint32_t nowMs = millis();
            if (runtime.lastAutoFeedMs == 0 || nowMs - runtime.lastAutoFeedMs > 60000UL) {
                runtime.lastAutoFeedMs = nowMs;
                show_feeder_modal("Scheduled feeding", "Auto dose triggered");
                lv_timer_create(close_feeder_modal_cb, 3000, nullptr);
                Serial.println("GUI: Scheduled feeding triggered.");
            }
        }
    }

    if (label_date != nullptr && time_str != nullptr) {
        char full_time[32];
        static const char *months[] = {
            "Jan", "Feb", "Mar", "Apr", "May", "Jun",
            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
        };
        const char *month_name = (clock_month >= 1 && clock_month <= 12) ? months[clock_month - 1] : "May";
        snprintf(full_time, sizeof(full_time), "%d %s %s", clock_day, month_name, time_str);
        lv_label_set_text(label_date, full_time);
    }
    if (label_power_mode != nullptr) {
        if (temp_valid) {
            lv_label_set_text_fmt(label_power_mode, "%.1f*C", temp);
        } else {
            lv_label_set_text(label_power_mode, "--.-*C");
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
    update_charts_data(temp, ph);
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

void gui_app_update_system_info(uint32_t free_heap, uint32_t uptime_sec) {
    if (diag_heap_lbl != nullptr) {
        lv_label_set_text_fmt(diag_heap_lbl, "Free heap: %.1f KB", static_cast<float>(free_heap) / 1024.0f);
    }

    if (diag_restarts_lbl != nullptr) {
        lv_label_set_text_fmt(diag_restarts_lbl, "Boot count: %lu", static_cast<unsigned long>(boot_count_val));
    }

    if (diag_cpu_temp_lbl != nullptr) {
        #ifdef ESP32
        float cpu_temp = temperatureRead();
        if (cpu_temp > 0.0f) {
            lv_label_set_text_fmt(diag_cpu_temp_lbl, "CPU Temp: %.1f *C", cpu_temp);
        } else {
            lv_label_set_text(diag_cpu_temp_lbl, "CPU Temp: 42.5 *C");
        }
        #else
        lv_label_set_text(diag_cpu_temp_lbl, "CPU Temp: 42.5 *C");
        #endif
    }

    if (diag_cpu_freq_lbl != nullptr) {
        lv_label_set_text_fmt(diag_cpu_freq_lbl, "CPU Freq: %d MHz", ESP.getCpuFreqMHz());
    }

    if (diag_flash_lbl != nullptr) {
        lv_label_set_text_fmt(diag_flash_lbl, "Flash size: %d MB", ESP.getFlashChipSize() / (1024 * 1024));
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
        lv_label_set_text_fmt(diag_reset_reason_lbl, "Reason: %s", reason_str);
    }

    if (diag_uptime_lbl != nullptr) {
        const uint32_t days = uptime_sec / 86400UL;
        const uint32_t hours = (uptime_sec % 86400UL) / 3600UL;
        const uint32_t minutes = (uptime_sec % 3600UL) / 60UL;
        const uint32_t seconds = uptime_sec % 60UL;
        if (days > 0) {
            lv_label_set_text_fmt(diag_uptime_lbl, "Uptime: %lud %luh %lum",
                                  static_cast<unsigned long>(days),
                                  static_cast<unsigned long>(hours),
                                  static_cast<unsigned long>(minutes));
        } else if (hours > 0) {
            lv_label_set_text_fmt(diag_uptime_lbl, "Uptime: %luh %lum %lus",
                                  static_cast<unsigned long>(hours),
                                  static_cast<unsigned long>(minutes),
                                  static_cast<unsigned long>(seconds));
        } else {
            lv_label_set_text_fmt(diag_uptime_lbl, "Uptime: %lum %lus",
                                  static_cast<unsigned long>(minutes),
                                  static_cast<unsigned long>(seconds));
        }
    }
}

void gui_app_update_sensor_debug(int ldr_value,
                                 bool adc_present,
                                 bool ph_valid,
                                 int16_t ph_raw,
                                 float ph_voltage,
                                 bool ec_valid,
                                 int16_t ec_raw,
                                 float ec_voltage,
                                 bool mcp_present,
                                 bool mcp_valid,
                                 uint16_t mcp_state) {
    sensor_debug.ldrValue = ldr_value;
    sensor_debug.adcPresent = adc_present;
    sensor_debug.phValid = ph_valid;
    sensor_debug.phRaw = ph_raw;
    sensor_debug.phVoltage = ph_voltage;
    sensor_debug.ecValid = ec_valid;
    sensor_debug.ecRaw = ec_raw;
    sensor_debug.ecVoltage = ec_voltage;
    sensor_debug.mcpPresent = mcp_present;
    sensor_debug.mcpValid = mcp_valid;
    sensor_debug.mcpState = mcp_state;
    sensor_debug.updatedMs = millis();
    update_calibration_value_label();

    auto mcp_bit = [mcp_valid, mcp_state](HwConfig::McpChannel channel) {
        if (!mcp_valid) {
            return false;
        }
        const uint16_t bit = static_cast<uint16_t>(1U << static_cast<uint8_t>(channel));
        return (mcp_state & bit) != 0;
    };

    if (diag_adc_lbl != nullptr) {
        if (adc_present) {
            lv_label_set_text_fmt(diag_adc_lbl, "ADS1115: OK  pH:%s EC:%s",
                                  ph_valid ? "OK" : "--",
                                  ec_valid ? "OK" : "--");
        } else {
            lv_label_set_text(diag_adc_lbl, "ADS1115: brak");
        }
    }
    if (diag_mcp_lbl != nullptr) {
        if (mcp_present && mcp_valid) {
            lv_label_set_text_fmt(diag_mcp_lbl, "MCP23017: OK  maska 0x%04X", static_cast<unsigned>(mcp_state));
        } else if (mcp_present) {
            lv_label_set_text(diag_mcp_lbl, "MCP23017: blad odczytu");
        } else {
            lv_label_set_text(diag_mcp_lbl, "MCP23017: brak");
        }
    }
    if (diag_queue_lbl != nullptr) {
        lv_label_set_text_fmt(diag_queue_lbl, "Kolejka overflow: %lu",
                              static_cast<unsigned long>(events_sample_overflow_count()));
    }
    if (diag_ldr_lbl != nullptr) {
        lv_label_set_text_fmt(diag_ldr_lbl, "LDR GPIO34: %d", ldr_value);
    }

    if (co2_state_lbl != nullptr) {
        const bool co2_on = mcp_bit(HwConfig::CH_CO2);
        lv_label_set_text(co2_state_lbl, mcp_valid ? (co2_on ? "Stan: ON" : "Stan: OFF") : "Stan: --");
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

    const bool water_raw = mcp_bit(HwConfig::CH_WATER_LEVEL);
    const bool leak_raw = mcp_bit(HwConfig::CH_LEAK);
    const bool flow_raw = mcp_bit(HwConfig::CH_FLOW_PULSE);
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
        lv_label_set_text(device_co2_detail_lbl, mcp_valid ? (mcp_bit(HwConfig::CH_CO2) ? "Zawor ON" : "Zawor OFF") : "MCP --");
    }
}

void gui_app_update_ldr(int ldr_value) {
    if (screen_ldr_raw_lbl != nullptr) {
        lv_label_set_text_fmt(screen_ldr_raw_lbl, "LDR ADC: %d", ldr_value);
    }
    
    if (!cfg.ldrThemeEnabled) return;
    
    // 0% czułości = próg 200 (najmniej czuły), 100% czułości = próg 0 (najbardziej czuły)
    // Logika: dużo światła (LDR wysoki) -> ciemny motyw; mało światła (LDR niski) -> jasny motyw
    int threshold = map(cfg.ldrSensitivity, 0, 100, 200, 0);
    
    bool should_be_light = (ldr_value < threshold); // Odwrócone: jasno na zewnątrz = ciemny ekran
    
    static int consecutive_diff_count = 0;
    static bool pending_state = false;
    
    if (should_be_light != ui_light_theme) {
        if (consecutive_diff_count == 0) {
            pending_state = should_be_light;
            consecutive_diff_count = 1;
        } else if (should_be_light == pending_state) {
            consecutive_diff_count++;
            if (consecutive_diff_count >= 5) { // Czekaj 5 sekund (5 odczytów co 1 sekunda)
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
    last_ldr_value = ldr_value;
}

bool gui_app_is_dev_mode(void) {
    return cfg.devMode;
}
