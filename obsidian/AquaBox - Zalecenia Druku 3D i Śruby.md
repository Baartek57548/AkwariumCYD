---
aliases:
  - Druk 3D AquaBox
  - Śruby i Montaż
tags:
  - cad
  - 3dprint
category: cad
status: complete
---

# 🖨️ AquaBox — Wytyczne Druku 3D i Elementy Złączne

Praktyczny poradnik przygotowania plików STL w slicerze (Bambu Studio, PrusaSlicer, Cura) oraz zestawienie śrub do montażu.

Powrót do: [[MOC - Obudowa AquaBox i CAD]] | CAD: [[AquaBox - Założenia Konstrukcyjne CAD]]

---

## 🧵 Dobór Filamentu

| Materiał | Zdatność do AquaBox | Zalety / Wady |
| :--- | :---: | :--- |
| **PETG** | ⭐⭐⭐⭐⭐ **ZALECANY** | Bardzo wysoka odporność na wilgoć, dobra wytrzymałość termiczna (do 75°C), zerowy skurcz, łatwy druk. |
| **ABS / ASA** | ⭐⭐⭐⭐ BARDZO DOBRY | Wysoka odporność termiczna (do 95°C), wymaga komory roboczej ze względu na skurcz. |
| **PLA** | ❌ **NIEZALECANY** | Zmięknie pod wpływem ciepła zasilacza 12V i ulegnie degradacji w wilgotnym środowisku szafki. |

---

## ⚙️ Parametry Slicera
- **Wysokość warstwy**: `0.20 mm` (lub `0.28 mm` dla szybszego druku wanny dolnej).
- **Liczba obrysów (Walls / Perimeters)**: **minimum 4** (zapewnia pełną litość słupków gwintowanych).
- **Warstwy pełne (Góra/Dół)**: 5 warstw dolnych, 5 warstw górnych.
- **Wypełnienie (Infill)**: **25%** (Wzór: `Gyroid` lub `Grid`).
- **Podpory (Supports)**: Wymagane jedynie pod otworami dławików kablowych i gniazda C14 (typ: *Tree Supports* / podpory organiczne).

---

## 🔩 Zestawienie Śrub i Elementów Złącznych (BOM)

- **16x Wkręt / Śruba M3 x 6 mm** — Montaż płytek PCB, WeMos, przetwornicy i zasilacza do słupków.
- **4x Śruba M3 x 10 mm** — Przykręcenie mostka przekaźników do kolumn dolnych.
- **4x Śruba M3 x 8 mm** — Przykręcenie płytki przekaźników do mostka.
- **6x Śruba M3 x 12 mm ze łbem stożkowym** — Skręcenie pokrywy górnej z wanną dolną.
- **Opcjonalnie**: Wprasowywane gwintowane wkładki mosiężne M3 (*heat-set inserts* M3x4x5).

---

## 🔗 Powiązane Notatki
- [[AquaBox - Założenia Konstrukcyjne CAD]]
- [[AquaBox - Wanna Dolna i Pokrywa]]
- [[AquaBox - Piętrowy Mostek Przekaźników]]
