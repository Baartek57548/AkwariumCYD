# AquaBox — sterownik WeMos i panel ESP32-S3

Repozytorium utrzymuje firmware dla dwóch urządzeń: WeMos D1 (ESP8266), który
obsługuje wejścia i wyjścia akwarium, oraz panelu ESP32-S3 z ekranem Waveshare
16249 (800 × 480, RGB565, dotyk GT911). Starszy projekt CYD i powiązane
materiały zachowano w [`archive/retired-repo-state/`](archive/retired-repo-state/);
nie jest on aktywnym celem kompilacji.

Wcześniej rozważano podział na ESP32-P4 i ESP32-C6, ale była to tylko
koncepcja i nie została przyjęta. Aktualnymi celami sprzętowymi są WeMos D1
i ESP32-S3.

## Co zawiera aktywny projekt

- `firmware/wemos_controller/` — odczyt czujników i sterowanie przekaźnikami
  na WeMos D1; mapę wejść/wyjść opisuje `wemos_config.h`.
- `firmware/esp32s3_ui/` — interfejs LVGL panelu 800 × 480, ustawienia,
  harmonogram, historię, diagnostykę, tryb jasny/ciemny i sześć motywów,
  wybieranych niezależnie dla obu trybów.
- `firmware/shared/uart_protocol/` — wspólny format komunikatów dla planowanej
  komunikacji panelu ze sterownikiem.
- `firmware/esp32s3_bringup/`, `firmware/esp32s3_lcd_test/` i
  `firmware/wemos_tests/` — oddzielne programy diagnostyczne; nie są domyślnym
  firmware urządzeń.
- `docs/` — aktualny opis architektury, połączeń, projektu UI, weryfikacji
  i renderów LVGL.

## Granice sprzętowe

Panel i firmware WeMos budują się niezależnie. Łącze UART między nimi nie jest
jeszcze podłączone, więc odczyty i sterowanie widoczne w lokalnym modelu UI nie
są potwierdzeniem telemetrii z fizycznego akwarium. Karmnik 28BYJ-48 z ULN2003
pozostaje odłączony; ręczny obrót nie jest aktywowany bez potwierdzonego
okablowania IN1–IN4 i roli przekaźnika zasilającego sterownik.

## Budowanie i weryfikacja

Wymagany jest Python z PlatformIO. Firmware można zbudować poleceniami:

```powershell
python -m platformio run -e esp32s3_waveshare_ui
python -m platformio run -e wemos_d1_uno
```

Renderowanie produkcyjnego UI w LVGL na hoście wraz ze scenariuszami regresji:

```powershell
./tools/verify_s3_ui.ps1
```

Testy sprzętowe wybiera się jawnie przez środowiska `esp32s3_n16r8_bringup`,
`esp32s3_waveshare_lcd_test` i `test_wemos_*`. Przed wgrywaniem sprawdź port
podłączonej płytki i wybierz właściwy cel PlatformIO. Port COM16 jest
zarezerwowany dla drukarki 3D — nie używaj go do ESP32-S3.

## Dokumentacja

- [Aktualna architektura i migracja sprzętu](docs/ESP32_S3_16249_MIGRATION.md)
- [Projekt i zachowanie interfejsu](docs/S3_PRZEKROJ_DESIGN.md)
- [Galeria renderów LVGL](docs/S3_PRZEKROJ_GALLERY.md)
- [Wyniki testów i ograniczenia weryfikacji](docs/S3_PRZEKROJ_VERIFICATION.md)
- [Historia starszego projektu CYD](archive/retired-repo-state/README.md)
