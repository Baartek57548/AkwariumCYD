# Firmware sterownika akwarium CYD

Firmware jest przeznaczony dla płytki ESP32-2432S028 (Cheap Yellow Display)
z ekranem ILI9341 320×240 i kontrolerem dotyku XPT2046. Profil ST7789 jest
dostępny osobno dla zgodnych wariantów sprzętowych.

## Architektura czasu wykonywania

- Core 1 obsługuje LVGL, renderowanie DMA i filtrowany dotyk. Tylko ta ścieżka
  wywołuje `lv_timer_handler()`.
- Core 0 obsługuje Wi-Fi, HTTP/OTA, BLE, dźwięk, odczyty czujników i wyjścia
  MCP23017.
- Jednoelementowa kolejka FreeRTOS przekazuje najnowszą spójną ramkę
  telemetrii z Core 0 do Core 1. Starsza ramka jest zastępowana, więc UI nie
  tworzy zaległości.
- Rekurencyjny mutex chroni obiekty LVGL używane przez kod komunikacyjny.
- Sterownik DS18B20 działa jako nieblokująca maszyna stanów: wykrywanie,
  rozpoczęcie konwersji, odczyt po 750 ms, CRC, walidacja zakresu i ponawianie
  z narastającym odstępem.

## Pinologia

| Funkcja | GPIO | Uwagi |
|---|---:|---|
| LCD MOSI / MISO / SCLK | 13 / 12 / 14 | magistrala VSPI ekranu |
| LCD DC / CS / RST | 2 / 15 / brak | ILI9341, rotacja 3 |
| Touch MOSI / MISO / SCLK | 32 / 39 / 25 | osobna magistrala SPI |
| Touch CS / IRQ | 33 / 36 | GPIO36 i GPIO39 są tylko wejściami |
| Podświetlenie | 21 | PWM LEDC, kanał 7 |
| Karta SD MOSI / MISO / SCLK / CS | 23 / 19 / 18 / 5 | HSPI |
| I2C SDA / SCL | 27 / 22 | MCP23017 `0x20`, ADS1115 `0x48` |
| DS18B20 | 17 | wymagany rezystor podciągający 4,7 kΩ do 3,3 V |
| LDR | 34 | wejście ADC, tłumienie 11 dB |
| Głośnik | 26 | PWM LEDC, kanał 0 |
| RGB R / G / B | 4 / 16 / 17 | wyłączone, ponieważ B koliduje z OneWire |

GPIO2, GPIO5, GPIO12 i GPIO15 są pinami strapującymi ESP32. Nie należy
wymuszać na nich niewłaściwego poziomu podczas resetu. GPIO34, GPIO36 i GPIO39
nie mają wewnętrznych rezystorów podciągających.

## Profile PlatformIO

Kompilacja produkcyjna dla standardowego CYD z ILI9341:

```powershell
pio run -e esp32dev
```

Kompilacja i wgranie przez USB:

```powershell
pio run -e esp32dev -t upload
pio device monitor -b 115200
```

Profil deweloperski z symulowanymi czujnikami i rozszerzonym logowaniem:

```powershell
pio run -e esp32dev-dev
```

Wariant płytki z panelem ST7789:

```powershell
pio run -e esp32dev-st7789
```

Testy logiki domenowej na komputerze:

```powershell
pio test -e native
```

Gotowy obraz produkcyjny powstaje w
`.pio/build/esp32dev/firmware.bin`. Przed wgraniem profilu ST7789 należy
użyć obrazu z `.pio/build/esp32dev-st7789/firmware.bin`.

## Wyświetlacz i dotyk

LVGL używa dwóch statycznych buforów DMA po 10 linii. Zakończenie operacji DMA
jest sygnalizowane do LVGL dopiero po faktycznym zwolnieniu bufora, co usuwa
nadpisywanie obrazu i migotanie. Tekst etykiety jest aktualizowany tylko wtedy,
gdy wartość się zmieniła.

Dotyk ma osobne czasy potwierdzania naciśnięcia i zwolnienia, filtr skoków oraz
histerezę pozycji. Wartości kalibracyjne znajdują się w `HwConfig::Touch`:

```text
X_MIN=300, X_MAX=3900, Y_MIN=200, Y_MAX=3700, INVERT_Y=true
```

Jeśli inna rewizja panelu raportuje przesunięte współrzędne, należy zmienić
wyłącznie te stałe, bez modyfikowania sterownika dotyku.

## Czujniki i zachowanie awaryjne

- Przekaźniki MCP23017 są ustawiane w bezpieczny stan przed inicjalizacją GUI.
- Brak MCP23017 lub ADS1115 uruchamia okresowe ponowne wykrywanie bez restartu.
- Ramki DS18B20 z błędnym CRC, wartości `NaN`, nieskończone i temperatury poza
  zakresem od -10°C do 50°C nie mogą sterować wyjściami.
- Ostatnia poprawna temperatura pozostaje dostępna diagnostycznie, ale po
  przekroczeniu limitu świeżości jest oznaczona jako nieważna.
- Pasek statusu pokazuje połączenie, RSSI, końcówkę adresu IP, wiek ostatniej
  ramki i uptime. Kolor statusu sygnalizuje dane świeże, stare lub błąd HAL.

## Aktualizacja

Panel WWW i ArduinoOTA działają w zadaniu Core 0. Po przyjęciu obrazu firmware
restart jest odroczony, aby odpowiedź HTTP zdążyła zostać wysłana. Lokalny
restart działa także wtedy, gdy portal WWW nie jest uruchomiony.

Hasło punktu dostępowego OTA i PIN administratora są zdefiniowane w
`include/config.h`. Przed wdrożeniem urządzenia należy je zmienić. Hasło WPA
musi mieć co najmniej osiem znaków.

## Ograniczenia sprzętowe

Pamięć Flash profilu produkcyjnego jest wykorzystana w około 86%, dlatego
kolejne duże zasoby graficzne powinny trafiać na kartę SD, a nie do binarki.
Bufory renderowania są statyczne, co stabilizuje stertę kosztem stałego użycia
około 12,8 kB RAM.
