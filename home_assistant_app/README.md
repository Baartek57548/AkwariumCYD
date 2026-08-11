# AquaHub — aplikacja Flutter

Druga aplikacja mobilna projektu. Łączy wszystkie urządzenia i czujniki przez
HTTPS API własnego centrum AquaHub na ESP32-P4. Nie komunikuje się bezpośrednio
z CYD i nie wymaga Home Assistant Core ani Raspberry Pi.

## Zakres

- Android, iOS i web z responsywną nawigacją;
- pierwsze parowanie sześciocyfrowym kodem z fizycznego panelu;
- porównanie i pinning SHA-256 certyfikatu P4 na Androidzie i iOS;
- token Bearer w systemowym secure storage;
- uniwersalne urządzenia i encje bez listy nazw zaszytej w aplikacji;
- obsługa `sensor`, `binary_sensor`, `switch`, `light`, `number`, `select` i
  `button`;
- pulpit stanu, alarmy krytyczne, grupowanie urządzeń, historia i diagnostyka;
- odświeżanie co 5 sekund z blokadą równoległych żądań;
- limit odpowiedzi 1 MiB, timeout 10 s i jawna klasyfikacja błędów;
- bezpośredni dostęp zdalny wyłącznie przez VPN.

Starszy klient oficjalnego Home Assistanta pozostaje w `lib/src/data`,
`lib/src/state` i `lib/src/ui` jako warstwa zgodności i dokumentacja migracji.
Punkt wejścia `lib/main.dart` uruchamia aplikację AquaHub.

## Uruchomienie

```powershell
cd home_assistant_app
flutter pub get
flutter analyze
flutter test
flutter run
```

Domyślny adres to `https://aquahub.local:8443`. Na panelu P4 należy otworzyć
ekran System, porównać pełny odcisk certyfikatu i wpisać aktualny kod parowania.
Zmiana klucza lub certyfikatu P4 powoduje celową odmowę połączenia i wymaga
ponownego parowania przy panelu.

## Platformy i TLS

Na Androidzie i iOS aplikacja oblicza SHA-256 z certyfikatu podczas handshake i
porównuje go z zapisanym odciskiem. Wersja webowa nie otrzymuje certyfikatu z API
przeglądarki, dlatego wymaga certyfikatu zaufanego przez system lub lokalnego
reverse proxy z prawidłowym TLS. Nie należy wyłączać weryfikacji w przeglądarce.
Jeżeli aplikacja webowa działa na innym originie niż API, w menu P4 trzeba
ustawić dokładny adres HTTPS w `AQUAHUB_CORS_ORIGIN`. Wartość `*` nie jest
obsługiwana.

Android ma wyłączony cleartext HTTP i kopie zapasowe danych aplikacji. iOS
deklaruje dostęp do sieci lokalnej oraz usługę Bonjour `_aquahub._tcp`.

## Testy

Testy obejmują modele, kontrakt API, paginację, komendy, onboarding AquaHub oraz
zachowaną warstwę kompatybilności Home Assistanta. Klient HTTP jest wstrzykiwany
w testach, a kod produkcyjny tworzy transport z pinningiem certyfikatu.

Pełny podział odpowiedzialności i procedura provisioningu znajdują się w
[`docs/AQUAHUB_ARCHITECTURE.md`](../docs/AQUAHUB_ARCHITECTURE.md).
