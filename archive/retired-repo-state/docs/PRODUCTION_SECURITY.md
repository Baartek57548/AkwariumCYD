# Bezpieczeństwo produkcyjne CYD

## Wejścia

HTTP, UART, SD i OTA są traktowane jako niezaufane. Bufory mają stałe limity,
napisy są zawsze zakończone znakiem NUL, a dane numeryczne są sprawdzane przed
konwersją. Błąd parsowania nie zmienia konfiguracji ani wyjść.

## Współbieżność

Mutex chroni wyłącznie krótki snapshot stanu. Formatowanie JSON, operacje SD i
wysyłanie sieciowe odbywają się poza sekcją krytyczną. Serwer WWW sam zarządza
gniazdem. Watchdog nadzoruje UI, I/O i supervisor; UART nie zależy od postępu
renderowania ani odpowiedzi HTTP.

## Sekrety

Repozytorium nie zawiera haseł Wi-Fi, PIN-ów produkcyjnych ani kluczy
podpisujących. Publiczne API nie przyjmuje konfiguracji sekretów bramy.
Logowanie redaguje dane dostępowe i nie zapisuje pełnych ładunków
uwierzytelniających.

## Powierzchnia radiowa

Aktywny jest wyłącznie Wi-Fi wymagany przez WWW/OTA. Bluetooth i ESP-NOW nie są
kompilowane ani inicjalizowane. Rezerwa BTDM jest zwalniana jednokrotnie podczas
startu.

## Reakcja na awarię

Błąd czujnika, UART, SD, Wi-Fi lub klienta WWW nie wyłącza podstawowej
automatyki. Krytyczne wyjścia przechodzą do stanu bezpiecznego zgodnie z
interlockami. Reset jest rejestrowany wraz z przyczyną, a powtarzalna awaria
musi mieć test regresyjny.
