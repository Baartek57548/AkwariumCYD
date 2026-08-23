# Zdalna bramka alarmowa i opcjonalny push

Standardowy APK nie wymaga Firebase i nie zawiera `google-services.json`.
Powiadomienia lokalne, alarmy z bieżącego połączenia oraz okresowa kontrola
lokalnego Wi‑Fi działają niezależnie od bramki.

## Kontrakt bramki

Użytkownik podaje w aplikacji:

- bazowy adres HTTPS bez query i fragmentu;
- `deviceId` złożony z liter, cyfr, `_` lub `-`;
- bearer/viewer token.

Adres i identyfikator nie są sekretami i trafiają do preferencji aplikacji.
Viewer token jest przechowywany wyłącznie przez `flutter_secure_storage`.
Test połączenia wykonuje:

```text
GET {baseUrl}/api/v1/devices/{deviceId}/health
Authorization: Bearer {viewerToken}
Accept: application/json
```

Odpowiedź jest uznawana za prawidłową wyłącznie wtedy, gdy ma postać
`{"status":"ok","deviceId":"<dokładnie żądany identyfikator>"}`. Sam kod
HTTP 2xx nie wystarcza.

Rejestracja tokenu FCM używa:

```text
POST   {baseUrl}/api/v1/devices/{deviceId}/push-tokens
DELETE {baseUrl}/api/v1/devices/{deviceId}/push-tokens
Authorization: Bearer {viewerToken}
Content-Type: application/json

{"token":"platform-token","platform":"android"}
```

Bramka nie może przekierowywać żądań zawierających bearer token.

## Opcjonalne parametry Firebase

Aplikacja zawiera produkcyjny adapter `firebase_core` i
`firebase_messaging`, ale uruchamia go dopiero przy komplecie parametrów runtime
oraz włączonej konfiguracji bramki. Parametry należy przekazać podczas
budowania, bez plików konfiguracyjnych i bez sekretów w repozytorium:

```powershell
flutter build apk --release --flavor current --target lib/main.dart `
  --dart-define=AQUACYD_FIREBASE_API_KEY=... `
  --dart-define=AQUACYD_FIREBASE_APP_ID=... `
  --dart-define=AQUACYD_FIREBASE_MESSAGING_SENDER_ID=... `
  --dart-define=AQUACYD_FIREBASE_PROJECT_ID=...
```

Po zapisaniu aktywnej bramki aplikacja pobiera token FCM, rejestruje go przez
HTTPS, obsługuje jego odświeżenie oraz usuwa rejestrację przed skasowaniem
tokenu viewer. Wiadomości foreground są przekładane na lokalne kanały
powiadomień, wiadomości data-only w tle obsługuje top-level background handler,
a tapnięcie w wiadomość prowadzi do właściwego ekranu również po zimnym
starcie.

Brak choćby jednego `dart-define` jest wspieranym trybem: Firebase nie jest
inicjalizowany, nie jest pobierany token i cała aplikacja działa lokalnie.
`google-services.json` nie jest wymagany.

Produkcyjny workflow pobiera te same cztery wartości z GitHub Environment
Variables środowiska `production-mobile`:
`AQUACYD_FIREBASE_API_KEY`, `AQUACYD_FIREBASE_APP_ID`,
`AQUACYD_FIREBASE_MESSAGING_SENDER_ID` i
`AQUACYD_FIREBASE_PROJECT_ID`. Wszystkie puste oznaczają jawny build
`local-only`; wszystkie ustawione włączają Firebase. Częściowy zestaw lub
wartość zawierająca białe znaki przerywa wydanie, dzięki czemu opublikowany APK
nie może przypadkowo zawierać niespójnej konfiguracji push. Workflow zapisuje
wyłącznie wybrany tryb, nigdy wartości zmiennych.

Obsługiwane payloady `data`:

- alarm: `type=alarm`, `eventType`, `id`, `title`, `body`,
  `severity=info|warning|critical`, `state=raised|resolved`, `deviceId`,
  `occurredAt`;
- serwis: `type=service`, `id`, `title`, `body`;
- aktualizacja: `type=update`, `id`, `title`, `body`, `version`, `tag`.

Każdy payload jest walidowany przed utworzeniem systemowego powiadomienia.
Stan `resolved` anuluje aktywne powiadomienie danego alarmu i pokazuje
potwierdzenie rozwiązania tylko wtedy, gdy użytkownik włączył tę opcję.
Zweryfikowane zdarzenie jest także trwale zapisywane w Centrum alarmów; jego
stan zmienia wyłącznie jawne zdarzenie `resolved`, a nie brak wpisu w lokalnym
snapshotcie sterownika.
Bramka powinna wysyłać wiadomości data-only z wysokim priorytetem; dodanie
równoległego bloku `notification` może spowodować podwójne powiadomienie.

## Provisioning sekretu sterownika

Formularz w ustawieniach przyjmuje 32–64-bajtowy sekret HMAC w standardowym
Base64. Pole jest efemeryczne: nie trafia do `SharedPreferences`,
`flutter_secure_storage`, logów ani historii i jest czyszczone po każdej próbie.

Konfiguracja firmware jest wysyłana wyłącznie przez aktywne, szyfrowane BLE v2
po autoryzacji administratora:

```text
action: save_remote_gateway
payload: {baseUrl, deviceId, hmacSecret, enabled}

action: clear_remote_gateway
```

Provisioning przez Wi‑Fi, REST, WebSocket lub HTTP/HTTPS jest celowo
zablokowany. Token viewer aplikacji jest niezależnym sekretem i nadal pozostaje
wyłącznie w systemowym magazynie kluczy.
