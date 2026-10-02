---
aliases:
  - Firmware ESP32 CYD
  - CYD Firmware MOC
tags:
  - moc
  - firmware
  - cyd
  - esp32
category: moc
status: active
---

# 🖥️ MOC — Firmware ESP32 CYD (Frontend & Web Gateway)

Oprogramowanie panelu operatorskiego CYD (ESP32-2432S028R) znajduje się w katalogu:
`firmware/cyd_controller/` w repozytorium `cydAquarium`.

Powrót do: [[00 - AquaCYD Knowledge Hub]] | Sprzęt: [[Hardware - ESP32 CYD 2432S028R]]

---

## 🎯 Rola i Zadania CYD w Systemie

W odróżnieniu od klasycznych sterowników jednoukładowych, ESP32 na płytce CYD nie steruje bezpośrednio przekaźnikami 230V, lecz pełni rolę **stacji operatorskiej**:
1. **Dotykowy Ekran HMI**: Wizualizacja temperatury, pH, stanów urządzeń, zegara i alarmów za pomocą biblioteki **LVGL 8.4** i sterownika graficznego **LovyanGFX**.
2. **Serwer HTTP i Panel Web**: Pełny interfejs WWW responsywny na smartfony i przeglądarki desktopowe, serwowany w sieci domowej.
3. **Klient NTP**: Pobieranie precyzyjnego czasu z internetowych serwerów czasu i synchronizowanie zegara DS3231 na WeMos poleceniem `sync_time`.
4. **Klient UART**: Odbiór strumienia telemetrii co 1 sekundę z WeMos i wysyłanie poleceń operatorskich ([[Firmware - CYD UartClient i Telemetria]]).
5. **Dźwiękowy System Ostrzegania**: Sterowanie wbudowanym buzzerem (pin `GPIO26`) w przypadku przegrzania lub krytycznego błędu urządzenia.

---

## 📊 Parametry Kompilacji PlatformIO

Aktualne firmware dla fizycznego CYD ST7789 kompilowane jest środowiskiem `[env:esp32dev-st7789]` w pliku `platformio.ini`:
- **Płytka**: `esp32dev` (ESP32-WROOM-32 z 4MB Flash)
- **Ostatni zbudowany obraz (2026-09-23)**: RAM **87 864 B** (26,8%), Flash **1 574 249 B** (80,1%). Wartości należy traktować jako migawkę konkretnego obrazu, nie stały budżet.
- **Stan WWW**: sekwencyjne transfery HTML/CSS/JS przeszły, lecz stabilność przy normalnym użyciu przeglądarki nadal jest w trakcie weryfikacji. Nie oznaczaj jej jako ukończonej wyłącznie na podstawie poprawnej kompilacji lub napisu WWW na ekranie.

Polecenia kompilacji i wgrania:
```powershell
pio run -e esp32dev-st7789
pio run -e esp32dev-st7789 -t upload --upload-port COM7
```

Przed wgrywaniem ręcznie wprowadź urządzenie w tryb BOOT. Odbiór wgrania następuje dopiero po `Hash of data verified`; szczegółową procedurę zawiera [[Procedura - Programowanie i Wgrywanie Firmware]].

---

## 🔗 Powiązane Notatki
- [[Firmware - CYD UartClient i Telemetria]]
- [[Firmware - CYD Interfejs LVGL i Serwer WWW]]
- [[Hardware - ESP32 CYD 2432S028R]]
- [[Protokół - Specyfikacja NDJSON UART]]
- [[Procedura - Programowanie i Wgrywanie Firmware]]
- [[2026-09-23 - Weryfikacja HTTP CYD i wdrożenie]]
