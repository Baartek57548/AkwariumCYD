# Raport QA — Home Control 2.0 / monorepo

Data raportu: 2026-08-12. Gałąź robocza: `codex/home-control-monorepo`.

## Zakres

Raport obejmuje aplikacje Home Control i AquaCYD Service, pakiety Dart, panel
web, API Node, firmware CYD, AquaHub P4, Gateway C6, narzędzia wydaniowe i testy
HIL uruchamialne bez sprzętu. Dokładne wyniki finalnego przebiegu są wpisywane
poniżej po zakończeniu macierzy; CI powtarza te same bramy.

| Obszar | Brama | Oczekiwany wynik |
| --- | --- | --- |
| Home Control | `flutter analyze`, pełne `flutter test` | 0 problemów, wszystkie testy PASS |
| Home Control | web release, Android debug/release | build PASS |
| AquaCYD Service | analyze, test, Android debug/release | PASS |
| Pakiety Dart | analyze/test każdego pakietu | PASS |
| Node/API | `npm ci`, `npm test` | PASS |
| Panel web | Playwright desktop/mobile/a11y | PASS |
| CYD domena | PlatformIO native | PASS |
| CYD firmware | wszystkie środowiska PlatformIO | build PASS |
| P4/C6 | ESP-IDF 5.4.4 | build PASS, raport flash/RAM |
| Narzędzia | JSON/YAML/XML, release validator, SBOM | PASS |
| HIL mock | dry-run i self-test | PASS, jawne SKIP portu gdy brak |
| HIL fizyczny | `--require-hardware --forbid-skips` | BLOCKED bez stanowiska |
| Produkcyjne podpisy | weryfikacja kluczy właściciela | BLOCKED bez sekretów CI |

## Pokrycie funkcjonalne Home Control

- onboarding AquaHub / Home Assistant / Demo;
- profile wielu instancji HA, secure storage i selektywne usuwanie;
- REST, WebSocket, rejestry areas/devices/entities/services i reconnect;
- pomieszczenia, urządzenia, 28 typów encji oraz bezpieczny unknown fallback;
- kontrolki domenowe, ACK/rollback, potwierdzenia ryzyka i historia;
- akwarium, alarm, sceny, skrypty, automatyzacje i aktualizacje;
- edytowalny dashboard, PL/EN, motywy, telefon/tablet i duży tekst;
- offline cache, uszkodzony snapshot, błędy token/sieć/certyfikat/serwer;
- automatyczne sprawdzenie OTA przy starcie i wznowieniu.

## Znane ograniczenia i blokery właściciela

1. OAuth HA nie może zostać produkcyjnie aktywowany bez publicznego Client ID,
   polityki redirect URI i hostowanej strony właściciela. Token długoterminowy
   pozostaje jawną opcją zaawansowaną, nie atrapą OAuth.
2. Produkcyjne APK wymagają keystore i fingerprintu certyfikatu właściciela.
3. Secure Boot/eFuse wymagają kontrolowanego fizycznego provisioningu.
4. OTA C6 pozostaje `unsupported`, dopóki nie ma dwóch slotów, podpisu, health i
   rollbacku sprawdzonego na płytce.
5. Produkcyjny tag/release jest zabroniony bez fizycznego HIL bez skipów.

## Interpretacja

Zielone testy hostowe i kompilacje oznaczają gotowość kodu do review i HIL, nie
certyfikację sprzętu. Wynik HIL mock nigdy nie jest przedstawiany jako test
fizyczny. Wydanie może zostać przygotowane jako draft/artefakt CI, ale kanał
produkcyjny jest aktywowany dopiero po zamknięciu wszystkich bramek.
