# Implementation Plan: Platforma sterowania akwarium

## Overview
<!-- Przegląd -->

Plan wdrożenia przekształca działający, jednowątkowy sterownik (`main.cpp` + `gui_app.cpp` +
`hal_display.*`) w modułową, dwurdzeniową platformę opisaną w `design.md`. Pracę podzielono na
**12 przyrostowych etapów (sprintów)**. Naczelna zasada: **po każdym zadaniu projekt MUSI
pozostawać stabilny i kompilowalny** — każdy etap kończy się jawną weryfikacją buildu PlatformIO
(`pio run -e esp32dev`), a testy czystych funkcji budowane są w osobnym środowisku natywnym
(`pio test -e native`), więc nie wpływają na firmware urządzenia.

Trzy decyzje zatwierdzone przez użytkownika są w pełni odzwierciedlone w planie:

1. **ADS1115 na magistrali I2C** obok MCP23017 (Etap 1, Etap 6).
2. **Pełna migracja logiki biznesowej, harmonogramów i algorytmów** z `gui_app.cpp` do nowych
   klas kontrolera/czujników wg tabeli "Planowana migracja" z `design.md`. UI staje się cienkim
   widokiem komunikującym się wyłącznie zdarzeniami przez kolejki FreeRTOS (Etapy 2–8, 11–12).
3. **`synchronizeLights()` jako NIEBLOKUJĄCA maszyna stanów (FSM)** (Etap 4).

Każda migracja stosuje bezpieczną metodę **"extract & delegate"** z `design.md`: najpierw kopiujemy
logikę do nowego modułu jako funkcje czyste i każemy `gui_app.cpp` je wywoływać (identyczne
zachowanie, build przechodzi), a dopiero **po weryfikacji** usuwamy duplikat — usunięcie jest zawsze
osobnym, jawnym podzadaniem.

## Przegląd etapów

| Etap | Cel | Kluczowe moduły | Właściwości (PBT) |
|------|-----|-----------------|-------------------|
| 1 | Komunikacja niskopoziomowa I2C: MCP23017 + ADS1115 + diagnostyka | `config.h`, `hal_adc.*`, `main.cpp` | — |
| 2 | `Menedzer_Zdarzen` + szkielet zadań dwurdzeniowych FreeRTOS | `events.*`, `main.cpp` | 9 |
| 3 | `Kontroler_Urzadzen` `RelayDevice` + migracja `schedule_active` | `controller.*` | 1 |
| 4 | `Sterownik_Oswietlenia` + `synchronizeLights` FSM | `controller.*` | 5 |
| 5 | `Regulator_Grzalki` (`heater_decide`) | `controller.*` | 2 |
| 6 | `Warstwa_Czujnikow` + kalibracja pH/EC przez ADS1115 | `sensors.*` | 6, 7 |
| 7 | `Sterownik_CO2` + `Karmnik` | `controller.*` | 3 |
| 8 | `Rejestrator_Danych` `CircularBuffer<T,N>` + wykresy | `data_log.*` | 8 |
| 9 | Hardening OTA/diagnostyki + `hal_mcp_all_relays_safe` | `gui_app.cpp` | — |
| 10 | `Straznik_PIN` | `pin_guard.*` | 12 |
| 11 | `Tryb_Serwisowy` (auto-wygaśnięcie) | `controller.*` | 4 |
| 12 | Łagodna degradacja + migracja konfiguracji (10→11) + integracja | `gui_app.cpp`, `config_codec.*` | 10, 11 |

## Tasks

- [x] 1. Etap 1: Komunikacja niskopoziomowa I2C (MCP23017 + ADS1115) i diagnostyka startowa
  - [x] 1.1 Dodać stałe ADS1115 do `config.h` (`namespace HwConfig`)
    - Dodać `constexpr uint8_t ADS1115_ADDR = 0x48;` oraz `enum AdcChannel { ADC_PH=0, ADC_EC=1, ADC_ANALOG=2, ADC_SPARE=3 };` zgodnie z tabelą "Propozycja kanałów ADS1115"
    - Zmiana wyłącznie addytywna; nie modyfikować istniejących definicji `McpChannel`/pinów
    - _Wymagania: 3.1, 3.3; Projekt: Tabela alokacji pinów (ADS1115), Warstwa_Czujnikow_
  - [x] 1.2 Utworzyć sterownik ADS1115 `hal_adc.h` / `hal_adc.cpp`
    - Rejestrowy sterownik na `Wire` (bez zewnętrznej biblioteki), adres z `HwConfig::ADS1115_ADDR`
    - API: `bool hal_adc_init();`, `bool hal_adc_is_present();`, `bool hal_adc_read_raw(uint8_t ch, int16_t *out);`, `bool hal_adc_read_voltage(uint8_t ch, float *out);` (odczyt single-shot kanałów A0–A3)
    - Bezpieczny wątkowo (mutex FreeRTOS, timeout) analogicznie do `hal_mcp23017.cpp`; brak ekspandera ⇒ `is_present()==false`
    - _Wymagania: 7.1, 7.2; Projekt: Warstwa_Czujnikow (ograniczenie ADC i ADS1115)_
  - [x] 1.3 Zintegrować `hal_mcp_init()` i `hal_adc_init()` w `setup()` w `main.cpp` + tymczasowy odczyt diagnostyczny
    - Dołączyć `config.h`, `hal_mcp23017.h`, `hal_adc.h`; wywołać `hal_mcp_init()` i `hal_adc_init()` po `hal_display_init()`
    - Tymczasowy log na Serial: `hal_mcp_is_present()`, `hal_mcp_read_all()`, `hal_adc_is_present()`, surowe kanały ADS1115 — wyłącznie addytywnie, bez zmiany logiki UI ani pętli LVGL
    - _Wymagania: 3.3, 4.7, 7.5, 16.4; Projekt: Architektura (rola setup)_
  - [x] 1.4 Weryfikacja buildu Etapu 1
    - Uruchomić `pio run -e esp32dev`; upewnić się, że firmware kompiluje się i UI działa jak dotąd
    - _Wymagania: 16.1_

- [x] 2. Etap 2: Menedzer_Zdarzen (kolejki) + szkielet zadań dwurdzeniowych FreeRTOS
  - [x] 2.1 Skonfigurować środowisko testów natywnych z RapidCheck
    - Dodać `[env:native]` (`platform = native`) do `platformio.ini`; podłączyć **RapidCheck** (`emil-e/rapidcheck`) zintegrowany z runnerem testów (`doctest`/Unity); utworzyć katalog `test/` ze szkieletem
    - Środowisko obejmuje tylko funkcje czyste (bez Arduino/LVGL); env `esp32dev` pozostaje nienaruszony
    - _Wymagania: 19.1; Projekt: Strategia testów (konfiguracja PBT)_
  - [x] 2.2 Zaimplementować `events.h` / `events.cpp` (Menedzer_Zdarzen)
    - Typy: `enum class SensorId`, `struct SensorSample`, `enum class CommandType`, `struct Command` wg szkiców w `design.md`
    - Kolejki FreeRTOS: `sampleQueue` + `commandQueue`; API `events_init/publish_sample/poll_sample/publish_command/poll_command/subscribe`
    - Polityka przepełnienia: drop-oldest dla próbek + inkrement licznika przepełnień; rejestr wielu subskrybentów (callbacki)
    - _Wymagania: 2.1, 2.2, 2.3, 2.4, 2.5, 2.6_
  - [x]* 2.3 Test właściwościowy: polityka przepełnienia kolejki próbek
    - Wydzielić testowalny model kolejki (`SampleQueueModel` — funkcja/klasa czysta, niezależna od FreeRTOS) i objąć go PBT w `[env:native]`
    - **Property 9: Polityka przepełnienia kolejki próbek (drop-oldest + licznik)**
    - **Validates: Wymagania 2.5**
    - Min. 100 iteracji; tag: `// Feature: aquarium-control-platform, Property 9: Polityka przepełnienia kolejki próbek (drop-oldest + licznik)`
  - [x] 2.4 Refaktoryzacja `main.cpp`: bootstrap `uiTask` + `controlTask` przypięte do rdzeni
    - `setup()` tworzy zadania `xTaskCreatePinnedToCore(uiTask,...,1)` i `xTaskCreatePinnedToCore(controlTask,...,0)`; `loop()` => `vTaskDelay`
    - `uiTask`: `lv_tick_inc` + `hal_display_loop_cb()` + `lv_timer_handler()` co ≤10 ms (`vTaskDelay(5)`); `controlTask`: cykl ≤1 s, na razie publikuje istniejące próbki (symulacja/ADC) przez `events_publish_sample`, a `uiTask` konsumuje je i woła dotychczasowe `gui_*`
    - Zachować bieżące zachowanie (zegar, temp/pH, LDR, heap, OTA); LVGL wyłącznie w `uiTask`
    - _Wymagania: 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 2.1, 2.2, 18.2_
  - [x] 2.5 Weryfikacja buildu Etapu 2
    - `pio run -e esp32dev` (firmware) oraz `pio test -e native` (Property 9); UI płynne, brak watchdoga
    - _Wymagania: 1.4, 1.5, 16.1_

- [x] 3. Etap 3: Kontroler_Urzadzen — RelayDevice + migracja decyzji harmonogramu
  - [x] 3.1 Utworzyć `controller.h` / `controller.cpp` z czystymi funkcjami harmonogramu (kopia z `gui_app.cpp`)
    - Przenieść jako funkcje czyste: `to_minutes`, `is_within_window`, `schedule_active` (logika 1:1, w tym zawijanie przez północ i `start == end` ⇒ poza oknem)
    - Dodać `enum class DeviceMode` (== `ScheduleMode`), `struct ScheduleWindow`, klasę `RelayDevice` (`shouldBeOn(nowMinutes)`, `apply(nowMinutes, serviceForcedOff)` wołające `hal_mcp_write_channel`)
    - Funkcje muszą kompilować się natywnie (bez Arduino/LVGL)
    - _Wymagania: 4.1, 4.2, 4.3, 4.4, 4.5, 4.7; Projekt: Kontroler_Urzadzen_
  - [x]* 3.2 Test właściwościowy: poprawność decyzji harmonogramu
    - **Property 1: Poprawność decyzji harmonogramu (w tym zawijanie przez północ)**
    - **Validates: Wymagania 4.1, 4.2, 4.3, 4.4, 4.5, 4.6, 5.5, 9.1**
    - Min. 100 iteracji; tag: `// Feature: aquarium-control-platform, Property 1: Poprawność decyzji harmonogramu (w tym zawijanie przez północ)`
  - [x] 3.3 Przełączyć `gui_app.cpp` na wywołania `controller::schedule_active`/`is_within_window`
    - Etykiety i logika UI wołają funkcje z `controller.*` zamiast lokalnych kopii; identyczne wyniki, build przechodzi
    - _Wymagania: 16.1; Projekt: Warstwa modułowa (extract & delegate)_
  - [x] 3.4 Usunąć zduplikowane `to_minutes`/`is_within_window`/`schedule_active` z `gui_app.cpp`
    - Usunięcie wykonać dopiero po weryfikacji 3.3; pozostaje jedno źródło prawdy w `controller.*`
    - _Wymagania: 19.1, 19.2_
  - [x] 3.5 Zinstancjonować Filtr i Napowietrzacz jako `RelayDevice` w `controlTask`
    - Dwie instancje na `CH_FILTER` i `CH_AERATOR` z oknami i trybami z `cfg`; `apply()` w cyklu sterowania
    - _Wymagania: 4.6, 4.7_
  - [x] 3.6 Weryfikacja buildu Etapu 3
    - `pio run -e esp32dev` + `pio test -e native` (Property 1)
    - _Wymagania: 16.1_

- [x] 4. Etap 4: Sterownik_Oswietlenia + nieblokująca FSM synchronizeLights()
  - [x] 4.1 Zaimplementować `LightSyncFsm` w `controller.*` (stany Idle/Resetting/Pulsing/Done)
    - `requestMode(target)`, `tick(nowMs)` (czas przez `millis()`), `busy()`; stałe z `HwConfig::Lights` (`RESET_POWER_OFF_MS`, `ADVANCE_PULSE_ON_MS/GAP_MS`, `MODE_COUNT`)
    - Czysta statyczna `pulsesForMode(target)` = `target-1` w zakresie, `0` poza; sterowanie `CH_LIGHT_A`/`CH_LIGHT_B` jednocześnie; blokada `RelayDevice` lamp dopóki `busy()`
    - _Wymagania: 5.1, 5.2, 5.3, 5.4, 5.5, 17.1, 17.2, 17.3, 17.4, 17.5, 17.6; Projekt: Sterownik_Oswietlenia_
  - [x]* 4.2 Test właściwościowy: liczba impulsów synchronizacji świateł
    - **Property 5: Liczba impulsów synchronizacji świateł**
    - **Validates: Wymagania 17.4**
    - Min. 100 iteracji; tag: `// Feature: aquarium-control-platform, Property 5: Liczba impulsów synchronizacji świateł`
  - [x]* 4.3 Testy jednostkowe przejść czasowych FSM
    - Sterowany zegar (`nowMs`): okno resetu 10 s, kolejność impulsów ON/GAP, blokada harmonogramu lamp w toku
    - _Wymagania: 17.2, 17.3, 17.5_
  - [x] 4.4 Podłączyć FSM do `controlTask` i polecenia `CommandType::SetLightMode`
    - `controlTask` woła `LightSyncFsm::tick()` co cykl; odbiór `SetLightMode` przez `events_poll_command` wyzwala `requestMode`
    - _Wymagania: 17.6_
  - [x] 4.5 Przełączyć UI na publikację `SetLightMode` i usunąć duplikat `cycle_light_color_mode`/sterowanie lampami z `gui_app.cpp`
    - UI publikuje polecenie zamiast bezpośrednio zmieniać stan lamp; usunięcie po weryfikacji
    - _Wymagania: 2.3, 19.1, 19.2_
  - [x] 4.6 Weryfikacja buildu Etapu 4
    - `pio run -e esp32dev` + `pio test -e native` (Property 5)
    - _Wymagania: 16.1_

- [x] 5. Etap 5: Regulator_Grzalki
  - [x] 5.1 Wydzielić `heater_decide()` do `controller.*` (kopia logiki histerezy z `gui_update_metrics`)
    - Sygnatura `bool heater_decide(bool prevOn, HeaterMode mode, float temp, float target, float hysteresis)`; fail-safe dla `Off`/`NaN`/`inf`, antyoscylacja w paśmie, próg wyłączenia ujednolicony do `target + hysteresis`
    - _Wymagania: 6.3, 6.4, 6.5, 6.6; Projekt: Regulator_Grzalki_
  - [x]* 5.2 Test właściwościowy: decyzja grzałki
    - **Property 2: Decyzja grzałki — progi, antyoscylacja i fail-safe**
    - **Validates: Wymagania 6.3, 6.4, 6.5, 6.6**
    - Min. 100 iteracji; tag: `// Feature: aquarium-control-platform, Property 2: Decyzja grzałki — progi, antyoscylacja i fail-safe`
  - [x] 5.3 Podłączyć `heater_decide` w `controlTask`, sterować `CH_HEATER`, log błędu pomiaru
    - `controlTask` używa `heater_decide` z `cfg.heaterMode/targetTemp/tempHysteresis`; nieprawidłowy pomiar ⇒ `CH_HEATER` OFF + zdarzenie błędu w diagnostyce
    - _Wymagania: 6.5, 6.6_
  - [x] 5.4 Przełączyć `gui_app.cpp` na `heater_decide` i usunąć zduplikowany blok histerezy
    - UI/`runtime.heaterOn` korzysta z wyniku funkcji; usunięcie duplikatu po weryfikacji
    - _Wymagania: 16.1, 19.1, 19.2_
  - [x] 5.5 Weryfikacja buildu Etapu 5
    - `pio run -e esp32dev` + `pio test -e native` (Property 2)
    - _Wymagania: 16.1_

- [x] 6. Etap 6: Warstwa_Czujnikow + kalibracja pH/EC (ADS1115)
  - [x] 6.1 Utworzyć `sensors.h` / `sensors.cpp` z interfejsem `Sensor` i implementacjami
    - Abstrakcyjny `Sensor` (`id()`, `enabled()`, `read()` zwracające `SensorSample` z timestampem i flagą `valid`); konkretne: LDR (GPIO34), temperatura, poziom wody/wyciek (MCP, debounce), pH/EC (ADS1115)
    - Przeliczanie surowego ADC na jednostki inżynierskie; oznaczanie `valid` wg zakresu `[min,max]`
    - _Wymagania: 7.1, 7.2, 7.3, 7.4, 7.5_
  - [x]* 6.2 Test właściwościowy: oznaczanie próbki poza zakresem
    - **Property 7: Oznaczanie próbki poza zakresem jako nieprawidłowej**
    - **Validates: Wymagania 7.4**
    - Min. 100 iteracji; tag: `// Feature: aquarium-control-platform, Property 7: Oznaczanie próbki poza zakresem jako nieprawidłowej`
  - [x] 6.3 Zaimplementować `CalibrationCurve` (interpolacja segmentowa) w `sensors.*`
    - `addPoint(ref, raw)` (sort rosnąco po raw, min. 2, maks. 5 punktów), `toEngineering(raw)` (interpolacja liniowa + ekstrapolacja skrajnych segmentów), `pointCount()`, `clear()`
    - _Wymagania: 8.1, 8.2, 8.6; Projekt: Kalibracja wielopunktowa_
  - [x]* 6.4 Test właściwościowy: interpolacja kalibracji
    - **Property 6: Interpolacja kalibracji — monotoniczność i zgodność w węzłach**
    - **Validates: Wymagania 7.2, 8.4, 8.6**
    - Min. 100 iteracji; tag: `// Feature: aquarium-control-platform, Property 6: Interpolacja kalibracji — monotoniczność i zgodność w węzłach`
  - [x] 6.5 Podłączyć Warstwa_Czujnikow do `controlTask` (publikacja `SensorSample`) + pomijanie wyłączonych
    - `controlTask` próbkuje ≤1 s i publikuje przez `events_publish_sample`; czujnik z `enabled()==false` jest pomijany; `Sonda_pH`/`Sonda_EC` używają `CalibrationCurve`
    - _Wymagania: 7.1, 8.6, 15.3, 18.2_
  - [x] 6.6 Kreator_Kalibracji w UI (kopia robocza, zapis dopiero przy zatwierdzeniu)
    - Podstrona prowadząca przez ≥2 punkty; `StartCalibrationPoint`/zapis; anulowanie zachowuje poprzednie punkty; punkty pH/EC w osobnym kluczu NVS z CRC32
    - _Wymagania: 8.3, 8.4, 8.5, 8.7_
  - [x]* 6.7 Testy jednostkowe Kreatora_Kalibracji
    - Przepływ ≥2 punktów oraz anulowanie zachowujące poprzednią kalibrację
    - _Wymagania: 8.3, 8.7_
  - [x] 6.8 Weryfikacja buildu Etapu 6
    - `pio run -e esp32dev` + `pio test -e native` (Property 6, 7)
    - _Wymagania: 16.1_

- [x] 7. Etap 7: Sterownik_CO2 + Karmnik
  - [x] 7.1 Zaimplementować `co2_decide()` w `controller.*`
    - Sygnatura wg `design.md`; dominacja `serviceActive`, tryb pH (`ph > threshold`) z fail-safe przy nieprawidłowym pH, tryb harmonogramu przez `is_within_window`; sterowanie `CH_CO2`
    - _Wymagania: 9.1, 9.2, 9.3, 9.4, 9.5; Projekt: Sterownik_CO2_
  - [x]* 7.2 Test właściwościowy: decyzja CO2
    - **Property 3: Decyzja CO2 — dominacja serwisu, tryb pH i harmonogram**
    - **Validates: Wymagania 9.2, 9.3, 9.4, 9.5**
    - Min. 100 iteracji; tag: `// Feature: aquarium-control-platform, Property 3: Decyzja CO2 — dominacja serwisu, tryb pH i harmonogram`
  - [x] 7.3 Zaimplementować szkielet `Karmnik` w `controller.*` (migracja harmonogramu karmnika)
    - Przenieść jako funkcje czyste decyzję karmienia (maska `feedDays`, `feedCount`, `feedHour1/Minute1`, `feedHour2/Minute2`, deduplikacja 60 s); wyzwolenie impulsu na `CH_FEEDER_DRIVE`, odczyt `CH_FEEDER_POS`; `feedEnabled==false` ⇒ brak sygnału
    - _Wymagania: 10.1, 10.2, 10.3, 10.4, 10.5; Projekt: Karmnik_
  - [x]* 7.4 Testy jednostkowe dopasowania godziny karmienia
    - Trafienie zaplanowanej godziny, deduplikacja 60 s, brak sygnału gdy wyłączony
    - _Wymagania: 10.3, 10.5_
  - [x] 7.5 Przełączyć `gui_app.cpp` na nowe moduły i usunąć duplikaty (CO2/karmnik)
    - UI publikuje `SetCo2Mode`/`TriggerFeed`; usunięcie zduplikowanej logiki po weryfikacji
    - _Wymagania: 2.3, 19.1, 19.2_
  - [x] 7.6 Weryfikacja buildu Etapu 7
    - `pio run -e esp32dev` + `pio test -e native` (Property 3)
    - _Wymagania: 16.1_

- [x] 8. Etap 8: Rejestrator_Danych (CircularBuffer) + wiązanie wykresów
  - [x] 8.1 Zaimplementować szablon `CircularBuffer<T,N>` w `data_log.h`
    - `push`, `size`, `capacity`, `at(i)` (indeks 0 = najstarsza z N) wg `design.md`; nagłówkowy szablon kompilowalny natywnie
    - _Wymagania: 11.1, 11.2, 11.4; Projekt: Rejestrator_Danych_
  - [x]* 8.2 Test właściwościowy: bufor cykliczny
    - **Property 8: Bufor cykliczny — pojemność i zachowanie N najnowszych (FIFO)**
    - **Validates: Wymagania 11.1, 11.2, 11.4**
    - Min. 100 iteracji; tag: `// Feature: aquarium-control-platform, Property 8: Bufor cykliczny — pojemność i zachowanie N najnowszych (FIFO)`
  - [x] 8.3 Zamienić tablice `*_history[32]` w `gui_app.cpp` na instancje `CircularBuffer`
    - Instancje dla temp/pH (`float`), LDR (`int`), heap (`uint32_t`), stan grzałki (`bool`); `add_history_point` deleguje do `push`; UI tylko czyta `at(i)`
    - _Wymagania: 11.1, 11.2, 11.4; Projekt: Warstwa modułowa (extract & delegate)_
  - [x] 8.4 Usunąć stare tablice i ręczne przesuwanie z `gui_app.cpp`
    - Usunąć `temp_history`/`heater_history`/`ph_history`/`ldr_history`/`heap_history` oraz pętlę shift; po weryfikacji 8.3
    - _Wymagania: 16.1, 19.1, 19.2_
  - [x] 8.5 Podpiąć `Modul_Wykresow` do `CircularBuffer` + bufor decymowany 24 h
    - `update_charts_data` czyta `at(i)`; dodatkowy bufor 24 h z decymacją (`N × interwał ≥ 24h`); `Rejestrator_Danych` zasilany zdarzeniami próbek (multi-subscriber)
    - _Wymagania: 11.3, 11.5_
  - [x] 8.6 Weryfikacja buildu Etapu 8
    - `pio run -e esp32dev` + `pio test -e native` (Property 8); wykresy rysują się jak dotąd
    - _Wymagania: 16.1_

- [x] 9. Etap 9: Hardening OTA i diagnostyki
  - [x] 9.1 Wydzielić obsługę OTA do dedykowanego fragmentu zadania sieciowego
    - Przenieść `ArduinoOTA.handle()` poza pętlę renderowania; hostname/hasło z `Secrets::OTA_HOSTNAME`/`OTA_PASSWORD`; postęp (`onProgress/onStart/onEnd/onError`) prezentowany przez zdarzenia do UI
    - _Wymagania: 12.1, 12.2, 12.3_
  - [x] 9.2 Wywołać `hal_mcp_all_relays_safe()` przed restartem po OTA i przy błędzie
    - W `onEnd`/`onError` ustawić bezpieczny stan przekaźników przed restartem; niepowodzenie OTA loguje błąd w diagnostyce
    - _Wymagania: 12.3; Projekt: Menedzer_OTA (bezpieczeństwo przed restartem)_
  - [x] 9.3 Rozszerzyć `Modul_Diagnostyki` o licznik przepełnień kolejki i monitor heapu
    - Dodać licznik przepełnień (z Etapu 2) i wolny heap do panelu; odświeżanie ≤2 s timerem LVGL w `uiTask`
    - _Wymagania: 2.5, 12.4, 12.5, 18.3_
  - [x] 9.4 Weryfikacja buildu Etapu 9
    - `pio run -e esp32dev`; panele OTA/diagnostyki działają, przekaźniki w bezpiecznym stanie przed restartem
    - _Wymagania: 16.1_

- [x] 10. Etap 10: Straznik_PIN
  - [x] 10.1 Zaimplementować `PinGuard` w `pin_guard.h` / `pin_guard.cpp`
    - `isAuthenticated`, `authenticate(entered)` (porównanie z `Secrets::DEFAULT_PIN`/zapisanym), `invalidate`, `requireFor(action)`; rdzeń porównania jako funkcja czysta (testowalna natywnie)
    - _Wymagania: 13.1, 13.2, 13.3, 13.4; Projekt: Straznik_PIN_
  - [x]* 10.2 Test właściwościowy: autoryzacja PIN
    - **Property 12: Autoryzacja PIN — brak fałszywych akceptacji**
    - **Validates: Wymagania 13.2, 13.3, 13.4**
    - Min. 100 iteracji; tag: `// Feature: aquarium-control-platform, Property 12: Autoryzacja PIN — brak fałszywych akceptacji`
  - [x] 10.3 Wpiąć bramkę PIN przed akcjami krytycznymi w UI
    - Chronić: edycję harmonogramów, nastawy grzałki, uruchomienie kalibracji, aktywację OTA, zmianę PIN-u; podgląd odczytów/wykresów/diagnostyki bez PIN-u; `invalidate()` przy starcie i wybudzeniu
    - _Wymagania: 13.1, 13.5, 13.6, 13.4_
  - [x] 10.4 Weryfikacja buildu Etapu 10
    - `pio run -e esp32dev` + `pio test -e native` (Property 12)
    - _Wymagania: 16.1_

- [x] 11. Etap 11: Tryb_Serwisowy z automatycznym wygaśnięciem
  - [x] 11.1 Zaimplementować `ServiceMode` w `controller.*` (nieblokujący licznik)
    - `enter(nowMs)` (deadline 60 min), `exit()`, `tick(nowMs)` (auto-wygaśnięcie), `active()`, `remainingMs(nowMs)`; integracja `serviceForcedOff` w `RelayDevice::apply`, `heater_decide`, `co2_decide`
    - _Wymagania: 14.1, 14.2, 14.3, 14.4, 14.5; Projekt: Tryb_Serwisowy_
  - [x]* 11.2 Test właściwościowy: tryb serwisowy wymusza wyłączenie
    - **Property 4: Tryb serwisowy wymusza wyłączenie objętych urządzeń**
    - **Validates: Wymagania 14.1, 14.2**
    - Min. 100 iteracji; tag: `// Feature: aquarium-control-platform, Property 4: Tryb serwisowy wymusza wyłączenie objętych urządzeń`
  - [x]* 11.3 Testy jednostkowe licznika trybu serwisowego
    - Auto-wygaśnięcie po deadline, ręczne zakończenie, `remainingMs` dla UI
    - _Wymagania: 14.3, 14.4, 14.5, 14.6_
  - [x] 11.4 Podłączyć `Tryb_Serwisowy` do `controlTask`/UI (rozbudowa `subpage_service`)
    - Polecenia `EnterServiceMode`/`ExitServiceMode`; UI pokazuje pozostały czas; `controlTask` woła `tick()` co cykl
    - _Wymagania: 14.1, 14.6_
  - [x] 11.5 Weryfikacja buildu Etapu 11
    - `pio run -e esp32dev` + `pio test -e native` (Property 4)
    - _Wymagania: 16.1_

- [x] 12. Etap 12: Łagodna degradacja, migracja konfiguracji i integracja końcowa
  - [x] 12.1 Rozszerzyć `AquariumUiConfig` i podbić `UI_CONFIG_VERSION` 10 → 11
    - Dodać nowe pola **na końcu** struktury (CO2, `phCalCount`/`ecCalCount`, flagi `enable*`, `serviceTimeoutMin`); zmienić `UI_CONFIG_VERSION` z 10 na 11; mechanizm `magic`/`version`/`crc32` zachowany
    - _Wymagania: 16.2, 15.1; Projekt: Modele danych_
  - [x] 12.2 Rozszerzyć `sanitize_config` o walidację nowych pól + wydzielić kodek do `config_codec.*`
    - Clamp zakresów (targetTemp `[18,30]`, hysteresis `[0.1,5.0]`, `feedCount {1,2}`, godziny/minuty); serializacja + CRC32 jako funkcje czyste w `config_codec.*` (testowalne natywnie); zły CRC/wersja ⇒ `load_default_config`
    - _Wymagania: 6.1, 6.2, 10.4, 16.2, 16.3_
  - [x]* 12.3 Test właściwościowy: round-trip serializacji konfiguracji z CRC32
    - **Property 10: Round-trip serializacji konfiguracji z walidacją CRC32**
    - **Validates: Wymagania 8.5, 16.2, 16.3**
    - Min. 100 iteracji; tag: `// Feature: aquarium-control-platform, Property 10: Round-trip serializacji konfiguracji z walidacją CRC32`
  - [x]* 12.4 Test właściwościowy: sanityzacja konfiguracji utrzymuje zakresy
    - **Property 11: Sanityzacja konfiguracji utrzymuje zakresy**
    - **Validates: Wymagania 6.1, 6.2, 10.4**
    - Min. 100 iteracji; tag: `// Feature: aquarium-control-platform, Property 11: Sanityzacja konfiguracji utrzymuje zakresy`
  - [x] 12.5 Zaimplementować łagodną degradację (flagi `enable*` + brak ekspandera)
    - UI ukrywa/dezaktywuje elementy gdy flaga `false` (uogólnienie `showPhSensor`); `Warstwa_Czujnikow` pomija wyłączone czujniki; `hal_mcp_is_present()==false` ⇒ `Kontroler_Urzadzen` pomija przekaźniki, reszta działa
    - _Wymagania: 15.1, 15.2, 15.3, 15.4_
  - [x] 12.6 Integracja końcowa i pełna weryfikacja buildu
    - Połączyć wszystkie moduły w `controlTask`/`uiTask`; potwierdzić luźne powiązanie (UI tylko zdarzenia), zachowane funkcje (16.1) i podział modułów (19.1, 19.3); uruchomić `pio run -e esp32dev` oraz `pio test -e native` (wszystkie 12 właściwości)
    - _Wymagania: 1.1, 2.1, 2.2, 2.3, 16.1, 19.1, 19.3_

## Notes
<!-- Uwagi -->

- Zadania oznaczone `*` (testy jednostkowe/właściwościowe/integracyjne) są opcjonalne i mogą zostać pominięte dla szybszego MVP.
- Każde zadanie odwołuje się do konkretnych wymagań i sekcji projektu dla pełnej śledzalności.
- **Zasada stabilności:** każdy etap kończy się weryfikacją `pio run -e esp32dev`; firmware pozostaje kompilowalny po każdym zadaniu.
- **Metoda "extract & delegate":** migracja z `gui_app.cpp` zawsze przebiega: kopiuj jako funkcję czystą → przełącz wywołania → (po weryfikacji) usuń duplikat (osobne podzadanie).
- **PBT:** 12 właściwości z `design.md`, jedna właściwość = jeden test, min. 100 iteracji, RapidCheck w `[env:native]`; każdy test otagowany komentarzem `// Feature: aquarium-control-platform, Property {n}: {treść}`.
- Testy właściwościowe budowane są wyłącznie w środowisku natywnym i nie wpływają na firmware urządzenia.

## Task Dependency Graph

```json
{
  "waves": [
    { "id": 0, "tasks": ["1.1", "1.2", "2.1"] },
    { "id": 1, "tasks": ["1.3"] },
    { "id": 2, "tasks": ["1.4", "2.2"] },
    { "id": 3, "tasks": ["2.3", "2.4"] },
    { "id": 4, "tasks": ["2.5", "3.1"] },
    { "id": 5, "tasks": ["3.2", "3.3"] },
    { "id": 6, "tasks": ["3.4"] },
    { "id": 7, "tasks": ["3.5", "4.1"] },
    { "id": 8, "tasks": ["3.6", "4.2", "4.3"] },
    { "id": 9, "tasks": ["4.4"] },
    { "id": 10, "tasks": ["4.5"] },
    { "id": 11, "tasks": ["4.6", "5.1"] },
    { "id": 12, "tasks": ["5.2", "5.3"] },
    { "id": 13, "tasks": ["5.4"] },
    { "id": 14, "tasks": ["5.5", "6.1"] },
    { "id": 15, "tasks": ["6.2", "6.3"] },
    { "id": 16, "tasks": ["6.4", "6.5"] },
    { "id": 17, "tasks": ["6.6"] },
    { "id": 18, "tasks": ["6.7", "6.8", "7.1"] },
    { "id": 19, "tasks": ["7.2", "7.3"] },
    { "id": 20, "tasks": ["7.4", "7.5"] },
    { "id": 21, "tasks": ["7.6", "8.1"] },
    { "id": 22, "tasks": ["8.2", "8.3"] },
    { "id": 23, "tasks": ["8.4"] },
    { "id": 24, "tasks": ["8.5"] },
    { "id": 25, "tasks": ["8.6", "9.1"] },
    { "id": 26, "tasks": ["9.2"] },
    { "id": 27, "tasks": ["9.3"] },
    { "id": 28, "tasks": ["9.4", "10.1"] },
    { "id": 29, "tasks": ["10.2", "10.3"] },
    { "id": 30, "tasks": ["10.4", "11.1"] },
    { "id": 31, "tasks": ["11.2", "11.3"] },
    { "id": 32, "tasks": ["11.4"] },
    { "id": 33, "tasks": ["11.5", "12.1"] },
    { "id": 34, "tasks": ["12.2"] },
    { "id": 35, "tasks": ["12.3", "12.4"] },
    { "id": 36, "tasks": ["12.5"] },
    { "id": 37, "tasks": ["12.6"] }
  ]
}
```
