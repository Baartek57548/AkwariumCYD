# Podpisywanie firmware i bezpieczny provisioning

## Łańcuch zaufania

Produkcyjny tag firmware ma postać `firmware-vX.Y.Z`. Wartość `X.Y.Z` musi być
identyczna z `FirmwareInfo::VERSION` w `include/config.h`. Ten sam kontrakt
źródłowy definiuje:

- `SECURITY_VERSION` — monotoniczną wersję bezpieczeństwa;
- `BOOTLOADER_COMPATIBILITY_VERSION` — minimalną zgodną wersję bootloadera.

`SECURITY_VERSION` nie jest numerem każdego wydania. Należy zwiększyć ją dopiero,
gdy starszy, poprawnie podpisany firmware trzeba kryptograficznie wycofać. Nie
wolno jej zmniejszać ani ponownie używać po wycofaniu wersji.

Publiczny klucz RSA-3072 znajduje się w
`security/firmware-signing-public.pem`, a identyczny trust anchor jest kompilowany
z `include/firmware_trust_anchor.h`.

```text
keyId: 9470c281de5f898f
SHA-256 SubjectPublicKeyInfo DER:
9470c281de5f898fd387acd8b89c3f21fb435ed71bf7d638159b4b9339fc2bc5
```

Klucz publiczny nie jest sekretem. Prywatny klucz RSA-3072 nie może znaleźć się w
repozytorium, logach, artefaktach, cache ani na urządzeniu.

## Artefakty wydania

Dla każdego panelu workflow publikuje dwa powiązane artefakty:

```text
AquaCYD-Firmware-X.Y.Z-ili9341-sbv2.bin
AquaCYD-Firmware-X.Y.Z-ili9341.aqfw
AquaCYD-Firmware-X.Y.Z-st7789-sbv2.bin
AquaCYD-Firmware-X.Y.Z-st7789.aqfw
```

Plik `*-sbv2.bin` jest finalnym obrazem aplikacji podpisanym przez
`espsecure.py sign_data --version 2`. Nadaje się do kontrolowanego flashowania
serwisowego oraz do urządzeń z provisionowanym Secure Boot v2.

Plik `.aqfw` ma stały 512-bajtowy nagłówek i zawiera ten sam, niezmieniony obraz
Secure Boot v2. Podpisane metadane wiążą:

- produkt `aquacyd-cyd`;
- wariant `ili9341` albo `st7789`;
- wersję firmware i `securityVersion`;
- minimalną wersję bootloadera;
- 19-znakowy prefiks SHA commita (pełny SHA pozostaje w manifeście release);
- dokładny rozmiar i SHA-256 payloadu;
- `keyId`.

Podpis metadanych jest RSA-3072/PSS/SHA-256. Firmware ufa wyłącznie kluczowi
wbudowanemu; plik PEM dołączony do GitHub Release służy operatorowi do
niezależnej kontroli, a nie do ustanawiania zaufania.

Release zawiera również `SHA256SUMS`, `release-manifest.json`, jego podpis
`release-manifest.json.sig`, publiczny PEM oraz `SIGNING_KEY_SHA256`.

## Sekret GitHub

Environment `production-firmware` musi mieć wymaganych reviewerów, ograniczenie
do chronionych tagów `firmware-v*` oraz sekret:

```text
FIRMWARE_SIGNING_KEY_BASE64
```

Sekret jest kodowaniem Base64 kompletnego prywatnego klucza PEM RSA-3072
odpowiadającego zatwierdzonemu kluczowi publicznemu. Wartość należy przekazać do
GitHub bez wypisywania jej w terminalu lub historii powłoki. Workflow:

1. materializuje klucz w katalogu tymczasowym z uprawnieniami `0600`;
2. sprawdza typ i długość RSA;
3. wyprowadza klucz publiczny i porównuje jego DER bajt w bajt z plikiem w repo;
4. odrzuca niedopasowany sekret przed podpisaniem;
5. usuwa plik prywatny w kroku wykonywanym także po błędzie.

Preferowanym kolejnym etapem jest zdalne podpisywanie przez HSM/KMS z GitHub OIDC,
aby prywatny klucz nie był materializowany na runnerze.

## Przebieg workflow

Zadanie firmware w `.github/workflows/release.yml`:

1. sprawdza tag względem `FirmwareInfo`;
2. uruchamia self-test formatu `.aqfw`, audytu eFuse i testy natywne;
3. odtwarza assety WWW oraz buduje oba profile PlatformIO;
4. podpisuje każdy finalny payload przez `espsecure` Secure Boot v2;
5. natychmiast weryfikuje blok podpisu kluczem publicznym;
6. opakowuje payload w `.aqfw`;
7. weryfikuje podpis, target, wersję, `securityVersion`, rozmiar i SHA-256;
8. ponownie wyciąga payload z `.aqfw`, porównuje go bajt w bajt i sprawdza
   podpis Secure Boot v2;
9. generuje i podpisuje manifest;
10. odmawia nadpisania istniejącego wydania i publikuje komplet artefaktów.

Workflow nie wykonuje żadnej operacji na fizycznym ESP32 i nigdy nie zapisuje
eFuse.

## Walidacja lokalna bez klucza prywatnego

Poniższe polecenia są bezpieczne i nie modyfikują urządzenia:

```powershell
python scripts/validate_release.py --self-test
python scripts/validate_release.py `
  --tag firmware-v5.1.0 `
  --firmware-config include/config.h `
  --dry-run
python scripts/verify_firmware_trust.py
python tools/firmware_package.py self-test
python scripts/audit_esp32_security.py --self-test
```

Weryfikacja pobranego pakietu:

```powershell
python tools/firmware_package.py inspect `
  --package AquaCYD-Firmware-5.1.0-ili9341.aqfw `
  --public-key security/firmware-signing-public.pem `
  --target ili9341 `
  --version 5.1.0 `
  --security-version 1
```

Finalny `*-sbv2.bin` należy dodatkowo zweryfikować publicznym kluczem za pomocą
`espsecure.py verify_signature --version 2`.

## Odczytowy audyt płytki

Przed kwalifikacją urządzenia należy wykonać:

```powershell
python scripts/audit_esp32_security.py `
  --port COM5 `
  --output artifacts/device-security-audit.json
```

Skrypt wywołuje tylko `esptool chip_id` i `espefuse summary`. Raport wyjściowy
jest sanitizowany: nie zapisuje MAC, surowych bloków kluczy ani pełnego dumpu
eFuse. Opcja `--require-secure` zwraca błąd, jeśli urządzenie nie spełnia bramki
produkcyjnej, ale nadal niczego nie zapisuje.

Secure Boot v2 klasycznego ESP32 wymaga rewizji ECO3 lub nowszej. Płytkę o
starszej rewizji należy odrzucić z profilu Secure Boot v2, a nie próbować
provisionować.

## Provisioning — obowiązkowe bramki bezpieczeństwa

Przepalenie eFuse jest nieodwracalne, dlatego nie ma automatycznego skryptu
provisionującego. Procedura fabryczna wymaga oddzielnego, zatwierdzonego narzędzia
stanowiskowego i fizycznego potwierdzenia operatora.

Kolejność kwalifikacji:

1. użyć najpierw płytki przeznaczonej do testów destrukcyjnych;
2. zapisać odczytowy raport rewizji i stanu eFuse;
3. sprawdzić stabilne zasilanie oraz zgodność rozmiaru flash;
4. wgrać przewodowo podpisany bootloader, tablicę partycji i dwa sprawne obrazy
   A/B;
5. uruchomić test wyjść, watchdogów, OTA, zaniku zasilania oraz rollbacku;
6. najpierw kwalifikować weryfikację podpisanej aplikacji bez hardware Secure
   Boot;
7. Secure Boot v2 uruchomić na osobnej serii pilotażowej;
8. Flash Encryption sprawdzić najpierw w Development Mode;
9. Release Mode, blokadę UART i JTAG dopuścić dopiero po zaliczeniu procedury
   odzyskania/RMA;
10. po każdej nieodwracalnej zmianie ponownie odczytać eFuse i wykonać pełny HIL.

Nie wolno przeprowadzać migracji istniejącej floty do Secure Boot/Flash
Encryption przez zwykłe OTA. Zmiana bootloadera, tablicy partycji i eFuse wymaga
kontrolowanego stanowiska serwisowego.

## Flash Encryption i odzyskiwanie

Flash Encryption powinno używać unikalnego klucza dla każdego urządzenia. Tryb
Release ogranicza możliwość ponownego flashowania przez UART. Jeśli wyłączony
zostanie ROM Download Mode, a oba sloty A/B utracą poprawny obraz, naprawa
programowa może nie być możliwa.

Przed produkcją trzeba jawnie wybrać politykę:

- maksymalna ochrona: klucz generowany na urządzeniu, UART wyłączony, uszkodzoną
  płytkę wymienia się w RMA;
- kontrolowane odzyskanie: indywidualny klucz escrow i pozostawiona, ściśle
  chroniona ścieżka serwisowa.

Nie wolno używać jednego klucza Flash Encryption dla całej floty.
