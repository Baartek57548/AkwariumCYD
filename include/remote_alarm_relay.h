#ifndef AQUARIUM_REMOTE_ALARM_RELAY_H
#define AQUARIUM_REMOTE_ALARM_RELAY_H

#include <Arduino.h>

constexpr size_t REMOTE_ALARM_RELAY_URL_BYTES = 192U;
constexpr size_t REMOTE_ALARM_RELAY_DEVICE_ID_BYTES = 65U;

enum class RemoteAlarmRelayError : uint8_t {
    None = 0U,
    Disabled = 1U,
    MissingConfiguration = 2U,
    MissingCaCertificate = 3U,
    ClockNotSynchronized = 4U,
    WifiUnavailable = 5U,
    TlsConnectionFailed = 6U,
    HttpRejected = 7U,
    StorageFailure = 8U,
    InvalidConfiguration = 9U
};

struct RemoteAlarmRelayStatus {
    bool enabled;
    bool provisioned;
    bool task_running;
    bool ca_certificate_loaded;
    uint32_t delivered_events;
    uint32_t failed_attempts;
    uint32_t last_success_epoch;
    uint32_t next_retry_ms;
    RemoteAlarmRelayError last_error;
    char base_url[REMOTE_ALARM_RELAY_URL_BYTES];
    char device_id[REMOTE_ALARM_RELAY_DEVICE_ID_BYTES];
};

/**
 * Loads the relay configuration and starts the low-priority network task.
 *
 * The controller remains fully functional when the relay is disabled or
 * unavailable. The task never executes network I/O in the sensor or UI loop.
 */
bool remote_alarm_relay_initialize(void);

/**
 * Provisions the relay. Call only from an authenticated, encrypted transport
 * such as bonded BLE. The Base64 secret is decoded directly into NVS and is
 * never returned by any status function.
 */
bool remote_alarm_relay_configure(const char *base_url,
                                  const char *device_id,
                                  const char *base64_secret,
                                  bool enabled);

bool remote_alarm_relay_set_enabled(bool enabled);
bool remote_alarm_relay_clear(void);
RemoteAlarmRelayStatus remote_alarm_relay_status(void);
const char *remote_alarm_relay_error_code(RemoteAlarmRelayError error);

#endif // AQUARIUM_REMOTE_ALARM_RELAY_H
