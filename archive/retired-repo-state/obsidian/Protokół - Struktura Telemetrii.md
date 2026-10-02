---
aliases:
  - Ramka Telemetrii
  - Telemetry Frame
tags:
  - protocol
  - uart
  - telemetry
category: protocol
status: complete
---

# 📊 Protokół — Struktura i Pola Ramki Telemetrii

Ramka telemetrii przesyłana jest z WeMos D1 Uno do ESP32 CYD co **1000 ms** oraz natychmiast po każdej zmianie stanu przekaźnika lub nastawy.

Powrót do: [[MOC - Protokół Komunikacji NDJSON]] | Protokół: [[Protokół - Specyfikacja NDJSON UART]]

---

## 📋 Przykładowa Ramka NDJSON
```json
{"type":"telemetry","ts":1773852100,"temp":25.35,"temp_tgt":25.00,"temp_hyst":0.30,"ph":6.82,"ec":420.0,"relays":20,"alarms":0,"w_low":false,"w_max":false,"w_max_valid":true,"uptime":3600,"safe":false,"lmode":1}
```

---

## 🔍 Opis Wszystkich Pól

| Pole JSON | Typ danych | Jednostka / Znaczenie | Opis |
| :--- | :---: | :---: | :--- |
| `"type"` | String | — | Zawsze `"telemetry"`. |
| `"ts"` | uint32 | Sekundy | Czas Unix Epoch odczytany z zegara DS3231 RTC. |
| `"temp"` | float | °C | Aktualna temperatura wody zmierzona przez DS18B20. |
| `"temp_tgt"` | float | °C | Temperatura zadana termostatu (np. 25.0°C). |
| `"temp_hyst"`| float | °C | Histereza termostatu (np. 0.3°C). |
| `"ph"` | float | pH | Aktualny odczyn wody zmierzony przez ADS1115 (0.00 – 14.00). |
| `"ec"` | float | µS/cm | Przewodność wody z sondy EC. |
| `"relays"` | uint8 | Bitmaska | Bity 0..7 stanów załączenia przekaźników (1 = załączony). |
| `"alarms"` | uint16 | Bitmaska | Flagi aktywnych alarmów (zobacz tabelę poniżej). |
| `"w_low"` | bool | — | `true` jeśli pływak wykrył niski poziom wody. |
| `"w_max"` | bool | — | `true` jeśli górny pływak wykrył poziom MAX. |
| `"w_max_valid"` | bool | — | `true` gdy odczyt wejść MCP23017 jest wiarygodny. Brak pola w starszej ramce oznacza stan nieznany. |
| `"uptime"` | uint32 | Sekundy | Czas ciągłej pracy WeMos od ostatniego restartu. |
| `"safe"` | bool | — | `true` jeśli sterownik znajduje się w trybie awaryjnym. |
| `"lmode"` | uint8 | 1..3 | Aktualny tryb lampy Aquael Day & Night. |

Osiągnięcie MAX nie ustawia bitu alarmu. Stare ramki bez `w_max_valid` należy traktować jako brak danych o górnym pływaku.

---

## 🚨 Maska Bitowa Pola `alarms`

- `0x0001` (`1U << 0`): `ALARM_OVERHEAT` — Przegrzanie wody (>28.5°C)
- `0x0002` (`1U << 1`): `ALARM_UNDERHEAT` — Przechłodzenie wody (<21.0°C)
- `0x0004` (`1U << 2`): `ALARM_RESERVED_INPUT` — bit zachowany jako rezerwa zgodności protokołu
- `0x0008` (`1U << 3`): `ALARM_WATER_LOW` — Niski poziom wody w akwarium
- `0x0010` (`1U << 4`): `ALARM_TEMP_SENSOR_FAIL` — Błąd odczytu DS18B20
- `0x0020` (`1U << 5`): `ALARM_HEATER_TIMEOUT` — Grzałka pracuje bez przerwy > 30 min
- `0x0080` (`1U << 7`): `ALARM_I2C_FAIL` — Błąd komunikacji z ekspanderem MCP23017

---

## 🔗 Powiązane Notatki
- [[Protokół - Specyfikacja NDJSON UART]]
- [[Protokół - Katalog Komend i Sterowanie]]
- [[Hardware - Czujnik Poziomu Wody]]
