# AquaBox — galeria aktualnego interfejsu LVGL

Ten sam kod C++, fonty i renderer LVGL RGB565 co firmware, 800×480.
Nie są to makiety HTML ani obrazy AI. Pomiary i wyjścia pozostają lokalnym modelem;
radio/czas oraz długa historia na hostcie są kontrolowanymi danymi testowymi.
[Weryfikacja](S3_PRZEKROJ_VERIFICATION.md). Odtwarzanie: tools/verify_s3_ui.ps1.

## Aktualizacja motywów — 2.10.2026

Kontrast wszystkich sześciu palet w trybie jasnym i ciemnym został zwiększony.
Podglądy poniżej są generowane ponownie z bieżącego rendererа produkcyjnego
przez `tools/verify_s3_ui.ps1`; nie są symulacją jasności fizycznego LCD.

![Wybór motywu w dzień](ui/przekroj/themes-original-light.png)

![Niezależny wybór dnia i nocy](ui/przekroj/themes-independent-modes.png)

![Panel Matrix w trybie nocnym](ui/przekroj/themes-matrix-dark-panel.png)

![Motywy Grafit i Bursztyn w trybie nocnym](ui/przekroj/themes-bursztyn-dark.png)

![Motyw Koral w trybie jasnym](ui/przekroj/themes-koral-light.png)

## Panel i niezależne palety

![Panel jasny](ui/przekroj/v11-panel-light.png)

![Panel ciemny](ui/przekroj/v11-panel-dark.png)

Dane i graficzne zakresy po lewej; stan akwarium, poziom wody i bezpośrednie karmienie po prawej.
Dolne ikony są monochromatyczne. Kolejność: Panel / Plan dnia / Historia / Serwis / System.
Niebieski jest kolorem prowadzącym; czerwony wskazuje problem, nie dekorację.

![Niski poziom wody](ui/przekroj/v9-water-low.png)

![Sprzeczne wskazania pływaków](ui/przekroj/v9-water-fault.png)

![Podawanie dawki](ui/przekroj/v9-feeding-dose.png)

## Plan dnia

![Wspólna linia czasu i stan teraz](ui/przekroj/v9-plan-noon.png)

![Plan w trybie ciemnym](ui/przekroj/v9-plan-night.png)

Pionowy marker pokazuje bieżącą godzinę; osobna kolumna stanów nie myli planu z faktycznym
stanem wykonania modelu. Linia i etykieta pozostają widoczne również przy północy.

## Historia — Parametry wody

![Tylko temperatura i godziny pomiarów](ui/przekroj/v10-history-temperature.png)

Na pierwszym ekranie nie ma przełączników pH/EC, liczby próbek, min/max ani instrukcji Dotknij.
Pod wykresem są godziny rzeczywiście zapisane z pomiarami, z sekundami. Nieznany czas to --:--:--.

![Pełny widok temperatury](ui/przekroj/v10-history-temperature-full.png)

![Wybór zakresu pod jednym przyciskiem](ui/przekroj/v10-history-range.png)

Pełny widok ma jeden przycisk Zakres z wyborem 1/12/24/48/72 h.
Obok są trzy niezależne przyciski dołączania pH, EC i Grzałki.

![pH i EC dołączone do temperatury](ui/przekroj/v10-history-water.png)

![Wspólny kursor, trzy skale i pas ON/OFF](ui/przekroj/v10-history-all-selected.png)

Wykresy mają wspólną oś czasu i kursor, ale osobne skale. Stopnie, pH i µS/cm nie są mieszane.
Grzałka dołącza historię stanów oraz przerywane bieżące progi histerezy.
Brak danych lub korekta zegara przerywa linię; zero nie udaje brakującego pomiaru.

![Liczba próbek i min/max pod Szczegóły](ui/przekroj/v10-history-72h-details.png)

![Ten sam układ w ciemnej palecie](ui/przekroj/v10-history-night-all.png)

Wielogodzinny przebieg jest fixture hosta. Na urządzeniu historia narasta od uruchomienia;
wcześniejsze próbki bez ustawionego zegara nie dostają dopisanej wstecz godziny.
Zmiana czasu nie przepisuje zapisanych dat, a zakres wielodniowy pokazuje także dzień/miesiąc.

## Powiększenie parametru

![Powiększone pH, zachowany czas i kursor](ui/przekroj/v11-history-day-ph.png)

![Powiększone EC w ciemnej palecie](ui/przekroj/v11-history-night-ec.png)

Dotknięcie nazwy/wartości parametru powiększa jego pas. Strzałka wraca do porównania;
zakres i warstwy są pamiętane w sesji, bez dodatkowych zapisów NVS.

## Historia — Dolewki / ATO

![Poziomy wody i dolewki](ui/przekroj/v10-history-ato.png)

![Szczegóły pojedynczej dolewki](ui/przekroj/v10-history-ato-full.png)

ATO również używa jednego wyboru zakresu. Podsumowanie dawek jest w Szczegółach.
Pokazywane są stany MIN/MID/MAX, czas pracy pompy i odstępy między dawkami.
Litry są przybliżeniem przy założeniu 1 l/min, nie pomiarem objętości.
Dawki w renderach pochodzą z raportów testowych; firmware nie tworzy ich sam i nie uruchamia pompy.

## Zdarzenia

![Ważność wyróżniona tylko wizualnie](ui/przekroj/v11-events-active.png)

Ważniejsze wpisy są pierwsze, mają czerwony akcent i delikatne tło.
![Przyczyna, która ustąpiła](ui/przekroj/v11-events-resolved.png)

Wpisy mają zapisane godziny, licznik powtórzeń i czas ustąpienia przyczyny. Czyszczenie jest w Zaawansowanych.
Pozostałe są neutralne. Nie ma nagłówków „ważne/zwykłe” ani takich etykiet w wierszach.

## System — spójny układ

![Nowa kolejność kategorii](ui/przekroj/v9-system-ordered.png)

![Sterowanie](ui/przekroj/v11-system-control.png)

![Zaawansowane z jednolitą typografią](ui/przekroj/v11-system-advanced.png)

Obsługa / Sieć i czas / Sterowanie / Zaawansowane.
Bez dodatkowego AquaBox przy Ustawienia i bez informacji o automatycznym zapisie w menu.

![Mapa sprzętu: moduły](ui/przekroj/v9-hardware-map.png)

![Mapa sprzętu: obieg wody](ui/przekroj/v9-hardware-water.png)

Mapa znajduje się w Sterowaniu. Kategorie po lewej i listy modułów po prawej używają
tego samego języka interfejsu co System. Strzałka modułu wraca do wybranej kategorii mapy. Wszystkie cztery wiersze Sterowania mieszczą się w całości.

![Diagnostyka sterownika ESP8266](ui/przekroj/v9-controller-diagnostics.png)

Brak UART jest stanem rzeczywistym, nie udaną próbą łączności.
Nie udaje się ACK, opóźnienia ani wersji modułu wykonawczego.

## Edytory suwakowe

![Kąt karmnika](ui/przekroj/v9-feeder-slider.png)

![Wygaszanie treści](ui/przekroj/v9-sleep-slider.png)

![Czas serwisu](ui/przekroj/v9-service-slider.png)

Wartość na stronie ustawień otwiera edytor z suwakiem oraz precyzyjną korektą ±1.
Strzałka powrotu zatwierdza szkic i wraca do miejsca otwarcia; nie ma drugiego przycisku Wróć.
Przeciąganie nie zapisuje każdej pozycji do flash. Granice i specjalne Wyłączone są jawne.

![Ustawienia karmnika](ui/przekroj/v11-feeder-settings.png)

![Nastawy termostatu](ui/przekroj/v9-thermostat-controls.png)

## Skupienie na podstronie i zegar

![Sieć: jasna paleta i wygaszona rama](ui/przekroj/v9-network-light-focused.png)

![Sieć: ciemna paleta i wygaszona rama](ui/przekroj/v9-network-dark-focused.png)

![Klawiatura jasna](ui/przekroj/v9-keyboard-light.png)

![Zegar i czas](ui/przekroj/v9-clock.png)

![Edytor zegara](ui/przekroj/v9-clock-edit.png)

Rama jest widoczna tylko w 20%; treść zachowuje normalny kontrast.
Powiadomienia dotyczą ważnych komunikatów i błędów, nie udanych zapisów ustawień.

Rendery wcześniejszych iteracji zachowano na dysku. Nowe ekrany odsyłają do v11; zachowane scenariusze v9/v10 również wyrenderowano ponownie na kodzie v11.


## Ochrona karmienia

![Odstęp przed kolejną dawką](ui/przekroj/v11-feeding-guard.png)

Ręczne karmienie pozostaje jednym kliknięciem. Domyślny odstęp 10 min ma suwak 1–60 min;
bliska automatyczna dawka jest pomijana, nie odrabiana. Ostatni czas w renderach pochodzi z modelu,
nie raportu fizycznego silnika. UART nadal niepodłączony.
