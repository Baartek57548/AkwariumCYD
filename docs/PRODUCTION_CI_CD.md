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
2. Zwiększ `FirmwareInfo::VERSION` w `include/config.h`. Jeśli wydanie wycofuje
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
pio test --environment native
```

Self-test i dry-run nie zapisują eFuse ani nie wysyłają żądań do urządzenia.
Odczyt stanu konkretnego ESP32 wykonuje
`python scripts/audit_esp32_security.py --port COMx`; skrypt nie zawiera żadnej
operacji zapisu lub przepalania eFuse.
