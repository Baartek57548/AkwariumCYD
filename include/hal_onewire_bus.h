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

struct HalTemperatureReading {
    // Last accepted sample. It remains available for diagnostics after it
    // becomes stale; consumers must use valid before controlling outputs.
    float celsius;
    uint32_t sample_ms;
    uint32_t age_ms;
    // Lifetime count of discovery, bus, CRC and range-validation failures.
    uint32_t error_count;
    uint8_t rom[HAL_ONEWIRE_ROM_BYTES];
    bool valid;
    bool present;
    bool stale;
};

bool hal_onewire_bus_scan(HalOneWireScanResult *result);
bool hal_temperature_init(void);
// Advances at most one FSM phase and never waits for sensor conversion.
// The output snapshot is filled on every successful lock acquisition. The
// return value is true only when this call publishes a fresh valid sample.
bool hal_temperature_poll(uint32_t now_ms, HalTemperatureReading *out);

#endif
