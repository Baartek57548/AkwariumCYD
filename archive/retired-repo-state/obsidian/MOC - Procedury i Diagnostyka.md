---
aliases:
  - Procedury i Diagnostyka
  - Procedures MOC
tags:
  - moc
  - procedure
  - test
category: moc
status: complete
---

# 🛠️ MOC — Procedury Warsztatowe, Testy i Kalibracja

Instrukcje krok po kroku przygotowane dla użytkownika do pracy ze sterownikiem na stanowisku warsztatowym i w akwarium.

Powrót do: [[00 - AquaCYD Knowledge Hub]] | Bezpieczeństwo: [[MOC - Bezpieczeństwo i Fail-Safe]]

---

## 📑 Dostępne Instrukcje Warsztatowe

1. ⚡ [[Procedura - Programowanie i Wgrywanie Firmware]]
   - Prawidłowa kolejność wgrywania oprogramowania na WeMos D1 Uno i CYD.
   - **Kluczowa zasada bezpieczeństwa: rozwarcie zworek UART na czas flashowania przez USB**.
2. 🔍 [[Procedura - Diagnostyka i Self-Test WeMos]]
   - Uruchomienie kompleksowego programu diagnostycznego `test_wemos_diag`.
   - Interaktywne menu testowe w monitorze portu szeregowego (test przekaźników, czujników, I2C, RTC).
3. 🧪 [[Procedura - Kalibracja Sondy pH]]
   - Kalibracja dwupunktowa (bufory pH 4.01 i pH 7.00 lub 9.21).
   - Obliczenie i zapis offsetu oraz nachylenia (slope) do pamięci EEPROM.
4. 🚀 [[Procedura - Pierwsze Uruchomienie w Akwarium]]
   - Checklista montażowa: zasilacz 12V, podłączenie grzałki, filtra, CO2, oświetlenia i sond.
   - Sprawdzenie połączenia UART i pierwsza synchronizacja czasu.

---

## 🔗 Powiązane Notatki
- [[Hardware - Zworki UART i Izolacja]]
- [[Hardware - Przetwornik ADS1115 i pH]]
- [[Firmware - WeMos Controller Pętla Główna]]
