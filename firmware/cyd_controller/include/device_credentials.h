#ifndef AQUARIUM_DEVICE_CREDENTIALS_H
#define AQUARIUM_DEVICE_CREDENTIALS_H

#include <stddef.h>

/**
 * Loads or creates credentials that are unique to this controller.
 *
 * Production credentials are generated from the ESP32 hardware RNG and kept
 * in a dedicated NVS namespace. The administrator PIN is persisted as SHA-256;
 * its plaintext copy exists only until the user confirms the first login.
 */
bool device_credentials_initialize();

/** Constant-time verification of a 4-8 digit administrator PIN. */
bool device_credentials_admin_pin_matches(const char *candidate);

/** Length of the active PIN; used by the compact on-device keypad. */
size_t device_credentials_admin_pin_length();

/**
 * Returns the first-boot PIN while it still needs to be recorded by the owner,
 * otherwise returns nullptr.
 */
const char *device_credentials_setup_pin();

/** Removes the recoverable first-boot PIN after a successful login. */
bool device_credentials_acknowledge_setup_pin();

/** Returns the controller-specific WPA2 password for the maintenance SoftAP. */
const char *device_credentials_ota_ap_password();

/** Clears only the credential namespace; new values are created after reboot. */
bool device_credentials_factory_reset();

#endif
