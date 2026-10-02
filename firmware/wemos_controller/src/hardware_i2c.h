#ifndef HARDWARE_I2C_H
#define HARDWARE_I2C_H

#include <Arduino.h>
#include <stdint.h>

class HardwareI2C {
public:
    HardwareI2C();

    bool begin();

    // MCP23017 - Przekaźniki
    bool setRelay(uint8_t channel, bool on);
    bool setAllRelays(uint8_t bitmask);
    uint8_t getRelayStates() const { return relay_state_mask_; }

    // MCP23017 - Wejścia cyfrowe
    bool readDigitalInputs(uint8_t &inputs);
    bool isWaterLevelLow() const { return water_level_low_; }
    bool isWaterLevelMaxReached() const { return water_level_max_reached_; }
    bool areWaterLevelInputsValid() const { return water_level_inputs_valid_; }

    // ADS1115 - Pomiary ADC
    float readPh(float cal_offset = 0.0f, float cal_slope = 1.0f);
    float readEc();

    // DS3231 - Zegar RTC
    uint32_t getEpochTime();
    bool setEpochTime(uint32_t epoch);
    bool isRtcRunning();

    // Statusy diagnostyczne
    bool isMcpPresent() const { return mcp_ok_; }
    bool isAdsPresent() const { return ads_ok_; }
    bool isRtcPresent() const { return rtc_ok_; }

private:
    uint8_t relay_state_mask_;
    uint8_t physical_shadow_a_;
    bool water_level_low_;
    bool water_level_max_reached_;
    bool water_level_inputs_valid_;
    bool mcp_ok_;
    bool ads_ok_;
    bool rtc_ok_;

    bool writeMcpRegister(uint8_t reg, uint8_t val);
    bool readMcpRegister(uint8_t reg, uint8_t &val);
    int16_t readAdsRaw(uint8_t channel);
};

extern HardwareI2C g_hardware_i2c;

#endif // HARDWARE_I2C_H
