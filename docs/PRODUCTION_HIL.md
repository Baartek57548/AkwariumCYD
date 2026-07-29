# Produkcyjne testy HIL

## Cel stanowiska

Test jednostkowy potwierdza logikę, lecz nie wykryje resetu radia, problemu z
partycją OTA ani zachowania przekaźnika po reconnect. Workflow
`.github/workflows/hil.yml` uruchamia najpierw symulator runnera, a na żądanie
przekazuje testy do runnera `self-hosted` z etykietą `aquacyd-hil`.

Stanowisko powinno mieć:

- CYD z zasilaniem o ograniczonym prądzie i dostępem USB/UART;
- testową sieć Wi-Fi bez dostępu do urządzeń domowych;
- zarządzany AP albo niezależny przekaźnik odcinający radio;
- dwukanałowy izolowany rejestrator stanów przekaźników oraz dwa czujniki
  optyczne rozpoznające barwę/moc lamp front i rear;
- bezpieczne obciążenia testowe zamiast grzałki, pompy i dozownika;
- poprawny obraz w drugim slocie OTA;
- możliwość fizycznego odzyskania urządzenia.

## Profile uruchomienia

`python tools/hil/runner.py --dry-run` sprawdza konfigurację bez I/O.
`--self-test` uruchamia lokalny serwer symulujący cały kontrakt. Bez
`AQUACYD_HIL_BASE_URL` zwykły run pokazuje każdy test jako `SKIP`.
Workflow sprzętowy używa `--require-hardware`, więc brak adresu jest błędem.

Zmiany stanu wymagają `AQUACYD_HIL_ALLOW_MUTATIONS=1`. Rollback ma dodatkową,
niezależną blokadę `AQUACYD_HIL_ALLOW_OTA_ROLLBACK=1`. Dzięki temu operator może
najpierw uruchomić wyłącznie testy odczytu.

## Scenariusze i kryteria

### Zdrowie urządzenia

Rzeczywisty `GET /api/status` musi odpowiedzieć w limicie i nie może zgłosić
`healthy=false`. To kontrola bazowa przed każdą próbą mutacji.

### Sesja administratora

Jeżeli podano `AQUACYD_HIL_ADMIN_PIN`, runner wywołuje form POST
`/api/v2/auth`, wymaga 32-znakowego `data.sessionToken` i dołącza go do
kanonicznego form POST `/api/action`. Alias `/api/v2/action` obsługuje również
pełną kopertę JSON. Token pozostaje wyłącznie w RAM procesu. Odpowiedź
`session_expired` lub `session_required` powoduje jedno kontrolowane ponowienie
logowania, nigdy nie nieograniczoną pętlę.

### Deduplikacja komendy

Runner wysyła dwa identyczne payloady z tym samym UUID. Druga odpowiedź musi
oznaczyć `duplicate`, `deduplicated`, `replayed`, zwrócić HTTP 208/409 albo
zwrócić dokładnie ten sam rezultat. Komenda może być zastosowana najwyżej raz.

### Wygaśnięcie override

Wyjście testowe jest przełączane z krótkim TTL. Status musi najpierw pokazać
tryb ręczny, a następnie samoczynnie wrócić do `auto` przed upływem TTL i
tolerancji. Pole jest konfigurowane przez
`AQUACYD_HIL_OVERRIDE_STATE_PATH`.

### Awaria Wi-Fi

Niezależny endpoint odcina sieć. Runner musi zaobserwować niedostępność, zawsze
przywrócić połączenie w `finally`, a następnie potwierdzić health przed deadlinem.
Endpoint restore nie może być hostowany na testowanym ESP32.

### OTA health

Rzeczywisty `GET /api/v2/capabilities` musi reklamować `safeOta` i zawierać
`data.ota`: `rollbackAvailable`, `updatePartitionBytes`, `pendingVerify` oraz
`state`. Dodatni rozmiar partycji i dostępność rollbacku podczas
`pendingVerify` są sprawdzane bez założenia nieistniejącej ścieżki API.

Pełny endpoint health ze stanem `healthy`, `bootSlot`, `rollbackAvailable` i
`pendingValidation` jest opcjonalnym rozszerzeniem stanowiska. Jego ścieżkę
podaje się przez `AQUACYD_HIL_OTA_HEALTH_PATH`; bez niej kontrola capabilities
działa, a destrukcyjny test zmiany slotu zwraca jawny `SKIP`.

### Rollback

Po podwójnej zgodzie operatora i ustawieniu pełnego health runner wyzwala rollback.
Test zalicza się dopiero, gdy urządzenie wróci jako zdrowe, przestanie oczekiwać
na walidację i zgłosi inny slot startowy. Brak obrazu, triggera albo pełnego
health oznacza jawny `SKIP`, a nie pozorny sukces.

### Dwie lampy Aquael LEDDY TUBE Day&Night

Producent definiuje trzy tryby i zmianę po cyklu OFF→ON w czasie nie dłuższym
niż 5 sekund; wyłączenie na dłużej niż 5 sekund przywraca tryb dzienny.
Opis znajduje się na [stronie produktu Aquael](https://www.aquael.pl/produkty/akwarystyka/akwarystyka/leddy-tube-sunny-daynight/)
oraz w [instrukcji producenta](https://www.aquael.pl/wp-content/uploads/2022/04/leddy_tube_daynight_a_multi-4_print_345.pdf).
Starsza instrukcja używa nazw `SUNNY / SUNNY+BLUE / BLUE`; runner mapuje je na
stosowane w projekcie `DAY / DAYBREAK / NIGHT`.

Najpierw runner używa faktycznego form POST `/api/action` z akcją
`set_light_profile`. Utrzymuje lampy przez ograniczony czas w override ON,
sprawdza niezależnie `lights.front` i `lights.rear` w `/api/status`, a na końcu
przywraca oba wyjścia do AUTO. Weryfikowana jest sekwencja konfiguracji:
`DAY/DAY`, `DAYBREAK/NIGHT`, `NIGHT/DAYBREAK`, `DAY/DAY`.

Zewnętrzny fixture skonfigurowany przez `AQUACYD_HIL_AQUAEL_TRACE_URL` mierzy
rzeczywiste wyjścia i barwę niezależnie dla lampy przedniej (`front`) i tylnej
(`rear`):

1. OFF dłużej niż 5 s → ON i potwierdzenie `DAY`;
2. OFF→ON w maksymalnie 5 s → `DAYBREAK`;
3. kolejne OFF→ON w maksymalnie 5 s → `NIGHT`;
4. OFF dłużej niż 5 s → ON i reset do `DAY`.

Runner wymaga ośmiu ściśle uporządkowanych zboczy na kanał, unikalnego
`commandId` każdego zbocza, pustej listy `conflicts` i dokładnej kolejności
profili. Polecenia `front` i `rear` mogą przebiegać równolegle, ale na jednym
kanale nie mogą wystąpić nakładające się ani sprzeczne komendy. Fixture musi
mierzyć rzeczywiste wyjścia, nie tylko odczytywać deklarowany stan API.
Dokładnie `5000 ms` jest poprawnym krótkim cyklem; dopiero wartość większa od
`5000 ms` resetuje do `DAY`. Firmware stosuje konserwatywny impuls cyklu
`1000 ms`, a przy kalibracji do `DAY` utrzymuje OFF przez `6000 ms`; te wartości
nie zmieniają oficjalnego progu lampy.

### Port szeregowy

Jeżeli skonfigurowano port oraz zainstalowano `pyserial`, runner oczekuje markera
startowego. Brak portu nie wpływa na testy HTTP i jest widoczny jako `SKIP`.

## Bezpieczeństwo operacyjne

Testów mutujących nie wolno uruchamiać na działającym akwarium. Konto runnera
nie powinno mieć uprawnień administracyjnych poza stanowiskiem. PIN stanowiska
jest sekretem środowiska `aquacyd-hil`; krótkotrwały token jest tworzony dopiero
w procesie i nie trafia do sekretów trwałych ani logów. Logów nie wolno
rozszerzać o nagłówki autoryzacji lub payload logowania.

Po każdym teście operator sprawdza stan AUTO, brak aktywnego override, stan
alarmów i właściwy slot OTA. Nieudane przywrócenie Wi-Fi lub rollback wymaga
odłączenia obciążeń przed dalszą diagnostyką.
