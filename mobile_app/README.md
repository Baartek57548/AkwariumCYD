# AquaCYD Control

Kompletna aplikacja Flutter dla Androida i iOS. Natywny tryb Wi-Fi używa
bezpośrednio tego samego API i tych samych nazw akcji co panel `web/`, dlatego
walidacja oraz zachowanie firmware pozostają wspólne dla strony i telefonu.

## Funkcje

- pulpit telemetryczny, alarmy i ręczne sterowanie pięcioma wyjściami,
- karmienie ręczne z potwierdzeniem wykonania,
- wykres temperatury 1/3/6/12/24 h, tabela próbek, archiwa SD i eksport CSV,
- kompletny harmonogram światła, lampy roślinnej, filtra, napowietrzania,
  termostatu i karmnika,
- automatyka temperatury, CO₂, ATO i zabezpieczenie przed wyciekiem,
- kreator mapy ośmiu przekaźników MCP23017, test kanałów oraz import/eksport JSON,
- logi normalne i krytyczne, eksport TXT i czyszczenie ważnych wpisów,
- diagnostyka czujników oraz skanowanie I²C, UART i OneWire,
- ustawienia Wi-Fi, wyświetlacza CYD i zegara,
- restart, reset fabryczny i aktualizacja firmware OTA,
- tryb DEV z pełną symulacją wszystkich powyższych operacji w pamięci RAM,
- podstawowe sterowanie BLE bez dostępu do sieci,
- oryginalny panel WWW w WebView jako tryb zgodności.

Operacje zmieniające konfigurację wymagają sesji administratora. Domyślny PIN
firmware i symulatora DEV to `1234`.

## Wymagania

- Flutter 3.41.5 lub nowszy z Dart 3.11,
- Android SDK 24+ albo iOS 13+,
- Bluetooth w telefonie dla trybu BLE,
- telefon oraz sterownik w tej samej sieci Wi-Fi albo połączenie z AP sterownika
  dla pełnej aplikacji natywnej i WebView.

## Uruchomienie

```powershell
cd mobile_app
flutter pub get
flutter run
```

W trybie Wi-Fi domyślny adres to `http://akwarium.local`. Dla punktu dostępowego
sterownika można użyć `http://192.168.4.1`. Lokalny symulator WWW działa przez
`npm run dev:web`; emulator Androida łączy się z hostem przez
`http://10.0.2.2:8000`.

Tryb DEV nie wykonuje operacji na fizycznym sprzęcie. Zawiera kompletne modele
statusu, logów, diagnostyki, harmonogramów, ustawień, przekaźników i OTA, dzięki
czemu pozwala sprawdzić całą nawigację bez sterownika.

Specyfikacja istniejącego transportu BLE znajduje się w
[docs/ble-protocol.md](docs/ble-protocol.md).

## Weryfikacja

```powershell
flutter analyze
flutter test
flutter build apk --debug --split-per-abi
```

Wydanie produkcyjne Androida wymaga podpisania kluczem właściciela aplikacji.
Klucz oraz hasła muszą pozostać poza repozytorium. Konfiguracja
`android/key.properties` musi definiować niepuste pola `storePassword`,
`keyPassword`, `keyAlias` oraz `storeFile`. Gradle celowo przerywa build release,
jeśli konfiguracja podpisu jest niekompletna, aby nie opublikować ponownie
nieinstalowalnego APK.

## Aktualizacje aplikacji z GitHub Releases

Wariant `current` sprawdza aktualizacje po uruchomieniu, po powrocie do aplikacji
i nie częściej niż raz na 12 godzin. Użytkownik może też uruchomić sprawdzanie
ręcznie na ekranie połączenia albo w ustawieniach profilu. Po wyrażeniu zgody
APK jest pobierany strumieniowo, a Android przed otwarciem instalatora sprawdza:

- sumę SHA-256 zwróconą przez GitHub Releases,
- identyfikator pakietu `pl.cydakwarium.cyd_aquarium_mobile`,
- kod wersji wyższy od zainstalowanego,
- certyfikat podpisu zgodny z bieżącą aplikacją.

Każde stabilne wydanie mobilne musi spełniać cały kontrakt:

- tag: `mobile-vX.Y.Z`,
- nazwa assetu: `AquaCYD-Control-X.Y.Z-current.apk`,
- wydanie nie może być draftem ani prerelease,
- `version` w `pubspec.yaml` musi mieć wyższy `versionCode`,
- APK musi być podpisany tym samym prywatnym kluczem co poprzednie wersje.

Pierwsza aktualizacja wymaga jednorazowego zezwolenia Androida „Zezwalaj z tego
źródła”. Każda instalacja nadal kończy się systemowym potwierdzeniem użytkownika;
aplikacja nie omija zabezpieczeń Androida. Warianty `full` oraz `dev` mają inne
identyfikatory pakietów i celowo nie używają kanału aktualizacji `current`.
