---
aliases:
  - WeMos hardware_i2c
  - Obsługa I2C WeMos
tags:
  - firmware
  - i2c
  - esp8266
category: firmware
status: complete
---

# 🔌 Firmware — Obsługa Magistrali I2C (`hardware_i2c.cpp`)

Pliki: `firmware/wemos_controller/src/hardware_i2c.h` oraz `.cpp`.
Klasa `HardwareI2C` zarządza wszystkimi urządzeniami podłączonymi do szyny I2C (piny `D1` SCL, `D2` SDA): ekspanderem MCP23017, przetwornikiem ADS1115 oraz zegarem DS3231.

Powrót do: [[MOC - Firmware ESP8266 WeMos]] | Sprzęt: [[MOC - Sprzęt i Peryferia]]

---

## 🛠️ Implementacja Kluczowych Metod

### 1. Sterowanie Przekaźnikami przez MCP23017:
```cpp
bool HardwareI2C::setRelay(uint8_t channel, bool on)
{
    if (channel >= 8) return false;
    if (on) relay_state_mask_ |= (1U << channel);
    else    relay_state_mask_ &= ~(1U << channel);

    // Active-Low: logiczne 1 w kodzie = 0 na wyjściu MCP
    physical_shadow_a_ = ~relay_state_mask_;
    return writeMcpRegister(REG_OLATA, physical_shadow_a_);
}
```
Zastosowanie zmiennej cieniowej `physical_shadow_a_` pozwala na natychmiastowe operacje bitowe bez konieczności każdorazowego odczytu rejestru z układu MCP23017.

### 2. Obsługa DS3231 RTC i Konwersja Czasu:
- Zegar zwraca wartości w formacie BCD. Funkcja wewnętrzna `bcd2bin()` przelicza je na liczby dziesiętne.
- Funkcja `to_epoch(y, m, d, hh, mm, ss)` dokonuje niezależnej kalkulacji sekund Unix Epoch uwzględniając lata przestępne i zmienną liczbę dni w miesiącach.

### 3. Odczyt Sonda pH z ADS1115:
- Konfiguracja rejestru `ADS_REG_CONFIG` na pomiar single-ended na kanale AIN0 przy zakresie ±4.096V.
- Przeliczenie surowego odczytu 16-bitowego na miliwolty i podstawienie do wzoru kalibracyjnego.

---

## 🔗 Powiązane Notatki
- [[Hardware - Ekspander MCP23017]]
- [[Hardware - Zegar DS3231 RTC]]
- [[Hardware - Przetwornik ADS1115 i pH]]
