---
aliases:
  - CYD
  - ESP32-2432S028R
  - Cheap Yellow Display
tags:
  - hardware
  - esp32
  - cyd
category: hardware
status: complete
---

# 📱 Hardware — ESP32 CYD (ESP32-2432S028R)

Płytka **ESP32-2432S028R** (znana potocznie jako *Cheap Yellow Display*) to zintegrowany moduł deweloperski łączący mikrokontroler **ESP32-WROOM-32** z kolorowym wyświetlaczem TFT 2.8" i panelem dotykowym.

Powrót do: [[MOC - Sprzęt i Peryferia]] | Firmware: [[MOC - Firmware ESP32 CYD]]

---

## 📌 Specyfikacja Płytki CYD

- **Procesor**: Dwurdzeniowy Xtensa 32-bit LX6 @ 240 MHz, 520 KB SRAM, 4 MB Flash.
- **Wyświetlacz**: 2.8 cala TFT LCD, rozdzielczość **320 x 240 pikseli**, sterownik **ILI9341** (magistrala SPI).
- **Dotyk**: Oporowy kontroler dotyku **XPT2046** (współdzielona magistrala SPI z osobnym pinem CS).
- **Buzzer piezo**: Wbudowany przetwornik na pinie `GPIO26`. Służy do sygnalizacji alarmów temperatury i krytycznych błędów urządzenia.
- **Slot karty MicroSD**: Dostępny na magistrali SPI (używany opcjonalnie na logi i zasoby webowe).
- **Łączność używana przez AquaCYD**: Wi-Fi 802.11 b/g/n; pozostała część radia nie jest inicjalizowana, a jej rezerwa pamięci jest zwalniana.

---

## 🔌 Połączenie z WeMos w AquaBox (Wiązka 4-przewodowa)

CYD łączy się ze stacją WeMos w AquaBox **jedną wiązką 4-przewodową** (kabel 4-żyłowy, np. złącze JST / goldpin):

| Nr | Linia | Płytka WeMos D1 Uno | Płytka ESP32 CYD | Rola w Systemie |
| :---: | :---: | :---: | :---: | :--- |
| **1** | **GND** | Pin **GND** | Pin **GND** | Wspólna masa zasilania i odniesienia sygnałów UART. |
| **2** | **TX** | Pin **TX (GPIO1)** | Pin **RX (GPIO3)** | Telemetria z WeMos do CYD (przez zworkę `J_TX`). |
| **3** | **RX** | Pin **RX (GPIO3)** | Pin **TX (GPIO1)** | Komendy sterujące z CYD do WeMos (przez zworkę `J_RX`). |
| **4** | **5V** | Pin **5V** (szyna zasilania) | Pin **5V / VIN** | Zasilanie panelu CYD ze stacji AquaBox (brak osobnego zasilacza!). |

> [!TIP]
> **Pojedynczy przewód 4-żyłowy**:
> Dzięki doprowadzeniu linii 5V z WeMos, panel CYD nie wymaga osobnego kabla zasilającego ani zasilacza USB przy akwarium — całość zasilana jest centralnie ze stacji AquaBox.
>
> ⚠️ **Zworki UART**: Na liniach TX i RX zastosowano zworki `J_TX` i `J_RX`, które rozwieramy wyłącznie na czas programowania płytek przez kabel USB (patrz: [[Hardware - Zworki UART i Izolacja]]).

---

## 🔗 Powiązane Notatki
- [[Firmware - CYD UartClient i Telemetria]]
- [[Firmware - CYD Interfejs LVGL i Serwer WWW]]
- [[Hardware - Zworki UART i Izolacja]]
- [[Protokół - Specyfikacja NDJSON UART]]
