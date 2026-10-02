---
aliases:
  - Firmware ESP8266
  - WeMos Firmware MOC
tags:
  - moc
  - firmware
  - esp8266
category: moc
status: complete
---

# ⚙️ MOC — Firmware ESP8266 WeMos Controller

Oprogramowanie sterownika WeMos D1 Uno (ESP8266) znajduje się w katalogu:
`firmware/wemos_controller/` w repozytorium `cydAquarium`.

Powrót do: [[00 - AquaCYD Knowledge Hub]] | Sprzęt: [[Hardware - WeMos D1 Uno ESP8266]]

---

## 📦 Struktura Modułowa Kodu

```
firmware/wemos_controller/
├── include/
│   └── wemos_config.h                  # [[Hardware - WeMos D1 Uno ESP8266|Definicje pinów, adresów I2C i limitów bezpieczeństwa]]
└── src/
    ├── main.cpp                        # [[Firmware - WeMos Controller Pętla Główna|Pętla główna loop, obsługa UART i telemetria]]
    ├── hardware_i2c.h / .cpp           # [[Firmware - WeMos Hardware I2C|Sterowniki I2C: MCP23017, ADS1115, DS3231]]
    ├── temp_control.h / .cpp           # [[Firmware - WeMos Termostat i TempControl|Obsługa DS18B20, histereza, termostat i alarmy]]
    ├── automation_engine.h / .cpp      # [[Firmware - WeMos AutomationEngine i Harmonogramy|Silnik 4 harmonogramów, karmnik, lampy Aquael]]
    └── storage_manager.h / .cpp        # [[Firmware - WeMos Pamięć EEPROM i Storage|Pamięć Flash EEPROM z sumą CRC32 i nagłówkiem AQUA]]
```

---

## 📊 Parametry Kompilacji PlatformIO

Firmware kompilowany jest środowiskiem `[env:wemos_d1_uno]` w pliku `platformio.ini`:
- **Płytka**: `d1_mini` (kompatybilna binarnie z WeMos D1 R2/Uno ESP8266)
- **Taktowanie**: 80 MHz
- **Zużycie RAM**: **36.5%** (29.9 KB z 81.9 KB) — bardzo stabilny zapas
- **Zużycie Flash**: **27.9%** (291.8 KB z 1.0 MB)
- **Status kompilacji**: **SUCCESS**

Polecenie kompilacji i wgrania:
```powershell
pio run -e wemos_d1_uno -t upload
```

---

## 🧪 Zestaw Programów Testowych (`firmware/wemos_tests/`)
Przed uruchomieniem właściwego sterownika dostępne są dedykowane szkice diagnostyczne:
- `test_wemos_diag` — [[Procedura - Diagnostyka i Self-Test WeMos|Kompleksowy interaktywny self-test z menu szeregowym]]
- `test_wemos_i2c` — Skaner adresów magistrali I2C
- `test_wemos_relays` — Cykliczne załączanie 8 przekaźników
- `test_wemos_temp` — Ciągły odczyt temperatury DS18B20
- `test_wemos_rtc` — Odczyt i ustawianie czasu w DS3231 RTC
- `test_wemos_ads1115` — Odczyt surowych napięć z ADS1115 i sondy pH

---

## 🔗 Powiązane Notatki
- [[Firmware - WeMos Controller Pętla Główna]]
- [[Firmware - WeMos Hardware I2C]]
- [[Firmware - WeMos Termostat i TempControl]]
- [[Firmware - WeMos AutomationEngine i Harmonogramy]]
- [[Firmware - WeMos Pamięć EEPROM i Storage]]
- [[Protokół - Specyfikacja NDJSON UART]]
