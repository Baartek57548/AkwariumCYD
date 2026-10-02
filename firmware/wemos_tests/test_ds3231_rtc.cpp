/**
 * @file test_ds3231_rtc.cpp
 * @brief Program testowy dla Zegara Czasu Rzeczywistego DS3231 RTC.
 * 
 * Piny:
 *   SCL -> D1 (GPIO5)
 *   SDA -> D2 (GPIO4)
 *   Adres I2C: 0x68
 */

#include <Arduino.h>
#include <Wire.h>
#include <ESP8266WiFi.h>

#define PIN_I2C_SDA 4 // D2
#define PIN_I2C_SCL 5 // D1
#define DS3231_ADDR 0x68

static uint8_t bcd_to_dec(uint8_t val) {
    return ((val / 16 * 10) + (val % 16));
}

static uint8_t dec_to_bcd(uint8_t val) {
    return ((val / 10 * 16) + (val % 10));
}

struct RtcDateTime {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    float temp_c;
};

static bool read_ds3231(RtcDateTime &dt) {
    Wire.beginTransmission(DS3231_ADDR);
    Wire.write(0x00); // Rejestr sekund
    if (Wire.endTransmission() != 0) return false;

    if (Wire.requestFrom((uint8_t)DS3231_ADDR, (uint8_t)7) != 7) return false;

    dt.second = bcd_to_dec(Wire.read() & 0x7F);
    dt.minute = bcd_to_dec(Wire.read());
    dt.hour   = bcd_to_dec(Wire.read() & 0x3F);
    Wire.read(); // pomiń dzień tygodnia
    dt.day    = bcd_to_dec(Wire.read());
    dt.month  = bcd_to_dec(Wire.read() & 0x1F);
    dt.year   = 2000 + bcd_to_dec(Wire.read());

    // Odczyt wewnętrznego termometru RTC (0x11, 0x12)
    Wire.beginTransmission(DS3231_ADDR);
    Wire.write(0x11);
    if (Wire.endTransmission() == 0 && Wire.requestFrom((uint8_t)DS3231_ADDR, (uint8_t)2) == 2) {
        int8_t msb = (int8_t)Wire.read();
        uint8_t lsb = Wire.read() >> 6;
        dt.temp_c = msb + (lsb * 0.25f);
    } else {
        dt.temp_c = 0.0f;
    }
    return true;
}

static void set_ds3231_time(uint16_t y, uint8_t m, uint8_t d, uint8_t h, uint8_t min, uint8_t s) {
    Wire.beginTransmission(DS3231_ADDR);
    Wire.write(0x00);
    Wire.write(dec_to_bcd(s));
    Wire.write(dec_to_bcd(min));
    Wire.write(dec_to_bcd(h));
    Wire.write(dec_to_bcd(1)); // Dzień tygodnia (1)
    Wire.write(dec_to_bcd(d));
    Wire.write(dec_to_bcd(m));
    Wire.write(dec_to_bcd(y >= 2000 ? y - 2000 : y));
    Wire.endTransmission();
    Serial.println("[RTC] Czas został pomyślnie zaktualizowany!");
}

void setup() {
    WiFi.mode(WIFI_OFF);
    WiFi.forceSleepBegin();

    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("==================================================");
    Serial.println("     AquaCYD - Test Zegara DS3231 RTC             ");
    Serial.println("     SDA: D2 (GPIO4) | SCL: D1 (GPIO5)            ");
    Serial.println("==================================================");
    Serial.println("Wskazówka: Aby ustawić czas, wpisz w monitorze:   ");
    Serial.println("SET RRRR-MM-DD GG:MM:SS (np. SET 2026-09-10 14:30:00)");
    Serial.println("==================================================");

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(100000);

    // Sprawdzenie komunikacji
    Wire.beginTransmission(DS3231_ADDR);
    if (Wire.endTransmission() != 0) {
        Serial.println("[BŁĄD] Brak odpowiedzi z modułu DS3231 na adresie 0x68!");
        Serial.println("Sprawdź połączenia I2C oraz czy bateria CR2032 jest włożona.");
    } else {
        Serial.println("[OK] Moduł DS3231 wykryty pomyślnie.");
    }
}

void loop() {
    // Sprawdzenie komendy z Serial
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        if (cmd.startsWith("SET ")) {
            int y, m, d, h, min, s;
            if (sscanf(cmd.c_str(), "SET %d-%d-%d %d:%d:%d", &y, &m, &d, &h, &min, &s) == 6) {
                set_ds3231_time(y, m, d, h, min, s);
            } else {
                Serial.println("[BŁĄD] Nieprawidłowy format. Wpisz: SET RRRR-MM-DD GG:MM:SS");
            }
        }
    }

    RtcDateTime dt;
    if (read_ds3231(dt)) {
        Serial.printf("[DS3231] Czas: %04d-%02d-%02d %02d:%02d:%02d | Temp modułu: %.2f °C\n",
                      dt.year, dt.month, dt.day,
                      dt.hour, dt.minute, dt.second,
                      dt.temp_c);
    } else {
        Serial.println("[DS3231] Błąd odczytu danych z magistrali I2C!");
    }

    delay(1000);
}
