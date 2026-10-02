---
aliases:
  - Diagnostyka WeMos
  - Self-Test
tags:
  - procedure
  - test
category: procedure
status: complete
---

# 🔍 Procedura — Diagnostyka i Self-Test WeMos (`test_wemos_diag`)

Przed zamontowaniem sterownika w akwarium warto zweryfikować wszystkie podzespoły za pomocą dedykowanego interaktywnego programu diagnostycznego.

Powrót do: [[MOC - Procedury i Diagnostyka]] | Firmware: [[MOC - Firmware ESP8266 WeMos]]

---

## 🛠️ Wgranie Programu Diagnostycznego

1. Upewnij się, że zworki UART są **rozwarte**.
2. Podłącz WeMos kablem USB do komputera.
3. Wgraj program diagnostyczny poleceniem:
   ```powershell
   pio run -e test_wemos_diag -t upload
   ```
4. Otwórz monitor portu szeregowego z prędkością 115200 baud:
   ```powershell
   pio device monitor -b 115200
   ```

---

## ⌨️ Interaktywne Menu Konsolowe

Po uruchomieniu monitora wciśnij odpowiedni klawisz numeryczny na klawiaturze:

| Klawisz | Testowana Funkcja | Oczekiwany Wynik w Terminalu |
| :---: | :--- | :--- |
| **`[1]`** | Skaner magistrali I2C | Wykrycie urządzeń: `0x20` (MCP), `0x48` (ADS), `0x68` (RTC). |
| **`[2]`** | Cykliczny test przekaźników | Sekwencyjne kliknięcie przekaźnikami 1 do 8 (po 1 s każdy). |
| **`[3]`** | Podgląd na żywo wejść cyfrowych | Porusz pływakiem albo zewrzyj wejście rezerwowe — stan zmieni się z 1 na 0. |
| **`[4]`** | Odczyt termometru DS18B20 | Wyświetlenie aktualnej temperatury (np. `23.8 °C`). |
| **`[5]`** | Odczyt i test zegara DS3231 | Wypisanie aktualnej daty i godziny oraz temperatury wewnętrznej RTC. |
| **`[6]`** | Pomiary przetwornika ADS1115 | Wypisanie napięć w mV na 4 kanałach oraz wyliczonego pH. |
| **`[7]`** | **AUTOMATYCZNY SELF-TEST** | Sprawdza wszystkie moduły naraz i generuje raport `[PASS]` / `[FAIL]`. |

---

## 🔗 Powiązane Notatki
- [[Procedura - Programowanie i Wgrywanie Firmware]]
- [[Hardware - WeMos D1 Uno ESP8266]]
- [[MOC - Sprzęt i Peryferia]]
