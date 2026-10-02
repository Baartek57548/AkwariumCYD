# Kontrakt urządzeń AquaCYD

## Własność stanu

CYD jest jedynym źródłem prawdy dla pomiarów, GPIO, harmonogramów, alarmów i
interlocków. Ekran oraz WWW wysyłają intencje, a stan zmienia się dopiero po
walidacji i zapisie po stronie CYD.

## HTTP

Publiczne odpowiedzi nie zawierają haseł ani kluczy. Operacje zapisu wymagają
aktywnej sesji administratora, identyfikatora komendy i walidacji zakresów.
Powtórzona komenda o tym samym identyfikatorze jest idempotentna; konflikt
treści jest odrzucany. Nieznane akcje kończą się błędem bez mutacji.

Podczas odpowiedzi strumieniowanej cyklem życia gniazda zarządza WebServer.
Kod aplikacyjny nie wywołuje ręcznie `flush()` ani `stop()` na aktywnym
kliencie.

## UART CYD–WeMos

Transport jest przewodowy i pracuje stale w osobnym zadaniu. Ramka ma
ograniczony rozmiar, jawny typ i wersję. Parser odrzuca przepełnienie,
niekompletne dane i nieznane wartości. Utrata UART ustawia stan stale/awaryjny,
ale nie blokuje UI ani HTTP.

Drugi pływak poziomu MAX korzysta z `GPB1`, a dotychczasowy MIN z `GPB2`.
Protokół UART dodaje `w_max` i `w_max_valid`, a API statusu dodaje
`sensors.water_level_max_reached` i `sensors.water_level_max_valid` bez zmiany
znaczenia `w_low` i `water_level_high`. Brak nowych pól w starszej ramce UART
oznacza nieznany stan MAX. Rezerwowy bit alarmu zachowuje dotychczasową pozycję.

## Zgodność

Wersja API i wersja protokołu są niezależne od wersji firmware. Dodawane pola są
opcjonalne, nieznane pola są ignorowane, a zmiana semantyki istniejącego pola
wymaga zwiększenia wersji kontraktu.
