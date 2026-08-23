# AquaCYD ESP32-P4 HMI

Natywne centrum AquaHub dla Waveshare ESP32-P4-WIFI6-Touch-LCD-7B. ESP32-P4
renderuje interfejs LVGL 9 w 1024×600, prowadzi uniwersalny rejestr urządzeń,
krótką historię, lokalny broker MQTTS i HTTPS API. Pokładowy ESP32-C6 działa
przez ESP-Hosted jako modem Wi-Fi. Panel nie uruchamia Home Assistant Core ani
przeglądarki.

## Budowanie

Projekt wymaga ESP-IDF 5.4.4 i połączenia z internetem podczas pierwszego
pobrania komponentów:

```powershell
cd firmware/esp32p4_hub
idf.py set-target esp32p4
idf.py menuconfig
idf.py build
idf.py flash monitor
```

Z katalogu głównego można użyć sprawdzonego skryptu:

```powershell
.\tools\build-p4-c6.ps1 -Target p4 -IdfPath C:\esp\v5.4.4-full\esp-idf
```

W menu `AquaHub ESP32-P4` należy ustawić Wi-Fi oraz osobne konto lokalnego
brokera: nazwa użytkownika ma co najmniej 4 znaki, a hasło co najmniej 12.
Te same dane otrzymuje stały C6. Komponenty są przypięte w
`dependencies.lock`: BSP Waveshare 1.0.2, LVGL 9.2.2 i `esp_lvgl_port` 2.6.3.
Konfiguracja partycji zachowuje dwa sloty OTA po 5 MiB; dopóki podpisany
łańcuch OTA nie jest gotowy, obraz wgrywa się lokalnie przez USB.

Przy pierwszym uruchomieniu P4 tworzy klucz P-256 i samopodpisany certyfikat.
Ekran System pokazuje pełny odcisk SHA-256 i sześciocyfrowy kod parowania.
Aplikacja Flutter zapisuje token w secure storage i przypina fingerprint.
Stały C6 otrzymuje certyfikat przez kontrolowany proces opisany w
`../../docs/AQUAHUB_ARCHITECTURE.md`.

Cross-origin API jest domyślnie wyłączone. Dla wersji Flutter web należy ustawić
w `AQUAHUB_CORS_ORIGIN` jeden dokładny origin HTTPS; wildcard nie jest
akceptowany. Android i iOS nie wymagają CORS.

Jeżeli dane Wi-Fi lub MQTT są jeszcze puste, panel uruchamia kompletny interfejs
w bezpiecznym trybie offline zamiast wpadać w pętlę restartów. Sterowanie i
edycja konfiguracji pozostają wtedy zablokowane, a ekran System pokazuje brak
łączności.

Lokalny broker przekazuje Discovery, stan, dostępność i potwierdzenia do
rejestru AquaHub. Przy braku CYD panel blokuje sterowanie, ale nadal wyświetla
ostatni stan. Rejestr odrzuca replay na podstawie `boot_id` i `sequence`, a API
odmawia komend dla encji tylko do odczytu oraz encji krytycznych.

Interfejs zawiera sześć kompletnych ekranów: dashboard, sterowanie, czujniki,
alarmy, automatykę i diagnostykę. Obsługuje animowany start, przejścia stron,
puls alarmu, komunikaty toast, animację oczekiwania na ACK, ostrzeżenie o
nieaktualnych danych oraz modalne potwierdzenie wyłączenia filtra i karmienia.
Kliknięcie stanu łączności otwiera diagnostykę, a kliknięcie statusu alarmu
przechodzi bezpośrednio do listy przyczyn i zaleceń.

Panel wyświetla stany wyjść, czujniki bezpieczeństwa, uptime, pamięć i rewizję
konfiguracji; jasność zapisuje w NVS. Ekran Automatyka odczytuje i edytuje
harmonogram obu lamp, filtra i napowietrzania, profile lamp Aquael oraz tryb,
nastawę i histerezę grzałki. Każdy formularz wysyła pełną transakcję z rewizją,
a CYD waliduje zakresy, zapisuje ją atomowo i zwraca ACK. Polecenia ręczne mają
ograniczony czas i nie omijają zabezpieczeń wykonywanych autonomicznie w CYD.
Po wypięciu ze stacji ściennej panel może działać z akumulatora, o ile pozostaje
w zasięgu domowego Wi-Fi; odłączenie albo rozładowanie panelu nie wpływa na
bramkę, automatykę CYD ani Home Assistanta.

Mapa ekranów, komponenty, stany błędów i proces Figma → LVGL są
opisane w `../../docs/HMI_LVGL_FIGMA_WORKFLOW.md`. Kolory, odstępy, promienie,
typografia i wymiary bazowe są wersjonowane w
`../../design/hmi/aquacyd-hmi.tokens.json`.
Komplet ramek do importu, manifest prototypu i czasy animacji znajdują się w
`../../design/hmi/figma-import`, `../../design/hmi/figma-manifest.json` oraz
`../../design/hmi/motion-spec.json`.
