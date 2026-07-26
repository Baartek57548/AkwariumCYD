#include "hal_onewire_bus.h"

#include "config.h"

#include <OneWire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <math.h>
#include <string.h>

namespace {

constexpr uint8_t ONEWIRE_CMD_CONVERT_T = 0x44U;
constexpr uint8_t ONEWIRE_CMD_READ_SCRATCHPAD = 0xBEU;
constexpr uint32_t TEMPERATURE_CONVERSION_MS = 750UL;
constexpr uint32_t TEMPERATURE_SAMPLE_INTERVAL_MS = 2000UL;
constexpr uint32_t TEMPERATURE_STALE_TIMEOUT_MS = 5000UL;
constexpr uint32_t TEMPERATURE_RETRY_BASE_MS = 500UL;
constexpr uint32_t TEMPERATURE_RETRY_MAX_MS = 30000UL;

enum class TemperatureState : uint8_t {
    Uninitialized = 0U,
    Discover,
    StartConversion,
    Converting,
    WaitForNextSample,
    Backoff
};

StaticSemaphore_t onewire_mutex_storage;
SemaphoreHandle_t onewire_mutex = xSemaphoreCreateMutexStatic(&onewire_mutex_storage);
OneWire onewire_bus(HwConfig::OneWireBus::DATA_PIN);

TemperatureState temperature_state = TemperatureState::Uninitialized;
HalTemperatureReading temperature_reading = {
    NAN,
    0U,
    0U,
    0U,
    {0U},
    false,
    false,
    true
};
uint8_t temperature_family = 0U;
uint8_t temperature_consecutive_errors = 0U;
uint32_t temperature_deadline_ms = 0U;
bool temperature_has_sample = false;

bool deadline_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return static_cast<int32_t>(now_ms - deadline_ms) >= 0;
}

bool temperature_family_supported(uint8_t family)
{
    return family == 0x28U || family == 0x22U || family == 0x10U;
}

uint32_t retry_delay_ms(uint8_t consecutive_errors)
{
    const uint8_t shift = consecutive_errors > 6U
                              ? 6U
                              : static_cast<uint8_t>(
                                    consecutive_errors > 0U ? consecutive_errors - 1U : 0U);
    const uint32_t delay_ms = TEMPERATURE_RETRY_BASE_MS << shift;
    return delay_ms < TEMPERATURE_RETRY_MAX_MS
               ? delay_ms
               : TEMPERATURE_RETRY_MAX_MS;
}

void update_stale_state_locked(uint32_t now_ms)
{
    if (!temperature_has_sample) {
        temperature_reading.age_ms = 0U;
        temperature_reading.valid = false;
        temperature_reading.stale = true;
        return;
    }

    temperature_reading.age_ms = now_ms - temperature_reading.sample_ms;
    temperature_reading.stale =
        temperature_reading.age_ms > TEMPERATURE_STALE_TIMEOUT_MS;
    if (temperature_reading.stale) {
        temperature_reading.valid = false;
    }
}

void schedule_temperature_retry_locked(uint32_t now_ms, bool sensor_present)
{
    if (temperature_reading.error_count < UINT32_MAX) {
        ++temperature_reading.error_count;
    }
    if (temperature_consecutive_errors < UINT8_MAX) {
        ++temperature_consecutive_errors;
    }
    temperature_reading.present = sensor_present;
    temperature_deadline_ms = now_ms + retry_delay_ms(temperature_consecutive_errors);
    temperature_state = TemperatureState::Backoff;
    onewire_bus.depower();
}

bool discover_temperature_sensor_locked(void)
{
    uint8_t rom[HAL_ONEWIRE_ROM_BYTES] = {};
    onewire_bus.reset_search();

    while (onewire_bus.search(rom)) {
        const bool rom_valid =
            OneWire::crc8(rom, HAL_ONEWIRE_ROM_BYTES - 1U) ==
            rom[HAL_ONEWIRE_ROM_BYTES - 1U];
        if (rom_valid && temperature_family_supported(rom[0])) {
            memcpy(temperature_reading.rom, rom, HAL_ONEWIRE_ROM_BYTES);
            temperature_family = rom[0];
            temperature_reading.present = true;
            onewire_bus.reset_search();
            return true;
        }
    }

    onewire_bus.reset_search();
    memset(temperature_reading.rom, 0, sizeof(temperature_reading.rom));
    temperature_family = 0U;
    temperature_reading.present = false;
    return false;
}

bool start_temperature_conversion_locked(void)
{
    if (!temperature_reading.present ||
        !temperature_family_supported(temperature_family) ||
        onewire_bus.reset() == 0U) {
        return false;
    }

    onewire_bus.select(temperature_reading.rom);
    // Holding the data line high also supports parasite-powered sensors.
    // The scan API refuses access while the conversion is in progress.
    onewire_bus.write(ONEWIRE_CMD_CONVERT_T, 1U);
    return true;
}

bool decode_temperature_locked(const uint8_t *scratchpad, float *celsius)
{
    if (scratchpad == nullptr || celsius == nullptr ||
        OneWire::crc8(scratchpad, 8U) != scratchpad[8]) {
        return false;
    }

    const int16_t raw = static_cast<int16_t>(
        static_cast<uint16_t>(scratchpad[0]) |
        (static_cast<uint16_t>(scratchpad[1]) << 8U));
    float decoded = NAN;

    if (temperature_family == 0x10U) {
        decoded = static_cast<float>(raw) / 2.0f;
        const uint8_t count_remain = scratchpad[6];
        const uint8_t count_per_c = scratchpad[7];
        if (count_per_c != 0U && count_remain <= count_per_c) {
            decoded = decoded - 0.25f +
                      static_cast<float>(count_per_c - count_remain) /
                          static_cast<float>(count_per_c);
        }
    } else {
        int16_t normalized_raw = raw;
        switch (scratchpad[4] & 0x60U) {
        case 0x00U:
            normalized_raw = static_cast<int16_t>(normalized_raw & ~0x0007);
            break;
        case 0x20U:
            normalized_raw = static_cast<int16_t>(normalized_raw & ~0x0003);
            break;
        case 0x40U:
            normalized_raw = static_cast<int16_t>(normalized_raw & ~0x0001);
            break;
        default:
            break;
        }
        decoded = static_cast<float>(normalized_raw) / 16.0f;
    }

    if (!isfinite(decoded) || decoded < -10.0f || decoded > 50.0f) {
        return false;
    }

    *celsius = decoded;
    return true;
}

bool read_temperature_scratchpad_locked(float *celsius, bool *sensor_responded)
{
    if (celsius == nullptr || sensor_responded == nullptr) {
        return false;
    }

    *sensor_responded = false;
    onewire_bus.depower();
    if (onewire_bus.reset() == 0U) {
        return false;
    }

    *sensor_responded = true;
    onewire_bus.select(temperature_reading.rom);
    onewire_bus.write(ONEWIRE_CMD_READ_SCRATCHPAD);

    uint8_t scratchpad[9] = {};
    for (uint8_t index = 0U; index < sizeof(scratchpad); ++index) {
        scratchpad[index] = onewire_bus.read();
    }
    return decode_temperature_locked(scratchpad, celsius);
}

bool lock_onewire(TickType_t timeout_ticks)
{
    return onewire_mutex != nullptr &&
           xSemaphoreTake(onewire_mutex, timeout_ticks) == pdTRUE;
}

void initialize_temperature_locked()
{
    temperature_reading.celsius = NAN;
    temperature_reading.sample_ms = 0U;
    temperature_reading.age_ms = 0U;
    temperature_reading.error_count = 0U;
    memset(temperature_reading.rom, 0, sizeof(temperature_reading.rom));
    temperature_reading.valid = false;
    temperature_reading.present = false;
    temperature_reading.stale = true;
    temperature_family = 0U;
    temperature_consecutive_errors = 0U;
    temperature_deadline_ms = 0U;
    temperature_has_sample = false;
    temperature_state = TemperatureState::Discover;
    onewire_bus.depower();
    onewire_bus.reset_search();
}

void copy_temperature_reading_locked(uint32_t now_ms, HalTemperatureReading *out)
{
    update_stale_state_locked(now_ms);
    if (out != nullptr) {
        *out = temperature_reading;
    }
}

} // namespace

bool hal_onewire_bus_scan(HalOneWireScanResult *result)
{
    if (result == nullptr) {
        return false;
    }

    memset(result, 0, sizeof(*result));
    if (!lock_onewire(pdMS_TO_TICKS(HwConfig::OneWireBus::MUTEX_TIMEOUT_MS))) {
        return false;
    }

    if (temperature_state == TemperatureState::Converting) {
        xSemaphoreGive(onewire_mutex);
        return false;
    }

    uint8_t rom[HAL_ONEWIRE_ROM_BYTES] = {};
    uint8_t detected_count = 0;
    onewire_bus.reset_search();
    while (onewire_bus.search(rom)) {
        if (detected_count < HAL_ONEWIRE_SCAN_MAX_DEVICES) {
            HalOneWireDevice &device = result->devices[detected_count];
            memcpy(device.rom, rom, HAL_ONEWIRE_ROM_BYTES);
            device.crc_valid = OneWire::crc8(rom, HAL_ONEWIRE_ROM_BYTES - 1U) == rom[HAL_ONEWIRE_ROM_BYTES - 1U];
        } else {
            result->truncated = true;
        }
        ++detected_count;
    }
    onewire_bus.reset_search();
    xSemaphoreGive(onewire_mutex);

    result->count = detected_count < HAL_ONEWIRE_SCAN_MAX_DEVICES
                        ? detected_count
                        : HAL_ONEWIRE_SCAN_MAX_DEVICES;
    return true;
}

bool hal_temperature_init(void)
{
    if (!lock_onewire(pdMS_TO_TICKS(HwConfig::OneWireBus::MUTEX_TIMEOUT_MS))) {
        return false;
    }

    if (temperature_state == TemperatureState::Uninitialized) {
        initialize_temperature_locked();
    }

    xSemaphoreGive(onewire_mutex);
    return true;
}

bool hal_temperature_poll(uint32_t now_ms, HalTemperatureReading *out)
{
    if (out == nullptr) {
        return false;
    }
    if (!lock_onewire(0U)) {
        // The caller may keep displaying its previous coherent snapshot. Do
        // not replace it with a synthetic failure when diagnostics briefly
        // owns the bus mutex.
        return false;
    }

    bool sample_updated = false;
    if (temperature_state == TemperatureState::Uninitialized) {
        initialize_temperature_locked();
    }
    switch (temperature_state) {
    case TemperatureState::Uninitialized:
        break;

    case TemperatureState::Discover:
        if (discover_temperature_sensor_locked()) {
            temperature_state = TemperatureState::StartConversion;
        } else {
            schedule_temperature_retry_locked(now_ms, false);
        }
        break;

    case TemperatureState::StartConversion:
        if (start_temperature_conversion_locked()) {
            temperature_deadline_ms = now_ms + TEMPERATURE_CONVERSION_MS;
            temperature_state = TemperatureState::Converting;
        } else {
            schedule_temperature_retry_locked(now_ms, false);
        }
        break;

    case TemperatureState::Converting:
        if (deadline_reached(now_ms, temperature_deadline_ms)) {
            float celsius = NAN;
            bool sensor_responded = false;
            if (read_temperature_scratchpad_locked(&celsius, &sensor_responded)) {
                temperature_reading.celsius = celsius;
                temperature_reading.sample_ms = now_ms;
                temperature_reading.age_ms = 0U;
                temperature_reading.valid = true;
                temperature_reading.present = true;
                temperature_reading.stale = false;
                temperature_has_sample = true;
                temperature_consecutive_errors = 0U;
                temperature_deadline_ms =
                    now_ms + TEMPERATURE_SAMPLE_INTERVAL_MS;
                temperature_state = TemperatureState::WaitForNextSample;
                sample_updated = true;
            } else {
                schedule_temperature_retry_locked(now_ms, sensor_responded);
            }
        }
        break;

    case TemperatureState::WaitForNextSample:
        if (deadline_reached(now_ms, temperature_deadline_ms)) {
            temperature_state = TemperatureState::StartConversion;
        }
        break;

    case TemperatureState::Backoff:
        if (deadline_reached(now_ms, temperature_deadline_ms)) {
            temperature_state = TemperatureState::Discover;
        }
        break;
    }

    copy_temperature_reading_locked(now_ms, out);
    xSemaphoreGive(onewire_mutex);
    return sample_updated;
}
