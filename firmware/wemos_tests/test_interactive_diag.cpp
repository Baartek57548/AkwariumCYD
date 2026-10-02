/**
 * @file test_interactive_diag.cpp
 * @brief Kompleksowy, interaktywny program diagnostyczny dla WeMos D1 (ESP8266).
 * 
 * Pozwala na przetestowanie wszystkich modułów z poziomu jednego programu
 * poprzez menu w Monitorze Szeregowym (115200 baud):
 *   [1] Skaner magistrali I2C
 *   [2] Sekwencyjny test 8 przekaźników (MCP23017)
 *   [3] Monitor wejść cyfrowych: pływaki MIN/MAX i zalanie (MCP23017 Port B)
 *   [4] Odczyt temperatury wody (DS18B20 1-Wire)
 *   [5] Odczyt czasu rzeczywistego (DS3231 RTC)
 *   [6] Odczyt napięć i sondy pH (ADS1115 ADC)
 *   [7] Pełny automatyczny Self-Test z raportem gotowości sprzętowej
 */

#include <Arduino.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ESP8266WiFi.h>

#define PIN_I2C_SDA     4  // D2
#define PIN_I2C_SCL     5  // D1
#define PIN_ONEWIRE     14 // D5
#define PIN_LED_STATUS  2  // D4

#define MCP23017_ADDR   0x20
#define ADS1115_ADDR    0x48
#define DS3231_ADDR     0x68

static OneWire s_oneWire(PIN_ONEWIRE);
static DallasTemperature s_sensors(&s_oneWire);

static uint8_t bcd_to_dec(uint8_t val) {
    return ((val / 16 * 10) + (val % 16));
}

static bool mcp_write(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(MCP23017_ADDR);
    Wire.write(reg);
    Wire.write(val);
    return (Wire.endTransmission() == 0);
}

static uint8_t mcp_read(uint8_t reg) {
    Wire.beginTransmission(MCP23017_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission() != 0) return 0xFF;
    Wire.requestFrom((uint8_t)MCP23017_ADDR, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0xFF;
}

static int16_t ads_read(uint8_t ch) {
    if (ch > 3) return 0;
    uint16_t config = 0x8000 | ((0x4 + ch) << 12) | 0x0200 | 0x0100 | 0x0080 | 0x0003;
    Wire.beginTransmission(ADS1115_ADDR);
    Wire.write(0x01);
    Wire.write((uint8_t)(config >> 8));
    Wire.write((uint8_t)(config & 0xFF));
    if (Wire.endTransmission() != 0) return 0;
    delay(9);
    Wire.beginTransmission(ADS1115_ADDR);
    Wire.write(0x00);
    if (Wire.endTransmission() != 0) return 0;
    if (Wire.requestFrom((uint8_t)ADS1115_ADDR, (uint8_t)2) != 2) return 0;
    return (int16_t)((Wire.read() << 8) | Wire.read());
}

static void print_menu() {
    Serial.println();
    Serial.println("==========================================================");
    Serial.println("       AquaCYD - Kompleksowa Diagnostyka WeMos D1         ");
    Serial.println("==========================================================");
    Serial.println("  [1] Skaner magistrali I2C (D1 SCL, D2 SDA)");
    Serial.println("  [2] Test przekaźników (kolejno 1 do 8)");
    Serial.println("  [3] Monitor wejść (Pływak MIN, Pływak MAX, Zalanie)");
    Serial.println("  [4] Odczyt temperatury wody (DS18B20 na D5)");
    Serial.println("  [5] Odczyt zegara czasu rzeczywistego (DS3231 RTC)");
    Serial.println("  [6] Odczyt przetwornika ADC i sondy pH (ADS1115)");
    Serial.println("  [7] Pełny automatyczny Self-Test (Wszystkie moduły)");
    Serial.println("==========================================================");
    Serial.print("Wybierz opcję [1-7]: ");
}

static void run_i2c_scan() {
    Serial.println("\n\n>>> [1] SKANOWANIE MAGISTRALI I2C <<<");
    uint8_t found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            const char* desc = "Inne urządzenie";
            if (addr == 0x20) desc = "MCP23017 (8x Przekaźnik + Wejścia)";
            else if (addr == 0x48) desc = "ADS1115 (ADC 16-bit)";
            else if (addr == 0x57) desc = "AT24C32 (EEPROM na RTC)";
            else if (addr == 0x68) desc = "DS3231 (RTC Zegar Czasu)";
            Serial.printf("  [0x%02X] %s\n", addr, desc);
            found++;
        }
    }
    Serial.printf("Wykryto urządzeń: %d\n", found);
}

static void run_relay_test() {
    Serial.println("\n\n>>> [2] TEST PRZEKAŹNIKÓW (MCP23017 Port A) <<<");
    Serial.println("Załączam kolejno przekaźniki od 1 do 8 na 1 sekundę...");
    mcp_write(0x00, 0x00); // IODIRA = wyjścia

    for (uint8_t ch = 0; ch < 8; ch++) {
        mcp_write(0x14, ~(1 << ch)); // OLATA Active Low
        Serial.printf("  -> Przekaźnik #%d: WŁĄCZONY (KLIK)\n", ch + 1);
        delay(1000);
    }
    mcp_write(0x14, 0xFF); // Wyłącz wszystkie
    Serial.println("  -> Wszystkie przekaźniki wyłączone. Test zakończony.");
}

static void run_inputs_monitor() {
    Serial.println("\n\n>>> [3] MONITOR WEJŚĆ CYFROWYCH (Port B) <<<");
    Serial.println("Wciśnij dowolny klawisz, aby zakończyć podgląd.");
    mcp_write(0x01, 0xFF); // IODIRB = wejścia
    mcp_write(0x0D, 0xFF); // GPPUB = pull-up

    while (!Serial.available()) {
        uint8_t val = mcp_read(0x13);
        bool feeder_position = (val & 0x01) == 0;
        bool max_float = (val & 0x02) == 0;
        bool min_float = (val & 0x04) == 0;

        Serial.printf("  Krańcówka karmnika: [%s] | Pływak MAX: [%s] | Pływak MIN: [%s]\r",
                      feeder_position ? "AKTYWNA / GND" : "NIEAKTYWNA / HIGH",
                      max_float ? "ZALANY / GND" : "SUCHY / HIGH",
                      min_float ? "NISKI POZIOM / GND" : "POZIOM OK / HIGH");
        delay(250);
    }
    while (Serial.available()) Serial.read();
    Serial.println();
}

static void run_temp_test() {
    Serial.println("\n\n>>> [4] ODCZYT TEMPERATURY DS18B20 (D5) <<<");
    s_sensors.requestTemperatures();
    float t = s_sensors.getTempCByIndex(0);
    if (t <= -100.0f) {
        Serial.println("  [BŁĄD] Czujnik DS18B20 nie odpowiada (-127.0 °C).");
        Serial.println("  Sprawdź czy rezystor 4.7kΩ łączy pin D5 z 3.3V!");
    } else {
        Serial.printf("  [OK] Zmierzona temperatura wody: %.2f °C\n", t);
    }
}

static void run_rtc_test() {
    Serial.println("\n\n>>> [5] ODCZYT ZEGARA DS3231 RTC <<<");
    Wire.beginTransmission(DS3231_ADDR);
    Wire.write(0x00);
    if (Wire.endTransmission() != 0 || Wire.requestFrom((uint8_t)DS3231_ADDR, (uint8_t)7) != 7) {
        Serial.println("  [BŁĄD] Brak komunikacji z DS3231 (0x68)!");
        return;
    }
    uint8_t s = bcd_to_dec(Wire.read() & 0x7F);
    uint8_t m = bcd_to_dec(Wire.read());
    uint8_t h = bcd_to_dec(Wire.read() & 0x3F);
    Wire.read();
    uint8_t d = bcd_to_dec(Wire.read());
    uint8_t mo = bcd_to_dec(Wire.read() & 0x1F);
    uint16_t y = 2000 + bcd_to_dec(Wire.read());

    Serial.printf("  [OK] Data i czas z DS3231: %04d-%02d-%02d %02d:%02d:%02d\n",
                  y, mo, d, h, m, s);
}

static void run_adc_test() {
    Serial.println("\n\n>>> [6] ODCZYT PRZETWORNIKA ADS1115 (ADC) <<<");
    for (uint8_t ch = 0; ch < 4; ch++) {
        int16_t raw = ads_read(ch);
        float mv = raw * 0.125f;
        Serial.printf("  Kanał A%d: raw=%6d, napięcie=%7.2f mV (%5.3f V)", ch, raw, mv, mv / 1000.0f);
        if (ch == 0) {
            float ph = 7.0f + ((2500.0f - mv) / 59.16f);
            Serial.printf(" [Est. pH: %.2f]", ph);
        }
        Serial.println();
    }
}

static void run_full_self_test() {
    Serial.println("\n\n==========================================================");
    Serial.println("          PEŁNY AUTOMATYCZNY SELF-TEST SPRZĘTOWY          ");
    Serial.println("==========================================================");

    // 1. I2C Check
    Wire.beginTransmission(MCP23017_ADDR);
    bool mcp_ok = (Wire.endTransmission() == 0);

    Wire.beginTransmission(ADS1115_ADDR);
    bool ads_ok = (Wire.endTransmission() == 0);

    Wire.beginTransmission(DS3231_ADDR);
    bool rtc_ok = (Wire.endTransmission() == 0);

    // 2. Temp Check
    s_sensors.requestTemperatures();
    float t = s_sensors.getTempCByIndex(0);
    bool temp_ok = (t > -50.0f && t < 70.0f);

    Serial.printf(" 1. Ekspander MCP23017 (0x20): %s\n", mcp_ok ? "[PASS] Wykryty" : "[FAIL] BRAK");
    Serial.printf(" 2. Przetwornik ADS1115 (0x48): %s\n", ads_ok ? "[PASS] Wykryty" : "[FAIL] BRAK");
    Serial.printf(" 3. Zegar DS3231 RTC   (0x68): %s\n", rtc_ok ? "[PASS] Wykryty" : "[FAIL] BRAK");
    Serial.printf(" 4. Czujnik DS18B20     (D5):  %s (%.2f °C)\n", temp_ok ? "[PASS] OK" : "[FAIL] BŁĄD", t);

    if (mcp_ok && ads_ok && rtc_ok && temp_ok) {
        Serial.println("\n>>> WYNIK: WSZYSTKIE MODUŁY PRACUJĄ PRAWIDŁOWO! (100% PASS) <<<");
    } else {
        Serial.println("\n>>> WYNIK: WYKRYTO PROBLEMY ZE SPRZĘTEM. SPRAWDŹ POŁĄCZENIA. <<<");
    }
    Serial.println("==========================================================");
}

void setup() {
    WiFi.mode(WIFI_OFF);
    WiFi.forceSleepBegin();

    pinMode(PIN_LED_STATUS, OUTPUT);
    digitalWrite(PIN_LED_STATUS, HIGH);

    Serial.begin(115200);
    delay(1000);

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(100000);
    s_sensors.begin();

    print_menu();
}

void loop() {
    if (Serial.available()) {
        char c = Serial.read();
        while (Serial.available()) Serial.read(); // flush

        switch (c) {
            case '1': run_i2c_scan(); break;
            case '2': run_relay_test(); break;
            case '3': run_inputs_monitor(); break;
            case '4': run_temp_test(); break;
            case '5': run_rtc_test(); break;
            case '6': run_adc_test(); break;
            case '7': run_full_self_test(); break;
            default:  break;
        }
        delay(500);
        print_menu();
    }
}
