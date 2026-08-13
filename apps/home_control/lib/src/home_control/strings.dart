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

  String withValues(String key, Map<String, Object> values) {
    var result = t(key);
    for (final entry in values.entries) {
      result = result.replaceAll('{${entry.key}}', '${entry.value}');
    }
    return result;
  }

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
    final rawState = entity.state.toString();
    final stateKey = 'rawState_$rawState';
    final translated = t(stateKey);
    return translated == stateKey ? rawState.replaceAll('_', ' ') : translated;
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
      'appSubtitle': 'TwÃ³j dom. Jedno bezpieczne centrum.',
      'booting': 'PrzygotowujÄ™ bezpieczne Å›rodowiskoâ€¦',
      'connecting': 'ÅÄ…czenie ze ÅºrÃ³dÅ‚em danychâ€¦',
      'sourceTitle': 'Wybierz ÅºrÃ³dÅ‚o',
      'sourceDescription':
          'Home Control jest natywnÄ… aplikacjÄ…. Å¹rÃ³dÅ‚o dostarcza dane, ale nie definiuje interfejsu.',
      'aquaHub': 'AquaHub',
      'aquaHubDescription':
          'Lokalny panel ESP32-P4, urzÄ…dzenia ESP-NOW i akwarium.',
      'hubWelcome': 'Witaj w AquaHub',
      'hubConfirmPanel': 'PotwierdÅº panel',
      'hubWelcomeDescription':
          'Aplikacja automatycznie odnajdzie panel ESP32-P4 w sieci lokalnej. Jednorazowe parowanie otwiera natywny pulpit urzÄ…dzeÅ„, czujnikÃ³w, automatyzacji i aktualizacji.',
      'hubConfirmDescription':
          'PorÃ³wnaj odcisk certyfikatu z ekranem System fizycznego panelu i wpisz wyÅ›wietlony kod.',
      'hubDemo': 'Zobacz peÅ‚nÄ… aplikacjÄ™ w trybie demo',
      'hubAutonomyHint':
          'Sterownik CYD pozostaje autonomiczny. Token trafia do szyfrowanego magazynu systemu, a dostÄ™p zdalny dziaÅ‚a przez VPN.',
      'hubSearching': 'Szukam panelu w sieciâ€¦',
      'hubChooseFound': 'Wybierz znaleziony panel',
      'hubAutoDiscovery': 'Automatyczne wykrywanie',
      'hubSameWifi': 'Telefon i AquaHub muszÄ… byÄ‡ w tej samej sieci Wi-Fi.',
      'hubHttpsVerified': 'PoÅ‚Ä…czenie zostanie zweryfikowane przez HTTPS.',
      'hubNotFound': 'Nie znaleziono panelu. SprawdÅº Wi-Fi i zasilanie P4.',
      'hubMdns': 'Aplikacja uÅ¼ywa natywnego Bonjour/mDNS.',
      'scanAgain': 'Skanuj ponownie',
      'hubAdvancedConnection': 'PoÅ‚Ä…czenie zaawansowane',
      'hubManualOnly': 'RÄ™czny adres tylko wtedy, gdy mDNS jest zablokowany',
      'hubHttpsAddress': 'Adres HTTPS panelu',
      'connectManually': 'PoÅ‚Ä…cz rÄ™cznie',
      'hubSecureHttps': 'AquaHub ESP32-P4 Â· bezpieczne HTTPS',
      'chooseAnotherHub': 'Wybierz inny panel',
      'certificateFingerprint': 'Odcisk certyfikatu SHA-256',
      'fingerprintMatches': 'Odcisk jest identyczny jak na panelu',
      'pairingCode': '6â€‘cyfrowy kod z panelu',
      'pairAndOpen': 'Sparuj i otwÃ³rz pulpit',
      'hubDiscoveryFailed': 'Nie udaÅ‚o siÄ™ uruchomiÄ‡ wykrywania AquaHub.',
      'hubErrorSession': 'Nie udaÅ‚o siÄ™ odczytaÄ‡ bezpiecznej sesji AquaHub.',
      'hubErrorHttpsAddress':
          'Podaj peÅ‚ny adres HTTPS, np. https://aquahub.local:8443.',
      'hubErrorDiscovery': 'Nie udaÅ‚o siÄ™ odnaleÅºÄ‡ AquaHub w sieci lokalnej.',
      'hubErrorDiscoverFirst': 'Najpierw sprawdÅº poÅ‚Ä…czenie z AquaHub.',
      'hubErrorFingerprintConfirm':
          'PorÃ³wnaj odcisk z panelem i potwierdÅº jego zgodnoÅ›Ä‡.',
      'hubErrorPairingCode': 'Kod parowania musi mieÄ‡ dokÅ‚adnie szeÅ›Ä‡ cyfr.',
      'hubErrorSaveSession': 'Nie udaÅ‚o siÄ™ bezpiecznie zapisaÄ‡ sesji AquaHub.',
      'hubErrorAuthentication': 'AquaHub odrzuciÅ‚ dane uwierzytelniajÄ…ce.',
      'hubErrorNetwork': 'AquaHub jest nieosiÄ…galny w sieci lokalnej.',
      'hubErrorInvalidResponse': 'AquaHub zwrÃ³ciÅ‚ nieprawidÅ‚owÄ… odpowiedÅº.',
      'hubErrorServer': 'AquaHub zwrÃ³ciÅ‚ bÅ‚Ä…d serwera.',
      'hubErrorSecurity':
          'Nie moÅ¼na potwierdziÄ‡ bezpiecznej toÅ¼samoÅ›ci AquaHub.',
      'homeAssistant': 'Home Assistant',
      'homeAssistantDescription':
          'IstniejÄ…ca instancja lokalna lub bezpieczny adres zdalny.',
      'demo': 'Demo offline',
      'demoDescription':
          'PeÅ‚ny dom, akwarium, alarmy, historia i aktualizacje bez konta.',
      'recommended': 'Zalecane',
      'advanced': 'Zaawansowane',
      'back': 'Wstecz',
      'connectHa': 'Dodaj Home Assistant',
      'haUrl': 'Adres instancji',
      'haUrlHint': 'https://homeassistant.local:8123',
      'haProfileName': 'Nazwa profilu',
      'haProfileNameHint': 'Dom, biuro lub domek',
      'haToken': 'DÅ‚ugoterminowy token dostÄ™pu',
      'showToken': 'PokaÅ¼ token',
      'hideToken': 'Ukryj token',
      'testAndSave': 'SprawdÅº i zapisz',
      'oauthHint':
          'To zaawansowane logowanie tokenem dÅ‚ugoterminowym. OAuth wymaga publicznego Client ID i bezpiecznego redirect URI wÅ‚aÅ›ciciela aplikacji; gotowa warstwa jest opisana w dokumentacji wydania.',
      'secureStorageHint':
          'PoÅ›wiadczenia sÄ… przechowywane w bezpiecznym magazynie systemu. HTTP jest akceptowane wyÅ‚Ä…cznie w sieci lokalnej.',
      'dashboard': 'Pulpit',
      'rooms': 'Pomieszczenia',
      'devices': 'UrzÄ…dzenia',
      'automations': 'Automatyzacje',
      'updates': 'Aktualizacje',
      'settings': 'Ustawienia',
      'navDashboard': 'Pulpit',
      'navRooms': 'Pokoje',
      'navDevices': 'SprzÄ™t',
      'navAutomations': 'Akcje',
      'navUpdates': 'OTA',
      'navSettings': 'Opcje',
      'more': 'WiÄ™cej',
      'home': 'Dom',
      'homeHealthyTitle': 'Dom dziaÅ‚a spokojnie',
      'homeHealthyDescription':
          'Wszystkie najwaÅ¼niejsze systemy odpowiadajÄ… prawidÅ‚owo.',
      'homeAttentionTitle': 'Kilka rzeczy wymaga uwagi',
      'homeAttentionDescription': '{value} spraw do krÃ³tkiego sprawdzenia.',
      'homeOfflineTitle': 'Sterowanie jest chwilowo offline',
      'homeOfflineDescription':
          'Ostatni zapisany stan pozostaje dostÄ™pny tylko do odczytu.',
      'liveStatus': 'Na Å¼ywo',
      'devicesOnlineLabel': 'UrzÄ…dzenia online',
      'automationsActiveLabel': 'Aktywne reguÅ‚y',
      'updatesAvailableLabel': 'Do instalacji',
      'refresh': 'OdÅ›wieÅ¼',
      'source': 'Å¹rÃ³dÅ‚o',
      'connected': 'PoÅ‚Ä…czono',
      'offline': 'Offline',
      'stale': 'Dane nieaktualne',
      'partial': 'CzÄ™Å›ciowa synchronizacja',
      'lastSync': 'Ostatnia synchronizacja: {value}',
      'favorites': 'Ulubione',
      'quickControls': 'Szybkie sterowanie',
      'quickControlsDescription': 'NajwaÅ¼niejsze funkcje zawsze pod rÄ™kÄ…',
      'customize': 'Dostosuj',
      'chooseDevices': 'Wybierz urzÄ…dzenia',
      'noQuickControlsTitle': 'Zbuduj wÅ‚asny panel skrÃ³tÃ³w',
      'areasOverview': 'Pomieszczenia',
      'areasOverviewDescription':
          'NajwaÅ¼niejsze informacje w jednym spojrzeniu',
      'roomsSummary': '{rooms} pomieszczeÅ„ Â· {items} elementÃ³w',
      'devicesSummary': '{online} z {all} urzÄ…dzeÅ„ online',
      'seeAll': 'Zobacz wszystkie',
      'noRoomsTitle': 'Brak aktywnych pomieszczeÅ„',
      'recentActivity': 'Ostatnia aktywnoÅ›Ä‡',
      'recentChanges': 'Ostatnie zmiany',
      'recentChangesDescription': 'Rzeczywiste zmiany stanu urzÄ…dzeÅ„',
      'noRecentChangesTitle': 'W domu jest spokojnie',
      'noRecentChanges': 'Nowe zdarzenia pojawiÄ… siÄ™ tutaj automatycznie.',
      'attentionCenter': 'Do sprawdzenia',
      'attentionOfflineTitle': 'Brak poÅ‚Ä…czenia ze ÅºrÃ³dÅ‚em',
      'attentionOfflineDescription':
          'Dotknij, aby sprÃ³bowaÄ‡ poÅ‚Ä…czyÄ‡ ponownie.',
      'attentionSyncTitle': 'Dane wymagajÄ… odÅ›wieÅ¼enia',
      'attentionSyncDescription': 'Ostatnia synchronizacja nie jest kompletna.',
      'attentionAquariumTitle': 'Alarm moduÅ‚u akwarium',
      'attentionAquariumDescription': 'OtwÃ³rz kartÄ™ i sprawdÅº aktywne alarmy.',
      'attentionDevicesTitle': '{value} urzÄ…dzeÅ„ jest offline',
      'attentionDevicesDescription': 'SprawdÅº zasilanie i Å‚Ä…cznoÅ›Ä‡ urzÄ…dzeÅ„.',
      'attentionUpdatesTitle': '{value} aktualizacji czeka',
      'attentionUpdatesDescription':
          'Pakiety sÄ… gotowe do bezpiecznej instalacji.',
      'aquarium': 'Akwarium',
      'aquariumHealthy': 'Parametry stabilne',
      'aquariumAlarm': 'Wymaga uwagi',
      'aquariumNoAlarms': 'Brak aktywnych alarmÃ³w',
      'aquariumOffline': 'Ostatni zapisany odczyt',
      'aquariumIncomplete': 'NiepeÅ‚ny zestaw odczytÃ³w',
      'aquariumStale': 'Odczyty wymagajÄ… odÅ›wieÅ¼enia',
      'waterTemperature': 'Temperatura wody',
      'waterChemistry': 'Chemia wody',
      'connection': 'PoÅ‚Ä…czenie',
      'details': 'SzczegÃ³Å‚y',
      'noAquarium': 'To ÅºrÃ³dÅ‚o nie udostÄ™pnia jeszcze moduÅ‚u akwarium.',
      'noAquariumTitle': 'ModuÅ‚ akwarium nie jest dostÄ™pny',
      'noFavorites':
          'Wybierz urzÄ…dzenia i funkcje, ktÃ³rych uÅ¼ywasz najczÄ™Å›ciej.',
      'noAreas': 'Å¹rÃ³dÅ‚o nie zwrÃ³ciÅ‚o pomieszczeÅ„.',
      'noDevices': 'Nie znaleziono urzÄ…dzeÅ„.',
      'noAutomations': 'Brak automatyzacji w tym ÅºrÃ³dle.',
      'scenesAndScripts': 'Sceny i skrypty',
      'noUpdates': 'Brak dostÄ™pnych aktualizacji.',
      'entitiesCount': '{value} encji',
      'itemsCount': '{value} elementÃ³w',
      'onlineDevices': '{value} urzÄ…dzeÅ„ online',
      'available': 'DostÄ™pne',
      'unavailable': 'NiedostÄ™pne',
      'unknown': 'Nieznane',
      'removed': 'UsuniÄ™te',
      'noData': 'Brak danych',
      'stateOn': 'WÅ‚Ä…czone',
      'stateOff': 'WyÅ‚Ä…czone',
      'activeEntities': '{value} aktywne',
      'activeNow': '{value} aktywne teraz',
      'rawState_heat': 'Grzanie',
      'rawState_heating': 'Grzanie',
      'rawState_cooling': 'ChÅ‚odzenie',
      'rawState_playing': 'Odtwarzanie',
      'rawState_paused': 'Wstrzymano',
      'rawState_idle': 'Bezczynne',
      'rawState_docked': 'W bazie',
      'rawState_home': 'W domu',
      'rawState_away': 'Poza domem',
      'rawState_partlycloudy': 'CzÄ™Å›ciowe zachmurzenie',
      'turnOn': 'WÅ‚Ä…cz',
      'turnOff': 'WyÅ‚Ä…cz',
      'openCover': 'OtwÃ³rz',
      'closeCover': 'Zamknij',
      'opened': 'Otwarta',
      'closed': 'ZamkniÄ™ta',
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
      'returnToBase': 'WrÃ³Ä‡ do bazy',
      'textValue': 'WartoÅ›Ä‡ tekstowa',
      'run': 'Uruchom',
      'setValue': 'Ustaw wartoÅ›Ä‡: {value}',
      'selectOption': 'Wybierz opcjÄ™',
      'history': 'Historia',
      'favorite': 'Ulubione',
      'removeFavorite': 'UsuÅ„ z ulubionych',
      'confirmTitle': 'PotwierdÅº operacjÄ™',
      'confirmRoutine': 'Polecenie zostanie wysÅ‚ane do urzÄ…dzenia.',
      'confirmConsequential':
          'Ta operacja moÅ¼e zmieniÄ‡ dziaÅ‚anie urzÄ…dzenia przez dÅ‚uÅ¼szy czas.',
      'confirmCritical':
          'To operacja krytyczna. Sterownik nadal zweryfikuje blokady i moÅ¼e jÄ… odrzuciÄ‡.',
      'cancel': 'Anuluj',
      'confirm': 'PotwierdÅº',
      'commandPending': 'Oczekiwanie na potwierdzenieâ€¦',
      'editDashboard': 'Edytuj pulpit',
      'resetDashboard': 'PrzywrÃ³Ä‡ pulpit',
      'visible': 'Widoczna',
      'largeCard': 'DuÅ¼a karta',
      'theme': 'Motyw',
      'themeSystem': 'Systemowy',
      'themeLight': 'Jasny',
      'themeDark': 'Ciemny',
      'language': 'JÄ™zyk',
      'polish': 'Polski',
      'english': 'English',
      'security': 'BezpieczeÅ„stwo',
      'biometricProtection': 'Biometria dla operacji krytycznych',
      'biometricProtectionDescription':
   ß¾;¶‰žËkºwµç@€€€€€É•½µµ•¹‘•œè€I•½µµ•¹‘•œ°4(€€€€€€…‘Ù…¹•œè€‘Ù…¹•œ°4(€€€€€€‰…¬œè€	…¬œ°4(€€€€€€½¹¹•Ñ!„œè€‘!½µ”ÍÍ¥ÍÑ…¹Ðœ°4(€€€€€€¡…UÉ°œè€%¹ÍÑ…¹”…‘‘É•ÍÌœ°4(€€€€€€¡…UÉ±!¥¹Ðœè€¡ÑÑÁÌè¼½¡½µ•…ÍÍ¥ÍÑ…¹Ð¹±½…°èàÄÈÌœ°4(€€€€€€¡…AÉ½™¥±•9…µ”œè€AÉ½™¥±”¹…µ”œ°4(€€€€€€¡…AÉ½™¥±•9…µ•!¥¹Ðœè€!½µ”°½™™¥”½È…‰¥¸œ°4(€€€€€€¡…Q½­•¸œè€1½¹œµ±¥Ù•…•ÍÌÑ½­•¸œ°4(€€€€€€Í¡½ÝQ½­•¸œè€M¡½ÜÑ½­•¸œ°4(€€€€€€¡¥‘•Q½­•¸œè€!¥‘”Ñ½­•¸œ°4(€€€€€€Ñ•ÍÑ¹‘M…Ù”œè€Q•ÍÐ…¹Í…Ù”œ°4(€€€€€€½…ÕÑ¡!¥¹Ðœè4(€€€€€€€€€€Q¡¥Ì¥ÌÑ¡”…‘Ù…¹•±½¹œµ±¥Ù•µÑ½­•¸Í¥¸µ¥¸¸=ÕÑ É•ÅÕ¥É•ÌÑ¡”…ÁÀ½Ý¹•ÈÁÕ‰±¥Œ±¥•¹Ð%…¹„Í•ÕÉ”É•‘¥É•ÐUI$ìÑ¡”É•±•…Í”‘½Õµ•¹Ñ…Ñ¥½¸‘•™¥¹•ÌÑ¡…Ð…Ñ¥Ù…Ñ¥½¸…Ñ”¸œ°4(€€€€€€Í•ÕÉ•MÑ½É…•!¥¹Ðœè4(€€€€€€€€€€É•‘•¹Ñ¥…±Ì…É”­•ÁÐ¥¸Ñ¡”½Á•É…Ñ¥¹œÍåÍÑ•´Í•ÕÉ”ÍÑ½É…”¸!QQ@¥Ì…•ÁÑ•½¹±ä½¸„±½…°¹•ÑÝ½É¬¸œ°4(€€€€€€‘…Í¡‰½…Éœè€…Í¡‰½…Éœ°4(€€€€€€É½½µÌœè€I½½µÌœ°4(€€€€€€‘•Ù¥•Ìœè€•Ù¥•Ìœ°4(€€€€€€…ÕÑ½µ…Ñ¥½¹Ìœè€ÕÑ½µ…Ñ¥½¹Ìœ°4(€€€€€€ÕÁ‘…Ñ•Ìœè€UÁ‘…Ñ•Ìœ°4(€€€€€€Í•ÑÑ¥¹Ìœè€M•ÑÑ¥¹Ìœ°4(€€€€€€¹…Ù…Í¡‰½…Éœè€!½µ”œ°4(€€€€€€¹…ÙI½½µÌœè€I½½µÌœ°4(€€€€€€¹…Ù•Ù¥•Ìœè€•Ù¥•Ìœ°4(€€€€€€¹…ÙÕÑ½µ…Ñ¥½¹Ìœè€Ñ¥½¹Ìœ°4(€€€€€€¹…ÙUÁ‘…Ñ•Ìœè€UÁ‘…Ñ•Ìœ°4(€€€€€€¹…ÙM•ÑÑ¥¹Ìœè€M•ÑÑ¥¹Ìœ°4(€€€€€€µ½É”œè€5½É”œ°4(€€€€€€¡½µ”œè€!½µ”œ°4(€€€€€€¡½µ•!•…±Ñ¡åQ¥Ñ±”œè€e½ÕÈ¡½µ”¥ÌÉÕ¹¹¥¹œÍµ½½Ñ¡±äœ°4(€€€€€€¡½µ•!•…±Ñ¡å•ÍÉ¥ÁÑ¥½¸œè4(€€€€€€€€€€±°•ÍÍ•¹Ñ¥…°ÍåÍÑ•µÌ…É”É•ÍÁ½¹‘¥¹œ…Ì•áÁ•Ñ•¸œ°4(€€€€€€¡½µ•ÑÑ•¹Ñ¥½¹Q¥Ñ±”œè€™•ÜÑ¡¥¹Ì¹••…ÑÑ•¹Ñ¥½¸œ°4(€€€€€€¡½µ•ÑÑ•¹Ñ¥½¹•ÍÉ¥ÁÑ¥½¸œè€íÙ…±Õ•ô¥Ñ•µÌ…É”Ý½ÉÑ „ÅÕ¥¬¡•¬¸œ°4(€€€€€€¡½µ•=™™±¥¹•Q¥Ñ±”œè€½¹ÑÉ½°¥ÌÑ•µÁ½É…É¥±ä½™™±¥¹”œ°4(€€€€€€¡½µ•=™™±¥¹••ÍÉ¥ÁÑ¥½¸œè4(€€€€€€€€€€Q¡”±…Ñ•ÍÐÍ…Ù•ÍÑ…Ñ”É•µ…¥¹Ì…Ù…¥±…‰±”¥¸É•…µ½¹±äµ½‘”¸œ°4(€€€€€€±¥Ù•MÑ…ÑÕÌœè€1¥Ù”œ°4(€€€€€€‘•Ù¥•Í=¹±¥¹•1…‰•°œè€•Ù¥•Ì½¹±¥¹”œ°4(€€€€€€…ÕÑ½µ…Ñ¥½¹ÍÑ¥Ù•1…‰•°œè€Ñ¥Ù”ÉÕ±•Ìœ°4(€€€€€€ÕÁ‘…Ñ•ÍÙ…¥±…‰±•1…‰•°œè€]…¥Ñ¥¹œœ°4(€€€€€€É•™É•Í œè€I•™É•Í œ°4(€€€€€€Í½ÕÉ”œè€M½ÕÉ”œ°4(€€€€€€½¹¹•Ñ•œè€½¹¹•Ñ•œ°4(€€€€€€½™™±¥¹”œè€=™™±¥¹”œ°4(€€€€€€ÍÑ…±”œè€MÑ…±”‘…Ñ„œ°4(€€€€€€Á…ÉÑ¥…°œè€A…ÉÑ¥…°Íå¹¡É½¹¥é…Ñ¥½¸œ°4(€€€€€€±…ÍÑMå¹Œœè€1…ÍÐÍå¹ŒèíÙ…±Õ•ôœ°4(€€€€€€™…Ù½É¥Ñ•Ìœè€…Ù½É¥Ñ•Ìœ°4(€€€€€€ÅÕ¥­½¹ÑÉ½±Ìœè€EÕ¥¬½¹ÑÉ½±Ìœ°4(€€€€€€ÅÕ¥­½¹ÑÉ½±Í•ÍÉ¥ÁÑ¥½¸œè€e½ÕÈ•ÍÍ•¹Ñ¥…°…Ñ¥½¹Ì°…±Ý…åÌÝ¥Ñ¡¥¸É•… œ°4(€€€€€€ÕÍÑ½µ¥é”œè€ÕÍÑ½µ¥é”œ°4(€€€€€€¡½½Í••Ù¥•Ìœè€¡½½Í”‘•Ù¥•Ìœ°4(€€€€€€¹½EÕ¥­½¹ÑÉ½±ÍQ¥Ñ±”œè€	Õ¥±å½ÕÈÍ¡½ÉÑÕÑÌÁ…¹•°œ°4(€€€€€€…É•…Í=Ù•ÉÙ¥•Üœè€I½½µÌœ°4(€€€€€€…É•…Í=Ù•ÉÙ¥•Ý•ÍÉ¥ÁÑ¥½¸œè€Q¡”•ÍÍ•¹Ñ¥…±Ì…Ð„±…¹”œ°4(€€€€€€É½½µÍMÕµµ…Éäœè€íÉ½½µÍôÉ½½µÌƒ
Üí¥Ñ•µÍô¥Ñ•µÌœ°4(€€€€€€‘•Ù¥•ÍMÕµµ…Éäœè€í½¹±¥¹•ô½˜í…±±ô‘•Ù¥•Ì½¹±¥¹”œ°4(€€€€€€Í••±°œè€M•”…±°œ°4(€€€€€€¹½I½½µÍQ¥Ñ±”œè€9¼…Ñ¥Ù”É½½µÌœ°4(€€€€€€É••¹ÑÑ¥Ù¥Ñäœè€I••¹Ð…Ñ¥Ù¥Ñäœ°4(€€€€€€É••¹Ñ¡…¹•Ìœè€I••¹Ð¡…¹•Ìœ°4(€€€€€€É••¹Ñ¡…¹•Í•ÍÉ¥ÁÑ¥½¸œè€ÑÕ…°‘•Ù¥”ÍÑ…Ñ”¡…¹•Ìœ°4(€€€€€€¹½I••¹Ñ¡…¹•ÍQ¥Ñ±”œè€Ù•ÉåÑ¡¥¹œ¥Ì…±´œ°4(€€€€€€¹½I••¹Ñ¡…¹•Ìœè€9•Ü•Ù•¹ÑÌÝ¥±°…ÁÁ•…È¡•É”…ÕÑ½µ…Ñ¥…±±ä¸œ°4(€€€€€€…ÑÑ•¹Ñ¥½¹•¹Ñ•Èœè€9••‘Ì¡•­¥¹œœ°4(€€€€€€…ÑÑ•¹Ñ¥½¹=™™±¥¹•Q¥Ñ±”œè€Q¡”Í½ÕÉ”¥Ì½™™±¥¹”œ°4(€€€€€€…ÑÑ•¹Ñ¥½¹=™™±¥¹••ÍÉ¥ÁÑ¥½¸œè€Q…ÀÑ¼ÑÉä½¹¹•Ñ¥¹œ……¥¸¸œ°4(€€€€€€…ÑÑ•¹Ñ¥½¹Må¹Q¥Ñ±”œè€…Ñ„¹••‘ÌÉ•™É•Í¡¥¹œœ°4(€€€€€€…ÑÑ•¹Ñ¥½¹Må¹•ÍÉ¥ÁÑ¥½¸œè€Q¡”±…Ñ•ÍÐÍå¹¡É½¹¥é…Ñ¥½¸¥Ì¥¹½µÁ±•Ñ”¸œ°4(€€€€€€…ÑÑ•¹Ñ¥½¹ÅÕ…É¥ÕµQ¥Ñ±”œè€ÅÕ…É¥Õ´µ½‘Õ±”…±…É´œ°4(€€€€€€…ÑÑ•¹Ñ¥½¹ÅÕ…É¥Õµ•ÍÉ¥ÁÑ¥½¸œè€=Á•¸Ñ¡”…É…¹É•Ù¥•Ü…Ñ¥Ù”…±…ÉµÌ¸œ°4(€€€€€€…ÑÑ•¹Ñ¥½¹•Ù¥•ÍQ¥Ñ±”œè€íÙ…±Õ•ô‘•Ù¥•Ì…É”½™™±¥¹”œ°4(€€€€€€…ÑÑ•¹Ñ¥½¹•Ù¥•Í•ÍÉ¥ÁÑ¥½¸œè€¡•¬‘•Ù¥”Á½Ý•È…¹½¹¹•Ñ¥Ù¥Ñä¸œ°4(€€€€€€…ÑÑ•¹Ñ¥½¹UÁ‘…Ñ•ÍQ¥Ñ±”œè€íÙ…±Õ•ôÕÁ‘…Ñ•Ì…É”Ý…¥Ñ¥¹œœ°4(€€€€€€…ÑÑ•¹Ñ¥½¹UÁ‘…Ñ•Í•ÍÉ¥ÁÑ¥½¸œè4(€€€€€€€€€€A…­…•Ì…É”É•…‘ä™½ÈÍ•ÕÉ”¥¹ÍÑ…±±…Ñ¥½¸¸œ°4(€€€€€€…ÅÕ…É¥Õ´œè€ÅÕ…É¥Õ´œ°4(€€€€€€…ÅÕ…É¥Õµ!•…±Ñ¡äœè€A…É…µ•Ñ•ÉÌÍÑ…‰±”œ°4(€€€€€€…ÅÕ…É¥Õµ±…É´œè€9••‘Ì…ÑÑ•¹Ñ¥½¸œ°4(€€€€€€…ÅÕ…É¥Õµ9½±…ÉµÌœè€9¼…Ñ¥Ù”…±…ÉµÌœ°4(€€€€€€…ÅÕ…É¥Õµ=™™±¥¹”œè€1…Ñ•ÍÐÍ…Ù•É•…‘¥¹œœ°4(€€€€€€…ÅÕ…É¥Õµ%¹½µÁ±•Ñ”œè€%¹½µÁ±•Ñ”Í•Ð½˜É•…‘¥¹Ìœ°4(€€€€€€…ÅÕ…É¥ÕµMÑ…±”œè€I•…‘¥¹Ì¹••É•™É•Í¡¥¹œœ°4(€€€€€€Ý…Ñ•ÉQ•µÁ•É…ÑÕÉ”œè€]…Ñ•ÈÑ•µÁ•É…ÑÕÉ”œ°4(€€€€€€Ý…Ñ•É¡•µ¥ÍÑÉäœè€]…Ñ•È¡•µ¥ÍÑÉäœ°4(€€€€€€½¹¹•Ñ¥½¸œè€½¹¹•Ñ¥½¸œ°4(€€€€€€‘•Ñ…¥±Ìœè€•Ñ…¥±Ìœ°4(€€€€€€¹½ÅÕ…É¥Õ´œè€Q¡¥ÌÍ½ÕÉ”‘½•Ì¹½Ð•áÁ½Í”…¸…ÅÕ…É¥Õ´µ½‘Õ±”å•Ð¸œ°4(€€€€€€¹½ÅÕ…É¥ÕµQ¥Ñ±”œè€ÅÕ…É¥Õ´µ½‘Õ±”¥ÌÕ¹…Ù…¥±…‰±”œ°4(€€€€€€¹½…Ù½É¥Ñ•Ìœè€¡½½Í”Ñ¡”‘•Ù¥•Ì…¹…Ñ¥½¹Ìå½ÔÕÍ”µ½ÍÐ½™Ñ•¸¸œ°4(€€€€€€¹½É•…Ìœè€Q¡”Í½ÕÉ”É•ÑÕÉ¹•¹¼É½½µÌ¸œ°4(€€€€€€¹½•Ù¥•Ìœè€9¼‘•Ù¥•Ì™½Õ¹¸œ°4(€€€€€€¹½ÕÑ½µ…Ñ¥½¹Ìœè€Q¡¥ÌÍ½ÕÉ”¡…Ì¹¼…ÕÑ½µ…Ñ¥½¹Ì¸œ°4(€€€€€€Í•¹•Í¹‘MÉ¥ÁÑÌœè€M•¹•Ì…¹ÍÉ¥ÁÑÌœ°4(€€€€€€¹½UÁ‘…Ñ•Ìœè€9¼ÕÁ‘…Ñ•Ì…É”…Ù…¥±…‰±”¸œ°4(€€€€€€•¹Ñ¥Ñ¥•Í½Õ¹Ðœè€íÙ…±Õ•ô•¹Ñ¥Ñ¥•Ìœ°4(€€€€€€¥Ñ•µÍ½Õ¹Ðœè€íÙ…±Õ•ô¥Ñ•µÌœ°4(€€€€€€½¹±¥¹••Ù¥•Ìœè€íÙ…±Õ•ô‘•Ù¥•Ì½¹±¥¹”œ°4(€€€€€€…Ù…¥±…‰±”œè€Ù…¥±…‰±”œ°4(€€€€€€Õ¹…Ù…¥±…‰±”œè€U¹…Ù…¥±…‰±”œ°4(€€€€€€Õ¹­¹½Ý¸œè€U¹­¹½Ý¸œ°4(€€€€€€É•µ½Ù•œè€I•µ½Ù•œ°4(€€€€€€¹½…Ñ„œè€9¼‘…Ñ„œ°4(€€€€€€ÍÑ…Ñ•=¸œè€=¸œ°4(€€€€€€ÍÑ…Ñ•=™˜œè€=™˜œ°4(€€€€€€…Ñ¥Ù•¹Ñ¥Ñ¥•Ìœè€íÙ…±Õ•ô…Ñ¥Ù”œ°4(€€€€€€…Ñ¥Ù•9½Üœè€íÙ…±Õ•ô…Ñ¥Ù”¹½Üœ°4(€€€€€€É…ÝMÑ…Ñ•}¡•…Ðœè€!•…Ñ¥¹œœ°4(€€€€€€É…ÝMÑ…Ñ•}¡•…Ñ¥¹œœè€!•…Ñ¥¹œœ°4(€€€€€€É…ÝMÑ…Ñ•}½½±¥¹œœè€½½±¥¹œœ°4(€€€€€€É…ÝMÑ…Ñ•}Á±…å¥¹œœè€A±…å¥¹œœ°4(€€€€€€É…ÝMÑ…Ñ•}Á…ÕÍ•œè€A…ÕÍ•œ°4(€€€€€€É…ÝMÑ…Ñ•}¥‘±”œè€%‘±”œ°4(€€€€€€É…ÝMÑ…Ñ•}‘½­•œè€½­•œ°4(€€€€€€É…ÝMÑ…Ñ•}¡½µ”œè€!½µ”œ°4(€€€€€€É…ÝMÑ…Ñ•}…Ý…äœè€Ý…äœ°4(€€€€€€É…ÝMÑ…Ñ•}Á…ÉÑ±å±½Õ‘äœè€A…ÉÑ±ä±½Õ‘äœ°4(€€€€€€ÑÕÉ¹=¸œè€QÕÉ¸½¸œ°4(€€€€€€ÑÕÉ¹=™˜œè€QÕÉ¸½™˜œ°4(€€€€€€½Á•¹½Ù•Èœè€=Á•¸œ°4(€€€€€€±½Í•½Ù•Èœè€±½Í”œ°4(€€€€€€½Á•¹•œè€=Á•¸œ°4(€€€€€€±½Í•œè€±½Í•œ°4(€€€€€€±½­Ñ¥½¸œè€1½¬œ°4(€€€€€€Õ¹±½­Ñ¥½¸œè€U¹±½¬œ°4(€€€€€€±½­•œè€1½­•œ°4(€€€€€€Õ¹±½­•œè€U¹±½­•œ°4(€€€€€€…±…Éµ5½‘”œè€±…É´µ½‘”œ°4(€€€€€€…±…Éµ}‘¥Í…Éµ•œè€¥Í…Éµ•œ°4(€€€€€€…±…Éµ}…Éµ•‘}¡½µ”œè€Éµ•¡½µ”œ°4(€€€€€€…±…Éµ}…Éµ•‘}…Ý…äœè€Éµ•…Ý…äœ°4(€€€€€€…±…Éµ}…Éµ•‘}¹¥¡Ðœè€Éµ•¹¥¡Ðœ°4(€€€€€€ÍÑ…ÉÐœè€MÑ…ÉÐœ°4(€€€€€€É•ÑÕÉ¹Q½	…Í”œè€I•ÑÕÉ¸Ñ¼‰…Í”œ°4(€€€€€€Ñ•áÑY…±Õ”œè€Q•áÐÙ…±Õ”œ°4(€€€€€€ÉÕ¸œè€IÕ¸œ°4(€€€€€€Í•ÑY…±Õ”œè€M•ÐÙ…±Õ”èíÙ…±Õ•ôœ°4(€€€€€€Í•±•Ñ=ÁÑ¥½¸œè€M•±•Ð½ÁÑ¥½¸œ°4(€€€€€€¡¥ÍÑ½Éäœè€!¥ÍÑ½Éäœ°4(€€€€€€™…Ù½É¥Ñ”œè€…Ù½É¥Ñ”œ°4(€€€€€€É•µ½Ù•…Ù½É¥Ñ”œè€I•µ½Ù”™…Ù½É¥Ñ”œ°4(€€€€€€½¹™¥ÉµQ¥Ñ±”œè€½¹™¥É´½Á•É…Ñ¥½¸œ°4(€€€€€€½¹™¥ÉµI½ÕÑ¥¹”œè€Q¡”½µµ…¹Ý¥±°‰”Í•¹ÐÑ¼Ñ¡”‘•Ù¥”¸œ°4(€€€€€€½¹™¥Éµ½¹Í•ÅÕ•¹Ñ¥…°œè4(€€€€€€€€€€Q¡¥Ì½Á•É…Ñ¥½¸µ…ä¡…¹”Ñ¡”‘•Ù¥”‰•¡…Ù¥½È™½È…¸•áÑ•¹‘•Ñ¥µ”¸œ°4(€€€€€€½¹™¥ÉµÉ¥Ñ¥…°œè4(€€€€€€€€€€Q¡¥Ì¥Ì„É¥Ñ¥…°½Á•É…Ñ¥½¸¸Q¡”½¹ÑÉ½±±•ÈÝ¥±°ÍÑ¥±°Ù…±¥‘…Ñ”¥¹Ñ•É±½­Ì…¹µ…äÉ•©•Ð¥Ð¸œ°4(€€€€€€…¹•°œè€…¹•°œ°4(€€€€€€½¹™¥É´œè€½¹™¥É´œ°4(€€€€€€½µµ…¹‘A•¹‘¥¹œœè€]…¥Ñ¥¹œ™½È…­¹½Ý±•‘•µ•¹ÓŠ˜œ°4(€€€€€€•‘¥Ñ…Í¡‰½…Éœè€‘¥Ð‘…Í¡‰½…Éœ°4(€€€€€€É•Í•Ñ…Í¡‰½…Éœè€I•Í•Ð‘…Í¡‰½…Éœ°4(€€€€€€Ù¥Í¥‰±”œè€Y¥Í¥‰±”œ°4(€€€€€€±…É•…Éœè€1…É”…Éœ°4(€€€€€€Ñ¡•µ”œè€Q¡•µ”œ°4(€€€€€€Ñ¡•µ•MåÍÑ•´œè€MåÍÑ•´œ°4(€€€€€€Ñ¡•µ•1¥¡Ðœè€1¥¡Ðœ°4(€€€€€€Ñ¡•µ•…É¬œè€…É¬œ°4(€€€€€€±…¹Õ…”œè€1…¹Õ…”œ°4(€€€€€€Á½±¥Í œè€A½±Í­¤œ°4(€€€€€€•¹±¥Í œè€¹±¥Í œ°4(€€€€€€Í•ÕÉ¥Ñäœè€M•ÕÉ¥Ñäœ°4(€€€€€€‰¥½µ•ÑÉ¥AÉ½Ñ•Ñ¥½¸œè€	¥½µ•ÑÉ¥Ì™½ÈÉ¥Ñ¥…°½Á•É…Ñ¥½¹Ìœ°4(€€€€€€‰¥½µ•ÑÉ¥AÉ½Ñ•Ñ¥½¹•ÍÉ¥ÁÑ¥½¸œè4(€€€€€€€€€€I•ÅÕ¥É”™¥¹•ÉÁÉ¥¹Ð½È™…”É•½¹¥Ñ¥½¸‰•™½É”±½¬°…±…É´°…Ñ”…¹ÕÁ‘…Ñ”½Á•É…Ñ¥½¹Ì¸œ°4(€€€€€€‰¥½µ•ÑÉ¥U¹…Ù…¥±…‰±”œè4(€€€€€€€€€€9¼‰¥½µ•ÑÉ¥Œ…ÕÑ¡•¹Ñ¥…Ñ¥½¸¥Ì½¹™¥ÕÉ•½¸Ñ¡¥Ì‘•Ù¥”¸œ°4(€€€€€€‰¥½µ•ÑÉ¥¡•­…¥±•œè4(€€€€€€€€€€	¥½µ•ÑÉ¥Œ…Ù…¥±…‰¥±¥Ñä½Õ±¹½Ð‰”¡•­•¸É¥Ñ¥…°½Á•É…Ñ¥½¹ÌÉ•µ…¥¸‰±½­•¸œ°4(€€€€€€Í½ÕÉ•5…¹…•µ•¹Ðœè€M½ÕÉ”µ…¹…•µ•¹Ðœ°4(€€€€€€¡…%¹ÍÑ…¹•Ìœè€!½µ”ÍÍ¥ÍÑ…¹Ð¥¹ÍÑ…¹•Ìœ°4(€€€€€€…‘‘!…%¹ÍÑ…¹”œè€‘!½µ”ÍÍ¥ÍÑ…¹Ð¥¹ÍÑ…¹”œ°4(€€€€€€É•µ½Ù•!…%¹ÍÑ…¹”œè€I•µ½Ù”¥¹ÍÑ…¹”œ°4(€€€€€€É•µ½Ù•!…%¹ÍÑ…¹•½¹™¥É´œè4(€€€€€€€€€€AÉ½™¥±”ƒŠqíÙ…±Õ•÷Št°¥ÑÌÑ½­•¸…¹±½…°…¡”Ý¥±°‰”É•µ½Ù•™É½´Ñ¡¥Ì‘•Ù¥”¸œ°4(€€€€€€ÍÝ¥Ñ¡M½ÕÉ”œè€MÝ¥Ñ Í½ÕÉ”œ°4(€€€€€€É•µ½Ù•M½ÕÉ”œè€I•µ½Ù”Í½ÕÉ”…¹±½…°‘…Ñ„œ°4(€€€€€€É•µ½Ù•M½ÕÉ•½¹™¥É´œè4(€€€€€€€€€€Q¡”Ñ½­•¸°•ÉÑ¥™¥…Ñ”™¥¹•ÉÁÉ¥¹Ð°Í•ÍÍ¥½¸…¹Í½ÕÉ”…¡”Ý¥±°‰”É•µ½Ù•™É½´Ñ¡¥Ì‘•Ù¥”¸œ°4(€€€€€€‘•µ½M•¹…É¥½Ìœè€•µ¼Í•¹…É¥½Ìœ°4(€€€€€€Í¥µÕ±…Ñ•=™™±¥¹”œè€M¥µÕ±…Ñ”½™™±¥¹”œ°4(€€€€€€Í¥µÕ±…Ñ•±…É´œè€M¥µÕ±…Ñ”…ÅÕ…É¥Õ´…±…É´œ°4(€€€€€€…ÁÁUÁ‘…Ñ”œè€!½µ”½¹ÑÉ½°ÕÁ‘…Ñ”œ°4(€€€€€€‘•Ù¥•UÁ‘…Ñ•Ìœè€•Ù¥”ÕÁ‘…Ñ•Ìœ°4(€€€€€€ÕÉÉ•¹ÑY•ÉÍ¥½¸œè€ÕÉÉ•¹ÐèíÙ…±Õ•ôœ°4(€€€€€€±…Ñ•ÍÑY•ÉÍ¥½¸œè€1…Ñ•ÍÐèíÙ…±Õ•ôœ°4(€€€€€€¥¹ÍÑ…±°œè€%¹ÍÑ…±°œ°4(€€€€€€ÕÁQ½…Ñ”œè€UÀÑ¼‘…Ñ”œ°4(€€€€€€Õ¹ÍÕÁÁ½ÉÑ•œè€9½ÐÍ…™•±äÍÕÁÁ½ÉÑ•å•Ðœ°4(€€€€€€µ…¹‘…Ñ½Éäœè€I•ÅÕ¥É•œ°4(€€€€€€…Ù…¥±…‰±•Y•ÉÍ¥½¸œè€Y•ÉÍ¥½¸íÙ…±Õ•ô¥Ì…Ù…¥±…‰±”œ°4(€€€€€€±…Ñ•Èœè€1…Ñ•Èœ°4(€€€€€€‘½Ý¹±½…‘¹‘%¹ÍÑ…±°œè€½Ý¹±½……¹¥¹ÍÑ…±°œ°4(€€€€€€½¹Ñ¥¹Õ•%¹ÍÑ…±±…Ñ¥½¸œè€½¹Ñ¥¹Õ”¥¹ÍÑ…±±…Ñ¥½¸œ°4(€€€€€€½Ñ…Ù…¥±…‰±•5•ÍÍ…”œè4(€€€€€€€€€€Q¡¥ÌÕÁ‘…Ñ”½¹Ñ…¥¹Ì™¥á•Ì…¹¥µÁÉ½Ù•µ•¹ÑÌ¸Q¡”Á…­…”Ý¥±°‰”Ù•É¥™¥•‰•™½É”¥¹ÍÑ…±±…Ñ¥½¸¸œ°4(€€€€€€½Ñ…½Ý¹±½…‘¥¹5•ÍÍ…”œè4(€€€€€€€€€€½Ý¹±½…‘¥¹œÑ¡”Í¥¹•Á…­…”…¹Ù•É¥™å¥¹œ¥ÑÌM!´ÈÔØ‘¥•ÍÐ¸œ°4(€€€€€€½Ñ…Y•É¥™å¥¹5•ÍÍ…”œè4(€€€€€€€€€€Y•É¥™å¥¹œÁ…­…”¥‘•¹Ñ¥Ñä°Ù•ÉÍ¥½¸…¹Í¥¹¥¹œ•ÉÑ¥™¥…Ñ”¸œ°4(€€€€€€½Ñ…A•Éµ¥ÍÍ¥½¹5•ÍÍ…”œè4(€€€€€€€€€€¹‘É½¥½Á•¹•Ñ¡”¥¹ÍÑ…±°µÍ½ÕÉ”Í•ÑÑ¥¹Ì¸±±½Ü!½µ”½¹ÑÉ½°°É•ÑÕÉ¸Ñ¼Ñ¡”…ÁÀ…¹½¹Ñ¥¹Õ”¸œ°4(€€€€€€½Ñ……¥±•‘5•ÍÍ…”œè€Q¡”ÕÁ‘…Ñ”Ý…Ì¹½Ð¥¹ÍÑ…±±•¸œ°4(€€€€€€½Ñ…AÉ•Á…É¥¹5•ÍÍ…”œè€AÉ•Á…É¥¹œÑ¡”Í•ÕÉ”…ÁÁ±¥…Ñ¥½¸ÕÁ‘…Ñ”¸œ°4(€€€€€€‘¥…¹½ÍÑ¥Ìœè€¥…¹½ÍÑ¥Ìœ°4(€€€€€€•¹Ñ¥Ñ¥•Ìœè€¹Ñ¥Ñ¥•Ìœ°4(€€€€€€±…ÍÑMå¹1…‰•°œè€1…ÍÐÍå¹¡É½¹¥é…Ñ¥½¸œ°4(€€€€€€±½…±…¡”œè€1½…°…¡”œ°4(€€€€€€•¹ÉåÁÑ•œè€¹ÉåÁÑ•œ°4(€€€€€€ÁÉ¥Ù…å¹‘‰½ÕÐœè€AÉ¥Ù…ä…¹…‰½ÕÐœ°4(€€€€€€ÁÉ¥Ù…äœè€AÉ¥Ù…äœ°4(€€€€€€ÁÉ¥Ù…å•ÍÉ¥ÁÑ¥½¸œè4(€€€€€€€€€€!½µ”½¹ÑÉ½°‘½•Ì¹½ÐÍ•±°‘…Ñ„¸É•‘•¹Ñ¥…±Ì…¹Ñ¡”±…Ñ•ÍÐÍ¹…ÁÍ¡½ÐÉ•µ…¥¸¥¸Ñ¡”‘•Ù¥”Í•ÕÉ”ÍÑ½É…”¸œ°4(€€€€€€…ÁÁY•ÉÍ¥½¸œè€Y•ÉÍ¥½¸íÙ…±Õ•ôœ°4(€€€€€€½Á•¹M½ÕÉ•1¥•¹Í•Ìœè€=Á•¸µÍ½ÕÉ”±¥•¹Í•Ìœ°4(€€€€€€…ÑÑÉ¥‰ÕÑ•Ìœè€M½ÕÉ”…ÑÑÉ¥‰ÕÑ•Ìœ°4(€€€€€€•¹Ñ¥ÑåM½ÕÉ”œè€M½ÕÉ”èíÙ…±Õ•ôœ°4(€€€€€€•¹Ñ¥ÑåUÁ‘…Ñ•œè€UÁ‘…Ñ•èíÙ…±Õ•ôœ°4(€€€€€€™¥ÉµÝ…É”œè€¥ÉµÝ…É”œ°4(€€€€€€µ…¹Õ™…ÑÕÉ•Èœè€5…¹Õ™…ÑÕÉ•Èœ°4(€€€€€€µ½‘•°œè€5½‘•°œ°4(€€€€€€±…ÍÑM••¸œè€1…ÍÐÍ••¸èíÙ…±Õ•ôœ°4(€€€€€€Í•…É œè€M•…É œ°4(€€€€€€Í•…É¡!¥¹Ðœè€•Ù¥”°•¹Ñ¥Ñä½ÈÉ½½´œ°4(€€€€€€¹½I•ÍÕ±ÑÌœè€9¼É•ÍÕ±ÑÌ™½ÈÑ¡¥ÌÅÕ•Éä¸œ°4(€€€€€€¹½¹Ñ¥Ñ¥•Í%¹É•„œè€Q¡¥ÌÉ½½´¡…Ì¹¼•¹Ñ¥Ñ¥•Ì¸œ°4(€€€€€€…±°œè€±°œ°4(€€€€€€Õ¹­¹½Ý¹¹Ñ¥Ñå!¥¹Ðœè4(€€€€€€€€€€™ÕÑÕÉ”½ÈÕÍÑ½´ÑåÁ”¥Ì‘¥ÍÁ±…å•Ý¥Ñ¡½ÕÐÕ•ÍÍ¥¹œ¡½ÜÑ¼½¹ÑÉ½°¥Ð¸œ°4(€€€€€€•ÉÉ½ÉQ¥Ñ±”œè€Q¡”½Á•É…Ñ¥½¸½Õ±¹½Ð‰”½µÁ±•Ñ•œ°4(€€€€€€•ÉÉ½É9•ÑÝ½É¬œè4(€€€€€€€€€€Q¡”Í½ÕÉ”¥ÌÕ¹É•…¡…‰±”¸¡•¬å½ÕÈ18°YA8½È%¹Ñ•É¹•Ð½¹¹•Ñ¥½¸¸œ°4(€€€€€€•ÉÉ½É=™™±¥¹”œè4(€€€€€€€€€€Q¡•É”¥Ì¹¼½¹¹•Ñ¥½¸¸1…ÍÐ­¹½Ý¸‘…Ñ„É•µ…¥¹ÌÉ•…µ½¹±ä¸œ°4(€€€€€€•ÉÉ½ÉQ½­•¸œè4(€€€€€€€€€€Q¡”Ñ½­•¸•áÁ¥É•°Ý…ÌÉ•©•Ñ•½È±…­ÌÑ¡”É•ÅÕ¥É•Á•Éµ¥ÍÍ¥½¹Ì¸œ°4(€€€€€€•ÉÉ½ÉM•ÉÙ•Èœè€Q¡”Í•ÉÙ•ÈÉ•ÑÕÉ¹•…¸•ÉÉ½È¸QÉä……¥¸Í¡½ÉÑ±ä¸œ°4(€€€€€€•ÉÉ½É%¹Ù…±¥‘I•ÍÁ½¹Í”œè4(€€€€€€€€€€Q¡”Í½ÕÉ”É•ÑÕÉ¹•‘…µ…•½ÈÕ¹ÍÕÁÁ½ÉÑ•‘…Ñ„¸œ°4(€€€€€€•ÉÉ½É•ÉÑ¥™¥…Ñ”œè4(€€€€€€€€€€Q¡”•ÉÑ¥™¥…Ñ”½È™¥¹•ÉÁÉ¥¹Ð¡…¹•¸½¹™¥É´¥‘•¹Ñ¥Ñä½¸„ÑÉÕÍÑ•‘¥ÍÁ±…ä¸œ°4(€€€€€€•ÉÉ½ÉMÑ½É…”œè€Q¡”Í•ÕÉ”ÍÑ½É…”½Õ±¹½Ð‰”½Á•¹•½ÈÕÁ‘…Ñ•¸œ°4(€€€€€€•ÉÉ½ÉA•Éµ¥ÍÍ¥½¸œè4(€€€€€€€€€€Q¡¥Ì½Á•É…Ñ¥½¸¥Ì¹½ÐÁ•Éµ¥ÑÑ•™½ÈÑ¡”Í•±•Ñ••¹Ñ¥Ñä¸œ°4(€€€€€€•ÉÉ½ÉU¹ÍÕÁÁ½ÉÑ•œè4(€€€€€€€€€€Q¡”Í½ÕÉ”‘½•Ì¹½ÐÍ…™•±äÍÕÁÁ½ÉÐÑ¡¥Ì™•…ÑÕÉ”å•Ð¸œ°4(€€€€€€•ÉÉ½É%¹Ù…±¥‘Y…±Õ”œè4(€€€€€€€€€€Q¡”Ù…±Õ”¥Ì½ÕÑÍ¥‘”¥ÑÌ…±±½Ý•É…¹”½È¡…Ì…¸¥¹Ù…±¥™½Éµ…Ð¸œ°4(€€€€€€•ÉÉ½É%¹Ù…±¥‘É•‘•¹Ñ¥…±Ìœè4(€€€€€€€€€€¡•¬Ñ¡”…‘‘É•ÍÌ…¹Á…ÍÑ”Ñ¡”½µÁ±•Ñ”…•ÍÌÑ½­•¸¸œ°4(€€€€€€•ÉÉ½É%¹Ù…±¥‘!…UÉ°œè€¹Ñ•ÈÑ¡”™Õ±°!QQ@½È!QQAL¥¹ÍÑ…¹”…‘‘É•ÍÌ¸œ°4(€€€€€€•ÉÉ½É%¹Ù…±¥‘!…Q½­•¸œè€Q¡”Ñ½­•¸µÕÍÐ½¹Ñ…¥¸…Ð±•…ÍÐ€ÈÀ¡…É…Ñ•ÉÌ¸œ°4(€€€€€€•ÉÉ½É½µµ…¹‘U¹…Ù…¥±…‰±”œè4(€€€€€€€€€€½¹ÑÉ½°¥Ì±½­•Ý¡¥±”Ñ¡”Í½ÕÉ”½È•¹Ñ¥Ñä¥ÌÕ¹…Ù…¥±…‰±”¸œ°4(€€€€€€•ÉÉ½É	¥½µ•ÑÉ¥…¹•±±•œè4(€€€€€€€€€€	¥½µ•ÑÉ¥Œ…ÕÑ¡½É¥é…Ñ¥½¸Ý…Ì…¹•±±•¸Q¡”½Á•É…Ñ¥½¸Ý…Ì¹½ÐÍ•¹Ð¸œ°4(€€€€€€•ÉÉ½É	¥½µ•ÑÉ¥U¹…Ù…¥±…‰±”œè4(€€€€€€€€€€	¥½µ•ÑÉ¥Ì…É”Õ¹…Ù…¥±…‰±”¸½¹™¥ÕÉ”Ñ¡•´¥¸Ñ¡”½Á•É…Ñ¥¹œÍåÍÑ•´Ñ¼½¹Ñ¥¹Õ”¸œ°4(€€€€€€•ÉÉ½É	¥½µ•ÑÉ¥1½­•œè4(€€€€€€€€€€	¥½µ•ÑÉ¥Ì…É”Ñ•µÁ½É…É¥±ä±½­•¸U¹±½¬Ñ¡•´¥¸Ñ¡”½Á•É…Ñ¥¹œÍåÍÑ•´…¹É•ÑÉä¸œ°4(€€€€€€•ÉÉ½É	¥½µ•ÑÉ¥…¥±•œè4(€€€€€€€€€€%‘•¹Ñ¥Ñä½Õ±¹½Ð‰”½¹™¥Éµ•¸Q¡”É¥Ñ¥…°½Á•É…Ñ¥½¸É•µ…¥¹Ì‰±½­•¸œ°4(€€€€€€•ÉÉ½ÉU¹­¹½Ý¸œè4(€€€€€€€€€€¸Õ¹•áÁ•Ñ••ÉÉ½È½ÕÉÉ•¸M•¹Í¥Ñ¥Ù”‘…Ñ„Ý…Ì¹½ÐÉ•½É‘•¥¸±½Ì¸œ°4(€€€€€€É•ÑÉäœè€QÉä……¥¸œ°4(€€€€€€É•½¹™¥ÕÉ”œè€¡½½Í”…¹½Ñ¡•ÈÍ½ÕÉ”œ°4(€€€€€€‘¥Íµ¥ÍÌœè€¥Íµ¥ÍÌœ°4(€€€€€€¹½Ñ¥•I•™É•Í¡•œè€…Ñ„É•™É•Í¡•¸œ°4(€€€€€€¹½Ñ¥•½µµ…¹‘•ÁÑ•œè€Q¡”Í½ÕÉ”…­¹½Ý±•‘•Ñ¡”½µµ…¹¸œ°4(€€€€€€¹½Ñ¥•UÁ‘…Ñ•MÑ…ÉÑ•œè€Q¡”ÕÁ‘…Ñ”ÁÉ½•ÍÌ¡…ÌÍÑ…ÉÑ•¸œ°4(€€€€€€¹½Ñ¥•	¥½µ•ÑÉ¥¹…‰±•œè4(€€€€€€€€€€	¥½µ•ÑÉ¥ŒÁÉ½Ñ•Ñ¥½¸¥Ì•¹…‰±•™½ÈÉ¥Ñ¥…°½Á•É…Ñ¥½¹Ì¸œ°4(€€€€€€¹½Ñ¥•	¥½µ•ÑÉ¥¥Í…‰±•œè4(€€€€€€€€€€	¥½µ•ÑÉ¥ŒÁÉ½Ñ•Ñ¥½¸¥Ì‘¥Í…‰±•™½ÈÉ¥Ñ¥…°½Á•É…Ñ¥½¹Ì¸œ°4(€€€€€€©ÕÍÑ9½Üœè€©ÕÍÐ¹½Üœ°4(€€€€€€Í•½¹‘Í¼œè€íÙ…±Õ•ôÍ•Œ…¼œ°4(€€€€€€µ¥¹ÕÑ•Í¼œè€íÙ…±Õ•ôµ¥¸…¼œ°4(€€€€€€¡½ÕÉÍ¼œè€íÙ…±Õ•ô¡È…¼œ°4(€€€€€€¹•Ù•Èœè€¹•Ù•Èœ°4(€€€€€€¡¥ÍÑ½ÉåµÁÑäœè€Q¡”Í½ÕÉ”É•ÑÕÉ¹•¹¼Í…µÁ±•Ì™½ÈÑ¡¥ÌÁ•É¥½¸œ°4(€€€€€€¡¥ÍÑ½ÉåAÉ½µÁÐœè€¡½½Í”„Á•É¥½Ñ¼±½…¡¥ÍÑ½Éä¸œ°4(€€€€€€¡¥ÍÑ½ÉåMÕµµ…Éäœè4(€€€€€€€€€€!¥ÍÑ½Éä¡…ÉÐ¸5¥¹¥µÕ´íµ¥¹¥µÕµô°µ…á¥µÕ´íµ…á¥µÕµô°íÍ…µÁ±•ÍôÍ…µÁ±•Ì¸œ°4(€€€€€€¡¥ÍÑ½ÉäÈÑ œè€œÈÐ¡½ÕÉÌœ°4(€€€€€€¡¥ÍÑ½ÉäÝœè€œÜ‘…åÌœ°4(€€€€€€•¹Ñ¥Ñå}±¥¡Ðœè€1¥¡Ðœ°4(€€€€€€•¹Ñ¥Ñå}ÍÝ¥Ñ¡¹Ñ¥Ñäœè€MÝ¥Ñ œ°4(€€€€€€•¹Ñ¥Ñå}Í•¹Í½Èœè€M•¹Í½Èœ°4(€€€€€€•¹Ñ¥Ñå}‰¥¹…ÉåM•¹Í½Èœè€	¥¹…ÉäÍ•¹Í½Èœ°4(€€€€€€•¹Ñ¥Ñå}±¥µ…Ñ”œè€±¥µ…Ñ”œ°4(€€€€€€•¹Ñ¥Ñå}½Ù•Èœè€½Ù•Èœ°4(€€€€€€•¹Ñ¥Ñå}±½¬œè€1½¬œ°4(€€€€€€•¹Ñ¥Ñå}…±…Éµ½¹ÑÉ½±A…¹•°œè€±…É´œ°4(€€€€€€•¹Ñ¥Ñå}…µ•É„œè€…µ•É„œ°4(€€€€€€•¹Ñ¥Ñå}µ•‘¥…A±…å•Èœè€5•‘¥„Á±…å•Èœ°4(€€€€€€•¹Ñ¥Ñå}™…¸œè€…¸œ°4(€€€€€€•¹Ñ¥Ñå}Ù…ÕÕ´œè€Y…ÕÕ´œ°4(€€€€€€•¹Ñ¥Ñå}Ý•…Ñ¡•Èœè€]•…Ñ¡•Èœ°4(€€€€€€•¹Ñ¥Ñå}Á•ÉÍ½¸œè€A•ÉÍ½¸œ°4(€€€€€€•¹Ñ¥Ñå}‘•Ù¥•QÉ…­•Èœè€QÉ…­•Èœ°4(€€€€€€•¹Ñ¥Ñå}Í•¹”œè€M•¹”œ°4(€€€€€€•¹Ñ¥Ñå}ÍÉ¥ÁÐœè€MÉ¥ÁÐœ°4(€€€€€€•¹Ñ¥Ñå}…ÕÑ½µ…Ñ¥½¸œè€ÕÑ½µ…Ñ¥½¸œ°4(€€€€€€•¹Ñ¥Ñå}‰ÕÑÑ½¸œè€	ÕÑÑ½¸œ°4(€€€€€€•¹Ñ¥Ñå}¥¹ÁÕÑ	ÕÑÑ½¸œè€%¹ÁÕÐ‰ÕÑÑ½¸œ°4(€€€€€€•¹Ñ¥Ñå}¹Õµ‰•Èœè€9Õµ‰•Èœ°4(€€€€€€•¹Ñ¥Ñå}¥¹ÁÕÑ9Õµ‰•Èœè€%¹ÁÕÐ¹Õµ‰•Èœ°4(€€€€€€•¹Ñ¥Ñå}Í•±•Ðœè€M•±•Ðœ°4(€€€€€€•¹Ñ¥Ñå}¥¹ÁÕÑM•±•Ðœè€%¹ÁÕÐÍ•±•Ðœ°4(€€€€€€•¹Ñ¥Ñå}Ñ•áÐœè€Q•áÐœ°4(€€€€€€•¹Ñ¥Ñå}¥¹ÁÕÑQ•áÐœè€%¹ÁÕÐÑ•áÐœ°4(€€€€€€•¹Ñ¥Ñå}ÕÁ‘…Ñ”œè€UÁ‘…Ñ”œ°4(€€€€€€•¹Ñ¥Ñå}Õ¹­¹½Ý¸œè€ÕÍÑ½´ÑåÁ”œ°4(€€€ô°4(€ôì4)ô4(4)™¥¹…°±…ÍÌ}!½µ•½¹ÑÉ½±MÑÉ¥¹Í•±•…Ñ”4(€€€•áÑ•¹‘Ì1½…±¥é…Ñ¥½¹Í•±•…Ñ”ñ!½µ•½¹ÑÉ½±MÑÉ¥¹Ìøì4(€½¹ÍÐ}!½µ•½¹ÑÉ½±MÑÉ¥¹Í•±•…Ñ” ¤ì4(4(€½Ù•ÉÉ¥‘”4(€‰½½°¥ÍMÕÁÁ½ÉÑ•¡1½…±”±½…±”¤€ôø4(€€€€€€ñMÑÉ¥¹œùìÁ°œ°€•¸ô¹½¹Ñ…¥¹Ì¡±½…±”¹±…¹Õ…•½‘”¤ì4(4(€½Ù•ÉÉ¥‘”4(€ÕÑÕÉ”ñ!½µ•½¹ÑÉ½±MÑÉ¥¹Ìø±½…¡1½…±”±½…±”¤€ôø4(€€€€€Må¹¡É½¹½ÕÍÕÑÕÉ”ñ!½µ•½¹ÑÉ½±MÑÉ¥¹Ìø¡!½µ•½¹ÑÉ½±MÑÉ¥¹Ì¡±½…±”¤¤ì4(4(€½Ù•ÉÉ¥‘”4(€‰½½°Í¡½Õ±‘I•±½…¡½Ù…É¥…¹Ð1½…±¥é…Ñ¥½¹Í•±•…Ñ”ñ!½µ•½¹ÑÉ½±MÑÉ¥¹Ìø½±¤€ôø4(€€€€€™…±Í”ì4)ô4