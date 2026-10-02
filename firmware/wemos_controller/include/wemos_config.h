#ifndef WEMOS_CONFIG_H
#define WEMOS_CONFIG_H

#include <Arduino.h>
#include <stdint.h>

namespace WemosConfig {

// Piny WeMos D1 Uno (ESP8266)
constexpr uint8_t PIN_I2C_SCL = 5;      // D1 (GPIO5)
constexpr uint8_t PIN_I2C_SDA = 4;      // D2 (GPIO4)
constexpr uint8_t PIN_ONEWIRE = 14;     // D5 (GPIO14)
constexpr uint8_t PIN_LED_STATUS = 2;   // D4 (GPIO2 - wbudowany LED, active-low)
constexpr uint8_t PIN_UART_TX = 1;      // TX (GPIO1)
constexpr uint8_t PIN_UART_RX = 3;      // RX (GPIO3)

// Adresy magistrali I2C
constexpr uint8_t ADDR_MCP23017 = 0x20;
constexpr uint8_t ADDR_ADS1115  = 0x48;
constexpr uint8_t ADDR_DS3231   = 0x68;

// Mapowanie przekaźników na MCP23017 Port A (0..7)
constexpr uint8_t RELAY_CH_LIGHT_A  = 0;
constexpr uint8_t RELAY_CH_LIGHT_B  = 1;
constexpr uint8_t RELAY_CH_FILTER   = 2;
constexpr uint8_t RELAY_CH_AERATOR  = 3;
constexpr uint8_t RELAY_CH_HEATER   = 4;
constexpr uint8_t RELAY_CH_CO2      = 5;
constexpr uint8_t RELAY_CH_FEEDER   = 6;
constexpr uint8_t RELAY_CH_SPARE    = 7;

constexpr bool RELAY_ACTIVE_LOW = true;

// Mapowanie wejść cyfrowych na MCP23017 Port B (0..7)
constexpr uint8_t INPUT_CH_FEEDER_POS       = 0;
constexpr uint8_t INPUT_CH_WATER_LEVEL_MAX  = 1;
constexpr uint8_t INPUT_CH_WATER_LEVEL_MIN  = 2;
constexpr uint8_t INPUT_CH_FLOW             = 3;

// Oba pływaki zwierają wejście MCP23017 do GND po zadziałaniu. Pull-up Portu
// B utrzymuje stan nieaktywny, a jawne polaryzacje pozwalają zmienić typ
// pływaka bez modyfikacji logiki odczytu.
constexpr bool WATER_LEVEL_MIN_ACTIVE_LOW = true;
constexpr bool WATER_LEVEL_MAX_ACTIVE_LOW = true;

// Domyślne parametry termostatu
constexpr float DEFAULT_TEMP_TARGET     = 25.0f;
constexpr float DEFAULT_TEMP_HYSTERESIS = 0.3f;
constexpr float TEMP_OVERHEAT_LIMIT     = 28.5f;
constexpr float TEMP_UNDERHEAT_LIMIT    = 21.0f;
constexpr uint32_t HEATER_MAX_RUN_MS    = 30UL * 60UL * 1000UL; // 30 min max ciągłego grzania

// Czasy i interwały
constexpr uint32_t SENSOR_READ_INTERVAL_MS   = 500UL;
constexpr uint32_t TELEMETRY_INTERVAL_MS     = 1000UL;
constexpr uint32_t SAFETY_CHECK_INTERVAL_MS  = 250UL;
constexpr uint32_t UART_BAUD_RATE            = 115200UL;

// ---- Tryb serwisowy OTA ----
// Przycisk monostabilny podłączony między PIN_OTA_BUTTON a GND.
// GPIO0 (D3) ma wbudowany pull-up 10 kΩ i jest sprawdzany w setup().
// Przytrzymanie przycisku PRZED podaniem zasilania (boot) wyzwala tryb OTA.
constexpr uint8_t  PIN_OTA_BUTTON            = 0;    // D3 (GPIO0), active-low

// Timeout watchdoga OTA — po tym czasie bez wgrania firmware WeMos restartuje się
constexpr uint32_t OTA_WATCHDOG_MS           = 5UL * 60UL * 1000UL; // 5 minut

// Fallback AP — używany gdy STA nie jest skonfigurowane lub nie może się połączyć
constexpr char     OTA_AP_SSID[]             = "AquaBox-OTA";
constexpr char     OTA_AP_PASSWORD[]         = "aquabox8";   // min. 8 znaków (wymóg WPA2)

// Dane sieci STA do OTA (opcjonalne — jeśli puste, od razu uruchamiany AP)
// Skonfiguruj przez UART lub ustaw tutaj jeśli sieć jest stała
constexpr char     OTA_STA_SSID[]            = "";   // Wypełnij SSID domowej sieci
constexpr char     OTA_STA_PASSWORD[]        = "";   // Wypełnij hasło domowej sieci
constexpr uint32_t OTA_STA_CONNECT_TIMEOUT_MS = 8000UL; // 8 sekund na połączenie STA

} // namespace WemosConfig

#endif // WEMOS_CONFIG_H
