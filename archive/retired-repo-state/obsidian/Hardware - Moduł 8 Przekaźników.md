---
aliases:
  - Moduł 8 Przekaźników
  - 8 Relay Board
tags:
  - hardware
  - relays
category: hardware
status: complete
---

# ⚡ Hardware — Moduł 8 Przekaźników (Active-Low)

Moduł 8 przekaźników elektromechanicznych z izolacją transoptorową służy do załączania urządzeń wykonawczych akwarium (zarówno 230V AC, jak i 12V DC).

Powrót do: [[MOC - Sprzęt i Peryferia]] | Sterowanie: [[Hardware - Ekspander MCP23017]]

---

## 📌 Parametry i Zasada Działania

- **Napięcie cewek**: 5V DC.
- **Obciążalność styków**: do 10A 250V AC / 10A 30V DC.
- **Izolacja**: Transoptory PC817 dla każdego kanału.
- **Sterowanie**: **Active-Low** — podanie masy (`GND` / stan logiczny `0`) powoduje zaświecenie diody LED transoptora i załączenie styków przekaźnika (zwarcie `COM` z `NO`).

---

## 🗺️ Przypisanie Kanałów w Systemie

| Nr Kanału | Odbiornik | Napięcie | Tryb Domyślny | Opis Bezpieczeństwa |
| :---: | :--- | :---: | :---: | :--- |
| **0** | Światło Główne A | 230V / 12V | OFF | Sterowane harmonogramem dobowym |
| **1** | Światło Roślinne B | 230V / 12V | OFF | Sterowane harmonogramem dobowym |
| **2** | **Filtr Główny** | 230V | **ON** | Pracuje stale 24h; wyłączany tylko na 5 min przy karmieniu |
| **3** | Napowietrzanie | 230V | OFF | Załączane głównie w nocy |
| **4** | **Grzałka** | 230V | OFF | Termostat sprzętowy; odcinana przy awarii czujnika i >28.5°C |
| **5** | **Zawór CO2** | 12V / 230V | OFF | Załączany tylko przy włączonym świetle; domyślnie wyłączony po błędzie |
| **6** | Silnik Karmnika | 12V / 5V | OFF | Impuls 1.5 sekundy przy procedurze karmienia |
| **7** | Rezerwa / Dolewka | 12V | OFF | Wolny kanał do rozbudowy |

---

## 🏗️ Montaż w Obudowie
Moduł montowany jest na specjalnym, podwyższonym mostku [[AquaBox - Piętrowy Mostek Przekaźników|WSPORNIK PRZEKAZNIKOW.SLDPRT]] na wysokości 47 mm ponad zasilaczem 12V.

---

## 🔗 Powiązane Notatki
- [[Hardware - Ekspander MCP23017]]
- [[AquaBox - Piętrowy Mostek Przekaźników]]
- [[Bezpieczeństwo - Zabezpieczenia Grzałki]]
- [[Firmware - WeMos AutomationEngine i Harmonogramy]]
