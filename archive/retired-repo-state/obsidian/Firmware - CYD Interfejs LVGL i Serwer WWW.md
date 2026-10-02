---
aliases:
  - CYD GUI LVGL
  - Serwer WWW CYD
tags:
  - firmware
  - cyd
  - lvgl
  - web
category: firmware
status: partial-hardware-verification
last_reviewed: 2026-09-23
hardware_validation: incomplete
---

# 🖥️ Firmware — Interfejs Dotykowy LVGL i Serwer WWW

Pliki źródłowe: `firmware/cyd_controller/src/gui_app.cpp` oraz moduły webowe.
ESP32 CYD udostępnia dwa równoległe interfejsy użytkownika: lokalny ekran dotykowy TFT oraz zdalny panel WWW (PWA) w sieci domowej.

Powrót do: [[MOC - Firmware ESP32 CYD]] | Sprzęt: [[Hardware - ESP32 CYD 2432S028R]]

---

## 📱 Ekran Dotykowy (LVGL 8.4 + LovyanGFX)
- **Układ Kafelek**:
  - Kafelek Główny: Aktualna temperatura dużą czcionką, wykres słupkowy pH, status grzałki.
  - Kafelek Przekaźników: 8 przycisków typu toggle z ikonami stanu (załączony/wyłączony).
  - Kafelek Harmonogramów: Edycja godzin włączenia/wyłączenia świateł i CO2.
  - Kafelek Ustawień: Wprowadzanie temp. zadanej i histerezy za pomocą klawiatury ekranowej.
- **Odświeżanie**: Ekran aktualizowany jest reaktywnie natychmiast po odebraniu nowej ramki telemetrii z WeMos.

---

## 🌐 Serwer WWW i REST API
CYD po podłączeniu do domowej sieci Wi-Fi udostępnia panel dostępny pod adresem IP sterownika:
- `GET /api/status` — Zwraca pełny stan telemetrii w formacie JSON.
- `POST /api/relay` — Przełączanie kanałów przekaźników.
- `POST /api/temp` — Zmiana temperatury docelowej.
- `POST /api/feed` — Zdalne wywołanie karmienia ryb.

### Serwowanie plików SD i ograniczenie TCP

Produkcyjny obiekt `ota_http_server` korzysta ze standardowego `WebServer`. Jego cykl życia (`begin()`, `stop()` i `handleClient()`) pozostaje własnością zadania UI; zadanie I/O jedynie atomowo publikuje żądany tryb portalu. Bieżący kandydat firmware wysyła pliki SD przez `ota_portal_stream_file()` z jednym statycznym buforem **1024 B**.

Na użytej wersji Arduino-ESP32 okno wysyłkowe TCP ma około **5760 B**. `WiFiClient::write()` może przejść do dziesięciu długich prób `select()` i zwrócić zapis częściowy. Wcześniejsza pętla po takim wyniku urywała body mimo nagłówka z pełnym `Content-Length`, co dokładnie odpowiada błędowi przeglądarki `ERR_CONTENT_LENGTH_MISMATCH`.

Kandydat używa ograniczonego czasowo zapisu `send(..., MSG_DONTWAIT)`: ponawia wyłącznie niesłaną część bieżącego bufora, oddaje czas Wi-Fi co 4 ms i kończy po 1,5 s bez postępu. Po kompletnej porcji nadal wykonuje rzeczywiste opóźnienie **8 ms** (`vTaskDelay`). Dzięki temu nie ma wielosekundowego blokowania zadania UI ani dynamicznej alokacji, a licznik `/api/transport-health` ujawnia każdy przerwany transfer.

> [!WARNING] Stan weryfikacji
> Pełne sekwencyjne transfery kilku zasobów potwierdzają wyłącznie poprawę ścieżki transportowej dla kontrolowanego, pojedynczego klienta. Nie są dowodem stabilności panelu w zwykłej sesji przeglądarki. Jedno normalne otwarcie panelu ujawniło `ERR_CONTENT_LENGTH_MISMATCH`; nowy bounded writer jest skompilowany, lecz wymaga jeszcze wgrania i testu sprzętowego.

### Tryb WEB i lokalny ekran

- zegar LVGL jest aktualizowany stale, natomiast bieżąca **wersja robocza** ogranicza pełny render i obsługę dotyku w aktywnym trybie WEB do maksymalnie jednego przebiegu na **500 ms** (2 FPS);
- aktywność WWW jest rozpoznawana od pierwszego żądania zasobu statycznego, nie dopiero po uruchomieniu JavaScriptu panelu; dzięki temu ekran nie konkuruje z pobieraniem HTML, CSS i JS;
- podczas aktywności WWW ekran przechodzi wyłącznie do zakładki **WiFi**. Pozostałe lokalne panele nie są odświeżane, a operator zachowuje dostęp do funkcji przerwania trybu WWW;
- w połączeniu STA aktywność sesji wygasa po 20 s bez żądania lub heartbeat; podłączona stacja punktu dostępowego utrzymuje tryb WEB do rozłączenia. Odświeżanie kontrolek ekranu jest ograniczone do 2 s;
- transmisja UART CYD–AquaBox nadal działa na rdzeniu I/O; pojedyncza ramka NDJSON jest wysyłana jednym `Serial.write()` razem ze znakiem końca linii.

Ograniczenie 2 FPS jest świadomym kompromisem: priorytetem jest zwolnienie czasu procesora i magistrali dla Wi-Fi/TCP, a nie płynność lokalnej animacji podczas korzystania z panelu. Wariant został wgrany, ale pełna stabilizacja strony wymaga jeszcze wgrania nowszego bounded writera i fizycznej weryfikacji opisanej w [[2026-09-23 - Weryfikacja HTTP CYD i wdrożenie]].

### Kontrolowana weryfikacja transportu — 2026-09-23

Po wgraniu wariantu `esp32dev-st7789` na COM7 sprawdzono sekwencyjnie zasoby skompresowane gzipem. Test nie uruchamiał równoległych żądań i nie zastępuje testu normalnej przeglądarki:

| Zasób | `Content-Length` | Wynik |
| --- | ---: | --- |
| `/` | 17 765 B | HTTP 200, pełny transfer |
| `/style.css` | 28 735 B | HTTP 200, pełny transfer |
| `/app-init.js` | 3 855 B | HTTP 200, pełny transfer |

Do zamknięcia problemu konieczne są: pełne załadowanie panelu w normalnej przeglądarce, przejście przez kluczowe widoki bez restartu CYD, brak urwanych zasobów oraz potwierdzenie, że urządzenie nadal odpowiada po zakończeniu sesji. Szczegółowy zapis wdrożenia i następnej weryfikacji: [[2026-09-23 - Weryfikacja HTTP CYD i wdrożenie]].

---

## 🔗 Powiązane Notatki
- [[Hardware - ESP32 CYD 2432S028R]]
- [[Firmware - CYD UartClient i Telemetria]]
- [[Protokół - Katalog Komend i Sterowanie]]
- [[2026-09-23 - Weryfikacja HTTP CYD i wdrożenie]]
