#ifndef AQUARIUM_WIFI_CREDENTIAL_STORE_H
#define AQUARIUM_WIFI_CREDENTIAL_STORE_H

#include <Arduino.h>

constexpr size_t WIFI_CREDENTIAL_SSID_BYTES = 33U;
constexpr size_t WIFI_CREDENTIAL_PASSWORD_BYTES = 65U;

/**
 * Stores a Wi-Fi password only in NVS. On production devices NVS must be
 * protected by ESP32 Flash Encryption; removable SD files never contain it.
 */
bool wifi_credential_store_save(const char *ssid, const char *password);

bool wifi_credential_store_load(const char *ssid,
                                char *password,
                                size_t password_size);

bool wifi_credential_store_load_latest(char *ssid,
                                       size_t ssid_size,
                                       char *password,
                                       size_t password_size);

bool wifi_credential_store_clear(void);

#endif // AQUARIUM_WIFI_CREDENTIAL_STORE_H
