# Wydawanie OTA dla AquaHub ESP32-P4

## Model bezpieczeństwa

Panel pobiera manifest i obraz tylko z katalogu wskazanego podczas kompilacji
przez `AQUAHUB_OTA_BASE_URL`. API aplikacji nie przyjmuje URL-a ani pliku.
Klient wymaga HTTPS z certyfikatem z publicznego lub wbudowanego pakietu CA,
odrzuca przekierowania, kontroluje limit 5 MiB oraz porównuje rozmiar i SHA-256.
Nowy obraz trafia do nieaktywnej partycji OTA i zostaje potwierdzony dopiero po
uruchomieniu brokera, API, rejestru i UI.

To jest gotowy bezpieczny kanał dla instalacji domowej. Dla produktu seryjnego
należy dodatkowo włączyć Secure Boot v2 i Flash Encryption podczas
kontrolowanego provisioningu. Sam hash z manifestu nie zastępuje podpisu obrazu,
jeśli serwer wydawniczy lub zaufany urząd certyfikacji zostaną przejęte.

## Zbudowanie i spakowanie wydania

Po kompilacji ESP-IDF generator kopiuje obraz pod bezpieczną nazwę, oblicza hash
i tworzy UTF-8 `manifest.json` bez BOM:

```powershell
.\tools\build-p4-c6.ps1 -Target p4 -IdfPath C:\esp\v5.4.4-full\esp-idf -IdfToolsPath C:\Espressif\tools
.\tools\package-aquahub-ota.ps1 `
  -FirmwarePath .\firmware\esp32p4_hub\build\aquahub_esp32p4.bin `
  -Version 1.1.0 `
  -SecurityVersion 1 `
  -Notes 'Automatyzacje lokalne, centrum OTA i poprawki stabilności.'
```

Wynikiem jest katalog `artifacts/aquahub-ota` zawierający dokładnie obraz oraz
manifest. Przykładowa struktura manifestu:

```json
{
  "target": "aquahub-p4",
  "release_id": "stable-1.1.0",
  "version": "1.1.0",
  "file": "aquahub-p4-1.1.0.bin",
  "size": 1098208,
  "sha256": "9A7D4ED3C07C7B7F7B960F18A77AE10E566813013DC0D59B8AB4E534708A5B75",
  "security_version": 1,
  "mandatory": false,
  "notes": "Automatyzacje lokalne, centrum OTA i poprawki stabilności."
}
```

Wartości rozmiaru i SHA-256 w prawdziwym wydaniu zawsze generuje skrypt; nie
wolno kopiować ich z przykładu.

## Publikacja

1. Skonfigurować statyczny katalog HTTPS bez przekierowań, uwierzytelniania
   formularzem i dynamicznego przepisywania nazw.
2. Wgrać najpierw plik `.bin`, a po nim atomowo zastąpić `manifest.json`.
3. Ustawić w `idf.py menuconfig` adres, na przykład
   `https://firmware.aquahub.example/aquahub/stable`.
4. Zbudować panel ponownie. Pusty adres pozostawia API OTA w stanie `disabled`.
5. W aplikacji otworzyć **Więcej → Aktualizacje → Sprawdź wydania**.
6. Po instalacji sprawdzić wersję, dostępność broker/API i rejestr urządzeń.

Kanał testowy powinien mieć osobny katalog i osobny build z innym
`AQUAHUB_OTA_BASE_URL`. Manifest stabilny nie powinien wskazywać wersji testowej.

## Rollback i wersja bezpieczeństwa

`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` jest włączone. Jeśli nowy obraz nie
dojdzie do punktu potwierdzenia po starcie usług, bootloader wróci do poprzedniej
partycji. Pole `security_version` nie może być niższe od wartości obrazu
uruchomionego. Podniesienie wersji bezpieczeństwa jest decyzją wydaniową i przed
produkcją wymaga testu na zapasowej płytce.

OTA nie zastępuje testu sprzętowego. Każde wydanie musi przejść próbę zaniku
zasilania podczas pobierania, restartu po zapisie, niedostępnego serwera,
błędnego hasha, zbyt starej wersji bezpieczeństwa i automatycznego rollbacku.

Dokumentacja referencyjna: [ESP-IDF OTA](https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32/api-reference/system/ota.html),
[ESP HTTPS OTA](https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32/api-reference/system/esp_https_ota.html)
oraz [Secure Boot v2 dla ESP32-P4](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/security/secure-boot-v2.html).
