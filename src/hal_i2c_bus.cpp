#include "hal_i2c_bus.h"

#include "config.h"

#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <string.h>

namespace {

StaticSemaphore_t i2c_mutex_storage;
SemaphoreHandle_t i2c_mutex = xSemaphoreCreateMutexStatic(&i2c_mutex_storage);
bool i2c_initialized = false;

bool initialize_bus_locked()
{
    if (i2c_initialized) {
        return true;
    }

    if (!Wire.begin(HwConfig::I2C_SDA_PIN,
                    HwConfig::I2C_SCL_PIN,
                    HwConfig::I2C_FREQUENCY_HZ)) {
        return false;
    }
    Wire.setTimeOut(HwConfig::I2C_MUTEX_TIMEOUT_MS);
    i2c_initialized = true;
    return true;
}

} // namespace

bool hal_i2c_bus_init(void)
{
    if (i2c_mutex == nullptr ||
        xSemaphoreTake(i2c_mutex,
                       pdMS_TO_TICKS(HwConfig::I2C_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return false;
    }

    const bool ok = initialize_bus_locked();
    xSemaphoreGive(i2c_mutex);
    return ok;
}

bool hal_i2c_bus_lock(uint32_t timeout_ms)
{
    if (i2c_mutex == nullptr) {
        return false;
    }

    const TickType_t ticks = pdMS_TO_TICKS(timeout_ms);
    if (xSemaphoreTake(i2c_mutex, ticks) != pdTRUE) {
        return false;
    }
    if (!initialize_bus_locked()) {
        xSemaphoreGive(i2c_mutex);
        return false;
    }
    return true;
}

void hal_i2c_bus_unlock(void)
{
    if (i2c_mutex != nullptr) {
        xSemaphoreGive(i2c_mutex);
    }
}

bool hal_i2c_bus_is_initialized(void)
{
    if (i2c_mutex == nullptr || xSemaphoreTake(i2c_mutex, 0U) != pdTRUE) {
        return false;
    }
    const bool initialized = i2c_initialized;
    xSemaphoreGive(i2c_mutex);
    return initialized;
}

bool hal_i2c_bus_scan(HalI2cScanResult *result)
{
    if (result == nullptr) {
        return false;
    }

    memset(result, 0, sizeof(*result));
    uint8_t detected_count = 0;
    for (uint8_t address = 0x08U; address <= 0x77U; ++address) {
        if (!hal_i2c_bus_lock(250U)) {
            return false;
        }
        Wire.beginTransmission(address);
        const uint8_t error = Wire.endTransmission(true);
        hal_i2c_bus_unlock();

        // ESP32 Wire reports code 5 when its transaction timeout expires.
        // Abort instead of multiplying a wedged-bus timeout by all addresses.
        if (error == 5U) {
            return false;
        }
        if (error == 0U) {
            if (detected_count < HAL_I2C_SCAN_MAX_DEVICES) {
                result->addresses[detected_count] = address;
            } else {
                result->truncated = true;
            }
            ++detected_count;
        }

        if ((address & 0x0FU) == 0U) {
            taskYIELD();
        }
    }

    result->count = detected_count < HAL_I2C_SCAN_MAX_DEVICES
                        ? detected_count
                        : HAL_I2C_SCAN_MAX_DEVICES;
    return true;
}
