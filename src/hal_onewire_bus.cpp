#include "hal_onewire_bus.h"

#include "config.h"

#include <OneWire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

namespace {

StaticSemaphore_t onewire_mutex_storage;
SemaphoreHandle_t onewire_mutex = nullptr;

SemaphoreHandle_t ensure_mutex()
{
    if (onewire_mutex == nullptr) {
        onewire_mutex = xSemaphoreCreateMutexStatic(&onewire_mutex_storage);
    }
    return onewire_mutex;
}

} // namespace

bool hal_onewire_bus_scan(HalOneWireScanResult *result)
{
    if (result == nullptr) {
        return false;
    }

    memset(result, 0, sizeof(*result));
    SemaphoreHandle_t mutex = ensure_mutex();
    if (mutex == nullptr ||
        xSemaphoreTake(mutex, pdMS_TO_TICKS(HwConfig::OneWireBus::MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return false;
    }

    OneWire bus(HwConfig::OneWireBus::DATA_PIN);
    uint8_t rom[HAL_ONEWIRE_ROM_BYTES] = {};
    uint8_t detected_count = 0;
    bus.reset_search();
    while (bus.search(rom)) {
        if (detected_count < HAL_ONEWIRE_SCAN_MAX_DEVICES) {
            HalOneWireDevice &device = result->devices[detected_count];
            memcpy(device.rom, rom, HAL_ONEWIRE_ROM_BYTES);
            device.crc_valid = OneWire::crc8(rom, HAL_ONEWIRE_ROM_BYTES - 1U) == rom[HAL_ONEWIRE_ROM_BYTES - 1U];
        } else {
            result->truncated = true;
        }
        ++detected_count;
        delay(0);
    }
    bus.reset_search();
    xSemaphoreGive(mutex);

    result->count = detected_count < HAL_ONEWIRE_SCAN_MAX_DEVICES
                        ? detected_count
                        : HAL_ONEWIRE_SCAN_MAX_DEVICES;
    return true;
}
