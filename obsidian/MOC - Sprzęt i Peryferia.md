---
aliases:
  - Sprzęt i Peryferia
  - Hardware MOC
tags:
  - moc
  - hardware
category: moc
status: complete
---

# 🔌 MOC — aktywny sprzęt AquaBox

Aktywna konfiguracja to WeMos D1 z ESP8266 oraz panel ESP32-S3 z ekranem
Waveshare 16249. Szczegółowe notatki o CYD opisują poprzedni sprzęt i są
zachowane wyłącznie jako historia.

Powrót do: [[00 - AquaCYD Knowledge Hub]] | Zobacz też: [[MOC - Architektura Systemu]]

---

## 📋 Zestawienie Komponentów Sprzętowych

| Komponent | Model / Oznaczenie | Magistrala / Piny | Rola w Systemie | Notatka Szczegółowa |
| :--- | :--- | :--- | :--- | :--- |
| **Główny Sterownik** | WeMos D1 Uno (ESP8266 R3) | D1 (SCL), D2 (SDA), D5 (1-Wire), D4 (LED) | Autonomiczny mózg wykonawczy | [[Hardware - WeMos D1 Uno ESP8266]] |
| **Panel HMI** | ESP32-S3-WROOM-1-N16R8 + Waveshare 16249 | RGB565 800×480, GT911 I²C | Interfejs LVGL i ustawienia | [[Hardware - ESP32-S3 Waveshare 16249]] |
| **Ekspander Portów** | MCP23017 SO28/DIP28 | I2C: Adres `0x20` | 8 wyjść przekaźników + 4 wejścia cyfrowe | [[Hardware - Ekspander MCP23017]] |
| **Moduł Przekaźników** | 8x Przekaźnik 5V/10A | Active-Low z MCP23017 Port A | Załączanie odbiorników 230V i 12V | [[Hardware - Moduł 8 Przekaźników]] |
| **Zegar Czasu (RTC)** | DS3231 + Bateria CR2032 | I2C: Adres `0x68` | Precyzyjny czas rzeczywisty 24/7 | [[Hardware - Zegar DS3231 RTC]] |
| **Przetwornik ADC** | ADS1115 (16-bit 4ch) | I2C: Adres `0x48` | Pomiary sondy pH (AIN0) oraz EC (AIN1) | [[Hardware - Przetwornik ADS1115 i pH]] |
| **Termometr Cyfrowy** | Dallas DS18B20 | 1-Wire: Pin `D5` (GPIO14) + 4.7kΩ | Pomiar temperatury wody w akwarium | [[Hardware - Termometr DS18B20 1-Wire]] |
| **Czujnik Poziomu Wody** | Pływak cieczy | MCP23017 Port B (GPB2) | Wykrywanie niskiego poziomu | [[Hardware - Czujnik Poziomu Wody]] |
| **Zasilacz Główny** | Sieciowy 12V DC 5A | Złącze śrubowe / gniazdo C14 | Zasilanie oświetlenia, cewek, pompy | [[Hardware - Zasilanie i Przetwornica 5V]] |
| **Przetwornica Step-Down** | DC-DC Step-Down (12V -> 5V) | Szyna zasilania 5V | Logika WeMos, peryferia i przekaźniki | [[Hardware - Zasilanie i Przetwornica 5V]] |
| **Interfejs UART** | WeMos D1 ↔ ESP32-S3 | GPIO WeMos RX/TX; połączenie fizyczne niepotwierdzone | Zaplanowane łącze sterownika z panelem | [[Hardware - Zworki UART i Izolacja]] |

---

## 🗺️ Adresy na Magistrali I2C (WeMos D1: Pin D1=SCL, D2=SDA)

- **`0x20`**: [[Hardware - Ekspander MCP23017]] (Piny A0=GND, A1=GND, A2=GND)
- **`0x48`**: [[Hardware - Przetwornik ADS1115 i pH]] (Pin ADDR zwarty do GND)
- **`0x68`**: [[Hardware - Zegar DS3231 RTC]] (Stały adres fabryczny)

---

## 🔗 Powiązane Notatki
- [[MOC - Firmware ESP8266 WeMos]]
- [[MOC - Obudowa AquaBox i CAD]]
- [[Procedura - Programowanie i Wgrywanie Firmware]]
