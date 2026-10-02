# Bezpieczeństwo runtime i diagnostyka firmware

Dokument dotyczy profili `cyd_2_8` (ILI9341) i `esp32dev-st7789`.

## Zadania i watchdog

| Zadanie | Rdzeń | Odpowiedzialność | Maksymalny wiek heartbeat |
| --- | ---: | --- | ---: |
| UI | 1 | LVGL, dotyk, zastosowanie snapshotu | 4 s |
| I/O | 0 | UART, czujniki, wyjścia, Wi-Fi, HTTP i OTA | 4 s |
| supervisor | 1 | kontrola heartbeat i fail-safe | Task WDT 8 s |

Po 15-sekundowym oknie rozruchowym supervisor wymaga świeżego heartbeat UI i
I/O. Jego brak zapisuje przyczynę do NVS, ustawia bezpieczne wyjścia i dopiero
potem restartuje urządzenie. Rekord zawiera m.in. `bootCount`, `faultCount`,
`minimumFreeHeap`, `lastFaultReason` i potwierdzenie fail-safe.

UART działa w zadaniu I/O niezależnie od renderowania oraz klienta WWW.
Zatrzymanie lub przeciążenie HTTP nie może zatrzymać wymiany z WeMos.

## Naprawiona blokada HTTP

Poprzednia implementacja ręcznie wywoływała `flush()` i `stop()` na
`_currentClient` w trakcie odpowiedzi o nieznanej długości. Kolejne
`sendContent()` mogło blokować się lub ponawiać zapis, przez co UI nie
aktualizowało heartbeat. Supervisor rejestrował `ui_heartbeat_stale` i
restartował CYD podczas zmiany danych.

Kod pozostawia teraz własność połączenia klasie `WebServer`, wysyła końcowy
pusty fragment odpowiedzi i nie zamyka gniazda z warstwy aplikacyjnej. Test
regresyjny sprawdza brak ręcznego `flush/stop` w `FastWebServer`.

## Alarmy

Alarmy typowe wymagają dwóch kolejnych próbek do aktywacji i trzech do
wyczyszczenia. Błąd zapisu wyjścia aktywuje się natychmiast. Historia 16 zmian
jest pierścieniem NVS z CRC.

| Bit | Flaga | Znaczenie |
| ---: | --- | --- |
| 0 | `AlarmTemperatureHigh` | temperatura powyżej limitu |
| 1 | `AlarmTemperatureLow` | temperatura poniżej limitu |
| 2 | `AlarmPhOutOfRange` | pH poza zakresem |
| 3 | `AlarmWaterLevelLow` | niski poziom wody |
| 4 | `AlarmReserved4` | rezerwa, bez funkcji |
| 5 | `AlarmSupplyLow` | zbyt niskie zasilanie |
| 6 | `AlarmSensorMissing` | brak wymaganego czujnika |
| 7 | `AlarmSensorStale` | nieświeży pomiar |
| 8 | `AlarmSensorBusFault` | błąd magistrali |
| 9 | `AlarmActuatorWriteFailed` | nieudany zapis wyjścia |

Nie ma czujnika ani alarmu zalania. Pływak poziomu MAX na `GPB1` jest zwykłym
wejściem poziomu wody i nie zajmuje bitu `AlarmReserved4`. Dawny bit pozostaje
rezerwą dla stabilności zapisanych struktur i ramek. Brak wiarygodnego odczytu
MAX jest sygnalizowany osobnym polem ważności, a nie stanem nieaktywnym.

## Kalibracja

pH i EC mają wersjonowany rekord NVS z CRC. Zapis NVS i podmiana aktywnej kopii
RAM są atomowe pod krótką blokadą; błąd nie pozostawia częściowej konfiguracji.
Tor ADC używa mediany z pięciu próbek. Wartości `NaN`, nieskończone,
niekompletne i poza zakresem są odrzucane.

Akcja `save_calibration` wymaga aktywnej sesji administratora. Pełny status
HTTP zwraca wyłącznie niesekretną konfigurację kalibracji.

## Wi-Fi i karta SD

Hasła Wi-Fi są w NVS `aq_wifi_sec`, a karta SD zawiera tylko SSID, ostatni IP,
RSSI i czas aktualizacji. Migracja starszych profili usuwa pole hasła i jest
ograniczona do 32 plików na rozruch. Bufory sekretów są zerowane.

NVS chroni poufność dopiero po włączeniu Flash Encryption. CRC wykrywa
uszkodzenie, ale nie szyfruje danych.

## Powierzchnia radiowa

Firmware nie zawiera Bluetooth ani ESP-NOW. Podczas startu zwalnia
nieużywaną rezerwę BTDM. Jedynym aktywnym radiem jest Wi-Fi potrzebne do
lokalnego WWW i OTA.

Opcjonalny outbound relay HTTPS może działać wyłącznie z rekordem
przygotowanym w kontrolowanym procesie serwisowym. Publiczne HTTP nie przyjmuje
sekretów ani nie udostępnia komend provisioningu.

## OTA

Nowa partycja jest zatwierdzana dopiero, gdy działa I/O i UI, konfiguracja jest
poprawna, wymagane czujniki są świeże, MCP23017 odpowiada i nie ma krytycznych
alarmów sprzętowych. Błędny podpis, target, rozmiar, wersja bezpieczeństwa lub
niepełny payload powodują odrzucenie albo rollback.

## Walidacja

```powershell
pio test -e native
pio test -e native_ui
pio run -e cyd_2_8
pio run -e esp32dev-st7789
pio run -e wemos_d1_uno
npm run test:api
```
