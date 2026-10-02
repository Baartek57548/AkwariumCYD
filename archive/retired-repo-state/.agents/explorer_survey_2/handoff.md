# Handoff Report — ESP32 CYD Firmware & OTA Architecture Survey

**Agent:** `explorer_survey_2`  
**Data:** 2026-09-18  
**Rola:** Badanie kodu źródłowego ESP32 CYD, środowiska `cyd_2_8`, podsystemu OTA, łączności Wi-Fi STA / AP fallback oraz architektury LVGL pod kątem wdrożenia kontrolek OTA.

---

## 1. Observation (Obserwacje Bezpośrednie)

1. **Konfiguracja Środowiska `cyd_2_8` w `platformio.ini` (linie 38–46):**
   ```ini
   [env:cyd_2_8]
   extends = cyd_firmware
   build_flags =
       ${cyd_firmware.build_flags}
       -D AQUARIUM_FORCE_DEVELOPER_MODE=0
       -D AQUARIUM_ALLOW_UNSIGNED_ARDUINO_OTA=0
       -D CYD_PANEL_ST7789=0
       -D CORE_DEBUG_LEVEL=1
   ```
   - Płyta: `esp32dev` (`platformio/espressif32@7.0.1`), framework `arduino`.
   - Układ partycji: `board_build.partitions = min_spiffs.csv` (linie 16). W `min_spiffs.csv` zdefiniowane są dwie partycje OTA po 1 966 080 B (~1.88 MiB) każda (`app0`, `app1`).
   - Flaga `-D AQUARIUM_ALLOW_UNSIGNED_ARDUINO_OTA=0` w środowisku produkcyjnym `cyd_2_8` blokuje uruchomienie niepodpisanego `ArduinoOTA`.

2. **Obsługa OTA i Serwer Web w `firmware/cyd_controller/src/gui_app.cpp`:**
   - Trasa HTTP `/update` zarejestrowana na `ota_http_server` (linia 10117):
     ```cpp
     ota_http_server.on("/update", HTTP_POST, ota_portal_handle_update_finish, ota_portal_handle_update_upload);
     ```
   - Weryfikacja formatu w `ota_portal_handle_update_upload()` (linie 9870–9876):
     ```cpp
     String normalized_filename = upload.filename;
     normalized_filename.toLowerCase();
     if (!normalized_filename.endsWith(".aqfw")) {
         ota_portal_set_update_error("Wybierz podpisany plik .aqfw");
         ota_portal_refresh_upload_screen(true);
         return;
     }
     ```
   - Przetwarzanie i weryfikacja podpisu RSA-3072 w `firmware/cyd_controller/src/secure_ota.cpp` (linie 109–129, 333–374) z zabezpieczeniem anti-rollback w `ota_guard.cpp`.
   - `ArduinoOTA` zarejestrowane w `gui_app.cpp` (linie 10737–10779, 16570–16573), ale aktywne tylko gdy `AQUARIUM_ALLOW_UNSIGNED_ARDUINO_OTA == 1`.

3. **Nazwa AP Fallback w `firmware/cyd_controller/include/config.h` (linia 175) i `gui_app.cpp` (linia 14499):**
   - W `config.h`:
     ```cpp
     constexpr char OTA_AP_SSID[] = "cydAkwarium-OTA";
     ```
   - W `gui_app.cpp`:
     ```cpp
     lv_obj_t *ota_ssid_lbl = create_label(ota_card, LV_SYMBOL_WIFI "  SSID: cydAkwarium-OTA", theme_text_main(), &lv_font_montserrat_12);
     ```
   - Wymaganie Follow-up R2 w `ORIGINAL_REQUEST.md` (linia 65): punkt dostępowy ma nosić nazwę **`AquaCYD-OTA`**.

4. **Architektura Interfejsu LVGL w `gui_app.cpp`:**
   - Wyświetlacz 320x240, sterownik ILI9341 + XPT2046.
   - Pasek nawigacji dolnej (`build_nav_bar()`, linie 13134–13156) zawiera 5 stron: Start (0), Plan (1), Moduly (2), Wykres (3), System (4).
   - Zakładka `System` (`build_system_page()`, linie 12863–12881) to siatka 8 kafelków (`WiFi`, `Ekran`, `Logi`, `Czas`, `Diag`, `Zasilanie`, `Audio`, `Sprzet`).
   - Podstrona `ActiveSubpage::Wifi` (`build_subpages()`, linie 14332–14520) zarządza panelami: `wifi_main_panel`, `wifi_sta_panel`, `wifi_pwd_panel`, `wifi_ota_panel`.
   - Na ekranie Start istnieje kafelek `SERWIS` (`gui_app.cpp:11914`), otwierający `ActiveSubpage::Service` ("Tryb serwisowy", linie 15217–15321).
   - Cykl życia stron w LVGL: `switch_to_page()` usuwa nieaktywną stronę przez `delete_active_page() -> lv_obj_del()` celem ochrony sterty SRAM (brak PSRAM).

5. **Komunikacja UART NDJSON w `firmware/shared/uart_protocol/` i `uart_client.cpp`:**
   - Komunikacja dwukierunkowa na porcie Serial (UART0, 115200 baud).
   - `AquaProtocol::CommandType` (linie 40–51 w `uart_protocol.h`) posiada komendy m.in. `SetRelay`, `SetTempTarget`, `SetSchedule`, `FeedTrigger`, `SyncLights`, `SyncTime`, `ClearAlarm`, `GetStatus`, `SetSafeMode`.
   - Aktualnie brak komendy `EnterOta` oraz flagi `ota_mode` w strukturze `AquariumTelemetry`.

6. **Weryfikacja Budowania (`pio run -e cyd_2_8`):**
   - Polecenie `pio run -e cyd_2_8` zakończyło się statusem `[SUCCESS]` w 244.80 s.
   - Pamięć RAM: 27.0% (użyte 88 632 B / 327 680 B).
   - Pamięć Flash: 83.6% (użyte 1 644 353 B / 1 966 080 B w partycji app min_spiffs).
   - Wygenerowano pliki binarne: `.pio/build/cyd_2_8/firmware.bin` oraz `.elf`.


---

## 2. Logic Chain (Ciąg Logiczny)

1. W oparciu o bezpośrednią obserwację partycjonowania `min_spiffs.csv` (1.88 MiB na aplikację) i brak PSRAM na płycie CYD (obserwacja 1 i 4), flashowanie przez sieć wymaga ścisłej dyscypliny pamięci SRAM (~40–60 KB wolnego heapu podczas działania UI).
2. W oparciu o obserwację 1 (`AQUARIUM_ALLOW_UNSIGNED_ARDUINO_OTA=0`) oraz obserwację 2 (wymóg `.aqfw` w `ota_portal_handle_update_upload`), bieżący firmware CYD w trybie produkcyjnym nie pozwala na wgranie bezpośrednio pliku `.bin` z PlatformIO przez `ArduinoOTA`, a formularz HTTP akceptuje wyłącznie podpisane pakiety `.aqfw`.
3. Zgodnie z wymaganiem R2 ("z obsługą wgrywania przez przeglądarkę WWW oraz bezpośrednio z PlatformIO"), należy włączyć `ArduinoOTA` (zabezpieczone hasłem z `device_credentials_ota_ap_password()`) w trybie serwisowym OTA lub zezwolić na wgrywanie bezpośrednio ze środowiska `cyd_2_8`.
4. Obserwacja 3 wykazuje niespójność pomiędzy kodem źródłowym (`cydAkwarium-OTA`) a wymaganiem specyfikacji (`AquaCYD-OTA`). Stała `Secrets::OTA_AP_SSID` oraz etykiety UI muszą zostać zaktualizowane do `AquaCYD-OTA`.
5. Zgodnie z architekturą LVGL (obserwacja 4), najbezpieczniejszym i najbardziej ergonomicznym miejscem dodania kontrolek "Aktualizuj Panel (CYD)" oraz "Aktualizuj AquaBox (WeMos)" jest podstrona `ActiveSubpage::Wifi` (lub dedykowana sekcja w `ActiveSubpage::Service`), gdzie użytkownik ma natychmiastowy wgląd w stan sieci Wi-Fi i adres IP.
6. Zgodnie z obserwacją 5, realizacja przycisku "Aktualizuj AquaBox (WeMos)" wymaga dodania do protokołu UART NDJSON komendy `enter_ota`, metody `g_uart_client.sendEnterOta()` oraz obsługi po stronie WeMos (odcięcie przekaźników, wybudzenie Wi-Fi w trybie STA/AP, start serwera OTA).

---

## 3. Caveats (Zastrzeżenia i Ograniczenia)

- Niniejsze zadanie ma charakter ściśle analityczny (read-only investigation) — żadne pliki źródłowe poza katalogiem `.agents/explorer_survey_2/` nie zostały zmodyfikowane.
- Wgrywanie firmware przez `ArduinoOTA` z poziomu PlatformIO w trybie Access Point (`AquaCYD-OTA` lub `AquaBox-OTA`) wymaga wcześniejszego ręcznego połączenia komputera z odpowiednią siecią Wi-Fi o adresie bramy 192.168.4.1.

---

## 4. Conclusion (Wnioski Końcowe)

1. Kod źródłowy CYD jest w pełni gotowy architektonicznie do wdrożenia bezprzewodowego OTA, posiadając już zalążki `ArduinoOTA`, serwer HTTP na porcie 80 z trasą `/update` oraz infrastrukturę SoftAP.
2. Należy dokonać precyzyjnych modyfikacji:
   - Zmiana nazwy AP na `AquaCYD-OTA` w `config.h` i `gui_app.cpp`.
   - Zapewnienie aktywacji `ArduinoOTA` w trybie serwisowym.
   - Rozszerzenie `uart_protocol` o `CommandType::EnterOta` i `g_uart_client.sendEnterOta()`.
   - Dodanie w UI przycisków "Aktualizuj Panel (CYD)" i "Aktualizuj AquaBox (WeMos)" z ekranem instrukcji (SSID, IP, URL).
3. Szczegółowy raport i harmonogram wdrożenia znajduje się w:
   `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_survey_2\report.md`.

---

## 5. Verification Method (Metoda Weryfikacji)

1. **Weryfikacja Budowania CYD:**
   Uruchomienie polecenia w terminalu głównym projektu:
   ```powershell
   pio run -e cyd_2_8
   ```
   Kryterium sukcesu: Kompilacja kończy się statusem `[SUCCESS]` z wygenerowaniem `.pio/build/cyd_2_8/firmware.bin`.
2. **Weryfikacja Plików i Linii:**
   - Sprawdzenie `platformio.ini`: sekcja `[env:cyd_2_8]`.
   - Sprawdzenie `firmware/cyd_controller/include/config.h`: linia 175 (`OTA_AP_SSID`).
   - Sprawdzenie `firmware/cyd_controller/src/gui_app.cpp`: linia 10117 (`/update`), 10737 (`ArduinoOTA`), 12863 (`build_system_page`), 14332 (`subpage_wifi`).
   - Sprawdzenie `firmware/shared/uart_protocol/include/uart_protocol.h`: `CommandType` i `AquariumTelemetry`.
