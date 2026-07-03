#ifndef GUI_APP_H
#define GUI_APP_H

#include <Arduino.h>

/**
 * @brief Inicjalizuje i tworzy układ graficzny LVGL (Status Bar + Zakładki).
 */
void gui_app_init(void);

void gui_app_handle_ota_portal(void);

/**
 * @brief Aktualizuje stan połączenia Wi-Fi na pasku statusu przy użyciu tekstów (STA, AP, OFF).
 * @param state 0=OFF, 1=STA, 2=AP.
 * @param rssi Siła sygnału w dBm.
 */
void gui_app_update_wifi(int state, int rssi);

/**
 * @brief Skonsolidowana funkcja aktualizacji metryk interfejsu (wywoływana co 1s).
 * @param temp Temperatura w stopniach Celsjusza.
 * @param ph Wartość odczynu pH.
 * @param free_heap Rozmiar wolnej pamięci RAM w bajtach.
 * @param time_str Czas w formacie tekstowym "HH:MM:SS".
 */
void gui_update_metrics(float temp, float ph, uint32_t free_heap, const char *time_str);

/**
 * @brief Aktualizuje automatyke motywu na podstawie odczytu LDR.
 * @param ldr_value Surowy odczyt ADC z GPIO 34 w zakresie 0..4095.
 */
void gui_app_update_ldr(int ldr_value, bool valid = true);

/**
 * @brief Aktualizuje czas działania urządzenia w sekcji systemowej.
 * @param free_heap Rozmiar wolnej pamięci RAM (heap) w bajtach.
 * @param uptime_sec Czas pracy w sekundach.
 */
void gui_app_update_system_info(uint32_t free_heap, uint32_t uptime_sec);

/**
 * @brief Aktualizuje panel deweloperski i moduly opcjonalne rzeczywistymi odczytami HAL.
 */
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
                                 uint16_t mcp_state);

/**
 * @brief Sprawdza czy tryb deweloperski jest aktywny.
 * @return true jesli wlaczony, false w przeciwnym wypadku.
 */
bool gui_app_is_dev_mode(void);

/**
 * @brief Informuje, czy panel WWW jest aktywnie uzywany i lokalny ekran jest w trybie WiFi-focus.
 */
bool gui_app_is_web_focus_active(void);

struct GuiBleSnapshot {
    uint8_t protocol_version;
    bool developer_mode;
    uint32_t uptime_seconds;
    uint32_t free_heap_bytes;
    float temperature;
    bool temperature_valid;
    float target_temperature;
    float ph;
    bool ph_valid;
    float ec;
    bool ec_valid;
    int ldr;
    bool ldr_valid;
    uint16_t alarm_flags;
    bool water_level_high;
    bool leak_detected;
    bool light_on;
    bool plant_light_on;
    bool filter_on;
    bool heater_on;
    bool aeration_on;
};

struct GuiBleCommandResult {
    bool success;
    const char *code;
    const char *message;
};

/**
 * @brief Tworzy spójny, stałorozmiarowy obraz telemetrii dla transportu BLE.
 * @return false przed zakończeniem inicjalizacji konfiguracji GUI.
 */
bool gui_app_ble_snapshot(GuiBleSnapshot *out);

/**
 * @brief Zmienia bezpieczny podzbiór wyjść sterownika po autoryzacji PIN-em.
 */
GuiBleCommandResult gui_app_ble_set_output(const char *target, bool state, const char *pin);

/**
 * @brief Uruchamia pojedynczą dawkę karmnika po autoryzacji PIN-em.
 */
GuiBleCommandResult gui_app_ble_feed(const char *pin);

#endif // GUI_APP_H
