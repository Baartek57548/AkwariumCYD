/**
 * @file test_ds18b20_temp.cpp
 * @brief Program testowy dla czujnika temperatury DS18B20 (1-Wire).
 * 
 * Pin:
 *   DATA -> D5 (GPIO14)
 *   Wymagany rezystor pull-up: 4.7kΩ pomiędzy D5 (Data) a 3.3V.
 */

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ESP8266WiFi.h>

#define PIN_ONEWIRE 14   // D5
#define PIN_LED_STATUS 2 // D4

OneWire oneWire(PIN_ONEWIRE);
DallasTemperature sensors(&oneWire);

void printAddress(DeviceAddress deviceAddress) {
    for (uint8_t i = 0; i < 8; i++) {
        if (deviceAddress[i] < 16) Serial.print("0");
        Serial.print(deviceAddress[i], HEX);
        if (i < 7) Serial.print(":");
    }
}

void setup() {
    WiFi.mode(WIFI_OFF);
    WiFi.forceSleepBegin();

    pinMode(PIN_LED_STATUS, OUTPUT);
    digitalWrite(PIN_LED_STATUS, HIGH);

    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("==================================================");
    Serial.println("   AquaCYD - Test Czujnika Temperatury DS18B20    ");
    Serial.println("   Linia 1-Wire: Pin D5 (GPIO14)                  ");
    Serial.println("==================================================");

    sensors.begin();
    int count = sensors.getDeviceCount();
    Serial.printf("[1-Wire] Liczba wykrytych czujników: %d\n", count);

    if (count == 0) {
        Serial.println("[BŁĄD] Nie wykryto żadnego czujnika DS18B20!");
        Serial.println("Wskazówki diagnostyczne:");
        Serial.println("  1. Upewnij się, że rezystor 4.7kΩ jest podłączony między D5 a 3.3V.");
        Serial.println("  2. Sprawdź czy GND i VCC czujnika nie są zamienione.");
        Serial.println("  3. Sprawdź czy żółty/biały przewód danych trafia do pinu D5 (GPIO14).");
    } else {
        DeviceAddress addr;
        for (int i = 0; i < count; i++) {
            if (sensors.getAddress(addr, i)) {
                Serial.printf("  -> Czujnik #%d ROM: ", i + 1);
                printAddress(addr);
                Serial.println();
                sensors.setResolution(addr, 12); // Rozdzielczość 12-bit (0.0625°C)
            }
        }
    }
}

void loop() {
    sensors.requestTemperatures();
    float tempC = sensors.getTempCByIndex(0);

    digitalWrite(PIN_LED_STATUS, LOW); // Blink LED
    delay(50);
    digitalWrite(PIN_LED_STATUS, HIGH);

    Serial.print("[DS18B20] ");
    if (tempC == DEVICE_DISCONNECTED_C || tempC <= -100.0f) {
        Serial.println("BŁĄD: Czujnik odłączony! (Odczyt -127.0 °C)");
    } else if (tempC >= 84.9f && tempC <= 85.1f) {
        Serial.println("OSTRZEŻENIE: Odczyt 85.0 °C (domyślna wartość po resecie zasilania układu)");
    } else {
        Serial.printf("Temperatura wody: %.2f °C", tempC);
        if (tempC >= 24.0f && tempC <= 27.0f) {
            Serial.println(" [Optymalna]");
        } else if (tempC > 27.0f) {
            Serial.println(" [CIEPŁA / UWAGA]");
        } else {
            Serial.println(" [CHŁODNA / UWAGA]");
        }
    }

    delay(1500);
}
