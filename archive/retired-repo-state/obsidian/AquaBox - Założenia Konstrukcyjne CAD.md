---
aliases:
  - Założenia CAD AquaBox
  - AquaBox Assembly
tags:
  - cad
  - solidworks
category: cad
status: complete
---

# 📦 AquaBox — Założenia Konstrukcyjne i Złożenie CAD

Pliki projektu: `cydAquarium/AquaBox/`.
Główne złożenie: `ZLOZENIE KOMPLETNE.SLDASM` wykonane w środowisku **SolidWorks 2024**.

Powrót do: [[MOC - Obudowa AquaBox i CAD]] | Druk: [[AquaBox - Zalecenia Druku 3D i Śruby]]

---

## 📐 Koncepcja Przestrzenna i Wymiary

Obudowa została zwymiarowana tak, aby pomieścić kompletny układ zasilania i sterowania przy zachowaniu zwartej bryły montowanej na ścianie w szafce akwarium:
- **Długość całkowita**: 240.0 mm
- **Szerokość całkowita**: 180.5 mm
- **Wysokość całkowita**: 82.0 mm
- **Grubość ścianki nośnej**: 3.0 mm

```
+-------------------------------------------------------------+
| Gniazdo C14 |        Zasilacz Sieciowy 12V 5A               |
| z włączn.   |  (Nad nim: Mostek z 8 Przekaźnikami Y=47mm)   |
+-------------+-----------------------------------------------+
| Przetwornica| Płytka Pośrednia PCB 5x7cm   | Płytka WeMos   |
| Step-Down 5V| (MCP23017, ADS1115, DS3231)  | D1 Uno (R3)    |
+-------------+------------------------------+----------------+
| Dławiki M20 | Dławiki M12 (Sondy pH/Temp)  | Port USB WeMos |
+-------------------------------------------------------------+
```

---

## 🧩 Lista Części Złożenia (`SLDPRT`)

1. `OBUDOWA PODSTAWA.SLDPRT` — Wanna dolna ze słupkami montażowymi.
2. `OBUDOWA POKRYWA.SLDPRT` — Górna pokrywa ze szczelinami wentylacyjnymi.
3. `WSPORNIK PRZEKAZNIKOW.SLDPRT` — Piętrowy mostek nośny dla modułu przekaźników.
4. `ZASILACZ SIECIOWY 12V.SLDPRT` — Bryła gabarytowa zasilacza 12V.
5. `ARDUINO UNO R3 (ESP8266).SLDPRT` — Bryła płytki WeMos D1 Uno.
6. `LISTWA 8 PRZEKAZNIKOW.SLDPRT` — Bryła płytki przekaźników.
7. `PCB 5x7mm.SLDPRT` — Płytka uniwersalna z układami scalonymi.
8. `PRZETWORNICA 5V.SLDPRT` — Moduł przetwornicy step-down.

---

## 🔗 Powiązane Notatki
- [[AquaBox - Wanna Dolna i Pokrywa]]
- [[AquaBox - Piętrowy Mostek Przekaźników]]
- [[AquaBox - Zalecenia Druku 3D i Śruby]]
