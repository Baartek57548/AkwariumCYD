# cydAquarium

Sterownik akwarium dla ESP32 CYD z interfejsem LVGL, panelem WWW, BLE,
obsługą OTA i aplikacją Flutter.

Dokumentacja firmware, pinologia, profile kompilacji i procedura wgrywania:
[docs/CYD_FIRMWARE.md](docs/CYD_FIRMWARE.md).

Gotowe obrazy ILI9341 i ST7789 do pobrania znajdują się w
[`artifacts/`](artifacts/CYD-FIRMWARE-2026.07.26.md).

Najważniejsze katalogi:

- `src/`, `include/`, `lib/` — firmware ESP32;
- `test/` — testy logiki domenowej;
- `web/` — panel WWW;
- `mobile_app/` — aplikacja Flutter;
- `sdcard/` — struktura danych i zasobów karty SD.
