# Raport z Badania Firmware ESP32 CYD i Podsystemu OTA

**Autor:** `explorer_survey_2`  
**Data:** 2026-09-18  
**Cel:** Kompleksowa analiza architektury oprogramowania ESP32 CYD (środowisko `cyd_2_8`), istniejących mechanizmów OTA, obsługi sieci STA / AP fallback (`AquaCYD-OTA`), architektury interfejsu LVGL oraz opracowanie planu wdrożenia kontrolek OTA ("Aktualizuj Panel (CYD)" oraz "Aktualizuj AquaBox (WeMos)").

---

## 1. Analiza Środowiska PlatformIO `cyd_2_8`

### 1.1. Konfiguracja w `platformio.ini`
Plik `platformio.ini` w głównym katalogu projektu definiuje środowisko `[env:cyd_2_8]`:

```ini
[cyd_firmware]
platform = platformio/espressif32@7.0.1
board = esp32dev
framework = arduino
monitor_speed = 115200
upload_speed = 115200
board_build.partitions = min_spiffs.csv
lib_extra_dirs = firmware/shared, firmware/cyd_controller/lib

lib_deps =
    uart_protocol
    lvgl/lvgl@8.4.0
    lovyan03/LovyanGFX@1.2.21
    paulstoffregen/OneWire@2.3.8
    h2zero/NimBLE-Arduino@2.5.0
    bblanchon/ArduinoJson@^6.21.4

build_flags =
    -D LV_CONF_INCLUDE_SIMPLE
    -D CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1
    -I firmware/cyd_controller/include
    -I firmware/shared/uart_protocol/include
    -finput-charset=UTF-8
    -fexec-charset=UTF-8

build_src_filter = -<*> +<cyd_controller/src/*>

[env:cyd_2_8]
extends = cyd_firmware
build_flags =
    ${cyd_firmware.build_flags}
    -D AQUARIUM_FORCE_DEVELOPER_MODE=0
    -D AQUARIUM_ALLOW_UNSIGNED_ARDUINO_OTA=0
    -D CYD_PANEL_ST7789=0
    -D CORE_DEBUG_LEVEL=1
```

### 1.2. Kluczowe Obserwacje Dotyczące Środowiska
1. **Układ partycji (`min_spiffs.csv`):**
   - Zapewnia dwie symetryczne partycje aplikacji: `app0` (ota_0, ~1.88 MiB / 1966080 B) oraz `app1` (ota_1, ~1.88 MiB), małą partycję SPIFFS (~190 KiB), partycję NVS (20 KiB) oraz partycję stanu OTA `otadata` (8 KiB).
   - Maksymalny rozmiar pojedynczego obrazu binarnego firmware wynosi **1 966 080 bajtów** (~1.875 MB).
2. **Flaga `-D AQUARIUM_ALLOW_UNSIGNED_ARDUINO_OTA=0`:**
   - W domyślnym profilu produkcyjnym `cyd_2_8` bezwarunkowo wyłącza usługę ArduinoOTA (`gui_app.cpp:10796`: *"OTA: raw ArduinoOTA disabled; only signed .aqfw packages are accepted."*).
   - Aby umożliwić bezpośrednie wgrywanie z PlatformIO (zgodnie z wymaganiem R2 Follow-up), flaga ta musi zostać włączona (bądź powiązana z dedykowanym trybem serwisowym z hasłem uwierzytelniającym).
3. **Model Wielowątkowości FreeRTOS (Dual-Core):**
   - **Core 1 (Pętla główna Arduino):** Odpowiada za renderowanie UI i obsługę zdarzeń dotykowych: `service_lvgl()`, `hal_display_loop_cb()`, `lv_timer_handler()`.
   - **Core 0 (Zadanie I/O `aquarium_io`):** Przypięte zadanie FreeRTOS (`runtime_controller.cpp:337`, stos 16 KB, priorytet 2): obsługuje `g_uart_client.update()`, `gui_app_service_background()`, polling magistral sprzętowych oraz serwer HTTP/OTA.
   - **Synchronizacja:** Wszystkie wywołania LVGL lub współdzielonego stanu pomiędzy rdzeniami muszą chronić się muteksem rekurencyjnym `gui_app_lock(timeout_ms)` / `gui_app_unlock()` (klasa pomocnicza `GuiMutexGuard`).

---

## 2. Istniejące Mechanizmy OTA i Serwer Web

### 2.1. Aktualizacja Web OTA (Podpisany Pakiet `.aqfw`)
W obecnym kodzie obsługa aktualizacji przez przeglądarkę zrealizowana jest w `gui_app.cpp` (linie 9854–10003):
- Serwer HTTP `ota_http_server` (klasa `WebServer` na porcie 80) posiada zarejestrowaną trasę:
  ```cpp
  ota_http_server.on("/update", HTTP_POST, ota_portal_handle_update_finish, ota_portal_handle_update_upload);
  ```
- **Wymóg rozszerzenia `.aqfw`:**
  ```cpp
  if (!normalized_filename.endsWith(".aqfw")) {
      ota_portal_set_update_error("Wybierz podpisany plik .aqfw");
      return;
  }
  ```
- **Weryfikacja kryptograficzna (`secure_ota.cpp`):**
  - Oczekuje 512-bajtowego nagłówka `OtaPackageHeader`.
  - Weryfikuje podpis RSA-3072 kluczem publicznym `FirmwareTrust::RSA3072_PUBLIC_KEY_PEM`.
  - Sprawdza zgodność wersji bezpieczeństwa z monotonicznym licznikiem w NVS (`ota_guard_minimum_security_version()`).
  - Weryfikuje skrót SHA-256 strumieniowanego obrazu binarnego.
  - Zapisuje dane do nieaktywnej partycji za pośrednictwem biblioteki `Update` (`Update.begin(bytes, U_FLASH)`, `Update.write()`, `Update.end()`).
- **Nadzorca Integralności (`ota_guard.cpp`):**
  - Monitoruje poprawność pierwszych rozruchów po aktualizacji (okno walidacji 30 sekund).
  - W razie awarii (crash loop) automatycznie wycofuje bootloader do poprzedniej sprawnej partycji (`esp_ota_mark_app_invalid_rollback_and_reboot()`).

### 2.2. ArduinoOTA
- Kod rejestracji `ArduinoOTA` znajduje się w `gui_app.cpp` (linie 10737–10779):
  - Ustawia hostname `Secrets::OTA_HOSTNAME` (`"akwarium"`).
  - Ustawia hasło z `device_credentials_ota_ap_password()`.
  - W hooku `onStart()` przełącza sterownik w tryb bezpieczny (`force_safe_service_outputs()`).
  - W hooku `onEnd()` odnotowuje restart i wywołuje `hal_mcp_latch_all_relays_safe()`.
- W pętli `gui_app_service_background()` wywoływane jest `ArduinoOTA.handle()`, jednak wyłącznie pod warunkiem `wifi_ota_active && AQUARIUM_ALLOW_UNSIGNED_ARDUINO_OTA`.

---

## 3. Łączność Wi-Fi: STA i AP Fallback (`AquaCYD-OTA`)

### 3.1. Rejestracja i Magazyn Danych Dostępowych
1. **NVS:** Główny magazyn konfiguracji Wi-Fi obsługiwany przez `wifi_credential_store.cpp` (`wifi_credential_store_load_latest`).
2. **Karta SD (Fallback profilu):** Katalog `/aq/wifi/profile_*` zawierający pliki konfiguracyjne ze zrzutami SSID/hasła.
3. **Sekwencja Autopołączenia:**
   - Podczas startu urządzenia (`gui_app_init()`), flaga `wifi_autoconnect_pending = !cfg.modemSleep` inicjuje próbę połączenia w tle na Core 0.
   - Jeśli router domowy jest w zasięgu i połączenie zostanie nawiązane, podnoszona jest usługa mDNS (`http://akwarium.local/`) oraz `start_sta_service_portal()`.

### 3.2. Rozbieżność w Nazewnictwie AP Fallback
- W pliku `firmware/cyd_controller/include/config.h` (linia 175):
  ```cpp
  constexpr char OTA_AP_SSID[] = "cydAkwarium-OTA";
  ```
- W pliku `gui_app.cpp` (linia 14499):
  ```cpp
  lv_obj_t *ota_ssid_lbl = create_label(ota_card, LV_SYMBOL_WIFI "  SSID: cydAkwarium-OTA", ...);
  ```
- **Wymaganie Follow-up R2:** Wyraźnie definiuje nazwę punktu dostępowego jako **`AquaCYD-OTA`**.  
  *Rekomendacja:* Należy ujednolicić stałą w `config.h` do `"AquaCYD-OTA"` i zaktualizować etykiety w UI.

### 3.3. Zachowanie w Przypadku Braku Sieci STA
- Obecnie w `wifi_check_timer_cb()` po wyczerpaniu limitu czasu łączenia (`status == WL_CONNECT_FAILED` lub timeout 15 s) kontroler jedynie raportuje błąd tekstowy na etykiecie statusu i pozostaje w trybie rozłączonym.
- **Brak automatycznego przełączenia w AP:** W obecnej implementacji wejście w AP wymagało ręcznego kliknięcia przycisku "OTA" (`btn_ota`) i autoryzacji PIN.
- Aby spełnić wymaganie automatycznego fallbacku: gdy wyzwalany jest tryb aktualizacji (lub w trybie serwisowym OTA), kontroler musi najpierw spróbować połączyć się z domowym STA (np. przez 10–15 s), a w przypadku niepowodzenia automatycznie podnieść SoftAP `AquaCYD-OTA` (IP: 192.168.4.1).

---

## 4. Architektura Interfejsu LVGL na ESP32 CYD

### 4.1. Parametry Sprzętowe Wyświetlacza i Pamięci
- **Ekran:** 320x240 px, kontroler ILI9341 (SPI 40 MHz), dotyk oporowy XPT2046 (SPI 1 MHz).
- **Zasoby RAM:** Brak zewnętrznego PSRAM. Wewnętrzna pamięć DRAM po inicjalizacji LVGL i drzewa obiektów pozostawia zaledwie ~40–60 KB wolnej sterty.
- **Strategia Zarządzania Pamięcią UI:**
  W projekcie zastosowano rygorystyczny model dynamicznej alokacji stron:
  - W pamięci utrzymywana jest **wyłącznie jedna aktywna strona**.
  - Przy przełączaniu zakładek (`switch_to_page()`) stara strona jest natychmiast niszczona za pomocą `delete_active_page()` (`lv_obj_del()`), a jej wskaźniki resetowane.
  - Substrony (panele modalne / nakładki) są tworzone w kontenerze `subpage_*` (wysokość 210 px, y=30) i niszczone przy powrocie przyciskiem "Wstecz".

### 4.2. Struktura Nawigacji
Dolny pasek nawigacyjny (`build_nav_bar()`, wysokość 35 px, y=205) posiada 5 zakładek:
1. **Index 0: "Start" (`build_home_page`)** — Podsumowanie parametrów akwarium, kafelki skrótów, przycisk natychmiastowego karmienia oraz kafelek akcji:
   ```cpp
   create_home_action_card(pages[0], 160, 49, 156, 39, "SERWIS", "Sterowanie reczne", color_water_cyan, service_tile_cb, nullptr, nullptr);
   ```
2. **Index 1: "Plan" (`build_schedules_page`)** — Harmonogramy oświetlenia, filtra i napowietrzania.
3. **Index 2: "Moduly" (`build_optional_page`)** — Kafelki sensorów i modułów opcjonalnych (CO2, EC, wyciek, pływak itp.).
4. **Index 3: "Wykres" (`build_charts_page`)** — Wizualizacja historii temperatury, pH, LDR i pamięci RAM.
5. **Index 4: "System" (`build_system_page`)** — Siatka 8 kafelków (2 kolumny x 4 wiersze):
   - `WiFi` (x=4, y=4) -> `ActiveSubpage::Wifi`
   - `Ekran` (x=166, y=4) -> `ActiveSubpage::Screen`
   - `Logi` (x=4, y=48) -> `ActiveSubpage::Logs`
   - `Czas` (x=166, y=48) -> `ActiveSubpage::Clock`
   - `Diag` (x=4, y=92) -> `ActiveSubpage::Diagnostics`
   - `Zasilanie` (x=166, y=92) -> `ActiveSubpage::Power`
   - `Audio` (x=4, y=136) -> `ActiveSubpage::Sounds`
   - `Sprzet` (x=166, y=136) -> `ActiveSubpage::Hardware`

### 4.3. Istniejąca Podstrona WiFi (`ActiveSubpage::Wifi`)
Podstrona WiFi tworzona w `build_subpages()` (linie 14332–14520) zawiera strukturę paneli przełączanych za pomocą flagi `LV_OBJ_FLAG_HIDDEN`:
- `wifi_main_panel`: Główny widok z kartą parametrów połączenia (`wifi_info_card`) oraz przyciskami:
  - `btn_sta` ("Sieci" — wyszukiwanie i łączenie ze stacją),
  - `btn_ota` ("OTA" — dotychczasowe włączanie trybu AP),
  - `btn_disconnect` ("Rozłącz").
- `wifi_sta_panel`: Lista zeskanowanych sieci Wi-Fi.
- `wifi_pwd_panel`: Wprowadzanie hasła Wi-Fi z klawiaturą ekranową.
- `wifi_ota_panel`: Widok aktywnego portalu OTA (informacja o SSID AP, IP `192.168.4.1`, haśle i przycisk "Zatrzymaj OTA").

---

## 5. Projekt i Rekomendacja Dodania Kontrolek OTA

Wymaganie R2 określa:
> Dedykowany ekran / kafelek serwisowy w interfejsie LVGL na wyświetlaczu dotykowym:
> - Przycisk **"Aktualizuj Panel (CYD)"** — przełącza CYD w dedykowany tryb OTA / restartuje z flagą OTA.
> - Przycisk **"Aktualizuj AquaBox (WeMos)"** — wysyła komendę UART do WeMos nakazującą podniesienie sieci OTA i wyświetla na ekranie CYD instrukcję: nazwę sieci Wi-Fi i adres IP WeMos do wpisania w przeglądarce.

### 5.1. Analiza Opcji Lokalizacji Kontrolek w UI

| Wariant | Umiejscowienie | Zalety | Wady / Uwagi |
| :--- | :--- | :--- | :--- |
| **Wariant A (Rekomendowany)** | **Rozbudowa podstrony WiFi (`subpage_wifi`)** w zakładce *System* | Naturalne miejsce zarządzania łącznością i OTA. Użytkownik widzi status bieżącej sieci domowej oraz ma bezpośredni dostęp do wyzwalania OTA dla obu modułów. | Wymaga przeprojektowania dolnej części `wifi_main_panel` lub dodania dedykowanego widoku wyboru celu aktualizacji. |
| **Wariant B** | **W podstronie Trybu Serwisowego (`subpage_service`)** dostępnej z kafelka "SERWIS" na ekranie *Start* | Idealnie wpisuje się w semantykę "trybu serwisowego". Wejście w ten tryb już teraz wymusza bezpieczny stan urządzeń (`force_safe_service_outputs()`). | Kafelek na ekranie Start jest obecnie zorientowany na ręczne testowanie wyjść i odtwarzacz muzyki; może być mniej intuicyjny dla aktualizacji sieciowej niż zakładka System. |
| **Wariant C** | **Dedykowany kafelek "Aktualizacja (OTA)" w zakładce *System*** | Pełna izolacja procedury aktualizacji od codziennych ustawień Wi-Fi. Bardzo czytelna architektura. | Wymaga modyfikacji siatki 8 kafelków (np. połączenia kafelków lub reorganizacji menu System). |

### 5.2. Szczegółowy Projekt UI (Wariant A / B z panelem modalnym instrukcji)

1. **Przycisk "Aktualizuj Panel (CYD)":**
   - **Działanie:** 
     1. Wyświetla okno dialogowe z potwierdzeniem: *"Czy przełączyć panel w tryb aktualizacji firmware? Przekaźniki zostaną wyłączone."*
     2. Po potwierdzeniu: przełącza przekaźniki w tryb bezpieczny (`hal_mcp_latch_all_relays_safe()`).
     3. Jeśli CYD jest połączony z domowym Wi-Fi: pozostaje w trybie STA, aktywuje ArduinoOTA oraz Web OTA (`/update`) i wyświetla instrukcję z adresem URL: `http://<IP_STA>/update` lub `http://akwarium.local/update`.
     4. Jeśli brak połączenia z domowym Wi-Fi: podnosi SoftAP `AquaCYD-OTA` z adresem `192.168.4.1`, uruchamia Captive Portal i serwer Web OTA, wyświetlając instrukcję połączenia z siecią `AquaCYD-OTA`.
     5. Alternatywnie (dla maksymalnej niezawodności RAM): zapisuje flagę rozruchową OTA w pamięci NVS / RTC i wykonuje kontrolowany restart do minimalnego, ultralekkiego środowiska OTA, w którym wolny RAM wynosi >200 KB.

2. **Przycisk "Aktualizuj AquaBox (WeMos)":**
   - **Działanie:**
     1. Sprawdza, czy WeMos jest online (`g_uart_client.isControllerOnline()`). Jeśli nie – wyświetla ostrzeżenie o braku komunikacji UART.
     2. Wysyła przez UART rozkaz `enter_ota` do WeMos (`g_uart_client.sendEnterOta()`).
     3. Oczekuje na ramkę ACK lub telemetrię z flagą `ota_mode=true`.
     4. Przełącza ekran CYD w dedykowany widok instrukcji serwisowej:
        - Nagłówek: **Aktualizacja AquaBox (WeMos)**
        - Status: *Tryb serwisowy WeMos aktywny (przekaźniki bezpieczne)*
        - Połączenie Wi-Fi:
          - Jeśli WeMos połączył się ze stacją domową: *Adres IP w sieci domowej: `http://<IP_WEMOS>/update`*
          - W trybie awaryjnym AP: *Połącz się z siecią Wi-Fi: **`AquaBox-OTA`**, otwórz w przeglądarce: `http://192.168.4.1/update`*
        - Narzędzie PlatformIO: *Port sieciowy do wgrywania: `<IP_WEMOS>`*
        - Automatyczny licznik czasu (watchdog 5 minut): *Automatyczny powrót za: MM:SS*
        - Przycisk: *Zakończ / Wróć* (wysyłający rozkaz zakończenia trybu serwisowego lub restartu).

---

## 6. Integracja z Protokołem UART NDJSON

Dla realizacji powyższego przepływu biblioteka współdzielona `firmware/shared/uart_protocol` oraz `firmware/cyd_controller/src/uart_client.cpp` wymagają następujących rozszerzeń:

1. **W `uart_protocol.h`:**
   - Dodanie typu komendy w `CommandType`:
     ```cpp
     enum class CommandType : uint8_t {
         ...
         EnterOta,
         ExitOta
     };
     ```
   - Dodanie flagi w `AquariumTelemetry`:
     ```cpp
     struct AquariumTelemetry {
         ...
         bool ota_mode;    // Informacja o wejściu w tryb serwisowy OTA
     };
     ```
2. **W `uart_protocol.cpp`:**
   - Obsługa serializacji i deserializacji komendy `"cmd": "enter_ota"`.
   - Serializacja potwierdzenia `{"type":"ack","cmd":"enter_ota","ok":true,"ip":"..."}`.
3. **W `uart_client.cpp` (CYD):**
   - Implementacja metody:
     ```cpp
     bool UartClient::sendEnterOta() {
         AquaProtocol::AquariumCommand cmd;
         cmd.type = AquaProtocol::CommandType::EnterOta;
         return sendCommand(cmd);
     }
     ```
   - Blokada wysyłania zbędnych zapytań cyklicznych w trakcie flashowania WeMos, aby nie zakłócać odbiornika UART.

---

## 7. Weryfikacja Budowania Projektu PlatformIO (`cyd_2_8`)

Zgodnie z protokołem zbadano środowisko kompilacji za pomocą polecenia `pio run -e cyd_2_8`:
- **Polecenie:** `pio run -e cyd_2_8` w katalogu głównym projektu `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium`.
- **Wynik kompilacji:** `[SUCCESS] Took 244.80 seconds` (kod wyjścia 0).
- **Zużycie pamięci (PlatformIO Size Checker):**
  - **RAM (DRAM):** `27.0%` (użyte **88 632** bajtów z 327 680 bajtów dostępnych). Pozostawia to ~239 KB dla sterty systemowej, alokacji buforów graficznych LVGL oraz stosów zadań FreeRTOS.
  - **Flash:** `83.6%` (użyte **1 644 353** bajtów z 1 966 080 bajtów dostępnych w partycji `app0` / `app1`). Oznacza to bezpieczny margines ~321 KB na rozbudowę kodu OTA i UI.
- **Weryfikacja pakietów:**
  - Platforma: `platformio/espressif32@7.0.1` (`toolchain-xtensa-esp32@8.4.0`, framework Arduino 3.20017).
  - Biblioteki skompilowane bez ostrzeżeń: `lvgl@8.4.0`, `LovyanGFX@1.2.21`, `OneWire@2.3.8`, `NimBLE-Arduino@2.5.0`, `ArduinoJson@6.21.6`.
- **Artefakty binarne:** Wygenerowano kompletny zestaw plików w `.pio/build/cyd_2_8/`: `firmware.elf`, `firmware.bin` oraz mapę symboli `firmware.map`.


---

## 8. Szczegółowy Plan Wdrożenia (Roadmap)

### Faza 1: Ujednolicenie Parametrów Sieciowych i Bezpieczeństwa OTA
1. W `firmware/cyd_controller/include/config.h`:
   - Zmiana `Secrets::OTA_AP_SSID` z `"cydAkwarium-OTA"` na `"AquaCYD-OTA"`.
2. W `platformio.ini`:
   - Weryfikacja flagi `AQUARIUM_ALLOW_UNSIGNED_ARDUINO_OTA` pod kątem środowiska docelowego lub aktywacja ArduinoOTA z hasłem w trybie serwisowym OTA.

### Faza 2: Rozszerzenie Protokołu UART NDJSON
1. W `firmware/shared/uart_protocol/include/uart_protocol.h`:
   - Dodanie `CommandType::EnterOta` oraz `bool ota_mode` do struktury telemetrii.
2. W `firmware/shared/uart_protocol/src/uart_protocol.cpp`:
   - Dodanie parsowania i serializacji `"enter_ota"`.
3. W `firmware/cyd_controller/src/uart_client.cpp`:
   - Dodanie `sendEnterOta()`.

### Faza 3: Implementacja Trybu OTA na WeMos (AquaBox)
1. W `firmware/wemos_controller/src/main.cpp`:
   - Obsługa komendy `enter_ota`:
     - Wymuszenie stanu bezpiecznego przekaźników (grzałka, CO2, oświetlenie wyłączone).
     - Wstrzymanie regularnej telemetrii UART.
     - Wybudzenie Wi-Fi z głębokiego uśpienia (`WiFi.forceSleepWake()`).
     - Próba połączenia z domowym STA; w przypadku błędu start SoftAP `AquaBox-OTA` (192.168.4.1).
     - Uruchomienie Web OTA (`ESP8266WebServer` z endpointem `/update`) oraz `ArduinoOTA`.
     - Uruchomienie 5-minutowego watchdoga bezczynności przywracającego normalny tryb pracy.
   - Obsługa wejścia sprzętowego (przycisk boot na wolnym pinie WeMos, np. GPIO12 / D6).

### Faza 4: Interfejs Użytkownika LVGL na CYD
1. W `firmware/cyd_controller/src/gui_app.cpp`:
   - Wzbogacenie `wifi_main_panel` (lub `subpage_service`) o dedykowane przyciski:
     - Przycisk **"Aktualizuj Panel (CYD)"** ze wsparciem dla automatycznego fallbacku `AquaCYD-OTA`.
     - Przycisk **"Aktualizuj AquaBox (WeMos)"**.
   - Utworzenie karty/modalnego widoku instrukcji serwisowej wyświetlającej SSID (`AquaBox-OTA`), adres IP (`192.168.4.1` lub IP w sieci domowej) oraz odliczanie czasu sesji OTA.

### Faza 5: Aktualizacja Dokumentacji Obsidian
1. W `obsidian/Procedura - Programowanie i Wgrywanie Firmware.md`:
   - Uzupełnienie instrukcji o procedurę bezprzewodowej aktualizacji OTA obu mikrokontrolerów (bez konieczności rozkręcania obudowy i wyjmowania zworek UART).

---
*Koniec raportu.*
