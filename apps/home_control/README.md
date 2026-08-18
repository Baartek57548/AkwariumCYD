# Home Control

Home Control jest główną, natywną aplikacją Flutter do obsługi całego domu.
Nie osadza panelu WWW ani interfejsu Home Assistant w WebView. Te same ekrany
Material 3 pracują z trzema wymiennymi źródłami danych: AquaHub, jedną lub wieloma
instancjami Home Assistant oraz deterministycznym Demo offline.

Wewnętrzna nazwa pakietu Dart `aquacyd_home` i istniejący identyfikator aplikacji
zostały zachowane, aby nie zerwać ścieżki aktualizacji już zainstalowanych buildów.
Nazwa produktu, ikony, splash i interfejs użytkownika to **Home Control**.

## Zakres produktu

- onboarding z wyborem AquaHub, Home Assistant albo Demo;
- natywne wykrywanie `_aquahub._tcp`, ręczny adres awaryjny, HTTPS, pinning
  SHA-256 certyfikatu, sześciocyfrowe parowanie i token w secure storage;
- profile wielu instancji Home Assistant, REST, WebSocket, rejestry obszarów,
  urządzeń, encji i usług, historia oraz statystyki Recorder;
- 28 domen encji Home Assistant i bezpieczny widok nieznanego przyszłego typu;
- pulpit domu, ulubione, obszary, wyszukiwanie, filtry, szybkie akcje, alarmy,
  automatyzacje, sceny, skrypty, historię, aktualizacje i diagnostykę źródeł;
- katalog pomieszczeń z kartami kondycji, kluczowymi metrykami i osobnym,
  responsywnym widokiem urządzeń każdego pokoju;
- moduł Akwarium wykorzystujący faktyczne możliwości CYD/P4 bez przejmowania
  autonomicznej automatyki, interlocków ani operacji kalibracyjnych sterownika;
- edytowalny układ dashboardu, motyw jasny/ciemny/systemowy, język polski i
  angielski oraz responsywną nawigację telefonu, tabletu i desktopu;
- warstwę wizualną „calm intelligence”: jednoznaczny stan domu, centrum spraw
  wymagających reakcji, moduły domenowe bez fałszywych komunikatów, semantyczne
  kolory o zweryfikowanym kontraście oraz duże cele dotykowe dla panelu 800×480;
- wspólny design system i komponenty kart, statusów, przełączników, suwaków,
  dialogów, paneli oraz stanów loading/empty/error używane przez wszystkie ekrany;
- sterowanie bez przedwczesnej zmiany stanu: interfejs pokazuje `changing`, a
  wartość ON/OFF aktualizuje dopiero po potwierdzeniu źródła;
- wersjonowany cache, stale data, reconnect z backoffem, zatrzymanie pollingu po
  ukryciu aplikacji i natychmiastowe odświeżenie po wznowieniu; na Windows
  widoczne okno bez fokusu pozostaje aktywne;
- opcjonalne systemowe potwierdzenie tożsamości dla zamków, alarmów, bram,
  ryzykownych wartości i instalacji aktualizacji, w tym Windows Hello;
- automatyczną kontrolę dostępności aktualizacji aplikacji przy starcie oraz
  każdym wznowieniu.

UI komunikuje się wyłącznie z `HomeDataSource`. Transport HTTP, WebSocket,
discovery, secure storage i cache pozostają poza widżetami. Identyfikatory encji
są namespacowane źródłem, więc podobne nazwy z AquaHub i Home Assistant nigdy nie
są automatycznie scalane.

## Uruchomienie

Wymagane jest Flutter SDK zgodne z Dart `^3.11.3`.

```powershell
cd apps/home_control
flutter pub get
flutter analyze
flutter test
flutter run
```

Przy pierwszym uruchomieniu wybierz **Wypróbuj Demo**. Tryb Demo nie wymaga sieci,
konta ani sekretów, korzysta z produkcyjnego modelu domenowego i zawiera pokoje,
akwarium, alarm, historię, aktualizację, encję offline oraz nieznany typ encji.
Dane Demo nie są zapisywane jako sesja produkcyjna.

Buildy dostępne na Windows:

```powershell
flutter build web --release
flutter build apk --debug
flutter build apk --release
flutter build windows --release
../../scripts/build_home_control_windows.ps1
```

Ostatnie polecenie buduje `HomeControl.exe` wraz z pełnym bundle Flutter oraz
dwujęzyczny kreator
`artifacts/home-control-windows/Home-Control-X.Y.Z-Windows-x64-Setup.exe`.
Wymaga Windows 10 1903 lub nowszego, Flutter 3.41.5, Visual Studio z workloadem
Desktop development with C++ i ATL (produkcyjny runner: `windows-2025` z Visual
Studio 2026) oraz Inno Setup 6. Instalator działa per-user, obsługuje upgrade,
blokuje downgrade, dodaje deinstalator i opcjonalny skrót pulpitu oraz dołącza
VC++ Runtime tylko wtedy, gdy system go nie posiada.

Build iOS wymaga macOS z Xcode oraz tożsamości podpisującej właściciela.

## AquaHub i TLS

Na Androidzie i iOS aplikacja wykrywa AquaHub przez Bonjour/mDNS, a ręczny adres
HTTPS pozostaje opcją zaawansowaną. Pierwsze połączenie wymaga porównania pełnego
fingerprintu certyfikatu z fizycznym panelem i wpisania aktualnego kodu parowania.
Zmiana certyfikatu celowo blokuje połączenie do czasu jawnego ponownego parowania.

Przeglądarka nie udostępnia aplikacji certyfikatu serwera ani natywnego discovery.
Wersja webowa wymaga więc certyfikatu zaufanego przez system lub lokalnego reverse
proxy TLS oraz ręcznego adresu. Weryfikacja TLS nigdy nie jest wyłączana.

## Home Assistant

Instancję dodaje się przez lokalny lub zdalny adres HTTPS i token zapisany w
systemowym secure storage. Token długoterminowy jest oznaczony jako opcja
zaawansowana. Produkcyjny OAuth pozostaje zablokowany do czasu dostarczenia przez
właściciela publicznego Client ID oraz kontrolowanego redirect URI; aplikacja nie
udaje w tym miejscu gotowego logowania.

REST pobiera konfigurację, stany i historię oraz wywołuje usługi. WebSocket
subskrybuje zmiany stanu, pobiera rejestry i zagregowane statystyki. Jeżeli encja
nie ma statystyk długoterminowych albo serwer odrzuca ewoluujący kontrakt Recorder,
wykres bezpiecznie wraca do surowej historii REST.

## Aktualizacja Home Control

Na Androidzie aplikacja przy starcie i wznowieniu sprawdza najnowsze stabilne
wydanie `home-v*`. Dla nowszego `versionCode` pokazuje natywny dialog, pobiera APK
do prywatnego cache i przed uruchomieniem instalatora weryfikuje:

- dozwolony host i format manifestu wydania;
- nazwę pliku, deklarowany rozmiar oraz SHA-256;
- identyczny package ID;
- wyższy `versionCode`;
- identyczny certyfikat podpisujący jak w zainstalowanej aplikacji.

Włączona ochrona biometryczna blokuje pobranie/instalację do czasu potwierdzenia
tożsamości. Android nadal pokazuje obowiązkowe systemowe potwierdzenie, a pierwsza
instalacja spoza sklepu wymaga jednorazowej zgody. Po powrocie z ustawień proces
jest wznawiany. Błąd kanału aktualizacji nie blokuje pulpitu. Windows jest
aktualizowany przez pobranie nowszego Setup z tego samego wydania `home-v*`;
lokalny mechanizm APK pozostaje na nim wyłączony. iOS korzysta z App Store,
TestFlight albo zarządzanego MDM; web jest aktualizowany przez hosting.

Tokeny i sesje są przechowywane przez systemowy secure storage. Odtwarzalny
cache snapshotów na Windows jest celowo rozdzielony od sekretów i zapisywany
crash-safe jako zwykły tekst w nieroamingowym katalogu cache profilu użytkownika.
Może zawierać nazwy, topologię i ostatnie stany, ale nie zawiera tokenów ani
danych parowania. Zwykły uninstall zachowuje preferencje, poświadczenia i cache,
aby upgrade lub reinstalacja nie zerwały konfiguracji domu.

## Walidacja i bezpieczeństwo

Pełna brama lokalna obejmuje formatowanie, analizę, testy jednostkowe i widgetowe,
build web release oraz Android debug/release. Osobny runner Windows buduje EXE i
Setup oraz sprawdza cichą instalację, start aplikacji i deinstalację. CI
dodatkowo publikuje sumy SHA-256, SBOM, uruchamia skan sekretów i zależności oraz
waliduje pozostałe części monorepo.
Sekretów, PIN-ów, prywatnych adresów domu i kluczy podpisujących nie wolno dodawać
do repozytorium ani logów.

Regresje premium UI są sprawdzane dla telefonu 320×568 i panelu 800×480 przy
dwukrotnie powiększonym tekście. Jasny i ciemny motyw mają osobne scenariusze
wizualne dla telefonu 393×852 i panelu 800×480. Goldeny znajdują się w
`test/goldens/`; osobne testy weryfikują kontrast motywu,
live-region statusu, rozdzielenie semantyki szczegółów i szybkiej akcji oraz cele
dotykowe minimum 48 dp.

Aktualny rozwój produktu ma wersję `2.2.0+8`. Wyniki walidacji są w
[`docs/QA_REPORT.md`](../../docs/QA_REPORT.md), architektura w
[`docs/HOME_CONTROL_ARCHITECTURE.md`](../../docs/HOME_CONTROL_ARCHITECTURE.md),
model zagrożeń w [`docs/SECURITY.md`](../../docs/SECURITY.md), a procedura wydania
w [`docs/RELEASE.md`](../../docs/RELEASE.md). Szczegółowy audyt stabilności i
responsywnego UI/UX wersji 2.0.1 znajduje się w
[`docs/HOME_CONTROL_UI_UX_AUDIT_2_0_1.md`](../../docs/HOME_CONTROL_UI_UX_AUDIT_2_0_1.md).
