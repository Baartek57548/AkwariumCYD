# Changelog

## mobile-v3.7.0 - 2026-07-25

Produkcyjne wydanie aplikacji AquaCYD Control `3.7.0+13`.

### What is included

- Material 3 UI przystosowane do ekranów 320 px i systemowej skali tekstu 300%.
- Pasek kondycji sterownika: Online/Offline/Connecting, RSSI, ping i wiek danych.
- Automatyczny reconnect z backoffem oraz zatrzymywanie odpytywania w tle.
- Serializacja komend REST/BLE, odporne parsowanie statusu i limity odpowiedzi.
- Bezpieczny cykl życia BLE, batchowanie skanowania i obsługa uszkodzonych ramek.
- Przewijalny, dostępny interfejs aktualizacji APK po zgodzie użytkownika.

### Verification

- `flutter analyze` zakończone bez problemów.
- 86 testów Flutter zakończonych powodzeniem.
- Podpisany wariant `current` zweryfikowany jako `3.7.0` (`versionCode 13`).

## v20260626release - 2026-06-26

Release panelu web, OTA i firmware po reorganizacji sekcji oraz stabilizacji
lokalnego srodowiska PlatformIO.

### What is included

- Reorganized web panel sections for relays, module automation and system settings.
- Updated OTA SD package with cache-busted assets `20260626release`.
- Factory schedule profile and Aquael DAY / DAYBREAK / NIGHT UI support.
- Improved web status, settings layout and OTA upload flow.
- PlatformIO pinned to the official `platformio/espressif32@7.0.1` platform.
- VS Code terminal configured for UTF-8 Python output to avoid Windows `cp1250` crashes.

### Verification

- Firmware build passed with `pio run --environment esp32dev`.
- JavaScript syntax checks passed for active web and OTA scripts.
- VS Code IntelliSense include paths regenerated with no missing paths.

## v1.0.0 - 2026-06-10

Stable UI/UX release for the CYD Aquarium firmware.

### What is included

- Responsive LVGL interface with Polish language assets.
- Event-driven separation between UI and control logic.
- Modular hardware, sensor, controller, and display layers.
- Lighting synchronization for Aquael lamps.
- Heater regulation with hysteresis and fail-safe handling.
- OTA and diagnostics panels.
- Circular history buffers for charts and telemetry.
- PIN protection for critical actions.
- Service mode with automatic timeout.
- Graceful degradation when optional hardware is absent.

### Verification

- Firmware build passed with `py -3.13 -m platformio run -e esp32dev`.

### Notes

- This release is marked as stable based on the current repository state and the successful firmware build.
- No changes were made to the existing UI flow beyond the warning cleanup in `events.cpp`.
