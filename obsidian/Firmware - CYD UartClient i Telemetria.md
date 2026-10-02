---
aliases:
  - CYD UartClient
  - Klient UART CYD
tags:
  - firmware
  - cyd
  - uart
  - esp32
category: firmware
status: complete
---

# 📡 Firmware — Klient UART i Obsługa Telemetrii (`uart_client.cpp`)

Pliki: `firmware/cyd_controller/include/uart_client.h` oraz `src/uart_client.cpp`.
Klasa `UartClient` na ESP32 CYD odpowiada za nasłuchiwanie magistrali szeregowej, dekodowanie ramek NDJSON oraz detekcję utraty łączności ze sterownikiem WeMos.

Powrót do: [[MOC - Firmware ESP32 CYD]] | Protokół: [[Protokół - Specyfikacja NDJSON UART]]

---

## ⏱️ Detekcja Utraty Połączenia (Heartbeat Timeout)

```cpp
bool UartClient::isControllerOnline() const {
    return (millis() - last_rx_ms_ < AquaProtocol::OFFLINE_TIMEOUT_MS);
}
```
- Stała `OFFLINE_TIMEOUT_MS = 3000UL` (3 sekundy).
- Jeśli przez 3 kolejne sekundy nie dotrze żadna poprawna ramka telemetrii:
  1. Flaga `online_` przechodzi w stan `false`.
  2. Interfejs graficzny wyświetla żółto-czerwony baner ostrzegawczy: `STEROWNIK OFFLINE`.
  3. Uruchamiany jest przerywany sygnał dźwiękowy na wbudowanym buzzerze (`GPIO26`).

---

## 📤 Metody Wysyłania Komend
Klasa dostarcza metody wysokiego poziomu:
- `sendSetRelay(channel, state)` — przełączenie przekaźnika.
- `sendSetTemp(target, hyst)` — zmiana nastaw termostatu.
- `sendFeedTrigger()` — uruchomienie cyklu karmienia.
- `sendSyncTime(epoch)` — przesłanie czasu z NTP do WeMos.

Każda komenda serializowana jest do formatu NDJSON i natychmiast wysyłana na port szeregowy `Serial`.

---

## 🔗 Powiązane Notatki
- [[Protokół - Specyfikacja NDJSON UART]]
- [[Protokół - Katalog Komend i Sterowanie]]
- [[Firmware - CYD Interfejs LVGL i Serwer WWW]]
