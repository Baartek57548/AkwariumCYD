---
aliases:
  - Potwierdzenia ACK
  - ACK Frames
tags:
  - protocol
  - uart
category: protocol
status: complete
---

# ✅ Protokół — Potwierdzenia ACK i Obsługa Błędów

Po odebraniu i przetworzeniu każdej ramki typu `"cmd"`, WeMos D1 Uno odsyła do CYD ramkę potwierdzenia typu `"ack"`.

Powrót do: [[MOC - Protokół Komunikacji NDJSON]] | Komendy: [[Protokół - Katalog Komend i Sterowanie]]

---

## 📋 Struktura Ramki ACK

### Sukces wykonania:
```json
{"type":"ack","cmd":"set_relay","ok":true}
```

### Błąd wykonania (z komunikatem tekstowym):
```json
{"type":"ack","cmd":"set_sched","ok":false,"msg":"Invalid device ID"}
```

---

## ⚙️ Obsługa w Firmware CYD
- Jeśli CYD wyśle komendę, a w ciągu 500 ms nie nadejdzie ramka ACK, interfejs ponawia próbę transmisji (do 3 razy).
- Po nadejściu potwierdzenia z flagą `"ok": true`, kontrolka w interfejsie graficznym LVGL potwierdza stan zielonym kolorem.

---

## 🔗 Powiązane Notatki
- [[Protokół - Katalog Komend i Sterowanie]]
- [[Protokół - Specyfikacja NDJSON UART]]
- [[Firmware - CYD UartClient i Telemetria]]
