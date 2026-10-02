# Checklista HIL AquaCYD

## Identyfikacja

- [ ] zapisano wariant LCD, numer płytki CYD, wersję firmware i commit;
- [ ] zapisano wersję WeMos, kartę SD i zasilacz;
- [ ] wynik pochodzi z fizycznego sprzętu, nie z symulatora.

## Kandydat ESP32-S3 N16R8 + Waveshare 16249

- [ ] potwierdzono wyprowadzenia konkretnej płytki i połączenia RGB/GT911;
- [ ] profil `esp32s3_n16r8_bringup` zgłosił `RESULT: PASS` bez ekranu i WeMos;
- [ ] zmierzono zasilanie LCD, logikę 3,3 V i pobór prądu podświetlenia;
- [ ] obraz 800×480 i dotyk GT911 mają prawidłową orientację, bez artefaktów;
- [ ] sprawdzono PSRAM, Wi-Fi, UART do WeMos oraz oba pływaki MIN/MAX;
- [ ] WWW i OTA działają po przeniesieniu zasobów z karty SD do flash;
- [ ] wcześniejszy profil CYD i WeMos przechodzą regresję po zmianie kodu.

## Start i UI

- [ ] zimny start i 20 kolejnych restartów kończą się poprawnym ekranem;
- [ ] jasny motyw ma spójny nagłówek, treść i dolną nawigację;
- [ ] wszystkie dolne przyciski reagują po starcie i po wznowieniu;
- [ ] dotyk ma prawidłową orientację na ILI9341/ST7789;
- [ ] ekran nie miga i nie tworzy narastającej liczby obiektów LVGL.

## Ustawienia i WWW

- [ ] co najmniej 100 zmian ustawień nie powoduje resetu;
- [ ] aktywny klient WWW pokazuje na CYD wyłącznie zakładkę Wi-Fi;
- [ ] powrót pyta o zerwanie klienta i wykonuje wybraną decyzję;
- [ ] telefon ma czytelne cyfry i cele dotykowe bez przewijania poziomego;
- [ ] brak błędów konsoli oraz zerwanych odpowiedzi chunked;
- [ ] rozłączenie Wi-Fi nie zatrzymuje automatyki.

## UART i bezpieczeństwo

- [ ] UART CYD–WeMos pracuje podczas nawigacji, zapisu i obciążenia WWW;
- [ ] utrata i powrót WeMos są wykrywane bez restartu CYD;
- [ ] uszkodzona lub zbyt długa ramka jest odrzucana;
- [ ] interlocki grzałki, CO₂, dozowania i filtra działają;
- [ ] brak dodatkowego czujnika zalania nie generuje alarmu;
- [ ] watchdog nie raportuje `ui_heartbeat_stale` podczas transferu HTTP.

## OTA i soak

- [ ] poprawny podpisany pakiet instaluje się i przechodzi health check;
- [ ] zły podpis, target i downgrade są odrzucane;
- [ ] przerwanie zasilania podczas OTA wraca do poprzedniego obrazu;
- [ ] 72 h pracy nie wykazuje restartów ani trendu utraty sterty.
