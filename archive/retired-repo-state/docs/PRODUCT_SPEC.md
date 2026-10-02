# Specyfikacja produktu AquaCYD

## Zakres

AquaCYD składa się z autonomicznego sterownika CYD, przewodowego modułu WeMos
oraz panelu WWW zapisanego na karcie SD. Nie ma aplikacji Flutter, Bluetooth,
ESP-NOW, P4, C6, zewnętrznego HMI ani czujnika zalania.

## CYD Controller

Sterownik działa bez Internetu i telefonu. Jest właścicielem stanu akwarium,
harmonogramów, interlocków i reakcji fail-safe. Awaria interfejsu użytkownika
nie może zatrzymać UART ani automatyki.

Lokalny ekran 320×240 służy do podglądu, prostego sterowania, ustawień Wi-Fi,
alarmów i diagnostyki. W jasnym i ciemnym motywie nagłówek, treść i dolna
nawigacja używają spójnej palety oraz pozostają aktywne dotykowo.

## Panel WWW

Panel WWW jest jedynym klientem na telefon i komputer. Korzysta z HTTP w sieci
lokalnej, ma responsywną typografię i duże cele dotykowe. Podczas aktywnej sesji
WWW ekran CYD pokazuje zakładkę Wi-Fi. Próba wyjścia wyświetla pytanie o
zerwanie połączenia z klientem.

Odpowiedzi HTTP nie mogą blokować zadania UI. Biblioteka WebServer jest
właścicielem cyklu życia gniazda; kod aplikacyjny nie zamyka klienta w trakcie
odpowiedzi strumieniowanej.

## Kryteria akceptacji

- zmiana danych z ekranu lub WWW nie restartuje urządzenia;
- UART działa stale w osobnym zadaniu, z limitem czasu i walidacją ramek;
- nieznane dane wejściowe są odrzucane bez zmiany stanu;
- panel WWW jest czytelny i obsługiwalny dotykiem na telefonie;
- nieużywane radio Bluetooth nie jest inicjalizowane, a jego rezerwa pamięci
  jest zwalniana podczas startu;
- testy hostowe, API i kompilacje CYD/WeMos przechodzą przed wgraniem.
