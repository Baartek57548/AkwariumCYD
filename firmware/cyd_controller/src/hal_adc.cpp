#include "hal_adc.h"

#include "config.h"
#include "hal_i2c_bus.h"

#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {

constexpr uint8_t REG_CONVERSION = 0x00;
constexpr uint8_t REG_CONFIG = 0x01;

constexpr uint16_t CFG_OS_SINGLE = 0x8000;
constexpr uint16_t CFG_MUX_SINGLE_A0 = 0x4000;
constexpr uint16_t CFG_PGA_4V096 = 0x0200;
constexpr uint16_t CFG_MODE_SINGLE_SHOT = 0x0100;
constexpr uint16_t CFG_DR_128SPS = 0x0080;
constexpr uint16_t CFG_COMP_DISABLED = 0x0003;

constexpr float ADS1115_FSR_VOLTS = 4.096f;
constexpr float ADS1115_COUNTS = 32768.0f;
constexpr uint32_t ADS1115_CONVERSION_TIMEOUT_MS = 20;
constexpr uint32_t ADS1115_RETRY_BASE_MS = 500UL;
constexpr uint32_t ADS1115_RETRY_MAX_MS = 30000UL;

bool adc_present = false;
uint8_t adc_consecutive_errors = 0U;
uint32_t adc_next_probe_ms = 0U;

bool deadline_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return static_cast<int32_t>(now_ms - deadline_ms) >= 0;
}

uint32_t retry_delay_ms(uint8_t consecutive_errors)
{
    const uint8_t shift = consecutive_errors > 6U
                              ? 6U
                              : static_cast<uint8_t>(
                                    consecutive_errors > 0U ? consecutive_errors - 1U : 0U);
    const uint32_t delay_ms = ADS1115_RETRY_BASE_MS << shift;
    return delay_ms < ADS1115_RETRY_MAX_MS
               ? delay_ms
               : ADS1115_RETRY_MAX_MS;
}

void record_failure_locked(uint32_t now_ms)
{
    adc_present = false;
    if (adc_consecutive_errors < UINT8_MAX) {
        ++adc_consecutive_errors;
    }
    adc_next_probe_ms = now_ms + retry_delay_ms(adc_consecutive_errors);
}

void record_success_locked()
{
    adc_present = true;
    adc_consecutive_errors = 0U;
    adc_next_probe_ms = 0U;
}

bool write_register_locked(uint8_t reg, uint16_t value)
{
    Wire.beginTransmission(HwConfig::ADS1115_ADDR);
    Wire.write(reg);
    Wire.write(static_cast<uint8_t>((value >> 8) & 0xFF));
    Wire.write(static_cast<uint8_t>(value & 0xFF));
    return Wire.endTransmission() == 0;
}

bool read_register_locked(uint8_t reg, uint16_t *value)
{
    if (value == nullptr) {
        return false;
    }

    Wire.beginTransmission(HwConfig::ADS1115_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) {
        return false;
    }

    if (Wire.requestFrom(static_cast<int>(HwConfig::ADS1115_ADDR), 2) != 2) {
        return false;
    }

    const uint16_t msb = static_cast<uint16_t>(Wire.read());
    const uint16_t lsb = static_cast<uint16_t>(Wire.read());
    *value = static_cast<uint16_t>((msb << 8) | lsb);
    return true;
}

bool probe_locked()
{
    Wire.beginTransmission(HwConfig::ADS1115_ADDR);
    return Wire.endTransmission() == 0;
}

bool probe_if_due_locked(uint32_t now_ms, bool force)
{
    if (adc_present) {
        return true;
    }
    if (!force && !deadline_reached(now_ms, adc_next_probe_ms)) {
        return false;
    }
    if (probe_locked()) {
        record_success_locked();
        return true;
    }
    record_failure_locked(now_ms);
    return false;
}

bool channel_valid(uint8_t ch)
{
    return ch <= static_cast<uint8_t>(HwConfig::ADC_SPARE);
}

uint16_t config_for_channel(uint8_t ch)
{
    const uint16_t mux = static_cast<uint16_t>(CFG_MUX_SINGLE_A0 + (static_cast<uint16_t>(ch) << 12));
    return static_cast<uint16_t>(CFG_OS_SINGLE |
                                 mux |
                                 CFG_PGA_4V096 |
                                 CFG_MODE_SINGLE_SHOT |
                                 CFG_DR_128SPS |
                                 CFG_COMP_DISABLED);
}

} // namespace

bool hal_adc_init(void)
{
    if (!hal_i2c_bus_lock(HwConfig::I2C_MUTEX_TIMEOUT_MS)) {
        return false;
    }

    adc_present = false;
    adc_consecutive_errors = 0U;
    adc_next_probe_ms = 0U;
    const bool present = probe_if_due_locked(millis(), true);
    hal_i2c_bus_unlock();
    return present;
}

bool hal_adc_is_present(void)
{
    if (!hal_i2c_bus_lock(HwConfig::I2C_MUTEX_TIMEOUT_MS)) {
        return false;
    }
    const bool present = probe_if_due_locked(millis(), false);
    hal_i2c_bus_unlock();
    return present;
}

bool hal_adc_read_raw(uint8_t ch, int16_t *out)
{
    if (out == nullptr || !channel_valid(ch)) {
        return false;
    }

    if (!hal_i2c_bus_lock(HwConfig::I2C_MUTEX_TIMEOUT_MS)) {
        return false;
    }

    const uint32_t now_ms = millis();
    if (!probe_if_due_locked(now_ms, false)) {
        hal_i2c_bus_unlock();
        return false;
    }

    bool ok = write_register_locked(REG_CONFIG, config_for_channel(ch));
    hal_i2c_bus_unlock();
    if (!ok) {
        if (hal_i2c_bus_lock(HwConfig::I2C_MUTEX_TIMEOUT_MS)) {
            record_failure_locked(millis());
            hal_i2c_bus_unlock();
        }
        return false;
    }

    uint16_t cfg = 0;
    const uint32_t started_ms = millis();
    while (ok) {
        vTaskDelay(pdMS_TO_TICKS(1U));
        if (!hal_i2c_bus_lock(HwConfig::I2C_MUTEX_TIMEOUT_MS)) {
            ok = false;
            break;
        }
        ok = read_register_locked(REG_CONFIG, &cfg);
        hal_i2c_bus_unlock();
        if (!ok || (cfg & CFG_OS_SINGLE) != 0) {
            break;
        }
        if (millis() - started_ms >= ADS1115_CONVERSION_TIMEOUT_MS) {
            ok = false;
            break;
        }
    }

    uint16_t raw = 0;
    if (ok) {
        if (hal_i2c_bus_lock(HwConfig::I2C_MUTEX_TIMEOUT_MS)) {
            ok = read_register_locked(REG_CONVERSION, &raw);
            if (ok) {
                *out = static_cast<int16_t>(raw);
                record_success_locked();
            } else {
                record_failure_locked(millis());
            }
            hal_i2c_bus_unlock();
        } else {
            ok = false;
        }
    } else {
        if (hal_i2c_bus_lock(HwConfig::I2C_MUTEX_TIMEOUT_MS)) {
            record_failure_locked(millis());
            hal_i2c_bus_unlock();
        }
    }

    return ok;
}

bool hal_adc_read_voltage(uint8_t ch, float *out)
{
    if (out == nullptr) {
        return false;
    }

    int16_t raw = 0;
    if (!hal_adc_read_raw(ch, &raw)) {
        return false;
    }

    *out = (static_cast<float>(raw) * ADS1115_FSR_VOLTS) / ADS1115_COUNTS;
    return true;
}
