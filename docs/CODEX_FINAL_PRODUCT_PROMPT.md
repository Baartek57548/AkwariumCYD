# Dokument historyczny — wcześniejszy prompt finalizacji AquaHub

> Ten prompt nie jest już źródłem wymagań. Zastępują go zrealizowana
> specyfikacja Home Control i kanoniczne dokumenty w katalogu `docs`.

Poniższy prompt jest dopasowany do tego repozytorium. Wymusza pracę na kodzie,
debugowanie, testy wizualne, uczciwe granice sprzętowe i końcowy push zamiast
samego opisu lub makiet.

```text
Pracujesz w repozytorium cydAquarium jako senior Flutter, UX, ESP-IDF, LVGL,
embedded-security i system architect. Twoim celem jest doprowadzenie AquaHub do
stanu spójnego produktu, a nie przygotowanie demonstracji. Zachowaj autonomię
istniejącego sterownika CYD: pomiary, harmonogramy, interlocki, GPIO i fail-safe
muszą działać bez panelu, Wi-Fi, telefonu i Internetu. ESP32-P4 + pokładowy C6
jest centrum AquaHub z LVGL, HTTPS API, lokalnym MQTTS, rejestrem, historią,
automatyzacjami i OTA. Stały C6 przy akwarium jest bramką ESP-NOW ↔ MQTTS.
Aplikacja home_assistant_app jest odrębnym, uniwersalnym klientem Flutter
AquaHub; nie łączy się bezpośrednio ze sterownikiem i nie wymaga Home Assistant
Core. Oficjalny Home Assistant ma pozostać opcjonalną integracją Discovery.

Najpierw przeczytaj AGENTS.md, README, docs/AQUAHUB_ARCHITECTURE.md,
docs/PROJECT_DIRECTION.md, dokumentację bezpieczeństwa i aktualny git diff.
Nie usuwaj ani nie nadpisuj zmian użytkownika. Utwórz lub wykorzystaj gałąź z
prefiksem codex/. Nie używaj placeholderów, TODO, atrap udających działającą
funkcję ani zakomentowanej logiki. Każdy zmieniony kod ma się kompilować, mieć
obsługę błędów, walidację granic i deterministyczne limity pamięci.

Dokończ aplikację Flutter w Material 3 jako pełne UI/UX Android, iOS i web:
onboarding z kodem fizycznym i pinningiem TLS, pulpit, alarmy, uniwersalne
urządzenia i encje, wyszukiwanie i filtry, wszystkie bezpieczne typy sterowania,
historię, automatyzacje z edytorem reguł, diagnostykę, stany puste, ładowanie,
offline/stale-data, retry, dostępność, responsywną nawigację i centrum
aktualizacji. Nie zaszywaj nazw czujników. Nieznany przyszły typ encji nie może
zepsuć całego rejestru. Krytyczne encje nie mogą otrzymać zwykłej komendy.
Zatrzymuj polling w tle i odświeżaj po wznowieniu. Sekrety zapisuj tylko w
systemowym secure storage. Zdalny dostęp zakłada VPN, nie publiczny port P4.

Zaimplementuj równocześnie wymagane endpointy i logikę P4. Automatyzacje mają
być walidowane, trwałe w NVS, ograniczone pojemnością i wykonywane lokalnie bez
telefonu. OTA P4 ma używać stałego bazowego HTTPS, manifestu o ograniczonym
rozmiarze, walidacji target/version/file/size/security_version, SHA-256,
nieaktywnej partycji A/B, potwierdzenia zdrowia po starcie i rollbacku. API nie
może przyjmować dowolnego URL-a. UI ma jawnie odróżniać zaimplementowane OTA P4
od przyszłego OTA urządzeń. Opisz Secure Boot v2 i Flash Encryption jako etap
kontrolowanego provisioningu; nigdy automatycznie nie przepalaj eFuse.

Dodaj kompletny tryb demo korzystający z tego samego parsera kontraktu, aby UI
można było przetestować bez sprzętu. Dodaj testy modeli, błędnych odpowiedzi,
paginacji, komend, OTA, automatyzacji, przyszłych encji, lifecycle i nawigacji.
Uruchom dart format, flutter analyze, cały flutter test, release build Flutter
Web, testy natywne PlatformIO oraz pełne buildy P4 i C6 na przypiętym ESP-IDF.
Uruchom aplikację webową w trybie demo i użyj przeglądarki do sprawdzenia
widoków desktop i 390×844, nawigacji, scrollowania, dialogów oraz logów konsoli.
Napraw każdą regresję i ponawiaj właściwy test aż będzie czysty. Jeśli nie ma
podłączonego sprzętu, nie udawaj HIL: wykonaj testy programowe, przygotuj ścisłą
checklistę HIL i zaznacz tę jedną granicę w raporcie.

Zaktualizuj dokumentację architektury, kontrakt API, instrukcję uruchomienia,
wydawania OTA, rollbacku, model bezpieczeństwa, tryb demo i plan dodawania
nowych urządzeń przez Discovery. Sprawdź git diff pod kątem sekretów, binariów,
plików build i zmian użytkownika. Dodaj do commita wyłącznie pliki należące do
zadania. Zrób opisowy commit i wypchnij aktualną gałąź do origin. Push jest
warunkiem ukończenia. W raporcie końcowym podaj branch, hash commita, wykonane
testy, wynik buildów, linki do najważniejszych plików oraz jedyne pozostałe
czynności wymagające fizycznego sprzętu. Dopiero po udanym pushu, i tylko jeśli
użytkownik jawnie zażądał tego w tej samej rozmowie, zaplanuj wyłączenie Windows
z dwuminutowym opóźnieniem, aby można było je anulować przez shutdown /a.
```

Najważniejsze rozszerzenie względem krótkiego polecenia to mierzalna definicja
„gotowe”: konkretne ekrany i granice bezpieczeństwa, testy, buildy obu układów,
oględziny responsywnego UI, zakaz udawania HIL oraz push jako warunek końcowy.
