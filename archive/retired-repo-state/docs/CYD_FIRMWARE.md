# Firmware sterownika akwarium CYD

Firmware obsługuje ESP32-2432S028 z ekranem 320×240 i dotykiem XPT2046. Profile
ILI9341 (`cyd_2_8`) i ST7789 (`esp32dev-st7789`) są rozdzielone. Ten
egzemplarz używa ST7789.

## Runtime

- Core 1: LVGL, DMA, filtrowany dotyk i heartbeat UI.
- Core 0: UART CYD–WeMos, czujniki, wyjścia, Wi-Fi, HTTP/OTA i dźwięk.
- Jednoelementowa kolejka przekazuje najnowszy spójny snapshot do UI.
- UART pracuje stale i nie zależy od renderowania ani obsługi WWW.
- Bluetooth i ESP-NOW nie są kompilowane; nieużywana rezerwa BTDM jest
  zwalniana podczas startu.

## Profile

```powershell
pio test -e native
pio test -e native_ui
pio run -e cyd_2_8
pio run -e esp32dev-st7789
pio run -e wemos_d1_uno
```

Wgrywanie ST7789:

```powershell
pio run -e esp32dev-st7789 -t upload --upload-port COM7
```

Port należy potwierdzić przez `pio device list`.

## Wyświetlacz i dotyk

LVGL używa dwóch statycznych buforów DMA po 10 linii. Zakończenie DMA jest
sygnalizowane dopiero po zwolnieniu bufora. Dotyk ma osobne potwierdzenie
naciśnięcia i zwolnienia, filtr skoków oraz histerezę. Kalibracja znajduje się
w `HwConfig::Touch`.

Jasny i ciemny motyw muszą ustawiać tło nagłówka, treści i dolnej nawigacji z
jednego zestawu tokenów. Obiekty nawigacji pozostają nad treścią i aktywne
dotykowo po każdym przeładowaniu motywu.

## Sprzęt i fail-safe

MCP23017 jest ustawiany w bezpieczny stan przed utworzeniem GUI. Brak
MCP23017, ADS1115 lub poprawnej ramki UART uruchamia ponawianie i odpowiedni
alarm, ale nie restartuje pętli. Błędne CRC, `NaN` i pomiary poza zakresem nie
sterują wyjściami.

Nie ma czujnika zalania. `GPB1` obsługuje pływak poziomu MAX, a `GPB2` pływak
MIN. Telemetria MAX wymaga poprawnego odczytu `w_max_valid`; brak tego pola w
starszej ramce oznacza stan nieznany. Osiągnięcie MAX jest normalnym stanem
czujnika, nie alarmem zalania. Dawny bit alarmu pozostaje rezerwą dla zgodności
struktur.

## WWW i restart podczas zapisu

Warstwa aplikacyjna nie zamyka aktywnego klienta podczas odpowiedzi chunked.
Cykl życia gniazda należy do `WebServer`. Zapobiega to blokadzie
`sendContent()`, zagłodzeniu heartbeat UI i resetowi
`ui_heartbeat_stale` przy zmianie ustawień.

## OTA

Produkcja przyjmuje wyłącznie podpisany pakiet `.aqfw` dla właściwego panelu.
Weryfikowane są RSA-3072/PSS, target, wersja, security version, rozmiar i
SHA-256. Restart następuje dopiero po wysłaniu odpowiedzi HTTP, a nowy slot
wymaga health checku.
