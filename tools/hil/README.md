# AquaCYD HIL runner

`runner.py` wykonuje testy odporności na prawdziwym sterowniku, ale pozostaje
bezpieczny na komputerze bez podłączonego sprzętu: brak `AQUACYD_HIL_BASE_URL`
powoduje jawny wynik `SKIP`, a nie fałszywy sukces. Zmiany stanu są blokowane,
dopóki operator nie ustawi `AQUACYD_HIL_ALLOW_MUTATIONS=1`.

Szybka walidacja samego narzędzia:

```powershell
python tools/hil/runner.py --self-test
python tools/hil/runner.py --dry-run
```

Minimalny test tylko do odczytu:

```powershell
$env:AQUACYD_HIL_BASE_URL = "http://192.168.1.50"
python tools/hil/runner.py
```

Testy mutujące uwierzytelniają się przez rzeczywisty `POST /api/v2/auth`.
Runner pobiera token sesyjny przy starcie pierwszej komendy, przechowuje go tylko
w pamięci procesu i raz ponawia uwierzytelnienie po `session_expired`:

```powershell
$env:AQUACYD_HIL_ADMIN_PIN = "<pin-stanowiska>"
$env:AQUACYD_HIL_ALLOW_MUTATIONS = "1"
python tools/hil/runner.py
```

Kompletne stanowisko może skonfigurować:

| Zmienna | Znaczenie |
| --- | --- |
| `AQUACYD_HIL_BASE_URL` | bazowy adres sterownika w izolowanej sieci laboratoryjnej |
| `AQUACYD_HIL_ADMIN_PIN` | PIN stanowiska używany wyłącznie do utworzenia 5-minutowej sesji v2 |
| `AQUACYD_HIL_TOKEN` | opcjonalny, już utworzony token 32-znakowy; nie zapisuj go na stałe |
| `AQUACYD_HIL_ALLOW_MUTATIONS=1` | zgoda na komendy i testy zmieniające stan |
| `AQUACYD_HIL_WIFI_CUT_URL` | endpoint zarządzanego AP/przekaźnika odcinający Wi-Fi |
| `AQUACYD_HIL_WIFI_RESTORE_URL` | endpoint przywracający Wi-Fi, niezależny od sterownika |
| `AQUACYD_HIL_ALLOW_OTA_ROLLBACK=1` | osobna zgoda na destrukcyjny test rollbacku |
| `AQUACYD_HIL_OTA_ROLLBACK_URL` | endpoint wyzwalający rollback na stanowisku |
| `AQUACYD_HIL_OTA_HEALTH_PATH` | opcjonalna ścieżka pełnego health ze slotem; wymagana tylko do testu rollbacku |
| `AQUACYD_HIL_AQUAEL_TRACE_URL` | endpoint izolowanego fixture rejestrującego dwa kanały lamp |
| `AQUACYD_HIL_AQUAEL_TRANSITION_TIMEOUT_SECONDS` | deadline profilu lampy; domyślnie `20` s |
| `AQUACYD_HIL_LAB_TOKEN` | krótkotrwały token tylko do usług stanowiska |
| `AQUACYD_HIL_SERIAL_PORT` | opcjonalny port, np. `COM5` albo `/dev/ttyUSB0` |
| `AQUACYD_HIL_SERIAL_BAUD` | prędkość portu, domyślnie `115200` |

Ścieżki API, czasy i pole stanu override można dostosować zmiennymi opisanymi
przez `python tools/hil/runner.py --help` oraz w `Config.from_environment`.
Kontrakt domyślny jest zgodny z firmware: form POST `/api/v2/auth`, kanoniczny
form POST `/api/action`, GET `/api/status` i GET `/api/v2/capabilities`.
`/api/v2/action` pozostaje zgodnym aliasem dla klientów używających pełnej
koperty JSON. Firmware raportuje stan OTA w `data.ota` capabilities. Nie
zakładamy istnienia osobnego endpointu health OTA; jeżeli stanowisko udostępnia
pełny kontrakt z `bootSlot`, operator podaje jego ścieżkę jawnie.

Test odcięcia Wi-Fi wymaga osobnego kontrolera AP lub przekaźnika. Endpoint
przywracający nie może znajdować się na urządzeniu, któremu właśnie odcinamy
łączność. Test rollbacku powinien działać tylko na stanowisku z bezpiecznym
zasilaniem i znanym, poprawnym obrazem w drugiej partycji.

Runner najpierw używa realnej akcji `set_light_profile`, aby sprawdzić niezależne
profile `front` i `rear`. W bloku końcowym próbuje przywrócić oba override do
AUTO, a niepowodzenie cleanup oznacza FAIL. Osobny, zewnętrzny fixture
laboratoryjny (to nie jest endpoint firmware) przyjmuje JSON z operacją
`exercise_aquael_daynight` i zwraca `events` oraz pustą listę `conflicts`.
Każde zdarzenie zawiera `target`
(`front`/`rear`), `state`, monotoniczne `atMs`, unikalny `commandId` oraz
`profile` dla zbocza ON. Runner wymaga czterech par OFF/ON na kanał i profili
`day`, `daybreak`, `night`, `day`: dwa środkowe czasy OFF nie przekraczają
5 sekund, a pierwszy i ostatni reset trwają dłużej niż 5 sekund.
