# OTA / portal serwisowy

Ten katalog jest serwowany przez ESP32 jako portal WWW/OTA. Glowny plik to
`index.html`, skopiowany z katalogu `web/` i dopasowany do endpointow firmware:

- `/api/status`
- `/api/history.csv`
- `/api/files`
- `/download`
- `/api/ota/stop?pin=<PIN>`
- `/update?pin=<PIN>`

Ta sama strona dziala w dwoch trybach sieci:

- STA: `http://akwarium.local/` oraz adres IP przydzielony przez router.
- OTA AP: `http://192.168.4.1/`.

Opcjonalne pliki pomocnicze:

```text
firmware.bin
version.txt
```

Firmware wymaga poprawnego PIN-u przed zamknieciem portalu albo startem OTA.
Aktualny PIN domyslny jest zdefiniowany w `include/config.h`.
