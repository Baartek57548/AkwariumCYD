# AquaCYD Control 4.0.0

Produkcyjne wydanie mobilnego centrum dowodzenia dla sterownika AquaCYD.

## Najważniejsze zmiany

- pięć obszarów pracy: Centrum, Steruj, Auto, Historia i System,
- alarmy, kondycja połączenia, sensory, fizyczne stany wyjść i najbliższe zdarzenie na jednym pulpicie,
- jawne tryby `AUTO`, wymuszone `ON` i wymuszone `OFF`,
- blokowanie poleceń przy braku połączenia albo nieaktualnej telemetrii,
- automatyczne ponawianie połączenia i przywracanie ostatniego sterownika Wi‑Fi,
- adaptacyjny interfejs Material 3 dla telefonu, tabletu i dużego tekstu,
- sesja administratora wygasająca po pięciu minutach i czyszczona po przejściu aplikacji w tło,
- bezpieczny aktualizator aplikacji uruchamiany po zgodzie użytkownika.

## Instalacja

1. Pobierz `AquaCYD-Control-4.0.0-current.apk`.
2. Otwórz plik na urządzeniu z Androidem 7.0 lub nowszym.
3. Przy pierwszej instalacji zezwól Androidowi na instalowanie z tego źródła.
4. Potwierdź instalację w systemowym instalatorze.

Pakiet aktualizuje wcześniejszą instalację wariantu `current`, ponieważ zachowuje identyfikator aplikacji i certyfikat podpisu.

## Dane artefaktu

- pakiet: `pl.cydakwarium.cyd_aquarium_mobile`,
- `versionName`: `4.0.0`,
- `versionCode`: `14`,
- rozmiar: `58 917 746` bajtów,
- SHA‑256 APK: `6400127dd73e86cc0a14101a368203979b01f6e5fe7c3b9a3044db6c8657b7c7`,
- SHA‑256 certyfikatu: `394b82b7779b9b07375237fb5233082d416c75a06dfc3a232955861176880e9e`,
- minimalny Android: API 24,
- docelowy Android: API 36.

## Ważne ograniczenia firmware

- wymuszenie `ON/OFF` zmienia zapisany tryb harmonogramu; bieżący firmware nie obsługuje czasowego override,
- bieżący firmware nie stosuje zapisanego profilu przekaźników, dlatego jego edytor jest zablokowany,
- REST działa przez lokalny HTTP; sterownika nie należy wystawiać bezpośrednio do Internetu,
- historia, OTA i pełna diagnostyka wymagają Wi‑Fi albo protokołu BLE v2.
