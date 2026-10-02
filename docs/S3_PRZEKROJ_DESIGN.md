# AquaBox — projekt panelu 800 × 480

Opis dotyczy bieżącego UI LVGL 8.4.0 dla ESP32-S3, aktualizowany 2.10.2026.
Panel obsługuje Waveshare 16249 przez RGB565 i GT911. Łącze UART do WeMos nie
jest podłączone: odczyty i sterowanie na ekranie należą do lokalnego modelu.
Status integracji jest pokazany w diagnostyce.

## Panel i hierarchia

Panel pokazuje wyłącznie czujniki: temperaturę DS18B20, pH, EC, obecność impulsów przepływu i dwa pływaki.
Nie pokazuje filtra ani lamp jako elementów sterujących. Cztery pola pomiarowe zajmują lewą część treści.
Prawy kontener 298 × 326 px łączy stan akwarium, poziom wody i karmienie.
Wszystko w normie albo najważniejsza przyczyna alarmu mieści się w jego górnej karcie;
nie ma już pełnoszerokiego komunikatu nad pomiarami. Długie tytuły zawijają się,
a opis przesuwa się pod rzeczywistą wysokość tytułu. Pola pomiarowe mają wysokość 142 px.
Przycisk Serwis akwarium usunięto z Panelu. Serwis pozostaje osobną kartą dolnej nawigacji.
Stan normalny jest spokojny, ostrzeżenie bursztynowe, alarm krytyczny czerwony.
Potwierdzenie alarmu nie usuwa przyczyny; nowe przyczyny ponownie wymagają uwagi.

Temperatura, pH i EC mają skalę: neutralne tło, niebieski odcinek skonfigurowanego zakresu i znacznik wartości.
Przekroczenie zmienia kolor oraz tekst na Za nisko / Za wysoko. Brak danych nie tworzy znacznika ani zera.
Zakres edytuje się przez dotknięcie pomiaru → Zakres normy. Zapis jest trwały i walidowany.
Początkowe zakresy 24–26°C, pH 6,5–7,5 i EC 300–700 µS/cm są nastawami UI,
nie uniwersalnymi zaleceniami hodowlanymi. Użytkownik dopasowuje je do obsady.
Ochrona grzałki 28,5°C pozostaje niezależna od tych zakresów.

Woda jest wizualizacją jakościową, nie procentowym pomiarem wysokości.
MIN oznacza niski poziom, MAX osiągnięty poziom docelowy. MAX aktywny sam w sobie nie jest alarmem.
MIN i MAX aktywne jednocześnie oznaczają sprzeczność; nieważne wejście daje Brak danych.
Fala jest rysowana w małym prostokącie co 30 ms, bez pełnoekranowego bufora animacji.
W dzień zbiornik ma jasne tło #E1EFFB i błękitną wodę #89BDE5; w nocy #142F50 i #3C84B9.
Niski poziom używa odrębnego koloru ostrzeżenia z bieżącego motywu.
Sprzeczność i brak danych mają statyczne kreskowanie, nie sugerują wiarygodnej wysokości wody.
Przepływ pozostaje detekcją impulsów — brak skalibrowanych danych l/h.

Karmienie na Panelu rozpoczyna jedną dawkę od razu, bez pytania o czas pauzy.
Aktywny przycisk otwiera zakończenie bieżącej operacji, nigdy kolejną dawkę.
Animacja pokazuje obrót karmnika, kąt oraz opadające ziarenka wyłącznie podczas ruchu.
Źródło lokalne jest wyraźnie odróżnione od potwierdzonej pozycji sterownika.
Model lokalny odwzorowuje skonfigurowany kąt w 2 s; nie jest telemetrią fizycznego silnika.
Przy źródle Controller pozycja pozostaje nieznana do pierwszego raportu.
Raporty wymagają bieżącego ID operacji, skończonej i monotonicznej pozycji w zakresie celu.
Zakończenie wymaga raportu complete przy osiągniętym kącie, a nie upływu animacji.
Brak raportu przez 8 s lub przekroczenie 120 s obrotu daje ważny błąd i blokadę ponowienia dawki.
Odblokowanie wymaga jawnego potwierdzenia sprawdzenia karmnika w Centrum uwagi.
Pauza zaczyna się przy starcie i obejmuje wyłącznie wybrane urządzenia; krótsza pauza nie ucina obrotu.
Potwierdzony koniec obrotu i koniec pauzy to dwa odrębne stany interfejsu.
Naturalny koniec odświeża Panel, zamyka nieaktualny dialog i informuje o przywróceniu AUTO.
Ta sama reguła dotyczy upływu czasu serwisu. Zamknięcie starego dialogu zapobiega ponownemu
uruchomieniu dawki przez przycisk, który wcześniej oznaczał Zakończ teraz.

## Nagłówek i nawigacja

AquaBox po lewej, HH:MM pośrodku, SSID i ikona po prawej.
STA pokazuje rzeczywistą nazwę sieci i poziom RSSI; AP podpis AquaBox AP i inną ikonę routera.
Wyłączone radio pokazuje wyłącznie szarą ikonę Wi-Fi. Brak połączenia i brak odczytu są osobnymi stanami.
Dotknięcie otwiera SSID/RSSI/liczbę klientów i skrót do ustawień.
Podgląd statusu działa także podczas edycji. Skrót do ustawień jest wtedy wyłączony
z wyjaśnieniem; nie porzuca niezapisanego szkicu ani operacji zapisu w toku.
Alarmy są w Panelu, nie w nagłówku. Stan Wi-Fi nie potwierdza połączenia UART.

Pięć kart w kolejności: Panel / Plan dnia / Historia / Serwis / System.
Dolna nawigacja używa subtelnych ikon: pomiary, pojedynczy konturowy klucz, kalendarz, wykres i ustawienia.
Zgodnie z ostatnią decyzją użytkownika ikony są monochromatyczne, bez czerwono-zielonych stanów.
Aktywna karta jest wyróżniona spokojnym tłem. Kolory semantyczne występują wyłącznie w treści.
Każda ikona z nazwą należy do pola 146 × 54 px. Przesuwanie: próg 72 px,
bez zawijania i bez klikania kontrolki pod końcem gestu. Animacja wejścia trwa 270 ms,
przesuwa treść o 72 px i wyhamowuje; działa zarówno po geście, jak i dotknięciu dolnej nawigacji.
Edytory, dialogi i klawiatura blokują gesty kart do zatwierdzenia lub anulowania.
Dialog wchodzi przez 240 ms z przesunięciem 16 px i narastaniem krycia. Edycja pojedynczej
cyfry PIN-u lub kroku wartości nie uruchamia ponownie animacji całego dialogu.

Sześć niezależnych palet w `ui_theme.cpp`, wybieranych osobno dla dnia i nocy.
AquaBox zachowuje niebieską tożsamość; Matrix, Ocean, Koral, Grafit i Bursztyn
mają własne barwy. Zwiększono różnicę jasności między tłem, powierzchnią, kartą,
nawigacją i zaznaczeniem. Kontrast tekstu jest sprawdzany dla wszystkich
wariantów. Font Inter OFL: 14/16/20/24/44 px.
W jasnym motywie ramy, aktywne przyciski, pola, klawiatura i komunikaty pozostają jasne.
W ciemnym aktywne powierzchnie pozostają granatowe; jasny tekst nie staje się tłem przycisku.
Testy sprawdzają jasność powierzchni i kontrast tekstu dla obu zestawów.
Tylko bieżące drzewo obiektów LVGL; brak pełnoekranowych bitmap, rozmyć i cieni.

Powiadomienia dotyczą błędów i istotnych zakończeń operacji; powodzenie zapisu i zwykłej zmiany nie daje pop-upu.
Stan zapisu pozostaje dyskretnie w formularzu; błąd wymaga uwagi i nadal ma Ponów/Cofnij.
Powiadomienia pojawiają się na górze treści, pod nagłówkiem (x80, y64, szerokość 640 px).
Wejście z góry trwa 360 ms z delikatnym przekroczeniem pozycji docelowej i wyhamowaniem;
równolegle przez 240 ms narasta krycie. Po 3,5 s od utworzenia komunikat wyjeżdża do góry
i wygasa przez 220 ms. Wysokość dopasowuje się do zawiniętego tekstu.
Dotknięcie komunikatu go zamyka i nie aktywuje elementu pod nim. Nowy komunikat zastępuje poprzedni,
kasując jego animacje; usuwanie po animacji jest odroczone poza jej callback.
Ograniczenie animacji wyłącza również ruch i wygaszanie powiadomień. Błędy pozostają widoczne
niezależnie od opcji potwierdzeń działań. To overlay: podczas wyświetlania zasłania fragment treści,
ale nie zmienia jej układu ani nie ściska danych.

## Serwis

Przed rozpoczęciem wybiera się podstawową lampę: przednia lub tylna.
Start: ta lampa ON, druga lampa i wszystkie pozostałe wyjścia OFF.
Podstawowej lampy nie da się wyłączyć. Zmiana wyboru zachowuje co najmniej jedno światło.
Druga lampa, filtr, napowietrzanie, grzałka i CO2 mają rzeczywiste przełączniki lokalnego modelu.
Grzałka nadal podlega poprawności pomiaru i odcięciu 28,5°C.
Karmnik jest zablokowany; ATO nie ma przypisanej pompy.
Serwis ma limit 1–120 min. Koniec albo timeout przywracają AUTO i profile lamp sprzed serwisu.
Karmienie i serwis nie mogą działać jednocześnie.

To nie jest potwierdzenie przełączenia fizycznego przekaźnika. WeMos nadal działa niezależnie.
Integracja profili Aquael i komend/ACK będzie osobnym etapem po podłączeniu UART.

## Funkcjonalne karty i System

Plan dnia edytuje harmonogramy, dni tygodnia oraz 1–2 pory karmienia.
Wspólna pionowa linia z etykietą Teraz HH:MM przebiega przez wszystkie pięć torów.
Pełne kółko i podpis WŁ. oznaczają bieżący stan modelu; pusty znacznik ma WYŁ., Pauza lub Brak.
Oś obejmuje 00:00–24:00. Harmonogram nocny uwzględnia ogon poprzedniego aktywnego dnia.
Zmiana minuty, dnia albo blokady aktualizuje linię i stany bez ponownego otwierania karty.
Formularze konfiguracji nie mają przycisków zapisu: zapis następuje 1,2 s po ostatniej zmianie.
Wróć przy niezapisanej zmianie wymusza zapis i wychodzi dopiero po jego powodzeniu.
Zapis działa transakcyjnie: najpierw NVS, potem model. Zmiana dialogowego szkicu godziny
harmonogramu nadal może być anulowana przed zatwierdzeniem tego pola.
Automatyczne karmienie modelu startuje tylko na obserwowanej granicy minuty;
zmiana zegara nie odtwarza pominiętych dawek i nie dubluje dawki z tego samego dnia.
Historia ma trzy grupy: Parametry wody (temperatura/pH/EC), Dolewki / ATO i Zdarzenia.
Podgląd Parametrów wody zawiera wyłącznie temperaturę, jej wykres i godziny pomiarów.
Bez liczby próbek, Wyczyść historię, instrukcji Dotknij, min/max i filtrów czasu na pierwszym planie.
Dotyk otwiera pełny ekran: jeden przycisk Zakres z wyborem 1/12/24/48/72 h,
niezależne przyciski pH, EC i Grzałka, wspólny kursor oraz Na żywo.
pH i EC dołączają osobne, zsynchronizowane pasy z własnymi skalami; nie miesza się jednostek na jednej osi Y.
Przesuwanie bada ten sam moment na wszystkich włączonych wykresach.
Szczegóły otwierają liczbę/poprawność próbek, min/max, rzeczywistą długość danych i informacje o zegarze.
Krótki dotyk otwiera wykres; gest na podglądzie nadal zmienia kartę, nie otwiera szczegółów.
W temperaturze pełny widok po włączeniu Grzałki ma przerywane obecne progi ON < cel-histereza / OFF >= cel
oraz osobny tor rzeczywistych stanów lokalnego modelu grzałki. Brak stanu nie udaje OFF.
Model utrzymuje ON między progami, zamiast przełączać przy każdej wartości wewnątrz histerezy.
Chronologia pozostaje monotonicznym czasem od startu. Każda próbka zapisuje dodatkowo lokalną datę,
godzinę i sekundę znaną w chwili pobrania, w stałym polu 4 B. Etykiety osi i kursora używają tych zapisów.
Zmiana zegara/NTP/DST nie przepisuje starych godzin; większa korekta przerywa łączenie próbek.
Nieznana godzina to --:--:--, nigdy wyliczony wstecz czas. Przy zakresach wielodniowych oś dodaje datę.
64 szybkie próbki co 15 s i 865 archiwalnych co 5 min są łączone bez duplikatów.
Nie ma backfillu, zer za brakujące odczyty ani wytwarzania wcześniejszych 72 godzin.
Po restarcie historia zaczyna się od początku. Min/max dotyczy zarejestrowanych próbek, nie wszystkich chwil między nimi.
Stałe dzienniki przechowują 1024 zmiany grzałki, 1024 zmiany pływaków, 512 zakończonych dolewek i 24 zdarzenia.
Najstarsze wpisy są nadpisywane po zapełnieniu; przerwy nie są interpolowane jako wiarygodny stan.
ATO pokazuje poziomy jakościowe, odcinki pracy pompy, czas dolewki i odstęp od końca poprzedniej do początku następnej.
Objętość jest oznaczona ~ i liczona z czasu przy nominalnym 1 l/min podanym przez użytkownika;
nie jest pomiarem przepływomierza ani kalibracją pompki. Brak raportów daje Brak telemetrii dolewek.
API recordAtoDose przyjmuje wyłącznie poprawny, zakończony raport na właścicielu modelu, odrzuca duplikaty i nakładanie.
Widok nie uruchamia pompy. UART i fizyczna pompa nie są tu zintegrowane.
Zdarzenia tworzą jedną listę: ostrzeżenia/alarmy najpierw, neutralne wpisy później,
w obu częściach najnowsze na początku. Rozróżnienie jest wyłącznie wizualne: czerwone tło i pionowy akcent,
bez podpisów WAŻNE/ZWYKŁE i bez nagłówków sekcji. Na początek przewija listę.

System ma stałą listę czterech kategorii po lewej i jeden panel z podsumowaniami po prawej:

| Obsługa | Sieć i czas | Sterowanie | Zaawansowane |
|---|---|---|---|
| Ekran | Sieć Wi-Fi | Karmnik | Diagnostyka panelu |
| Interakcja | Zegar i czas | Termostat | Sterownik ESP8266 |
| PIN i dostęp | | Plan dnia | Dziennik zdarzeń |
| | | Mapa sprzętu | Firmware |
| | | | Zasilanie i restart |
| | | | Test czujników |

Wiersze mają wspólny font tytułu 20 px, podsumowanie 14 px i pojedynczą strzałkę, także w Zaawansowanych.
Dłuższa lista przewija się zamiast zmniejszać font. Sidebar nie ma dodatkowego AquaBox ani informacji o autozapisie.
Mapa sprzętu pod Sterowaniem ma ten sam układ: cztery kategorie po lewej i listę modułów po prawej.
Stan wiersza odświeża się; strzałka ze szczegółu modułu wraca do tej mapy, a nie do początku Systemu.
W szczegółach jest jedna strzałka powrotu, bez dodatkowego przycisku Wróć.
Kąt, pauza, wygaszanie, czas serwisu, cel i histereza otwierają edytor z suwakiem i dokładną korektą -/+.
Suwak ma zakres z walidatora (kąt 1–360°, pauza 1–3600 s, wygaszanie 0–60 min, gdzie 0 wyłącza automat).
Strzałka zatwierdza wartość i wraca; konfiguracja zapisuje się po zwłoce, nie w każdej klatce przeciągania.
Treść 326 px jest wycentrowana między nagłówkiem 56 px i nawigacją od y420: y75 bez paska stanu,
y88 z paskiem. W podzakładkach scrim 80% wygasza obie ramy: jasna mgła w dzień, ciemna w nocy.
Treść ustawień zachowuje pełny kontrast i wspólne powierzchnie, odstępy oraz typografię.
Karmnik: 1–360° na dawkę, czas pauzy oraz niezależny wybór filtra, powietrza, grzałki, CO2 i obu lamp.
ATO nie jest przełącznikiem pauzy, bo nie przypisano pompy; harmonogram karmienia pozostaje w Planie dnia.

Ekran: motyw i wygaszanie treści 0–60 min; 0 wyłącza automat. Pierwszy dotyk tylko wybudza.
Podświetlenie nie jest sterowane: DISP jest stałe, PWM niepodłączony. Czarny obraz nie wyłącza LED.
Interakcja: potwierdzenia działań i ograniczenie animacji. Nie ma udawanego dźwięku bez buzzeru.
PIN: własne cztery cyfry, sesja edycji 5 min, po pięciu błędach blokada 30 s.
PIN chroni lokalną edycję i akcje; nie szyfruje flash. Hasła Wi-Fi są przechowywane w zwykłym NVS.
Sieć: OFF / router / zabezpieczony AP AquaBox-Setup, edycja danych, ponowienie połączenia.
Połączenie STA jest asynchroniczne z limitem pierwszej próby 20 s; nie blokuje dotyku.
Czas: osobna karta bieżącego czasu/daty i źródła; po prawej wybór Automatyczna / Ręczna
oraz synchronizacja. Przycisk NTP uwzględnia bieżące połączenie z routerem, także jego zmianę
podczas oglądania ekranu; dostępny router nie jest dowodem pomyślnej synchronizacji NTP.
Edycja pięciu pól daty/godziny jest osobnym widokiem, z czytelnym zaznaczeniem i parą −/+.
Ustaw zegar jest potwierdzeniem operacji, nie przyciskiem zapisu formularza konfiguracji:
czas nie zmienia się przy każdej edycji cyfry. Ręczne ustawienie najpierw trwale wyłącza NTP;
błąd zapisu preferencji nie przestawia zegara. Zmiana dokładnie o minutę również aktualizuje model.
Walidacja obejmuje kalendarz 2024–2099, lata przestępne, nieistniejącą godzinę wiosenną DST
oraz podwójną godzinę jesienną: użytkownik wybiera wtedy czas letni albo zimowy.
DS3231 na WeMos nie jest jeszcze odczytywany, więc czas ręczny nie przetrwa utraty zasilania.
Mapa sprzętu otwiera szczegóły wszystkich 11 modułów.
Diagnostyka odświeża pamięć, GT911, licznik dotyku, NVS i czas pracy; test dotyku ma pięć pól.
Osobny Sterownik ESP8266 pokazuje stan transportu, TX/RX, ostatni ACK, opóźnienie i błędy.
Obecnie UART niepodłączony: brak danych nie jest zerem błędów ani udaną próbą komunikacji.
Podgląd diagnostyki i mapy nie wymaga odblokowania edycji PIN-em; akcje zmieniające stan nadal są chronione.
Firmware sprawdza integralność aktualnego obrazu przez ESP-IDF. Aktualizacja odbywa się USB; brak OTA.
Zasilanie: prawdziwy restart ESP32 i usunięcie wyłącznie klucza konfiguracji AquaBox.
Test czujników: normalny, offline, przegrzanie, karmienie, serwis, niski poziom i sprzeczne pływaki.
Nie ma aktywnego przycisku, który udaje kalibrację niepodłączonej sondy.

## Pamięć i zapis

Model domenowy, próbki, logi i bufory danych używają stałych rozmiarów.
Konfiguracja ma wersję, rozmiar, CRC i walidację pól. Nieprawidłowy zapis nie zatwierdza roboczych nastaw.
Wersja NVS 6 bezpiecznie odczytuje wersje 4 i 5 po sprawdzeniu CRC, zachowując sieć i nastawy.
Nowe pola karmnika otrzymują lokalne domyślne 90° i pauzę filtra/CO2; to nie kalibracja fizycznej dawki.
Brak konfiguracji nie jest błędem; uszkodzona konfiguracja daje domyślne nastawy i wpis diagnostyczny.
Autozapis jest łączony po okresie bezczynności; konfiguracja identyczna z zatwierdzoną nie zapisuje flash.
Podczas zapisu blokowane są kolejne zmiany, cofnięcie i zmiana zegara. Błąd pozostawia szkic
oraz poprzednie trwałe nastawy; Ponów ponawia próbę, Cofnij odrzuca niezapisane zmiany.
Nie ma nieskończonej pętli ponawiania błędu pamięci. PIN chroni również ponowienie zapisu,
a wygasła sesja nie powoduje automatycznego zatwierdzenia chronionej edycji.
Przed zapisem flash zatrzymywany jest RGB DMA/ISR, następnie odtwarzany jest sterownik i obraz.
To chroni tryb bounce-buffer z framebufferem PSRAM podczas wyłączenia cache przy NVS;
może spowodować krótką przerwę obrazu i wymaga odbioru fizycznego.
[Ograniczenia RGB ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32s3/api-reference/peripherals/lcd/rgb_lcd.html).

Piny i timing panelu nie zostały zmienione. GPIO19/20 nadal są wolne dla USB.
Nie zmieniono firmware CYD ani WeMos w tej iteracji.

## Rdzenie i budżet klatki

Rdzeń 1: przypięte zadanie aquabox-display jest jedynym właścicielem LVGL, RGB i GT911.
Rdzeń 0: aquabox-io wykonuje Wi-Fi, NTP, NVS, kontrolę obrazu i operacje czasu.
Sterownik Wi-Fi ma również własne zadania systemowe; nie oznacza to wyłączenia FreeRTOS z rdzenia UI.
Statyczne kolejki po cztery żądania i wyniki izolują oba zadania. UI nie czeka na radio/flash,
przepełnienie kolejki ma jawny błąd, a wynik zapisu nie może zostać zgubiony.
NVS używa handshake: tylko właściciel UI zatrzymuje i odtwarza RGB, a I/O zapisuje po potwierdzeniu.
Timeout anuluje zapis, jeżeli RGB nie został zatrzymany. Po awarii odtworzenia obrazu panel ponawia
inicjalizację bez cofania pomyślnie zapisanej konfiguracji. Krótka przerwa RGB przy zapisie pozostaje.
[LVGL i wątki](https://lvgl.io/docs/open/8.4/porting/os),
[przypinanie zadań ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32s3/api-reference/system/freertos_idf.html).

Odświeżanie LVGL co 30 ms daje cel 33,3 FPS. Timing RGB 16 MHz / (892 × 529) daje około 33,9 Hz.
Animacje zależą od upływu czasu, nie liczby pętli. Inwalidowane są tylko małe aktywne obszary.
Ograniczenie animacji wyłącza fale, ruch karmienia, przejścia i animacje powiadomień.
Test hosta mierzy 33–34 odświeżenia/s w aktywnych obszarach; nie jest pomiarem fizycznego LCD
ani gwarancją stałych 34 FPS podczas Wi-Fi/NVS. Pełny redraw przy zmianie karty ma większy koszt.
UART pozostaje niepodłączony i wyłączony; warstwa komunikacji ma właściciela na rdzeniu 0,
ale piny/protokół i rzeczywiste komendy WeMos wymagają osobnej integracji.

## Źródła i wcześniejsze decyzje

Pierwotne trzy kierunki w preview/src/exploration.cpp: Przekrój, Rytm, Obieg.
Wybrany Przekrój rozwinięto według uwag użytkownika, a nie skopiowano UI CYD.
Notatki obsidian oraz nowszy kod i uzgodnienia sprzętowe określają 11 modułów,
dwa pływaki, brak czujnika zalania, brak pompy ATO oraz ochronę grzałki 28,5°C.
Nie wprowadzono fikcyjnego pomiaru przepływu, wcześniejszej historii ani odczytu ADC/RTC.
[Weryfikacja](S3_PRZEKROJ_VERIFICATION.md) i [rzeczywiste rendery](S3_PRZEKROJ_GALLERY.md)
oddzielają test hosta, zapis firmware i odbiór na fizycznym panelu.


## Iteracja 11 — dopracowanie punktów 1–5

Panel używa nazwy pH WODY; surowe MIN/MAX są wyłącznie w szczegółach pływaków.
Obok karmienia są ostatnie zakończenie oraz następna dostępna pora z planu.
Brak wcześniejszego zakończenia nie jest uzupełniany fikcyjną godziną.

Dotknięcie nazwy/wartości parametru powiększa jego wykres z własną skalą i tym samym kursorem.
Strzałka wraca do porównania, następna zamyka pełny ekran. Nie ma drugiego przycisku powrotu.
Zakresy wody i ATO oraz warstwy są pamiętane w sesji UI, oddzielnie, bez zapisów do flash.

Zdarzenia zapisują własne daty/godziny i nie są przestawiane po korekcie zegara.
Powtórzenia tego samego komunikatu i poziomu w ciągu 60 s współdzielą wpis z nasycanym licznikiem;
przyczyny alarmów zachowują pierwszy/ostatni czas, stan aktywny i czas ustąpienia.
Aktywne/ważne wpisy są pierwsze, ustąpione przyczyny spokojniejsze, pozostałe neutralne.
Nieznany czas pozostaje --:--:--. Wyczyść dziennik jest wyłącznie w
System / Zaawansowane / Dziennik zdarzeń, za potwierdzeniem i ochroną PIN-u.

Cztery kompletne wiersze Sterowania mieszczą się bez ucięcia Mapy sprzętu.
Dłuższe listy mają pasek przewijania i pamiętają pozycję dla każdej kategorii.
Podstrony pokazują ścieżkę działu; termostat nie dziedziczy ścieżki ostatnio odwiedzonych ustawień.

Odstęp między rozpoczęciami dawek ma domyślnie 10 min, z suwakiem 1–60 min w Karmniku.
Blokada używa monotonicznego czasu: anulowanie, korekta zegara i zmiana konfiguracji
nie skracają już rozpoczętej blokady. Następna pora uwzględnia także sekundy do granicy minuty.
Ręczna dawka zastępuje zbyt bliską automatyczną; pominięcie jest logowane, bez późniejszego odrabiania.
Ostatnie zakończenie wymaga końca LocalModel albo kompletnego raportu Controller przy osiągniętym kącie,
nie końca pauzy. Nierozwiązany błąd karmnika pokazuje następne karmienie jako wstrzymane.
Ostatnia dawka, blokada i dziennik pozostają w RAM; po restarcie nie udaje się wcześniejszej telemetrii.
Rzeczywiste bezpieczeństwo po restartach/utracie łącza musi należeć do niezależnego sterownika wykonawczego.

Konfiguracja NVS v6 migruje v4/v5 po sprawdzeniu CRC. Migracja v5 zachowuje kąt i maskę pauzy,
dodając domyślny odstęp 10 min. Walidacja v6 obejmuje zakres odstępu 1–60 min.
Nie zmieniono HAL, okablowania, firmware WeMos/CYD ani nie podłączono UART.
