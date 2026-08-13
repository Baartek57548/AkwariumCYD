# Raport QA — Home Control 2.0 / monorepo

Data raportu: 2026-08-13. Gałąź: `codex/home-control-monorepo`.

## Wynik końcowy

Kod, testy hostowe i wszystkie kompilacje bez sprzętu są zielone. Stan jest
gotowy do przeglądu i testów HIL, ale nie do publikacji produkcyjnego tagu:
brakuje fizycznego stanowiska CYD/P4/C6 oraz kluczy właściciela.

| Obszar | Wykonana brama | Wynik |
| --- | --- | --- |
| Home Control | `flutter analyze`, pełne `flutter test --coverage` | PASS, 0 problemów, 105 testów, 62,22% pokrycia |
| Home Control | web release, Android debug/release | PASS, APK release 55,9 MB; debug APK publikowany przez CI |
| AquaCYD Service | analyze, pełne testy | PASS, 0 problemów, 240 testów |
| AquaCYD Service | Android debug/release z jednorazowym kluczem walidacyjnym | PASS, APK release 65,5 MB |
| Pakiety Dart | analyze/test każdego pakietu | PASS, 5 testów |
| Node/API | API i secure gateway | PASS, 16 + 17 testów |
| Zależności Node | `npm audit --omit=dev --audit-level=high` | PASS, 0 podatności |
| Panel web | Chromium/Firefox/WebKit, mobile, desktop i a11y | PASS, 44 testy |
| CYD domena | PlatformIO `native` | PASS, 40 testów |
| CYD firmware | `esp32dev`, `esp32dev-st7789`, `esp32dev-espnow` | PASS, trzy obrazy |
| ESP32-C6 | ESP-IDF 5.4.4 | PASS, 91% partycji aplikacji wolne |
| ESP32-P4 | ESP-IDF 5.4.4, ograniczona równoległość | PASS, 79% partycji aplikacji wolne |
| Projekty HMI | generator, XML/JSON i brak różnic po regeneracji | PASS, 13 ramek P4 + 6 ramek CYD |
| Narzędzia | actionlint, compileall, release, firmware, web, SBOM, security | PASS |
| Sekrety i duże pliki | Gitleaks dla historii gałęzi, pliki śledzone powyżej 5 MiB | PASS, brak rzeczywistych trafień |
| Podatności zależności | OSV-Scanner 2.3.8, rekurencyjny skan lockfile'i Dart, npm i Python | PASS, brak znanych podatności |
| SBOM walidacyjny | CycloneDX 1.6 dla Home Control i AquaCYD Service | PASS, generowany i publikowany przez CI |
| HIL mock | pełny self-test | PASS, 12 scenariuszy + 1 jawny SKIP portu |
| HIL dry-run | uruchomienie bez stanowiska | 0 FAIL, 13 SKIP z podaną przyczyną |
| HIL fizyczny | `--require-hardware --forbid-skips` | BLOCKED bez stanowiska |
| Produkcyjne podpisy | klucze Android/OTA i fingerprint właściciela | BLOCKED bez sekretów CI |

Pierwsza równoległa kompilacja P4 została przerwana przez proces kompilatora bez
diagnostyki przy małym zapasie pamięci hosta. Powtórzenie jednym zadaniem przeszło
w całości. Skrypt `tools/build-p4-c6.ps1` domyślnie używa teraz jednego zadania,
a CI dwóch, aby wynik nie zależał od chwilowej presji RAM.

## Budżety firmware

- CYD `esp32dev`: 36,7% RAM i 96,1% partycji aplikacji;
- CYD `esp32dev-st7789`: obraz zbliżony rozmiarem do wariantu ILI9341;
- CYD `esp32dev-espnow`: 37,5% RAM i 96,9% partycji aplikacji;
- C6: obraz `0x28940`, 91% najmniejszej partycji aplikacji wolne;
- P4: obraz `0x10c1e0`, 79% najmniejszej partycji aplikacji wolne;
- bootloader P4: obraz `0x5d10`, tylko 3% przydzielonego obszaru wolne.

Warianty CYD oraz bootloader P4 są blisko limitów. Nowe funkcje należy dodawać z
bramą rozmiaru; zwiększenie partycji wymaga osobnej, świadomej migracji układu
flash i testu aktualizacji istniejącego urządzenia.

## Pokrycie funkcjonalne Home Control

- onboarding AquaHub, Home Assistant i Demo;
- wiele niezależnych profili Home Assistant w systemowym secure storage;
- natywne REST, WebSocket, registry areas/devices/entities/services i reconnect;
- historia REST oraz długoterminowe statystyki Recorder przez WebSocket z
  bezpiecznym fallbackiem dla encji bez statystyk i starszych serwerów;
- pomieszczenia, urządzenia, 28 typów encji i bezpieczny fallback unknown;
- kontrolki domenowe, potwierdzenia ryzyka, ACK/rollback i historia;
- akwarium, alarm, sceny, skrypty, automatyzacje i aktualizacje;
- edytowalny dashboard, PL/EN, motywy, telefon, tablet i duży tekst;
- premium UI: status domu, centrum uwagi, adaptacyjna nawigacja 800×480,
  semantyczne kolory, cele 48 dp i goldeny telefonu/panelu w obu motywach;
- opcjonalna biometria dla krytycznych poleceń i instalacji OTA z bezpiecznym
  zachowaniem przy anulowaniu, lockoucie lub braku skonfigurowanej biometrii;
- wersjonowany cache offline odporny na uszkodzony snapshot i mieszanie źródeł;
- obsługa błędów tokenu, sieci, certyfikatu i serwera;
- automatyczne sprawdzenie OTA przy starcie oraz wznowieniu aplikacji.

## Blokery właściciela

1. Produkcyjne OAuth Home Assistant wymaga publicznego Client ID, kontrolowanego
   redirect URI i hostowanej polityki właściciela. Token długoterminowy pozostaje
   jawną opcją zaawansowaną, a nie atrapą OAuth.
2. Produkcyjne APK wymagają keystore i fingerprintu certyfikatu właściciela.
3. Secure Boot i eFuse wymagają kontrolowanego fizycznego provisioningu.
4. C6 ma dwie partycje OTA, ale kanał pozostaje `unsupported`, dopóki podpisana
   instalacja, health check i rollback nie zostaną wdrożone i sprawdzone na płytce.
5. P4, C6 i CYD wymagają pełnego HIL bez pominiętych scenariuszy.
6. Produkcyjny tag i GitHub Release są zabronione do czasu zamknięcia punktów 2–5.

## Interpretacja

Zielone testy hostowe i kompilacje oznaczają gotowość kodu do przeglądu oraz HIL,
nie certyfikację sprzętu. Wynik mock nigdy nie jest przedstawiany jako test
fizyczny. CI generuje wyłącznie artefakty walidacyjne bez produkcyjnych sekretów;
kanał produkcyjny może zostać aktywowany dopiero po podpisaniu i odbiorze HIL.
