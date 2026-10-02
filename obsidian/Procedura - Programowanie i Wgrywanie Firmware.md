---
aliases:
  - Wgrywanie Firmware
  - Flashing Procedure
  - OTA Update
tags:
  - procedure
  - firmware
  - ota
category: procedure
status: active
---

# ⚡ Procedura — Programowanie i Wgrywanie Firmware

Precyzyjna instrukcja krok po kroku wgrywania oprogramowania do obu mikrokontrolerów w projekcie — zarówno przez USB, jak i bezprzewodowo przez OTA.

Powrót do: [[MOC - Procedury i Diagnostyka]] | Sprzęt: [[Hardware - Zworki UART i Izolacja]]

---

## 🔄 Metody Wgrywania Firmware

| Metoda                      | WeMos AquaBox                            | ESP32 CYD                        | Wymaga rozkręcenia   |
| --------------------------- | ---------------------------------------- | -------------------------------- | -------------------- |
| **USB + PlatformIO**        | ✅ `pio run -e wemos_d1_uno -t upload`    | ✅ `pio run -e esp32dev-st7789 -t upload --upload-port COM7` | ⚠️ Tak (zworki UART) |
| **Web OTA (przeglądarka)**  | ✅ Przez panel WiFi CYD lub przycisk boot | ✅ Przez portal `http://IP/`      | ❌ Nie                |
| **ArduinoOTA (PlatformIO)** | ✅ Gdy WeMos w trybie serwisowym          | ✅ Env `esp32dev-dev`             | ❌ Nie                |

---

## 🚨 ZASADA KRYTYCZNA: ZWORKI UART (tylko USB)

> [!IMPORTANT]
> Przed podłączeniem kabla USB do WeMos D1 Uno lub do CYD **BEZWZGLĘDNIE ROZEWRZYJ (ZDEJMIJ) ZWORKI UART** w obudowie AquaBox!
> Zignorowanie tego kroku spowoduje kolizję konwerterów USB-UART i błąd wgrywania w PlatformIO.

---

## 📋 Metoda 1: USB + PlatformIO (Pierwsze Wgranie, Pełen Reset)

### Krok 1: Wgranie Firmware do WeMos D1 Uno (Sterownik w AquaBox)
1. Zdejmij obie zworki UART (`J_TX`, `J_RX`).
2. Podłącz WeMos kablem microUSB do komputera.
3. W terminalu VS Code / PlatformIO wykonaj polecenie:
   ```powershell
   pio run -e wemos_d1_uno -t upload
   ```
4. Poczekaj na komunikat `[SUCCESS]`.
5. Odłącz kabel microUSB od WeMos.

### Krok 2: Wgranie Firmware do ESP32 CYD (Wyświetlacz i Web)
1. Zworki UART nadal pozostają **rozwarte**.
2. Podłącz ESP32 CYD kablem USB do komputera i potwierdź, że jest widoczny jako `COM7` (`USB-SERIAL CH340`). Jeżeli system przydzielił inny port, użyj faktycznie wykrytego portu zamiast zgadywać.
3. Zamknij monitor portu szeregowego oraz narzędzia, które mogą trzymać COM7.
4. Wciśnij i **przytrzymaj BOOT** na CYD, a następnie w terminalu wykonaj:
   ```powershell
   pio run -e esp32dev-st7789 -t upload --upload-port COM7
   ```
5. Zwolnij BOOT dopiero, gdy log esptool potwierdzi uruchomienie stub-a (np. `Stub running...`). W razie komunikatu `Wrong boot mode detected (0x13)` przerwij próbę, ponownie ręcznie wprowadź CYD w tryb BOOT i uruchom polecenie od początku.
6. Za sukces uznaj wgranie **wyłącznie** po komunikacie `Hash of data verified` oraz restarcie `Hard resetting via RTS`. Sam komunikat `[SUCCESS]` nie jest wystarczającym potwierdzeniem integralności obrazu.
7. Odczekaj na normalny start ekranu i Wi-Fi przed kolejną diagnostyką. Nie odłączaj USB w trakcie zapisu ani weryfikacji.

### Krok 3: Połączenie i Start Całości
1. **Załóż zworki UART** (`J_TX` oraz `J_RX`).
2. Podłącz główne zasilanie sieciowe 12V do obudowy AquaBox.
3. Niebieska dioda `D4` na WeMos zacznie miarowo mrugać co 1 sekundę (telemetria).
4. Na ekranie CYD pojawi się interfejs LVGL z aktualnymi odczytami temperatury i zegara.

---

## 📡 Metoda 2: OTA przez WiFi — Aktualizacja WeMos AquaBox (bez rozkręcania!)

### Sposób A: Z ekranu CYD (główny, zalecany)
1. Na ekranie CYD przejdź do **Ustawienia → WiFi**.
2. Naciśnij przycisk **⚙️ Aktualizuj AquaBox** (pomarańczowy).
3. CYD wyśle komendę UART `EnterOta` do WeMos.
4. Na ekranie pojawi się panel z informacjami:
   - **Sieć WiFi**: `AquaBox-OTA` (AP, hasło: `aquabox8`) lub sieć domowa (STA)
   - **Adres IP**: wyświetlony na ekranie (np. `192.168.4.1` dla AP)
5. Na komputerze:
   - Połącz z siecią `AquaBox-OTA` (jeśli AP)
   - Otwórz przeglądarkę i wejdź na `http://192.168.4.1/`
   - Wgraj plik `.pio/build/wemos_d1_uno/firmware.bin`
6. WeMos uruchomi się automatycznie po wgraniu.

> [!TIP]
> Jeśli w `wemos_config.h` ustawisz `OTA_STA_SSID` i `OTA_STA_PASSWORD`, WeMos połączy się z siecią domową zamiast tworzyć AP.

### Sposób B: Przycisk fizyczny przy starcie (awaryjny fallback)
> [!NOTE]
> Używaj gdy CYD jest niedostępny lub uszkodzony.

1. **Odłącz zasilanie** AquaBox.
2. Wciśnij i przytrzymaj przycisk monostabilny podłączony do **GPIO0 (D3)** WeMos.
3. **Podaj zasilanie** trzymając przycisk.
4. Po 2-3 sekundach zwolnij — WeMos wszedł w tryb serwisowy OTA.
5. Dioda `D4` mruga wolno (co 1 sekundę) — tryb aktywny.
6. Połącz z siecią `AquaBox-OTA` i otwórz `http://192.168.4.1/`.

> [!WARNING]
> **Watchdog 5 minut**: jeśli w ciągu 5 minut nic nie zostanie wgrane, WeMos automatycznie uruchomi się ponownie w normalnym trybie pracy. Grzałka i CO2 są wyłączone przez cały czas trybu serwisowego.

---

## 📡 Metoda 3: OTA przez WiFi — Aktualizacja CYD

### Przez przeglądarkę (Web OTA)
1. Na ekranie CYD przejdź do **Ustawienia → WiFi**.
2. Naciśnij przycisk **⬆️ OTA Panel**.
3. CYD uruchomi własny punkt dostępowy `cydAkwarium-OTA` (lub połączy się z siecią domową STA).
4. Na komputerze połącz z siecią AP lub użyj IP CYD z sieci domowej.
5. Wgraj plik `.pio/build/esp32dev-st7789/firmware.bin` przez formularz HTTP.

### Przez ArduinoOTA (PlatformIO — tylko `esp32dev-dev`)
> [!CAUTION]
> Dostępne tylko w środowisku developerskim `esp32dev-dev` z flagą `AQUARIUM_ALLOW_UNSIGNED_ARDUINO_OTA=1`. Nigdy nie używaj w produkcji!

```powershell
pio run -e esp32dev-dev -t upload --upload-port <IP_CYD>
```

---

## 📦 Lokalizacja Plików Binarnych

Po kompilacji `pio run`:

| Środowisko | Plik `.bin` |
|------------|------------|
| `wemos_d1_uno` | `.pio/build/wemos_d1_uno/firmware.bin` |
| `esp32dev-st7789` | `.pio/build/esp32dev-st7789/firmware.bin` |

## 🧪 Minimalna weryfikacja WWW po wgraniu CYD

CYD ma ograniczony zapas RAM, dlatego po zmianach w warstwie HTTP nie wykonuj serii równoległych odświeżeń ani automatycznego testu obciążeniowego. Najpierw sprawdź pojedyncze, sekwencyjne transfery z jednym klientem:

```powershell
curl.exe --compressed --fail --silent --show-error -D .\cyd-style.headers -o .\cyd-style.css http://192.168.0.112/style.css
curl.exe --compressed --fail --silent --show-error -D .\cyd-app.headers -o .\cyd-app-init.js http://192.168.0.112/app-init.js
curl.exe --compressed --fail --silent --show-error -D .\cyd-root.headers -o .\cyd-root.html http://192.168.0.112/
```

Porównaj rozmiar każdego pobranego pliku z nagłówkiem `Content-Length`. Dopiero po powodzeniu pojedynczych transferów otwórz panel w zwykłej przeglądarce i sprawdź jego działanie. Pełne `200 OK` dla trzech plików nie zamyka jeszcze problemu stabilności pod wielozasobowym obciążeniem przeglądarki; aktualny stan i kryteria odbioru są w [[2026-09-23 - Weryfikacja HTTP CYD i wdrożenie]].

---

## 🔗 Powiązane Notatki
- [[Hardware - Zworki UART i Izolacja]]
- [[MOC - Firmware ESP8266 WeMos]]
- [[MOC - Firmware ESP32 CYD]]
- [[Procedura - Diagnostyka i Self-Test WeMos]]
- [[Protokół - Specyfikacja NDJSON UART]]
- [[2026-09-23 - Weryfikacja HTTP CYD i wdrożenie]]
