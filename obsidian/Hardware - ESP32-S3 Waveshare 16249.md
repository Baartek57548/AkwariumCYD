---
aliases:
  - Panel AquaBox ESP32-S3
tags:
  - hardware
  - esp32s3
  - display
category: hardware
status: active
---

# ESP32-S3 i ekran Waveshare 16249

Panel AquaBox korzysta z modułu ESP32-S3-WROOM-1-N16R8 oraz ekranu Waveshare
16249. Wyświetlacz ma 800 × 480 pikseli, magistralę równoległą RGB 16-bit
(RGB565) i dotyk po I²C przez GT911. To nie jest ekran SPI ze starego CYD.

Aktywna mapa RGB/synchronizacji i obsługa GT911 znajdują się w
`firmware/esp32s3_ui/src/panel_hal.cpp`. Przed przepinaniem przewodów sprawdź
mapę GPIO w tym pliku i zgodność rewizji posiadanej płytki z dokumentacją.
Nie używaj GPIO16 do wgrywania — port COM16 jest podłączony do drukarki 3D.

Dotyk i obraz były wcześniej potwierdzone przez użytkownika. Bieżąca zmiana
palet kontrastu wymaga osobnego odbioru na fizycznym LCD. Podgląd RGB565
na komputerze nie symuluje jasności podświetlenia ani charakterystyki panelu.

Firmware: [[Firmware - ESP32-S3 Panel UI]].
Poprzedni sprzęt CYD jest opisany historycznie w [[Hardware - ESP32 CYD 2432S028R]].
