# Audyt stabilności i UI/UX — Home Control 2.0.1

Data audytu: 2026-08-13. Gałąź: `codex/home-control-monorepo`.

## Wynik

Audyt objął natywną aplikację Flutter, źródła AquaHub i Home Assistant, cache
offline, przełączanie profili, OTA aplikacji oraz układy od telefonu 320×568 do
panelu 800×480 i desktopu. Usunięto wykryte defekty o wysokim priorytecie, a
pakiet jest gotowy do następnej wersji poprawkowej po fizycznym teście na
docelowym panelu i podpisaniu APK kluczem właściciela.

## Usunięte defekty

- przełączanie profili i źródeł jest wersjonowane i serializowane, dzięki czemu
  spóźniona odpowiedź, zapis poświadczeń albo zamknięcie transportu nie może
  zastąpić nowszej sesji;
- usunięcie aktywnego profilu rzeczywiście przełącza transport, a nie tylko
  zaznaczenie w interfejsie;
- snapshoty offline są rozdzielone per profil i po odzyskaniu sieci aplikacja
  odtwarza pełne połączenie REST/WebSocket zamiast pozostawać w samym pollingu;
- cache większy niż limit secure storage jest redukowany w kontrolowany sposób,
  bez trwałej pętli błędów zapisu;
- instalacja APK wymaga biometrii zarówno z dialogu startowego, jak i z ekranu
  Aktualizacje; wynik autoryzacji jest ponownie wiązany z bieżącym źródłem;
- niezapisana lub odrzucona komenda encji wykonuje rollback i pokazuje błąd
  wewnątrz arkusza szczegółów;
- encje `unknown`, `unavailable`, offline i pending nie tworzą aktywnych
  kontrolek ani nie powodują asercji list rozwijanych;
- formularz Home Assistant zachowuje adres, token, nazwę i tryb po błędzie
  połączenia oraz pokazuje walidację właściwego pola;
- wyeliminowano zapętlenie identycznych ekranów szczegółów akwarium;
- ustawienie dużej karty ma widoczny efekt również w pełnym shellu panelu
  800×480;
- mobilna nawigacja ma trzy główne pozycje i arkusz „Więcej”, dzięki czemu nie
  ściska sześciu etykiet na małym ekranie;
- naprawiono czyszczenie wyszukiwania, pusty stan ulubionych, liczenie aktywnych
  encji, oś czasu historii oraz przepełnienia długich nazw i wartości;
- ograniczono lawinowe przebudowy interfejsu przy seriach zdarzeń WebSocket;
- konfiguracja AquaHub, w tym błędy pierwszego połączenia, używa wybranego języka
  polskiego albo angielskiego;
- lokalny HTTP Home Assistant jest dozwolony wyłącznie dla adresów prywatnych,
  loopback i link-local; klient nie podąża za przekierowaniem z tokenem do
  publicznego hosta.

## Decyzje UX

Panel 800×480 korzysta z zwartego `NavigationRail`, dwóch kolumn kart i pełnej
szerokości tylko dla sekcji oznaczonych jako duże. Telefon przechodzi na dolną
nawigację, a desktop rozszerza rail dopiero przy szerokości zapewniającej miejsce
treści. Te same ekrany zachowują działanie przy powiększeniu tekstu i nie
uzależniają logiki domenowej od konkretnego rozmiaru wyświetlacza.

Interfejs pozostaje natywny: nie używa WebView ani strony Home Assistant. Widżety
operują na jednolitym `HomeDataSource`, natomiast transport, cache, autoryzacja i
ponawianie połączeń są poza warstwą widoku.

## Brama jakości

Po audycie obowiązują następujące bramy:

- `dart format --output=none --set-exit-if-changed lib test`;
- `flutter analyze --no-pub`;
- pełny `flutter test --no-pub`, w tym regresje wyścigów, cache, biometrii,
  lokalizacji i macierzy responsywnej;
- `flutter build web --release --no-pub`;
- `flutter build apk --debug --no-pub`;
- `flutter build apk --release --no-pub`.

Pełny pakiet zawiera 80 zaliczonych testów, a pokrycie po audycie wynosi 58,57%
całej aplikacji. Testy hostowe nie zastępują odbioru HIL: przed
produkcyjną publikacją trzeba sprawdzić dotyk, skalowanie, obrót, utratę Wi‑Fi,
biometrię i instalator OTA na rzeczywistym urządzeniu 800×480.

## Świadome ograniczenia

- logowanie OAuth Home Assistant wymaga Client ID i redirect URI właściciela;
  token długoterminowy pozostaje zabezpieczoną opcją zaawansowaną;
- release APK musi zostać podpisany tym samym produkcyjnym certyfikatem co
  poprzednia wersja, inaczej Android prawidłowo odrzuci aktualizację;
- testy automatyczne nie potwierdzają jakości fizycznego digitizera, radia Wi‑Fi
  ani zachowania konkretnego launchera panelu ściennego.
