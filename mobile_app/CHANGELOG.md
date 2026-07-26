# Historia zmian

Wersjonowanie aplikacji jest zgodne z [Semantic Versioning](https://semver.org/).

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
