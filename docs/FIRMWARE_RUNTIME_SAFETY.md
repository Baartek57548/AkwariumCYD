# Bezpieczeństwo runtime, kalibracja i diagnostyka firmware

Ten dokument opisuje mechanizmy wykonawcze firmware CYD, które chronią
automatykę akwarium przed zawieszeniem zadania, niestabilnym czujnikiem,
uszkodzonym zapisem konfiguracji i utratą łączności. Dotyczy profili
`esp32dev` (ILI9341) i `esp32dev-st7789`.

## Watchdog, supervisor i stan bezpieczny

Firmware rozdziela pracę między trzy nadzorowane zadania:

| Zadanie | Rdzeń | Odpowiedzialność | Maksymalny wiek heartbeat |
| --- | ---: | --- | ---: |
| UI | 1 | LVGL, dotyk, zastosowanie telemetrii | 4 s |
| I/O | 0 | czujniki, wyjścia, Wi-Fi, HTTP, BLE i OTA | 4 s |
| supervisor | 1 | kontrola heartbeat i inicjowanie fail-safe | Task WDT 8 s |

Po 15-sekundowym oknie rozruchowym supervisor wymaga zarówno obecności, jak
i świeżości heartbeat z UI oraz I/O. Brak heartbeat powoduje następującą,
deterministyczną sekwencję:

1. sprzętowe wyjścia MCP23017 są zatrzaskiwane w stanie bezpiecznym;
2. przyczyna, uptime, minimalny wolny heap i potwierdzenie stanu bezpiecznego
   są zapisywane w NVS;
3. log szeregowy jest opróżniany;
4. ESP32 jest restartowany.

Task WDT jest konfigurowany na 8 sekund. Firmware obsługuje również wariant,
w którym framework Arduino zainicjalizował WDT wcześniej: poprawna subskrypcja
zadania jest wtedy ostatecznym potwierdzeniem działania watchdog. Dostęp do
rekordu diagnostycznego i NVS jest serializowany mutexem, aby supervisor,
restart ręczny i odczyt statusu nie modyfikowały go równocześnie.

Zapisany powód restartu ma jeden z kodów:

- `ui_heartbeat_stale`;
- `io_heartbeat_stale`;
- `task_watchdog_reset`;
- `panic_reset`;
- `gui_initialization_failed`;
- `io_task_start_failed`;
- `manual_restart`;
- `factory_reset`;
- `ota_update`.

Pole `system` odpowiedzi `GET /api/status` zawiera między innymi `bootId`,
`bootCount`, `faultCount`, `minimumFreeHeap`, `lastFaultReason`,
`lastFaultUptimeMs`, `lastFailSafeConfirmed` i `actuatorWriteErrors`.
Ten sam skrócony stan jest dostępny w diagnostyce BLE.

## Stabilizacja i trwała historia alarmów

Każdy alarm jest stabilizowany niezależnie przed zmianą
`current_alarm_flags`, zapisem do NVS i przekazaniem do bramki:

- typowe alarmy wymagają 2 kolejnych próbek do aktywacji;
- wyczyszczenie każdego alarmu wymaga 3 kolejnych poprawnych próbek;
- wyciek i błąd zapisu wyjścia są aktywowane natychmiast, ale ich
  wyczyszczenie nadal wymaga 3 potwierdzeń.

Chroni to pamięć Flash i powiadomienia przed serią zmian przy wartości
oscylującej wokół progu. Zasada nie opóźnia alarmów stanowiących bezpośrednie
zagrożenie.

| Bit | Flaga | Znaczenie |
| ---: | --- | --- |
| 0 | `AlarmTemperatureHigh` | temperatura powyżej 28°C |
| 1 | `AlarmTemperatureLow` | temperatura poniżej 20°C |
| 2 | `AlarmPhOutOfRange` | pH poza zakresem 6,0–8,0 |
| 3 | `AlarmWaterLevelLow` | niski poziom wody |
| 4 | `AlarmLeak` | wykryty wyciek |
| 5 | `AlarmSupplyLow` | zbyt niskie napięcie zasilania |
| 6 | `AlarmSensorMissing` | brak wymaganego czujnika |
| 7 | `AlarmSensorStale` | dane wymaganego czujnika są nieświeże |
| 8 | `AlarmSensorBusFault` | błąd magistrali czujników lub MCP23017 |
| 9 | `AlarmActuatorWriteFailed` | nieudany zapis stanu wyjścia |

Wymagane czujniki wynikają z konfiguracji. Temperatura i MCP23017 są wymagane
zawsze, pH po włączeniu prezentacji pH lub CO2, EC po włączeniu EC, a wejścia
poziomu wody i wycieku po włączeniu odpowiednich funkcji.

Ostatnich 16 przejść jest przechowywanych w pierścieniu NVS z CRC. Każdy wpis
zawiera `bootId`, sekwencję, nonce, aktualne flagi oraz maski `raisedFlags`
i `clearedFlags`. Historia jest dostępna przez:

```text
GET /api/alarm-events
GET /api/v2/alarm-events
```

`eventId` jest stabilny, dlatego bramka może bezpiecznie ponowić wysyłkę po
zerwaniu połączenia bez tworzenia logicznie nowego zdarzenia.

## Kalibracja pH i EC

Kalibracja ma wersjonowany rekord NVS z CRC. Niepoprawna wersja, CRC lub
wartość powoduje bezpieczny powrót do ustawień domyślnych. Zapis NVS i zmiana
aktywnej kopii RAM odbywają się pod jednym mutexem: po błędzie zapisu aktywna
kalibracja nie zmienia się.

Tor pomiarowy używa mediany z 5 próbek. Wartość jest oznaczana jako ważna
dopiero po zapełnieniu całego okna, co odrzuca pojedyncze skoki ADC.

- pH korzysta z interpolacji dwóch punktów, domyślnie 4,01 i 6,86;
- EC korzysta z jednego roztworu referencyjnego, domyślnie 1413 µS/cm;
- EC ma kompensację temperatury z domyślnym współczynnikiem `0.019` i
  temperaturą referencyjną 25°C;
- niepoprawne, nieskończone i pozazakresowe wyniki są zwracane jako brak
  ważnego odczytu.

Pełny status HTTP i BLE zawiera bieżący obiekt `calibration`. Zapis przez
`POST /api/action` albo alias JSON `POST /api/v2/action` używa akcji
`save_calibration` i aktywnej sesji administratora.

Przykład pH:

```json
{
  "action": "save_calibration",
  "type": "ph",
  "lowRaw": 10420,
  "lowReference": 4.01,
  "highRaw": 8120,
  "highReference": 6.86
}
```

Przykład EC:

```json
{
  "action": "save_calibration",
  "type": "ec",
  "referenceRaw": 11304,
  "referenceUsCm": 1413.0,
  "temperatureCoefficient": 0.019,
  "referenceTemperatureC": 25.0
}
```

Punkty pH muszą różnić się co najmniej o 64 jednostki ADC. Pola liczbowe są
parsowane ściśle; wartości niepełne, `NaN`, nieskończoność i dane poza
zakresem są odrzucane bez częściowej zmiany konfiguracji.

## Hasła Wi-Fi i migracja profili SD

Hasło Wi-Fi nie jest zapisywane na karcie SD. Rekord poświadczeń znajduje się
w przestrzeni NVS `aq_wifi_sec`; klucz profilu jest wyprowadzany z SSID,
a rekord ma wersję, nonce i CRC. Na SD pozostaje tylko niesekretny profil v2:

```text
format=aq-wifi-profile-v2
schema_version=2
credential_store=nvs
ssid=MojaSiec
last_ip=192.168.1.20
last_rssi=-61
updated_ms=123456
```

Podczas pierwszego uruchomienia po aktualizacji firmware wykrywa starsze pliki
z polem `password`, przenosi sekret do NVS, nadpisuje poprzednią zawartość pliku
i zapisuje metadane v2. Migracja jest ograniczona do 32 profili na rozruch,
aby uszkodzona lub celowo przepełniona karta nie blokowała automatyki.
Bufory zawierające hasła są zerowane przez zapis `volatile`.

NVS jest bezpiecznym miejscem na sekret wyłącznie wtedy, gdy urządzenie
produkcyjne ma włączone Flash Encryption zgodnie z procedurą
`FIRMWARE_SIGNING_AND_PROVISIONING.md`. CRC wykrywa uszkodzenie, ale nie jest
szyfrowaniem. Nadpisanie sektorów SD jest działaniem best-effort; dla nośników
flash nie daje gwarancji usunięcia kopii pozostałej po wear-levelingu.

## Warunek zatwierdzenia OTA

Nowa partycja nie zostaje uznana za zdrową tylko dlatego, że uruchomił się
interfejs. W profilu produkcyjnym health gate wymaga równocześnie:

- świeżej ramki telemetrii i działającego zadania I/O;
- gotowego UI i poprawnej konfiguracji;
- świeżej, ważnej temperatury;
- ważnego pH, jeżeli pH lub CO2 są włączone;
- ważnego EC, jeżeli EC jest włączone;
- obecnego i poprawnie odczytanego MCP23017;
- braku alarmów `SensorMissing`, `SensorStale`, `SensorBusFault` oraz
  `ActuatorWriteFailed`.

Tryb DEV omija wymagania fizycznych czujników. W produkcji brak skonfigurowanego
czujnika powoduje brak zatwierdzenia i umożliwia rollback zamiast utrwalenia
obrazu, który nie potrafi poprawnie obsłużyć bieżącej konfiguracji.

## Zdalna bramka alarmowa

Konfiguracja bramki przyjmuje wyłącznie adres `https://`, identyfikator
urządzenia i sekret HMAC Base64. Sekret nigdy nie występuje w statusie, logach
ani odpowiedzi API. Provisioning oraz włączenie, wyłączenie i czyszczenie
bramki są dostępne wyłącznie przez BLE z LE Secure Connections, bondingiem,
MITM, kluczem 128-bit i aktywną sesją administratora. Próba przez HTTP zwraca
`403` z kodem `secure_transport_required`.

Obsługiwane akcje BLE:

- `save_remote_gateway`;
- `set_remote_gateway_enabled`;
- `clear_remote_gateway`.

Status HTTP/BLE udostępnia tylko dane niesekretne: stan włączenia,
provisioningu i zadania, obecność certyfikatu CA, liczniki prób, ostatni kod
błędu, bazowy URL i identyfikator urządzenia.

## Walidacja wydania

Minimalny zestaw przed publikacją firmware:

```powershell
pio test -d firmware/cyd_controller -e native
pio run -d firmware/cyd_controller -e esp32dev
pio run -d firmware/cyd_controller -e esp32dev-st7789
```

Oba obrazy zajmują obecnie około 96% partycji aplikacji `min_spiffs.csv`.
Kolejne duże biblioteki, certyfikaty i zasoby powinny zostać poprzedzone
budżetem Flash. Zasoby WWW i grafika powinny pozostać na SD, a dalsze funkcje
sieciowe warto wydzielać do bramki zamiast powiększać firmware ESP32.
