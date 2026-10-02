# AquaBox — ESP32-S3 N16R8 + Waveshare 4,3″

LVGL 8.4.0, 800×480, osobny kod i zasoby od UI CYD.
Karty Panel / Plan dnia / Historia / Serwis / System; gesty poziome i monochromatyczne dolne ikony.
Panel: pomiary z graficznymi zakresami, jakościowa animacja wody, priorytetowy stan akwarium.
Pomiary po lewej; stan akwarium, woda i karmienie w jednym kontenerze po prawej.
Bez przycisku Serwisu na Panelu; dolna karta Serwis ma konturową ikonę klucza.
Ważne powiadomienia wysuwane z góry i płynnie wygaszane, zamykane dotknięciem;
uwzględniają ograniczenie animacji i nie przebudowują treści. Zapis i oczekiwanie na zapis nie generują popupów.
Serwis: jedna obowiązkowa lampa, druga opcjonalna, pozostałe wyjścia OFF na starcie, automatyczny powrót.
Autozapis konfiguracji po 1,2 s bez kolejnej zmiany, bez przycisków Zapisz.
NVS z wersją/CRC/walidacją; błąd zachowuje szkic i poprzednie trwałe nastawy, daje Ponów/Cofnij.
Karmienie: jeden dotyk, animacja kąta/pozycji, osobna pauza; ustawienia dawki i wyłączanych modułów w Systemie.
Bez UART pozycja pochodzi z jawnego modelu lokalnego; API raportów nie oznacza działającej komunikacji.
Plan dnia ma wspólną linię obecnego czasu i bieżące stany. Historia: Parametry wody / Dolewki ATO / Zdarzenia.
Parametry wody na pierwszym ekranie pokazują wyłącznie temperaturę i godziny próbek.
Dotknięcie otwiera pełny ekran. Jeden przycisk Zakres wybiera 1/12/24/48/72 h,
a pH, EC i Grzałka jednym dotknięciem dołączają osobne, zsynchronizowane pasy ze wspólnym kursorem.
Różne jednostki mają osobne skale. Próbki i min/max są ukryte pod Szczegóły, bez instrukcji Dotknij.
Grzałka dołącza bieżącą histerezę przerywaną i historię ON/OFF; ATO: pływaki, dawki i odstępy.
Szacunek ilości ATO przy 1 l/min jest oznaczony jako przybliżony. Bez raportów brak fikcyjnych dolewek.
Ważne zdarzenia są na początku i wyróżnione wizualnie, bez nagłówków kategorii.
System: Obsługa / Sieć i czas / Sterowanie / Zaawansowane. Mapa sprzętu w Sterowaniu używa
tego samego układu kategorii i list. Zaawansowane zawiera oddzielną diagnostykę ESP8266/UART.
Kąt karmnika, pauza, wygaszanie, czas serwisu i termostat mają edytory suwakowe z dokładną korektą.
Suwak zmienia szkic, strzałka powrotu zatwierdza wartość; nie zapisuje flash podczas przeciągania.
Treść wycentrowana; rama wygaszona do 20% widoczności we wszystkich podstronach.
Motywy wybierane niezależnie dla trybu jasnego i ciemnego w System → Obsługa → Motywy interfejsu.
AquaBox zachowuje oryginalne panele; dodatkowe palety to Matrix, Ocean, Koral, Grafit i Bursztyn.
Każda ma wariant jasny i ciemny oraz podgląd na kafelku. Tryb Jasny/Ciemny/Auto nadal ustawia się w Ekran.
Ustawienia NVS v10 przechowują dwa identyfikatory motywów. Migracja v9 zachowuje dotychczasowy nazwany
motyw dla obu trybów; wcześniejsze ręczne nadpisania RGB zastępuje jego gotową paletą.
Czas ma osobny odczyt i edytor pięciu pól,
świadome Ustaw zegar, walidację kalendarza oraz obu przypadków DST.
NVS v6 migruje prawidłową konfigurację v4/v5 bez kasowania istniejących nastaw.

Rzeczywiste funkcje panelu: Wi-Fi STA/AP/OFF, czas ręczny/NTP, NVS, restart, GT911, wygaszanie
treści i kontrola integralności firmware. UART niepodłączony: telemetria i wyjścia to lokalny model,
opisany w diagnostyce/serwisie. Radio domyślnie OFF. Wi-Fi nie potwierdza łącza WeMos.
Kalibracja niepodłączonych sond oraz OTA nie są udawane.
Historia w RAM: 64 próbki co 15 s, archiwum 865 próbek co 5 min, 1024 zmiany grzałki,
1024 zmiany poziomów i 512 zakończonych dolewek; dziennik 24 wpisy. Bufory mają stały limit.
Archiwum zaczyna się od uruchomienia, nie tworzy wcześniejszych danych; braki i sprzeczności są przerwami.
Chronologia historii używa czasu od startu; każda próbka zapisuje dodatkowo znaną wówczas lokalną
datę/godzinę z sekundami. Zmiana zegara nie przepisuje archiwum. Nieznany czas ma --:--:--,
a korekta zegara przerywa linię. Zakresy wielodniowe mają również daty na osi.
Zakresy pomiarów są nastawami użytkownika, nie uniwersalnymi normami akwarium.
Wygaszanie daje czarną treść, nie wyłącza podświetlenia.

## Kompilacja i wgrywanie

Z głównego katalogu repozytorium:

```powershell
./tools/verify_s3_ui.ps1
./tools/s3_ui.ps1 build
./tools/s3_ui.ps1 upload -Port COM10
```

Arduino 3.3.8 / ESP-IDF 5.5.4; 8 MB OPI PSRAM. Izolowane pakiety .pio/packages-s3.
Upload używa automatycznego resetu. Ręczny BOOT potrzebny tylko, jeśli automatyczne wejście nie działa.
Build, hash-zweryfikowany upload i odbiór wizualny są osobnymi potwierdzeniami.

## Panel sprzętowy

RGB565 D0–D15: 15,16,17,18,21,9,10,11,12,13,14,4,5,6,7,8.
PCLK1 / HSYNC2 / VSYNC39 / DE40. GT911 SDA41 / SCL42 / RST47; IRQ nieużywany.
GPIO19/20 pozostają wolne dla USB. Timing 16 MHz; h4/40/48, v4/32/13, ujemne PCLK.
Jeden framebuffer RGB565 w PSRAM, dwa bufory bounce po 16 KB, mały bufor LVGL w SRAM.
Rdzeń 1: LVGL/RGB/GT911, jeden właściciel UI. Rdzeń 0: kolejka I/O, Wi-Fi/NTP/NVS/czas/firmware.
Statyczne zadania: stos UI 12 KB, stos I/O 8 KB; kolejki po cztery elementy.
UART nie uruchomiono bez podanych pinów i połączenia z WeMos.
Cel odświeżania LVGL: co 30 ms (33,3 FPS), RGB około 33,9 Hz. Brak fizycznego pomiaru FPS.
Przed zapisem NVS sterownik RGB jest zatrzymywany i odtwarzany po zapisie;
możliwa krótka przerwa obrazu. Sprawdzić fizycznie zapis ustawień oraz ponowne uruchomienie.

LCD wejście 5V wymaga 5 V; logika 3,3 V. Nie uznawać zasilania wejścia 5V z 3V3 ESP
za zatwierdzone na podstawie wcześniejszego pliku użytkownika.
DISP pozostaje aktywne, PWM niepodłączony — brak regulacji podświetlenia.

Projekt: docs/S3_PRZEKROJ_DESIGN.md. Wyniki: docs/S3_PRZEKROJ_VERIFICATION.md.
Ta iteracja nie zmienia firmware CYD/WeMos ani przypisania GPIO.


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
