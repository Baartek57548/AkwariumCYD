# Handoff Report — explorer_m1_3

**Agent:** explorer_m1_3  
**Data:** 2026-09-18  
**Rola:** Badanie architektury testów jednostkowych dla protokołu UART NDJSON (EnterOta, ACK, telemetria) oraz weryfikacja wykonalności `pio test -e native`  
**Typ Przekazania:** Hard Handoff (Zadanie badawcze w pełni ukończone)  

---

## 1. Obserwacja (Observation)

1. **Istniejący zestaw testów w `firmware/cyd_controller/test/test_native_domain/test_main.cpp`:**
   - Plik liczy 1677 linii kodu i zawiera 45 przypadków testowych Unity uruchamianych w funkcji `main()` (linie 1628–1676).
   - Testuje logikę harmonogramów, polityki bezpieczeństwa termostatu i gazu, kalkulacje kalibracji oraz binarny protokół COBS (`aquacyd_link_protocol.h`).
   - W całym pliku ani razu nie występuje odwołanie do `AquaProtocol`, `uart_protocol.h`, `serializeCommand`, `serializeTelemetry` czy `parseMessage`.

2. **Wyniki wykonania `pio test -e native`:**
   - **W katalogu `firmware/cyd_controller`:**
     Komenda: `pio test -e native`
     Wynik: `45 test cases: 45 succeeded in 00:00:04.797` (PASSED).
   - **W katalogu głównym repozytorium (`cydAquarium`):**
     Komenda: `pio test -e native`
     Wynik:
     ```
     firmware\cyd_controller\test\test_native_domain\test_main.cpp:9:33: fatal error: aquarium_automation.h: No such file or directory
     compilation terminated.
     *** [.pio\build\native\test\test_native_domain\test_main.o] Error 1
     ```
     Powód: w pliku `platformio.ini` (root, linia 137) profil `[env:native]` definiuje wyłącznie `lib_extra_dirs = firmware/shared`. Brakuje ścieżki do `firmware/cyd_controller/lib`.

3. **Zależności sprzętowe w `firmware/shared/uart_protocol/include/uart_protocol.h`:**
   - Linia 4: `#include <Arduino.h>`
   - Linie 112–115: zwracanie typu `String` przez `serializeTelemetry()`, `serializeCommand()`, `serializeAck()`, `serializeTimeSync()`.
   - W kompilatorze desktopowym MinGW (`toolchain-gccmingw32@1.50100.0`, GCC 5.1.0) brak jest pliku `Arduino.h` oraz klasy `String`. Próba bezpośredniego dołączenia `uart_protocol.h` do testu natywnego powoduje błąd kompilacji.

4. **Biblioteka `ArduinoJson`:**
   - Wersja `bblanchon/ArduinoJson @ 6.21.6` znajduje się lokalnie w pamięci podręcznej `.pio/libdeps/wemos_d1_uno/ArduinoJson`.
   - Jest to biblioteka header-only w pełni kompatybilna z czystym standardem C++11 (bez konieczności używania frameworka Arduino).

5. **Empiryczna weryfikacja prototypu testów w środowisku natywnym:**
   - Utworzono prototyp `.agents/explorer_m1_3/test_proto.cpp` oraz lekki adapter `Arduino.h`.
   - Skompilowano kompilatorem MinGW GCC 5.1.0 z flagami `-std=gnu++11 -static -static-libgcc -static-libstdc++`.
   - Wynik wykonania testu binarnego:
     ```
     .agents/explorer_m1_3/test_proto.cpp:311:test_enter_ota_serialization_round_trip:PASS
     .agents/explorer_m1_3/test_proto.cpp:312:test_enter_ota_default_fallback_round_trip:PASS
     .agents/explorer_m1_3/test_proto.cpp:313:test_ack_enter_ota_handling:PASS
     .agents/explorer_m1_3/test_proto.cpp:314:test_telemetry_ota_flags_and_backward_compatibility:PASS
     -----------------------
     4 Tests 0 Failures 0 Ignored 
     OK
     ```

---

## 2. Ciąg Logiczny (Logic Chain)

1. **Krok 1:** Obserwacja 1 wykazuje, że dotychczas protokół UART NDJSON (`uart_protocol`) nie posiadał żadnych automatycznych testów jednostkowych.
2. **Krok 2:** Obserwacja 3 wskazuje przyczynę: twarde powiązanie `uart_protocol.h` z frameworkiem Arduino (`#include <Arduino.h>` i typ `String`), podczas gdy testy natywne budowane są w czystym C++11 bez frameworka.
3. **Krok 3:** Obserwacja 4 i 5 udowadniają, że parser i serializer NDJSON bazujący na `ArduinoJson` może być bezproblemowo kompilowany i wykonywany przez natywny kompilator MinGW desktop pod warunkiem dostarczenia definicji typu `String` kompatybilnego z `std::string` (lub shima `Arduino.h`).
4. **Krok 4:** Obserwacja 2 identyfikuje usterkę w konfiguracji root `platformio.ini` uniemożliwiającą uruchomienie `pio test -e native` z głównego katalogu monorepozytorium.
5. **Krok 5:** Zatem, uzupełnienie `uart_protocol.h` o warunkową kompilację (`#if defined(ARDUINO) ... #else ... #endif`), dodanie `bblanchon/ArduinoJson@^6.21.4` do `lib_deps` w `[env:native]` oraz poprawienie ścieżki `lib_extra_dirs` umożliwi włączenie pełnego zestawu testów dla komendy `EnterOta`, ramek ACK oraz flag telemetrii bezpośrednio do suite'u `test_native_domain`.

---

## 3. Zastrzeżenia i Granice Badania (Caveats)

1. **Brak modyfikacji plików produkcyjnych:** Zgodnie z rolą explorer (read-only), nie modyfikowano plików w `firmware/` ani `platformio.ini`. Wszystkie testy prototypowe i weryfikacje kompilacji przeprowadzono w izolowanym folderze `.agents/explorer_m1_3/`.
2. **Kompilacja firmware docelowego:** Niniejsze badanie koncentrowało się na środowisku natywnym `native`. Weryfikacja budowania pod mikrokontrolery fizyczne (`pio run -e wemos_d1_uno` oraz `pio run -e cyd_2_8`) podlega zadaniom kolejnych agentów implementacyjnych (M1/M2/M3).
3. **Alternatywy architektoniczne:** Rozważono czysto buforowe API (`char*`, zero alokacji na stercie). Mimo że jest to rekomendowane docelowo dla optymalizacji SRAM (R2), dla zachowania 100% kompatybilności wstecznej z istniejącymi wywołaniami w `main.cpp` i `uart_client.cpp` zaleca się zachowanie interfejsu `String` z warunkowym shimem dla środowiska natywnego.

---

## 4. Konkluzja (Conclusion)

Uruchomienie natywnych testów jednostkowych dla rozszerzeń protokołu UART NDJSON (`EnterOta`, ACK, flagi telemetrii `ota_mode` i `ota_ip`) za pomocą `pio test -e native` jest **w 100% wykonalne i nie wymaga żadnych zależności sprzętowych**. 

Zaprojektowano 11 szczegółowych przypadków testowych pokrywających:
1. Pełną pętlę serializacji i deserializacji `EnterOta` z poświadczeniami SSID/hasło oraz fallback domyślny.
2. Odporność na przepełnienia buforów znakowych (bounds safety).
3. Potwierdzenia ACK w trybach STA i AP oraz warianty błędów.
4. Flagi telemetrii OTA i zachowanie 100% kompatybilności wstecznej ze starszymi ramkami.
5. Logikę buforowania ACK oraz wygaszania timeoutów offline w `UartClient`.

Wszystkie szczegóły techniczne, schematy struktur oraz instrukcje krok po kroku dla implementera znajdują się w pliku:
`c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_m1_3\report.md`.

---

## 5. Metoda Weryfikacji (Verification Method)

Implementer i audytor mogą niezależnie powtórzyć weryfikację za pomocą następujących kroków:

1. **Weryfikacja istniejącego suite'u testów natywnych:**
   ```powershell
   cd c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\firmware\cyd_controller
   pio test -e native
   ```
   *Kryterium sukcesu:* Wszystkie 45 testów kończy się statusem `[PASSED]`.

2. **Weryfikacja prototypu testów protokołu UART NDJSON:**
   ```powershell
   cd c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium
   $env:PATH = "C:\Users\Bartek\.platformio\packages\toolchain-gccmingw32\bin;" + $env:PATH
   g++ -std=gnu++11 -static -static-libgcc -static-libstdc++ `
       -I .agents/explorer_m1_3 `
       -I .pio/libdeps/wemos_d1_uno/ArduinoJson/src `
       -I firmware/cyd_controller/.pio/libdeps/native/Unity/src `
       .agents/explorer_m1_3/test_proto.cpp `
       firmware/cyd_controller/.pio/libdeps/native/Unity/src/unity.c `
       -o .agents/explorer_m1_3/test_proto_verify.exe
   & .\.agents\explorer_m1_3\test_proto_verify.exe
   Remove-Item .\.agents\explorer_m1_3\test_proto_verify.exe
   ```
   *Kryterium sukcesu:* Wyjście konsoli: `4 Tests 0 Failures 0 Ignored OK`.

3. **Pliki źródłowe i dokumentacja do wglądu:**
   - Raport: `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_m1_3\report.md`
   - Test prototypowy: `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_m1_3\test_proto.cpp`
   - Shim Arduino: `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_m1_3\Arduino.h`
