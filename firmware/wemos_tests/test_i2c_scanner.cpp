/**
 * @file test_i2c_scanner.cpp
 * @brief Skaner magistrali I2C dla WeMos D1 (ESP8266).
 * 
 * Piny:
 *   SCL -> D1 (GPIO5)
 *   SDA -> D2 (GPIO4)
 * 
 * Wykrywa układy AquaCYD:
 *   0x20: MCP23017 (8x Przekaźnik + Czujniki pływakowe/zalania)
 *   0x48: ADS1115 (Przetwornik analogowo-cyfrowy pH/EC)
 *   0x57: AT24C32 (Pamięć EEPROM na płytce DS3231)
 *   0x68: DS3231 (Zegar Czasu Rzeczywistego RTC)
 */

#include <Arduino.h>
#include <Wire.h>
#include <ESP8266WiFi.h>

#define PIN_I2C_SDA 4 // D2
#define PIN_I2C_SCL 5 // D1

static const char* identify_device(uint8_t address) {
    switch (address) {
        case 0x20: return "MCP23017 (8x Przekaźniki + Wejścia cyfrowe)";
        case 0x21: return "MCP23017 (Alternatywny adres A0=1)";
        case 0x48: return "ADS1115 (ADC 16-bit dla sondy pH/EC)";
        case 0x49: return "ADS1115 (Alternatywny adres ADDR=VDD)";
        case 0x57: return "AT24C32 (EEPROM na module DS3231 RTC)";
        case 0x68: return "DS3231 (Zegar Czasu Rzeczywistego RTC)";
        default:   return "Nieznane urządzenie I2C";
    }
}

void setup() {
    // Wyłączenie Wi-Fi dla stabilności i braku zakłóceń
    WiFi.mode(WIFI_OFF);
    WiFi.forceSleepBegin();

    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("==================================================");
    Serial.println("   AquaCYD - Skaner Magistrali I2C (WeMos D1)     ");
    Serial.println("   SDA: D2 (GPIO4) | SCL: D1 (GPIO5)              ");
    Serial.println("==================================================");

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(100000); // 100 kHz
}

void loop() {
    Serial.println("\n[I2C] Rozpoczynam skanowanie adresów 0x01 .. 0x7F...");

    uint8_t devices_found = 0;
    bool found_mcp = false;
    bool found_ads = false;
    bool found_rtc = false;

    for (uint8_t address = 1; address < 127; address++) {
        Wire.beginTransmission(address);
        uint8_t error = Wire.endTransmission();

        if (error == 0) {
            Serial.printf("  -> Znaleziono: 0x%02X | %s\n", address, identify_device(address));
            devices_found++;

            if (address == 0x20) found_mcp = true;
            if (address == 0x48) found_ads = true;
            if (address == 0x68) found_rtc = true;
        } else if (error == 4) {
            Serial.printf("  -> Błąd magistrali na adresie 0x%02X\n", address);
        }
    }

    Serial.println("--------------------------------------------------");
    Serial.printf("Liczba wykrytych urządzeń: %d\n", devices_found);
    Serial.println("Status kluczowych modułów AquaCYD:");
    Serial.printf("  * MCP23017 (0x20): %s\n", found_mcp ? "[OK] Wykryty" : "[BLAD] Brak odpowiedzi (sprawdz zasilanie 5V/3.3V i I2C)");
    Serial.printf("  * ADS1115  (0x48): %s\n", found_ads ? "[OK] Wykryty" : "[BLAD] Brak odpowiedzi (sprawdz linie I2C)");
    Serial.printf("  * DS3231   (0x68): %s\n", found_rtc ? "[OK] Wykryty" : "[BLAD] Brak odpowiedzi (sprawdz baterie i I2C)");
    Serial.println("--------------------------------------------------");
    Serial.println("Kolejne skanowanie za 4 sekundy...\n");

    delay(4000);
}
