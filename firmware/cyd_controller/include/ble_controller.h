#ifndef AQUARIUM_BLE_CONTROLLER_H
#define AQUARIUM_BLE_CONTROLLER_H

#include <stdint.h>

// Bluetooth is permanently disabled and removed to maximize RAM for Wi-Fi and LVGL.
inline bool ble_controller_initialize(void) { return false; }
inline bool ble_controller_request_forget_bonds(void) { return false; }
inline int ble_controller_bond_count(void) { return 0; }

#endif
