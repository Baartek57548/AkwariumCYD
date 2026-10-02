# DISPATCH — explorer_survey_2

## Role
Investigate ESP32 CYD firmware, PlatformIO environment `cyd_2_8`, LVGL UI screens/widgets, and OTA subsystem requirements.

## Task
1. Read `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\ORIGINAL_REQUEST.md` (specifically header `## Follow-up — 2026-09-18T17:21:02Z`).
2. Search and analyze the codebase for ESP32 CYD (`platformio.ini` env `cyd_2_8`, `firmware/cyd_controller` or main sources, LVGL UI code, web server).
3. Investigate:
   - Existing OTA mechanisms (Web OTA, ArduinoOTA, `Update.h`, WebServer handlers).
   - How Wi-Fi STA and AP fallback (`AquaCYD-OTA`) are configured and initialized.
   - Current LVGL UI architecture: screen hierarchy, service/settings tiles or screens.
   - Where and how to add OTA controls:
     - Button "Aktualizuj Panel (CYD)" — switches CYD into dedicated OTA mode or triggers restart with OTA flag / boots into OTA service.
     - Button "Aktualizuj AquaBox (WeMos)" — sends UART command to WeMos and renders visual instruction with SSID and IP to connect to in browser.
   - PlatformIO build setup for `cyd_2_8` (`pio run -e cyd_2_8`).
4. Write detailed findings, UI structure recommendations, code touchpoints, and build commands to `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_survey_2\report.md`.
5. Send completion message when done.

## 2026-09-18T17:23:19Z
Investigate the ESP32 CYD codebase:
- platformio.ini environment cyd_2_8 (location of files, frameworks, build flags, libs).
- Existing OTA mechanisms (Web OTA, ArduinoOTA, Update library).
- STA connection and AP fallback (AquaCYD-OTA).
- LVGL UI architecture: screens, tiles, service/settings tabs.
- Adding OTA UI controls:
  - "Aktualizuj Panel (CYD)" button (triggers CYD OTA mode / restart with flag).
  - "Aktualizuj AquaBox (WeMos)" button (sends UART command to WeMos and shows Wi-Fi SSID / IP instructions).
- Build command and verify current build status (pio run -e cyd_2_8).

Write your detailed findings and implementation roadmap to:
c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_survey_2\report.md

