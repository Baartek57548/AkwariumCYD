---
aliases:
  - Kalibracja pH
  - pH Calibration
tags:
  - procedure
  - sensors
category: procedure
status: complete
---

# 🧪 Procedura — Kalibracja Sondy pH w Płynach Wzorcowych

Szklana elektroda pH wymaga okresowej kalibracji (zazwyczaj raz na 1–2 miesiące), aby skompensować starzenie membrany i dryft punktu zerowego.

Powrót do: [[MOC - Procedury i Diagnostyka]] | Sprzęt: [[Hardware - Przetwornik ADS1115 i pH]]

---

## 📋 Wymagane Materiały
1. Płyn buforowy wzorcowy **pH 7.00** (lub 6.86).
2. Płyn buforowy wzorcowy **pH 4.01** (lub 9.18).
3. Woda demineralizowana / destylowana do płukania elektrody.
4. Ręcznik papierowy do delikatnego osuszania.

---

## ⚙️ Procedura Kalibracji Krok po Kroku

### Krok 1: Punkt Neutralny (pH 7.00)
1. Przepłucz sondę w wodzie destylowanej i osusz delikatnie bibułą.
2. Zanurz sondę w buforze pH 7.00 i odczekaj 60 sekund na ustabilizowanie odczytu.
3. W programie diagnostycznym (`test_wemos_ads1115` lub `test_wemos_diag` opcja `[6]`) odczytaj napięcie $V_7$.
4. Przy poprawnie działającej sondzie napięcie powinno wynosić ok. **2.500 V** (2500 mV).
5. Wylicz przesunięcie:
   $$	ext{offset} = 7.0 - (7.0 + (2.5 - V_7) \cdot 	ext{slope})$$

### Krok 2: Punkt Kwasowy (pH 4.01)
1. Przepłucz sondę w wodzie destylowanej.
2. Zanurz w buforze pH 4.01 i odczekaj 60 sekund.
3. Odczytaj napięcie $V_4$ (zazwyczaj ok. 3.05 V).
4. Wylicz nachylenie elektrody:
   $$	ext{slope} = rac{7.0 - 4.01}{V_4 - V_7} = rac{2.99}{V_4 - V_7}$$

### Krok 3: Zapis do Pamięci EEPROM
Wyznaczone wartości `ph_cal_offset` oraz `ph_cal_slope` wprowadzane są przez interfejs CYD lub panel WWW i zapisywane trwale w pamięci [[Firmware - WeMos Pamięć EEPROM i Storage|EEPROM]] WeMos.

---

## 🔗 Powiązane Notatki
- [[Hardware - Przetwornik ADS1115 i pH]]
- [[Firmware - WeMos Pamięć EEPROM i Storage]]
- [[Procedura - Diagnostyka i Self-Test WeMos]]
