# cydAkwarium Mobile

Aplikacja Flutter dla Androida i iOS obsługująca trzy niezależne sposoby pracy:

- Bluetooth Low Energy — natywna telemetria i sterowanie bez Wi-Fi,
- DEV RAM — pełna lokalna symulacja bez sterownika,
- Wi-Fi — responsywny panel WWW w natywnym kontenerze WebView.

## Wymagania

- Flutter 3.41.5 lub nowszy z Dart 3.11,
- Android SDK 24+ albo iOS 13+,
- Bluetooth w telefonie dla trybu BLE,
- telefon oraz sterownik w tej samej sieci Wi-Fi wyłącznie dla trybu WWW.

## Uruchomienie

```powershell
cd mobile_app
flutter pub get
flutter run
```

Ekran początkowy pozwala wybrać BLE, DEV lub Wi-Fi. Tryb DEV używa PIN-u `1234`
i symuluje czujniki, przekaźniki oraz karmienie wyłącznie w pamięci RAM. Nie
wykonuje żadnych operacji na fizycznym sprzęcie.

Tryb BLE skanuje wyłącznie urządzenia reklamujące usługę cydAkwarium. Komendy
sterujące wymagają PIN-u administratora, a telemetria jest przesyłana co dwie
sekundy w wersjonowanych, fragmentowanych ramkach GATT. Specyfikacja znajduje
się w [docs/ble-protocol.md](docs/ble-protocol.md).

W trybie Wi-Fi domyślny adres to `http://akwarium.local`. Menu z trzema kropkami
w prawym dolnym rogu pozwala odświeżyć panel lub ustawić adres IP, na przykład
`http://192.168.1.40`.

Lokalny symulator z repozytorium można uruchomić poleceniem `npm run dev:web`.
Emulator Androida łączy się z serwerem hosta przez `http://10.0.2.2:8000`, a
fizyczny telefon wymaga adresu IP komputera dostępnego w sieci LAN.

## Weryfikacja

```powershell
flutter analyze
flutter test
flutter build apk --debug --split-per-abi
```

Wydanie produkcyjne Androida wymaga podpisania kluczem właściciela aplikacji.
Klucz i hasła należy przechowywać poza repozytorium i przekazać Gradle przez
bezpieczny magazyn CI lub lokalny plik właściwości wyłączony z kontroli wersji.
