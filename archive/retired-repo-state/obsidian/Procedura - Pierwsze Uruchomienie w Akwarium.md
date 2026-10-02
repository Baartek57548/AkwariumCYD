---
aliases:
  - Pierwsze Uruchomienie
  - Commissioning
tags:
  - procedure
  - commissioning
category: procedure
status: complete
---

# 🚀 Procedura — Pierwsze Uruchomienie Sterownika w Akwarium

Checklista wdrożeniowa przed ostatecznym podłączeniem zasilania 230V i urządzeń wykonawczych do stacji AquaBox.

Powrót do: [[MOC - Procedury i Diagnostyka]] | Bezpieczeństwo: [[MOC - Bezpieczeństwo i Fail-Safe]]

---

## ✅ Checklista Przed Włączeniem Zasilania

- [ ] **Wiązka 4-przewodowa CYD ↔ WeMos**: Prawidłowo wpięta w złącza (GND, TX, RX, 5V).
- [ ] **Zworki UART**: Założone na pozycjach `J_TX` i `J_RX`.
- [ ] **Bezpiecznik w gnieździe C14**: Sprawny bezpiecznik rurkowy 5x20 mm (5A lub 6.3A).
- [ ] **Dławiki kablowe**: Dokręcone wokół przewodów wychodzących z obudowy.
- [ ] **Masa gwiaździsta**: Wspólne połączenie GND zasilacza 12V, przetwornicy 5V, WeMos i CYD.
- [ ] **Termometr DS18B20**: Rezystor podciągający 4.7kΩ obecny; sonda umieszczona w strefie dobrego przepływu wody.
- [ ] **Pływak poziomu wody**: Zamocowany stabilnie i sprawdzony przy odłączonych odbiornikach.

---

## ⚡ Sekwencja Startowa

1. Włącz główny włącznik kołyskowy na obudowie AquaBox.
2. Zaświeci się dioda zasilania 12V oraz dioda przetwornicy 5V.
3. Po ok. 2 sekundach dioda `D4` na WeMos zacznie regularnie pulsować co 1 sekundę.
4. Wyświetlacz CYD zainicjalizuje bibliotekę LVGL i wyświetli ekran główny.
5. Po połączeniu CYD z domowym Wi-Fi nastąpi automatyczna synchronizacja czasu NTP.
6. Filtr główny załączy się automatycznie (przekaźnik 2).

---

## 🔗 Powiązane Notatki
- [[Procedura - Programowanie i Wgrywanie Firmware]]
- [[Hardware - Czujnik Poziomu Wody]]
- [[AquaBox - Założenia Konstrukcyjne CAD]]
