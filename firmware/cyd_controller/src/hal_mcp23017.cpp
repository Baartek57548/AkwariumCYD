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
constexpr uint32_t MCP23017_RETRY_BASE_MS = 500UL;
constexpr uint32_t MCP23017_RETRY_MAX_MS = 30000UL;

bool mcp_present = false;
uint8_t output_shadow_a = 0xFF;
uint8_t output_shadow_b = 0x00;
uint8_t mcp_consecutive_errors = 0U;
uint32_t mcp_next_probe_ms = 0U;
volatile bool mcp_safe_latched = false;

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
    const uint32_t delay_ms = MCP23017_RETRY_BASE_MS << shift;
    return delay_ms < MCP23017_RETRY_MAX_MS
               ? delay_ms
               : MCP23017_RETRY_MAX_MS;
}

void record_failure_locked(uint32_t now_ms)
{
    mcp_present = false;
    if (mcp_consecutive_errors < UINT8_MAX) {
        ++mcp_consecutive_errors;
    }
    mcp_next_probe_ms = now_ms + retry_delay_ms(mcp_consecutive_errors);
}

void record_success_locked()
{
    mcp_present = true;
    mcp_consecutive_errors = 0U;
    mcp_next_probe_ms = 0U;
}

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

bool initialize_device_locked(uint32_t now_ms)
{
    if (!probe_locked()) {
        record_failure_locked(now_ms);
        return false;
    }

    const uint8_t safe_shadow_a = safe_relay_physical_byte();
    constexpr uint8_t safe_shadow_b = 0x00U;
    bool ok = true;
    ok = ok && write_register_locked(REG_OLATA, safe_shadow_a);
    ok = ok && write_register_locked(REG_OLATB, safe_shadow_b);
    ok = ok && write_register_locked(REG_IODIRA, 0x00U);
    ok = ok && write_register_locked(REG_IODIRB, 0xFFU);
    ok = ok && write_register_locked(REG_GPPUA, 0x00U);
    ok = ok && write_register_locked(REG_GPPUB, 0xFFU);
    ok = ok && write_register_locked(REG_GPIOA, safe_shadow_a);
    if (!ok) {
        record_failure_locked(now_ms);
        return false;
    }

    output_shadow_a = safe_shadow_a;
    output_shadow_b = safe_shadow_b;
    record_success_locked();
    return true;
}

bool ensure_present_locked(uint32_t now_ms, bool force)
{
    if (mcp_present) {
        return true;
    }
    if (!force && !deadline_reached(now_ms, mcp_next_probe_ms)) {
        return false;
    }
    return initialize_device_locked(now_ms);
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
    if (!hal_i2c_bus_lock(HwConfig::I2C_MUTEX_TIMEOUT_MS)) {
        return false;
    }

    mcp_present = false;
    mcp_consecutive_errors = 0U;
    mcp_next_probe_ms = 0U;
    const bool ok = ensure_present_locked(millis(), true);
    hal_i2c_bus_unlock();
    return ok;
}

bool hal_mcp_is_present(void)
{
    if (!hal_i2c_bus_lock(HwConfig::I2C_MUTEX_TIMEOUT_MS)) {
        return false;
    }
    const bool present = ensure_present_locked(millis(), false);
    hal_i2c_bus_unlock();
    return present;
}

bool hal_mcp_write_channel(HwConfig::McpChannel channel, bool on)
{
    if (!channel_is_output(channel)) {
        return false;
    }
    if (on && mcp_safe_latched) {
        return false;
    }

    if (!hal_i2c_bus_lock(HwConfig::I2C_MUTEX_TIMEOUT_MS)) {
        return false;
    }

    const uint32_t now_ms = millis();
    if (!ensure_present_locked(now_ms, false)) {
        hal_i2c_bus_unlock();
        return false;
    }

    const uint8_t bit = static_cast<uint8_t>(1U << static_cast<uint8_t>(channel));
    const bool physical_high = HwConfig::RELAY_ACTIVE_LOW ? !on : on;
    uint8_t desired_shadow_a = output_shadow_a;
    if (physical_high) {
        desired_shadow_a = static_cast<uint8_t>(desired_shadow_a | bit);
    } else {
        desired_shadow_a = static_cast<uint8_t>(desired_shadow_a & ~bit);
    }

    const bool ok = write_register_locked(REG_OLATA, desired_shadow_a);
    if (ok) {
        output_shadow_a = desired_shadow_a;
        record_success_locked();
    } else {
        record_failure_locked(millis());
    }

    hal_i2c_bus_unlock();
    return ok;
}

bool hal_mcp_read_channel(HwConfig::McpChannel channel, bool *out)
{
    if (out == nullptr || !channel_is_valid(channel)) {
        return false;
    }

    if (!hal_i2c_bus_lock(HwConfig::I2C_MUTEX_TIMEOUT_MS)) {
        return false;
    }

    const uint32_t now_ms = millis();
    if (!ensure_present_locked(now_ms, false)) {
        hal_i2c_bus_unlock();
        return false;
    }

    if (channel_is_output(channel)) {
        const uint8_t bit = static_cast<uint8_t>(1U << static_cast<uint8_t>(channel));
        const bool physical_high = (output_shadow_a & bit) != 0U;
        *out = HwConfig::RELAY_ACTIVE_LOW ? !physical_high : physical_high;
        hal_i2c_bus_unlock();
        return true;
    }

    uint8_t gpio_b = 0;
    const bool ok = read_register_locked(REG_GPIOB, &gpio_b);
    if (ok) {
        const uint8_t bit = static_cast<uint8_t>(1U << (static_cast<uint8_t>(channel) - 8U));
        *out = (gpio_b & bit) != 0;
        record_success_locked();
    } else {
        record_failure_locked(millis());
    }

    hal_i2c_bus_unlock();
    return ok;
}

bool hal_mcp_read_all(uint16_t *out)
{
    if (out == nullptr) {
        return false;
    }

    if (!hal_i2c_bus_lock(HwConfig::I2C_MUTEX_TIMEOUT_MS)) {
        return false;
    }

    const uint32_t now_ms = millis();
    if (!ensure_present_locked(now_ms, false)) {
        hal_i2c_bus_unlock();
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
        record_success_locked();
    } else {
        record_failure_locked(millis());
    }

    hal_i2c_bus_unlock();
    return ok;
}

bool hal_mcp_all_relays_safe(void)
{
    if (!hal_i2c_bus_lock(HwConfig::I2C_MUTEX_TIMEOUT_MS)) {
        return false;
    }

    const uint32_t now_ms = millis();
    if (!ensure_present_locked(now_ms, false)) {
        hal_i2c_bus_unlock();
        return false;
    }

    const uint8_t safe_shadow_a = safe_relay_physical_byte();
    const bool ok = write_register_locked(REG_OLATA, safe_shadow_a);
    if (ok) {
        output_shadow_a = safe_shadow_a;
        record_success_locked();
    } else {
        record_failure_locked(millis());
    }

    hal_i2c_bus_unlock();
    return ok;
}

bool hal_mcp_latch_all_relays_safe(void)
{
    mcp_safe_latched = true;
    return hal_mcp_all_relays_safe();
}
