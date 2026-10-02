# AquaBox — weryfikacja aktualna na 2.10.2026

## Najnowsza zmiana: kontrast motywów

- Wszystkie sześć palet działa niezależnie dla trybu jasnego i ciemnego.
- Testy palet: 258 kontroli PASS, w tym czytelność tekstu, rozdział kart od
  tła, kontrast zaznaczeń oraz niezależny wybór pory dnia.
- Scenariusz `themes-v2.txt`: PASS; testuje wybór i trwałość motywu oraz
  eksportuje ekrany LVGL RGB565 800 × 480.
- Build firmware ESP32-S3: SUCCESS; RAM 135 664 B (41,4%), flash 1 455 311 B
  (22,2%). Zmiana kontrastu nie była jeszcze wgrywana na płytkę.
- Wynik hostowy nie kalibruje jasności podświetlenia ani nie gwarantuje, że
  fizyczny LCD będzie wyglądał identycznie. Obraz i dotyk były wcześniej
  potwierdzone; nowe palety wymagają osobnego odbioru na ekranie.

Poniższa część opisuje wcześniejszą weryfikację iteracji 11 i pozostaje
zachowana jako historia, nie jako wynik bieżącego firmware.

---

# Archiwalna weryfikacja — 28.09.2026, iteracja 11

Wdrożone punkty 1–5: prostszy Panel, powiększanie parametru z zachowaniem kursora,
pamięć zakresów/warstw, datowane i łączone zdarzenia z ustąpieniem przyczyny,
pełne wiersze Systemu/ścieżki oraz ostatnie/następne karmienie i odstęp między dawkami.

## Wyniki aktualnej wersji

| Sprawdzenie | Wynik | Granica wniosku |
|---|---|---|
| Model | 134 sprawdzenia, PASS | Logi/przyczyny, odstęp monotoniczny, sekundy granicy minuty, anulowanie, rollover, ręczna/automatyczna dawka; lokalny model |
| Status / runtime | 24 / 36 sprawdzeń, PASS | NVS v4/v5→v6, zachowanie kąta/maski/sieci i trwały odstęp; host nie emuluje fizycznego flash |
| Plan / palety / czas historii | 19 / 32 / 19 sprawdzeń, PASS | Zachowane regresje, własne skale i zapisane godziny |
| Scenariusze UI | Wszystkie 16 PASS, 3 575 interakcji | Rzeczywisty LVGL RGB565 800×480, nie physical LCD |
| refinement-v11 | 60 interakcji, PASS | Panel, blokada powtórnej dawki, powiększenie pH/EC/temp. bez zmiany próbki, pamięć widoków, daty/powtórzenia/ustąpienie, pełna mapa, ścieżka termostatu po Wi-Fi, suwak odstępu, reload, czyszczenie tylko przez Zaawansowane |
| history-v11 | 1 467 interakcji, PASS | Zachowane granice historii, wspólny kursor i 100 cykli powiększenia/porównania/statystyk/ATO |
| Cykle wykresów | 100; LVGL 8 949 B przed i po | Bez przyrostu śledzonej alokacji hosta |
| Cykle nawigacji / gestów | 100 / 100; 11 922 B przed i po | Nie fizyczne FPS |
| Cykle powiadomień | 100; 10 956 B przed i po | Ważne powiadomienia zachowane; brak popupów powodzenia zapisu |
| Kompilacja ESP32-S3 | SUCCESS, bez ostrzeżeń/błędów w logu | RAM statyczny 134 352 B; flash programu 1 278 995 B |
| Upload COM10 | SUCCESS, hashe segmentów potwierdzone, reset RTS | Zapis firmware, nie odbiór nowego UI na urządzeniu |

264 różnych sprawdzeń jednostkowych, powtarzanych przed scenariuszami.
16 scenariuszy i trzy zachowane eksploracje: PASS. Szczyt śledzonej alokacji LVGL: 25 575 B.
Host nadal mierzy 33 klatki/s dla wody/pauzy i 16 klatek/500 ms dawki; nie jest to pomiar fizycznego LCD.

Obraz firmware.bin: 1 279 392 B.
SHA-256: 69BFE544DAFAE694D785837F3B3DA5D31C0670E194007537530FB088B663CC49.
Logi: firmware/esp32s3_ui/preview/output/verify-v11.log, s3-build-v11.log, s3-upload-v11.log.
Odtwarzanie z repozytorium: tools/verify_s3_ui.ps1; tools/s3_ui.ps1 build; tools/s3_ui.ps1 upload -Port COM10.

## Ograniczenia i odbiór

UART do WeMos nadal niepodłączony. LastFeedingSource odróżnia LocalModel od Controller;
zakończenie lokalnej animacji nie jest potwierdzeniem fizycznego karmienia.
Domyślny odstęp 10 min jest zabezpieczeniem przed bliskim ponowieniem, nie zaleceniem dawki pokarmu.
Suwak ma 1–60 min; rozpoczęta blokada zachowuje pierwotny termin mimo zmiany konfiguracji/anulowania.
Korekta zegara nie skraca blokady; zbyt bliska automatyczna dawka nie jest odrabiana.
Ostatnia dawka i blokada są w RAM; po restarcie panel nie zna wcześniejszej dawki.
Po integracji UART niezależny sterownik musi zapewnić ochronę/rejestr operacji również przy restartach i utracie łącza.

Zdarzenia zachowują własną godzinę, pierwsze/ostatnie wystąpienie i ustąpienie przyczyny.
Nieznany czas to --:--:--; nowy zegar nie przepisuje starego wpisu.
Pierścień ma nadal 24 wpisy; liczniki są nasycane, a stare rekordy nadpisywane.
Powtórzenia tego samego komunikatu/poziomu do 60 s są łączone; przyczyny alarmów są rozróżniane flagami.
Ważność pozostaje wizualna, bez literalnych kategorii. Czyszczenie wymaga potwierdzenia i obowiązującego PIN-u.

Zakresy wody/ATO i warstwy są pamiętane tylko w sesji; nie ma dodatkowych zapisów NVS przy nawigacji.
Historia nadal pozostaje w RAM, a wykres grzałki ma bieżące, nie historyczne progi.
Nie wdrażano punktu 6, trwałego archiwum ani historycznych nastaw.
Nie zmieniono HAL, okablowania, firmware CYD/WeMos, pomp ani pinów UART.
Krótka przerwa RGB przy autozapisie NVS i stałe podświetlenie pozostają wcześniejszymi ograniczeniami.

Odbiór fizyczny tej iteracji pozostaje do potwierdzenia: obraz/dotyk po uploadzie, czytelność obu palet,
powiększenie/powrót wykresu, zdarzenia, karmienie/blokada i ustawienia po restarcie.
Poprzednie potwierdzenie użytkownika dotyczyło obrazu i dotyku iteracji 9.
Nie odczytywano wielokrotnie portu ani nie zmieniano HAL na podstawie wcześniejszego pojedynczego błędu I²C.

[Projekt](S3_PRZEKROJ_DESIGN.md) i [aktualne rendery LVGL](S3_PRZEKROJ_GALLERY.md).

---

## Archiwalny raport iteracji 10

### Weryfikacja iteracji 10 — wcześniejsza wersja

Historia: tylko temperatura na pierwszym ekranie, jeden wybór zakresu w pełnym widoku,
niezależne pasy pH/EC/grzałki, wspólny kursor i własne skale.
Godzina z sekundami jest zapisywana z każdą próbką; liczba próbek i min/max są pod Szczegóły.
Przetestowano nieznany zegar, północ, korekty oraz brak retroaktywnego dopisywania godzin.
Poprzednie zmiany Systemu, menu, suwaków i palet pozostają w pełnym zestawie regresji.

UART do WeMos nie jest podłączony. Nie uruchomiono fikcyjnego odbioru, pompy ATO
ani komend przekaźników. Diagnostyka transportu jawnie pokazuje brak połączenia i danych.

## Wyniki aktualnej wersji

| Sprawdzenie | Wynik | Granica wniosku |
|---|---|---|
| Model domenowy | 107 sprawdzeń, 0 błędów | Serwis, minimalne światło, ochrona, histereza, archiwum 72 h, bufory zdarzeń, walidacja i szacunki ATO; nie fizyczne wyjścia |
| Provider statusu | 24 sprawdzenia, 0 błędów | Klasyfikacja Wi-Fi/RSSI/czasu |
| Runtime panelu | 32 sprawdzenia, 0 błędów | Host: zapis, błąd, migracja, sieć, kalendarz i DST; nie fizyczne NVS/radio |
| Plan dnia | 19 sprawdzeń, 0 błędów | Przedziały dobowe, północ, pozycja i granice markera |
| Palety | 32 sprawdzenia, 0 błędów | Jasność powierzchni i kontrast tekstów obu niezależnych palet |
| Godziny historii | 19 sprawdzeń, 0 błędów | Sekundy, daty, północ, rok, dzień przestępny, nieważne dane, DST/korekta i ograniczone buforowanie tekstu |
| ux-v9 | 83 interakcje, PASS | Dawka/pauza, autozapis, Ponów, restart UI, SSID/hasło, czas, test dotyku i stan wody |
| edge-v9 | 93 interakcje, PASS | Łączenie zmian, blokada podczas zapisu, stare NVS po błędzie, Cofnij, DST, PIN i reset |
| animation-v9 | 155 interakcji, PASS | Animacje, granice etykiet, alarmy, powiadomienia i tryb ograniczonego ruchu |
| navigation-v9 | 41 interakcji, PASS | Nowa kolejność gestów, granice, brak przypadkowych akcji, blokada edycji/dialogu/klawiatury |
| header-v9 | 23 interakcje, PASS | SSID/RSSI, AP, off, północ, niezależność Wi-Fi od UART |
| settings-v9 | 95 interakcji, PASS | 11 modułów, normy, AP/OFF po restarcie, wygaszanie, diagnostyka i restart |
| lifecycle-v9 | 19 interakcji, PASS | Koniec karmienia/serwisu przy otwartym dialogu, suwak czasu serwisu i usunięcie nieaktualnych akcji |
| system-v9 | 31 interakcji, PASS | Kategorie, jednolite fonty, zmiana kąta, maska pauzy, palety, zegar i diagnostyka |
| plan-v9 | 10 interakcji, PASS | Rzeczywiste piksele wspólnego markera, północ i stany teraz |
| history-v10 | 1564 interakcje, PASS | Temperatura bez szumu, ukryte statystyki, zakres, niezależne warstwy/skale/kursor, dokładne zapisane godziny, nieznany czas, północ, korekta, ATO i 100 cykli wykresów/warstw/szczegółów |
| feeding-v9 | 13 interakcji, PASS | Pozycja bieżącego zadania, brak feedbacku, timeout i potwierdzenie błędu; raporty hosta, nie UART |
| palette-v9 | 21 interakcji, PASS | Jasny/ciemny Panel, System, skupienie na podstronie, klawiatura i karmienie |
| system-refinement-v9 | 47 interakcji, PASS | Kolejność menu, brak duplikatów i popupów zapisu, ESP8266 bez UART, mapa/powrót, suwaki i granice, zapis po zatwierdzeniu oraz odczyt po restarcie |
| Cykle dotykowe | 100 cykli, 606 interakcji, PASS | Śledzona alokacja LVGL 11 899 B przed i po |
| Cykle przesuwania | 100 cykli w obie strony, 809 interakcji, PASS | Śledzona alokacja LVGL 11 899 B przed i po, kontrola każdej karty |
| Cykle powiadomień | 100 cykli, PASS | Śledzona alokacja LVGL 11 001 B przed i po; wymiana, wygaszanie i zamknięcie |
| Cykle wykresów | 100 cykli, PASS | Śledzona alokacja LVGL 8 931 B przed i po; temperatura+pH+EC+grzałka, szczegóły, zakres i ATO |
| Kompilacja ESP32-S3 | SUCCESS, brak ostrzeżeń w logu | RAM statyczny 133 696 B; flash programu 1 273 179 B; bufory dynamiczne oddzielnie |
| Upload COM10 | SUCCESS, hashe wszystkich segmentów potwierdzone, reset RTS | ESP32-S3 USB 303A:1001, MAC 10:20:BA:4B:B6:84; zapis nie potwierdza fizycznego wyglądu/dotyku |
| Poprzedni odbiór sprzętowy | Użytkownik potwierdził działający obraz i dotyk iteracji 9 | Nie potwierdza odbioru nowej Historii iteracji 10 |

233 różnych sprawdzeń jednostkowych, powtarzanych przed scenariuszami.
12 scenariuszy v9 i nowa Historia v10: 2 195 interakcji, w tym 100 cykli wykresów i 100 cykli powiadomień.
Dwa scenariusze cyklicznej nawigacji: 1 415 interakcji. Łącznie 3 610 interakcji.
Wszystkie 15 scenariuszy i trzy zachowane eksploracje: PASS.
Szczyt śledzonej alokacji LVGL: 22 215 B. To metryki hosta, nie całego ESP32.

W aktywnych obszarach host wyrenderował: woda 33 klatki/1000 ms, karmienie 16/500 ms,
pauza 33/1000 ms. Ograniczony ruch: identyczne piksele wody, 1 odświeżenie/1000 ms.
Nie są to pomiary FPS fizycznego wyświetlacza.

Obraz firmware.bin: 1 273 584 B.
SHA-256: AFA213BCF2C4529FD8C0B510B7DA16E55A44DE0C019EABFBB8B08FBA8AD7647E.
Log kompilacji: firmware/esp32s3_ui/preview/output/s3-build-v10.log.
Log wgrania: firmware/esp32s3_ui/preview/output/s3-upload-v10.log.
Pełny przebieg hosta: firmware/esp32s3_ui/preview/output/verify-v10.log.

## Odtwarzanie

Z katalogu głównego:

```powershell
./tools/verify_s3_ui.ps1
./tools/s3_ui.ps1 build
./tools/s3_ui.ps1 upload -Port COM10
```

Dwa pierwsze kroki nie wgrywają urządzenia. Pakiety S3 są izolowane od CYD.
Logi hosta znajdują się w firmware/esp32s3_ui/preview/output.
Scenariusze przerywają na pierwszym błędzie; starsze skrypty nie są aktywnym zestawem.
[Galeria](S3_PRZEKROJ_GALLERY.md) wskazuje aktualne rendery Historii v10 i niezmienionych ekranów v9, starsze pliki zachowano.

## Granice potwierdzenia

Telemetria i przełączniki wykonawcze pozostają lokalnym modelem, bo użytkownik potwierdził brak UART.
Historia zapisuje odczyty modelu, nie rzeczywistych ADC. Długie przebiegi w testach są fixture hosta;
urządzenie nie tworzy wcześniejszej historii ani samodzielnych raportów ATO.
Archiwum w RAM zaczyna się od startu i ma stałe limity; restart kasuje historię, nie konfigurację.

Objętość ATO to przybliżenie z czasu pracy przy założeniu 1 l/min użytkownika.
Brak danych lub sprzeczne pływaki tworzą przerwę, nie zerowy pomiar.
Pełny wykres temperatury pokazuje bieżące progi histerezy; nie przechowuje historycznych nastaw.
Chronologia używa czasu od startu. Pole 4 B zapisuje dodatkowo lokalną datę/godzinę/sekundę znaną przy pobraniu.
Nieznany zegar daje --:--:--; ustawienie czasu nie przepisuje archiwum. Korekta/DST przerywa łączenie próbek.
Osobne skale pH/EC nie są normalizowanym overlayem sugerującym wspólne jednostki.
Wybrana dolewka poza nowym zakresem ustępuje najnowszej dostępnej w nim dawce;
test potwierdza, że skrócenie zakresu nie wyświetla fałszywego Brak danych.
Godziny pochodzą z zegara panelu/manual/NTP, a wartości nadal z modelu, nie fizycznych sond.

Panel obsługuje prawdziwe NVS, STA/AP, NTP, zegar ręczny, restart i kontrolę obrazu.
Host testuje ich logikę na izolowanych danych, nie wydajność radia/flash.
Własne zakresy nie są rekomendacjami hodowlanymi; ochrona 28,5°C jest osobną regułą.
Odczyty ACK, opóźnienia, błędów i wersji ESP8266 wymagają podłączonego UART oraz integracji protokołu.

Piny i timing RGB/GT911 nie zmieniły się. Jeden właściciel LVGL na rdzeniu 1;
Wi-Fi/NTP/NVS na rdzeniu 0. UART jest wyłączony, nie zgaduje się jego GPIO.
NVS używa namespace aquabox / config; reset nie kasuje całego NVS i nie restartuje WeMos.
RGB DMA/ISR zatrzymuje się przed zapisem flash i jest odtwarzane po zapisie;
możliwa krótka przerwa obrazu. Host nie emuluje tego handshaku.

Wygaszanie treści nie wyłącza podświetlenia; pierwszy dotyk tylko wybudza.
PIN/hasła w zwykłym NVS nie są szyfrowane. OTA i RTC na WeMos nie są zintegrowane.
Nie zmieniono firmware CYD/WeMos ani okablowania.

Użytkownik potwierdził działający obraz i dotyk poprzedniej iteracji 9. Odbiór iteracji 10 pozostaje do wykonania.
Pełny odbiór fizyczny do wykonania: nowe menu i suwaki, kontrast dzień/noc, duży wykres,
karmienie/pauza i powrót AUTO, obraz po autozapisie NVS, nastawy po restarcie i STA/AP.
Nie potwierdzono fizycznych FPS ani wielogodzinnej stabilności tej iteracji.

Krótki odczyt COM10 po wgraniu poprzedniej iteracji 9 zawierał błąd I²C dotyku:
i2c_master_transmit_receive / ESP_ERR_INVALID_STATE (259), przy czasie pracy 110 847 ms.
W kolejnym 5-sekundowym odczycie nie było nowych logów. Nie ustalono przyczyny;
brak powtórzenia w krótkim oknie nie potwierdza stabilności. Użytkownik osobno potwierdził sprawny dotyk.
Istniejący sterownik ogranicza utrzymanie naciśnięcia bez danych do 90 ms,
a po 10 kolejnych nieudanych odczytach oznacza GT911 niedostępny i ponawia wykrywanie co 3 s.
Nie zmieniano HAL ani okablowania na podstawie jednego zdarzenia.
[Projekt](S3_PRZEKROJ_DESIGN.md), [aktualne rendery](S3_PRZEKROJ_GALLERY.md).
