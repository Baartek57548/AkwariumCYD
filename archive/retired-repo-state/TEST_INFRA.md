# Infrastruktura testowa AquaCYD

## Testy programowe

```powershell
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
```

## Testy przeglądarkowe

Uruchom `npm run dev:web`, sprawdź widok telefonu i komputera, nawigację,
formularze, komunikaty błędów oraz konsolę. Test symulatora nie zastępuje HIL.

## HIL

Prawdziwy test obejmuje CYD, WeMos, UART, kartę SD, oba warianty LCD, dotyk,
Wi-Fi i zapis ustawień. Wyniku HIL nie wolno deklarować na podstawie samej
kompilacji.
