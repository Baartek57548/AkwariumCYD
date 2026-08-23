# AquaCYD CYD UI 320×240

Pakiet odwzorowuje pięć głównych ekranów rzeczywistego sterownika ESP32-CYD
oraz wariant alarmowy ekranu startowego. Jest zgodny wizualnie z panelem
ESP32-P4, ale zachowuje istniejącą nawigację, funkcje i ograniczenia małego
wyświetlacza.

## Zawartość

- `aquacyd-cyd.tokens.json` — tokeny dopasowane do matrycy 320×240;
- `figma-manifest.json` — kolejność importu i mapowanie na kod LVGL;
- `figma-import/*.svg` — sześć edytowalnych ramek;
- `tools/generate-cyd-figma-pack.mjs` — deterministyczny generator ramek.

## Generowanie i import do Figma

```powershell
node design/cyd-hmi/tools/generate-cyd-figma-pack.mjs
```

Pliki SVG można umieścić w tym samym dokumencie Figma co panel P4. Powtarzalne
elementy `Status bar`, `Metric card`, `Device card`, `Navigation item` i
`System tile` należy przekształcić w komponenty. Teksty oraz kształty pozostają
edytowalne.

Źródłem implementacji jest `../../src/gui_app.cpp`. Makiety nie wprowadzają
funkcji, których nie ma w firmware: pięć pozycji nawigacji odpowiada dokładnie
stronom `Start`, `Plan`, `Moduly`, `Wykres` i `System`.
