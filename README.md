# cydAquarium

Sterownik akwarium dla ESP32 CYD z interfejsem LVGL, panelem WWW, BLE,
obsługą OTA i aplikacją Flutter.

Dokumentacja firmware, pinologia, profile kompilacji i procedura wgrywania:
[docs/CYD_FIRMWARE.md](docs/CYD_FIRMWARE.md).

Gotowe obrazy ILI9341 i ST7789 do pobrania znajdują się w
[`artifacts/`](artifacts/CYD-FIRMWARE-2026.07.26.md).

Najważniejsze katalogi:

- `src/`, `include/`, `lib/` — firmware ESP32;
- `test/` — testy logiki domenowej;
- `web/` — panel WWW;
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
```

Konfiguracja pipeline i wydań jest opisana w
[docs/PRODUCTION_CI_CD.md](docs/PRODUCTION_CI_CD.md), stanowisko sprzętowe w
[docs/PRODUCTION_HIL.md](docs/PRODUCTION_HIL.md), a model zagrożeń i zasady
podpisywania artefaktów OTA, lokalnego rollbacku i docelowego Secure Boot v2 w
[docs/PRODUCTION_SECURITY.md](docs/PRODUCTION_SECURITY.md).

Sterownika nie należy wystawiać bezpośrednio do Internetu. Zdalny dostęp powinien
działać przez VPN lub uwierzytelnioną bramę, z krótkotrwałymi tokenami i limitami
żądań. Sekrety podpisujące pozostają wyłącznie w chronionych środowiskach CI.
Do czasu provisionowania klucza zaufania w ESP32 aktualizację firmware wolno
wykonywać tylko lokalnie, po ręcznej weryfikacji SHA-256; automatyczne zdalne OTA
nie jest jeszcze funkcją produkcyjną.
