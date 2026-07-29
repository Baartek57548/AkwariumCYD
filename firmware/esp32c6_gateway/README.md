# AquaCYD ESP32-C6 gateway

Ten firmware jest przeznaczony dla **osobnego, nieruchomego ESP32-C6**. Układ
pozostaje w pobliżu akwarium lub routera, odbiera zaszyfrowany ESP-NOW z CYD i
publikuje stan do MQTT Home Assistanta. ESP32-C6 znajdujący się na płytce
Waveshare/Elecrow z ESP32-P4 nadal pełni rolę modemu Wi-Fi panelu HMI.

## Budowanie

Wymagany jest ESP-IDF 5.4.4:

```powershell
cd firmware/esp32c6_gateway
idf.py set-target esp32c6
idf.py menuconfig
idf.py build
idf.py flash monitor
```

Z katalogu głównego można użyć sprawdzonego skryptu:

```powershell
.\tools\build-p4-c6.ps1 -Target c6 -IdfPath C:\esp\v5.4.4-full\esp-idf
```

W menu `AquaCYD ESP32-C6 gateway` trzeba ustawić SSID 2,4 GHz, dane brokera
MQTT, MAC sterownika CYD oraz niezależne losowe klucze PMK i LMK zapisane jako
32 cyfry szesnastkowe. Klucze muszą być identyczne po stronie CYD. Pliku
`sdkconfig` zawierającego dane dostępowe nie należy commitować.

Gateway łączy się z punktem dostępowym przed uruchomieniem ESP-NOW, dlatego
ESP-NOW automatycznie używa kanału aktualnej sieci Wi-Fi. Router powinien mieć
ustawiony stały kanał 2,4 GHz; automatyczna zmiana kanału przerwie łączność z
CYD do czasu ponownej synchronizacji kanału.

## Kontrakt MQTT

- stan: `aquacyd/aquarium/state`,
- dostępność: `aquacyd/aquarium/availability`,
- polecenia: `aquacyd/aquarium/command/set`,
- potwierdzenia: `aquacyd/aquarium/command/ack`.

Przykładowe polecenie:

```json
{
  "command_id": "18d4a6c30f924be1",
  "action": "set_output",
  "target": "filter",
  "value": 1,
  "duration_ms": 900000,
  "expected_revision": 19
}
```

`command_id` jest obowiązkowym, niezerowym identyfikatorem zapisanym jako
dokładnie 16 cyfr szesnastkowych. Gateway waliduje format i zakresy, ponawia
ramkę do czterech razy i raportuje błąd transportu, jeżeli CYD nie prześle
aplikacyjnego potwierdzenia. CYD wykonuje końcową kontrolę dozwolonego celu,
rewizji konfiguracji i duplikatów. Samo potwierdzenie warstwy radiowej nie jest
traktowane jako wykonanie polecenia.
