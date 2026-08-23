# AquaCYD Service

## Odpowiedzialność

AquaCYD Service w `apps/aquacyd_service` jest aplikacją techniczną do lokalnej
obsługi pojedynczego sterownika CYD. Nie jest panelem całego domu i nie jest
zastępowana przez Home Control. Zachowuje dotychczasowe identyfikatory Android/iOS
oraz ścieżkę migracji danych użytkownika.

Zakres aplikacji:

- bezpośrednie połączenie HTTPS/REST i BLE;
- provisioning Wi-Fi oraz bezpieczne wykrywanie urządzenia;
- diagnostyka sprzętu, alarmów, sieci, pamięci i restartów;
- kalibracja pH/EC i świadomy tryb serwisowy;
- konfiguracja harmonogramów i parametrów akwarium;
- odzyskiwanie, eksport diagnostyczny oraz OTA CYD;
- osobna aktualizacja APK w kanale `mobile-vX.Y.Z`.

## Granica bezpieczeństwa

Serwis wysyła intencję i prezentuje autorytatywną odpowiedź sterownika. CYD
zawsze ponownie waliduje zakres, tryb serwisowy, wersję konfiguracji, blokady,
TTL oraz stan czujników. Aplikacja nie może wymusić GPIO ani wyłączyć lokalnego
fail-safe. Krytyczne operacje wymagają lokalnej sesji i potwierdzenia.

BLE produkcyjne używa LE Secure Connections, bondingu, MITM i kodu pokazanego na
zaufanym ekranie CYD. REST używa krótkotrwałej sesji administracyjnej. PIN,
token, hasło Wi-Fi i klucze nie trafiają do URL ani logów.

## Rozwój i testy

```powershell
cd apps/aquacyd_service
flutter pub get
flutter analyze
flutter test
flutter build apk --debug
flutter build apk --release
```

Build release potwierdza kompilację, ale nie jest produkcyjnym artefaktem, jeśli
nie użyto właścicielskiego keystore i pipeline nie zweryfikował podpisu. Zmiany
serwisowe nie powinny importować UI Home Control; współdzielenie jest dozwolone
tylko przez małe pakiety `packages/`.
