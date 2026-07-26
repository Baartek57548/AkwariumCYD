# Protokół cydAkwarium BLE v2

Firmware 5.0.0 udostępnia protokół v2 oraz zachowuje operacje v1 dla starszych
wersji aplikacji. Klient powinien najpierw odczytać informacje urządzenia i
wywołać `capabilities`; brak tej operacji oznacza firmware v1.

## Usługa GATT

| Element | UUID | Właściwości |
|---|---|---|
| Usługa | `7c4a0001-6e8d-4f84-9f3f-2c3a0a0c0001` | — |
| Komendy | `7c4a0002-6e8d-4f84-9f3f-2c3a0a0c0001` | Write, Write Without Response |
| Zdarzenia | `7c4a0003-6e8d-4f84-9f3f-2c3a0a0c0001` | Read, Notify |
| Informacje | `7c4a0004-6e8d-4f84-9f3f-2c3a0a0c0001` | Read |

Charakterystyka informacji zwraca wersję firmware, `protocol: 2`,
`apiVersions: [1, 2]`, limit komendy 4096 bajtów i maksymalnie 32 fragmenty.
Kodowanie tekstu to UTF-8. Każda komenda jest obiektem JSON z całkowitym `id`
z zakresu 1–65535 i polem `op`.

## Fragmentacja

Wiadomość mieszcząca się w pojedynczym zapisie może zostać wysłana jako zwykły
JSON. Większe komendy i wszystkie fragmentowane zdarzenia używają
czterobajtowego nagłówka:

| Bajty | Znaczenie |
|---|---|
| 0–1 | identyfikator wiadomości, little-endian |
| 2 | indeks części, liczony od zera |
| 3 | łączna liczba części |
| 4–159 | fragment danych UTF-8, maksymalnie 156 bajtów |

Fragmenty muszą nadejść kolejno, z tym samym identyfikatorem i liczbą części.
Niekompletna wiadomość jest odrzucana po 10 sekundach. Komenda może mieć
maksymalnie 32 części, a zdarzenie 64 części.

## Wykrywanie możliwości i pełny status

```json
{"id":1,"v":2,"op":"capabilities"}
```

Sterownik publikuje zdarzenie `type: "capabilities"` z manifestem funkcji,
limitów, akcji i dwóch lamp Aquael, a następnie odpowiedź kończącą komendę.

```json
{"id":2,"op":"full_status","history":false}
```

Pełna telemetria przychodzi jako `type: "full_status"` z obiektem `data`.
Firmware wysyła ją również po udanej akcji v2. Aplikacja waliduje strukturę i
nie tworzy alarmów z niepełnego albo zapisanego offline payloadu.

## Sesja administratora v2

```json
{"id":3,"v":2,"op":"auth","pin":"1234"}
```

Udane logowanie publikuje zdarzenie:

```json
{
  "type": "auth",
  "v": 2,
  "id": 3,
  "ok": true,
  "code": "authenticated",
  "data": {
    "sessionToken": "0123456789abcdef0123456789abcdef",
    "expiresInSec": 300
  }
}
```

Token ma 128 bitów, istnieje tylko w RAM i wygasa po pięciu minutach. Pięć
błędnych prób PIN blokuje logowanie na 60 sekund. Zdarzenie `auth` niesie token,
a osobna odpowiedź `type: "response"` kończy oczekujące wywołanie transportu.

## Idempotentne akcje v2

Każda zmieniająca stan akcja v2 wymaga nieużytego `commandId` długości 8–48
znaków oraz ważnego tokenu:

```json
{
  "id": 4,
  "v": 2,
  "op": "action",
  "name": "set_light_profile",
  "commandId": "mobile-7f4a91c2",
  "token": "0123456789abcdef0123456789abcdef",
  "args": {
    "target": "front",
    "profile": "night"
  }
}
```

Akcje v2:

- `set_light_profile` — `target: front|rear`, `profile: day|daybreak|night`;
- `set_timed_override` — `target`, `state`, `durationSec`;
- `clear_timed_override` — `target`;
- `start_feeding_mode` — `durationSec`, opcjonalnie `dispense`;
- `stop_feeding_mode`;
- `start_service_mode` — `durationSec`;
- `stop_service_mode`.

Ponowienie identycznego `commandId` i payloadu zwraca zapisany wynik bez
powtórzenia operacji. Użycie tego samego identyfikatora z innymi argumentami
zwraca `command_id_conflict`.

## Zgodność v1

Odczyt uproszczonego statusu:

```json
{"id":10,"op":"status"}
```

Zmiana stanu modułu:

```json
{"id":11,"op":"set","target":"light1","state":true,"pin":"1234"}
```

Dozwolone cele obejmują `light1`, `light2`, `filter`, `heater` i `aeration`.
Aliasy `light` i `plant` pozostają obsługiwane.

Karmienie:

```json
{"id":12,"op":"feed","pin":"1234"}
```

Odpowiedź v1 ma postać:

```json
{"type":"response","id":11,"ok":true,"code":"ok","message":"Stan modułu zapisany."}
```

Aplikacja używa operacji v1 wyłącznie dla starszych komend przekaźników lub
firmware bez manifestu v2. Nieudana autoryzacja zwraca `pin_invalid`; aplikacja
usuwa wtedy PIN i token z pamięci sesji.

## Bezpieczeństwo transportu

Tokeny, PIN-y i payloady nie mogą trafiać do logów ani trwałych snapshotów.
BLE nie zastępuje kontroli fizycznego dostępu. Produkcyjne wdrożenie powinno
wymuszać szyfrowanie łącza, bonding i potwierdzenie nowego telefonu na ekranie
sterownika; do czasu pełnego wdrożenia tej procedury nie należy pozostawiać
reklamowania BLE stale aktywnego w miejscu dostępnym publicznie.
