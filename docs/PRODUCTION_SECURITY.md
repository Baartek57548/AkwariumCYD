# Model bezpieczeństwa produkcyjnego AquaCYD

## Zakres i granice zaufania

System składa się z firmware ESP32, panelu WWW serwowanego przez sterownik,
aplikacji mobilnej, telefonu użytkownika, domowej sieci oraz opcjonalnej bramy
VPN/MQTT. Sterownik kontroluje urządzenia mogące wpływać na życie zwierząt,
dlatego utrata łączności nie może wyłączyć lokalnej automatyki ani pozostawić
wyjścia w trwałym trybie ręcznym.

Nie są zaufane: pakiety z LAN, dane BLE przed ukończeniem parowania, zawartość
odpowiedzi HTTP, nazwa pliku OTA, zegar telefonu, GitHub Release bez poprawnej
sygnatury i każde wejście przekraczające ustalony limit rozmiaru.

## Najważniejsze zagrożenia i zabezpieczenia

| Zagrożenie | Skutek | Wymagane zabezpieczenie |
| --- | --- | --- |
| Publiczne wystawienie HTTP ESP32 | przejęcie sterowania i wyciek telemetrii | brak przekierowania portów; dostęp zdalny wyłącznie przez VPN lub uwierzytelnioną bramę |
| Przechwycony PIN lub token | nieautoryzowane komendy | krótkotrwałe tokeny, rotacja, ograniczony zakres uprawnień i unieważnianie sesji |
| Powtórzenie tej samej komendy | podwójne karmienie lub przełączenie przekaźnika | unikalny `commandId`, pamięć ostatnich identyfikatorów i idempotentna odpowiedź |
| Zalanie żądaniami | watchdog, brak RAM, opóźnienie automatyki | limity rozmiaru, liczby klientów i token bucket per klient/endpoint |
| Podmieniony firmware | trwałe przejęcie urządzenia | podpis Ed25519, SHA-256, klucz publiczny w firmware, Secure Boot i A/B rollback |
| Utrata zasilania podczas OTA | urządzenie nie uruchamia się | zapis do nieaktywnej partycji, walidacja przed przełączeniem, rollback po braku health check |
| Przejęty telefon | dostęp do sterownika | Android Keystore/iOS Keychain, blokada biometryczna dla działań krytycznych, szybkie wylogowanie |
| Atak fizyczny na ESP32 | odczyt Wi-Fi i konfiguracji | szyfrowanie NVS/Flash Encryption oraz kontrolowany interfejs serwisowy |
| Fałszywe dane czujnika | błędna automatyka | zakresy, `NaN`/timeout, spójność czasowa oraz fail-safe niezależny od UI |
| Zależność lub workflow supply-chain | złośliwy build | lockfile, minimalne uprawnienia Actions, chronione środowiska i przegląd aktualizacji akcji |

## Uwierzytelnianie i sesje

- Parowanie powinno wymagać fizycznej obecności: przycisku na CYD albo
  jednorazowego kodu/QR ważnego maksymalnie 5 minut.
- Lokalny protokół v2 tworzy losowy, nieprzewidywalny token 128-bitowy zapisany
  jako 32 znaki hex. Token żyje 5 minut, porównanie jest stałoczasowe, a
  sterownik utrzymuje najwyżej dwie sesje równocześnie.
- Pięć kolejnych błędnych PIN-ów blokuje logowanie na 60 sekund. Klient może
  ponowić logowanie najwyżej raz po `session_expired`; nie wolno tworzyć pętli
  omijającej blokadę.
- Ewentualny token bramy zdalnej musi być osobny od lokalnej sesji ESP32,
  zawierać zakres i identyfikator urządzenia oraz być rotowany i przechowywany
  wyłącznie w bezpiecznym magazynie systemowym.
- Tokeny dla odczytu telemetrii nie powinny umożliwiać OTA, resetu, zmiany Wi-Fi
  ani sterowania wyjściami.
- PIN administratora służy lokalnie do utworzenia sesji, a nie jako sekret
  przesyłany przy każdej komendzie v2. Nie wolno zapisywać go w logach,
  snapshotach offline ani zwykłych preferencjach.
- Wylogowanie i reset urządzenia muszą unieważniać wszystkie aktywne sesje.

## Limity i walidacja API

Mechanizm sesji ESP32 już ogranicza logowanie do pięciu prób i 60-sekundowej
blokady. Dla bramy zdalnej oraz dalszego hardeningu HTTP wymagane są dodatkowo:

- telemetria: 60 żądań na minutę, burst 5;
- komendy sterujące: 20 na minutę, burst 3 i jedna komenda na wyjście w locie;
- konfiguracja: 10 zapisów na minutę;
- rozpoczęcie OTA: 2 próby na godzinę;
- maksymalnie 4 równoległe połączenia HTTP i jeden transfer OTA;
- JSON do 16 KiB, zwykła odpowiedź do 64 KiB, firmware zgodnie z rozmiarem
  nieaktywnej partycji.

Przekroczenie limitu zwraca `429` z czasem ponowienia. Walidacja odbywa się przed
zmianą stanu. Nazwy wyjść i rodzaje komend muszą pochodzić z zamkniętej listy,
TTL override ma dolną i górną granicę, a wartości liczbowe muszą być skończone.

## Sieć i transport

Nie należy wystawiać portu HTTP sterownika do Internetu. W domu komunikacja może
pozostać w izolowanym VLAN IoT; zdalny dostęp powinien przechodzić przez
WireGuard/Tailscale albo własną bramę TLS z uwierzytelnieniem. Brama nie może
przekazywać dowolnych ścieżek ani omijać limitów urządzenia.

BLE wymaga LE Secure Connections, bondingu i jawnego potwierdzenia nowego
telefonu. Po konfiguracji Wi-Fi tryb reklamowania powinien wygasać. MQTT wymaga
TLS, osobnych danych klienta i ACL ograniczającego urządzenie do jego tematów.

Firmware 5.0.0 nie wymusza jeszcze szyfrowania łącza, bondingu ani ochrony MITM
i reklamuje te ograniczenia w `capabilities`. Krótkotrwały token aplikacyjny
ogranicza czas przejętej sesji, ale nie zastępuje ochrony warstwy BLE. Do czasu
wdrożenia retry po żądaniu parowania i zaliczenia HIL na Androidzie oraz iOS
Bluetooth należy używać tylko przy fizycznie kontrolowanym dostępie do
sterownika; podstawowym transportem produkcyjnym pozostaje odizolowana sieć
lokalna.

## Podpisane OTA i rollback

Produkcyjny obraz powstaje z tagu `firmware-vX.Y.Z`. Workflow publikuje SHA-256,
manifest i sygnatury Ed25519. Prywatny klucz istnieje tylko jako sekret
chronionego środowiska GitHub; repozytorium i artefakty CI go nie zawierają.

Ważne ograniczenie obecnej wersji: podpis jest weryfikowany w pipeline wydania,
ale firmware nie ma jeszcze provisionowanego klucza zaufania i nie weryfikuje
Ed25519 na urządzeniu. Plik SHA-256 umieszczony obok obrazu wykrywa przypadkowe
uszkodzenie i umożliwia operatorowi ręczne porównanie, lecz sam nie chroni przed
atakującym, który podmieni jednocześnie obraz i sumę.

Obecny `ota_guard` sprawdza rozmiar partycji, wolną pamięć, bezpieczny stan
operacji i backup konfiguracji, zapisuje poprzednią partycję, prowadzi okno
`pendingVerify` i wykonuje rollback po nieudanym starcie. Chroni to przed
przerwaniem aktualizacji i wadliwym obrazem, ale nie potwierdza jego autora.
Dlatego OTA wolno wykonywać tylko lokalnie, z zaufanej sieci/AP, po ręcznej
weryfikacji SHA-256 pobranego release. Automatyczne zdalne OTA pozostaje
zablokowane produkcyjnie.

Pełne domknięcie wymaga provisionowania klucza publicznego w kontrolowanym
wydaniu fabrycznym i weryfikacji podpisanego obrazu przez ESP32 Secure Boot v2
lub równoważny mechanizm przed wyborem partycji startowej. Dopiero wtedy
sygnatura z release może być traktowana jako ochrona przed podmianą.

Secure Boot v2 oraz Flash Encryption należy włączać dopiero po przećwiczeniu
procedury odzyskiwania, ponieważ przepalenie eFuse jest nieodwracalne.

## Sekrety, logi i prywatność

- GitHub Environments `production-mobile`, `production-firmware` i
  `aquacyd-hil` powinny wymagać zatwierdzenia oraz ograniczać gałęzie/tagi.
- Klucze JKS i Ed25519 nie mogą trafiać do cache, raportów ani artefaktów.
- Logi zapisują identyfikator komendy i wynik, ale nie token, PIN, SSID, hasło,
  pełny adres MAC ani payload konfiguracji.
- Snapshot offline przechowuje tylko niezbędną telemetrię i ustawienia bez
  sekretów; użytkownik może go usunąć.
- Zegar i identyfikatory zdarzeń muszą pozwalać odróżnić nowe zdarzenie od
  ponownego odczytu po reconnect.

## Reakcja na incydent

Po podejrzeniu wycieku należy zatrzymać publikację, unieważnić tokeny, obrócić
sekrety CI, opublikować informację z zakresem wersji i przygotować podpisaną
aktualizację. Kompromitacja klucza OTA wymaga nowego, zaufanego łańcucha kluczy;
po wdrożeniu zaufania na urządzeniu nie wolno omijać weryfikacji podpisu ani
publikować „tymczasowego” unsigned OTA. Każdy incydent kończy się testem regresji
i aktualizacją tego modelu.
