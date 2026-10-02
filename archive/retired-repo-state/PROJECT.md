# AquaCYD — plan techniczny

## Zakres aktywny

System składa się z CYD, WeMos/UART, lokalnego ekranu LVGL i panelu WWW na
karcie SD. Usunięto aplikacje Flutter, Bluetooth, ESP-NOW, P4, C6, zewnętrzny
HMI oraz czujnik zalania.

## Priorytety

1. Stabilna automatyka i fail-safe bez zależności od Wi-Fi.
2. UART pracujący stale w osobnym zadaniu.
3. Brak restartów podczas zmiany ustawień.
4. Spójny jasny i ciemny motyw CYD.
5. Responsywny panel WWW z czytelnymi wartościami na telefonie.
6. Podpisane OTA, walidacja wejść i testy regresyjne.

## Bramy jakości

- `npm run build:web-assets` i `npm run test:api`;
- `pio test -e native` i `pio test -e native_ui`;
- `pio run -e cyd_2_8`, `pio run -e esp32dev-st7789` i
  `pio run -e wemos_d1_uno`;
- test przeglądarkowy telefonu oraz kontrola błędów konsoli;
- po kompilacji dopiero kopiowanie SD i wgrywanie CYD.

