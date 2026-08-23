# CI/CD i wydania produkcyjne

## Automatyczna weryfikacja

Workflow `.github/workflows/ci.yml` uruchamia pięć niezależnych zadań:

1. self-test narzędzi wydaniowych i HIL;
2. testy Node API i bramy, składnię JavaScript, zgodność assetów gzip oraz
   Playwright na Chromium, Firefox i WebKit;
3. testy natywnej logiki PlatformIO oraz build ILI9341 i ST7789;
4. `flutter analyze`, testy Flutter, testy Android/JVM i build wariantu `current`.
5. smoke test na emulatorze Android API 35: rzeczywiste kanały i dostarczenie
   powiadomienia, uprawnienie systemowe oraz start aplikacji z bezpiecznego
   payloadu powiadomienia; test nie emuluje ani nie wymaga sprzętu BLE.

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
  `PlatformIO firmware`, `Flutter and Android` oraz
  `Android notification smoke (API 35)`;
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

To samo środowisko może zawierać komplet czterech niesekretnych GitHub
Environment Variables używanych do opcjonalnego zdalnego push:

- `AQUACYD_FIREBASE_API_KEY`;
- `AQUACYD_FIREBASE_APP_ID`;
- `AQUACYD_FIREBASE_MESSAGING_SENDER_ID`;
- `AQUACYD_FIREBASE_PROJECT_ID`.

Workflow akceptuje wyłącznie dwa stany: brak wszystkich czterech zmiennych
oznacza jawny build `local-only`, natomiast komplet czterech jest przekazywany
do Fluttera jako `--dart-define` i włącza adapter Firebase. Konfiguracja częściowa
lub wartość zawierająca białe znaki zatrzymuje wydanie przed zbudowaniem APK.
Wybrany tryb jest zapisywany w podsumowaniu joba bez ujawniania wartości.

W chronionym środowisku firmware:

- `FIRMWARE_SIGNING_KEY_BASE64` — prywatny klucz RSA-3072 PEM zakodowany Base64,
  zgodny z publicznym kluczem `security/firmware-signing-public.pem`.

W chronionym środowisku `aquacyd-hil`:

- `AQUACYD_HIL_BASE_URL`;
- `AQUACYD_HIL_ADMIN_PIN`;
- opcjonalne adresy usług stanowiska opisane w
  [PRODUCTION_HIL.md](PRODUCTION_HIL.md).

Sekrety należy wprowadzić w GitHub Environments, włączyć wymaganych reviewerów i
ograniczyć deployment do tagów. Workflow nigdy nie publikuje klucza prywatnego.
Po buildzie usuwa materializowane pliki również przy błędzie.

## Wydanie aplikacji

1. Zwiększ `version: X.Y.Z+N` w `apps/aquacyd_service/pubspec.yaml`.
2. Upewnij się, że cały CI jest zielony.
3. Utwórz podpisany lub chroniony tag `mobile-vX.Y.Z` na zweryfikowanym commicie.
4. Workflow sprawdzi tag względem pubspec, zweryfikuje konfigurację
   `local-only`/Firebase, wykona pełne testy i zbuduje APK.
5. `aapt` potwierdzi package, `versionName` i `versionCode`; `apksigner`
   potwierdzi certyfikat i jego zapisany publiczny fingerprint SHA-256.
6. Publikowane są APK, `SHA256SUMS` i `release-manifest.json`.

Nazwa APK musi być dokładnie
`AquaCYD-Control-X.Y.Z-current.apk`. Istniejące wydanie nie jest nadpisywane.
Zmiana gotowego pliku wymaga nowego numeru wersji i tagu.

## Wydanie aplikacji Home Control

1. Zwiększ `version: X.Y.Z+N` w `apps/home_control/pubspec.yaml`.
2. Uruchom analizę, testy oraz kompilację web, Android i Windows.
3. Scal zweryfikowany commit do `main`, a następnie utwórz na nim tag
   `home-vX.Y.Z`.
4. Workflow Windows zbuduje pełny bundle `HomeControl.exe`, dwujęzyczny Setup i
   wykona cichą instalację, start oraz deinstalację; po jego sukcesie chroniony
   job Android zbuduje niezależny pakiet `pl.aquacyd.aquacyd_home` tym samym
   certyfikatem właściciela.
5. Walidator potwierdzi package, wersję, pojedynczego sygnatariusza i publiczny
   fingerprint certyfikatu.
6. Release publikuje `Home-Control-X.Y.Z.apk`,
   `Home-Control-X.Y.Z-Windows-x64-Setup.exe`, `SHA256SUMS`,
   `release-manifest.json`, wspólny CycloneDX SBOM oraz atestacje pochodzenia.

Tag musi wskazywać commit osiągalny z aktualnego `origin/main`.
Workflow odmawia nadpisania istniejącego wydania i nie ustawia Home Control jako
„Latest”, dzięki czemu głównym wydaniem pozostaje AquaCYD Control.

Windows Setup wymaga Windows 10 build 18362 lub nowszego i instaluje aplikację do
profilu bieżącego użytkownika. Jeżeli brakuje VC++ Runtime x64, kreator uruchamia
dołączony, podpisany redystrybutor Microsoft; ten krok może wyświetlić systemową
prośbę UAC o uprawnienia administratora. Do czasu
dostarczenia chronionego certyfikatu Authenticode Setup pozostaje jawnie
niepodpisany; SHA-256, manifest, SBOM i GitHub provenance są obowiązkowe.

## Wydanie firmware

1. Zakończ test na obu profilach ekranu oraz na stanowisku HIL.
2. Zwiększ `FirmwareInfo::VERSION` w `firmware/cyd_controller/include/config.h`. Jeśli wydanie wycofuje
   starszy, poprawnie podpisany obraz, zwiększ również
   `FirmwareInfo::SECURITY_VERSION`.
3. Po scaleniu zmian do `main` utwórz chroniony tag `firmware-vX.Y.Z` zgodny
   dokładnie z `FirmwareInfo::VERSION`. Workflow odrzuca tag, którego commit
   nie jest osiągalny z aktualnego `origin/main`.
4. Workflow ponownie wykonuje testy natywne, kontroluje trust anchor, sprawdza
   wygenerowane assety WWW i kompiluje oba buildy produkcyjne.
5. Każdy finalny obraz jest podpisywany przez `espsecure` jako Secure Boot v2,
   natychmiast weryfikowany kluczem publicznym i pakowany do `.aqfw`.
6. Narzędzie wydaniowe sprawdza podpis metadanych RSA-PSS/SHA-256, target,
   wersję, `securityVersion`, zgodność bootloadera, długość i SHA-256. Następnie
   workflow wyciąga payload z `.aqfw`, porównuje go bajt w bajt z podpisanym
   obrazem i ponownie weryfikuje podpis Secure Boot v2.
7. Manifest jest podpisywany tym samym chronionym kluczem. Release zawiera po
   jednym `*-sbv2.bin` i `.aqfw` dla ILI9341 oraz ST7789, `SHA256SUMS`,
   `release-manifest.json`, jego podpis, publiczny klucz kontrolny i fingerprint.

Firmware weryfikuje `.aqfw` wyłącznie względem trust anchora skompilowanego w
urządzeniu. Publiczny plik dołączony do release służy operatorowi do audytu i
nie może zastąpić wbudowanego klucza.

Weryfikacja `.aqfw` zapewnia ochronę OTA na poziomie aplikacji także przed
sprzętowym provisioningiem. Secure Boot v2 w ROM i Flash Encryption działają
jednak dopiero na płytkach bezpiecznie provisionowanych w fabryce; workflow
wydania nie zapisuje eFuse. Pełna procedura, fingerprint klucza i bramki
bezpieczeństwa są opisane w
[FIRMWARE_SIGNING_AND_PROVISIONING.md](FIRMWARE_SIGNING_AND_PROVISIONING.md).

## Walidacja lokalna

```powershell
python scripts/validate_release.py --self-test
python scripts/validate_release.py --tag firmware-v5.1.0 --print-firmware-contract
python scripts/verify_firmware_trust.py
python tools/firmware_package.py self-test
python scripts/audit_esp32_security.py --self-test
python tools/hil/runner.py --self-test
python tools/hil/runner.py --dry-run
actionlint
npm ci
npm run test:api
pio test --project-dir firmware/cyd_controller --environment native
```

Self-test i dry-run nie zapisują eFuse ani nie wysyłają żądań do urządzenia.
Odczyt stanu konkretnego ESP32 wykonuje
`python scripts/audit_esp32_security.py --port COMx`; skrypt nie zawiera żadnej
operacji zapisu lub przepalania eFuse.

## Bramka HIL dokładnego commita

Tag `firmware-vX.Y.Z` najpierw przechodzi job `firmware-source` na izolowanym
runnerze GitHub-hosted. Job rozwiązuje tag do pełnego SHA commita, sprawdza
kanoniczną wersję oraz wymaga, aby commit był osiągalny z `origin/main`.
Dopiero ten zatwierdzony SHA jest przekazywany do reusable workflow `hil.yml`.
Stanowisko sprawdza po checkout, że `git rev-parse HEAD` jest identyczny z
zatwierdzonym SHA. Dzięki temu self-hosted runner nigdy nie wykonuje kodu z
dowolnego taga przed kontrolą ancestry. Dopiero zaliczony fizyczny HIL
odblokowuje testy natywne, podpisanie i publikację. Raport `hil-report-<sha>`
jest przechowywany przez 90 dni i wiąże wyniki z pełnym commitem.

Wywołanie ręczne nadal jest możliwe, ale produkcyjne wydanie zawsze ustawia
`run_hardware`, kontrolowane mutacje i scenariusz rollbacku. Brak oznaczonego
runnera nie omija bramki — wydanie czeka albo kończy się błędem/timeoutem.

## SBOM, provenance i pakiet WWW

Wydanie mobilne publikuje CycloneDX SBOM zależności Dart obok APK. Wydanie
firmware publikuje osobne SBOM-y dla PlatformIO/firmware i npm/panelu WWW.
GitHub Artifact Attestations zapisuje SLSA build provenance oraz atestację SBOM
dla APK, obu wariantów firmware, pakietów `.aqfw` i archiwum WWW.

Weryfikację publicznego artefaktu można wykonać po zalogowaniu `gh`:

```powershell
gh attestation verify AquaCYD-Web-X.Y.Z.tar.gz `
  --repo Baartek57548/AkwariumCYD
gh attestation verify AquaCYD-Control-X.Y.Z-current.apk `
  --repo Baartek57548/AkwariumCYD
```

Pełny, podpisany i wersjonowany panel SD jest częścią release firmware. Procedurę
instalacji atomowej i rollbacku opisuje
[WEB_BUNDLE_AND_GATEWAY_PWA.md](WEB_BUNDLE_AND_GATEWAY_PWA.md).
