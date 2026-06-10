#include "hal_i2c_bus.h"

#include "config.h"

#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {

SemaphoreHandle_t i2c_mutex = nullptr;
bool i2c_initialized = false;

bool ensure_mutex()
{
    if (i2c_mutex != nullptr) {
        return true;
    }

    i2c_mutex = xSemaphoreCreateMutex();
    return i2c_mutex != nullptr;
}

} // namespace

bool hal_i2c_bus_init(void)
{
    if (!ensure_mutex()) {
        return false;
    }

    if (!i2c_initialized) {
        Wire.begin(HwConfig::I2C_SDA_PIN, HwConfig::I2C_SCL_PIN, HwConfig::I2C_FREQUENCY_HZ);
        Wire.setTimeOut(HwConfig::I2C_MUTEX_TIMEOUT_MS);
        i2c_initialized = true;
    }

    return true;
}

bool hal_i2c_bus_lock(uint32_t timeout_ms)
{
    if (!hal_i2c_bus_init()) {
        return false;
    }

    const TickType_t ticks = pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(i2c_mutex, ticks) == pdTRUE;
}

void hal_i2c_bus_unlock(void)
{
    if (i2c_mutex != nullptr) {
        xSemaphoreGive(i2c_mutex);
    }
}

bool hal_i2c_bus_is_initialized(void)
{
    return i2c_initialized;
}
