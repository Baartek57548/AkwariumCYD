# AquaCYD ESP32-P4 HMI

Natywny panel LVGL 9 dla Waveshare ESP32-P4-WIFI6-Touch-LCD-7B. ESP32-P4
renderuje interfejs 1024×600, a pokładowy ESP32-C6 działa przez ESP-Hosted jako
modem Wi-Fi. Panel nie uruchamia Home Assistant Core ani przeglądarki — jest
klientem MQTT Home Assistanta.

## Budowanie

Projekt wymaga ESP-IDF 5.4.x i połączenia z internetem podczas pierwszego
pobrania komponentów:

```powershell
cd firmware/esp32p4_hmi
idf.py set-target esp32p4
idf.py menuconfig
idf.py build
idf.py flash monitor
```

W menu `AquaCYD ESP32-P4 HMI` należy ustawić Wi-Fi i MQTT identycznie jak w
bramce ESP32-C6. Konfiguracja partycji zachowuje dwa sloty OTA po 5 MiB.

Panel subskrybuje stan, dostępność i potwierdzenia poleceń. Przy braku MQTT lub
CYD blokuje sterowanie, ale nadal wyświetla ostatni stan. Polecenia ręczne mają
ograniczony czas i nie omijają zabezpieczeń wykonywanych autonomicznie w CYD.
Po wypięciu ze stacji ściennej panel może działać z akumulatora, o ile pozostaje
w zasięgu domowego Wi-Fi; odłączenie albo rozładowanie panelu nie wpływa na
bramkę, automatykę CYD ani Home Assistanta.
