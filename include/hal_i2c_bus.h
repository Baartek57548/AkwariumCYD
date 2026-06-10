#ifndef HAL_I2C_BUS_H
#define HAL_I2C_BUS_H

#include <Arduino.h>

bool hal_i2c_bus_init(void);
bool hal_i2c_bus_lock(uint32_t timeout_ms);
void hal_i2c_bus_unlock(void);
bool hal_i2c_bus_is_initialized(void);

#endif
