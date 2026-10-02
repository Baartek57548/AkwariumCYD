# Bezpieczeństwo AquaCYD

## Granice zaufania

Zaufana jest lokalna logika CYD po przejściu walidacji. Niezaufane są żądania z
LAN, pliki z karty SD, pakiety OTA i dane UART przed sprawdzeniem formatu.
Każde wejście ma limit rozmiaru, walidację typu i zakresu oraz bezpieczny błąd.

## Sieć i sesje

Panel WWW działa w sieci lokalnej przez Wi-Fi. Operacje zapisu wymagają PIN-u,
krótkotrwałej sesji administratora, ochrony przed brute force i idempotentnego
`commandId`. Status nie zwraca sekretów. CYD nie powinien być przekierowywany
bezpośrednio do Internetu.

Firmware nie zawiera stosu ani usługi Bluetooth. Podczas startu zwalnia
nieużywaną rezerwę pamięci kontrolera BTDM, aby zwiększyć zapas sterty.

## OTA i łańcuch dostaw

Wydanie produkcyjne zawiera podpisany obraz dla ILI9341 i ST7789, pakiet
`.aqfw`, SHA-256, manifest oraz SBOM. Wersja bezpieczeństwa jest monotoniczna,
a obraz nie jest zatwierdzany przed health checkiem. Klucz prywatny istnieje
wyłącznie w chronionym środowisku wydawniczym.

CI skanuje historię sekretów, zależności, kompiluje oba warianty i uruchamia
testy hostowe/API. Brak fizycznego HIL jest raportowany jako brak dowodu, nie
jako sukces.
