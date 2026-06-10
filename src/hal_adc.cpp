#include "hal_adc.h"

#include "config.h"
#include "hal_i2c_bus.h"

#include <Wire.h>

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

bool adc_present = false;

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
    adc_present = false;

    if (!hal_i2c_bus_lock(HwConfig::I2C_MUTEX_TIMEOUT_MS)) {
        return false;
    }

    adc_present = probe_locked();
    hal_i2c_bus_unlock();
    return adc_present;
}

bool hal_adc_is_present(void)
{
    return adc_present;
}

bool hal_adc_read_raw(uint8_t ch, int16_t *out)
{
    if (out == nullptr || !channel_valid(ch) || !adc_present) {
        return false;
    }

    if (!hal_i2c_bus_lock(HwConfig::I2C_MUTEX_TIMEOUT_MS)) {
        return false;
    }

    bool ok = write_register_locked(REG_CONFIG, config_for_channel(ch));
    uint16_t cfg = 0;
    const uint32_t started_ms = millis();
    while (ok) {
        ok = read_register_locked(REG_CONFIG, &cfg);
        if (!ok || (cfg & CFG_OS_SINGLE) != 0) {
            break;
        }
        if (millis() - started_ms >= ADS1115_CONVERSION_TIMEOUT_MS) {
            ok = false;
            break;
        }
        delay(1);
    }

    uint16_t raw = 0;
    if (ok) {
        ok = read_register_locked(REG_CONVERSION, &raw);
    }

    if (ok) {
        *out = static_cast<int16_t>(raw);
    } else {
        adc_present = false;
    }

    hal_i2c_bus_unlock();
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
