# Raport QA — Home Control 2.2 / monorepo

Data raportu: 2026-08-18. Gałąź: `codex/home-control-monorepo`.

## Wynik końcowy

Kod Home Control, testy hostowe oraz lokalne kompilacje web i Android są zielone.
Natywny build Windows x64 jest kanonicznie wykonywany na GitHub `windows-2025`,
ponieważ lokalna instalacja Visual Studio nie zawiera ATL wymaganego przez jedną
z wtyczek Fluttera. Właścicielskie sekrety podpisu Androida są skonfigurowane
wyłącznie w chronionym środowisku GitHub `production-mobile`; lokalny build nie
korzysta z klucza produkcyjnego. Fizyczny HIL CYD/P4/C6 nie został wykonany i nie
jest przedstawiany jako zastąpiony przez testy hostowe. Zmiana 2.2.0 nie
modyfikuje firmware, ale ten brak pozostaje jawnym ograniczeniem odbioru całego
systemu.

| Obszar | Wykonana brama | Wynik |
| --- | --- | --- |
| Home Control | pełny formatter Dart, `flutter analyze --no-pub`, `flutter test --no-pub` | PASS, 72 pliki, 0 problemów, 110 testów |
| Home Control | web release i Android debug | PASS; web 38 250 510 B, debug APK 158 651 696 B |
| Home Control APK debug | `aapt`, `apksigner`, SHA-256 | PASS; `pl.aquacyd.aquacyd_home`, `2.2.0`/`8`, jeden prawidłowy sygnatariusz debug, SHA-256 `7b76a8954f27428ddd0905a592faf52c00b438e22c8cdd18c3b15afc961dbbe5` |
| Home Control UI | testy widget, goldeny i regresja responsywności w zestawie 110 testów | PASS; telefon, panel 800×480, light/dark, skalowanie tekstu i adaptacyjna nawigacja |
| Home Control Windows x64 | hosted `flutter build windows --release`, Inno Setup, cicha instalacja, start aplikacji przez 5 s i cicha deinstalacja | PASS w CI `32155965239`; `HomeControl.exe` i Setup mają `FileVersion`/`ProductVersion` `2.2.0.8` |
| Home Control Windows Setup | pobrany artefakt CI, ponowny lokalny smoke test, SHA-256 i Authenticode | PASS; 30 155 614 B, SHA-256 `ba4bb782513b78d302b5ce4578582655882e8c44ff9aab2ffcb928b3903250e8`, oczekiwany status `NotSigned` |
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
| Produkcyjny podpis Home Control | chronione sekrety `production-mobile`, `aapt` i `apksigner` w release workflow | CONFIGURED; wynik końcowy wymaga zielonego uruchomienia taga `home-v2.2.0` |

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
- pomieszczenia z kartami kondycji i dedykowanymi szczegółami, urządzenia,
  28 typów encji i bezpieczny fallback unknown;
- kontrolki domenowe, potwierdzenia ryzyka, stan pending bez fałszywego ON przed
  ACK oraz historia;
- akwarium, alarm, sceny, skrypty, automatyzacje i aktualizacje;
- edytowalny dashboard, PL/EN, motywy, telefon, tablet i duży tekst;
- premium UI: status domu, centrum uwagi, sceny/skrypty, adaptacyjna nawigacja
  800×480, semantyczne kolory, cele 48 dp i goldeny telefonu/panelu w obu
  motywach;
- centralne tokeny layoutu, ikon, cieni, elevation i ruchu oraz współdzielone
  karty, statusy, kontrolki, panele i stany loading/empty/error;
- opcjonalna biometria dla krytycznych poleceń i instalacji OTA z bezpiecznym
  zachowaniem przy anulowaniu, lockoucie lub braku skonfigurowanej biometrii;
- natywne okno Windows x64 z obsługą DPI, ograniczeniem rozmiaru do obszaru
  roboczego monitora i responsywną nawigacją dla małych okien;
- Windows Hello korzysta z poświadczenia systemowego i może zaakceptować PIN;
  platforma Windows nie obsługuje wymuszenia trybu wyłącznie biometrycznego;
- wersjonowany cache offline odporny na uszkodzony snapshot i mieszanie źródeł;
  na Windows snapshot topologii i stanów trafia do nieromingowego LocalAppData
  cache, jest jawnie oddzielony od tokenów i danych parowania, ale nie jest
  szyfrowany;
- obsługa błędów tokenu, sieci, certyfikatu i serwera;
- automatyczne sprawdzenie OTA przy starcie oraz wznowieniu aplikacji.

## Blokery właściciela

1. Produkcyjne OAuth Home Assistant wymaga publicznego Client ID, kontrolowanego
   redirect URI i hostowanej polityki właściciela. Token długoterminowy pozostaje
   jawną opcją zaawansowaną, a nie atrapą OAuth.
2. Produkcyjne APK powstają wyłącznie w chronionym workflow; lokalny host nie ma
   i nie powinien mieć prywatnego keystore właściciela.
3. Instalator Windows nie ma certyfikatu Authenticode właściciela, dlatego Windows
   SmartScreen może wymagać dodatkowego potwierdzenia. Dołączany instalator VC++
   Runtime zachowuje prawidłowy podpis Microsoftu; nie jest to substytut podpisu
   aplikacji ani Setupu.
4. Secure Boot i eFuse wymagają kontrolowanego fizycznego provisioningu.
5. C6 ma dwie partycje OTA, ale kanał pozostaje `unsupported`, dopóki podpisana
   instalacja, health check i rollback nie zostaną wdrożone i sprawdzone na płytce.
6. P4, C6 i CYD wymagają pełnego HIL bez pominiętych scenariuszy.
7. Wydanie Home Control musi jawnie podawać brak bieżącego fizycznego HIL;
   produkcyjne tagi firmware pozostają zablokowane do zamknięcia punktów 4–6.

## Interpretacja

Zielone testy hostowe i kompilacje oznaczają gotowość aplikacji do wydania oraz
dalszego HIL, nie certyfikację sprzętu. Wynik mock nigdy nie jest przedstawiany
jako test fizyczny. CI gałęzi generuje artefakty walidacyjne. Chroniony workflow
taga ponownie analizuje i testuje kod, buduje Windows Setup oraz APK kluczem
właściciela, sprawdza dokładny zestaw i wersje artefaktów, package, pojedynczego
sygnatariusza APK i fingerprint, a następnie publikuje SHA-256, manifest,
wspólny CycloneDX SBOM i provenance. Setup pozostaje celowo niepodpisany do czasu
dostarczenia certyfikatu Authenticode przez właściciela.
