# Plan migracji do monorepo Home Control

## Cel

Migracja rozdziela uniwersalną aplikację Home Control, serwisową aplikację AquaCYD Service, firmware trzech układów i integracje zewnętrzne. Jest wykonywana małymi commitami z zieloną walidacją po zmianach ścieżek, bez zmiany identyfikatorów aplikacji i bez przepisywania historii Git.

## Mapowanie ścieżek

| Przed migracją | Po migracji | Powód |
| --- | --- | --- |
| `home_assistant_app/` | `apps/home_control/` | nazwa produktu niezależna od jednego dostawcy danych |
| `mobile_app/` | `apps/aquacyd_service/` | jawna rola serwisowa i odzyskiwania |
| `src/`, `include/`, `platformio.ini`, `min_spiffs.csv` | `firmware/cyd_controller/` | jeden samodzielny projekt PlatformIO |
| `firmware/esp32p4_hmi/` | `firmware/esp32p4_hub/` | P4 jest panelem i lokalnym AquaHubem |
| `firmware/esp32c6_gateway/` | bez zmiany nazwy | nazwa już odpowiada odpowiedzialności |
| `lib/aquacyd_link/` | `firmware/shared/aquacyd_link/` | współdzielony kontrakt C++ urządzeń |
| `lib/aquahub_core/` | `firmware/shared/aquahub_core/` | współdzielona logika P4 i testów natywnych |
| `lib/aquarium_domain/` | `firmware/cyd_controller/lib/aquarium_domain/` | domena wykonawcza należy wyłącznie do CYD |
| `gateway/` | `services/remote_gateway/` | warstwa usług odróżniona od firmware C6 |
| `home_assistant/` | `integrations/home_assistant/` | opcjonalna integracja platformy |
| `test/test_native_domain/` | `firmware/cyd_controller/test/test_native_domain/` | testy są częścią projektu PlatformIO |
| `test/web/`, `test/cyd-web/` | `web/tests/` | testy są własnością interfejsu webowego |
| `web/`, `design/`, `docs/`, `tools/`, `scripts/` | bez zmiany | czytelne odpowiedzialności na poziomie repozytorium |

Pakiety Dart zostaną wydzielone tylko wtedy, gdy mają co najmniej dwóch realnych konsumentów lub wyraźnie oddzielają stabilny kontrakt od infrastruktury:

- `packages/aquacyd_protocol` — wersjonowane modele i parsery kontraktu akwarium;
- `packages/home_entities` — uniwersalny model encji używany przez adaptery i UI;
- `packages/secure_connectivity` — wyniki, błędy, redakcja, retry/backoff i kontrakty bezpiecznych poświadczeń;
- `packages/design_system` — tokeny marki, tematy i wspólne prymitywy obu aplikacji;
- `packages/test_support` — deterministyczne fixtures i fake zegara/sieci.

## Etapy i bramy jakości

1. Zapisanie audytu, źródeł prawdy, mapy zależności i kryteriów akceptacji.
2. Usunięcie odtwarzalnych binariów z bieżącego drzewa oraz wzmocnienie `.gitignore`.
3. Mechaniczne przeniesienie katalogów przez Git, bez zmiany działania.
4. Naprawa ścieżek w CMake, PlatformIO, skryptach, npm, workflowach i dokumentacji wykonawczej.
5. Pełna kompilacja obu aplikacji, webu, Node.js i wszystkich firmware po migracji.
6. Wydzielenie współdzielonych kontraktów i testów bez tworzenia cyklicznych zależności.
7. Przekształcenie aplikacji centralnej w Home Control oraz zachowanie AquaCYD Service.
8. Ukończenie adapterów AquaHub, Home Assistant i Demo z jednakowym modelem domenowym.
9. Ukończenie stanów UI/UX, dostępności, lokalizacji, modułu akwarium i osobnych procesów OTA.
10. Testy jednostkowe, widgetowe, integracyjne, E2E, analiza, kompilacje oraz wizualna kontrola telefon/tablet/web.
11. Dokumentacja operacyjna, threat model, SBOM, skany sekretów i końcowy audyt diffu.
12. Logiczne commity, push gałęzi i Pull Request bez automatycznego merge ani publikacji produkcyjnego release.

Każdy etap strukturalny jest akceptowany dopiero po potwierdzeniu, że ścieżki buildów i testów nie odwołują się do starej lokalizacji. Test fizyczny jest osobną bramą HIL i nie może być zastąpiony mockiem.

## Kompatybilność

- Identyfikatory Android/iOS obu aplikacji pozostają bez zmian.
- Dotychczasowe tagi `mobile-*`, `home-*` i `firmware-*` pozostają obsługiwane; nowe aliasy produktowe mogą być dodane bez zrywania starego procesu.
- Kontrakty urządzeń są wersjonowane i tolerują nieznane pola oraz nieznane typy encji.
- Dane lokalne aplikacji otrzymują migracje schematu; nie są czyszczone tylko dlatego, że zmieniła się nazwa produktu.
- Home Assistant jest jednym z adapterów. Użytkownik może uruchomić Home Control wyłącznie z AquaHubem lub trybem Demo.

## Strategia wycofania

Każdy logiczny etap ma osobny commit, dlatego awaria migracji może zostać wycofana przez standardowy revert bez utraty historii. Artefakty usunięte z bieżącego drzewa są dostępne w historycznych commitach, istniejących GitHub Releases albo odtwarzalne z przypiętego toolchainu. Nie jest planowany force-push ani rebase publicznej historii.

