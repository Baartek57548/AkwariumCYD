/**
 * @file test_ads1115_adc.cpp
 * @brief Program testowy dla 16-bitowego przetwornika ADS1115 ADC.
 * 
 * Piny:
 *   SCL -> D1 (GPIO5)
 *   SDA -> D2 (GPIO4)
 *   Adres I2C: 0x48 (ADDR zwarty do GND)
 */

#include <Arduino.h>
#include <Wire.h>
#include <ESP8266WiFi.h>

#define PIN_I2C_SDA 4 // D2
#define PIN_I2C_SCL 5 // D1
#define ADS1115_ADDR 0x48

// Rejestry ADS1115
#define ADS_REG_CONV   0x00
#define ADS_REG_CONFIG 0x01

static int16_t read_channel_raw(uint8_t channel) {
    if (channel > 3) return 0;

    // Config: OS=1 (start conversion), MUX = 100 + channel (single-ended AINx vs GND)
    // PGA = 001 (+/- 4.096V), MODE = 1 (single-shot), DR = 100 (128 SPS), COMP_QUE = 11 (disable)
    uint16_t mux = (0x4 + channel) << 12;
    uint16_t pga = 0x0200; // +/- 4.096V (1 bit = 0.125 mV)
    uint16_t mode = 0x0100; // Single-shot
    uint16_t dr = 0x0080;   // 128 SPS
    uint16_t comp = 0x0003; // Disable comparator
    uint16_t config = 0x8000 | mux | pga | mode | dr | comp;

    Wire.beginTransmission(ADS1115_ADDR);
    Wire.write(ADS_REG_CONFIG);
    Wire.write((uint8_t)(config >> 8));
    Wire.write((uint8_t)(config & 0xFF));
    if (Wire.endTransmission() != 0) return 0;

    // Czas konwersji przy 128 SPS to ok. 8 ms
    delay(9);

    Wire.beginTransmission(ADS1115_ADDR);
    Wire.write(ADS_REG_CONV);
    if (Wire.endTransmission() != 0) return 0;

    if (Wire.requestFrom((uint8_t)ADS1115_ADDR, (uint8_t)2) != 2) return 0;

    int16_t val = (int16_t)((Wire.read() << 8) | Wire.read());
    return val;
}

void setup() {
    WiFi.mode(WIFI_OFF);
    WiFi.forceSleepBegin();

    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("==================================================");
    Serial.println("     AquaCYD - Test Przetwornika ADS1115 (ADC)    ");
    Serial.println("     SDA: D2 (GPIO4) | SCL: D1 (GPIO5)            ");
    Serial.println("==================================================");

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(100000);

    Wire.beginTransmission(ADS1115_ADDR);
    if (Wire.endTransmission() != 0) {
        Serial.println("[BŁĄD] ADS1115 nie odpowiada na adresie 0x48!");
        Serial.println("Upewnij się, że pin ADDR modułu ADS1115 jest podłączony do GND.");
    } else {
        Serial.println("[OK] ADS1115 wykryty pomyślnie. Zakres pomiarowy: +/- 4.096V (0.125 mV/LSB)");
    }
}

void loop() {
    Serial.println("--- Pomiary napięć na wejściach analogowych ---");

    for (uint8_t ch = 0; ch < 4; ch++) {
        int16_t raw = read_channel_raw(ch);
        float voltage_mv = raw * 0.125f; // mV w trybie PGA 4.096V

        Serial.printf("  [AIN%d] Raw: %6d | Napięcie: %7.2f mV (%5.3f V)",
                      ch, raw, voltage_mv, voltage_mv / 1000.0f);

        // Dodatkowa estymacja pH dla kanału A0 (typowy moduł pH z offsetem 2.5V = pH 7.0)
        if (ch == 0) {
            float approx_ph = 7.0f + ((2500.0f - voltage_mv) / 59.16f);
            if (approx_ph >= 0.0f && approx_ph <= 14.0f) {
                Serial.printf(" -> Estymacja pH: ~%.2f", approx_ph);
            }
        }
        Serial.println();
    }

    Serial.println();
    delay(1500);
}
