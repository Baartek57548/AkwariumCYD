# Checklista HIL przed produkcyjnym wydaniem

## Warunki wejścia

- [ ] stanowisko jest odłączone od działającego akwarium i używa bezpiecznych obciążeń;
- [ ] CYD, P4 i C6 mają zapisane numery seryjne, wersje, board ID i bieżący slot;
- [ ] zasilacz ma ograniczenie prądu, UART zapisuje log, fixture mierzy wyjścia;
- [ ] sieć testowa jest izolowana, a konto MQTT ma laboratoryjne ACL;
- [ ] obraz rollbacku jest zdrowy; operator ma fizyczny dostęp do recovery;
- [ ] sekrety runnera są w chronionym środowisku i nie trafią do raportu.

## Automatyczna brama

```powershell
python tools/hil/runner.py --dry-run
python tools/hil/runner.py --self-test
python tools/hil/runner.py --require-hardware --forbid-skips
```

Ostatnie polecenie wymaga skonfigurowanych URL/portów/capabilities oraz jawnych
zgód mutacji. Wynik mock/self-test jest raportowany osobno i nie spełnia bramy.

## Scenariusze fizyczne

- [ ] 72 godziny ciągłej telemetrii nie powodują utraty próbek, narastania heap ani zawieszenia automatyki;
- [ ] health API, wersja, reset reason i zegar są poprawne;
- [ ] dwa identyczne `command_id` wykonują operację najwyżej raz;
- [ ] celowo utracony ACK uruchamia ograniczone ponowienie, ale nie wykonuje polecenia drugi raz;
- [ ] konflikt `expected_revision` zwraca NACK i nie zmienia konfiguracji;
- [ ] przeterminowane, złe CRC i nieznana wersja ramki są odrzucane;
- [ ] override kończy się sam i każdy przekaźnik wraca do AUTO;
- [ ] awaria Wi-Fi/MQTT/P4/C6 nie zatrzymuje lokalnych harmonogramów CYD;
- [ ] restart routera i zmiana dzierżawy DHCP kończą się automatycznym reconnectem bez ingerencji operatora;
- [ ] zmiana certyfikatu P4 jest odrzucana do czasu jawnego ponownego parowania i zatwierdzenia fingerprintu;
- [ ] sensor NaN/timeout aktywuje właściwy alarm i bezpieczny stan;
- [ ] odłączenie SD nie zatrzymuje sterowania, a remount nie uszkadza danych;
- [ ] brownout i watchdog startują z bezpiecznymi wyjściami;
- [ ] leak/low-water zatrzymuje odpowiednie urządzenia mimo komendy zewnętrznej;
- [ ] osobno zmierzono lampy front/rear: DAY → DAYBREAK → NIGHT → DAY;
- [ ] aktualizacja w obu slotach przechodzi health i potwierdza nowy obraz;
- [ ] odcięcie zasilania podczas pobierania/zapisu nie brickuje urządzenia;
- [ ] pełna utrata i powrót zasilania CYD, C6 i P4 przywracają bezpieczne wyjścia oraz ostatnią zatwierdzoną konfigurację;
- [ ] celowo zły hash/podpis/target/downgrade jest odrzucony;
- [ ] niezdrowy nowy obraz automatycznie wraca do starego slotu;
- [ ] po każdym scenariuszu brak aktywnego override, alarmy są znane, wyjścia AUTO.

## UI i aplikacje na sprzęcie

- [ ] P4 7″: dotyk, jasność, rotacja, 24 h pracy, reconnect i brak wycieku heap;
- [ ] CYD 320×240: wszystkie ekrany, alarm, PIN i czytelność bez przycięć;
- [ ] Android: onboarding, secure storage, LAN discovery, HA, AquaHub, offline;
- [ ] OTA Home Control i AquaCYD Service weryfikuje certyfikat i wymaga zgody;
- [ ] powiększony tekst i TalkBack pozwalają wykonać główne operacje;
- [ ] dwie instancje HA nie mieszają encji, tokenów ani cache.

## Dowody

Do releasu dołącza się hash commit, raport runnera, log UART z redakcją, wersje
narzędzi, zdjęcie stanowiska bez sekretów, wyniki pomiaru fixture i podpis
operatora. Każde `SKIP` blokuje produkcyjne firmware. Rozwinięcie scenariuszy:
[PRODUCTION_HIL.md](PRODUCTION_HIL.md).
