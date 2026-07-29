# AquaCYD Control 4.0.1

Produkcyjna aktualizacja mobilnego centrum dowodzenia AquaCYD w architekturze offline-first.

## Najważniejsze zmiany

- cały interfejs i wszystkie pięć obszarów aplikacji są dostępne od pierwszej klatki, także bez sterownika,
- ostatni potwierdzony stan, nastawy i historia są dostępne lokalnie w jednoznacznym trybie tylko do odczytu,
- centrum połączeń w prawym górnym rogu pozwala wybrać Wi‑Fi, Bluetooth albo świadomy tryb offline,
- zapamiętany sterownik Wi‑Fi jest łączony w tle, z kontrolowanym auto-reconnectem po odzyskaniu sieci,
- podczas utraty połączenia ostatnie dane pozostają widoczne, ale polecenia są blokowane do czasu świeżej synchronizacji,
- pierwszy start bez snapshotu pokazuje neutralne puste stany zamiast fikcyjnych pomiarów i ustawień,
- lokalny snapshot ma limit rozmiaru i głębokości oraz nie zapisuje PIN-ów, haseł, tokenów ani sekretów,
- zmiana transportu i cykl życia aplikacji poprawnie zatrzymują timery, heartbeat i poprzednią sesję.

## Instalacja

1. Pobierz `AquaCYD-Control-4.0.1-current.apk`.
2. Otwórz plik na urządzeniu z Androidem 7.0 lub nowszym.
3. Przy pierwszej instalacji zezwól Androidowi na instalowanie z tego źródła.
4. Potwierdź instalację lub aktualizację w systemowym instalatorze.

Pakiet aktualizuje wcześniejszą instalację wariantu `current`, ponieważ zachowuje identyfikator aplikacji i certyfikat podpisu.

## Dane artefaktu

- pakiet: `pl.cydakwarium.cyd_aquarium_mobile`,
- `versionName`: `4.0.1`,
- `versionCode`: `15`,
- rozmiar: `59 934 407` bajtów,
- SHA‑256 APK: `591f2c3008302291cc8a467aa8e99b1d32f877e495774e3152cdca57ec3991a0`,
- SHA‑256 certyfikatu: `394b82b7779b9b07375237fb5233082d416c75a06dfc3a232955861176880e9e`,
- minimalny Android: API 24,
- docelowy Android: API 36.

## Ważne ograniczenia firmware

- wymuszenie `ON/OFF` zmienia zapisany tryb harmonogramu; bieżący firmware nie obsługuje czasowego override,
- bieżący firmware nie stosuje zapisanego profilu przekaźników, dlatego jego edytor pozostaje zablokowany,
- REST działa przez lokalny HTTP; sterownika nie należy wystawiać bezpośrednio do Internetu,
- historia, OTA i pełna diagnostyka wymagają Wi‑Fi albo protokołu BLE v2.
