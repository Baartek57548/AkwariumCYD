---
aliases:
  - Katalog Komend
  - UART Commands
tags:
  - protocol
  - uart
  - commands
category: protocol
status: complete
---

# 🕹️ Protokół — Katalog Komend Sterujących (CYD -> WeMos)

Komendy przesyłane są w formacie JSON z ESP32 CYD do WeMos D1 Uno w odpowiedzi na akcje użytkownika w interfejsie graficznym lub zapytania REST API.

Powrót do: [[MOC - Protokół Komunikacji NDJSON]] | Potwierdzenia: [[Protokół - Potwierdzenia ACK i Błędy]]

---

## 📋 Lista Dostępnych Komend

### 1. Ręczne Przełączenie Przekaźnika (`set_relay`)
Wymusza stan przekaźnika (*manual override*):
```json
{"type":"cmd","cmd":"set_relay","ch":4,"val":1}
```
- `ch`: Numer kanału przekaźnika (0..7).
- `val`: `1` = włącz, `0` = wyłącz.

### 2. Zmiana Parametrów Termostatu (`set_temp`)
Zmienia zadaną temperaturę i histerezę oraz zapisuje do EEPROM:
```json
{"type":"cmd","cmd":"set_temp","target":25.5,"hyst":0.3}
```

### 3. Zmiana Harmonogramu Urządzenia (`set_sched`)
Konfiguruje godziny włączenia i wyłączenia urządzenia:
```json
{"type":"cmd","cmd":"set_sched","dev":0,"on_h":8,"on_m":0,"off_h":20,"off_m":0,"en":1}
```
- `dev`: `0`=Światło, `1`=Filtr, `2`=Napowietrzacz, `3`=CO2.
- `en`: `1` = aktywny, `0` = wyłączony.

### 4. Wyzwolenie Procedury Karmienia (`feed`)
Uruchamia impuls silnika karmnika (1.5s) i wyłącza filtr na 5 minut:
```json
{"type":"cmd","cmd":"feed"}
```

### 5. Synchronizacja Lamp Aquael (`sync_lights`)
Wykonuje 10-sekundowy cykl odcięcia zasilania lamp w celu zresetowania do trybu Day:
```json
{"type":"cmd","cmd":"sync_lights"}
```

### 6. Synchronizacja Zegara RTC z NTP (`sync_time`)
Przesyła aktualny czas pobrany przez Wi-Fi do zegara DS3231:
```json
{"type":"cmd","cmd":"sync_time","epoch":1773852500}
```

### 7. Kasowanie Alarmu (`clear_alarm`)
Potwierdzenie i skasowanie flag alarmowych przez użytkownika:
```json
{"type":"cmd","cmd":"clear_alarm"}
```

### 8. Wymuszenie Trybu Awaryjnego (`safe_mode`)
```json
{"type":"cmd","cmd":"safe_mode","val":1}
```

---

## 🔗 Powiązane Notatki
- [[Protokół - Potwierdzenia ACK i Błędy]]
- [[Protokół - Struktura Telemetrii]]
- [[Firmware - WeMos Controller Pętla Główna]]
