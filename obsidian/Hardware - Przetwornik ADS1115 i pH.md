---
aliases:
  - ADS1115
  - Sonda pH
  - ADC
tags:
  - hardware
  - i2c
  - sensors
category: hardware
status: complete
---

# 🧪 Hardware — Przetwornik ADS1115 i Pomiar pH / EC

Układ **ADS1115** to precyzyjny, 16-bitowy przetwornik analogowo-cyfrowy (ADC) z wbudowanym programowalnym wzmacniaczem (PGA) i interfejsem I2C.

Powrót do: [[MOC - Sprzęt i Peryferia]] | Procedura: [[Procedura - Kalibracja Sondy pH]]

---

## 📌 Konfiguracja Układu ADS1115

- **Adres I2C**: **`0x48`** (pin `ADDR` połączony do masy `GND`).
- **Napięcie referencyjne**: Full Scale Range (FSR) ustawione w rejestrze konfiguracyjnym na **±4.096V**.
- **Rozdzielczość**: 16 bitów ze znakiem (32768 kroków dla zakresu dodatniego).
- **Rozdzielczość napięciowa**: 4096 mV / 32768 = **0.125 mV na LSB**! Zapewnia to ogromną czułość dla odczytu sond elektrochemicznych.

---

## 🗺️ Wykorzystanie Kanałów ADC

1. **Kanał AIN0**: **Sonda pH (przez moduł dopasowujący z BNC)**
   - Wzmacniacz sondy pH wystawia sygnał napięciowy (zazwyczaj 2.5V odpowiada neutralnemu pH 7.0).
   - Wartość pH wyliczana jest wzorem:
     $$	ext{pH} = 7.0 + (2.5 - V_{	ext{sonda}}) \cdot 	ext{slope} + 	ext{offset}$$
   - Parametry `slope` i `offset` zapisywane są trwale w pamięci [[Firmware - WeMos Pamięć EEPROM i Storage|EEPROM]].
2. **Kanał AIN1**: **Sonda Przewodności EC** (opcjonalny pomiar zasolenia/czystości wody).
3. **Kanały AIN2, AIN3**: Rezerwowe wejścia analogowe.

---

## 🔗 Powiązane Notatki
- [[Procedura - Kalibracja Sondy pH]]
- [[Firmware - WeMos Hardware I2C]]
- [[Firmware - WeMos Pamięć EEPROM i Storage]]
