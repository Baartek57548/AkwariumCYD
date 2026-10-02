---
aliases:
  - MCP23017
  - I2C Expander
tags:
  - hardware
  - i2c
category: hardware
status: complete
---

# 🔌 Hardware — Ekspander Portów MCP23017 (I2C)

Układ **MCP23017** to 16-bitowy ekspander wejść/wyjść sterowany magistralą I2C. W projekcie AquaBox odpowiada za separację i sterowanie wszystkimi przekaźnikami oraz odbiór sygnałów z czujników binarnych.

Powrót do: [[MOC - Sprzęt i Peryferia]] | Kod: [[Firmware - WeMos Hardware I2C]]

---

## 📌 Konfiguracja Sprzętowa

- **Adres I2C**: `0x20` (piny sprzętowe `A0`, `A1`, `A2` połączone bezpośrednio do masy `GND`).
- **Napięcie zasilania**: 5.0V (dla pełnej zgodności z poziomami logicznymi modułu przekaźników).
- **Podłączenie magistrali**:
  - `SCL` ➡️ WeMos Pin `D1` (`GPIO5`)
  - `SDA` ➡️ WeMos Pin `D2` (`GPIO4`)

---

## 🗺️ Podział Portów (Port A i Port B)

### Port A (GPA0 – GPA7): 8 Wyjść dla [[Hardware - Moduł 8 Przekaźników|Modułu Przekaźników]]
Wszystkie piny skonfigurowane jako wyjścia (`IODIRA = 0x00`). Sterowanie w logice **Active-Low** (stan niski `0` na wyjściu załącza przekaźnik):
- `GPA0` (Bit 0): **Światło A** (Light Front)
- `GPA1` (Bit 1): **Światło B** (Light Plant/Rear)
- `GPA2` (Bit 2): **Filtr Główny** (Main Filter)
- `GPA3` (Bit 3): **Napowietrzacz** (Aerator)
- `GPA4` (Bit 4): **Grzałka** (Heater)
- `GPA5` (Bit 5): **Elektrozawór CO2**
- `GPA6` (Bit 6): **Karmnik Automatyczny** (Feeder trigger)
- `GPA7` (Bit 7): **Kanał Zapasowy** (Spare / Dolewka ATO)

### Port B (GPB0 – GPB7): Wejścia Cyfrowe z Pull-upem
Wszystkie piny skonfigurowane jako wejścia (`IODIRB = 0xFF`) z włączonymi wewnętrznymi rezystorami podciągającymi 100kΩ (`GPPUB = 0xFF`):
- `GPB0` (Bit 0): Krańcówka pozycji karmnika (`INPUT_CH_FEEDER_POS`)
- `GPB1` (Bit 1): **Pływak poziomu wody MAX** (`INPUT_CH_WATER_LEVEL_MAX`)
- `GPB2` (Bit 2): **Pływak poziomu wody MIN** (`INPUT_CH_WATER_LEVEL_MIN`)
- `GPB3` (Bit 3): Czujnik przepływu / rezerwa (`INPUT_CH_FLOW`)

---

## 🔗 Powiązane Notatki
- [[Hardware - Moduł 8 Przekaźników]]
- [[Hardware - Czujnik Poziomu Wody]]
- [[Firmware - WeMos Hardware I2C]]
