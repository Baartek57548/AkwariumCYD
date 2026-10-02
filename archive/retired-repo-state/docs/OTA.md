# Aktualizacje AquaCYD

## Produkty

Wydawany jest firmware CYD dla dwóch kontrolerów LCD:

| Cel | Środowisko | Artefakt |
| --- | --- | --- |
| ILI9341 | `cyd_2_8` | podpisany BIN i `.aqfw` |
| ST7789 | `esp32dev-st7789` | podpisany BIN i `.aqfw` |

Pakiet WWW na kartę SD jest budowany z `web/` do `sdcard/aq/ota/` i otrzymuje
manifest oraz SHA-256. WeMos jest wgrywany lokalnie po osobnej kompilacji; nie
ma zdalnej ścieżki aktualizacji w aktualnym zakresie.

## Zasady

- wersja, target, SHA-256, security version i podpis muszą się zgadzać;
- downgrade security version jest odrzucany;
- aktualizacja zapisuje nieaktywny slot i wymaga health checku;
- zanik zasilania nie może zatwierdzić niepełnego obrazu;
- użytkownik widzi postęp, wynik i instrukcję odzyskiwania;
- nie istnieją kanały aktualizacji aplikacji Flutter, P4 ani C6.
