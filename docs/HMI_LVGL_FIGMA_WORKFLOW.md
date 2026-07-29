# Projektowanie ekranów ESP32-P4: Figma i LVGL

## Odpowiedź krótka

Ekrany mogą być projektowane w Figma i implementowane jako natywny LVGL.
Najbezpieczniejszy proces nie polega jednak na automatycznym generowaniu całego
firmware z makiety. Figma jest źródłem układu, komponentów i wyglądu, natomiast
kod C++ pozostaje źródłem zachowania, ograniczeń bezpieczeństwa oraz zarządzania
pamięcią.

Po podłączeniu integracji Figma można pracować na wskazanym przez właściciela
pliku i dokładnych linkach do ramek. Bez połączenia z Figma wspólnym formatem
pozostają wersjonowane tokeny, specyfikacja ekranów, SVG i zrzuty przechowywane
w repozytorium.

## Źródła prawdy

| Obszar | Źródło prawdy |
|---|---|
| przepływ użytkownika i wygląd | zatwierdzone ramki Figma |
| kolory, odstępy, promienie i typografia | `design/hmi/aquacyd-hmi.tokens.json` |
| zachowanie, MQTT, ACK i bezpieczeństwo | kod C++/LVGL |
| teksty i tłumaczenia | wersjonowany katalog tekstów firmware |
| zgodność wizualna | zrzut z urządzenia porównany z ramką Figma |

Makieta nie może wprowadzać operacji, których nie obsługuje wersjonowany
kontrakt CYD–C6. Kod nie powinien z kolei tworzyć lokalnych kolorów i wymiarów,
jeżeli istnieje dla nich token.

## Ustawienia pliku Figma

### Strony

- `00 Foundations` — kolory, typografia, siatka i zasady dostępności;
- `01 Components` — przyciski, karty, statusy, formularze i dialogi;
- `02 Screens` — kompletne ekrany 1024×600;
- `03 Flows` — przejścia, potwierdzenia i stany awaryjne;
- `04 Handoff` — wyłącznie zatwierdzone warianty do implementacji.

### Nazewnictwo

- ekran: `HMI/Screen/<Nazwa>/<Stan>`;
- komponent: `HMI/Component/<Nazwa>/<Wariant>`;
- ikona: `HMI/Icon/<Nazwa>`;
- zmienna: `hmi/<grupa>/<nazwa>`;
- ramka do implementacji: prefiks `READY/`.

Przykłady:

- `HMI/Screen/Dashboard/Online`;
- `HMI/Screen/Dashboard/ControllerOffline`;
- `HMI/Component/MetricCard/Warning`;
- `HMI/Component/CommandButton/Pending`.

### Ramka bazowa

- rozdzielczość: 1024×600 px;
- orientacja: pozioma;
- siatka: bazowy krok 4 px;
- minimalny obszar dotyku: 48×48 px;
- główne marginesy: 24 px;
- zaokrąglenia kart: 18 px;
- zaokrąglenia przycisków: 14 px;
- tryb podstawowy: ciemny, czytelny w pomieszczeniu i przy stacji ściennej.

## Mapa ekranów

### 1. Start

- logo i wersja firmware;
- inicjalizacja wyświetlacza;
- stan ESP-Hosted;
- łączenie Wi-Fi i MQTT;
- przejście do dashboardu albo diagnostyki offline.

### 2. Dashboard

- temperatura, pH, EC i LDR;
- czytelny status bezpieczny/alarm;
- status CYD, C6, MQTT i Home Assistanta;
- skrót stanu pięciu wyjść;
- najważniejszy aktywny alarm;
- szybkie przejście do sterowania i szczegółów.

Warianty: `Loading`, `Online`, `StaleData`, `ControllerOffline`,
`AlarmWarning`, `AlarmCritical`.

### 3. Sterowanie

- światło główne;
- światło roślinne;
- filtr;
- napowietrzanie;
- tryb karmienia;
- odświeżenie snapshotu.

Każda akcja ma stany `Ready`, `Confirm`, `Pending`, `Accepted`, `Rejected`,
`Conflict` i `Timeout`. Grzałka jest prezentowana tylko informacyjnie.

### 4. Alarmy

- lista aktywnych alarmów;
- opis przyczyny;
- czas ostatniej zmiany;
- wpływ na wyjścia;
- zalecane działanie użytkownika;
- informacja, czy alarm może zostać potwierdzony, czy wymaga usunięcia przyczyny.

### 5. Czujniki

- wartość bieżąca i ważność;
- czas ostatniej próbki;
- surowe dane diagnostyczne;
- kalibracja z wieloetapowym potwierdzeniem;
- historia krótkookresowa pobierana z HA, gdy jest dostępna.

### 6. Harmonogramy i profile

- oświetlenie główne i roślinne;
- profile świt/dzień/noc;
- filtr i napowietrzanie;
- tryb karmienia i serwisowy;
- podsumowanie zmian przed zapisem;
- obsługa konfliktu rewizji.

Ekran zostanie włączony dopiero po wdrożeniu atomowego protokołu konfiguracji.

### 7. System

- jasność i wygaszanie;
- uptime oraz pamięć P4 i CYD;
- wersje firmware;
- RSSI Wi-Fi i ESP-NOW;
- rewizja konfiguracji;
- status brokera;
- informacje o panelu, dotyku i ESP-Hosted;
- bezpieczny restart HMI.

### 8. Provisioning i odzyskiwanie

- brak zapisanych danych;
- błąd Wi-Fi;
- błąd MQTT;
- utrata CYD;
- instrukcja uruchomienia trybu serwisowego BLE;
- przywrócenie ustawień wyłącznie po dodatkowym potwierdzeniu.

## Biblioteka komponentów

| Komponent Figma | Komponent LVGL | Wymagane warianty |
|---|---|---|
| `MetricCard` | `HmiMetricCard` | normal, stale, warning, invalid |
| `StatusChip` | `HmiStatusChip` | online, offline, pending, alarm |
| `OutputState` | `HmiOutputState` | on, off, forced, blocked |
| `CommandButton` | `HmiCommandButton` | ready, disabled, pending |
| `TopBar` | `HmiTopBar` | normal, warning, critical |
| `Navigation` | `HmiNavigation` | active, inactive, alarm badge |
| `ConfirmDialog` | `HmiConfirmDialog` | normal, destructive |
| `Toast` | `HmiToast` | success, warning, error |
| `SettingRow` | `HmiSettingRow` | value, slider, choice, time |
| `EmptyState` | `HmiEmptyState` | no data, offline, unsupported |

Komponenty powinny mieć stały układ obiektów i aktualizować wyłącznie właściwości
LVGL. Tworzenie i usuwanie całego drzewa przy każdej telemetrii jest zabronione.

## Mapowanie tokenów na LVGL

| Token | Zastosowanie LVGL |
|---|---|
| `color.background.canvas` | tło aktywnego ekranu |
| `color.background.surface` | nagłówek, nawigacja, dialog |
| `color.background.card` | karta pomiaru lub ustawienia |
| `color.feedback.success` | połączenie i poprawny ACK |
| `color.feedback.warning` | dane stare albo połączenie częściowe |
| `color.feedback.danger` | aktywny alarm lub odrzucona akcja |
| `radius.card` | `lv_obj_set_style_radius` dla kart |
| `spacing.*` | padding, gap i wyrównanie flex/grid |
| `font.metric` | główna wartość pomiaru |
| `font.body` | opisy i dane diagnostyczne |

W kodzie tokeny powinny zostać odwzorowane jako stałe `constexpr` albo style
LVGL tworzone raz podczas inicjalizacji.

## Ograniczenia embedded

- Nie używać animowanego rozmycia, filtrów ani dużych półprzezroczystych warstw.
- Obrazy należy konwertować do kontrolowanego formatu LVGL i mierzyć ich Flash.
- Font zawiera wyłącznie potrzebne znaki, w tym polskie znaki diakrytyczne.
- W pętli aktualizacji nie wolno wykonywać nieograniczonej alokacji dynamicznej.
- MQTT i callbacki sieciowe nie mogą bezpośrednio modyfikować obiektów LVGL.
- Telemetria trafia do UI przez kolejkę, a LVGL jest obsługiwany w swoim wątku.
- Każdy ekran musi działać przy braku danych, braku MQTT i utracie CYD.
- Sterowanie pozostaje zablokowane do czasu zakończenia poprzedniego ACK.

## Przepływ Figma → LVGL

1. Projektant tworzy lub aktualizuje komponent i jego wszystkie stany.
2. Ramka otrzymuje nazwę `READY/...` oraz link do dokładnego węzła.
3. Pobierane są kontekst projektu, zmienne i zrzut wskazanego węzła.
4. Wartości są porównywane z repozytoryjnymi tokenami.
5. Układ jest tłumaczony na flex/grid oraz komponenty LVGL.
6. Logika korzysta z istniejących kolejek, snapshotów i obsługi ACK.
7. Firmware jest kompilowany, uruchamiany na P4 i fotografowany lub zrzucany.
8. Wynik jest porównywany z Figma dla stanów normalnych i awaryjnych.
9. Dopiero po akceptacji zmiana trafia do głównej gałęzi.

Kod wygenerowany dla HTML albo Reacta przez narzędzia Figma jest traktowany
wyłącznie jako opis układu. Nie jest kopiowany bezpośrednio do firmware.

## Dostęp i współpraca

Aby pracować bezpośrednio na Figma:

1. właściciel tworzy plik w swoim zespole lub Drafts;
2. nadaje dostęp do edycji albo komentarzy;
3. podłącza integrację Figma w Codex;
4. przekazuje link do dokładnej ramki, komponentu lub wariantu;
5. każda implementacja jest wykonywana na podstawie tego linku i zatwierdzonego
   zrzutu.

Jeżeli integracja nie jest podłączona, można nadal projektować i implementować
ekrany przez:

- tokeny JSON w `design/hmi`;
- SVG importowane do Figma;
- zrzuty referencyjne;
- specyfikacje wymiarów i stanów w repozytorium.

## Warunek akceptacji ekranu

Ekran jest gotowy, gdy:

- ma komplet stanów normalnych, oczekiwania, offline i błędu;
- zachowuje minimalne obszary dotyku;
- nie pozwala ominąć lokalnych zabezpieczeń;
- nie aktualizuje LVGL spoza wątku UI;
- nie zwiększa użycia pamięci poza ustalony budżet;
- przechodzi kompilację ESP-IDF z `-Wall -Wextra -Werror`;
- zrzut z urządzenia odpowiada zatwierdzonej ramce Figma;
- tekst pozostaje czytelny po polsku i nie jest ucinany.
