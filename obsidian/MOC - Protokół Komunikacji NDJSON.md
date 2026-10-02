---
aliases:
  - Protokół Komunikacji
  - UART NDJSON MOC
tags:
  - moc
  - protocol
  - uart
category: moc
status: complete
---

# 📡 MOC — Protokół Komunikacji UART NDJSON

Magistrala komunikacyjna pomiędzy **ESP32 CYD** a **WeMos D1 Uno (ESP8266)** wykorzystuje format **NDJSON (New-line Delimited JSON)** przesyłany z prędkością **115200 baud**.

Powrót do: [[00 - AquaCYD Knowledge Hub]] | Kod źródłowy: `firmware/shared/uart_protocol/`

---

## 💡 Główne Założenia Protokołu

1. **Format Czystego Tekstu**: Każda ramka jest poprawnym obiektem JSON zakończonym znakiem nowej linii `
`. Pozwala to na łatwy podgląd i debugowanie na żywo w monitorze portu szeregowego.
2. **Dwukierunkowość**:
   - **WeMos -> CYD**: Cykliczna [[Protokół - Struktura Telemetrii|ramka telemetrii]] co 1000 ms oraz natychmiast po wykonaniu komendy.
   - **CYD -> WeMos**: [[Protokół - Katalog Komend i Sterowanie|Ramki komend operatorskich]] (przełączanie przekaźników, zmiana temp, synchronizacja NTP, karmienie).
   - **WeMos -> CYD**: [[Protokół - Potwierdzenia ACK i Błędy|Ramki potwierdzenia ACK]] po przetworzeniu komendy.
3. **Mechanizm Failsafe**: Jeśli CYD nie odbierze telemetrii przez ponad **3000 ms**, sygnalizuje błąd połączenia na ekranie oraz uruchamia sygnał ostrzegawczy na buzzerze.

---

## 📄 Podsumowanie Ramek

```mermaid
sequenceDiagram
    participant WeMos as WeMos D1 (AquaBox)
    participant CYD as ESP32 CYD (Frontend)

    Note over WeMos,CYD: Ciągłe strumieniowanie co 1s
    WeMos->>CYD: {"type":"telemetry","temp":25.4,"ph":6.85,"relays":20,...}
    
    Note over CYD,WeMos: Akcja użytkownika (np. kliknięcie grzałki)
    CYD->>WeMos: {"type":"cmd","cmd":"set_relay","ch":4,"val":1}
    WeMos->>CYD: {"type":"ack","cmd":"set_relay","ok":true}
    WeMos->>CYD: {"type":"telemetry","temp":25.4,"relays":24,...} (Natychmiastowy update)

    Note over CYD,WeMos: Synchronizacja czasu z NTP
    CYD->>WeMos: {"type":"cmd","cmd":"sync_time","epoch":1773850000}
    WeMos->>CYD: {"type":"ack","cmd":"sync_time","ok":true}
```

---

## 🔗 Powiązane Notatki
- [[Protokół - Specyfikacja NDJSON UART]]
- [[Protokół - Struktura Telemetrii]]
- [[Protokół - Katalog Komend i Sterowanie]]
- [[Protokół - Potwierdzenia ACK i Błędy]]
- [[Hardware - Zworki UART i Izolacja]]
