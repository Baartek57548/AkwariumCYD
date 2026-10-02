---
aliases:
  - Przegląd Projektów
  - Repo Architecture
tags:
  - repo
  - architecture
category: repo
status: complete
---

# 📂 Repozytorium — Przegląd Projektów, Katalogów i Wersji

Przewodnik po strukturze katalogów w środowisku roboczym PlatformIO.

Powrót do: [[00 - AquaCYD Knowledge Hub]] | Architektura: [[MOC - Architektura Systemu]]

---

## 🗺️ Mapa Katalogów Projektowych

W folderze roboczym `c:\Users\Bartek\Documents\PlatformIO\Projects\` znajdują się:

### 1. `cydAquarium/` (AKTYWNY GŁÓWNY PROJEKT)
- Główny projekt zintegrowany (Monorepo), nad którym obecnie pracujemy.
- Zawiera:
  - `firmware/wemos_controller/` — Firmware ESP8266 WeMos dla AquaBox.
  - `firmware/wemos_tests/` — Programy testowe i diagnostyczne WeMos.
  - `firmware/cyd_controller/` — Firmware ESP32 CYD (LVGL + Web).
  - `firmware/shared/uart_protocol/` — Współdzielona biblioteka protokołu NDJSON.
  - `AquaBox/` — Kompletne modele CAD SolidWorks (SLDPRT, SLDASM, STEP, STL).
  - `obsidian/` — Niniejsza baza wiedzy dla programu Obsidian.

### 2. `cydAquarium BAZOWA/`
- Pierwotna wersja bazowa projektu z czasów eksperymentów z jednym mikrokontrolerem ESP32.
- Służy wyłącznie jako punkt odniesienia historycznego.

### 3. `cydAquarium backup/`
- Kopia zapasowa stabilnej wersji sprzed rozbudowy. W folderze `polecenia/` znajdują się notatki założeń wstępnych.

### 4. `260225-101701-seeed_xiao_esp32s3/`
- Wczesna wersja prototypu oparta na miniaturowej płytce ESP32-S3 Zero oraz wyświetlaczu OLED 128x32 px.
- Projekt archiwalny — nie należy go modyfikować.

---

## 🔗 Powiązane Notatki
- [[00 - AquaCYD Knowledge Hub]]
- [[MOC - Architektura Systemu]]
