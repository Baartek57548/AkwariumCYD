# cydAquarium

Repozytorium zawiera dwie aplikacje Flutter: `mobile_app` komunikuje się
bezpośrednio ze sterownikiem CYD, a nowa `home_assistant_app` jest lekkim,
responsywnym klientem REST/WebSocket Home Assistanta. Architektura drugiej
aplikacji i procedura wdrożenia są opisane w
[docs/HOME_ASSISTANT_FLUTTER_APP.md](docs/HOME_ASSISTANT_FLUTTER_APP.md).

Sterownik akwarium dla ESP32 CYD z interfejsem LVGL, panelem WWW, BLE,
obsługą OTA i aplikacją Flutter.

Dokumentacja firmware, pinologia, profile kompilacji i procedura wgrywania:
[docs/CYD_FIRMWARE.md](docs/CYD_FIRMWARE.md).

Watchdog, fail-safe, kalibracja, bezpieczne profile Wi-Fi, trwałe alarmy oraz
warunek zatwierdzenia OTA są opisane w
[docs/FIRMWARE_RUNTIME_SAFETY.md](docs/FIRMWARE_RUNTIME_SAFETY.md).

Gotowe obrazy firmware ILI9341/ST7789 oraz instalacyjny APK są publikowane w
[GitHub Releases](https://github.com/Baartek57548/AkwariumCYD/releases).
Wybierz tag `firmware-vX.Y.Z` dla podpisanego pakietu `.aqfw` albo
`mobile-vX.Y.Z` dla aplikacji Android.

Najważniejsze katalogi:

- `src/`, `include/`, `lib/` — firmware ESP32;
- `test/` — testy logiki domenowej;
- `web/` — panel WWW;
- `gateway/` — opcjonalna brama HTTPS dla zdalnych alarmów i FCM;
- `mobile_app/` — aplikacja Flutter;
- `sdcard/` — struktura danych i zasobów karty SD.

Sterownik traktuje dwie lampy Aquael Day&Night jako niezależne urządzenia:
`front` (przednia) i `rear` (tylna), każde z profilami `DAY`, `DAYBREAK` i
`NIGHT`. Krótki cykl OFF→ON trwa najwyżej 5 sekund, a OFF dłuższy niż 5 sekund
resetuje lampę do `DAY`; implementacja używa impulsu 1 s oraz 6-sekundowej
kalibracji startowej, nie zmieniając progu producenta wynoszącego 5 sekund.

## CI/CD, HIL i bezpieczeństwo

GitHub Actions automatycznie sprawdza aplikację Flutter, testy Android/JVM,
firmware PlatformIO, panel WWW i narzędzia wydaniowe. Build z każdego commita
udostępnia krótkotrwałe artefakty diagnostyczne; APK z CI jest celowo pozbawiony
podpisu produkcyjnego. Dopiero tag `mobile-vX.Y.Z` albo `firmware-vX.Y.Z`
uruchamia kontrolowaną publikację z walidacją wersji, nazw i SHA-256.

Testy odporności sprzętowej można sprawdzić bez urządzenia:

```powershell
python tools/hil/runner.py --self-test
python tools/hil/runner.py --dry-run
python scripts/validate_release.py --self-test
python scripts/verify_firmware_trust.py
python tools/firmware_package.py self-test
python scripts/audit_esp32_security.py --self-test
```

Konfiguracja pipeline i wydań jest opisana w
[docs/PRODUCTION_CI_CD.md](docs/PRODUCTION_CI_CD.md), stanowisko sprzętowe w
[docs/PRODUCTION_HIL.md](docs/PRODUCTION_HIL.md), a model zagrożeń i zasady
podpisywania artefaktów OTA, lokalnego rollbacku i docelowego Secure Boot v2 w
[docs/PRODUCTION_SECURITY.md](docs/PRODUCTION_SECURITY.md). Dokładny kontrakt
pakietu `.aqfw`, publiczny fingerprint oraz procedura fabrycznego provisioningu
znajdują się w
[docs/FIRMWARE_SIGNING_AND_PROVISIONING.md](docs/FIRMWARE_SIGNING_AND_PROVISIONING.md).

Opcjonalna brama nie udostępnia sterownika bezpośrednio w Internecie. Przyjmuje
wyłącznie podpisane i chronione przed replayem zdarzenia HTTPS, przechowuje
ograniczoną historię oraz może przekazywać alarmy do FCM. Provisioning odbywa się
po bezpiecznym BLE, a sekret HMAC nie jest zapisywany w aplikacji mobilnej.
Instrukcje uruchomienia znajdują się w [gateway/README.md](gateway/README.md), a
zasady instalowalnego panelu w
[docs/WEB_BUNDLE_AND_GATEWAY_PWA.md](docs/WEB_BUNDLE_AND_GATEWAY_PWA.md).

Sterownika nie należy wystawiać bezpośrednio do Internetu. Zdalny dostęp powinien
działać przez VPN lub uwierzytelnioną bramę, z krótkotrwałymi tokenami i limitami
żądań. Sekrety podpisujące pozostają wyłącznie w chronionych środowiskach CI.
Firmware przyjmuje produkcyjnie tylko podpisany pakiet `.aqfw` zweryfikowany
względem wbudowanego trust anchora. Sprzętowe Secure Boot v2 i Flash Encryption
wymagają dodatkowo kontrolowanego provisioningu każdej płytki; żaden workflow
ani skrypt w repozytorium nie przepala eFuse automatycznie.

## Panel ESP32-P4, bramka ESP32-C6 i Home Assistant

Nowa, opcjonalna architektura zachowuje CYD jako autonomiczny sterownik, a
duży panel Waveshare ESP32-P4 7" traktuje jako odpinany klient LVGL/MQTT.
Nieruchomy ESP32-C6 przekazuje szyfrowany ESP-NOW do MQTT, więc wyłączenie
panelu nie odcina Home Assistanta. Projekty znajdują się w
`firmware/esp32p4_hmi`, `firmware/esp32c6_gateway` i `home_assistant`.
Oba firmware’y są przypięte plikami `dependencies.lock` do ESP-IDF 5.4.4,
budowane w CI i lokalnie jednym poleceniem:

```powershell
.\tools\build-p4-c6.ps1 -IdfPath C:\esp\v5.4.4-full\esp-idf
```

Panel pokazuje pomiary, alarmy, stany wyjść i diagnostykę, edytuje harmonogramy,
profile obu lamp oraz ustawienia temperatury, potwierdza każdą komendę i
zapisuje jasność w NVS. CYD pozostaje źródłem prawdy i atomowo zatwierdza
konfigurację. Pakiet Home Assistanta zawiera dashboard, czasowe sterowanie,
alarmy i przykład najmniejszych uprawnień MQTT.

Pełny opis odpowiedzialności, sprzętu, protokołu, provisioningu i kolejności
migracji znajduje się w
[docs/ESP32_P4_C6_HOME_ASSISTANT_ARCHITECTURE.md](docs/ESP32_P4_C6_HOME_ASSISTANT_ARCHITECTURE.md).

Kierunek produktu, granice odpowiedzialności oraz etapy dojścia od działającego
MVP do instalacji produkcyjnej opisuje
[docs/PROJECT_DIRECTION.md](docs/PROJECT_DIRECTION.md). Zasady wspólnego
projektowania ekranów w Figma i implementowania ich jako natywny LVGL znajdują
się w
[docs/HMI_LVGL_FIGMA_WORKFLOW.md](docs/HMI_LVGL_FIGMA_WORKFLOW.md), a wspólne
tokeny wizualne w `design/hmi/aquacyd-hmi.tokens.json`.

Gotowy pakiet dużego panelu zawiera 13 edytowalnych ramek Figma, manifest
prototypu i specyfikację animacji w `design/hmi`. Mały ekran sterownika CYD
korzysta z tego samego języka wizualnego; jego pięć bieżących stron i wariant
alarmowy są odwzorowane w sześciu ramkach 320×240 w
[`design/cyd-hmi`](design/cyd-hmi/README.md). Docelową instalację lokalnego
serwera na Raspberry Pi 5 4 GB opisuje
[deployment/raspberry-pi5/README.md](deployment/raspberry-pi5/README.md).
