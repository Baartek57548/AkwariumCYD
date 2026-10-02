---
aliases:
  - DS3231 RTC
  - Zegar Czasu Rzeczywistego
tags:
  - hardware
  - i2c
  - rtc
category: hardware
status: complete
---

# 🕒 Hardware — Zegar Czasu Rzeczywistego DS3231 (RTC)

Moduł **DS3231** to ultraprecyzyjny zegar czasu rzeczywistego z wbudowanym rezonatorem kwarcowym z kompensacją temperatury (TCXO) oraz podtrzymaniem bateryjnym CR2032.

Powrót do: [[MOC - Sprzęt i Peryferia]] | Kod: [[Firmware - WeMos Hardware I2C]]

---

## 📌 Dlaczego Sprzętowy RTC w AquaBox?

Sterownik WeMos D1 Uno ma celowo **wyłączone Wi-Fi**, co oznacza brak możliwości samodzielnego pobierania czasu przez NTP z Internetu.
Dzięki modułowi DS3231:
1. **Niezależność**: Sterownik dokładnie zna czas (godzinę, minutę, sekundę), co pozwala na precyzyjną realizację harmonogramów dobowych oświetlenia, CO2 i napowietrzania.
2. **Odporność na brak zasilania**: W razie zaniku prądu w domu, bateria CR2032 podtrzymuje bieg zegara. Po powrocie zasilania sterownik od razu wie, czy ma włączyć światło, czy zachować ciszę nocną.

---

## ⚙️ Parametry i Magistrala I2C
- **Adres I2C**: Stały adres fabryczny **`0x68`**.
- **Format Rejestrów**: Kodowanie BCD (Binary Coded Decimal) sekund, minut, godzin, dnia, miesiąca i roku.
- **Konwersja w kodzie**: Metoda `getEpochTime()` w `hardware_i2c.cpp` przelicza rejestry BCD bezpośrednio na 32-bitowy czas Unix Epoch.
- **Synchronizacja**: Gdy CYD połączy się z domowym Wi-Fi i pobierze czas NTP, wysyła do WeMos ramkę komendy `sync_time` z aktualnym czasem epoch. WeMos aktualizuje wówczas rejestry DS3231 metodą `setEpochTime()`.

---

## 🔗 Powiązane Notatki
- [[Firmware - WeMos Hardware I2C]]
- [[Firmware - WeMos AutomationEngine i Harmonogramy]]
- [[Protokół - Katalog Komend i Sterowanie]]
