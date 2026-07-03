# Protokół cydAkwarium BLE v1

## Usługa GATT

| Element | UUID | Właściwości |
|---|---|---|
| Usługa | `7c4a0001-6e8d-4f84-9f3f-2c3a0a0c0001` | — |
| Komendy | `7c4a0002-6e8d-4f84-9f3f-2c3a0a0c0001` | Write, Write Without Response |
| Zdarzenia | `7c4a0003-6e8d-4f84-9f3f-2c3a0a0c0001` | Read, Notify |
| Informacje | `7c4a0004-6e8d-4f84-9f3f-2c3a0a0c0001` | Read |

Kodowanie tekstu to UTF-8. Komenda jest pojedynczym obiektem JSON o maksymalnym
rozmiarze 160 bajtów. Każda komenda zawiera całkowity identyfikator `id` z
zakresu 1–65535 i pole `op`.

## Fragmentacja zdarzeń

Każde powiadomienie charakterystyki zdarzeń zaczyna się czterobajtowym
nagłówkiem:

| Bajty | Znaczenie |
|---|---|
| 0–1 | identyfikator wiadomości, little-endian |
| 2 | indeks części, liczony od zera |
| 3 | łączna liczba części |
| 4–159 | fragment danych UTF-8 |

Odbiornik składa części według identyfikatora i indeksu. Niekompletna wiadomość
jest odrzucana po 10 sekundach. Maksymalna liczba części wynosi 64.

## Komendy

Odczyt bieżącego statusu:

```json
{"id":1,"op":"status"}
```

Zmiana stanu modułu:

```json
{"id":2,"op":"set","target":"light","state":true,"pin":"1234"}
```

Dozwolone cele: `light`, `plant`, `filter`, `heater`, `aeration`.

Uruchomienie karmienia:

```json
{"id":3,"op":"feed","pin":"1234"}
```

## Odpowiedzi

```json
{"type":"response","id":2,"ok":true,"code":"ok","message":"Stan modulu zapisany."}
```

Nieudana autoryzacja zwraca `code: "pin_invalid"`. Aplikacja usuwa wtedy PIN z
pamięci sesji. PIN nie jest zapisywany w trwałej pamięci aplikacji.

## Telemetria

Status ma `type: "status"` oraz `v: 1`. Zawiera temperaturę, pH, EC, LDR,
alarmy, poziom wody, wyciek, wolny heap, czas pracy i bieżące stany pięciu
modułów. Firmware wysyła status po komendzie `status`, po zmianie wyjścia oraz
cyklicznie co dwie sekundy.
