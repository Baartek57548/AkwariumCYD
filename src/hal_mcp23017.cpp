#include "hal_mcp23017.h"

#include "hal_i2c_bus.h"

#include <Wire.h>

namespace {

constexpr uint8_t REG_IODIRA = 0x00;
constexpr uint8_t REG_IODIRB = 0x01;
constexpr uint8_t REG_GPPUA = 0x0C;
constexpr uint8_t REG_GPPUB = 0x0D;
constexpr uint8_t REG_GPIOA = 0x12;
constexpr uint8_t REG_GPIOB = 0x13;
constexpr uint8_t REG_OLATA = 0x14;
constexpr uint8_t REG_OLATB = 0x15;

bool mcp_present = false;
uint8_t output_shadow_a = 0xFF;
uint8_t output_shadow_b = 0x00;

uint8_t logical_to_physical_relay_byte(uint8_t logical_on_bits)
{
    if (HwConfig::RELAY_ACTIVE_LOW) {
        return static_cast<uint8_t>(~logical_on_bits);
    }
    return logical_on_bits;
}

uint8_t safe_relay_physical_byte()
{
    const uint8_t logical = HwConfig::RELAY_SAFE_STATE_ON ? 0xFF : 0x00;
    return logical_to_physical_relay_byte(logical);
}

bool write_register_locked(uint8_t reg, uint8_t value)
{
    Wire.beginTransmission(HwConfig::MCP23017_ADDR);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

bool read_register_locked(uint8_t reg, uint8_t *value)
{
    if (value == nullptr) {
        return false;
    }

    Wire.beginTransmission(HwConfig::MCP23017_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) {
        return false;
    }

    if (Wire.requestFrom(static_cast<int>(HwConfig::MCP23017_ADDR), 1) != 1) {
        return false;
    }

    *value = Wire.read();
    return true;
}

bool probe_locked()
{
    Wire.beginTransmission(HwConfig::MCP23017_ADDR);
    return Wire.endTransmission() == 0;
}

bool channel_is_output(HwConfig::McpChannel channel)
{
    return static_cast<uint8_t>(channel) <= static_cast<uint8_t>(HwConfig::CH_RELAY_SPARE);
}

bool channel_is_valid(HwConfig::McpChannel channel)
{
    return static_cast<uint8_t>(channel) <= static_cast<uint8_t>(HwConfig::CH_IN_SPARE_4);
}

} // namespace

bool hal_mcp_init(void)
{
    mcp_present = false;

    if (!hal_i2c_bus_lock(HwConfig::I2C_MUTEX_TIMEOUT_MS)) {
        return false;
    }

    const bool detected = probe_locked();
    if (!detected) {
        hal_i2c_bus_unlock();
        return false;
    }

    output_shadow_a = safe_relay_physical_byte();
    output_shadow_b = 0x00;

    bool ok = true;
    ok = ok && write_register_locked(REG_OLATA, output_shadow_a);
    ok = ok && write_register_locked(REG_OLATB, output_shadow_b);
    ok = ok && write_register_locked(REG_IODIRA, 0x00);
    ok = ok && write_register_locked(REG_IODIRB, 0xFF);
    ok = ok && write_register_locked(REG_GPPUA, 0x00);
    ok = ok && write_register_locked(REG_GPPUB, 0xFF);
    ok = ok && write_register_locked(REG_GPIOA, output_shadow_a);

    mcp_present = ok;
    hal_i2c_bus_unlock();
    return ok;
}

bool hal_mcp_is_present(void)
{
    return mcp_present;
}

bool hal_mcp_write_channel(HwConfig::McpChannel channel, bool on)
{
    if (!mcp_present || !channel_is_output(channel)) {
        return false;
    }

    if (!hal_i2c_bus_lock(HwConfig::I2C_MUTEX_TIMEOUT_MS)) {
        return false;
    }

    const uint8_t bit = static_cast<uint8_t>(1U << static_cast<uint8_t>(channel));
    const bool physical_high = HwConfig::RELAY_ACTIVE_LOW ? !on : on;
    if (physical_high) {
        output_shadow_a = static_cast<uint8_t>(output_shadow_a | bit);
    } else {
        output_shadow_a = static_cast<uint8_t>(output_shadow_a & ~bit);
    }

    const bool ok = write_register_locked(REG_OLATA, output_shadow_a);
    if (!ok) {
        mcp_present = false;
    }

    hal_i2c_bus_unlock();
    return ok;
}

bool hal_mcp_read_channel(HwConfig::McpChannel channel, bool *out)
{
    if (out == nullptr || !mcp_present || !channel_is_valid(channel)) {
        return false;
    }

    if (channel_is_output(channel)) {
        const uint8_t bit = static_cast<uint8_t>(1U << static_cast<uint8_t>(channel));
        const bool physical_high = (output_shadow_a & bit) != 0;
        *out = HwConfig::RELAY_ACTIVE_LOW ? !physical_high : physical_high;
        return true;
    }

    if (!hal_i2c_bus_lock(HwConfig::I2C_MUTEX_TIMEOUT_MS)) {
        return false;
    }

    uint8_t gpio_b = 0;
    const bool ok = read_register_locked(REG_GPIOB, &gpio_b);
    if (ok) {
        const uint8_t bit = static_cast<uint8_t>(1U << (static_cast<uint8_t>(channel) - 8U));
        *out = (gpio_b & bit) != 0;
    } else {
        mcp_present = false;
    }

    hal_i2c_bus_unlock();
    return ok;
}

bool hal_mcp_read_all(uint16_t *out)
{
    if (out == nullptr || !mcp_present) {
        return false;
    }

    if (!hal_i2c_bus_lock(HwConfig::I2C_MUTEX_TIMEOUT_MS)) {
        return false;
    }

    uint8_t gpio_b = 0;
    const bool ok = read_register_locked(REG_GPIOB, &gpio_b);
    if (ok) {
        const uint8_t logical_outputs = HwConfig::RELAY_ACTIVE_LOW
                                            ? static_cast<uint8_t>(~output_shadow_a)
                                            : output_shadow_a;
        *out = static_cast<uint16_t>(logical_outputs) |
               static_cast<uint16_t>(static_cast<uint16_t>(gpio_b) << 8);
    } else {
        mcp_present = false;
    }

    hal_i2c_bus_unlock();
    return ok;
}

bool hal_mcp_all_relays_safe(void)
{
    output_shadow_a = safe_relay_physical_byte();

    if (!mcp_present) {
        return false;
    }

    if (!hal_i2c_bus_lock(HwConfig::I2C_MUTEX_TIMEOUT_MS)) {
        return false;
    }

    const bool ok = write_register_locked(REG_OLATA, output_shadow_a);
    if (!ok) {
        mcp_present = false;
    }

    hal_i2c_bus_unlock();
    return ok;
}
