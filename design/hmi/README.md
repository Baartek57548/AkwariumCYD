# Zasoby projektowe HMI

Katalog jest wersjonowanym źródłem projektu Figma i natywnego interfejsu LVGL:

- `aquacyd-hmi.tokens.json` — kolory, odstępy, promienie, typografia i czasy;
- `motion-spec.json` — animacje wraz z mapowaniem Figma ↔ LVGL;
- `figma-manifest.json` — kolejność importu i połączenia prototypu;
- `figma-import/*.svg` — 13 kompletnych ramek 1024×600;
- `tools/generate-figma-pack.mjs` — deterministyczny generator ramek.

Pakiet obejmuje fundamenty, start, dashboard normalny i krytyczny, sterowanie,
dialog potwierdzenia, czujniki, alarmy, automatykę, edytor harmonogramu, edytor
temperatury, diagnostykę oraz odzyskiwanie po utracie CYD.

## Import do Figma

1. Uruchom `node design/hmi/tools/generate-figma-pack.mjs`.
2. W Figma utwórz strony `00 Foundations`, `01 Components`, `02 Screens`,
   `03 Flows` i `04 Handoff`.
3. Zaimportuj pliki zgodnie z `figma-manifest.json` przez `File → Place image`.
4. Rozgrupuj SVG, zamień powtarzalne elementy na komponenty i przypisz zmienne
   zgodnie z `aquacyd-hmi.tokens.json`.
5. Połącz warianty prototypu według `prototypeConnections`.
6. Ustaw przejścia i pętle dokładnie według `motion-spec.json`.

Teksty i kształty pozostają edytowalne. SVG są punktem startowym do komponentów
Figma, a nie spłaszczonymi zrzutami. Generator można uruchamiać wielokrotnie;
tworzy ten sam zestaw ramek bez zasobów zewnętrznych.

Implementacja odpowiada plikowi
`../../firmware/esp32p4_hmi/main/hmi_ui.cpp`, a pełny proces odbioru opisuje
`../../docs/HMI_LVGL_FIGMA_WORKFLOW.md`.
