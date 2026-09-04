#ifndef GUI_APP_H
#define GUI_APP_H

#include <Arduino.h>

/**
 * @brief Inicjalizuje rekurencyjną blokadę chroniącą LVGL i wspólny stan GUI.
 *
 * Funkcję należy wywołać przed lv_init() i przed uruchomieniem zadań
 * komunikacyjnych. Wszystkie wywołania LVGL spoza właściciela UI muszą używać
 * tej samej blokady.
 */
bool gui_app_sync_init(void);

/** @brief Przejmuje blokadę GUI na maksymalnie timeout_ms. */
bool gui_app_lock(uint32_t timeout_ms);

/** @brief Zwalnia blokadę przejętą przez bieżące zadanie. */
void gui_app_unlock(void);

/**
 * @brief Inicjalizuje i tworzy układ graficzny LVGL (Status Bar + Zakładki).
 */
void gui_app_init(void);

/**
 * @brief Obsługuje Wi-Fi, HTTP, OTA i nieblokujące sekwencje wyjść.
 *
 * Funkcja jest przeznaczona dla zadania I/O przypiętego do rdzenia 0.
 */
void gui_app_service_background(void);

void gui_app_handle_ota_portal(void);

/**
 * @brief Aktualizuje stan połączenia Wi-Fi na pasku statusu przy użyciu tekstów (STA, AP, OFF).
 * @param state 0=OFF, 1=STA, 2=AP.
 * @param rssi Siła sygnału w dBm.
 */
void gui_app_update_wifi(int state, int rssi);

/**
 * Publishes the BLE pairing state from Core 0 without touching LVGL there.
 * The UI task renders the six-digit passkey and result on its next refresh.
 *
 * @param passkey Six-digit code for state=1, otherwise ignored.
 * @param state 0=idle, 1=display passkey, 2=paired, 3=failed.
 */
void gui_app_update_ble_pairing(uint32_t passkey, uint8_t state);

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
 * @brief Odświeża wyłącznie kompaktowy pasek czasu, IP, wieku danych i uptime.
 */
void gui_app_update_status_bar(uint32_t uptime_sec);

/**
 * @brief Aktualizuje panel deweloperski i moduly opcjonalne rzeczywistymi odczytami HAL.
 */
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

/**
 * Returns true only after configuration and the essential LVGL tree are
 * initialized. Used by the post-OTA health gate before marking an image valid.
 */
bool gui_app_runtime_ready(void);

/** OTA health gate: validates hardware required by the active configuration. */
bool gui_app_ota_health_ready(void);

struct GuiScheduleSnapshot {
    uint8_t mode;
    uint8_t profile;
    uint16_t start_minute;
    uint16_t end_minute;
};

struct GuiBleSnapshot {
    uint8_t protocol_version;
    bool developer_mode;
    uint32_t uptime_seconds;
    uint32_t free_heap_bytes;
    uint32_t configuration_revision;
    float temperature;
    bool temperature_valid;
    float target_temperature;
    float temperature_hysteresis;
    uint8_t heater_mode;
    GuiScheduleSnapshot light_schedule;
    GuiScheduleSnapshot plant_light_schedule;
    GuiScheduleSnapshot filter_schedule;
    GuiScheduleSnapshot aeration_schedule;
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

struct GuiV2AuthResult {
    bool success;
    const char *code;
    const char *message;
    uint32_t expires_in_seconds;
    uint32_t retry_after_seconds;
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

/**
 * @brief Serializuje kompletny status sterownika w formacie zgodnym z API WWW.
 *
 * Funkcja nie alokuje pamieci dynamicznie. Zwraca false, gdy bufor jest zbyt
 * maly albo konfiguracja sterownika nie zostala jeszcze zainicjalizowana.
 */
bool gui_app_ble_full_status_json(char *out, size_t out_size);

/** @brief Serializuje logi panelu WWW do komunikatu protokolu BLE v2. */
bool gui_app_ble_logs_json(char *out, size_t out_size, const char *pin);

/** @brief Serializuje podstawowa diagnostyke magistral do BLE v2. */
bool gui_app_ble_diagnostics_json(char *out, size_t out_size, const char *pin);

/**
 * @brief Wykonuje akcje panelu WWW przeslana jako JSON protokolu BLE v2.
 *
 * @param action Nazwa akcji zgodna z endpointem /api/action.
 * @param command_json Pelna komenda JSON zawierajaca obiekt args.
 * @param pin PIN administratora.
 */
GuiBleCommandResult gui_app_ble_action(const char *action,
                                       const char *command_json,
                                       const char *pin);

/** @brief Issues a bounded, short-lived admin token after PIN rate limiting. */
GuiV2AuthResult gui_app_v2_auth(const char *pin,
                                char *out_token,
                                size_t out_token_size);

/**
 * Executes an idempotent protocol-v2 action using a valid short-lived token.
 * Legacy protocol-v1 entry points remain available separately with PIN auth.
 */
GuiBleCommandResult gui_app_v2_action(const char *action,
                                      const char *command_json,
                                      const char *pin,
                                      const char *token,
                                      const char *command_id,
                                      bool *out_duplicate,
                                      char *out_replay_code,
                                      size_t out_replay_code_size,
                                      char *out_replay_message,
                                      size_t out_replay_message_size);

/**
 * Executes the restricted control subset for an authenticated encrypted
 * machine link. The caller must validate peer identity, replay window and TTL
 * before entering this trust boundary.
 */
GuiBleCommandResult gui_app_trusted_link_action(const char *action,
                                                const char *command_json,
                                                const char *command_id,
                                                bool *out_duplicate);

/** @brief Serializes the common HTTP/BLE protocol-v2 capability document. */
bool gui_app_v2_capabilities_json(char *out, size_t out_size);

/**
 * @brief Resets the base tick timestamp for the local software clock.
 * Must be called whenever the clock is set (NTP sync, web sync, UI manual adjustment).
 */
void main_reset_clock_tick(uint32_t now_ms);

#endif // GUI_APP_H
