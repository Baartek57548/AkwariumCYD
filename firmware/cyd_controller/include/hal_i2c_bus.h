#ifndef HAL_I2C_BUS_H
#define HAL_I2C_BUS_H

#include <Arduino.h>

constexpr uint8_t HAL_I2C_SCAN_MAX_DEVICES = 16;

struct HalI2cScanResult {
    uint8_t addresses[HAL_I2C_SCAN_MAX_DEVICES];
    uint8_t count;
    bool truncated;
};

bool hal_i2c_bus_init(void);
bool hal_i2c_bus_lock(uint32_t timeout_ms);
void hal_i2c_bus_unlock(void);
bool hal_i2c_bus_is_initialized(void);
bool hal_i2c_bus_scan(HalI2cScanResult *result);

#endif
