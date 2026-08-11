#ifndef AQUACYD_HMI_UI_H
#define AQUACYD_HMI_UI_H

#include <stdint.h>

struct HmiSchedule {
    uint8_t mode;
    uint8_t profile;
    uint16_t start_minute;
    uint16_t end_minute;
};

struct HmiSnapshot {
    bool controller_online;
    bool temperature_valid;
    bool ph_valid;
    bool ec_valid;
    double temperature_c;
    double ph;
    double ec_us_cm;
    int ldr_raw;
    uint32_t alarm_flags;
    int espnow_rssi_dbm;
    uint32_t configuration_revision;
    bool configuration_valid;
    double target_temperature_c;
    double temperature_hysteresis_c;
    uint8_t heater_mode;
    HmiSchedule light_primary_schedule;
    HmiSchedule light_secondary_schedule;
    HmiSchedule filter_schedule;
    HmiSchedule aerator_schedule;
    uint32_t controller_uptime_seconds;
    uint32_t controller_free_heap_bytes;
    bool water_level_low;
    bool leak_detected;
    bool controller_safe;
    bool light_primary_on;
    bool light_secondary_on;
    bool filter_on;
    bool aerator_on;
    bool heater_on;
};

struct HmiCommandRequest {
    const char *action;
    const char *target;
    int32_t value;
    uint32_t duration_ms;
};

struct HmiHubSummary {
    uint16_t device_count;
    uint16_t online_device_count;
    uint16_t entity_count;
    uint16_t writable_entity_count;
    uint16_t api_port;
    uint16_t broker_port;
    uint32_t pairing_code;
    uint32_t pairing_seconds_remaining;
    uint32_t free_heap_bytes;
    bool api_running;
    bool broker_running;
    char tls_fingerprint[65];
};

enum class HmiFeedbackKind : uint8_t {
    Information = 0U,
    Success,
    Warning,
    Error
};

using HmiCommandCallback =
    void (*)(const HmiCommandRequest &request, void *context);
using HmiBrightnessCallback =
    void (*)(uint8_t brightness, bool persist, void *context);

struct HmiUiCallbacks {
    HmiCommandCallback command;
    HmiBrightnessCallback brightness;
    void *context;
};

/**
 * Creates the complete 1024x600 interface. The caller must hold the LVGL lock.
 * All callbacks are executed from the LVGL task and must return quickly.
 */
bool hmi_ui_create(const HmiUiCallbacks &callbacks,
                   uint8_t initial_brightness);

/** Updates widgets from a coherent controller snapshot. */
void hmi_ui_apply_snapshot(const HmiSnapshot &snapshot,
                           uint32_t received_at_ms);

/** Updates the local AquaHub service, pairing and registry diagnostics. */
void hmi_ui_apply_hub_summary(const HmiHubSummary &summary);

/** Updates link chips and the global command interlock. */
void hmi_ui_set_connectivity(bool wifi_connected,
                             bool mqtt_connected,
                             bool controller_online);

/** Shows the non-blocking pending state after a command is queued. */
void hmi_ui_set_command_pending(const char *message);

/** Clears the pending state and displays the final command result. */
void hmi_ui_set_command_result(HmiFeedbackKind kind, const char *message);

/** Displays a transient notification without changing command state. */
void hmi_ui_show_toast(HmiFeedbackKind kind, const char *message);

/**
 * Performs age-dependent updates. Call from an LVGL timer every 100-250 ms.
 */
void hmi_ui_tick(uint32_t now_ms, uint32_t hmi_free_heap_bytes);

#endif
