# Proces wydania

## Niezależne produkty

| Produkt | Wersja/tag | Główne artefakty |
| --- | --- | --- |
| Home Control | `home-vX.Y.Z` | `Home-Control-X.Y.Z.apk`, manifest, SHA-256, SBOM, provenance |
| AquaCYD Service | `mobile-vX.Y.Z` | `AquaCYD-Control-X.Y.Z-current.apk`, manifest, SBOM |
| CYD | `firmware-vX.Y.Z` | warianty `.aqfw`, manifesty, mapy pamięci, SBOM |
| AquaHub P4 | niezależny manifest | `aquahub-p4-X.Y.Z.bin`, manifest, SBOM |
| Gateway C6 | kanał nieaktywny | artefakt laboratoryjny do czasu bezpiecznego OTA |

Wersje aplikacji, firmware, kontraktu i cache nie są automatycznie zrównywane.
Compatibility matrix w manifeście określa minimalne wersje.

## Brama PR

1. Czysty diff bez buildów, cache, `.dart_tool`, sekretów i dużych binariów.
2. Formatowanie, lint, testy, kompilacje i walidatory z `docs/QA_REPORT.md`.
3. Review architektury, bezpieczeństwa, migracji i zgodności wstecznej.
4. SBOM oraz skan zależności/sekretów; wyjątki mają właściciela i termin.
5. PR zawiera ryzyko, plan rollbacku, wyniki oraz jawne blokery HIL/kluczy.

## Brama produkcyjna

1. Pełny fizyczny HIL z `--forbid-skips` i podpis operatora.
2. Build na chronionym runnerze z właścicielskimi kluczami.
3. Niezależna weryfikacja podpisu, package/target, hash i provenance.
4. Instalacja canary, test aktualizacji i wymuszonego rollbacku.
5. Tag utworzony z dokładnie zweryfikowanego commita; release notes zawierają
   migracje, kompatybilność, ograniczenia i procedurę powrotu.
6. Etapowy rollout. Nie nadpisuje się artefaktu ani manifestu pod tą samą wersją.

## Polecenia lokalne

```powershell
flutter analyze apps/home_control
flutter test apps/home_control
flutter analyze apps/aquacyd_service
flutter test apps/aquacyd_service
npm ci
npm test
python scripts/validate_release.py --self-test
python tools/generate_sbom.py --help
python tools/hil/runner.py --self-test
```

Build firmware wykonuje się przez PlatformIO oraz `tools/build-p4-c6.ps1` z
przypiętym ESP-IDF 5.4.4. Workflow GitHub jest kanoniczną automatyzacją.

## Decyzja dla Home Control 2.1.0

Wydanie aplikacji zostało jawnie autoryzowane przez właściciela po przejściu
bramek hostowych, web i Android. Tag `home-v2.1.0` musi uruchomić chroniony
workflow `production-mobile`; tylko jego podpisany i zweryfikowany APK może być
zasobem wydania. Brak bieżącego fizycznego HIL pozostaje opisanym ograniczeniem
i ta decyzja nie autoryzuje tagów firmware ani AquaCYD Service.
