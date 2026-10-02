---
aliases:
  - Weryfikacja HTTP CYD 2026-09-23
tags:
  - firmware
  - cyd
  - web
  - http
  - verification
category: changelog
status: in-progress
date: 2026-09-23
---

# Weryfikacja HTTP CYD i wdrożenie — 2026-09-23

## Stan w skrócie

Warstwa transportu HTTP została częściowo ustabilizowana: pojedyncze, sekwencyjne pobrania HTML, CSS i JavaScript zakończyły się pełnym transferem. Nie jest to jednak odbiór końcowy. Podczas rzeczywistego korzystania z panelu w przeglądarce użytkownik nadal zgłasza restart CYD lub niedoładowanie strony, dlatego stabilizacja WWW ma status **w toku**.

Powrót do: [[Firmware - CYD Interfejs LVGL i Serwer WWW]] | [[MOC - Firmware ESP32 CYD]]

---

## Zmiana transportu sprawdzona na urządzeniu

W testowanym obrazie portal korzysta ze standardowego `WebServer`. Pliki z SD są przesyłane przez `ota_portal_stream_file()` w jednym statycznym buforze 1360 B. Po każdej kompletnej porcji wykonywane jest `vTaskDelay(8 ms)`.

Uzasadnienie: użyteczne okno wysyłkowe TCP ma około 5760 B. Szybkie wysłanie pięciu porcji po 1360 B mogło je wypełnić przed potwierdzeniami ACK i spowodować krótki zapis z `WiFiClient::write()`. Pacing 8 ms ogranicza nacisk na stos Wi-Fi bez powiększania stałego bufora lub wprowadzania dynamicznej alokacji pamięci.

Obraz został wgrany do `ESP32-D0WD-V3` na `COM7` (`USB-SERIAL CH340`). Esptool zapisał 1 580 992 B i potwierdził integralność komunikatem `Hash of data verified`, po czym wykonał `Hard resetting via RTS`.

## Wynik kontrolowanego testu transportu

Test przeprowadzono jednym klientem, kolejno, z obsługą gzip. Poniższe dane potwierdzają kompletność trzech konkretnych odpowiedzi, a nie wydajność wielozasobowej sesji przeglądarki.

| Zasób | `Content-Length` | Wynik | Czas pomiaru |
| --- | ---: | --- | ---: |
| `/` | 17 765 B | HTTP 200, pełny transfer | 0,453 s |
| `/style.css` | 28 735 B | HTTP 200, pełny transfer | 0,338 s |
| `/app-init.js` | 3 855 B | HTTP 200, pełny transfer | 0,256 s |

Wcześniej `/style.css` urywał się na 6800 B mimo poprawnego nagłówka `Content-Length`; po wprowadzeniu opóźnienia 8 ms transfer CSS zakończył się poprawnie. To jest silny dowód naprawy konkretnego problemu backpressure, ale nie dowód braku innych wąskich gardeł.

## Otwarte ryzyko: normalne użycie przeglądarki

Po kontrolowanych pomiarach użytkownik zgłosił, że przy korzystaniu ze strony nadal występuje restart urządzenia albo strona nie ładuje się w całości. Możliwe różnice względem testu sekwencyjnego to jednoczesne pobieranie wielu zasobów, żądania API po inicjalizacji JavaScriptu oraz współzawodnictwo renderingu TFT z zadaniem obsługi portalu.

Nie należy wykonywać długiego testu obciążeniowego ani wielokrotnie odświeżać strony równolegle, dopóki nie zostanie sprawdzona kolejna, oszczędniejsza dla UI wersja. CYD ma ograniczony budżet pamięci, a powtarzanie obciążających prób po restarcie może utrudnić diagnozę.

## Kandydacka optymalizacja WEB focus

W bieżącym kodzie roboczym przygotowano i pomyślnie skompilowano optymalizację. Obraz oczekuje na wgranie i test fizyczny:

1. Aktywność WWW ma być wykrywana już przy pierwszym żądaniu zasobu statycznego, przed uruchomieniem JavaScriptu panelu.
2. Gdy klient WWW jest aktywny, ekran CYD ma pokazywać wyłącznie zakładkę **WiFi**.
3. Lokalny render LVGL oraz obsługa dotyku mają być ograniczone do maksymalnie 2 FPS (jeden przebieg na 500 ms), aby priorytet dostały Wi-Fi i TCP.
4. Zegar LVGL i krytyczne zadania bezpieczeństwa pozostają aktywne; nie jest wyłączany watchdog ani transmisja UART CYD–AquaBox.

To celowo poświęca płynność lokalnego HMI na czas obsługi klienta WWW. W połączeniu STA interfejs lokalny powinien wrócić do normalnego rytmu po 20 s bez żądania lub heartbeat; przy podłączonym kliencie punktu dostępowego tryb WEB trwa do rozłączenia klienta.

## Wdrożenie minimalnego UI — wynik wstępny

Kandydat z ograniczonym UI został wgrany na `COM7`. Esptool zapisał 1 581 088 B obrazu aplikacji, potwierdził `Hash of data verified` i wykonał `Hard resetting via RTS`.

Pierwsze żądanie wysłane około 12 s po resecie nie uzyskało odpowiedzi; po pełnym starcie urządzenie odpowiadało na ICMP. Następnie, bez równoległych żądań, zmierzono:

| Zasób | `Content-Length` | Wynik | Czas pomiaru |
| --- | ---: | --- | ---: |
| `/` | 17 765 B | HTTP 200, pełny transfer | 0,265 s |
| `/style.css` | 28 735 B | HTTP 200, pełny transfer | 0,885 s |

To potwierdza działanie podstawowego HTTP po wdrożeniu. Nie zastępuje jeszcze sprawdzenia jednej normalnej sesji przeglądarki, która pozostaje wymagana do zamknięcia problemu.

## Wynik zwykłej przeglądarki i korekta transportu

Jedno zwykłe otwarcie panelu po powyższych pomiarach zakończyło się `ERR_CONTENT_LENGTH_MISMATCH`. CYD nadal odpowiadał na ICMP, więc w tej próbie nie potwierdzono restartu, ale kompletność odpowiedzi HTTP nie została zachowana.

Przyczyna w kodzie: `ota_portal_stream_file()` najpierw wysyła `Content-Length` równy rozmiarowi pliku, a poprzednio po częściowym wyniku `WiFiClient::write()` przerywał transfer. Pojedynczy `curl` rzadziej wywołuje tę ścieżkę; przeglądarka uruchamia wiele pobrań CSS, JavaScriptu i API, podczas gdy standardowy `WebServer` obsługuje jeden request synchronicznie.

Następny obraz został już skompilowany (RAM 87 544 B, 26,7%; flash 1 574 849 B, 80,1%) i oczekuje na wgranie. Zawiera on:

1. statyczny bufor 1024 B;
2. ograniczony czasowo zapis `send(..., MSG_DONTWAIT)` z retry tylko niesłanej części bufora;
3. heartbeat oraz zwalnianie zadania co 4 ms przy backpressure;
4. limit 1,5 s bez postępu zamiast możliwego wielosekundowego blokowania `WiFiClient::write()`;
5. diagnostykę `/api/transport-health` z licznikiem ukończonych i przerwanych transferów.

Po wgraniu należy wykonać jedno otwarcie przeglądarki bez odświeżeń równoległych, a następnie odczytać ten endpoint. Jeśli nadal będą gubione zasoby, kolejnym krokiem będzie ograniczenie początkowej liczby assetów (bundle CSS/JS), ponieważ `WebServer` ma jednego aktywnego klienta i backlog 4.

## Następna bezpieczna weryfikacja

1. Kompilacja `esp32dev-st7789` zakończyła się powodzeniem: RAM 87 864 B (26,8%), flash 1 574 349 B (80,1%).
2. Obraz został wgrany na COM7 z potwierdzeniem `Hash of data verified` oraz `Hard resetting via RTS`.
3. Po pełnym starcie sprawdzono pojedyncze zasoby `/` i `/style.css`; ich pobrane rozmiary zgadzają się z `Content-Length`.
4. Otwórz panel w jednej zwykłej przeglądarce, sprawdź kompletność strony i przejścia po kluczowych widokach bez wymuszania równoległych reloadów.
5. Potwierdź brak restartu, pełne pobieranie zasobów oraz dalszą odpowiedź urządzenia po zamknięciu sesji.

Kryterium zamknięcia: panel musi działać stabilnie w normalnej sesji przeglądarki, a nie tylko zwracać kompletne odpowiedzi w pojedynczym pomiarze `curl`.

## Powiązane notatki

- [[Firmware - CYD Interfejs LVGL i Serwer WWW]]
- [[MOC - Firmware ESP32 CYD]]
- [[Procedura - Programowanie i Wgrywanie Firmware]]
- [[2026-09-21 - Stabilizacja WWW i usuniecie czujnika zalania]]
