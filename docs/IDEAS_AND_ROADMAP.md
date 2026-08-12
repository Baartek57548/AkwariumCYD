# Pomysły i roadmapa

## Priorytet P0 — przed pierwszym produkcyjnym wydaniem

- dostarczyć publiczny Client ID i redirect URI, wykonać OAuth HA z PKCE oraz
  test wygaśnięcia/odświeżenia tokenu;
- uruchomić self-hosted HIL dla CYD/P4/C6 i przejść checklistę bez skipów;
- skonfigurować keystore Android, Apple signing, klucze firmware i provenance;
- zakończyć OTA C6: dwie partycje, podpis, health, security version i rollback;
- przeprowadzić 24–72 h soak test P4, C6, MQTT i obu aplikacji;
- audyt dostępności na TalkBack/VoiceOver i urządzeniach 320 px oraz 7″.

## P1 — rozwój produktu

- discovery wielu AquaHubów oraz równoczesne źródła zamiast jednego aktywnego;
- polityki ról domownik/gość/serwis i ograniczenia per encja;
- lokalna baza historii z retencją, agregacją i eksportem zaszyfrowanym;
- kanał beta OTA opt-in z osobnym podpisem i możliwością powrotu do stable;
- natywne powiadomienia Home Control z deep linkiem do alarmu/urządzenia;
- edytor automatyzacji wysokiego poziomu z symulacją warunków przed zapisem;
- kamera przez bezpieczny proxy/token o krótkim TTL, bez ujawniania URL;
- rozbudowane media, climate presets, cover position i vacuum zones na podstawie
  oficjalnie reklamowanych capabilities.

## P2 — ekosystem urządzeń

- kreator nowego urządzenia z wersjonowanym schema capability i certyfikacją;
- Thread/Matter przez C6 jako oddzielny adapter, bez mieszania domeny z MQTT;
- energooszczędne sensory ESP-NOW z rotacją klucza i pomiarem baterii;
- redundancja P4/Raspberry Pi dla historii i automatyzacji bez przejęcia safety;
- plugin SDK tylko dla bezpiecznych typów encji i podpisanych rozszerzeń;
- eksport dashboardu do Home Assistant oraz import nazw/pomieszczeń bez utraty ID.

## HMI i Figma

`design/hmi` zawiera trzynaście ramek SVG, tokeny i manifest prototypu, a
`design/cyd-hmi` sześć ramek 320×240 odtwarzających bieżące ekrany CYD. SVG można
zaimportować do Figma z zachowaniem warstw wektorowych. Kod produkcyjny LVGL jest
w `firmware/esp32p4_hub/main/hmi_ui.cpp`; przepływ synchronizacji opisuje
[HMI_LVGL_FIGMA_WORKFLOW.md](HMI_LVGL_FIGMA_WORKFLOW.md).

Po udostępnieniu linku do pliku Figma i uprawnień można połączyć repo z Figma
MCP, pobierać konkretne node ID, porównywać screenshoty i implementować zmiany
1:1. Repo nie przechowuje tokenu Figma.

## Raspberry Pi

Raspberry Pi 5 4 GB jest optymalnym hostem Home Assistant OS, Mosquitto i historii
dla większej instalacji; Pi 4 4 GB pozostaje wariantem budżetowym. Pi nie zastępuje
autonomicznego CYD ani dotykowego P4. Panel 7″ może działać jako odpinany klient
Home Control, natomiast stały P4 zapewnia natychmiastowy lokalny HMI bez systemu
Linux. Szczegóły kierunku znajdują się w [PROJECT_DIRECTION.md](PROJECT_DIRECTION.md).
