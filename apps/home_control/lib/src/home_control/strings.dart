import 'package:flutter/foundation.dart';
import 'package:flutter/widgets.dart';
import 'package:home_entities/home_entities.dart';
import 'package:intl/intl.dart';

final class HomeControlStrings {
  const HomeControlStrings(this.locale);

  final Locale locale;

  static HomeControlStrings of(BuildContext context) =>
      Localizations.of<HomeControlStrings>(context, HomeControlStrings) ??
      const HomeControlStrings(Locale('pl'));

  static const LocalizationsDelegate<HomeControlStrings> delegate =
      _HomeControlStringsDelegate();

  String t(String key) =>
      (_values[locale.languageCode] ?? _values['pl']!)[key] ?? key;

  String withValue(String key, Object value) =>
      t(key).replaceAll('{value}', '$value');

  String sourceName(HomeSourceKind kind) => switch (kind) {
    HomeSourceKind.aquaHub => t('aquaHub'),
    HomeSourceKind.homeAssistant => t('homeAssistant'),
    HomeSourceKind.demo => t('demo'),
  };

  String entityType(HomeEntityType type) => t('entity_${type.name}');

  String entityState(HomeEntity entity) {
    if (!entity.available) return t('unavailable');
    if (entity.state == null) return t('noData');
    if (entity.type == HomeEntityType.lock) {
      return entity.booleanValue == true ? t('unlocked') : t('locked');
    }
    if (entity.type == HomeEntityType.cover) {
      return entity.booleanValue == true ? t('opened') : t('closed');
    }
    if (entity.type == HomeEntityType.alarmControlPanel) {
      final key = 'alarm_${entity.state}';
      final translated = t(key);
      if (translated != key) return translated;
    }
    final boolean = entity.booleanValue;
    if (boolean != null &&
        (entity.type.supportsToggle ||
            entity.type == HomeEntityType.binarySensor ||
            entity.type == HomeEntityType.lock)) {
      return boolean ? t('stateOn') : t('stateOff');
    }
    final number = entity.numericValue;
    if (number != null &&
        <HomeEntityType>{
          HomeEntityType.sensor,
          HomeEntityType.number,
          HomeEntityType.inputNumber,
        }.contains(entity.type)) {
      final decimals = number == number.roundToDouble() ? 0 : 2;
      final text = NumberFormat.decimalPatternDigits(
        locale: locale.toLanguageTag(),
        decimalDigits: decimals,
      ).format(number);
      return entity.unit.isEmpty ? text : '$text ${entity.unit}';
    }
    return entity.state.toString().replaceAll('_', ' ');
  }

  String relativeTime(DateTime? value) {
    if (value == null) return t('never');
    final difference = DateTime.now().difference(value);
    if (difference.isNegative || difference.inSeconds < 10) return t('justNow');
    if (difference.inMinutes < 1) {
      return withValue('secondsAgo', difference.inSeconds);
    }
    if (difference.inHours < 1) {
      return withValue('minutesAgo', difference.inMinutes);
    }
    if (difference.inDays < 1) {
      return withValue('hoursAgo', difference.inHours);
    }
    return DateFormat.yMMMd(locale.toLanguageTag()).add_Hm().format(value);
  }

  static const Map<String, Map<String, String>>
  _values = <String, Map<String, String>>{
    'pl': <String, String>{
      'appName': 'Home Control',
      'appSubtitle': 'Twój dom. Jedno bezpieczne centrum.',
      'booting': 'Przygotowuję bezpieczne środowisko…',
      'connecting': 'Łączenie ze źródłem danych…',
      'sourceTitle': 'Wybierz źródło',
      'sourceDescription':
          'Home Control jest natywną aplikacją. Źródło dostarcza dane, ale nie definiuje interfejsu.',
      'aquaHub': 'AquaHub',
      'aquaHubDescription':
          'Lokalny panel ESP32-P4, urządzenia ESP-NOW i akwarium.',
      'homeAssistant': 'Home Assistant',
      'homeAssistantDescription':
          'Istniejąca instancja lokalna lub bezpieczny adres zdalny.',
      'demo': 'Demo offline',
      'demoDescription':
          'Pełny dom, akwarium, alarmy, historia i aktualizacje bez konta.',
      'recommended': 'Zalecane',
      'advanced': 'Zaawansowane',
      'back': 'Wstecz',
      'connectHa': 'Dodaj Home Assistant',
      'haUrl': 'Adres instancji',
      'haUrlHint': 'https://homeassistant.local:8123',
      'haProfileName': 'Nazwa profilu',
      'haProfileNameHint': 'Dom, biuro lub domek',
      'haToken': 'Długoterminowy token dostępu',
      'showToken': 'Pokaż token',
      'hideToken': 'Ukryj token',
      'testAndSave': 'Sprawdź i zapisz',
      'oauthHint':
          'To zaawansowane logowanie tokenem długoterminowym. OAuth wymaga publicznego Client ID i bezpiecznego redirect URI właściciela aplikacji; gotowa warstwa jest opisana w dokumentacji wydania.',
      'secureStorageHint':
          'Poświadczenia są przechowywane w bezpiecznym magazynie systemu. HTTP jest akceptowane wyłącznie w sieci lokalnej.',
      'dashboard': 'Pulpit',
      'rooms': 'Pomieszczenia',
      'devices': 'Urządzenia',
      'automations': 'Automatyzacje',
      'updates': 'Aktualizacje',
      'settings': 'Ustawienia',
      'navDashboard': 'Pulpit',
      'navRooms': 'Pokoje',
      'navDevices': 'Sprzęt',
      'navAutomations': 'Akcje',
      'navUpdates': 'OTA',
      'navSettings': 'Opcje',
      'home': 'Dom',
      'refresh': 'Odśwież',
      'source': 'Źródło',
      'connected': 'Połączono',
      'offline': 'Offline',
      'stale': 'Dane nieaktualne',
      'partial': 'Częściowa synchronizacja',
      'lastSync': 'Ostatnia synchronizacja: {value}',
      'favorites': 'Ulubione',
      'areasOverview': 'Pomieszczenia',
      'recentActivity': 'Ostatnia aktywność',
      'aquarium': 'Akwarium',
      'aquariumHealthy': 'Parametry stabilne',
      'aquariumAlarm': 'Wymaga uwagi',
      'waterTemperature': 'Temperatura wody',
      'waterChemistry': 'Chemia wody',
      'connection': 'Połączenie',
      'details': 'Szczegóły',
      'noAquarium': 'To źródło nie udostępnia jeszcze modułu akwarium.',
      'noFavorites': 'Dodaj ulubione z menu encji.',
      'noAreas': 'Źródło nie zwróciło pomieszczeń.',
      'noDevices': 'Nie znaleziono urządzeń.',
      'noAutomations': 'Brak automatyzacji w tym źródle.',
      'scenesAndScripts': 'Sceny i skrypty',
      'noUpdates': 'Brak dostępnych aktualizacji.',
      'entitiesCount': '{value} encji',
      'onlineDevices': '{value} urządzeń online',
      'available': 'Dostępne',
      'unavailable': 'Niedostępne',
      'unknown': 'Nieznane',
      'removed': 'Usunięte',
      'noData': 'Brak danych',
      'stateOn': 'Włączone',
      'stateOff': 'Wyłączone',
      'turnOn': 'Włącz',
      'turnOff': 'Wyłącz',
      'openCover': 'Otwórz',
      'closeCover': 'Zamknij',
      'opened': 'Otwarta',
      'closed': 'Zamknięta',
      'lockAction': 'Zablokuj',
      'unlockAction': 'Odblokuj',
      'locked': 'Zablokowany',
      'unlocked': 'Odblokowany',
      'alarmMode': 'Tryb alarmu',
      'alarm_disarmed': 'Rozbrojony',
      'alarm_armed_home': 'Czuwanie: dom',
      'alarm_armed_away': 'Czuwanie: poza domem',
      'alarm_armed_night': 'Czuwanie: noc',
      'start': 'Uruchom',
      'returnToBase': 'Wróć do bazy',
      'textValue': 'Wartość tekstowa',
      'run': 'Uruchom',
      'setValue': 'Ustaw wartość: {value}',
      'selectOption': 'Wybierz opcję',
      'history': 'Historia',
      'favorite': 'Ulubione',
      'removeFavorite': 'Usuń z ulubionych',
      'confirmTitle': 'Potwierdź operację',
      'confirmRoutine': 'Polecenie zostanie wysłane do urządzenia.',
      'confirmConsequential':
          'Ta operacja może zmienić działanie urządzenia przez dłuższy czas.',
      'confirmCritical':
          'To operacja krytyczna. Sterownik nadal zweryfikuje blokady i może ją odrzucić.',
      'cancel': 'Anuluj',
      'confirm': 'Potwierdź',
      'commandPending': 'Oczekiwanie na potwierdzenie…',
      'editDashboard': 'Edytuj pulpit',
      'resetDashboard': 'Przywróć pulpit',
      'visible': 'Widoczna',
      'largeCard': 'Duża karta',
      'theme': 'Motyw',
      'themeSystem': 'Systemowy',
      'themeLight': 'Jasny',
      'themeDark': 'Ciemny',
      'language': 'Język',
      'polish': 'Polski',
      'english': 'English',
      'sourceManagement': 'Zarządzanie źródłem',
      'haInstances': 'Instancje Home Assistant',
      'addHaInstance': 'Dodaj instancję Home Assistant',
      'removeHaInstance': 'Usuń instancję',
      'removeHaInstanceConfirm':
          'Profil „{value}”, jego token i lokalny cache zostaną usunięte z urządzenia.',
      'switchSource': 'Przełącz źródło',
      'removeSource': 'Usuń źródło i dane lokalne',
      'removeSourceConfirm':
          'Token, odcisk certyfikatu, sesja i cache tego źródła zostaną usunięte z urządzenia.',
      'demoScenarios': 'Scenariusze Demo',
      'simulateOffline': 'Symuluj brak sieci',
      'simulateAlarm': 'Symuluj alarm akwarium',
      'appUpdate': 'Aktualizacja Home Control',
      'deviceUpdates': 'Aktualizacje urządzeń',
      'currentVersion': 'Obecna: {value}',
      'latestVersion': 'Najnowsza: {value}',
      'install': 'Zainstaluj',
      'upToDate': 'Aktualne',
      'unsupported': 'Jeszcze nieobsługiwane',
      'mandatory': 'Wymagana',
      'availableVersion': 'Dostępna wersja {value}',
      'later': 'Później',
      'downloadAndInstall': 'Pobierz i zainstaluj',
      'continueInstallation': 'Kontynuuj instalację',
      'otaAvailableMessage':
          'Aktualizacja zawiera poprawki i usprawnienia. Pakiet zostanie zweryfikowany przed instalacją.',
      'otaDownloadingMessage':
          'Pobieram podpisany pakiet i sprawdzam jego sumę SHA-256.',
      'otaVerifyingMessage':
          'Sprawdzam pakiet, wersję i certyfikat podpisujący.',
      'otaPermissionMessage':
          'Android otworzył ustawienia instalowania z tego źródła. Włącz zgodę dla Home Control, wróć do aplikacji i kontynuuj.',
      'otaFailedMessage': 'Aktualizacja nie została zainstalowana.',
      'otaPreparingMessage': 'Przygotowuję bezpieczną aktualizację aplikacji.',
      'diagnostics': 'Diagnostyka',
      'entities': 'Encje',
      'lastSyncLabel': 'Ostatnia synchronizacja',
      'localCache': 'Cache lokalny',
      'encrypted': 'Szyfrowany',
      'privacyAndAbout': 'Prywatność i informacje',
      'privacy': 'Prywatność',
      'privacyDescription':
          'Home Control nie sprzedaje danych. Poświadczenia i ostatni snapshot pozostają w bezpiecznym magazynie urządzenia.',
      'appVersion': 'Wersja {value}',
      'openSourceLicenses': 'Licencje open source',
      'attributes': 'Atrybuty źródłowe',
      'entitySource': 'Źródło: {value}',
      'entityUpdated': 'Aktualizacja: {value}',
      'firmware': 'Firmware',
      'manufacturer': 'Producent',
      'model': 'Model',
      'lastSeen': 'Ostatnio: {value}',
      'search': 'Szukaj',
      'searchHint': 'Urządzenie, encja lub pomieszczenie',
      'noResults': 'Brak wyników dla tego zapytania.',
      'all': 'Wszystkie',
      'unknownEntityHint':
          'Przyszły lub niestandardowy typ jest pokazany bez zgadywania sposobu sterowania.',
      'errorTitle': 'Nie udało się ukończyć operacji',
      'errorNetwork':
          'Źródło jest nieosiągalne. Sprawdź sieć lokalną, VPN lub Internet.',
      'errorOffline':
          'Brak połączenia. Ostatnie dane pozostają widoczne tylko do odczytu.',
      'errorToken':
          'Token wygasł, został odrzucony albo nie ma wymaganych uprawnień.',
      'errorServer': 'Serwer zwrócił błąd. Spróbuj ponownie za chwilę.',
      'errorInvalidResponse':
          'Źródło zwróciło dane w nieobsługiwanym lub uszkodzonym formacie.',
      'errorCertificate':
          'Certyfikat lub jego odcisk zmienił się. Potwierdź tożsamość na zaufanym ekranie.',
      'errorStorage':
          'Nie udało się otworzyć lub zaktualizować bezpiecznego magazynu.',
      'errorPermission': 'Ta operacja nie jest dozwolona dla wskazanej encji.',
      'errorUnsupported':
          'Ta funkcja nie jest jeszcze bezpiecznie obsługiwana przez źródło.',
      'errorInvalidValue':
          'Wartość jest poza dozwolonym zakresem albo ma zły format.',
      'errorInvalidCredentials': 'Sprawdź adres i wklej pełny token dostępu.',
      'errorCommandUnavailable':
          'Sterowanie jest zablokowane, gdy encja lub źródło jest niedostępne.',
      'errorUnknown':
          'Wystąpił nieoczekiwany błąd. Dane wrażliwe nie zostały zapisane w logu.',
      'retry': 'Spróbuj ponownie',
      'reconfigure': 'Wybierz inne źródło',
      'dismiss': 'Zamknij',
      'noticeRefreshed': 'Dane zostały odświeżone.',
      'noticeCommandAccepted': 'Źródło potwierdziło polecenie.',
      'noticeUpdateStarted': 'Proces aktualizacji został rozpoczęty.',
      'justNow': 'przed chwilą',
      'secondsAgo': '{value} s temu',
      'minutesAgo': '{value} min temu',
      'hoursAgo': '{value} godz. temu',
      'never': 'nigdy',
      'historyEmpty': 'Źródło nie zwróciło próbek dla wybranego okresu.',
      'history24h': '24 godziny',
      'history7d': '7 dni',
      'entity_light': 'Światło',
      'entity_switchEntity': 'Przełącznik',
      'entity_sensor': 'Czujnik',
      'entity_binarySensor': 'Czujnik binarny',
      'entity_climate': 'Klimat',
      'entity_cover': 'Osłona',
      'entity_lock': 'Zamek',
      'entity_alarmControlPanel': 'Alarm',
      'entity_camera': 'Kamera',
      'entity_mediaPlayer': 'Odtwarzacz',
      'entity_fan': 'Wentylator',
      'entity_vacuum': 'Odkurzacz',
      'entity_weather': 'Pogoda',
      'entity_person': 'Osoba',
      'entity_deviceTracker': 'Lokalizator',
      'entity_scene': 'Scena',
      'entity_script': 'Skrypt',
      'entity_automation': 'Automatyzacja',
      'entity_button': 'Przycisk',
      'entity_inputButton': 'Przycisk wejściowy',
      'entity_number': 'Liczba',
      'entity_inputNumber': 'Liczba wejściowa',
      'entity_select': 'Lista',
      'entity_inputSelect': 'Lista wejściowa',
      'entity_text': 'Tekst',
      'entity_inputText': 'Tekst wejściowy',
      'entity_update': 'Aktualizacja',
      'entity_unknown': 'Typ niestandardowy',
    },
    'en': <String, String>{
      'appName': 'Home Control',
      'appSubtitle': 'Your home. One secure control center.',
      'booting': 'Preparing the secure environment…',
      'connecting': 'Connecting to the data source…',
      'sourceTitle': 'Choose a source',
      'sourceDescription':
          'Home Control is a native app. A source provides data but does not define the interface.',
      'aquaHub': 'AquaHub',
      'aquaHubDescription':
          'Local ESP32-P4 panel, ESP-NOW devices and aquarium.',
      'homeAssistant': 'Home Assistant',
      'homeAssistantDescription':
          'An existing local instance or a secure remote address.',
      'demo': 'Offline demo',
      'demoDescription':
          'A complete home, aquarium, alarms, history and updates without an account.',
      'recommended': 'Recommended',
      'advanced': 'Advanced',
      'back': 'Back',
      'connectHa': 'Add Home Assistant',
      'haUrl': 'Instance address',
      'haUrlHint': 'https://homeassistant.local:8123',
      'haProfileName': 'Profile name',
      'haProfileNameHint': 'Home, office or cabin',
      'haToken': 'Long-lived access token',
      'showToken': 'Show token',
      'hideToken': 'Hide token',
      'testAndSave': 'Test and save',
      'oauthHint':
          'This is the advanced long-lived-token sign-in. OAuth requires the app owner public Client ID and a secure redirect URI; the release documentation defines that activation gate.',
      'secureStorageHint':
          'Credentials are kept in the operating system secure storage. HTTP is accepted only on a local network.',
      'dashboard': 'Dashboard',
      'rooms': 'Rooms',
      'devices': 'Devices',
      'automations': 'Automations',
      'updates': 'Updates',
      'settings': 'Settings',
      'navDashboard': 'Home',
      'navRooms': 'Rooms',
      'navDevices': 'Devices',
      'navAutomations': 'Actions',
      'navUpdates': 'Updates',
      'navSettings': 'Settings',
      'home': 'Home',
      'refresh': 'Refresh',
      'source': 'Source',
      'connected': 'Connected',
      'offline': 'Offline',
      'stale': 'Stale data',
      'partial': 'Partial synchronization',
      'lastSync': 'Last sync: {value}',
      'favorites': 'Favorites',
      'areasOverview': 'Rooms',
      'recentActivity': 'Recent activity',
      'aquarium': 'Aquarium',
      'aquariumHealthy': 'Parameters stable',
      'aquariumAlarm': 'Needs attention',
      'waterTemperature': 'Water temperature',
      'waterChemistry': 'Water chemistry',
      'connection': 'Connection',
      'details': 'Details',
      'noAquarium': 'This source does not expose an aquarium module yet.',
      'noFavorites': 'Add favorites from an entity menu.',
      'noAreas': 'The source returned no rooms.',
      'noDevices': 'No devices found.',
      'noAutomations': 'This source has no automations.',
      'scenesAndScripts': 'Scenes and scripts',
      'noUpdates': 'No updates are available.',
      'entitiesCount': '{value} entities',
      'onlineDevices': '{value} devices online',
      'available': 'Available',
      'unavailable': 'Unavailable',
      'unknown': 'Unknown',
      'removed': 'Removed',
      'noData': 'No data',
      'stateOn': 'On',
      'stateOff': 'Off',
      'turnOn': 'Turn on',
      'turnOff': 'Turn off',
      'openCover': 'Open',
      'closeCover': 'Close',
      'opened': 'Open',
      'closed': 'Closed',
      'lockAction': 'Lock',
      'unlockAction': 'Unlock',
      'locked': 'Locked',
      'unlocked': 'Unlocked',
      'alarmMode': 'Alarm mode',
      'alarm_disarmed': 'Disarmed',
      'alarm_armed_home': 'Armed home',
      'alarm_armed_away': 'Armed away',
      'alarm_armed_night': 'Armed night',
      'start': 'Start',
      'returnToBase': 'Return to base',
      'textValue': 'Text value',
      'run': 'Run',
      'setValue': 'Set value: {value}',
      'selectOption': 'Select option',
      'history': 'History',
      'favorite': 'Favorite',
      'removeFavorite': 'Remove favorite',
      'confirmTitle': 'Confirm operation',
      'confirmRoutine': 'The command will be sent to the device.',
      'confirmConsequential':
          'This operation may change the device behavior for an extended time.',
      'confirmCritical':
          'This is a critical operation. The controller will still validate interlocks and may reject it.',
      'cancel': 'Cancel',
      'confirm': 'Confirm',
      'commandPending': 'Waiting for acknowledgement…',
      'editDashboard': 'Edit dashboard',
      'resetDashboard': 'Reset dashboard',
      'visible': 'Visible',
      'largeCard': 'Large card',
      'theme': 'Theme',
      'themeSystem': 'System',
      'themeLight': 'Light',
      'themeDark': 'Dark',
      'language': 'Language',
      'polish': 'Polski',
      'english': 'English',
      'sourceManagement': 'Source management',
      'haInstances': 'Home Assistant instances',
      'addHaInstance': 'Add Home Assistant instance',
      'removeHaInstance': 'Remove instance',
      'removeHaInstanceConfirm':
          'Profile “{value}”, its token and local cache will be removed from this device.',
      'switchSource': 'Switch source',
      'removeSource': 'Remove source and local data',
      'removeSourceConfirm':
          'The token, certificate fingerprint, session and source cache will be removed from this device.',
      'demoScenarios': 'Demo scenarios',
      'simulateOffline': 'Simulate offline',
      'simulateAlarm': 'Simulate aquarium alarm',
      'appUpdate': 'Home Control update',
      'deviceUpdates': 'Device updates',
      'currentVersion': 'Current: {value}',
      'latestVersion': 'Latest: {value}',
      'install': 'Install',
      'upToDate': 'Up to date',
      'unsupported': 'Not safely supported yet',
      'mandatory': 'Required',
      'availableVersion': 'Version {value} is available',
      'later': 'Later',
      'downloadAndInstall': 'Download and install',
      'continueInstallation': 'Continue installation',
      'otaAvailableMessage':
          'This update contains fixes and improvements. The package will be verified before installation.',
      'otaDownloadingMessage':
          'Downloading the signed package and verifying its SHA-256 digest.',
      'otaVerifyingMessage':
          'Verifying package identity, version and signing certificate.',
      'otaPermissionMessage':
          'Android opened the install-source settings. Allow Home Control, return to the app and continue.',
      'otaFailedMessage': 'The update was not installed.',
      'otaPreparingMessage': 'Preparing the secure application update.',
      'diagnostics': 'Diagnostics',
      'entities': 'Entities',
      'lastSyncLabel': 'Last synchronization',
      'localCache': 'Local cache',
      'encrypted': 'Encrypted',
      'privacyAndAbout': 'Privacy and about',
      'privacy': 'Privacy',
      'privacyDescription':
          'Home Control does not sell data. Credentials and the latest snapshot remain in the device secure storage.',
      'appVersion': 'Version {value}',
      'openSourceLicenses': 'Open-source licenses',
      'attributes': 'Source attributes',
      'entitySource': 'Source: {value}',
      'entityUpdated': 'Updated: {value}',
      'firmware': 'Firmware',
      'manufacturer': 'Manufacturer',
      'model': 'Model',
      'lastSeen': 'Last seen: {value}',
      'search': 'Search',
      'searchHint': 'Device, entity or room',
      'noResults': 'No results for this query.',
      'all': 'All',
      'unknownEntityHint':
          'A future or custom type is displayed without guessing how to control it.',
      'errorTitle': 'The operation could not be completed',
      'errorNetwork':
          'The source is unreachable. Check your LAN, VPN or Internet connection.',
      'errorOffline':
          'There is no connection. Last known data remains read-only.',
      'errorToken':
          'The token expired, was rejected or lacks the required permissions.',
      'errorServer': 'The server returned an error. Try again shortly.',
      'errorInvalidResponse':
          'The source returned damaged or unsupported data.',
      'errorCertificate':
          'The certificate or fingerprint changed. Confirm identity on a trusted display.',
      'errorStorage': 'The secure storage could not be opened or updated.',
      'errorPermission':
          'This operation is not permitted for the selected entity.',
      'errorUnsupported':
          'The source does not safely support this feature yet.',
      'errorInvalidValue':
          'The value is outside its allowed range or has an invalid format.',
      'errorInvalidCredentials':
          'Check the address and paste the complete access token.',
      'errorCommandUnavailable':
          'Control is locked while the source or entity is unavailable.',
      'errorUnknown':
          'An unexpected error occurred. Sensitive data was not recorded in logs.',
      'retry': 'Try again',
      'reconfigure': 'Choose another source',
      'dismiss': 'Dismiss',
      'noticeRefreshed': 'Data refreshed.',
      'noticeCommandAccepted': 'The source acknowledged the command.',
      'noticeUpdateStarted': 'The update process has started.',
      'justNow': 'just now',
      'secondsAgo': '{value} sec ago',
      'minutesAgo': '{value} min ago',
      'hoursAgo': '{value} hr ago',
      'never': 'never',
      'historyEmpty': 'The source returned no samples for this period.',
      'history24h': '24 hours',
      'history7d': '7 days',
      'entity_light': 'Light',
      'entity_switchEntity': 'Switch',
      'entity_sensor': 'Sensor',
      'entity_binarySensor': 'Binary sensor',
      'entity_climate': 'Climate',
      'entity_cover': 'Cover',
      'entity_lock': 'Lock',
      'entity_alarmControlPanel': 'Alarm',
      'entity_camera': 'Camera',
      'entity_mediaPlayer': 'Media player',
      'entity_fan': 'Fan',
      'entity_vacuum': 'Vacuum',
      'entity_weather': 'Weather',
      'entity_person': 'Person',
      'entity_deviceTracker': 'Tracker',
      'entity_scene': 'Scene',
      'entity_script': 'Script',
      'entity_automation': 'Automation',
      'entity_button': 'Button',
      'entity_inputButton': 'Input button',
      'entity_number': 'Number',
      'entity_inputNumber': 'Input number',
      'entity_select': 'Select',
      'entity_inputSelect': 'Input select',
      'entity_text': 'Text',
      'entity_inputText': 'Input text',
      'entity_update': 'Update',
      'entity_unknown': 'Custom type',
    },
  };
}

final class _HomeControlStringsDelegate
    extends LocalizationsDelegate<HomeControlStrings> {
  const _HomeControlStringsDelegate();

  @override
  bool isSupported(Locale locale) =>
      <String>{'pl', 'en'}.contains(locale.languageCode);

  @override
  Future<HomeControlStrings> load(Locale locale) =>
      SynchronousFuture<HomeControlStrings>(HomeControlStrings(locale));

  @override
  bool shouldReload(covariant LocalizationsDelegate<HomeControlStrings> old) =>
      false;
}
