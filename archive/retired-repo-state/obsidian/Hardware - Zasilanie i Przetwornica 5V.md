---
aliases:
  - Zasilanie AquaBox
  - Przetwornica 5V
  - Power Supply
tags:
  - hardware
  - power
category: hardware
status: complete
---

# ⚡ Hardware — Zasilanie Główne i Przetwornica Step-Down 5V

Bezpieczna i stabilna dystrybucja zasilania w obudowie [[AquaBox - Założenia Konstrukcyjne CAD|AquaBox]] jest fundamentem bezawaryjnej pracy 24/7.

Powrót do: [[MOC - Sprzęt i Peryferia]] | CAD: [[AquaBox - Założenia Konstrukcyjne CAD]]

---

## 🔌 Schemat Dystrybucji Zasilania

```mermaid
graph TD
    AC["Sieć 230V AC (Gniazdo C14 z bezpiecznikiem)"] --> PSU["Zasilacz Impulsowy 12V DC 5A (MeanWell / LRS)"]
    AC --> REL_230["Styki Przekaźników 230V (Filtr, Grzałka)"]
    
    PSU --> BUCK["Przetwornica Step-Down DC-DC (12V -> 5V 3A)"]
    PSU --> LED["Oświetlenie LED 12V"]
    PSU --> CO2["Zawór CO2 12V"]

    BUCK --> WEMOS["WeMos D1 Uno (Zasilanie 5V)"]
    BUCK --> CYD["ESP32 CYD (Zasilanie 5V)"]
    BUCK --> COILS["Cewki 8 Przekaźników (5V)"]
    BUCK --> I2C_BUS["Szyna I2C (MCP23017, ADS1115, DS3231)"]

    GND["Wspólna Szyna Masy (Star Grounding)"] --- PSU
    GND --- BUCK
    GND --- WEMOS
    GND --- CYD
```

---

## ⚠️ Zasady Bezpieczeństwa Elektrycznego
1. **Separacja Galwaniczna i Odstępy Izolacyjne**: Przewody 230V AC są odseparowane fizycznie od linii sygnałowych 3.3V/5V.
2. **Punkt Masy Gwiaździstej (Star Ground)**: Masa zasilacza 12V i przetwornicy 5V spotykają się w jednym punkcie, co eliminuje pętle masy powodujące zakłócenia pomiarów sondy pH.
3. **Kondensatory Odsprzęgające**: Przy zasilaniu płytki przekaźników i przetwornicy zastosowano kondensatory elektrolityczne 470µF + ceramiczne 100nF tłumiące szpilki napięciowe przy wyłączaniu cewek.

---

## 🔗 Powiązane Notatki
- [[Hardware - Moduł 8 Przekaźników]]
- [[AquaBox - Założenia Konstrukcyjne CAD]]
- [[Hardware - WeMos D1 Uno ESP8266]]
