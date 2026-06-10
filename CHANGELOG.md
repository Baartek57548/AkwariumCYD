# Changelog

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
