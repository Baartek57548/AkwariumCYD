#ifndef HAL_ONEWIRE_BUS_H
#define HAL_ONEWIRE_BUS_H

#include <Arduino.h>

constexpr uint8_t HAL_ONEWIRE_ROM_BYTES = 8;
constexpr uint8_t HAL_ONEWIRE_SCAN_MAX_DEVICES = 8;

struct HalOneWireDevice {
    uint8_t rom[HAL_ONEWIRE_ROM_BYTES];
    bool crc_valid;
};

struct HalOneWireScanResult {
    HalOneWireDevice devices[HAL_ONEWIRE_SCAN_MAX_DEVICES];
    uint8_t count;
    bool truncated;
};

bool hal_onewire_bus_scan(HalOneWireScanResult *result);

#endif
