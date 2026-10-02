---
aliases:
  - WeMos D1 Uno
  - ESP8266 Hardware
tags:
  - hardware
  - esp8266
category: hardware
status: complete
---

# 🔌 Hardware — WeMos D1 Uno (ESP8266 R3 Form-Factor)

Płytka **WeMos D1 Uno** to mikrokontroler oparty na układzie **ESP8266EX** w obudowie o układzie wyprowadzeń zgodnym z formatem Arduino Uno R3. Stanowi serce stacji wykonawczej [[MOC - Obudowa AquaBox i CAD|AquaBox]].

Powrót do: [[MOC - Sprzęt i Peryferia]] | Firmware: [[Firmware - WeMos Controller Pętla Główna]]

---

## 📌 Przypisanie Pinów (Pinout)

Wszystkie stałe pinów zdefiniowane są w pliku `firmware/wemos_controller/include/wemos_config.h`:

| Oznaczenie na płytce | Numer GPIO | Funkcja w Projekcie | Uwagi Elektryczne |
| :--- | :--- | :--- | :--- |
| **D1** | `GPIO5` | **I2C SCL** | Zegar magistrali I2C (MCP23017, ADS1115, DS3231). 100 kHz. |
| **D2** | `GPIO4` | **I2C SDA** | Linia danych I2C. Wymagane rezystory pull-up 4.7kΩ do 3.3V. |
| **D4** | `GPIO2` | **Status LED** | Wbudowana niebieska dioda LED (Active-Low). Mruga co 1 s przy telemetrii. |
| **D5** | `GPIO14` | **1-Wire Data** | Linia danych dla czujnika DS18B20. Rezystor podciągający 4.7kΩ do 3.3V. |
| **TX** | `GPIO1` | **UART0 TX** | Wyjście telemetrii do CYD. Połączone przez [[Hardware - Zworki UART i Izolacja|zworkę]]. |
| **RX** | `GPIO3` | **UART0 RX** | Wejście komend z CYD. Połączone przez [[Hardware - Zworki UART i Izolacja|zworkę]]. |
| **5V** | — | **Zasilanie** | Zasilanie 5V z przetwornicy step-down. |
| **GND** | — | **Masa** | Wspólna masa systemu (zasilacz 12V, przetwornica, CYD, sensory). |

---

## ⚙️ Kluczowe Cechy Konfiguracji Firmware
1. **Wyłączone Wi-Fi**:
   ```cpp
   WiFi.mode(WIFI_OFF);
   WiFi.forceSleepBegin();
   ```
   Dzięki uśpieniu modułu radiowego ESP8266 pobiera zaledwie ok. 15-20 mA prądu, nie generuje zakłóceń dla czułych pomiarów ADC/pH i w 100% dedykuje czas procesora na pętlę sterowania.
2. **Pamięć Flash**: 4 MB (układ partycji: `eagle.flash.4m2m.ld`).
3. **Pamięć Trwała**: Emulacja EEPROM w pamięci Flash pod adresem zerowym ([[Firmware - WeMos Pamięć EEPROM i Storage]]).

---

## 🔗 Powiązane Notatki
- [[Hardware - Ekspander MCP23017]]
- [[Hardware - Zworki UART i Izolacja]]
- [[Firmware - WeMos Controller Pętla Główna]]
- [[Procedura - Programowanie i Wgrywanie Firmware]]
