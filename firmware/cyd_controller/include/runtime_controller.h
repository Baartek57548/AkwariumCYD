#ifndef AQUARIUM_RUNTIME_CONTROLLER_H
#define AQUARIUM_RUNTIME_CONTROLLER_H

#include <Arduino.h>

struct RuntimeTelemetry {
    uint32_t sequence;
    uint32_t sampled_ms;
    uint32_t uptime_seconds;
    uint32_t free_heap_bytes;

    float temperature_c;
    bool temperature_present;
    bool temperature_valid;
    bool temperature_stale;
    uint32_t temperature_age_ms;
    uint32_t temperature_error_count;

    int ldr_value;
    bool ldr_valid;

    bool adc_present;
    bool ph_valid;
    int16_t ph_raw;
    float ph_voltage;
    float ph_value;
    bool ec_valid;
    int16_t ec_raw;
    float ec_voltage;
    float ec_value;

    bool mcp_present;
    bool mcp_valid;
    uint16_t mcp_state;

    uint8_t wifi_state;
    int16_t wifi_rssi;
};

/**
 * @brief Ustawia wyjścia w stan bezpieczny i inicjalizuje fizyczne magistrale.
 *
 * Funkcja jest wywoływana przed animacją startową, aby błąd ekranu lub karty SD
 * nie pozostawił przekaźników w nieokreślonym stanie.
 */
bool runtime_controller_prepare_hardware(void);

/**
 * @brief Uruchamia zadanie I/O przypięte do rdzenia 0.
 */
bool runtime_controller_start(void);

/**
 * @brief Pobiera najnowszą spójną ramkę telemetrii bez oczekiwania.
 */
bool runtime_controller_take_latest(RuntimeTelemetry *out);

/**
 * Verifies that the Core 0 task is alive and still publishes telemetry.
 * Missing optional sensors do not make the firmware unhealthy.
 */
bool runtime_controller_is_healthy(uint32_t now_ms,
                                   uint32_t maximum_heartbeat_age_ms);

/** Returns the last Core 0 heartbeat without applying a health policy. */
uint32_t runtime_controller_last_heartbeat_ms(void);

#endif // AQUARIUM_RUNTIME_CONTROLLER_H
