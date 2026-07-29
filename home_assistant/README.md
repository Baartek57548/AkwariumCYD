# Home Assistant dla AquaCYD

Gateway ESP32-C6 publikuje encje pomiarowe przez MQTT Discovery, dlatego nie
trzeba ręcznie definiować sensorów. W Home Assistant należy:

1. zainstalować i skonfigurować integrację MQTT;
2. dodać do `configuration.yaml` obsługę pakietów:

   ```yaml
   homeassistant:
     packages: !include_dir_named packages
   ```

3. skopiować `packages/aquacyd.yaml` do katalogu `packages`;
4. sprawdzić konfigurację i uruchomić Home Assistant ponownie.

Pakiet dodaje cztery bezpieczne, czasowe skrypty sterujące oraz automatyzacje
dostępności i wycieku. Polecenia nie są retained, używają QoS 1 i wymagają
potwierdzenia wykonania z CYD. Zaawansowane harmonogramy należy zapisywać w HA,
ale podstawowe nastawy awaryjne i zabezpieczenia muszą pozostać lokalnie w CYD.

Broker MQTT powinien być dostępny wyłącznie w zaufanej sieci LAN. Dostęp z
Internetu należy realizować przez Home Assistant Cloud albo VPN, nigdy przez
publiczne przekierowanie portu 1883.
