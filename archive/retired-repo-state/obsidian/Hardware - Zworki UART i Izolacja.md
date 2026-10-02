---
aliases:
  - Zworki UART
  - UART Jumpers
  - Izolacja UART
tags:
  - hardware
  - uart
  - procedure
category: hardware
status: complete
---

# 🔀 Hardware — Zworki Magistrali UART i Procedura Izolacji

Magistrala szeregowa UART łącząca WeMos D1 Uno i ESP32 CYD korzysta z pinów `GPIO1` (TX) oraz `GPIO3` (RX). Ze względów konstrukcyjnych wymaga to zastosowania **zworek izolacyjnych (jumpers)**.

Powrót do: [[MOC - Sprzęt i Peryferia]] | Instrukcja: [[Procedura - Programowanie i Wgrywanie Firmware]]

---

## ❓ Dlaczego Zworki są Bezwzględnie Wymagane?

Zarówno płytka WeMos D1 Uno, jak i ESP32 CYD posiadają na swoich pokładach wbudowane układy konwerterów USB-UART (np. CH340 lub CP2102) podłączone na stałe do tych samych fizycznych pinów: `GPIO1` i `GPIO3`.

- Jeśli zworki byłyby **zwarte** podczas podłączenia kabla USB do komputera:
  - Nastąpiłby **konflikt sprzętowy**: wyjście TX konwertera USB próbowałoby wymusić stan logiczny na linii, którą jednocześnie steruje druga płytka.
  - Skutek: błędy wgrywania firmware (`esptool.FatalError: Failed to connect to ESP`), a w skrajnym wypadku przeciążenie wyjść układów scalonych.

---

## 📋 Schemat Połączenia i Zworek w Obudowie AquaBox

Pomiędzy WeMos (wewnątrz AquaBox) a panelem CYD przebiega **wiązka 4-przewodowa**:
- **GND**: Wspólna masa (połączenie bezpośrednie).
- **5V**: Zasilanie panelu CYD 5V (połączenie bezpośrednie ze stacji).
- **TX (GPIO1 WeMos ➡️ GPIO3 CYD)**: Linia telemetrii — **przechodzi przez zworkę `J_TX`**.
- **RX (GPIO3 WeMos ⬅️ GPIO1 CYD)**: Linia komend — **przechodzi przez zworkę `J_RX`**.

```
  WeMos D1 Uno                     Wiązka 4-żyłowa                    ESP32 CYD
┌──────────────┐                                                   ┌──────────────┐
│          GND ├─────────────────────── GND ───────────────────────┤ GND          │
│           5V ├─────────────────────── 5V ────────────────────────┤ 5V / VIN     │
│ TX (GPIO1)   ├──[ Zworka J_TX ]───── TX ───► RX (GPIO3) ─────────┤ RX (GPIO3)   │
│ RX (GPIO3)   ├──[ Zworka J_RX ]◄──── RX ──── TX (GPIO1) ─────────┤ TX (GPIO1)   │
└──────────────┘                                                   └──────────────┘
```

### Dwa Stany Pracy:
1. **STAN PROGRAMOWANIA (Flashowanie przez USB)**:
   - **Zworki ROZWARTE (zdjęte)**.
   - Linie TX/RX są odcięte — konwertery USB-UART obu płytek nie kolidują.
   - WeMos i CYD można programować niezależnie przez ich porty USB.
2. **STAN NORMALNEJ PRACY (Praca w akwarium)**:
   - **Zworki ZWARTE (założone)**.
   - WeMos i CYD wymieniają telemetrię i komendy po NDJSON, a CYD jest zasilany przez linię 5V.

---

## 🔗 Powiązane Notatki
- [[Procedura - Programowanie i Wgrywanie Firmware]]
- [[Protokół - Specyfikacja NDJSON UART]]
- [[Hardware - WeMos D1 Uno ESP8266]]
- [[Hardware - ESP32 CYD 2432S028R]]
