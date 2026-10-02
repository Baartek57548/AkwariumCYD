---
aliases:
  - Hub Wiedzy
  - Indeks Główny
  - AquaBox MOC
tags:
  - hub
  - akwarium
  - index
category: hub
status: active
---

# AquaBox — baza wiedzy

Aktualnym projektem jest **AquaBox**, utrzymywany dla sterownika wykonawczego
WeMos D1 (ESP8266) i panelu HMI ESP32-S3 z ekranem Waveshare 16249. Starsze
notatki CYD pozostają w vault jako historia wcześniejszej wersji sprzętu.

## Mapy i bieżące notatki

- [[MOC - Architektura Systemu]] — podział ról i stan integracji.
- [[MOC - Sprzęt i Peryferia]] — moduły, czujniki, piny i ograniczenia.
- [[MOC - Firmware ESP8266 WeMos]] — firmware WeMos D1.
- [[Firmware - ESP32-S3 Panel UI]] — aktualny panel LVGL 800 × 480.
- [[MOC - Protokół Komunikacji NDJSON]] — format wiadomości; UART nie jest jeszcze podłączony.
- [[MOC - Obudowa AquaBox i CAD]] — obudowa, pliki STL i wskazówki druku.
- [[MOC - Bezpieczeństwo i Fail-Safe]] — zabezpieczenia oraz ograniczenia runtime.
- [[MOC - Procedury i Diagnostyka]] — testy i czynności serwisowe.
- [[Repo - Przegląd Projektów i Wersji]] — układ repozytorium i historia.

## Granice obecnego stanu

WeMos i ESP32-S3 mają oddzielne środowiska kompilacji. Wspólny protokół UART
istnieje w kodzie, ale połączenie między płytkami nie jest podłączone; panel
nie prezentuje więc potwierdzonej telemetrii WeMos. Karmnik 28BYJ-48 + ULN2003
pozostaje odłączony do czasu ustalenia bezpiecznego okablowania IN1–IN4.

## Szybki dostęp

- [[Procedura - Programowanie i Wgrywanie Firmware]] — kompilacja i wgrywanie
  wybranego środowiska; przed uploadem trzeba wykryć właściwy port.
- [[Procedura - Diagnostyka i Self-Test WeMos]] — osobne testy wejść, I²C,
  temperatury i przekaźników.
- [[Procedura - Kalibracja Sondy pH]] — kalibracja czujnika.
- [[AquaBox - Zalecenia Druku 3D i Śruby]] — profil startowy i mocowanie.
