# Firmware CYD 2026.07.26

Gotowe obrazy dla płytki ESP32-2432S028:

- `cydAquarium-CYD-2026.07.26-ILI9341.bin` — standardowy CYD 2,8 cala;
- `cydAquarium-CYD-2026.07.26-ST7789.bin` — wariant sprzętowy z ST7789.

Oba pliki powstały z tego samego commita i przeszły pełną kompilację
PlatformIO. Pliki `.sha256` umożliwiają sprawdzenie integralności po pobraniu.

Przykład wgrywania standardowej wersji od adresu aplikacji:

```powershell
pio pkg exec --package "platformio/tool-esptoolpy@2.41100.0" -- `
  esptool.py --chip esp32 --baud 460800 write_flash 0x10000 `
  cydAquarium-CYD-2026.07.26-ILI9341.bin
```

Bezpieczniejszą metodą pierwszego wgrania całego projektu jest:

```powershell
pio run -e esp32dev -t upload
```

Sam plik `firmware.bin` nie zawiera bootloadera ani tablicy partycji, dlatego
adres `0x10000` jest właściwy tylko wtedy, gdy urządzenie ma już zgodny
bootloader i układ partycji z tego projektu.
