# CI/CD i wydania produkcyjne

## Automatyczna weryfikacja

Workflow `.github/workflows/ci.yml` uruchamia cztery niezależne zadania:

1. self-test narzędzi wydaniowych i HIL;
2. testy Node API, składnię JavaScript, zgodność assetów gzip i Playwright;
3. testy natywnej logiki PlatformIO oraz build ILI9341 i ST7789;
4. `flutter analyze`, testy Flutter, testy Android/JVM i build wariantu `current`.

Self-test HIL obejmuje rzeczywiste kontrakty v2 auth/action/capabilities,
idempotencję, timeout override oraz dwie niezależne lampy Aquael `front` i
`rear`. Bez urządzenia każdy test sprzętowy jest raportowany jako `SKIP`.

CI tworzy jednorazowy klucz Androida tylko po to, aby Gradle mógł skompilować
wariant release. Następnie przepakowuje wynik bez podpisu i sprawdza, że artefakt
`android-unsigned-validation` nie ma prawidłowej sygnatury. Nie wolno go
instalować ani publikować użytkownikom. Artefakty web/firmware służą do analizy
builda i są przechowywane przez 14 dni.

Wrapper Gradle jest obecnie ignorowany przez projekt mobilny. Workflow odtwarza
wersję `8.14` na runnerze przed buildem; nie pobiera ani nie zapisuje sekretów do
repozytorium.

## Ochrona repozytorium

Gałąź główna powinna wymagać:

- PR z co najmniej jednym zatwierdzeniem;
- zaliczonych zadań `Release and HIL tooling`, `Web and Node`,
  `PlatformIO firmware` oraz `Flutter and Android`;
- zablokowanego force-push i usuwania gałęzi;
- rozwiązania wszystkich komentarzy;
- aktualnej gałęzi względem `main`;
- ograniczenia `GITHUB_TOKEN` do uprawnień zadeklarowanych w workflow.

Aktualizacje zależności i akcji wymagają osobnego PR. Lockfile npm jest
obowiązkowy. Dla maksymalnej ochrony organizacja powinna przypiąć akcje do
zatwierdzonych SHA w polityce GitHub Actions.

## Sekrety i środowiska

W chronionym środowisku wydania mobilnego:

- `ANDROID_KEYSTORE_BASE64`;
- `ANDROID_KEY_ALIAS`;
- `ANDROID_KEY_PASSWORD`;
- `ANDROID_STORE_PASSWORD`.

W chronionym środowisku firmware:

- `FIRMWARE_SIGNING_KEY_BASE64` — prywatny klucz Ed25519 PEM zakodowany Base64.

W chronionym środowisku `aquacyd-hil`:

- `AQUACYD_HIL_BASE_URL`;
- `AQUACYD_HIL_ADMIN_PIN`;
- opcjonalne adresy usług stanowiska opisane w
  [PRODUCTION_HIL.md](PRODUCTION_HIL.md).

Sekrety należy wprowadzić w GitHub Environments, włączyć wymaganych reviewerów i
ograniczyć deployment do tagów. Workflow nigdy nie publikuje klucza prywatnego.
Po buildzie usuwa materializowane pliki również przy błędzie.

## Wydanie aplikacji

1. Zwiększ `version: X.Y.Z+N` w `mobile_app/pubspec.yaml`.
2. Upewnij się, że cały CI jest zielony.
3. Utwórz podpisany lub chroniony tag `mobile-vX.Y.Z` na zweryfikowanym commicie.
4. Workflow sprawdzi tag względem pubspec, wykona pełne testy i zbuduje APK.
5. `aapt` potwierdzi package, `versionName` i `versionCode`; `apksigner`
   potwierdzi certyfikat i jego zapisany publiczny fingerprint SHA-256.
6. Publikowane są APK, `SHA256SUMS` i `release-manifest.json`.

Nazwa APK musi być dokładnie
`AquaCYD-Control-X.Y.Z-current.apk`. Istniejące wydanie nie jest nadpisywane.
Zmiana gotowego pliku wymaga nowego numeru wersji i tagu.

## Wydanie firmware

1. Zakończ test na obu profilach ekranu oraz na stanowisku HIL.
2. Utwórz tag `firmware-vX.Y.Z`.
3. Workflow ponownie wykonuje testy natywne, sprawdza wygenerowane assety WWW i
   kompiluje oba buildy produkcyjne.
4. Nazwy binariów i sumy SHA-256 sprawdza `scripts/validate_release.py`.
5. Oba obrazy i manifest są podpisywane Ed25519, a sygnatury są natychmiast
   weryfikowane kluczem publicznym.
6. Release zawiera `.bin`, `.sig`, manifest, `SHA256SUMS`, publiczny klucz
   kontrolny oraz fingerprint klucza.

Docelowo firmware urządzenia musi ufać wcześniej wbudowanemu kluczowi, a nie
kluczowi pobranemu obok pliku release. Publiczny plik w release służy operatorowi
do porównania fingerprintu.

Aktualny firmware nie ma jeszcze provisionowanego klucza i nie weryfikuje
Ed25519 na urządzeniu. Publikowane sygnatury umożliwiają niezależną weryfikację
i audyt artefaktu, ale nie upoważniają do automatycznego zdalnego OTA. Do czasu
wdrożenia Secure Boot v2 obraz wolno wgrywać tylko z zaufanej sieci po ręcznym
sprawdzeniu SHA-256; szczegóły opisuje
[PRODUCTION_SECURITY.md](PRODUCTION_SECURITY.md).

## Walidacja lokalna

```powershell
python scripts/validate_release.py --self-test
python tools/hil/runner.py --self-test
python tools/hil/runner.py --dry-run
actionlint
npm ci
npm run test:api
pio test --environment native
```

Dry-run nie tworzy plików i nie wykonuje żądań sieciowych.
