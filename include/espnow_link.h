#ifndef AQUARIUM_ESPNOW_LINK_H
#define AQUARIUM_ESPNOW_LINK_H

#include <Arduino.h>

#include "runtime_controller.h"

/**
 * Starts the optional encrypted CYD-to-C6 link. Absence of a provisioned
 * configuration is a valid disabled state and never blocks local automation.
 */
bool espnow_link_initialize(void);

/** Queues a bounded telemetry snapshot without blocking the UI loop. */
bool espnow_link_publish(const RuntimeTelemetry &telemetry);

/**
 * Validates and stores an encrypted peer configuration in NVS.
 *
 * The new configuration becomes active after a controlled restart. PMK and
 * LMK must contain exactly 32 hexadecimal characters.
 */
bool espnow_link_configure(const char *peer_mac,
                           const char *pmk_hex,
                           const char *lmk_hex,
                           uint8_t fallback_channel);

/** Removes the peer and keys from NVS; the change applies after restart. */
bool espnow_link_clear_configuration(void);

/** Reports whether a valid peer configuration is active in this boot. */
bool espnow_link_is_enabled(void);

#endif // AQUARIUM_ESPNOW_LINK_H
