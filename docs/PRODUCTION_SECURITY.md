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
| Podmieniony firmware | trwałe przejęcie urządzenia | podpis RSA-3072/PSS, SHA-256, klucz publiczny w firmware, Secure Boot v2 i A/B rollback |
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
- Każde urządzenie produkcyjne generuje własny sześciocyfrowy PIN i hasło AP
  z generatora sprzętowego ESP32. PIN służy wyłącznie do utworzenia sesji, a nie
  jest przesyłany przy każdej komendzie. W NVS pozostaje jego SHA-256; jawna
  kopia potrzebna do pierwszego uruchomienia jest kasowana po poprawnym
  logowaniu. Nie wolno zapisywać PIN-u w logach ani snapshotach offline.
- Wylogowanie i reset urządzenia muszą unieważniać wszystkie aktywne sesje.

Panel WWW używa `X-AquaCYD-Session` także dla starszych ścieżek akcji,
diagnostyki, logów i ustawienia czasu. Parametr `pin` w URL/formularzu nie jest
już akceptowany przez firmware produkcyjny. Wylogowanie wywołuje
`POST /api/v2/logout`, który unieważnia token po stronie sterownika.

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

Firmware 5.1.0 wymusza LE Secure Connections, bonding, ochronę MITM i klucz
128-bit. Chronione charakterystyki nie przyjmują poleceń przed zakończeniem
uwierzytelnionego połączenia, a nieudane parowanie kończy się rozłączeniem.
Aplikacja Android inicjuje bonding natywnie i wykonuje chroniony handshake przed
subskrypcją oraz komendami. Przed szerokim wdrożeniem nadal należy zaliczyć HIL
na docelowych wersjach Androida i iOS, ponieważ zachowanie okna systemowego
parowania zależy od systemu i producenta telefonu.

## Podpisane OTA i rollback

Produkcyjny obraz powstaje z tagu `firmware-vX.Y.Z`, który musi odpowiadać
`FirmwareInfo::VERSION`. Workflow podpisuje finalny payload przez `espsecure`
Secure Boot v2 i opakowuje go w `.aqfw`. Nagłówek pakietu ma podpis
RSA-3072/PSS/SHA-256 i kryptograficznie wiąże target ekranu, wersję firmware,
`securityVersion`, minimalną wersję bootloadera, identyfikator klucza, długość
oraz SHA-256 całego payloadu. Prywatny klucz istnieje tylko jako sekret
chronionego środowiska GitHub; repozytorium i artefakty go nie zawierają.

Firmware ma wbudowany publiczny trust anchor i przed rozpoczęciem zapisu
weryfikuje podpis nagłówka `.aqfw`, target, politykę wersji oraz rozmiar
partycji. Podczas zapisu oblicza SHA-256, odrzuca obcięte i nadmiarowe dane,
kontroluje obecność bloku podpisu Secure Boot v2 i dopiero po pełnej walidacji
kończy aktualizację. Plik `SHA256SUMS` w release pozostaje dodatkowym narzędziem
audytowym operatora, ale nie jest źródłem zaufania urządzenia.

`ota_guard` sprawdza wolną pamięć, bezpieczny stan operacji i backup
konfiguracji, zapisuje poprzednią partycję, prowadzi okno `pendingVerify`,
realizuje rollback po nieudanym starcie i utrzymuje monotoniczny programowy
próg `securityVersion`. Niezgodny, starszy lub niepodpisany pakiet jest
odrzucany. Produkcyjny endpoint HTTP przyjmuje wyłącznie `.aqfw`; surowe
ArduinoOTA jest domyślnie wyłączone na etapie kompilacji.

To zabezpiecza ścieżkę aktualizacji na poziomie aplikacji. Egzekwowanie podpisu
przez ROM przed uruchomieniem kodu oraz ochrona pamięci Flash wymagają osobnego,
fabrycznego provisioningu Secure Boot v2 i Flash Encryption na każdej płytce.
Pipeline wydania nigdy nie zapisuje eFuse. Automatyczna aktualizacja może być
oferowana dopiero po jawnej zgodzie użytkownika i przez uwierzytelniony lokalny
transport albo kontrolowaną bramę TLS; nie wolno pobierać obrazu z dowolnego URL.

Secure Boot v2 i Flash Encryption należy włączać dopiero po sprawdzeniu rewizji
ESP32, utworzeniu obrazu odzyskiwania oraz przećwiczeniu HIL i rollbacku.
Przepalenie eFuse jest nieodwracalne. Szczegółowy kontrakt pakietu, fingerprint
klucza, odczytowy audyt i procedurę pilotażową opisuje
[FIRMWARE_SIGNING_AND_PROVISIONING.md](FIRMWARE_SIGNING_AND_PROVISIONING.md).

## Sekrety, logi i prywatność

- GitHub Environments `production-mobile`, `production-firmware` i
  `aquacyd-hil` powinny wymagać zatwierdzenia oraz ograniczać gałęzie/tagi.
- Klucze JKS i prywatny klucz RSA firmware nie mogą trafiać do cache, raportów
  ani artefaktów.
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
