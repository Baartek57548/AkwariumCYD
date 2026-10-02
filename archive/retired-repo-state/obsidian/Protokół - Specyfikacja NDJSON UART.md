---
aliases:
  - NDJSON UART
  - Specyfikacja Protokołu
tags:
  - protocol
  - uart
category: protocol
status: complete
---

# 📡 Protokół — Specyfikacja Magistrali Szeregowej NDJSON UART

Współdzielona biblioteka protokołu znajduje się w katalogu:
`firmware/shared/uart_protocol/include/uart_protocol.h` oraz `src/uart_protocol.cpp`.

Powrót do: [[MOC - Protokół Komunikacji NDJSON]] | Komendy: [[Protokół - Katalog Komend i Sterowanie]]

---

## ⚙️ Parametry Transmisji Fizycznej
- **Standard**: Asynchroniczna transmisja szeregowa UART.
- **Prędkość**: **115200 baud**.
- **Format danych**: 8 bitów danych, brak parzystości, 1 bit stopu (**8N1**).
- **Poziomy napięć**: 3.3V TTL (kompatybilne bezpośrednio między WeMos i ESP32).
- **Zabezpieczenie sprzętowe**: [[Hardware - Zworki UART i Izolacja|Zworki UART]] na liniach transmisyjnych.

---

## 📄 Format Wiadomości (NDJSON)
Każda ramka jest pojedynczą linią tekstu w formacie JSON zakończoną znakiem `
` (CRLF lub LF):
- Wszystkie ramki zawierają obowiązkowe pole `"type"`, które określa rodzaj wiadomości:
  - `"telemetry"` ➡️ [[Protokół - Struktura Telemetrii|Cykliczny stan sterownika]]
  - `"cmd"` ➡️ [[Protokół - Katalog Komend i Sterowanie|Polecenie operatorskie]]
  - `"ack"` ➡️ [[Protokół - Potwierdzenia ACK i Błędy|Potwierdzenie wykonania polecenia]]
  - `"time_sync"` ➡️ Synchronizacja czasu Unix Epoch

Maksymalny rozmiar bufora odbiorczego: **512 bajtów**.

---

## 🔗 Powiązane Notatki
- [[Protokół - Struktura Telemetrii]]
- [[Protokół - Katalog Komend i Sterowanie]]
- [[Protokół - Potwierdzenia ACK i Błędy]]
- [[Hardware - Zworki UART i Izolacja]]
