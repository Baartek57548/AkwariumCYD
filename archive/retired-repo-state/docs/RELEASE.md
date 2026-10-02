# Proces wydania AquaCYD

## Kanał

Jedyny tag wydawniczy ma format `firmware-vX.Y.Z`. Publikacja obejmuje dwa
podpisane obrazy CYD, dwa pakiety `.aqfw`, pakiet WWW, manifesty, SHA-256,
SBOM-y i provenance.

## Brama przed tagiem

```powershell
npm ci
npm run build:web-assets
npm run test:api
npm test
pio test -e native
pio test -e native_ui
pio run -e cyd_2_8
pio run -e esp32dev-st7789
pio run -e wemos_d1_uno
python scripts/validate_release.py --self-test
python tools/generate_sbom.py --self-test
git diff --check
```

Do wydania produkcyjnego dochodzi fizyczny HIL z zakazem pominięć. Klucz
podpisujący pozostaje w chronionym środowisku CI. Istniejącego wydania nie wolno
nadpisywać.

## Wgranie lokalne

Po zielonej bramie pakiet `sdcard/aq` jest kopiowany na czystą kartę, a
właściwy wariant firmware wgrywany przez PlatformIO. Po restarcie należy
sprawdzić ekran, dotyk, UART, Wi-Fi, zmianę ustawień i brak nowych resetów.
