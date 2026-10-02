---
aliases:
  - WeMos storage_manager
  - EEPROM Storage
tags:
  - firmware
  - eeprom
  - storage
category: firmware
status: complete
---

# 💾 Firmware — Pamięć Konfiguracji EEPROM i Integralność CRC32

Pliki: `firmware/wemos_controller/src/storage_manager.h` oraz `.cpp`.
Klasa `StorageManager` zarządza trwałym zapisem parametrów pracy sterownika w pamięci Flash mikrokontrolera ESP8266.

Powrót do: [[MOC - Firmware ESP8266 WeMos]] | Parametry: [[Firmware - WeMos Termostat i TempControl]]

---

## 📋 Struktura Danych `ControllerConfig`

```cpp
struct ControllerConfig {
    uint32_t magic;               // Nagłówek walidacyjny: 0x41515541 ("AQUA")
    uint16_t version;             // Wersja struktury: 1
    float target_temp;            // Temperatura zadana (°C)
    float hysteresis;             // Histereza termostatu (°C)
    float ph_cal_offset;          // Punkt zerowy kalibracji pH
    float ph_cal_slope;           // Współczynnik nachylenia elektrody pH
    DeviceSchedule schedules[4];  // 4 harmonogramy dobowe (wł/wył, godziny, minuty)
    uint32_t checksum;            // Suma kontrolna CRC32
};
```

---

## 🔒 Zabezpieczenie przed Uszkodzeniem Danych (CRC32)

Przed zapisem struktury wyliczana jest suma kontrolna algorytmem **CRC32 (wielomian 0xEDB88320)** ze wszystkich pól z wyjątkiem samego pola `checksum`.

Podczas startu sterownika (`loadConfig`):
1. Odczytywana jest struktura z Flash.
2. Sprawdzany jest nagłówek `magic == 0x41515541` oraz wersja.
3. Obliczana jest suma CRC32 i porównywana z polem `checksum`.
4. Jeśli suma się nie zgadza (np. pierwsze uruchomienie na nowej płytce lub uszkodzenie pamięci), sterownik **nie zawiesza się**, lecz automatycznie ładuje bezpieczne wartości fabryczne (`resetToDefaults`) i zapisuje poprawny profil do Flash.

---

## 🔗 Powiązane Notatki
- [[Firmware - WeMos Controller Pętla Główna]]
- [[Firmware - WeMos Termostat i TempControl]]
- [[Procedura - Kalibracja Sondy pH]]
