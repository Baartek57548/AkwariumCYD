# AquaCYD

AquaCYD to autonomiczny sterownik akwarium oparty na ESP32 CYD. Urządzenie
steruje sprzętem, wykonuje harmonogramy i zabezpieczenia lokalnie, a użytkownik
obsługuje je z ekranu 320×240 albo z responsywnego panelu WWW w sieci Wi-Fi.

Projekt nie zawiera aplikacji Flutter, Bluetooth, ESP-NOW, dodatkowej bramki
ani zewnętrznego HMI. Jedynym modułem wykonawczym poza CYD jest WeMos połączony
przewodowo przez UART.

## Architektura

- `firmware/cyd_controller/` — firmware CYD, LVGL, HTTP/OTA i UART;
- `firmware/wemos_controller/` — wejścia/wyjścia komunikujące się przez UART;
- `firmware/shared/uart_protocol/` — ograniczony, wersjonowany protokół UART;
- `web/` — źródła responsywnego panelu WWW;
- `sdcard/` — wygenerowany pakiet statyczny kopiowany na kartę SD;
- `services/remote_gateway/` — opcjonalna brama powiadomień HTTPS;
- `docs/` i `obsidian/` — dokumentacja techniczna oraz dziennik zmian.

CYD pozostaje jedynym źródłem prawdy dla GPIO, blokad, alarmów i fail-safe.
Interfejsy wysyłają intencje i otrzymują autorytatywny wynik. UART pracuje w
osobnym zadaniu czasu rzeczywistego i nie jest zatrzymywany przez ekran ani WWW.

## Budowanie i testy

```powershell
npm ci
npm run build:web-assets
npm run test:api
pio test -e native
pio test -e native_ui
pio run -e cyd_2_8
pio run -e esp32dev-st7789
pio run -e wemos_d1_uno
```

Lokalny podgląd WWW:

```powershell
npm run dev:web
```

## Zasady działania

- aktywny klient WWW przełącza ekran CYD do widoku Wi-Fi;
- powrót z widoku Wi-Fi wymaga potwierdzenia zerwania sesji klienta;
- panel WWW ma czytelną typografię i cele dotykowe na telefonie;
- zmiana ustawień nie może blokować UI ani powodować restartu urządzenia;
- odpowiedź HTTP pozostawia cykl życia gniazda bibliotece WebServer;
- dane dostępowe nie są zwracane w statusie ani zapisywane w repozytorium.

Aktualne dokumenty: [specyfikacja produktu](docs/PRODUCT_SPEC.md),
[architektura repozytorium](docs/MONOREPO_ARCHITECTURE.md),
[kierunek rozwoju](docs/PROJECT_DIRECTION.md),
[bezpieczeństwo runtime](docs/FIRMWARE_RUNTIME_SAFETY.md),
[OTA](docs/OTA.md), [wydania](docs/RELEASE.md) i
[checklista HIL](docs/HIL_CHECKLIST.md).
