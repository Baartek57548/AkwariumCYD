---
aliases:
  - Wanna Dolna AquaBox
  - Pokrywa AquaBox
tags:
  - cad
  - 3dprint
category: cad
status: complete
---

# 📦 AquaBox — Wanna Dolna i Pokrywa Górna

Szczegółowy opis geometrii głównych korpusów drukowanych w projekcie obudowy [[AquaBox - Założenia Konstrukcyjne CAD|AquaBox]].

Powrót do: [[MOC - Obudowa AquaBox i CAD]] | Druk: [[AquaBox - Zalecenia Druku 3D i Śruby]]

---

## 1. Wanna Dolna (`OBUDOWA PODSTAWA.SLDPRT` / `.STL`)
- **Płyta Denna**: Grubość 3.0 mm ze zintegrowanymi uszami montażowymi na zewnątrz (rozstaw pod wkręty ścienne fi 4.5 mm).
- **Słupki Montażowe z Otworami pod Wkręty M3**:
  - 4 słupki pod WeMos D1 Uno (standardowy rozstaw Arduino Uno).
  - 4 słupki pod zasilacz 12V.
  - 4 słupki pod płytkę prototypową 5x7 cm (MCP/ADS/RTC).
  - 2 słupki pod przetwornicę DC-DC.
  - 4 masywne kolumny fi 9 mm (wysokość 47 mm) pod [[AquaBox - Piętrowy Mostek Przekaźników|mostek przekaźników]].
- **Przepusty i Gniazda w Ściankach Bocznych**:
  - Wycięcie prostokątne pod standardowe gniazdo zasilające C14 z bezpiecznikiem i wyłącznikiem kołyskowym.
  - Okno dostępowe do portu microUSB WeMos (do wgrywania firmware bez rozkręcania obudowy).
  - 3 otwory pod dławiki kablowe M20 (wyjścia kabli 230V do grzałki, filtrów, oświetlenia).
  - 2 otwory pod dławiki kablowe M12 (przewody sondy pH i termometru DS18B20).

---

## 2. Pokrywa Górna (`OBUDOWA POKRYWA.SLDPRT` / `.STL`)
- **Kołnierz Wpustowy**: Schodek o wysokości 3.0 mm wchodzący ciasno w obrys dolnej wanny — usztywnia całą konstrukcję i chroni przed pryskającą wodą.
- **Wentylacja Konwekcyjna**: Matryca skośnych szczelin wentylacyjnych (żaluzji) umieszczona bezpośrednio nad zasilaczem i przekaźnikami.
- **Mocowanie**: 6 otworów z pogłębieniem stożkowym pod śruby M3 z łbem wpuszczanym.

---

## 🔗 Powiązane Notatki
- [[AquaBox - Piętrowy Mostek Przekaźników]]
- [[AquaBox - Zalecenia Druku 3D i Śruby]]
- [[AquaBox - Założenia Konstrukcyjne CAD]]
