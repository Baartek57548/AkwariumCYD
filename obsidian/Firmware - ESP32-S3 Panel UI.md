---
aliases:
  - Firmware AquaBox HMI
tags:
  - firmware
  - esp32s3
  - lvgl
category: firmware
status: active
---

# Firmware panelu ESP32-S3

Kod panelu znajduje się w `firmware/esp32s3_ui/`. Cel sprzętowy to
ESP32-S3-WROOM-1-N16R8 i Waveshare 16249, 800 × 480, RGB565, z dotykiem GT911.
Repozytorium buduje UI w LVGL 8.4.0.

Panel zapewnia karty Panel, Plan dnia, Historia, Serwis i System. Motyw jasny
i ciemny ma niezależny wybór spośród sześciu palet. Testy hostowe sprawdzają
kontrast tekstu i rozdział powierzchni; nie mierzą jasności ani kalibracji
fizycznego wyświetlacza.

## Budowanie i testy

Z katalogu głównego repozytorium:

```powershell
python -m platformio run -e esp32s3_waveshare_ui
./tools/verify_s3_ui.ps1
```

Profil `esp32s3_n16r8_bringup` służy do diagnostyki pamięci, a
`esp32s3_waveshare_lcd_test` do odrębnego sprawdzenia obrazu i dotyku. Żaden
z nich nie jest domyślnym firmware panelu.

## Stan integracji

Łącze UART do WeMos pozostaje niepodłączone. UI używa lokalnego modelu, więc
prezentowane odczyty i sterowanie nie są potwierdzeniem fizycznego stanu
akwarium. Ręczne sterowanie silnikiem karmnika jest wyłączone do czasu
potwierdzenia połączeń sterownika ULN2003. Kod HAL ekranu:
`firmware/esp32s3_ui/src/panel_hal.cpp`.

Szczegóły sprzętowe: [[Hardware - ESP32-S3 Waveshare 16249]].
Architektura: [[MOC - Architektura Systemu]].
