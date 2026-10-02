---
aliases:
  - WeMos automation_engine
  - Harmonogramy Akwarium
tags:
  - firmware
  - automation
  - esp8266
category: firmware
status: complete
---

# ⏰ Firmware — Silnik Automatyzacji i Harmonogramy (`automation_engine.cpp`)

Pliki: `firmware/wemos_controller/src/automation_engine.h` oraz `.cpp`.
Klasa `AutomationEngine` odpowiada za autonomiczne załączanie urządzeń w cyklu dobowym, procedury karmienia oraz synchronizację oświetlenia Aquael.

Powrót do: [[MOC - Firmware ESP8266 WeMos]] | Sprzęt: [[Hardware - Moduł 8 Przekaźników]]

---

## 📅 Domyślne Harmonogramy Dobowe

Sterownik obsługuje 4 niezależne urządzenia w pamięci EEPROM:
1. **Światło (Kanały 0 i 1)**: `08:00` – `20:00` (12 godzin świecenia).
2. **Filtr (Kanał 2)**: `00:00` – `23:59` (aktywny 24h na dobę).
3. **Napowietrzacz (Kanał 3)**: `22:00` – `06:00` (aktywne w nocy — obsługa przejścia przez północ!).
4. **Elektrozawór CO2 (Kanał 5)**: `07:30` – `19:30` (załączany 30 min przed światłem).

---

## 🌙 Algorytm Przejścia przez Północ (`isTimeInRange`)
Zwykłe porównanie `cur >= on && cur < off` nie zadziała, gdy urządzenie włącza się o 22:00, a wyłącza o 06:00. Kod rozwiązuje to elegancką logiką:
```cpp
if (on_mins < off_mins) {
    return (cur_mins >= on_mins && cur_mins < off_mins);
} else {
    // Przejście przez północ (np. 22:00 do 06:00)
    return (cur_mins >= on_mins || cur_mins < off_mins);
}
```

---

## 🐟 Procedura Karmienia (`triggerFeeder`)
Wywołana komendą z panelu dotykowego CYD lub harmonogramem:
1. Załącza silnik karmnika na **1.5 sekundy** (dozowanie pokarmu).
2. **Wyłącza filtr główny na 5 minut** (300 000 ms), aby pokarm nie został wciągnięty do mediów filtracyjnych.
3. Po 5 minutach automatycznie wznawia pracę filtra.

---

## 🔗 Powiązane Notatki
- [[Hardware - Moduł 8 Przekaźników]]
- [[Hardware - Zegar DS3231 RTC]]
- [[Firmware - WeMos Pamięć EEPROM i Storage]]
