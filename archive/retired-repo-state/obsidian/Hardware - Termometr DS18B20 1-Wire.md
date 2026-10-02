---
aliases:
  - DS18B20
  - Termometr 1-Wire
tags:
  - hardware
  - sensors
  - 1wire
category: hardware
status: complete
---

# 🌡️ Hardware — Termometr Cyfrowy Dallas DS18B20 (1-Wire)

Wodoodporna sonda ze stali nierdzewnej z układem **Dallas DS18B20** odpowiada za stały pomiar temperatury wody w zbiorniku.

Powrót do: [[MOC - Sprzęt i Peryferia]] | Algorytm: [[Firmware - WeMos Termostat i TempControl]]

---

## 📌 Połączenie Sprzętowe

- **Pin danych**: Pin WeMos **`D5`** (`GPIO14`).
- **Zasilanie**: 3.3V DC oraz GND.
- **Rezystor Pull-up**: Wymagany zewnętrzny rezystor **4.7 kΩ** wpięty pomiędzy linię danych (`D5`) a szynę zasilania `3.3V`. Bez tego rezystora magistrala 1-Wire nie będzie w stanie transmitować bitów!

---

## ⚙️ Charakterystyka Odczytu w Kodzie
- **Rozdzielczość**: Domyślnie 12-bitowa (dokładność odczytu 0.0625°C).
- **Konwersja asynchroniczna**:
  ```cpp
  dallas_sensors.setWaitForConversion(false);
  dallas_sensors.requestTemperatures();
  ```
  Dzięki wyłączeniu blokującego oczekiwania (`waitForConversion = false`), pętla główna `loop()` mikrokontrolera wykonuje się natychmiast bez zamrażania obsługi UART i przekaźników na 750 ms!
- **Filtr błędów**: Wartości `-127.0°C` (brak zasilania/oderwany przewód) oraz `+85.0°C` (wartość resetu układu) są automatycznie odrzucane jako niepoprawne.

---

## 🔗 Powiązane Notatki
- [[Firmware - WeMos Termostat i TempControl]]
- [[Bezpieczeństwo - Zabezpieczenia Grzałki]]
- [[Hardware - WeMos D1 Uno ESP8266]]
