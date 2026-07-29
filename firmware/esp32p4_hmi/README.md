# AquaCYD ESP32-P4 HMI

Natywny panel LVGL 9 dla Waveshare ESP32-P4-WIFI6-Touch-LCD-7B. ESP32-P4
renderuje interfejs 1024×600, a pokładowy ESP32-C6 działa przez ESP-Hosted jako
modem Wi-Fi. Panel nie uruchamia Home Assistant Core ani przeglądarki — jest
klientem MQTT Home Assistanta.

## Budowanie

Projekt wymaga ESP-IDF 5.4.4 i połączenia z internetem podczas pierwszego
pobrania komponentów:

```powershell
cd firmware/esp32p4_hmi
idf.py set-target esp32p4
idf.py menuconfig
idf.py build
idf.py flash monitor
```

Z katalogu głównego można użyć sprawdzonego skryptu:

```powershell
.\tools\build-p4-c6.ps1 -Target p4 -IdfPath C:\esp\v5.4.4-full\esp-idf
```

W menu `AquaCYD ESP32-P4 HMI` należy ustawić Wi-Fi i MQTT identycznie jak w
bramce ESP32-C6. Komponenty są przypięte w `dependencies.lock`: BSP Waveshare
1.0.2, LVGL 9.2.2 i `esp_lvgl_port` 2.6.3. Konfiguracja partycji zachowuje dwa
sloty OTA po 5 MiB na późniejsze wdrożenie podpisanej aktualizacji; bieżące
wydanie wgrywa się lokalnie przez USB.

Panel subskrybuje stan, dostępność i potwierdzenia poleceń. Przy braku MQTT lub
CYD blokuje sterowanie, ale nadal wyświetla ostatni stan. Koreluje aplikacyjny
ACK z `command_id`, pokazuje konflikt rewizji i timeout, a podczas oczekiwania
blokuje kolejne polecenie. Wyświetla stany wyjść, czujniki bezpieczeństwa,
uptime, pamięć i rewizję konfiguracji; jasność zapisuje w NVS. Polecenia ręczne
mają ograniczony czas i nie omijają zabezpieczeń wykonywanych autonomicznie w
CYD.
Po wypięciu ze stacji ściennej panel może działać z akumulatora, o ile pozostaje
w zasięgu domowego Wi-Fi; odłączenie albo rozładowanie panelu nie wpływa na
bramkę, automatykę CYD ani Home Assistanta.

Docelowa mapa ekranów, komponenty, stany błędów i proces Figma → LVGL są
opisane w `../../docs/HMI_LVGL_FIGMA_WORKFLOW.md`. Kolory, odstępy, promienie,
typografia i wymiary bazowe są wersjonowane w
`../../design/hmi/aquacyd-hmi.tokens.json`.
