# AquaBox HMI — high-end refinement, 29.09.2026

Zakres: wyłącznie interfejs ESP32-S3/LVGL dla Waveshare 4,3″ 800×480. Nie zmieniono firmware ESP8266, protokołu UART, pinów, automatyki, przekaźników ani modelu danych. To korekta istniejącego produktu, nie nowy projekt. Ocena wyglądu pozostaje częściowo subiektywna; poprawność geometrii i zachowań weryfikowano w rendererze hostowym LVGL, nie na fizycznym LCD.

## Backup i odtworzenie

Przed pierwszą zmianą skopiowano 122 pliki źródeł, konfiguracji i aktualnych podglądów do:

`C:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium-ui-backups\2026-09-29_22-57-11-pre-premium-refinement`

Każdy plik porównano z oryginałem przez SHA-256. Celowo nie kopiowano katalogów kompilacji `preview/.pio` i `preview/output` (odtwarzalne, łącznie około 589 MB). Nie tworzono commitu ani nie zmieniano pozostałych części dużego, już wcześniej nieczystego repozytorium.

## Zastany system i przyjęte tokeny

| Rola | Token / implementacja | Zastosowanie |
|---|---|---|
| Siatka | 4, 8, 12, 16, 24, 32 px | rytm odstępów; istniejącej kompozycji 800×480 nie skalowano globalnie |
| Promień | kontrolka 6, karta 10, dialog 12 px | wcześniej powtarzały się 4, 6, 8, 10 i 12 px bez jasnej roli |
| Typografia | Inter 14 / 16 / 20 / 24 / 44 px | opis pomocniczy / tekst i kontrolka / sekcja / tytuł / pomiar |
| Powierzchnie | `background`, `surface`, `card`, `chrome`, `selected` | osobne palety jasna i nocna w `ui_theme.h`; bez ciężkich cieni |
| Semantyka | `accent`, `good`, `warning`, `bad` oraz tła statusów | niebieski = interakcja, zieleń = norma, bursztyn = ostrzeżenie, czerwony = alarm |
| Ruch | fast 160, normal 220, slow 280 ms | krótki feedback, przejście nawigacji/dialogu, wejście powiadomienia |
| Dotyk | minimum docelowe 44 px wysokości | około 4,94 mm na tym panelu; ważna jest także szerokość i odstęp |

Kod tokenów: `firmware/esp32s3_ui/include/ui_design.h`. Palety semantyczne: `firmware/esp32s3_ui/include/ui_theme.h`. Fizyczny aktywny obszar panelu to około 95,04 × 53,86 mm, zatem 1 px odpowiada około 0,119 × 0,112 mm. Kompromis: 44 px nie jest dużym celem w milimetrach, ale podniesienie wszystkich kontrolek do 7 mm bez zmniejszenia treści wymagałoby przebudowy pięciu ekranów; zachowano szerokie pola i zwiększono małe cele tam, gdzie nie zachodziły na sąsiadów.

## Problemy jakościowe i decyzje

| ID | Problem w wersji wyjściowej | Korekta / decyzja |
|---|---|---|
| P01 | Duży blok zaznaczenia w dolnej nawigacji konkurował z treścią. | Zastąpiono go cienkim, neutralnym wskaźnikiem nad ikoną. Subiektywna, świadomie oszczędna korekta. |
| P02 | Promienie podobnych powierzchni i czasy ruchu były rozproszone w kodzie. | Dodano tokeny promieni, typografii, odstępów i ruchu; zastosowano je w kluczowych komponentach. |
| P03 | Karty pomiarów i stanu nie miały natychmiastowego feedbacku dotyku. | Dodano stan `PRESSED` dla kart, przepływu, poziomu i wspólnych przycisków; przyciski mają skalę około 97,7% tylko podczas nacisku. |
| P04 | Styl `DISABLED` był wpisywany jako zwykła przezroczystość; po ponownym włączeniu kontrolka mogła pozostać wyblakła. | Przeniesiono przezroczystość do selektora `LV_STATE_DISABLED`; stan może poprawnie wracać. Poziom 70% pozostawia czytelny opis niedostępnej funkcji. |
| P05 | Etykiety najważniejszych pomiarów miały 14 px; wtórny tekst nagłówka w jasnym motywie miał kontrast około 3,99:1. | Pomiarowe nagłówki mają 16 px; `chromeMuted` zmieniono z `#5C7290` na `#4F6685`, co daje około 4,76:1 na tle `#DFE8F5`. |
| P06 | Ostrzeżenia i alarmy w dzienniku oraz przekroczenia na wykresie używały jednakowej czerwieni. | Ostrzeżenie = bursztyn i punkt, alarm/temperatura krytyczna = czerwony i wykrzyknik; tekst, tło i znak wspierają rozróżnienie, więc kolor nie jest jedyną wskazówką. |
| P07 | Pusty lub jednoelementowy wykres wyglądał jak niedokończony. Nieznana godzina powtarzała techniczne kreski na trzech osiach. | Dodano czytelne stany „Brak pomiarów”, „Brak wiarygodnych pomiarów”, „Za mało danych do trendu” oraz pojedynczy opis „Czas nieznany”. |
| P08 | Progi grzałki rysowane nad historią mogły wyglądać jak nastawy historyczne. | Podpisano je jako „Progi bieżące”; samej historii nastaw ani modelu nie dodawano. |
| P09 | Akcje usuwające dane miały identyczne wyróżnienie jak zwykłe potwierdzenie. | Tylko przycisk destrukcyjny ma czerwony obrys, czerwony tekst i delikatne tło; reszta dialogu pozostaje neutralna. |
| P10 | Opisy „zawsze włączona” i „jedna dawka = jeden obrót” nie były zgodne z faktycznym kontekstem. | Doprecyzowano „w czasie serwisu” i „ustawiony kąt”; nie zmieniono sposobu działania. |
| P11 | Sześć gęstych przełączników karmnika miało tylko 40 px wysokości. | Każdy ma 44 px, a siatka została rozłożona z 4-pikselowym rytmem bez kolizji ze stopką. |

## Ekran po ekranie

| Ekran | CURRENT / problem | REFINEMENT, rationale i implementacja |
|---|---|---|
| Panel | Hierarchia: stan, trzy pomiary, przepływ, poziom i karmienie była trafna; część etykiet zbyt drobna, dotykane kafelki bez feedbacku. | Zachowano układ. Zwiększono etykiety pomiarów do 16 px, dodano stan nacisku kafelków i ujednolicony promień kontenera. Wartość 44 px nadal dominuje; alarm nie zalewa kolorem całego ekranu. |
| Plan dnia | Oś 00:00–24:00 i pionowy znacznik chwili są czytelne, a rozkład mieści się bez przewijania. | Nie ruszano danych ani geometrii wykresu. Działa wspólny nowy stan nawigacji i krótsze przejście głównych kart. |
| Historia / parametry | Pełny wykres i wybór warstw są dobre, lecz wyjątki pomiarowe były przesadnie czerwone, stan jednej próbki pusty wizualnie. | Rozdzielono ostrzeżenie od alarmu, zaprojektowano stany bez trendu, poprawiono nieznany czas i opis bieżących progów. Zakresy, wspólny kursor i dotyk punktów pozostały. |
| Historia / ATO | Odrębny widok dolewek z szacowaną objętością nie powinien udawać precyzyjnego przepływomierza. | Pozostawiono jednostki i znak przybliżenia; wspólne przyciski zakresów zyskały reakcję nacisku i niewidzialnie większą strefę. |
| Historia / zdarzenia | Wpisy były sortowane sensownie, ale warning i alarm wyglądały tak samo. | Nadal jedna lista bez literalnego podziału; aktywne ostrzeżenia mają bursztyn i punkt, alarmy czerwień i wykrzyknik, rozwiązane i zwykłe pozostają neutralne. |
| Serwis | Główny wybór lampy, czas i siatka wyjść są logiczne; etykieta sugerowała ciągłe świecenie lampy. | Poprawiono etykiety, zwiększono czytelność kontrolek niedostępnych i zachowano wszystkie działania oraz blokady. Nie zmieniono zabezpieczeń serwisowych. |
| System | Cztery kategorie porządkują wiele funkcji; powtarzalne powierzchnie miały różne promienie, jeden podtytuł dublował tytuł. | Ujednolicono promienie kart i poprawiono podtytuł „Zaawansowane”. Drzewo kategorii i kolejność pozostały. |
| Mapa sprzętu i strony 11 modułów | Oddzielenie stanu wykonania od trybu zadanego jest wartościowe. | Zachowano rozkład, tryby i przejścia. Strona karmnika otrzymała dokładny opis dawki; przyciski dziedziczą stany nacisku i fokusu klawiaturowego. |
| Harmonogram, termostat, karmienie, zakresy i edytor liczbowy | Posiadają automatyczny zapis i walidację modelu, a precyzyjny suwak ma już szeroki hitbox. | Nie zmieniono granic ani logiki. Ujednolicono wizualny stan przycisków; siatka pauz karmnika ma większe cele. |
| Sieć, zegar, ekran, interakcja, PIN | Wymagają zachowania istniejących wyjątków: klawiatury, DST, PIN-u i błędu zapisu. | Nie ruszano funkcjonalności; wspólny promień i krótszy ruch dialogu obejmują tę grupę. Stany `DISABLED` nie pozostają już przyklejone po zmianie. |
| Diagnostyka, ESP8266, dziennik, firmware, zasilanie, scenariusze | Wiarygodnie sygnalizują brak fizycznej integracji. | Zachowano treści techniczne i brak fikcyjnego ACK. Potwierdzenia kasowania mają jednoznaczny wizualny charakter. |
| Dialogi, PIN i klawiatura | Układ klawiszy i treści mieści się na 800×480; użytkownik ma już ścieżkę anulowania. | Dialog 12 px, wejście 220 ms; przyciski mają `PRESSED`, `DISABLED` i focus tylko dla sterowania klawiszowego. Nie dodawano kolejnych popupów zapisu. |

## Celowo pozostawione bez zmian

- Pięć głównych sekcji i ich kolejność — orientacja użytkownika była już spójna; istniejące testy gestów potwierdzają granice i powrót.
- Dwie strefy Panelu, z priorytetem pomiarów po lewej i stanu/wody/karmienia po prawej — układ pozwala szybko ocenić sytuację bez dokładania kolejnych kart.
- Model lokalny/demonstracyjny i brak UART do ESP8266 — zmiana źródła danych lub logiki wyjść wykracza poza UI; render nie może potwierdzić pracy przekaźników.
- Automatyczny zapis, walidacje, PIN, zachowanie zegara/DST i ograniczenia karmnika — są częścią sprawdzonych przepływów, nie dekoracji.
- Bieżący kursor wykresu, wybór 1/12/24/48/72 h, warstwy pH/EC/grzałki i osobne ATO — funkcje były użyteczne; zmieniono wyłącznie sposób komunikacji stanu.
- Animacje poziomu wody i dawkowania oraz osobne palety jasna/nocna — działają przy istniejącym budżecie klatek; nie dokładano animacji pozornych.

## Microinteractions i stany

- Dotyk przycisku: natychmiastowa zmiana powierzchni i transformacja do 250/256 skali; nie ma nowej animacji pełnoekranowej.
- Dotyk karty pomiaru/zdrowia/wody/przepływu: chwilowe podświetlenie powierzchni bez zmiany układu.
- Fokus klawiaturowy: obrys 2 px, tylko `LV_STATE_FOCUS_KEY`, aby po zwykłym dotyku nie zostawała niebieska ramka.
- Aktywna karta główna: cienka linia i wyższy kontrast ikony/tekstu, bez dodatkowego koloru w dolnym panelu.
- `DISABLED`: styl związany z rzeczywistym stanem, możliwy powrót do pełnej czytelności po odblokowaniu.
- Toast: istniejący zjazd z góry, skrócony 360 → 280 ms; wyświetlany wyłącznie dla ważnych komunikatów. Dialog: wejście 240 → 220 ms. Nawigacja główna: 270 → 220 ms. Tryb ograniczonego ruchu pozostaje respektowany.
- Woda i karmienie: zachowano istniejące animacje powiązane ze stanem, nie dodano dekoracyjnego bluru, nowych bitmap ani ciągłych pulsacji alarmu.

## Przed / po — mierzalne różnice

| Kryterium | Przed | Po | Interpretacja |
|---|---:|---:|---|
| Jasny wtórny tekst nagłówka | ~3,99:1 | ~4,76:1 | poprawa kontrastu na jasnym chrome |
| Karmnik: małe przełączniki | 40 px | 44 px | około 4,49 → 4,94 mm pionowo |
| Widok ostrzeżenia i alarmu | jeden czerwony styl | bursztyn / czerwień | łatwiejszy skan dziennika |
| RAM statyczny firmware | 134 352 B | 134 376 B | +24 B; 41,0% limitu 327 680 B |
| Flash programu | 1 278 995 B | 1 279 795 B | +800 B; 19,5% przydzielonej partycji |
| Śledzony heap LVGL po teście nawigacji hosta | 11 922 B | 12 676 B | +754 B; wartość hostowa, nie pomiar urządzenia |
| Największy szczyt śledzonego heap LVGL w zestawach | 25 575 B | 27 478 B | +1 903 B; stany stylu, etykieta trendu i znaki ważności wpisów |

Nie dodano bufora pełnej klatki, canvasu ani bitmapy, więc nie ma nowego dużego obciążenia PSRAM w kodzie. Rzeczywiste wolne PSRAM, przepustowość RGB i FPS trzeba zmierzyć na urządzeniu; render hostowy nie pozwala ich wywnioskować. Wszystkie 17 aktualnych scenariuszy (16 istniejących + galeria), łącznie 3 588 interakcji, przeszło; osobne 264 sprawdzenia modelu/statusu/runtime/planu/palet/czasu historii są uruchamiane przy starcie scenariuszy. Kompilacja ESP32-S3: PASS. Niczego nie wgrywano na płytkę.

## Galeria 800×480 (końcowy render LVGL)

| Ekran | Jasny | Nocny |
|---|---|---|
| Panel | [PNG](przekroj/premium-panel-light.png) | [PNG](przekroj/premium-panel-dark.png) |
| Plan dnia | [PNG](przekroj/premium-plan-light.png) | [PNG](przekroj/premium-plan-dark.png) |
| Historia | [PNG](przekroj/premium-history-light.png) | [PNG](przekroj/premium-history-dark.png) |
| Serwis | [PNG](przekroj/premium-service-light.png) | [PNG](przekroj/premium-service-dark.png) |
| System | [PNG](przekroj/premium-system-light.png) | [PNG](przekroj/premium-system-dark.png) |

Punkt odniesienia: wcześniejszy [Panel jasny](przekroj/v11-panel-light.png), [Panel nocny](przekroj/v11-panel-dark.png), [System](przekroj/v11-system-control.png), [Historia szczegółowa](przekroj/v11-history-focus-temperature.png). Nowy skrypt `firmware/esp32s3_ui/preview/premium-main.txt` odtwarza pięć ekranów w dwóch paletach.

## Odbiór na fizycznym panelu — nadal wymagany

1. Zobaczyć oba motywy w typowym i słabym oświetleniu oraz z typowej odległości montażowej; zwłaszcza etykiety 14 px i niedostępne wyjścia Serwisu.
2. Dotknąć brzegów kart, pięciu ikon nawigacji, strzałki powrotu, sześciu przełączników karmnika, zakresów wykresu i przycisków modalnych; sprawdzić brak przypadkowych sąsiednich aktywacji.
3. Obserwować płynność wody, karmienia, toastu i przejść przy jednoczesnym Wi-Fi; hostowe klatki nie dowodzą 34 FPS na RGB DMA.
4. Zmierzyć wolne heap/PSRAM po długiej pracy, wielokrotnych wejściach w szczegółowy wykres i po zmianie palety; potwierdzić brak narastania pamięci na urządzeniu.
5. Sprawdzić widoczność czerwieni i bursztynu na rzeczywistym IPS, w tym przy niewielkim odchyleniu kąta patrzenia.
6. Zweryfikować zachowanie po NVS autozapisie, restarcie i przy odłączonym ESP8266. Obecny lokalny model demonstracyjny nie jest potwierdzeniem stanu fizycznych wyjść.

Świadomie nie dodano pełnoekranowych animacji detali, gradientów, cieni ani nowych ikon. Wymagałyby kosztu renderingu lub zmieniałyby charakter, który w aktualnym projekcie działa dobrze.
