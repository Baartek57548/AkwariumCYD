---
aliases:
  - Obudowa AquaBox
  - CAD MOC
tags:
  - moc
  - cad
  - solidworks
  - 3dprint
category: moc
status: complete
---

# 📦 MOC — Obudowa AquaBox i Modele 3D SolidWorks

Wszystkie pliki CAD, złożenia i modele do druku 3D znajdują się w katalogu:
`cydAquarium/AquaBox/` w repozytorium.

Powrót do: [[00 - AquaCYD Knowledge Hub]] | Sprzęt: [[MOC - Sprzęt i Peryferia]]

---

## 📐 Koncepcja Obudowy AquaBox

Obudowa **AquaBox** to zwarta, dwupoziomowa stacja zasilająco-sterująca montowana w szafce akwarium. Mieści wszystkie komponenty wysokonapięciowe (zasilacz 12V, listwa przekaźników) oraz niskonapięciową elektronikę sterującą.

### Główne Elementy Złożenia (`ZLOZENIE KOMPLETNE.SLDASM`):
1. **Wanna Dolna** (`OBUDOWA PODSTAWA.SLDPRT` / `.STL`):
   - Wymiary zewnętrzne: ok. 240 x 180.5 x 82 mm.
   - Grubość ścianek i dna: 3.0 mm.
   - Słupki montażowe pod WeMos D1 Uno, zasilacz 12V, przetwornicę DC-DC, płytkę PCB (MCP/ADS/RTC).
   - Otwory pod dławiki kablowe (wejścia kabli grzałki, pomp, oświetlenia, sond).
   - Gniazdo zasilania C14 z bezpiecznikiem i włącznikiem kołyskowym.
2. **Wspornik Piętrowy Przekaźników** (`WSPORNIK PRZEKAZNIKOW.SLDPRT` / `.STL`):
   - Konstrukcja mostkowa wyniesiona na 4 kolumnach (wysokość Y=47 mm) bezpośrednio nad zasilaczem sieciowym 12V.
   - Oszczędność miejsca w szafce i separacja termiczna.
3. **Pokrywa Górna** (`OBUDOWA POKRYWA.SLDPRT` / `.STL`):
   - Schodkowy kołnierz wpustowy zapobiegający wnikaniu wilgoci.
   - Szczeliny wentylacyjne konwekcyjne.
   - Mocowanie na 6 stożkowych śrub M3.

---

## 🖨️ Wytyczne Druku 3D
- **Materiał**: **PETG** lub **ABS/ASA** (zalecany PETG ze względu na odporność na wilgoć, temperaturę do 75°C i brak skurczu). Unikać zwykłego PLA!
- **Warstwa**: 0.20 mm lub 0.28 mm.
- **Obrysy (Perimeters)**: minimum 4 (zapewnia szczelność i wytrzymałość słupków pod śruby).
- **Wypełnienie (Infill)**: 25% Gyroid lub Grid.

---

## 🔗 Powiązane Notatki
- [[AquaBox - Założenia Konstrukcyjne CAD]]
- [[AquaBox - Wanna Dolna i Pokrywa]]
- [[AquaBox - Piętrowy Mostek Przekaźników]]
- [[AquaBox - Zalecenia Druku 3D i Śruby]]
- [[Hardware - Zworki UART i Izolacja]]
