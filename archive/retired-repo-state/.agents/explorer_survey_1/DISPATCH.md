# DISPATCH — explorer_survey_1

## Role
Investigate AquaBox / WeMos D1 Uno ESP8266 firmware, hardware pinout, relay controls, Wi-Fi handling, and OTA implementation requirements.

## Task
1. Read `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\ORIGINAL_REQUEST.md` (specifically header `## Follow-up — 2026-09-18T17:21:02Z`).
2. Search and analyze the codebase for WeMos D1 Uno (`platformio.ini` env `wemos_d1_uno`, source files in `AquaBox` or `src/`, headers, libraries).
3. Investigate:
   - How normal mode operates with `WiFi.mode(WIFI_OFF)`.
   - Hardware pins: relay control pins (heater, CO2, power), spare GPIO pins suitable for hardware OTA fallback button.
   - Fail-safe relay shutdown logic.
   - Dual-mode OTA: connecting to STA (home Wi-Fi credentials from config/secrets or fallback) and fallback AP mode (`AquaBox-OTA`).
   - Web OTA HTTP server (with simple form) and ArduinoOTA integration for ESP8266.
   - 5-minute (300s) activity/inactivity watchdog to safely reboot to normal mode if no upload occurs.
   - PlatformIO build setup for `wemos_d1_uno` (`pio run -e wemos_d1_uno`).
4. Write detailed findings, architecture recommendation, code touchpoints, and build commands to `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_survey_1\report.md`.
5. Send completion message when done.

## 2026-09-18T17:23:19Z
Investigate the WeMos D1 Uno ESP8266 (AquaBox) codebase:
- platformio.ini environment wemos_d1_uno (location of files, frameworks, build flags, libs).
- AquaBox firmware structure, current Wi-Fi behavior (ensuring normal mode has WiFi.mode(WIFI_OFF)).
- Relay pinouts (heater, CO2, power) and safe-shutdown logic.
- Spare GPIO for physical fallback push button at boot.
- STA connection with fallback AP mode (SSID "AquaBox-OTA").
- ESP8266 Web OTA HTTP server (with simple form) and ArduinoOTA setup.
- 5-minute inactivity watchdog timer to reboot back to normal operation.
- Build command and verify current build status (pio run -e wemos_d1_uno).

Write your detailed findings and implementation roadmap to:
c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_survey_1\report.md
