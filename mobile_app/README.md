# cydAkwarium Mobile

Aplikacja Flutter dla Androida i iOS udostępniająca responsywny panel WWW
sterownika ESP32 w natywnym kontenerze WebView. Dzięki temu panel mobilny używa
tych samych widoków, endpointów API, logowania administratora, wykresów i OTA co
wersja otwierana w przeglądarce.

## Wymagania

- Flutter 3.41.5 lub nowszy z Dart 3.11,
- Android SDK 24+ albo iOS 13+,
- telefon oraz sterownik w tej samej sieci Wi-Fi.

## Uruchomienie

```powershell
cd mobile_app
flutter pub get
flutter run
```

Domyślny adres to `http://akwarium.local`. Menu z trzema kropkami w prawym
dolnym rogu pozwala odświeżyć panel lub ustawić adres IP, na przykład
`http://192.168.1.40`.

Lokalny symulator z repozytorium można uruchomić poleceniem `npm run dev:web`.
Emulator Androida łączy się z serwerem hosta przez `http://10.0.2.2:8000`, a
fizyczny telefon wymaga adresu IP komputera dostępnego w sieci LAN.

## Weryfikacja

```powershell
flutter analyze
flutter test
flutter build apk --debug
```

Wydanie produkcyjne Androida wymaga podpisania kluczem właściciela aplikacji.
Klucz i hasła należy przechowywać poza repozytorium i przekazać Gradle przez
bezpieczny magazyn CI lub lokalny plik właściwości wyłączony z kontroli wersji.
