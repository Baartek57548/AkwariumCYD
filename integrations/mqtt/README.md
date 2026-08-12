# Integracja MQTT

MQTT jest opcjonalnym transportem między Gateway C6, AquaHub P4 i Home
Assistantem. Nie jest źródłem prawdy dla wyjść i nie może ominąć walidacji CYD.

## Zasady

- broker działa w zaufanym LAN, bez anonymous i bez publicznego portu 1883;
- C6 publikuje stan/availability/discovery i czyta tylko własne command topics;
- P4 czyta stan/ACK i publikuje polecenia wyłącznie do przypisanego urządzenia;
- Home Assistant ma prawa discovery/state oraz jawnie dozwolone command topics;
- polecenia nie są retained, używają QoS 1 i unikatowego `command_id`;
- telemetria może być retained, ale zawiera timestamp/boot ID i jest oznaczana stale;
- sekrety są w NVS/secret store, a pliki repo zawierają jedynie szablony ACL.

Przykładowe ACL i dashboard znajdują się w
`integrations/home_assistant/mosquitto` oraz `integrations/home_assistant`. Format
komend i ACK opisuje [DEVICE_CONTRACT.md](../../docs/DEVICE_CONTRACT.md).
