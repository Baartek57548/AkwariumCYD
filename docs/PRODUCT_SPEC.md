# Specyfikacja produktów AquaCYD i Home Control

## Home Control

Home Control jest natywną aplikacją Flutter do sterowania całym domem. Nie jest przeglądarką strony pod podanym adresem ani aplikacją zależną od jednego serwera. Użytkownik wybiera źródło AquaHub, Home Assistant albo Demo, a wszystkie funkcje korzystają ze wspólnego, typowanego modelu encji.

Zakres produktu obejmuje onboarding, wykrywanie lokalne, wiele instancji, pomieszczenia, urządzenia, encje, historię, automatyzacje, sceny, skrypty, aktualizacje, diagnostykę, edytowalny dashboard, motyw jasny/ciemny/systemowy, język polski i angielski, układ telefon/tablet/web oraz pełne stany offline i błędów.

## AquaCYD Service

AquaCYD Service jest osobną aplikacją techniczną do bezpośredniego serwisu sterownika. Odpowiada za lokalne połączenie REST/BLE, provisioning, odzyskiwanie, kalibrację, diagnostykę i kontrolowane OTA CYD. Nie agreguje całego domu i nie przejmuje funkcji Home Control.

## CYD Controller

Sterownik CYD działa autonomicznie także bez P4, C6, Wi-Fi, Home Assistant i telefonu. Jest właścicielem fizycznego stanu akwarium oraz wszystkich ograniczeń bezpieczeństwa. Interfejsy zewnętrzne wysyłają intencje, nie wymuszają stanu GPIO.

## ESP32-P4 AquaHub i ESP32-C6 Gateway

ESP32-P4 zapewnia siedmiocalowy panel LVGL, lokalne API, rejestr urządzeń, historię i automatyzacje wysokiego poziomu. ESP32-C6 obsługuje łączność Wi-Fi/ESP-NOW jako kontrolowany most. Utrata dowolnego z nich nie może wyłączyć autonomicznej ochrony akwarium.

## Opcjonalne integracje

Home Assistant jest opcjonalnym adapterem danych i automatyki. MQTT jest transportem integracyjnym, a nie domeną produktu. Opcjonalna brama zdalna może pośredniczyć w dostępie spoza LAN, ale CYD ani P4 nie są wystawiane bezpośrednio do Internetu.

## Kryteria akceptacji

- Każda aplikacja ma odrębną nazwę, ikonę, przepływ startowy i odpowiedzialność.
- Home Control działa w pełnym trybie Demo bez sieci i używa tych samych repozytoriów oraz parserów co źródła produkcyjne.
- UI nie wykonuje bezpośrednich wywołań HTTP, WebSocket, MQTT, BLE, magazynu sekretów ani bazy danych.
- Identyfikator encji zawiera przestrzeń nazw źródła i pozostaje stabilny po synchronizacji.
- Nieznany typ albo przyszły stan encji jest wyświetlany bez awarii i bez niebezpiecznego domyślnego sterowania.
- Operacje ryzykowne pokazują wpływ, wymagają potwierdzenia i kończą się autorytatywnym ACK lub rollbackiem stanu optymistycznego.
- Poświadczenia są przechowywane w bezpiecznym magazynie, logi są redagowane, a usunięcie źródła czyści sesję i cache przypisany do tego źródła.
- Aktualizacje Home Control, AquaCYD Service, CYD, P4 i C6 są osobnymi procesami z osobnymi kompatybilnościami i statusami.
- Kompilacje, testy i skany działają lokalnie oraz w CI z filtrowaniem ścieżek i przypiętymi wersjami narzędzi.
- Brak testu fizycznego, klucza podpisu lub decyzji właściciela jest raportowany wprost i blokuje wyłącznie odpowiednią publikację, nie jest ukrywany jako sukces.
