---
aliases:
  - Hub Wiedzy
  - Indeks Główny
  - AquaCYD MOC
tags:
  - hub
  - akwarium
  - index
category: hub
status: complete
---

# 🐟 AquaCYD & AquaBox — Baza Wiedzy i Mapa Projektu

Witaj w centralnej bazie wiedzy inteligentnego sterownika akwariowego **AquaCYD** oraz stacji wykonawczej **AquaBox**. System oparty jest na architekturze rozproszonej: **ESP32 CYD (Frontend/Web)** + **WeMos D1 Uno ESP8266 (Autonomiczny Sterownik Sprzętowy)**, połączonych niezawodną magistralą **UART NDJSON**.

---

## 🗺️ Główne Mapy Zawartości (Maps of Content — MOC)

Wybierz obszar, który chcesz zgłębić lub kliknij węzeł w widoku grafu:

- 🏛️ [[MOC - Architektura Systemu|Architektura Systemu i Podział Ról]] — Podział odpowiedzialności, przepływ danych, schemat blokowy.
- 🔌 [[MOC - Sprzęt i Peryferia|Sprzęt, Układy Scalone i Pinout]] — Wszystkie moduły elektroniczne, I2C, 1-Wire, sensory i przekaźniki.
- ⚙️ [[MOC - Firmware ESP8266 WeMos|Firmware WeMos D1 Uno (ESP8266)]] — Kod sterownika wykonawczego 24/7 w AquaBox.
- 🖥️ [[MOC - Firmware ESP32 CYD|Firmware ESP32 CYD (Frontend & Web)]] — GUI LVGL 8.4, serwer WWW, REST API i synchronizacja NTP.
- 📡 [[MOC - Protokół Komunikacji NDJSON|Protokół Komunikacji UART NDJSON]] — Specyfikacja ramek telemetrii, komend i potwierdzeń ACK.
- 📦 [[MOC - Obudowa AquaBox i CAD|Obudowa AquaBox i Modele 3D SolidWorks]] — Wanna dolna, mostek przekaźników, pokrywa, druk 3D.
- 🛡️ [[MOC - Bezpieczeństwo i Fail-Safe|Bezpieczeństwo i Procedury Awaryjne]] — Fail-safe grzałki, kontrola poziomu wody i zegar RTC.
- 🛠️ [[MOC - Procedury i Diagnostyka|Procedury Warsztatowe i Testy]] — Wgrywanie oprogramowania, obsługa zworek UART, kalibracja pH.
- 📂 [[Repo - Przegląd Projektów i Wersji|Struktura Repozytorium i Historia]] — Relacje między projektami w PlatformIO i katalogami bazowymi.

---

## 🧭 Szybki Przewodnik po Tagach (Kolory w Grafie Obsidian)

W widoku **Graph View** (skrót `Ctrl + G` lub ikona grafu) węzły są pogrupowane kolorystycznie:
- `#hardware` — Płytki rozwojowe, czujniki, układy scalone (MCP23017, DS3231, ADS1115).
- `#firmware` / `#esp8266` — Oprogramowanie sterownika wykonawczego w AquaBox.
- `#cyd` / `#esp32` — Oprogramowanie panelu dotykowego i bramki sieciowej.
- `#protocol` / `#uart` — Komunikacja szeregowa NDJSON.
- `#cad` / `#3dprint` — Pliki SolidWorks, elementy obudowy, parametry druku 3D.
- `#safety` — Blokady sprzętowe, alarmy poziomu wody i ochrona przed przegrzaniem.
- `#procedure` — Instrukcje krok po kroku (flashowanie, kalibracja, diagnostyka).

---

## ⚡ Skróty do Kluczowych Zadań
- Chcesz wgrać nowy firmware? ➡️ Zobacz [[Procedura - Programowanie i Wgrywanie Firmware]] oraz [[Hardware - Zworki UART i Izolacja]].
- Chcesz przetestować sprzęt bez akwarium? ➡️ Zobacz [[Procedura - Diagnostyka i Self-Test WeMos]].
- Chcesz skalibrować sondę pH? ➡️ Zobacz [[Procedura - Kalibracja Sondy pH]].
- Chcesz zmodyfikować obudowę? ➡️ Zobacz [[AquaBox - Założenia Konstrukcyjne CAD]].
