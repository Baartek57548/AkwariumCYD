---
aliases:
  - Przegląd AquaBox
  - Repo Architecture
tags:
  - repo
  - architecture
category: repo
status: active
---

# Repozytorium AquaBox — aktualny układ

Repozytorium główne utrzymuje wyłącznie firmware dwóch urządzeń: WeMos D1
oraz ESP32-S3. Starsze materiały zachowano w archiwum, a vault Obsidiana
znajduje się w katalogu `obsidian/`.

Powrót: [[00 - AquaCYD Knowledge Hub]] · Architektura:
[[MOC - Architektura Systemu]]

## Aktywne katalogi

| Katalog | Zawartość |
|---|---|
| `firmware/wemos_controller/` | Firmware wykonawczy WeMos D1 / ESP8266. |
| `firmware/wemos_tests/` | Oddzielne testy diagnostyczne WeMos. |
| `firmware/esp32s3_ui/` | Panel LVGL dla ESP32-S3 i Waveshare 16249. |
| `firmware/esp32s3_bringup/`, `firmware/esp32s3_lcd_test/` | Jawnie wybierane profile testowe płytki i LCD. |
| `firmware/shared/uart_protocol/` | Wspólny format wiadomości; fizyczne łącze UART nie jest podłączone. |
| `docs/` | Architektura, projekt UI, weryfikacja i rendery. |
| `obsidian/` | Baza wiedzy projektu. |
| `3d-print/AquaBox-Case/` | STL podstawy i pokrywy obudowy; zgodność wymaga sprawdzenia. |
| `archive/retired-repo-state/` | Poprzedni projekt CYD i pozostałe materiały historyczne. |

Konfiguracja środowisk PlatformIO jest w głównym `platformio.ini`.
Szczegółowe polecenia i ograniczenia sprzętowe: [README repozytorium](../README.md)
oraz [[MOC - Architektura Systemu]].
