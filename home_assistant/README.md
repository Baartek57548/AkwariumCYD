# Home Assistant dla AquaCYD

Bramka ESP32-C6 publikuje pomiary, alarmy i stany wyjść przez MQTT Discovery.
Home Assistant przechowuje historię, realizuje automatyzacje wyższego poziomu
i udostępnia bezpieczny zdalny interfejs. Sterownik CYD pozostaje autonomiczny
i nie zależy od działania Home Assistanta.

## Instalacja

1. Zainstaluj integrację MQTT i broker Mosquitto.
2. Utwórz osobne konta MQTT dla `aquacyd_gateway`, `aquacyd_hmi` i
   `homeassistant`. Nie używaj konta anonimowego.
3. Dodaj do `configuration.yaml`:

   ```yaml
   homeassistant:
     packages: !include_dir_named packages

   lovelace:
     dashboards:
       aquacyd:
         mode: yaml
         title: AquaCYD
         icon: mdi:fishbowl
         show_in_sidebar: true
         filename: dashboards/aquacyd.yaml
   ```

4. Skopiuj `packages/aquacyd.yaml` oraz `dashboards/aquacyd.yaml` do
   odpowiadających katalogów konfiguracji Home Assistanta.
5. Sprawdź konfigurację w `Narzędzia deweloperskie → YAML` i uruchom Home
   Assistant ponownie.
6. Wgraj bramkę C6. Po pierwszej telemetrii encje o stabilnych identyfikatorach
   `sensor.aquacyd_aquarium_*` i `binary_sensor.aquacyd_aquarium_*` zostaną
   utworzone automatycznie.

Pakiet dodaje dziesięć bezpiecznych skryptów sterujących, czas ręcznego
nadpisania i karmienia oraz powiadomienia o utracie łącza, wycieku, niskim
poziomie wody i braku potwierdzenia komendy. Każde polecenie otrzymuje unikatowy
16-cyfrowy identyfikator, dzięki któremu CYD odrzuca duplikaty z MQTT QoS 1
w ograniczonym, deterministycznym oknie pamięci. Polecenia nie są retained.

## Uprawnienia brokera

Plik `mosquitto/aquacyd.acl` jest gotowym przykładem ACL dla samodzielnego
Mosquitto. W konfiguracji brokera wskaż go przez `acl_file` i połącz z plikiem
haseł utworzonym poleceniem `mosquitto_passwd`. Jeżeli używasz dodatku Mosquitto
Broker w Home Assistant OS, utwórz trzy równoważne konta w Home Assistant i
nie publikuj portu 1883 poza zaufaną sieć LAN.

Hasła MQTT zapisuje się przez `idf.py menuconfig` osobno w bramce i panelu.
Zdalny dostęp realizuj przez Home Assistant Cloud albo VPN, nigdy przez
publiczne przekierowanie portu MQTT.
