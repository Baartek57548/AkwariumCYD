---
aliases:
  - Stabilizacja CYD 2026-09-21
tags:
  - firmware
  - cyd
  - web
  - uart
  - maintenance
category: changelog
status: source-verified
---

# Stabilizacja CYD i uproszczenie konfiguracji — 2026-09-21

## Cel

- usunąć restarty CYD występujące podczas zmiany danych w panelu;
- usunąć nieużywaną funkcję czujnika zalania ze wszystkich aktywnych warstw;
- zachować działanie UART w tle oraz istniejące mechanizmy watchdog i fail-safe;
- usunąć aplikacje Flutter oraz nieużywane transporty i dodatkowe platformy;
- zakończyć weryfikację źródeł bez kolejnego wgrywania na ESP32 ani fizyczną kartę SD.

## Diagnoza restartu

W testach na fizycznym CYD znaleziono dwa niezależne źródła restartów:

1. Status `lastFaultReason = ui_heartbeat_stale` występował po około 60 sekundach. `WiFiClient::write()` w Arduino-ESP32 może wykonać dziesięć sekundowych prób dla każdego fragmentu; wiele małych fragmentów odpowiedzi `/api/status` blokowało pętlę UI aż do reakcji supervisora.
2. Po usunięciu blokady HTTP długi test ujawnił `abort()` na rdzeniu 0. Zdekodowany backtrace prowadził przez `_udp_recv` mDNS, `esp_log_writev`, `vprintf`, `uart_write` do `lock_init_generic`. Systemowy logger ESP-IDF próbował korzystać z UART0 zarezerwowanego dla protokołu CYD-AquaBox i wszedł w inicjalizację blokady stdout z zadania TCP/IP.

UART pozostaje w osobnym zadaniu I/O i nie został przeniesiony do obsługi HTTP.

## Wprowadzona poprawka

- usunięto ręczne `flush()` i `stop()` oraz ręczne przejmowanie gniazda klienta;
- fragmenty odpowiedzi są składane w statycznym buforze 8192 B, dzięki czemu typowy status jest wysyłany jednym zapisem treści;
- zakończenie odpowiedzi chunked i własność klienta pozostawiono klasie bazowej `WebServer`;
- heartbeat UI jest odświeżany bezpośrednio przed i po zapisie TCP;
- przed uruchomieniem Wi-Fi wyciszono logger ESP-IDF przez `esp_log_set_vprintf()` i `ESP_LOG_NONE`, aby UART0 służył wyłącznie protokołowi NDJSON;
- nie wydłużano timeoutu supervisora i nie wyłączano watchdog;
- nie zastosowano dynamicznego bufora, aby uniknąć fragmentacji sterty.
- `ota_portal_stream_file()` nie kopiuje już `WiFiClient` i nie wykonuje
  nieograniczonych zapisów; pliki SD przechodzą przez `FastWebServer`, porcje
  1360 B są buforowane i kończone kontrolowanym flush.

## Usunięcie nieużywanego czujnika

Funkcja została usunięta z firmware CYD i WeMos, protokołu UART, HTTP, panelu WWW, symulatorów, testów i zasobów SD. `GPB1` jest obecnie wejściem rezerwowym. Dawne bity zachowano jako nazwane rezerwy, aby nie przesuwać kolejnych pól i nie łamać zgodności zapisanych struktur oraz ramek.

Aktywnym zabezpieczeniem wodnym pozostaje pływak niskiego poziomu na `GPB2`; opisuje go [[Hardware - Czujnik Poziomu Wody]].

## Uproszczenie architektury

Decyzją operatora usunięto obie aplikacje Flutter, stos łączności krótkiego zasięgu, ESP-NOW, firmware P4/C6, duży HMI, powiązane biblioteki, projekty, testy, CI i dokumentację. Aktywny system składa się teraz z CYD, jego lokalnego ekranu, panelu WWW z karty SD oraz WeMos połączonego przewodowo przez UART.

Firmware korzysta wyłącznie z Wi-Fi do lokalnego WWW i OTA. Nieużywana rezerwa pamięci kontrolera radiowego jest zwalniana podczas startu. Usunięcie martwych modułów zmniejsza powierzchnię błędów, zależności i koszt utrzymania.

## Weryfikacja

- testy domeny CYD: 49/49;
- testy interfejsu LVGL: 5/5;
- testy API, kontraktu firmware i paczki WWW: 21/21;
- testy opcjonalnej bramy HTTPS: 17/17;
- walidator wydania, generator SBOM i kompilacja narzędzi Python: poprawne;
- `esp32dev-st7789`: 98 152 B RAM (30,0%), 1 576 961 B flash (80,2%);
- `cyd_2_8`: 96 792 B RAM (29,5%), 1 577 037 B flash (80,2%);
- `wemos_d1_uno`: 32 104 B RAM (39,2%), 358 476 B flash (34,3%);
- `git diff --check`: poprawny; lokalny `actionlint` pominięty z powodu braku
  kompilatora Go, ale pozostaje obowiązkowym krokiem workflow CI.

Panel WWW sprawdzono w Chrome przy viewport 390 x 844 px. Nie występuje poziome
przepełnienie, menu mobilne poprawnie otwiera się, przechodzi do widoku pomiarów
i zamyka. Widoczne wartości mają 24-27 px, a konsola przeglądarki nie zgłosiła
błędów.

## Status końcowy

2026-09-22 wgrano firmware `esp32dev-st7789` na wykryty CYD ST7789 przez
`COM7` (USB-SERIAL CH340). ESP32-D0WD-V3 został rozpoznany, bootloader i tablica
partycji przeszły weryfikację, a pełny obraz 1 583 696 B został zapisany i
zweryfikowany przez esptool; PlatformIO zakończyło zadanie statusem `SUCCESS`.
SHA-256 obrazu: `4BF4E9028E2439F623C41335B3F99DC66F56C9DA88964335F20A00517AEB90B2`.

Pakiet został skopiowany na kartę `E:` FAT32 (3,95 GB). Porównano SHA-256
wszystkich 244 plików źródłowych z kartą: brak brakujących plików i brak
niezgodności. Firmware na karcie ma ten sam SHA-256 co obraz wgrany na CYD,
a `version.txt` wskazuje ten hash. Źródła mają testy kontraktu, kompilację
`esp32dev-st7789`, `cyd_2_8` i `wemos_d1_uno`, a panel SD korzysta teraz z
ograniczonego transportu. W partycji aplikacji
pozostaje około 389 KB wolnego miejsca, więc gzipowany rdzeń `/aq/ota`
(ok. 140 KB) może być dodany jako awaryjny panel wbudowany. Pełne multimedia
splash (ok. 8,9 MB) pozostają poza partycją i nadal powinny być przechowywane
na SD.

## Błąd pustej strony i poprawka transportu — 2026-09-22

Zrzut Chrome pokazał pustą stronę pod `192.168.0.112`. Odtworzenie żądaniami
HTTP wykazało, że firmware zwracał `200 OK` z poprawnym `Content-Length`, ale
zamykał połączenie po pierwszym bloku 1024 B: HTML z SD deklarował 119 106 B,
a skompresowany HTML 17 765 B. `/api/status` również urywał się na około 1 KB.
Przeglądarka nie mogła sparsować niekompletnego dokumentu.

Zwiększono `SOCKET_WRITE_BUDGET_MS` z 1500 do 10000 ms, zgodnie z limitem prób
`WiFiClient` w Arduino-ESP32. Heartbeat interfejsu nadal jest odświeżany w
trakcie oczekiwania na możliwość zapisu do gniazda, a budżet pozostaje ograniczony.
Poprawka kompiluje się dla `esp32dev-st7789` (1 576 965 B flash); lokalny pakiet
SD i jego hash zostały zsynchronizowane z tym obrazem.

To był stan po pierwszej próbie, nie końcowe rozwiązanie. Kolejne obrazy zostały
poprawnie wgrane na `COM7` po ręcznym wejściu w tryb BOOT, ale zwiększenie
timeoutu nie naprawiło odpowiedzi. Próby z natywnym `WiFiClient::write()` oraz
zapisami porcjowanymi po 1024 B także nie przywróciły pełnej strony.

Wersja diagnostyczna ujawniła konkretny błąd: po nagłówku odpowiedzi pierwsza
porcja 1024 B treści może zwrócić 0 z `errno=11` (`EAGAIN`) po około 10 sekundach.
Zarówno HTML z SD, jak i generowany `/api/status` są wtedy niekompletne, więc
nie jest to sam błąd odczytu karty. Mały endpoint diagnostyczny działa.
2026-09-22 wgrano kolejny obraz z pomiarem czasu zapisów i próbą odpowiedzi
1–8 KiB; esptool zweryfikował hash obrazu. Po zapisie trwa oczekiwanie na zwykły
start CYD (zwolnienie BOOT i reset) przed pomiarami HTTP. Dopóki pełny HTML i
zasoby nie przejdą pomiaru na fizycznym urządzeniu, zadanie pozostaje otwarte.
Fizyczna karta SD jest w CYD, a nie w czytniku komputera; lokalny pakiet SD nie
został zsynchronizowany z tym diagnostycznym firmware.

## Audyt cyklu życia portalu — 2026-09-22

Po kolejnych testach widać, że samo zwiększanie timeoutu TCP nie usuwa problemu:
mały endpoint działa okresowo, ale po obciążeniu klient znika z sieci lub port
80 nie odpowiada mimo informacji o połączeniu Wi-Fi na ekranie. `FastWebServer`
odnotował `EAGAIN` na wysyłce treści. Wersję z nieblokującym `send()` i limitem
1,5 s wgrano; kompilacja i esptool potwierdziły poprawny obraz, ale po starcie
nie uzyskano stabilnego połączenia HTTP, więc nie wolno oznaczać naprawy jako
zweryfikowanej.

Przegląd kodu ujawnił dwie niezależne usterki współbieżności i stanu:

- zadanie I/O na rdzeniu 0 mogło uruchomić lub zamknąć `WebServer`/mDNS, gdy
  zadanie UI na rdzeniu 1 wykonywało `handleClient()` i wysyłało treść;
- `start_http_portal_common()` ustawiał `ota_portal_running=true` bez sprawdzenia,
  czy `WiFiServer::begin()` faktycznie przeszedł `bind()` i `listen()`. Biblioteka
  nie zwraca błędu z `begin()`, ale udostępnia stan nasłuchu przez `operator bool`;
- odczyt i zapis profilu Wi-Fi na SD z zadania I/O nie trzymały mutexu SD, choć
  serwowanie plików z zadania UI go używało.

W kodzie roboczym portal ma teraz żądany tryb `Off/Sta/AccessPoint` przekazywany
atomowo z zadania I/O; faktyczne `begin()/stop()/handleClient()` i mDNS/DNS
wykonywane są kolejno w zadaniu UI. Start nasłuchu jest sprawdzany, a nieudany
próbuje się ponownie po 5 s, bez fałszywego stanu „portal działa”. Operacje
profilu Wi-Fi na SD używają tego samego mutexu z limitem 20 ms, aby nie
blokować UART. Strumień pliku przerywa dalszy odczyt po błędzie wysyłki.

Obraz roboczy kompiluje się dla `esp32dev-st7789`, a test kontraktu firmware
przeszedł 7/7. Log tekstowy na UART0 pozostaje wyłączony, ponieważ UART0
obsługuje ciągłą transmisję NDJSON z AquaBox; diagnostyka resetów jest
zapisywana w NVS.

Obraz po refaktoryzacji wgrano na CYD przez COM7 dnia 2026-09-22; esptool
potwierdził hash i PlatformIO zakończyło się `SUCCESS` po 120,60 s. Po
zwolnieniu BOOT i RESET/EN użytkownik potwierdził, że ekran i zegar działają.
Komputer 192.168.0.108 jest w tej samej sieci Jamnik 2,4 GHz, ale port 80
urządzenia 192.168.0.112 nie przyjmuje połączeń (`curl`: timeout TCP). Ping
chwilowo odpowiedział (55–357 ms), a następnie zaczął gubić pakiety i zwracać
`DestinationHostUnreachable`. Oznacza to, że poprawny zapis firmware nie jest
równoznaczny z naprawą WWW. Nie potwierdzono pełnych transferów HTML, CSS, JS
ani `/api/status` i zadanie pozostaje otwarte. Fizyczna karta SD jest w CYD;
lokalny `sdcard/aq/ota/firmware.bin` jest starszym obrazem.

Ważne: napis `WWW: http://akwarium.local/` na ekranie jest ustawiany przez
`gui_app_update_wifi()` po samym połączeniu Wi-Fi, bez sprawdzenia nasłuchu
HTTP. Nie należy go interpretować jako potwierdzenia działającego portalu.

Kolejny obraz (1579497 B flash, 106400 B RAM statycznego) dodaje ponawianie
startu HTTP w zadaniu UI po wykryciu `WL_CONNECTED` oraz etykietę pokazującą
faktyczny nasłuch. Kompilacja i 23 testy API przeszły; wgrano go przez COM7
z wynikiem `SUCCESS` (125,30 s). Użytkownik widzi na zmianę stary adres i
`WWW: HTTP aktywny`. Po pierwszym zapytaniu diagnostycznym komputer odebrał
`200 OK` i `Content-Length: 174`, ale nie otrzymał pełnego JSON, a CYD według
użytkownika się przyciął. Następne zapytanie zwróciło cały 174-bajtowy JSON,
ale później TCP ponownie nie odpowiadał. Na życzenie bezpieczeństwa i ze
względu na mały RAM wstrzymano dalsze aktywne próby HTTP. Wniosek: serwer
może nasłuchiwać; awaria dotyczy wysyłki lub zasobów w trakcie obsługi żądania.

Użytkownik doprecyzował, że po żądaniu urządzenie zawiesiło się całkowicie i
musiał je ręcznie zrestartować. Nie wykonano kolejnych zapytań do sprzętu.
Przegląd mapy pamięci znalazł stały 15 KiB bufor DMA animacji startowej,
używany tylko podczas splash przy 8 FPS. Zmiana `RGB565_STREAM_ROWS` z 24 na 8
zmniejsza bufor do 5 KiB bez dynamicznej alokacji; rozmiar statycznej RAM
spadł z 106400 B do 96160 B (oszczędność 10240 B). Obraz kompiluje się i 24
testy API przechodzą. Ten obraz z oszczędnością pamięci NIE został jeszcze
wgrany ani zweryfikowany na CYD w chwili przygotowania. Etykieta WWW została
rozszerzona o liczbę wolnych kilobajtów sterty, aby kolejny pomiar nie wymagał
wielu zapytań HTTP. Następnie obraz wgrano na COM7 z `SUCCESS` i weryfikacją
hasha w 124,00 s. Oczekujemy na odczyt RAM i płynności ekranu po RESET/EN;
żadnych żądań HTTP po tym wgraniu jeszcze nie wysłano.
