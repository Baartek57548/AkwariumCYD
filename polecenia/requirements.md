# Requirements Document

## Introduction

Niniejszy dokument opisuje wymagania dla rozwoju istniejącego sterownika akwariowego opartego na module ESP32 "Cheap Yellow Display" (CYD). Celem jest przekształcenie obecnej, jednowątkowej aplikacji demonstracyjnej (symulacja czasu, temperatury i pH w `loop()`) w profesjonalną platformę sterowania akwarium o wysokiej jakości UX/UI oraz rozbudowanej, ale luźno powiązanej architekturze.

Rozwój prowadzony jest na bazie istniejącego kodu (PlatformIO, framework Arduino, LVGL 8.3 + LovyanGFX). Istniejące funkcje (m.in. konfiguracja w NVS walidowana CRC32, harmonogramy dla Światła/Światła roślinnego/Filtra/Napowietrzacza, sterowanie grzałką z histerezą, harmonogram karmnika, wykresy z 32-punktowymi buforami cyklicznymi, panele Wi-Fi i OTA, dźwięk i godziny ciszy, auto-motyw LDR, diagnostyka oraz wstępny tryb serwisowy) muszą zostać zachowane i nie mogą być usuwane ani przenoszone bez zgody użytkownika.

Zakres obejmuje sprzęt podstawowy (dwie świetlówki Aquael z 3 trybami oświetlenia, filtr powietrzny/kanistrowy, czujnik temperatury) oraz przygotowanie punktów rozszerzeń (hooków) dla sprzętu opcjonalnego: napowietrzacz 230 V, karmnik (na razie szkielet), sonda pH, sterowanie CO2, sonda EC, czujnik poziomu wody, czujnik jakości/przepływu wody, czujnik wycieku oraz grzałka.

Kluczowe założenia architektoniczne to: podział pracy na dwa rdzenie ESP32 z użyciem FreeRTOS (renderowanie UI na jednym rdzeniu, odczyt czujników i logika sterowania na drugim), komunikacja między warstwą sprzętową a UI wyłącznie asynchronicznie (zdarzenia/kolejki, luźne powiązanie), wieloetapowa kalibracja sond z poziomu UI, rejestrowanie danych historycznych w buforach cyklicznych, wsparcie OTA z diagnostyką, ochrona PIN-em akcji krytycznych, scentralizowany plik konfiguracji wrażliwych danych oraz dedykowany tryb serwisowy z automatycznym wygaśnięciem.

## Glossary

Słownik pojęć domenowych:


- **Platforma**: Cały system oprogramowania sterownika akwariowego działający na ESP32 CYD; nadrzędna nazwa systemu.
- **Warstwa_HAL**: Warstwa abstrakcji sprzętu (Hardware Abstraction Layer) odpowiedzialna za bezpośredni dostęp do GPIO, ADC, przekaźników i magistrali wyświetlacza/dotyku.
- **Interfejs_Uzytkownika**: Warstwa LVGL renderująca ekrany i obsługująca dotyk (UI).
- **Menedzer_Zdarzen**: Komponent pośredniczący przekazujący zdarzenia i dane między rdzeniami w sposób asynchroniczny i bezpieczny wątkowo (kolejki/zdarzenia FreeRTOS).
- **Kontroler_Urzadzen**: Komponent realizujący wspólną abstrakcję sterowania urządzeniami przekaźnikowymi (włącz/wyłącz + harmonogram).
- **Sterownik_Oswietlenia**: Komponent sterujący dwiema świetlówkami Aquael w trzech trybach oświetlenia, w tym poprzez operację Synchronizacja_swiatel (synchronizeLights) wymuszającą identyczny, deterministyczny tryb obu lamp.
- **Synchronizacja_swiatel (synchronizeLights)**: Operacja Sterownik_Oswietlenia zapewniająca, że obie świetlówki Aquael kończą w tym samym, znanym trybie niezależnie od wcześniejszego, nieznanego stanu. Operacja odcina zasilanie obu przekaźników lamp na 10 sekund (każdy cykl zasilania resetuje sterownik Aquael do domyślnego pierwszego trybu), a następnie wysyła do obu lamp jednocześnie odpowiednią liczbę impulsów przełączających (advance-pulses), które kolejno przełączają tryby (tryb 1 → tryb 2 → tryb 3, cyklicznie), aż do osiągnięcia żądanego trybu docelowego.
- **Regulator_Grzalki**: Komponent sterujący grzałką w trybie progowym z histerezą.
- **Karmnik**: Komponent obsługi karmnika (na obecnym etapie szkielet z pinem wyzwalającym karmienie i pinem odczytu pozycji/sprzężenia zwrotnego).
- **Warstwa_Czujnikow**: Komponent odczytujący i normalizujący wartości z czujników (temperatura, pH, EC, poziom wody, przepływ, wyciek, CO2, LDR).
- **Sonda_pH**: Analogowa sonda pomiaru odczynu pH wymagająca kalibracji wielopunktowej.
- **Sonda_EC**: Analogowa sonda pomiaru przewodności elektrolitycznej (EC) wymagająca kalibracji wielopunktowej.
- **Kreator_Kalibracji**: Komponent UI prowadzący użytkownika przez wieloetapową kalibrację sond i zapisujący punkty referencyjne.
- **Sterownik_CO2**: Komponent sterujący elektrozaworem/przekaźnikiem CO2 (wg harmonogramu lub powiązany z pH).
- **Rejestrator_Danych**: Komponent zapisujący próbki pomiarów w buforach cyklicznych w pamięci RAM dla potrzeb wykresów.
- **Modul_Wykresow**: Komponent UI rysujący wykresy LVGL na podstawie danych z Rejestrator_Danych.
- **Menedzer_OTA**: Komponent obsługujący aktualizacje oprogramowania przez sieć (Over-The-Air).
- **Modul_Diagnostyki**: Komponent zbierający i udostępniający dane diagnostyczne (wolny heap, czas pracy, przyczyna resetu, licznik uruchomień, temperatura CPU).
- **Straznik_PIN**: Komponent kontroli dostępu wymagający wprowadzenia kodu PIN przed akcjami krytycznymi.
- **Modul_Konfiguracji**: Scentralizowany plik/moduł konfiguracji przechowujący przypisania pinów, hasła i dane wrażliwe.
- **Tryb_Serwisowy**: Dedykowany tryb wyłączający wybrane urządzenia peryferyjne na czas obsługi, z automatycznym wygaśnięciem.
- **Aquael**: Producent świetlówek akwariowych zastosowanych w urządzeniu.
- **Napowietrzacz (Aerator)**: Urządzenie 230 V napowietrzające wodę, sterowane przekaźnikowo (włącz/wyłącz + harmonogram), analogicznie do filtra.
- **Filtr kanistrowy (Canister filter)**: Zewnętrzny filtr ciśnieniowy z wężem wylotowym, na którym może być zamontowany czujnik przepływu/jakości wody.
- **Histereza (Hysteresis)**: Margines wokół wartości docelowej zapobiegający częstemu przełączaniu urządzenia (np. grzałki) wokół punktu nastawy.
- **EARS**: Easy Approach to Requirements Syntax — zestaw szablonów zdań dla wymagań.
- **EC**: Electrical Conductivity — przewodność elektrolityczna wody, miara zasolenia/mineralizacji.
- **LDR**: Light Dependent Resistor — fotorezystor, czujnik natężenia światła otoczenia (GPIO34).
- **Bufor cykliczny (Circular buffer)**: Bufor o stałym rozmiarze, w którym najstarsze próbki są nadpisywane przez najnowsze (kolejka FIFO o stałej pojemności).
- **NVS**: Non-Volatile Storage — trwała pamięć ESP32 wykorzystywana do zapisu konfiguracji.
- **CRC32**: Suma kontrolna używana do walidacji integralności zapisanej konfiguracji.
- **OTA**: Over-The-Air — zdalna aktualizacja oprogramowania przez sieć.
- **Tryb deweloperski (Dev mode)**: Tryb, w którym Platforma odczytuje rzeczywiste wartości ADC zamiast danych symulowanych.
- **Akcja krytyczna**: Operacja zmieniająca harmonogramy, nastawy sterowania lub konfigurację systemu, wymagająca autoryzacji PIN-em.
- **Degradacja łagodna (Graceful degradation)**: Zachowanie, w którym brak opcjonalnego sprzętu nie powoduje błędu, a powiązane funkcje są dezaktywowane lub ukrywane.

## Requirements

### Requirement 1: Architektura dwurdzeniowa FreeRTOS

**User Story:** Jako użytkownik akwarium, chcę aby interfejs pozostawał płynny podczas odczytu czujników, aby obsługa ekranu dotykowego nie zacinała się w trakcie pracy systemu.

#### Acceptance Criteria

1. THE Platforma SHALL uruchamiać renderowanie Interfejs_Uzytkownika (LVGL) jako zadanie FreeRTOS przypięte do jednego rdzenia ESP32.
2. THE Platforma SHALL uruchamiać odczyt Warstwa_Czujnikow oraz logikę Kontroler_Urzadzen jako zadanie FreeRTOS przypięte do drugiego rdzenia ESP32.
3. WHILE odczyt czujników trwa na rdzeniu logiki, THE Interfejs_Uzytkownika SHALL kontynuować renderowanie i obsługę dotyku bez przerwania.
4. WHEN zadanie obsługi Interfejs_Uzytkownika wykonuje pętlę, THE Interfejs_Uzytkownika SHALL wywoływać `lv_timer_handler` w odstępach nie większych niż 10 ms.
5. IF zadanie FreeRTOS nie zwolni czasu procesora w wymaganym oknie, THEN THE Platforma SHALL oddać sterowanie planiście systemu, aby uniknąć zadziałania watchdoga.
6. THE Platforma SHALL chronić każde dane współdzielone między rdzeniami mechanizmem synchronizacji bezpiecznym wątkowo (kolejka, mutex lub semafor FreeRTOS).

### Requirement 2: Komunikacja zdarzeniowa i luźne powiązanie

**User Story:** Jako deweloper, chcę aby warstwa sprzętowa i interfejs komunikowały się asynchronicznie przez zdarzenia, aby komponenty były luźno powiązane i łatwe w rozbudowie.

#### Acceptance Criteria

1. WHEN Warstwa_HAL uzyska nową próbkę pomiaru, THE Warstwa_HAL SHALL przekazać próbkę do Interfejs_Uzytkownika za pośrednictwem Menedzer_Zdarzen.
2. THE Warstwa_HAL SHALL przekazywać dane do Interfejs_Uzytkownika wyłącznie przez Menedzer_Zdarzen, bez bezpośredniego wywoływania funkcji Interfejs_Uzytkownika.
3. WHEN Interfejs_Uzytkownika rejestruje żądanie zmiany stanu urządzenia, THE Interfejs_Uzytkownika SHALL publikować zdarzenie polecenia do Menedzer_Zdarzen zamiast bezpośrednio sterować Warstwa_HAL.
4. THE Menedzer_Zdarzen SHALL przekazywać zdarzenia między rdzeniami w sposób bezpieczny wątkowo z użyciem kolejki FreeRTOS.
5. IF kolejka zdarzeń jest pełna w momencie publikacji, THEN THE Menedzer_Zdarzen SHALL odrzucić najstarszą nieprzetworzoną próbkę danych pomiarowych i zarejestrować zdarzenie przepełnienia w Modul_Diagnostyki.
6. THE Menedzer_Zdarzen SHALL umożliwiać rejestrację wielu odbiorców (callbacków) dla danego typu zdarzenia.

### Requirement 3: Scentralizowany moduł konfiguracji i danych wrażliwych

**User Story:** Jako deweloper, chcę aby wszystkie hasła, dane wrażliwe i przypisania pinów znajdowały się w jednym miejscu, aby konfiguracja sprzętu i bezpieczeństwo były łatwe do zarządzania.

#### Acceptance Criteria

1. THE Modul_Konfiguracji SHALL definiować w jednym pliku wszystkie przypisania pinów GPIO wykorzystywane przez Platforma.
2. THE Modul_Konfiguracji SHALL definiować w jednym pliku dane uwierzytelniające sieci Wi-Fi, dane OTA oraz domyślny kod PIN.
3. THE Platforma SHALL pobierać przypisania pinów i dane wrażliwe wyłącznie z Modul_Konfiguracji.
4. WHERE plik z danymi wrażliwymi jest oddzielony od kodu współdzielonego w repozytorium, THE Modul_Konfiguracji SHALL udostępniać plik szablonu (przykładowy) z wartościami zastępczymi.
5. THE Modul_Konfiguracji SHALL definiować domyślny kod PIN o wartości "1234".

### Requirement 4: Wspólna abstrakcja sterowania urządzeniami przekaźnikowymi

**User Story:** Jako użytkownik, chcę aby filtr i napowietrzacz były sterowane w ten sam sposób (włącz/wyłącz z harmonogramem), aby obsługa wszystkich urządzeń przekaźnikowych była spójna.

#### Acceptance Criteria

1. THE Kontroler_Urzadzen SHALL udostępniać wspólną abstrakcję sterowania urządzeniem przekaźnikowym w trybach: harmonogram (AUTO), zawsze włączone (ALWAYS ON) oraz zawsze wyłączone (ALWAYS OFF).
2. WHERE urządzenie pracuje w trybie harmonogramu, WHEN bieżący czas mieści się w przedziale od godziny startu do godziny końca, THE Kontroler_Urzadzen SHALL ustawić urządzenie w stan włączony.
3. WHERE urządzenie pracuje w trybie harmonogramu, WHEN bieżący czas znajduje się poza przedziałem od godziny startu do godziny końca, THE Kontroler_Urzadzen SHALL ustawić urządzenie w stan wyłączony.
4. WHERE urządzenie pracuje w trybie zawsze włączone, THE Kontroler_Urzadzen SHALL utrzymywać urządzenie w stanie włączonym niezależnie od czasu.
5. WHERE urządzenie pracuje w trybie zawsze wyłączone, THE Kontroler_Urzadzen SHALL utrzymywać urządzenie w stanie wyłączonym niezależnie od czasu.
6. THE Kontroler_Urzadzen SHALL sterować Napowietrzacz przy użyciu tej samej abstrakcji co Filtr.
7. WHEN Kontroler_Urzadzen zmienia stan urządzenia, THE Kontroler_Urzadzen SHALL ustawić odpowiednie wyjście przekaźnikowe zgodnie z przypisaniem pinu z Modul_Konfiguracji.

### Requirement 5: Sterowanie oświetleniem w trzech trybach

**User Story:** Jako użytkownik, chcę wybierać spośród trzech trybów oświetlenia świetlówek Aquael, aby dostosować światło do potrzeb ryb i roślin.

#### Acceptance Criteria

1. THE Sterownik_Oswietlenia SHALL udostępniać trzy tryby oświetlenia: jasne (bright), jasne z roślinnym (bright+plants) oraz roślinne (plants).
2. WHEN użytkownik wybierze tryb jasne, THE Sterownik_Oswietlenia SHALL załączyć zestaw świetlówek odpowiadający trybowi jasnemu.
3. WHEN użytkownik wybierze tryb jasne z roślinnym, THE Sterownik_Oswietlenia SHALL załączyć obie świetlówki Aquael.
4. WHEN użytkownik wybierze tryb roślinne, THE Sterownik_Oswietlenia SHALL załączyć zestaw świetlówek odpowiadający trybowi roślinnemu.
5. THE Sterownik_Oswietlenia SHALL stosować tryby harmonogramu (AUTO/ALWAYS ON/ALWAYS OFF) dla oświetlenia zgodnie ze wspólną abstrakcją z Wymagania 4.

### Requirement 6: Regulacja grzałki z histerezą

**User Story:** Jako użytkownik, chcę aby grzałka utrzymywała zadaną temperaturę z konfigurowalną histerezą, aby woda miała stabilną temperaturę bez częstego przełączania.

#### Acceptance Criteria

1. THE Regulator_Grzalki SHALL umożliwiać konfigurację temperatury docelowej w zakresie od 18,0 °C do 30,0 °C.
2. THE Regulator_Grzalki SHALL umożliwiać konfigurację histerezy w zakresie od 0,1 °C do 5,0 °C.
3. WHILE Regulator_Grzalki jest w trybie progowym, WHEN zmierzona temperatura spadnie poniżej wartości (temperatura docelowa minus histereza), THE Regulator_Grzalki SHALL załączyć grzałkę.
4. WHILE Regulator_Grzalki jest w trybie progowym, WHEN zmierzona temperatura wzrośnie powyżej wartości (temperatura docelowa plus histereza), THE Regulator_Grzalki SHALL wyłączyć grzałkę.
5. WHERE Regulator_Grzalki jest w trybie wyłączonym (Off), THE Regulator_Grzalki SHALL utrzymywać grzałkę w stanie wyłączonym.
6. IF odczyt temperatury jest niedostępny lub nieprawidłowy, THEN THE Regulator_Grzalki SHALL wyłączyć grzałkę i zarejestrować zdarzenie błędu w Modul_Diagnostyki.

### Requirement 7: Warstwa abstrakcji czujników

**User Story:** Jako deweloper, chcę jednolitej warstwy czujników dla wszystkich pomiarów, aby dodawanie i odczyt czujników były spójne i niezależne od interfejsu.

#### Acceptance Criteria

1. THE Warstwa_Czujnikow SHALL udostępniać wspólny interfejs odczytu dla czujników: temperatury, pH, EC, poziomu wody, przepływu wody, wycieku, CO2 oraz LDR.
2. WHEN Warstwa_Czujnikow odczyta czujnik analogowy, THE Warstwa_Czujnikow SHALL przeliczyć surową wartość ADC na wielkość fizyczną w jednostkach inżynierskich.
3. THE Warstwa_Czujnikow SHALL udostępniać każdą próbkę pomiaru wraz ze znacznikiem czasu pozyskania.
4. IF odczyt czujnika znajdzie się poza zdefiniowanym zakresem poprawności, THEN THE Warstwa_Czujnikow SHALL oznaczyć próbkę jako nieprawidłową.
5. THE Warstwa_Czujnikow SHALL odczytywać czujnik LDR z pinu GPIO34 w zakresie surowym od 0 do 4095.

### Requirement 8: Kalibracja wielopunktowa sond pH i EC

**User Story:** Jako użytkownik, chcę kalibrować sondy pH i EC z poziomu interfejsu w wielu punktach, aby pomiary były dokładne.

#### Acceptance Criteria

1. THE Sonda_pH SHALL udostępniać metody zapisu punktów referencyjnych kalibracji.
2. THE Sonda_EC SHALL udostępniać metody zapisu punktów referencyjnych kalibracji.
3. WHEN użytkownik rozpocznie kalibrację w Kreator_Kalibracji, THE Kreator_Kalibracji SHALL prowadzić użytkownika kolejno przez co najmniej dwa punkty referencyjne.
4. WHEN użytkownik zatwierdzi punkt referencyjny, THE Kreator_Kalibracji SHALL zapisać parę (wartość referencyjna, surowy odczyt) w danej sondzie.
5. WHEN kalibracja zostanie ukończona, THE Platforma SHALL utrwalić punkty kalibracji w NVS z walidacją CRC32.
6. WHEN punkty kalibracji są dostępne, THE Warstwa_Czujnikow SHALL przeliczać kolejne odczyty sondy z użyciem zapisanych punktów referencyjnych.
7. IF użytkownik anuluje kalibrację przed jej ukończeniem, THEN THE Kreator_Kalibracji SHALL zachować poprzednio zapisane punkty kalibracji bez zmian.

### Requirement 9: Sterowanie CO2

**User Story:** Jako użytkownik, chcę sterować dozowaniem CO2 wg harmonogramu lub w powiązaniu z pH, aby utrzymać odpowiednie warunki dla roślin.

#### Acceptance Criteria

1. THE Sterownik_CO2 SHALL sterować elektrozaworem/przekaźnikiem CO2 zgodnie ze wspólną abstrakcją sterowania z Wymagania 4.
2. WHERE Sterownik_CO2 pracuje w trybie powiązanym z pH, WHEN zmierzone pH wzrośnie powyżej skonfigurowanego progu, THE Sterownik_CO2 SHALL otworzyć zawór CO2.
3. WHERE Sterownik_CO2 pracuje w trybie powiązanym z pH, WHEN zmierzone pH spadnie poniżej skonfigurowanego progu, THE Sterownik_CO2 SHALL zamknąć zawór CO2.
4. WHEN Tryb_Serwisowy zostanie aktywowany, THE Sterownik_CO2 SHALL zamknąć zawór CO2.
5. IF odczyt pH jest nieprawidłowy w trybie powiązanym z pH, THEN THE Sterownik_CO2 SHALL zamknąć zawór CO2 i zarejestrować zdarzenie błędu w Modul_Diagnostyki.

### Requirement 10: Karmnik (szkielet implementacji)

**User Story:** Jako użytkownik, chcę aby karmnik był sterowany harmonogramem z wyzwoleniem karmienia i odczytem pozycji, aby przygotować pełną obsługę karmienia w przyszłości.

#### Acceptance Criteria

1. THE Karmnik SHALL udostępniać pin wyzwalający karmienie zdefiniowany w Modul_Konfiguracji.
2. THE Karmnik SHALL udostępniać pin odczytu pozycji/sprzężenia zwrotnego zdefiniowany w Modul_Konfiguracji.
3. WHEN nastąpi zaplanowana godzina karmienia, THE Karmnik SHALL wystawić sygnał na pinie wyzwalającym karmienie.
4. THE Karmnik SHALL umożliwiać konfigurację harmonogramu karmienia obejmującą dni tygodnia oraz do dwóch godzin karmienia na dobę.
5. WHERE karmnik jest wyłączony w konfiguracji, THE Karmnik SHALL nie wystawiać sygnału wyzwalającego karmienie.

### Requirement 11: Rejestrowanie danych historycznych w buforach cyklicznych

**User Story:** Jako użytkownik, chcę przeglądać historię temperatury i pH z ostatnich 24 godzin, aby ocenić stabilność parametrów akwarium.

#### Acceptance Criteria

1. THE Rejestrator_Danych SHALL przechowywać próbki pomiarów w buforach cyklicznych w pamięci RAM.
2. WHEN bufor cykliczny osiągnie maksymalną pojemność, THE Rejestrator_Danych SHALL nadpisać najstarszą próbkę najnowszą próbką.
3. THE Rejestrator_Danych SHALL rejestrować dane historyczne co najmniej dla temperatury i pH obejmujące okres ostatnich 24 godzin.
4. THE Rejestrator_Danych SHALL udostępniać uogólnioną strukturę bufora cyklicznego wykorzystywaną przez serie temperatury, pH, LDR oraz wolnego heapu.
5. THE Modul_Wykresow SHALL rysować wykresy LVGL na podstawie próbek udostępnianych przez Rejestrator_Danych.

### Requirement 12: Aktualizacje OTA i diagnostyka systemu

**User Story:** Jako użytkownik, chcę aktualizować oprogramowanie zdalnie i widzieć stan systemu, aby utrzymywać urządzenie bez podłączania kablem.

#### Acceptance Criteria

1. WHERE OTA jest włączone, THE Menedzer_OTA SHALL nasłuchiwać żądań aktualizacji oprogramowania przez sieć.
2. WHILE trwa aktualizacja OTA, THE Menedzer_OTA SHALL prezentować postęp aktualizacji w Interfejs_Uzytkownika.
3. IF aktualizacja OTA zakończy się niepowodzeniem, THEN THE Menedzer_OTA SHALL zachować dotychczasowe oprogramowanie i zarejestrować zdarzenie błędu w Modul_Diagnostyki.
4. THE Modul_Diagnostyki SHALL udostępniać wartości: wolnego heapu, czasu pracy (uptime), przyczyny ostatniego resetu, licznika uruchomień oraz temperatury CPU.
5. WHEN Interfejs_Uzytkownika wyświetla panel diagnostyki, THE Modul_Diagnostyki SHALL aktualizować prezentowane wartości w odstępach nie większych niż 2 s.

### Requirement 13: Ochrona PIN-em akcji krytycznych

**User Story:** Jako właściciel akwarium, chcę aby zmiany krytyczne wymagały PIN-u, aby przypadkowe lub nieuprawnione zmiany nie zaburzyły pracy systemu.

#### Acceptance Criteria

1. WHEN użytkownik próbuje zmienić harmonogram, nastawę sterowania urządzeniem lub konfigurację systemu, THE Straznik_PIN SHALL zażądać wprowadzenia kodu PIN przed wykonaniem akcji krytycznej.
2. WHEN użytkownik wprowadzi PIN zgodny z PIN-em z Modul_Konfiguracji, THE Straznik_PIN SHALL zezwolić na wykonanie akcji krytycznej.
3. IF użytkownik wprowadzi PIN niezgodny z PIN-em z Modul_Konfiguracji, THEN THE Straznik_PIN SHALL odrzucić akcję krytyczną i pozostawić bieżącą konfigurację bez zmian.
4. WHEN ekran wybudzi się ze stanu uśpienia lub urządzenie zostanie uruchomione, THE Straznik_PIN SHALL wymagać ponownej autoryzacji PIN-em przy pierwszej akcji krytycznej.
5. THE Straznik_PIN SHALL chronić co najmniej następujące akcje: edycję harmonogramów urządzeń, zmianę nastaw grzałki, uruchomienie kalibracji sond, aktywację OTA oraz zmianę PIN-u.
6. THE Straznik_PIN SHALL zezwalać na podgląd odczytów, wykresów i diagnostyki bez wprowadzania PIN-u.

### Requirement 14: Tryb serwisowy z automatycznym wygaśnięciem

**User Story:** Jako użytkownik wykonujący obsługę akwarium, chcę jednym dotknięciem wyłączyć wybrane urządzenia, aby bezpiecznie serwisować, a system sam wrócił do normalnej pracy, jeśli zapomnę je włączyć.

#### Acceptance Criteria

1. WHEN użytkownik dotknie kafelka trybu serwisowego, THE Tryb_Serwisowy SHALL wyłączyć wybrane urządzenia peryferyjne: Filtr, grzałkę oraz CO2.
2. WHILE Tryb_Serwisowy jest aktywny, THE Tryb_Serwisowy SHALL utrzymywać wybrane urządzenia w stanie wyłączonym niezależnie od harmonogramów.
3. WHEN Tryb_Serwisowy zostanie aktywowany, THE Tryb_Serwisowy SHALL uruchomić licznik automatycznego wygaśnięcia o domyślnej wartości 60 minut.
4. WHEN licznik automatycznego wygaśnięcia osiągnie zero, THE Tryb_Serwisowy SHALL automatycznie zakończyć tryb serwisowy i przywrócić normalną pracę urządzeń zgodnie z harmonogramami.
5. WHEN użytkownik ręcznie zakończy Tryb_Serwisowy, THE Tryb_Serwisowy SHALL przywrócić normalną pracę urządzeń zgodnie z harmonogramami.
6. WHILE Tryb_Serwisowy jest aktywny, THE Interfejs_Uzytkownika SHALL prezentować pozostały czas do automatycznego wygaśnięcia.

### Requirement 15: Łagodna degradacja przy braku sprzętu opcjonalnego

**User Story:** Jako użytkownik, chcę aby system działał poprawnie nawet bez podłączonego sprzętu opcjonalnego, aby móc rozbudowywać akwarium etapami.

#### Acceptance Criteria

1. THE Platforma SHALL udostępniać dla każdego urządzenia i czujnika opcjonalnego przełącznik włączenia/wyłączenia w konfiguracji, analogicznie do istniejącego przełącznika `showPhSensor`.
2. WHERE urządzenie lub czujnik opcjonalny jest wyłączony w konfiguracji, THE Interfejs_Uzytkownika SHALL ukryć lub dezaktywować powiązane elementy interfejsu.
3. WHERE czujnik opcjonalny jest wyłączony w konfiguracji, THE Warstwa_Czujnikow SHALL pominąć odczyt tego czujnika.
4. IF sprzęt opcjonalny jest wyłączony w konfiguracji, THEN THE Platforma SHALL kontynuować normalną pracę pozostałych funkcji.

### Requirement 16: Zgodność wsteczna i zachowanie istniejących funkcji

**User Story:** Jako właściciel istniejącego sterownika, chcę aby dotychczasowe funkcje pozostały dostępne, aby rozbudowa nie odebrała mi działającego sprzętu.

#### Acceptance Criteria

1. THE Platforma SHALL zachować istniejące funkcje: harmonogramy Światła, Światła roślinnego, Filtra i Napowietrzacza, sterowanie grzałką, harmonogram karmnika, wykresy, panele Wi-Fi i OTA, dźwięk z godzinami ciszy, auto-motyw LDR oraz diagnostykę.
2. THE Platforma SHALL utrzymać zapis konfiguracji w NVS z walidacją CRC32.
3. WHEN wczytana konfiguracja ma nieprawidłową sumę CRC32, THE Platforma SHALL zastosować wartości domyślne konfiguracji.
4. THE Platforma SHALL zachować istniejący tryb deweloperski odczytujący rzeczywiste wartości ADC.

### Requirement 17: Synchronizacja trybów oświetlenia obu świetlówek Aquael

**User Story:** Jako użytkownik, chcę aby obie świetlówki Aquael zawsze pracowały w tym samym trybie po zmianie ustawienia, aby oświetlenie akwarium było spójne niezależnie od wcześniejszego, nieznanego stanu sterowników lamp.

#### Acceptance Criteria

1. THE Sterownik_Oswietlenia SHALL udostępniać operację synchronizeLights() synchronizującą tryby obu świetlówek Aquael.
2. WHEN operacja synchronizeLights() zostanie wywołana, THE Sterownik_Oswietlenia SHALL odciąć zasilanie obu przekaźników lamp na 10 sekund, aby wymusić powrót sterowników Aquael do domyślnego pierwszego trybu (tryb 1).
3. WHEN okno resetu o długości 10 sekund zakończy się, THE Sterownik_Oswietlenia SHALL wysłać impulsy przełączające (advance-pulses) do obu przekaźników lamp jednocześnie, aby osiągnąć żądany tryb docelowy.
4. THE Sterownik_Oswietlenia SHALL ustalić liczbę impulsów przełączających odpowiadającą trybowi docelowemu: tryb 1 = 0 impulsów, tryb 2 = 1 impuls, tryb 3 = 2 impulsy, tak aby obie świetlówki zakończyły w tym samym, deterministycznym trybie.
5. WHILE operacja synchronizeLights() jest w toku, THE Sterownik_Oswietlenia SHALL blokować normalne sterowanie harmonogramem i przekaźnikami dla obu świetlówek Aquael do czasu zakończenia operacji.
6. WHEN użytkownik zażąda zmiany trybu oświetlenia, THE Sterownik_Oswietlenia SHALL zastosować zmianę za pośrednictwem operacji synchronizeLights(), aby obie świetlówki pozostały zsynchronizowane.

## Wymagania niefunkcjonalne

### Requirement 18: Wydajność i stabilność

**User Story:** Jako użytkownik, chcę aby urządzenie działało płynnie i stabilnie przez długi czas, aby sterowanie akwarium było niezawodne.

#### Acceptance Criteria

1. WHEN użytkownik dotknie elementu interfejsu, THE Interfejs_Uzytkownika SHALL zareagować widoczną zmianą stanu w czasie nie dłuższym niż 100 ms.
2. THE Warstwa_Czujnikow SHALL pozyskiwać próbki pomiarów w odstępach nie większych niż 1 s.
3. WHILE Platforma pracuje nieprzerwanie, THE Modul_Diagnostyki SHALL udostępniać wartość wolnego heapu umożliwiającą wykrycie wycieków pamięci.

### Requirement 19: Utrzymywalność i organizacja kodu

**User Story:** Jako deweloper, chcę aby kod był modularny i czytelny, aby rozbudowa platformy była bezpieczna i przewidywalna.

#### Acceptance Criteria

1. THE Platforma SHALL oddzielać Warstwa_HAL, Warstwa_Czujnikow, Kontroler_Urzadzen oraz Interfejs_Uzytkownika jako odrębne moduły.
2. WHERE wprowadzana jest zmiana strukturalna usuwająca lub przenosząca istniejące funkcje, THE Platforma SHALL wymagać uprzedniej zgody użytkownika przed wykonaniem tej zmiany.
3. THE Platforma SHALL definiować nazwy pinów i stałe konfiguracyjne wyłącznie w Modul_Konfiguracji, bez wartości rozproszonych w kodzie modułów.


