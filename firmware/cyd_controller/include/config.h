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

namespace UartConsole {
constexpr uint8_t PORT = 0;
constexpr int8_t TX_PIN = 1;
constexpr int8_t RX_PIN = 3;
constexpr uint32_t BAUD = 115200UL;
constexpr char FORMAT[] = "8N1";
}

namespace OneWireBus {
// GPIO17 is the blue RGB channel on some CYD revisions. This project disables
// the onboard RGB LED so the pin belongs exclusively to the DS18B20 bus.
constexpr uint8_t DATA_PIN = 17;
constexpr uint32_t MUTEX_TIMEOUT_MS = 250UL;
}

constexpr int LDR_PIN = 34;

namespace Audio {
constexpr uint8_t SPEAKER_PIN = 26;
constexpr uint8_t LEDC_CHANNEL = 0;
}

namespace OnboardRgb {
constexpr bool ENABLED = false;
constexpr uint8_t RED_PIN = 4;
constexpr uint8_t GREEN_PIN = 16;
constexpr uint8_t BLUE_PIN = 17;
constexpr bool ACTIVE_LOW = true;
}

namespace Display {
constexpr uint16_t WIDTH = 320;
constexpr uint16_t HEIGHT = 240;
constexpr uint16_t PANEL_WIDTH = 240;
constexpr uint16_t PANEL_HEIGHT = 320;
constexpr uint8_t ROTATION = 3;
constexpr int8_t MOSI_PIN = 13;
constexpr int8_t MISO_PIN = 12;
constexpr int8_t SCLK_PIN = 14;
constexpr int8_t DC_PIN = 2;
constexpr int8_t CS_PIN = 15;
constexpr int8_t RESET_PIN = -1;
constexpr int8_t BUSY_PIN = -1;
constexpr uint32_t SPI_WRITE_FREQUENCY_HZ = 40000000UL;
constexpr uint32_t SPI_READ_FREQUENCY_HZ = 16000000UL;
constexpr uint16_t LVGL_DRAW_BUFFER_LINES = 40;
constexpr uint16_t RGB565_STREAM_ROWS = 8;
}

namespace Touch {
constexpr int8_t SPI_HOST = -1;
constexpr int8_t CS_PIN = 33;
constexpr int8_t SCLK_PIN = 25;
constexpr int8_t MOSI_PIN = 32;
constexpr int8_t MISO_PIN = 39;
constexpr int8_t IRQ_PIN = 36;
constexpr uint16_t X_MIN = 300;
constexpr uint16_t X_MAX = 3900;
constexpr uint16_t Y_MIN = 200;
constexpr uint16_t Y_MAX = 3700;
constexpr uint32_t SPI_FREQUENCY_HZ = 1000000UL;
constexpr bool INVERT_Y = true;
constexpr uint32_t PRESS_DEBOUNCE_MS = 30UL;
constexpr uint32_t RELEASE_DEBOUNCE_MS = 45UL;
constexpr uint32_t WAKE_RELEASE_DEBOUNCE_MS = 75UL;
constexpr int16_t POSITION_HYSTERESIS_PX = 3;
constexpr int16_t POSITION_SNAP_THRESHOLD_PX = 28;
constexpr bool DEBUG_LOGGING = true;
}

namespace Backlight {
constexpr uint8_t PIN = 21;
constexpr uint8_t LEDC_CHANNEL = 7;
constexpr uint32_t PWM_FREQUENCY_HZ = 5000UL;
constexpr uint8_t PWM_RESOLUTION_BITS = 8;
constexpr uint16_t PWM_MAX_DUTY = (1U << PWM_RESOLUTION_BITS) - 1U;
}

static_assert(!OnboardRgb::ENABLED ||
                  OneWireBus::DATA_PIN != OnboardRgb::BLUE_PIN,
              "GPIO17 cannot drive RGB blue and OneWire at the same time.");
static_assert(LDR_PIN != Touch::IRQ_PIN && LDR_PIN != Touch::MISO_PIN,
              "The LDR ADC input must not share a touch input pin.");
static_assert(Audio::SPEAKER_PIN != I2C_SDA_PIN &&
                  Audio::SPEAKER_PIN != I2C_SCL_PIN &&
                  Audio::SPEAKER_PIN != Backlight::PIN,
              "Audio pin conflicts with I2C or backlight.");
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

static_assert(Display::CS_PIN != Touch::CS_PIN &&
                  Display::CS_PIN != SdCard::CS_PIN &&
                  Touch::CS_PIN != SdCard::CS_PIN,
              "LCD, touch and SD require separate chip-select pins.");

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

namespace Debounce {
constexpr uint32_t DIGITAL_INPUT_MS = 30UL;
constexpr uint32_t FEEDER_PULSE_MS = 500UL;
}

} // namespace HwConfig

namespace FirmwareInfo {

constexpr char VERSION[] = "6.0.0";
constexpr uint8_t API_VERSION = 2U;
constexpr uint32_t SECURITY_VERSION = 2U;
constexpr uint16_t BOOTLOADER_COMPATIBILITY_VERSION = 1U;

} // namespace FirmwareInfo

#ifndef AQUARIUM_ALLOW_UNSIGNED_ARDUINO_OTA
#define AQUARIUM_ALLOW_UNSIGNED_ARDUINO_OTA 0
#endif

namespace Secrets {

constexpr char OTA_HOSTNAME[] = "akwarium";
constexpr char OTA_AP_SSID[] = "cydAkwarium-OTA";

} // namespace Secrets

#endif
