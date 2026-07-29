# AquaCYD Secure Gateway

Opcjonalna brama umożliwia sterownikowi wysyłanie alarmów na zewnątrz bez
otwierania portu HTTP ESP32. Lokalna aplikacja, BLE i panel sterownika działają
bez bramy.

## Model bezpieczeństwa

- ESP32 inicjuje wyłącznie wychodzące połączenie HTTPS.
- Każde zdarzenie ma podpis HMAC-SHA256, znacznik czasu i jednorazowy nonce.
- Sekret urządzenia oraz token aplikacji nie są zapisywane w repozytorium.
- Odczyt historii i rejestracja FCM wymagają osobnego tokenu tylko do odczytu.
- Brama ogranicza rozmiar żądania, tempo zdarzeń i odrzuca replay.
- Publicznie należy wystawić wyłącznie bramę przez TLS. Port ESP32 pozostaje w LAN.

## Konfiguracja

Generator tworzy dwa niezależne, kryptograficznie losowe sekrety:

```powershell
Set-Location gateway
npm run generate:secrets -- aquarium-main C:\secure\aquacyd-gateway.json
```

Generator nie nadpisuje istniejącego pliku. Wyświetlony token aplikacji należy
od razu zapisać w jej bezpiecznym magazynie. Plik zawiera wyłącznie jego hash.
Na Linuxie generator tworzy plik z prawami `0600`.

Generator wyświetla również dokładnie jeden raz 256-bitowy sekret HMAC
urządzenia w Base64. Ten sekret wolno przekazać do firmware wyłącznie podczas
lokalnego, efemerycznego provisioningu przez uwierzytelnione, sparowane i
szyfrowane połączenie BLE (bonded BLE). Po zakończeniu operacji bufor
provisioningu należy wyczyścić.

Sekret HMAC należy wyłącznie do sterownika i prywatnej konfiguracji bramy.
Aplikacja mobilna nie może go odczytywać ani zapisywać — przechowuje jedynie
oddzielny token użytkownika w bezpiecznym magazynie systemowym. Provisioning
sekretu HMAC przez HTTP lub HTTPS jest zabroniony, również w zaufanej sieci
LAN; nie wolno dodawać do tego celu endpointu REST, WebSocket ani MQTT.

Zmienne uruchomieniowe:

```text
AQUACYD_GATEWAY_SECRETS=/run/secrets/aquacyd-gateway.json
AQUACYD_GATEWAY_STATE=/var/lib/aquacyd-gateway
AQUACYD_GATEWAY_HOST=127.0.0.1
AQUACYD_GATEWAY_PORT=8787
AQUACYD_GATEWAY_ALLOWED_ORIGINS=https://panel.example.org
AQUACYD_FIREBASE_SERVICE_ACCOUNT=/run/secrets/firebase-service-account.json
```

Jeżeli TLS kończy się bezpośrednio w Node, ustaw także
`AQUACYD_GATEWAY_TLS_CERT` i `AQUACYD_GATEWAY_TLS_KEY`. Zalecana konfiguracja
produkcyjna to nasłuch na `127.0.0.1` za Caddy/Nginx z TLS 1.2+.

Plik konta Firebase jest opcjonalny. Bez niego brama zapisuje historię i
udostępnia health check, ale nie wysyła push.

## Kontrakt HTTP

Sterownik wysyła:

```text
POST /api/v1/devices/{deviceId}/events
X-AquaCYD-Timestamp: <Unix seconds>
X-AquaCYD-Nonce: <16-96 znaków base64url>
X-AquaCYD-Signature: v1=<hex HMAC-SHA256>
Content-Type: application/json
```

Łańcuch podpisu ma dokładnie postać:

```text
timestamp + "\n" + nonce + "\n" + method + "\n" + pathname + "\n" + sha256(body)
```

Zdarzenie zawiera `eventId`, `bootId`, `sequence`, `type`, `severity`,
`state`, `title`, `message` oraz opcjonalne `occurredAt` i `measurement`.

Aplikacja korzysta z:

```text
GET /api/v1/devices/{deviceId}/health
GET /api/v1/devices/{deviceId}/events?limit=50
POST /api/v1/devices/{deviceId}/push-tokens
DELETE /api/v1/devices/{deviceId}/push-tokens
Authorization: Bearer <viewerToken>
```

Opcjonalny host companion PWA może proxy'ować publiczny, pozbawiony sekretów
znacznik:

```text
GET /.well-known/aquacyd-gateway-pwa.json
Cache-Control: no-store
```

Znacznik tylko potwierdza tryb `read-only-no-command-queue`. Nie zastępuje
autoryzacji API i nie wolno go cache'ować. Pliki statyczne panelu oraz
kontrolowane proxy lokalnego API konfiguruje się oddzielnie zgodnie z
`docs/WEB_BUNDLE_AND_GATEWAY_PWA.md`; brama alarmowa nie otwiera portu ESP32.

## Uruchomienie i test

```powershell
Set-Location gateway
npm test
$env:AQUACYD_GATEWAY_SECRETS='C:\secure\aquacyd-gateway.json'
$env:AQUACYD_GATEWAY_STATE='C:\ProgramData\AquaCYD\gateway'
npm start
```

Historia jest zapisywana jako dzienne pliki JSONL. Tokeny FCM trafiają do
osobnych plików z ograniczonymi prawami i nigdy nie są zwracane przez API.
