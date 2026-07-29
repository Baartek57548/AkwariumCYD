# Zasoby projektowe HMI

Katalog przechowuje wizualny kontrakt panelu ESP32-P4 niezależnie od narzędzia
projektowego.

- `aquacyd-hmi.tokens.json` — kolory, odstępy, promienie, typografia i wymiary;
- `figma-import/dashboard-online.svg` — dashboard w stanie online;
- `figma-import/controls-ready.svg` — sterowanie bez oczekującej komendy;
- `figma-import/system-online.svg` — diagnostyka i ustawienia panelu.

Pliki SVG mają dokładnie 1024×600 px, używają wyłącznie prostych kształtów oraz
tekstu i mogą zostać zaimportowane do Figma przez `File → Place image`. Po
imporcie należy zachować nazwy grup, utworzyć z nich komponenty i podpiąć
zmienne z tokenów. SVG są punktem startowym oraz referencją w repozytorium;
zatwierdzona ramka Figma i odpowiadający jej kod LVGL muszą być dalej
porównywane zrzut do zrzutu.

Przepływ i kryteria akceptacji opisuje
`../../docs/HMI_LVGL_FIGMA_WORKFLOW.md`.
