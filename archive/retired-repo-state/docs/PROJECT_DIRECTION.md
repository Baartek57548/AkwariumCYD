# Kierunek rozwoju AquaCYD

> Nowy cel sprzętowy: ESP32-S3-WROOM-1-N16R8 z ekranem Waveshare 16249
> (RGB 800×480 + dotyk GT911), przy zachowaniu WeMos jako wykonawcy I/O z
> dwoma pływakami MIN/MAX. Migracja jest w fazie weryfikacji płytki i pamięci;
> produkcyjną bazą kodu pozostaje niżej opisany CYD. Zakres i kryteria opisuje
> [plan migracji](ESP32_S3_16249_MIGRATION.md).

## Priorytet

Najważniejsze są stabilność autonomicznego CYD, ciągła praca UART i spójne UI.
Pełna moc obliczeniowa jest przeznaczana na automatykę oraz aktywny interfejs:
lokalny ekran przy pracy samodzielnej albo HTTP podczas sesji WWW.

## Interfejsy

1. Ekran CYD zapewnia podgląd, alarmy, podstawowe sterowanie i Wi-Fi.
2. Responsywny panel WWW obsługuje telefon, tablet i komputer.
3. WeMos realizuje przewodowe wejścia/wyjścia przez UART.

Nie rozwijamy aplikacji Flutter, Bluetooth, ESP-NOW, P4, C6 ani zewnętrznego
HMI. Konfiguracja odbywa się lokalnie na CYD lub przez uwierzytelnioną sesję
WWW.

## Zasady rozwoju

- brak dynamicznej alokacji w gorącej pętli UI/I/O;
- krótkie sekcje krytyczne i brak operacji sieciowych pod mutexem stanu;
- atomowy zapis konfiguracji z walidacją zakresów i rewizją;
- ryzykowna operacja wymaga potwierdzenia oraz jawnego wyniku;
- brak czujnika zalania — zachowane pola protokołu są rezerwami, aby nie
  przesuwać istniejących ramek;
- funkcja bez sprzętu lub aktywnego interfejsu jest usuwana wraz z testami,
  dokumentacją i CI.

## Następne etapy

1. Długotrwały HIL obejmujący zmianę ustawień, aktywny WWW, UART i utratę Wi-Fi.
2. Pomiary stosu, sterty, opóźnień UART i czasu renderowania obu wariantów LCD.
3. Testy jasnego motywu oraz dolnej nawigacji po zimnym starcie i wznowieniu.
