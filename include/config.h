#ifndef AQUARIUM_CONFIG_H
#define AQUARIUM_CONFIG_H

#include <Arduino.h>

namespace HwConfig {

constexpr uint8_t I2C_SDA_PIN = 27;
constexpr uint8_t I2C_SCL_PIN = 22;
constexpr uint32_t I2C_FREQUENCY_HZ = 400000UL;
constexpr uint32_t I2C_MUTEX_TIMEOUT_MS = 50;

constexpr uint8_t MCP23017_ADDR = 0x20;
constexpr uint8_t ADS1115_ADDR = 0x48;

constexpr int LDR_PIN = 34;
constexpr int DEV_TEMP_ADC_PIN = 35;
constexpr int DEV_PH_ADC_PIN = 34;

namespace SdCard {
constexpr uint8_t CS_PIN = 5;
constexpr uint8_t SCLK_PIN = 18;
constexpr uint8_t MISO_PIN = 19;
constexpr uint8_t MOSI_PIN = 23;
constexpr uint32_t SPI_FREQUENCY_HZ = 20000000UL;
constexpr char WELCOME_ANIM_PATH[] = "/aq/assets/images/splash/welcome.anim";
constexpr char WELCOME_FRAME_PATTERN[] = "/aq/assets/images/splash/frames/welcome_%03u.rgb565";
constexpr char WELCOME_POSTER_PATH[] = "/aq/assets/images/splash/welcome-320x240.rgb565";
constexpr uint16_t WELCOME_FRAME_COUNT = 40;
constexpr uint16_t WELCOME_FRAME_RATE_FPS = 8;
constexpr uint16_t WELCOME_WIDTH = 320;
constexpr uint16_t WELCOME_HEIGHT = 240;
}

constexpr bool RELAY_ACTIVE_LOW = true;
constexpr bool RELAY_SAFE_STATE_ON = false;

enum McpChannel : uint8_t {
    CH_LIGHT_A = 0,
    CH_LIGHT_B = 1,
    CH_FILTER = 2,
    CH_AERATOR = 3,
    CH_HEATER = 4,
    CH_CO2 = 5,
    CH_FEEDER_DRIVE = 6,
    CH_RELAY_SPARE = 7,
    CH_FEEDER_POS = 8,
    CH_LEAK = 9,
    CH_WATER_LEVEL = 10,
    CH_FLOW_PULSE = 11,
    CH_IN_SPARE_1 = 12,
    CH_IN_SPARE_2 = 13,
    CH_IN_SPARE_3 = 14,
    CH_IN_SPARE_4 = 15
};

enum AdcChannel : uint8_t {
    ADC_PH = 0,
    ADC_EC = 1,
    ADC_ANALOG = 2,
    ADC_SPARE = 3
};

namespace Lights {
constexpr uint32_t RESET_POWER_OFF_MS = 10000UL;
constexpr uint32_t ADVANCE_PULSE_ON_MS = 250UL;
constexpr uint32_t ADVANCE_PULSE_GAP_MS = 350UL;
constexpr uint8_t MODE_COUNT = 3;
}

namespace Debounce {
constexpr uint32_t DIGITAL_INPUT_MS = 30UL;
constexpr uint32_t FEEDER_PULSE_MS = 500UL;
}

} // namespace HwConfig

namespace Secrets {

constexpr char DEFAULT_PIN[] = "1234";
constexpr char OTA_HOSTNAME[] = "cydAquarium";
constexpr char OTA_PASSWORD[] = "password123";

} // namespace Secrets

#endif
