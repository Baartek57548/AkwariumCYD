# Macierz OTA i aktualizacji

| Produkt | Target / kanał | Sprawdzenie | Instalacja | Rollback |
| --- | --- | --- | --- | --- |
| Home Control | `home-vX.Y.Z` | Android automatycznie; Windows ręcznie z release | Android po zgodzie; Windows przez Setup; iOS/web przez mechanizm platformy | poprzedni APK / poprzedni Setup / store |
| AquaCYD Service | `mobile-vX.Y.Z` | w aplikacji serwisowej | Android po zgodzie i weryfikacji podpisu | poprzedni APK / store |
| CYD Controller | `firmware-vX.Y.Z` + board ID | ręcznie z serwisu lub kontrolowany kanał | podpisany `.aqfw`, nieaktywny slot | bootloader po nieudanym health |
| AquaHub P4 | osobny manifest `aquahub-p4` | UI Home Control/HMI | HTTPS do nieaktywnej partycji | ESP-IDF app rollback |
| Gateway C6 | `aquahub-c6` | stan `unsupported` do ukończenia bezpiecznej ścieżki | niedostępne produkcyjnie | wymagane przed aktywacją |
| Home Assistant | encje domeny `update` | REST/WebSocket | `update.install` z backupem po potwierdzeniu | odpowiedzialność integracji HA |

## Wspólny kontrakt manifestu

Manifest musi zawierać co najmniej: produkt, target/płytkę, wersję semantyczną,
numer builda lub security version, nazwę pliku, rozmiar, SHA-256, minimalną wersję
bootloadera i kontraktu, flagę mandatory, notatki oraz podpis/atestację. Parser
odrzuca przekierowanie do niezaufanego hosta, zbyt duży plik, downgrade,
niezgodny target, brak pola, zły hash i niewłaściwy podpis.

## Home Control

Kontroler OTA startuje wraz z aplikacją. Stabilny kanał ignoruje draft i
prerelease, a ponowne wejście po ustawieniu zgody Androida wznawia instalację.
APK musi nazywać się `Home-Control-X.Y.Z.apk`. Tag `home-vX.Y.Z` zostaje
zachowany, aby istniejące instalacje mogły migrować bez utraty kanału. Kanał beta
powinien być oddzielnym, opt-in prerelease i nie jest domyślnie włączony.

Ten sam release zawiera
`Home-Control-X.Y.Z-Windows-x64-Setup.exe`. Windows nie uruchamia androidowego
updatera: użytkownik pobiera nowszy Setup, który wykonuje upgrade in-place,
zachowuje dane profilu i blokuje downgrade. Do czasu wdrożenia Authenticode
SmartScreen może wymagać ręcznego potwierdzenia; przed uruchomieniem należy
porównać SHA-256 z `SHA256SUMS` albo zweryfikować GitHub provenance.

## Firmware

Pobranie i zapis nie oznacza sukcesu. Nowy obraz jest `pending verify` aż do
uruchomienia tasków bezpieczeństwa, sterowania, komunikacji i UI. Watchdog,
brownout, brak sieci lub uszkodzona konfiguracja przed potwierdzeniem prowadzą do
rollbacku. Podniesienie security version jest jednokierunkową decyzją właściciela.

## Publikacja

1. Uruchom pełną macierz z [QA_REPORT.md](QA_REPORT.md).
2. Wykonaj fizyczny [HIL_CHECKLIST.md](HIL_CHECKLIST.md) bez skipów.
3. Zbuduj z chronionymi kluczami, wygeneruj SBOM i provenance.
4. Zweryfikuj artefakt niezależnym narzędziem i testową instalacją.
5. Opublikuj plik, a manifest atomowo jako ostatni element kanału.
6. Wykonaj rollout na urządzeniu canary, potem etapami; monitoruj rollback.

Procedura P4: [AQUAHUB_OTA_RELEASES.md](AQUAHUB_OTA_RELEASES.md). Produkcyjne
podpisywanie: [FIRMWARE_SIGNING_AND_PROVISIONING.md](FIRMWARE_SIGNING_AND_PROVISIONING.md).
