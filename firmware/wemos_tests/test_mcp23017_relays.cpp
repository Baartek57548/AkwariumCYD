/**
 * @file test_mcp23017_relays.cpp
 * @brief Program testowy dla ekspandera MCP23017:
 *        - Port A (GPA0-7): Cykliczne załączanie 8 przekaźników (Active-Low)
 *        - Port B (GPB0-7): Odczyt wejść binarnych, w tym pływaków MIN/MAX (Pull-Up)
 * 
 * Piny:
 *   SCL -> D1 (GPIO5)
 *   SDA -> D2 (GPIO4)
 */

#include <Arduino.h>
#include <Wire.h>
#include <ESP8266WiFi.h>

#define PIN_I2C_SDA 4 // D2
#define PIN_I2C_SCL 5 // D1
#define PIN_LED_STATUS 2 // D4 (Active-Low)

#define MCP23017_ADDR 0x20
#define MCP_IODIRA    0x00
#define MCP_IODIRB    0x01
#define MCP_GPPUA     0x0C
#define MCP_GPPUB     0x0D
#define MCP_GPIOA     0x12
#define MCP_GPIOB     0x13
#define MCP_OLATA     0x14
#define MCP_OLATB     0x15

static const char* RELAY_NAMES[8] = {
    "Przekaźnik 1: Grzałka (Heater)",
    "Przekaźnik 2: Chłodzenie (Cooler/Fan)",
    "Przekaźnik 3: Światło przód (Front Light)",
    "Przekaźnik 4: Światło tył (Plant Light)",
    "Przekaźnik 5: Filtr główny (Main Filter)",
    "Przekaźnik 6: Napowietrzacz (Aerator)",
    "Przekaźnik 7: Zawór CO2 (Solenoid)",
    "Przekaźnik 8: Karmnik automatyczny (Feeder)"
};

static bool write_reg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(MCP23017_ADDR);
    Wire.write(reg);
    Wire.write(val);
    return (Wire.endTransmission() == 0);
}

static uint8_t read_reg(uint8_t reg) {
    Wire.beginTransmission(MCP23017_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission() != 0) return 0xFF;
    Wire.requestFrom((uint8_t)MCP23017_ADDR, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0xFF;
}

void setup() {
    WiFi.mode(WIFI_OFF);
    WiFi.forceSleepBegin();

    pinMode(PIN_LED_STATUS, OUTPUT);
    digitalWrite(PIN_LED_STATUS, HIGH); // LED OFF

    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("==================================================");
    Serial.println("   AquaCYD - Test Przekaźników & Czujników MCP   ");
    Serial.println("   MCP23017 @ 0x20 (SDA: D2, SCL: D1)             ");
    Serial.println("==================================================");

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(100000);

    // Sprawdzenie obecności układu
    Wire.beginTransmission(MCP23017_ADDR);
    if (Wire.endTransmission() != 0) {
        Serial.println("[BŁĄD KRYTYCZNY] Nie wykryto MCP23017 na adresie 0x20!");
        Serial.println("Upewnij się, że linie SDA/SCL są podłączone i układ ma zasilanie.");
        while (true) {
            digitalWrite(PIN_LED_STATUS, !digitalRead(PIN_LED_STATUS));
            delay(200);
        }
    }

    // Konfiguracja Port A: wszystkie piny jako WYJŚCIA (0x00)
    write_reg(MCP_IODIRA, 0x00);
    // Wszystkie przekaźniki wyłączone na starcie (0xFF - Active Low)
    write_reg(MCP_OLATA, 0xFF);

    // Konfiguracja Port B: piny jako WEJŚCIA (0xFF) z PULL-UP (0xFF)
    write_reg(MCP_IODIRB, 0xFF);
    write_reg(MCP_GPPUB, 0xFF);

    Serial.println("[OK] MCP23017 zainicjalizowany poprawnie.");
    Serial.println("Rozpoczynam sekwencję testową przekaźników...");
}

void loop() {
    // 1. Sekwencja załączania każdego przekaźnika pojedynczo
    for (uint8_t ch = 0; ch < 8; ch++) {
        // Załącz wybrany przekaźnik (0 = ON, 1 = OFF dla modułu Active-Low)
        uint8_t relay_mask = ~(1 << ch);
        write_reg(MCP_OLATA, relay_mask);
        digitalWrite(PIN_LED_STATUS, LOW); // LED ON

        // Odczyt wejść cyfrowych z Portu B
        uint8_t port_b = read_reg(MCP_GPIOB);
        bool feeder_position = (port_b & (1 << 0)) == 0; // zwarty do GND = aktywny
        bool float_max = (port_b & (1 << 1)) == 0;
        bool float_min = (port_b & (1 << 2)) == 0;

        Serial.printf("[KROK %d/8] ZAŁĄCZONY -> %s\n", ch + 1, RELAY_NAMES[ch]);
        Serial.printf("           Odczyt Port B [GPB0-2]: KRAŃCÓWKA=%s | MAX=%s | MIN=%s\n",
                      feeder_position ? "AKTYWNA (GND)" : "NIEAKTYWNA (HIGH)",
                      float_max ? "ZALANY (GND)" : "SUCHY (HIGH)",
                      float_min ? "NISKI POZIOM (GND)" : "POZIOM OK (HIGH)");

        delay(1200);
    }

    // 2. Wyłączenie wszystkich przekaźników
    Serial.println("[PAUZA] Wszystkie przekaźniki WYŁĄCZONE (bezpieczna przerwa 2s)...");
    write_reg(MCP_OLATA, 0xFF);
    digitalWrite(PIN_LED_STATUS, HIGH); // LED OFF
    delay(2000);
}
