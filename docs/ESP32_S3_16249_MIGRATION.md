# Aktualna architektura AquaBox: WeMos D1 + ESP32-S3

Stan repozytorium na 2 października 2026 r. Aktywne firmware jest utrzymywane
dla dwóch urządzeń: sterownika WeMos D1 z ESP8266 oraz panelu ESP32-S3
WROOM-1-N16R8 z ekranem Waveshare 16249. Starszy kod CYD znajduje się poza
aktywnymi celami kompilacji, w `archive/retired-repo-state/`.

## Podział odpowiedzialności

| Urządzenie | Rola w kodzie | Stan integracji |
|---|---|---|
| WeMos D1 / ESP8266 | Czujniki, wejścia i wyjścia wykonawcze; konfiguracja w `firmware/wemos_controller/include/wemos_config.h`. | Firmware buduje się osobno. |
| ESP32-S3-WROOM-1-N16R8 | Interfejs dotykowy LVGL, ekran RGB, ustawienia i lokalny model UI. | Firmware buduje się osobno; bieżący model nie otrzymuje telemetrii przez UART. |
| Waveshare 16249 | Ekran IPS 4,3 cala 800 × 480, równoległe RGB565 i dotyk GT911 po I²C. | Obraz i dotyk były wcześniej potwierdzone na urządzeniu; aktualna zmiana palet kontrastu została zbudowana, ale nie wgrana. |

Protokół UART jest wydzielony w `firmware/shared/uart_protocol/`, lecz połączenie
panelu z WeMos nie jest obecnie podłączone ani odbierane jako działająca
telemetria. Informacje i animacje lokalnego modelu nie potwierdzają fizycznego
stanu pomp, lamp ani karmnika.

## Ekran i mapa sygnałów

Panel pracuje jako RGB565, 16-bitowa magistrala danych, 800 × 480. Bieżące
mapowanie RGB i synchronizacji znajduje się w
`firmware/esp32s3_ui/src/panel_hal.cpp`:

| Sygnał panelu | GPIO ESP32-S3 |
|---|---:|
| B3–B7 | 15, 16, 17, 18, 21 |
| G2–G7 | 9, 10, 11, 12, 13, 14 |
| R3–R7 | 4, 5, 6, 7, 8 |
| PCLK / HSYNC / VSYNC / DE | 1 / 2 / 39 / 40 |
| GT911 SDA / SCL | 41 / 42 |

Pozostałe sygnały dotyku i zasilania należy weryfikować względem rewizji
posiadanego panelu i jego schematu. Nie podłączaj linii RGB ani dotyku według
samego numeru złącza bez sprawdzenia oznaczeń i wspólnej masy. Zasilanie
podświetlenia wymaga odpowiedniego źródła; panel nie udostępnia obecnie
regulacji jasności z interfejsu. Kontrast renderu na hoście nie jest pomiarem
kalibracji fizycznego LCD.

Kolejność GPIO w firmware WeMos jest zdefiniowana w
`firmware/wemos_controller/include/wemos_config.h`. Nie przenoś jej na ESP32-S3:
to inny układ pinów i osobny firmware.

## Aktualny interfejs

Interfejs ma karty Panel, Plan dnia, Historia, Serwis i System. Harmonogram
rozdziela ustawienia oświetlenia i urządzeń, a każda pora dnia może mieć
własny kolor belki. Motyw jasny i ciemny konfiguruje się niezależnie; każdy ma
sześć wariantów kolorystycznych. Kontrast tekstu, kart i powierzchni jest
sprawdzany w testach motywów.

Odczyty na ekranie bazują obecnie na modelu lokalnym. Łącze do WeMos i ręczny
napęd karmnika nie są potwierdzonymi funkcjami fizycznymi. Silnik 28BYJ-48 z
ULN2003 pozostaje niepodłączony; sterowanie ruchem jest blokowane do czasu
potwierdzenia sygnałów IN1–IN4 i sposobu zasilania sterownika.

## Budowanie i weryfikacja

Z katalogu głównego repozytorium:

```powershell
python -m platformio run -e esp32s3_waveshare_ui
python -m platformio run -e wemos_d1_uno
./tools/verify_s3_ui.ps1
```

`tools/verify_s3_ui.ps1` buduje produkcyjny interfejs w LVGL na hoście,
uruchamia scenariusze regresji i odświeża renderowaną galerię w
`docs/ui/przekroj/`. Kompilacja hostowa sprawdza logikę i piksele RGB565,
ale nie emuluje jasności, jakości przewodów ani zachowania podświetlenia LCD.

Profile `esp32s3_n16r8_bringup`, `esp32s3_waveshare_lcd_test` oraz
`test_wemos_*` są programami diagnostycznymi i nie są domyślnym firmware.
Przed wgraniem wykryj aktualny port właściwej płytki. COM16 jest podłączony
do drukarki 3D i nie wolno używać go do ESP32-S3.

## Weryfikacja sprzętu i ograniczenia

- Kompilacja interfejsu ESP32-S3 została potwierdzona po zmianie kontrastu
  motywów 2.10.2026; ta konkretna wersja nie była jeszcze wgrywana.
- Użytkownik wcześniej potwierdził obraz i dotyk na module; nie oznacza to
  fizycznego odbioru każdej nowej palety ani poprawności kolorów monitora.
- UART do WeMos pozostaje niepodłączony, więc ekran nie jest źródłem
  rzeczywistych pomiarów ani potwierdzeń wykonania operacji.
- Nie wykonano testu ruchu silnika karmnika; wymagane mapowanie ULN2003 nadal
  nie istnieje.
- Build, test LVGL, upload i odbiór fizyczny to odrębne etapy. Wynik jednego
  nie zastępuje pozostałych.
