# Historia zmian

Wersjonowanie aplikacji jest zgodne z [Semantic Versioning](https://semver.org/).

## 5.1.0 — 2026-07-28

### Bezpieczeństwo połączenia

- Bluetooth wymaga teraz LE Secure Connections, bondingu, ochrony MITM i klucza
  128-bit; aplikacja nie wysyła poleceń przed zakończeniem bezpiecznego
  parowania,
- produkcyjny CYD generuje własny sześciocyfrowy PIN; aplikacja nie zakłada już
  publicznego kodu fabrycznego,
- na Androidzie dodano natywny przepływ bondingu z timeoutem, obsługą cofniętych
  uprawnień i pełnym sprzątaniem zasobów,
- firmware pokazuje jednorazowy kod parowania na ekranie CYD i pozwala usunąć
  zapisane urządzenia z poziomu narzędzi serwisowych.

### Bezpieczne aktualizacje

- aplikacja przyjmuje wyłącznie podpisane pakiety `.aqfw`, waliduje ich wariant
  ekranu, produkt, wersję, `securityVersion`, identyfikator klucza, rozmiar i
  SHA-256 przed wysłaniem,
- upload używa krótkotrwałego tokenu w nagłówku `X-AquaCYD-Session`; PIN nie jest
  już umieszczany w adresie URL,
- firmware weryfikuje RSA-3072/PSS, podpis obrazu Secure Boot v2 i anti-downgrade
  przed aktywowaniem partycji aktualizacji, a po restarcie wymaga potwierdzenia
  zdrowia obu rdzeni i interfejsu,
- po połączeniu aplikacja automatycznie sprawdza wydania `firmware-v*`, dobiera
  paczkę do ILI9341/ST7789 i — dopiero po zgodzie użytkownika — pobiera,
  ponownie waliduje oraz instaluje aktualizację.

### Dystrybucja

- wersja aplikacji: `5.1.0+18`,
- tag aplikacji: `mobile-v5.1.0`,
- tag firmware: `firmware-v5.1.0`,
- produkcyjny asset Androida: `AquaCYD-Control-5.1.0-current.apk`.

## 5.0.0 — 2026-07-26

### Nowe

- dodano centrum alarmów z cyklem `nowy → potwierdzony → rozwiązany`, deduplikacją, histerezą, cooldownem i powiadomieniami lokalnymi,
- dodano trwałą historię SQLite pomiarów, poleceń i zdarzeń oraz lokalne przypomnienia serwisowe dostępne bez sterownika,
- dodano opcjonalną synchronizację w tle przez zapisany sterownik w lokalnej sieci Wi‑Fi, z kontrolą kompletności danych i wykładniczym backoffem,
- dodano przewodnik pierwszego uruchomienia oraz prosty i ekspercki poziom interfejsu,
- dodano protokół v2 z krótkotrwałą sesją administratora, unikalnym `commandId` i idempotentnym wykonaniem poleceń,
- dodano bezpiecznie wygasające override, tryb karmienia i tryb serwisowy,
- dodano niezależne sterowanie świetlówką przednią i tylną Aquael w trybach `DAY`, `DAYBREAK` i `NIGHT`.

### Zmienione

- aplikacja pozostaje kompletnym centrum dowodzenia offline i automatycznie wraca do zapisanego sterownika Wi‑Fi,
- informacje techniczne, diagnostyka i narzędzia administracyjne są ukryte w trybie eksperckim,
- sterowanie Aquael wykorzystuje nieblokujące sekwencje zasilania: impuls OFF 1 s oraz kalibrację DAY po 6 s,
- nazwy lamp są spójne w aplikacji, firmware i panelu WWW: „Świetlówka przednia” oraz „Świetlówka tylna”.

### Bezpieczeństwo i jakość

- alarmy powstają wyłącznie z kompletnych, aktualnych odczytów live; cache offline nigdy nie generuje nowego alarmu,
- sekret opcjonalnego webhooka jest przechowywany w systemowym magazynie, a synchronizacja w tle akceptuje wyłącznie adres sterownika lokalnego,
- dodano testy HIL kontraktu v2, timeoutów, idempotencji, rollbacku OTA i obu lamp Aquael,
- pełny pakiet Flutter, Android, web, native firmware i oba warianty wyświetlacza jest weryfikowany w GitHub Actions.

### Znane ograniczenia

- firmware 5.0.0 jawnie raportuje brak szyfrowania łącza BLE, bondingu i ochrony MITM; Bluetooth wymaga fizycznie kontrolowanego dostępu do czasu wdrożenia bezpiecznego parowania i zaliczenia HIL na realnym telefonie,
- podpis obrazu firmware jest publikowany i sprawdzany w CI, ale ESP32 nie weryfikuje go jeszcze względem provisionowanego klucza; zdalne automatyczne OTA firmware pozostaje wyłączone.

### Dystrybucja

- wersja aplikacji: `5.0.0+17`,
- tag wydania: `mobile-v5.0.0`,
- produkcyjny asset Androida: `AquaCYD-Control-5.0.0-current.apk`.

## 4.1.0 — 2026-07-26

### Nowe

- wprowadzono progresywne ujawnianie informacji: najważniejsze dane są widoczne od razu, a szczegóły techniczne rozwija się na żądanie,
- dodano zwięzły przycisk połączenia w prawym górnym rogu oraz zwijany panel RSSI, ping i czasu synchronizacji,
- dodano dedykowane testy zachowania zwijanych sekcji sterowania, automatyki, historii i narzędzi serwisowych.

### Zmienione

- uproszczono nawigację do obszarów Start, Steruj, Auto, Historia i Więcej,
- ekran Start pokazuje najpierw bezpieczeństwo, alarmy, temperaturę i najbliższe zdarzenie; pozostałe czujniki oraz stany urządzeń są zwinięte,
- karmnik jest pierwszą akcją ekranu Steruj, a tryby AUTO / ON / OFF są dostępne po rozwinięciu konkretnego urządzenia,
- formularze automatyki, dane źródłowe wykresów, eksport, informacje o sterowniku i narzędzia serwisowe są domyślnie ukryte,
- dziennik zdarzeń nie otwiera samoczynnie dialogu PIN i jest pobierany dopiero po świadomej akcji użytkownika,
- tryb offline rozróżnia pierwszy start bez danych od podglądu ostatniego lokalnego snapshotu.

### Bezpieczeństwo i odporność

- zachowano szkice formularzy i wykrywanie konfliktów podczas zwijania sekcji,
- alarmy krytyczne pozostają zawsze widoczne i wyprzedzają treści drugorzędne,
- polecenia nadal są blokowane bez świeżej telemetrii, a ukrycie elementu nie zmienia jego walidacji ani uprawnień.

### Dystrybucja

- wersja aplikacji: `4.1.0+16`,
- tag wydania: `mobile-v4.1.0`,
- produkcyjny asset Androida: `AquaCYD-Control-4.1.0-current.apk`.

## 4.0.1 — 2026-07-26

### Nowe

- aplikacja uruchamia od razu kompletne centrum dowodzenia, również bez połączenia ze sterownikiem,
- ostatni potwierdzony stan urządzenia jest zapisywany lokalnie i dostępny we wszystkich pięciu sekcjach,
- przycisk w prawym górnym rogu otwiera wspólne centrum połączeń Wi‑Fi, Bluetooth i trybu offline,
- zapamiętany sterownik Wi‑Fi jest łączony w tle, a sesja automatycznie ponawia próbę po odzyskaniu sieci.

### Zmienione

- harmonogramy, automatyka i ustawienia pozostają dostępne offline jako jednoznaczny tryb tylko do odczytu,
- brak pierwszego snapshotu nie jest uzupełniany danymi symulatora ani fikcyjnymi pomiarami,
- podczas reconnectu interfejs zachowuje ostatni stan i blokuje polecenia do chwili otrzymania świeżej telemetrii,
- lokalny eksport CSV działa również dla zapisanej historii offline.

### Bezpieczeństwo i odporność

- snapshot ma wersjonowany format, limit rozmiaru i głębokości oraz usuwa PIN-y, hasła, tokeny i sekrety,
- zapis jest ograniczony czasowo, deduplikowany po znaczniku synchronizacji i wykonywany wyłącznie po udanym odczycie live,
- zmiana transportu zwalnia poprzednią sesję, anuluje jej timery i nie pozwala staremu połączeniu nadpisać nowszego stanu.

### Dystrybucja

- wersja aplikacji: `4.0.1+15`,
- tag wydania: `mobile-v4.0.1`,
- produkcyjny asset Androida: `AquaCYD-Control-4.0.1-current.apk`.

## 4.0.0 — 2026-07-26

### Nowe

- przebudowano aplikację w pięciosekcyjne centrum dowodzenia: Centrum, Steruj, Auto, Historia i System,
- dodano typowany model bezpieczeństwa, sensorów, wyjść, alarmów i najbliższego zdarzenia harmonogramu,
- dodano pulpit priorytetyzujący temperaturę, krytyczne alarmy, kondycję sieci i stan automatyki,
- dodano rozróżnienie stanu fizycznego wyjścia od trybu `AUTO`, wymuszonego `WŁ.` i wymuszonego `WYŁ.`,
- dodano automatyczne przywracanie ostatniego sterownika Wi‑Fi oraz możliwość jego zapomnienia,
- wprowadzono spójny design system Material 3 dla jasnego i ciemnego motywu.

### Zmienione

- uporządkowano ustawienia, automatykę, historię i diagnostykę w centra zadaniowe zamiast zbioru ekranów administracyjnych,
- operacje sterujące są dostępne tylko przy aktywnym połączeniu i aktualnej telemetrii,
- sterowanie grzałką opisuje tryb termostatu zamiast sugerować bezpośrednie przełączanie przekaźnika,
- wymuszenie stanu wyjścia wymaga potwierdzenia, ponieważ zmienia zapisany tryb harmonogramu,
- przywrócenie `AUTO` wysyła tylko tryb wskazanego kanału i nie nadpisuje równoległych zmian pozostałej automatyki,
- formularze są tworzone dopiero po pierwszej telemetrii, zachowują lokalny szkic i ostrzegają o nowszej konfiguracji sterownika,
- zoptymalizowano odświeżanie stanu i ograniczono zbędne pobieranie historii po komendach,
- produkcyjny ekran połączenia ukrywa symulator DEV i starszy panel WebView.

### Bezpieczeństwo i odporność

- odpowiedź `pin_invalid` jest traktowana jako błąd autoryzacji i unieważnia sesję administracyjną,
- PIN administratora jest usuwany po przejściu aplikacji w tło oraz po pięciu minutach bezczynności,
- jeden mutex obejmuje cały przebieg autoryzacja–potwierdzenie–komenda i zapobiega nakładaniu się dialogów,
- zapis ustawień Wi‑Fi wymaga pełnego, poprawnego hasła, aby nie wyczyścić go po stronie bieżącego firmware,
- zachowano serializację komend, kontrolę świeżości danych, automatyczne ponawianie połączenia i bezpieczne parsowanie odpowiedzi,
- aktualizator akceptuje wyłącznie stabilne wydanie GitHub z właściwą nazwą assetu, SHA‑256, pakietem, rosnącą wersją i zgodnym certyfikatem.

### Dystrybucja

- wersja aplikacji: `4.0.0+14`,
- tag wydania: `mobile-v4.0.0`,
- produkcyjny asset Androida: `AquaCYD-Control-4.0.0-current.apk`,
- kanał aktualizacji pozostaje dostępny wyłącznie dla podpisanego wariantu `current`.

### Znane ograniczenia

- firmware udostępnia polling REST zamiast WebSocket, SSE lub powiadomień push,
- wymuszenie wyjścia zmienia tryb harmonogramu i nie ma automatycznego czasu wygaśnięcia,
- starszy protokół BLE v1 nie obsługuje historii, archiwów SD, OTA ani pełnej diagnostyki,
- bieżący firmware nie stosuje zapisanego profilu przekaźników, dlatego jego edytor pozostaje zablokowany.
