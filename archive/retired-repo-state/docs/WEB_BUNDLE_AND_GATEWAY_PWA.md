# Pakiet WWW, instalacja SD i companion PWA

## Zakres

Pełny panel WWW pozostaje lekkim zestawem statycznych plików serwowanych z
`/aq/ota` na karcie SD. Produkcyjne wydanie firmware publikuje dodatkowo:

- `AquaCYD-Web-X.Y.Z.tar.gz` — deterministyczne archiwum z plikami panelu i ich
  wariantami gzip;
- `AquaCYD-Web-X.Y.Z.tar.gz.sha256` — skrót samego archiwum;
- `AquaCYD-Web-X.Y.Z.manifest.json` — podpisany kontrakt wersji, commita,
  docelowej ścieżki i skrótów każdego pliku;
- `AquaCYD-Web-X.Y.Z.manifest.json.sig` — podpis RSA-PSS/SHA-256 tego manifestu;
- `AquaCYD-Web-X.Y.Z.cdx.json` — CycloneDX SBOM zależności panelu.

Manifest WWW jest podpisywany tym samym chronionym kluczem wydawcy, który
podpisuje produkcyjne pakiety firmware. Archiwum nie zawiera `firmware.bin`,
konfiguracji, historii, danych diagnostycznych ani sekretów.

## Budowanie i weryfikacja

Lista plików i limity pamięci znajdują się w `tools/web-assets.json`. Jeden
kontrakt jest używany przez generator assetów, test budżetu i pakowarkę, dzięki
czemu nie można przypadkowo opublikować nieprzetestowanego pliku.

```powershell
npm ci
npm run build:web-assets
python tools/web_package.py --self-test
python tools/install_web_bundle.py --self-test
```

Produkcja pakietu wymaga klucza prywatnego i odpowiadającego mu publicznego PEM.
Tryb `--unsigned-for-testing` istnieje wyłącznie dla lokalnej diagnostyki i nie
jest używany przez workflow wydania.

## Atomowa instalacja na karcie SD

Najpierw zamontuj kartę i upewnij się, że wskazany katalog zawiera już podkatalog
`aq`. Następnie uruchom:

```powershell
python tools/install_web_bundle.py install `
  --archive AquaCYD-Web-X.Y.Z.tar.gz `
  --manifest AquaCYD-Web-X.Y.Z.manifest.json `
  --signature AquaCYD-Web-X.Y.Z.manifest.json.sig `
  --public-key firmware-signing-public.pem `
  --sd-root E:\
```

Instalator przed zapisem:

1. weryfikuje RSA-PSS podpisanego manifestu;
2. sprawdza nazwę, rozmiar i SHA-256 archiwum;
3. odrzuca ścieżki absolutne, `..`, dowiązania, duplikaty oraz przekroczenia
   limitów;
4. porównuje rozmiar i SHA-256 każdego rozpakowanego pliku;
5. przenosi dotychczasowy `/aq/ota` do `/aq/ota.rollback`;
6. atomowo podstawia kompletny katalog staging jako nowy `/aq/ota`.

Jeżeli ostatnia zamiana nie powiedzie się, poprzedni katalog jest natychmiast
przywracany. Jawny rollback wykonuje:

```powershell
python tools/install_web_bundle.py rollback --sd-root E:\
```

Nie wskazuj korzenia dysku systemowego ani katalogu bez struktury `aq`.

## Companion PWA wyłącznie przez bramę HTTPS

`manifest.webmanifest`, `service-worker.js` i `gateway-pwa.js` tworzą opcjonalny
shell PWA. Sam protokół HTTPS nie wystarcza do aktywacji. Host musi działać w
secure context oraz jawnie potwierdzić tryb bramy odpowiedzią
`application/json`, `Cache-Control: no-store` pod adresem
`/.well-known/aquacyd-gateway-pwa.json`:

```json
{
  "schemaVersion": 1,
  "productId": "aquacyd-https-gateway",
  "mode": "read-only-no-command-queue"
}
```

Dopiero po poprawnej odpowiedzi skrypt dołącza manifest i rejestruje Service
Workera. Bezpośredni panel ESP32 nie udostępnia tego znacznika, dlatego PWA nie
aktywuje się ani przez HTTP, ani po ewentualnym dodaniu HTTPS do firmware.
Udane potwierdzenie zapisuje w tym samym originie wyłącznie niesekretny znacznik
trybu. Pozwala on uruchomić wcześniej zainstalowany shell bez sieci; każda
osiągalna odpowiedź błędna, nie-JSON lub niezgodna usuwa znacznik. Pierwsza
instalacja nadal bezwzględnie wymaga poprawnej odpowiedzi bramy online.

Manifest zawiera deterministyczne, nieprzezroczyste PNG `192x192` i `512x512`
oraz osobny wpis `purpose: maskable`; po opt-in dodawany jest również
`apple-touch-icon`. Spełnia to kryterium ikon Chromium i zachowuje raster PNG
oczekiwany przez Web Clip na urządzeniach Apple. Ikony generuje lokalnie
`tools/render-pwa-icons.js` bez zewnętrznego fontu, CDN ani danych czasu, a test
sprawdza sygnaturę PNG, wymiary i wpis maskable.
Podstawą są kryteria [Chrome for Developers](https://developer.chrome.com/docs/lighthouse/pwa/installable-manifest)
oraz wskazówki [Apple Web Clip](https://developer.apple.com/library/archive/documentation/AppleApplications/Reference/SafariWebContent/ConfiguringWebApplications/ConfiguringWebApplications.html).

Bramka musi:

- terminować TLS i wystawiać panel oraz kontrolowane proxy API pod tym samym
  originem;
- wystawiać powyższy znacznik tylko na hoście skonfigurowanym jako companion
  PWA; znacznik i `/.well-known/*` nigdy nie mogą być cache'owane;
- nigdy nie przekazywać dowolnej ścieżki do ESP32;
- zwracać dla `service-worker.js` `Cache-Control: no-cache` lub `no-store`;
- zachować CSP i nie wstrzykiwać skryptów;
- stosować osobny, ograniczony token bramy i limity żądań.

Service Worker zapisuje wyłącznie zamkniętą listę plików statycznego shellu.
`/.well-known/*`, `/api/*`, `/update`, `/settime`, `/download` i historia
pozostają network-only. IndexedDB otrzymuje ograniczony snapshot telemetrii bez
sieci, tokenów i konfiguracji tajnej.
Snapshot jest oznaczony jako archiwalny, a warstwa komend odrzuca każde
sterowanie kodem `offline_read_only`. Żadne polecenie nie jest kolejkowane do
późniejszego wykonania.

Przed zapisem każda dozwolona sekcja jest ponownie kopiowana rekurencyjnie.
Klucze związane z hasłem, PIN-em, tokenem, sekretem, credential, sesją,
autoryzacją lub HMAC są odrzucane na dowolnej głębokości. Sanitizer odrzuca
niestandardowe prototypy i wartości niefinitywne oraz ogranicza głębokość,
liczbę kluczy, elementów tablic, węzłów, długość tekstu i finalny rozmiar
snapshotu. Odczyt starszego rekordu z IndexedDB przechodzi przez ten sam filtr,
więc rozszerzenie payloadu firmware nie może przypadkowo ujawnić sekretu.
