import 'dart:async';

import 'package:flutter/foundation.dart';

import '../data/credentials_store.dart';
import '../data/home_assistant_api.dart';
import '../data/home_assistant_socket.dart';
import '../domain/entity_ids.dart';
import '../domain/models.dart';

enum AquaAppPhase { loading, setup, connecting, ready, failure }

typedef HomeAssistantApiFactory =
    HomeAssistantApi Function(HomeAssistantCredentials credentials);
typedef HomeAssistantSocketFactory =
    HomeAssistantSocket Function(HomeAssistantCredentials credentials);

final class AquaCydController extends ChangeNotifier {
  AquaCydController({
    required CredentialsStore credentialsStore,
    HomeAssistantApiFactory? apiFactory,
    HomeAssistantSocketFactory? socketFactory,
    bool enableRealtime = true,
  }) : _credentialsStore = credentialsStore,
       _apiFactory = apiFactory ?? HomeAssistantApi.new,
       _socketFactory = socketFactory ?? HomeAssistantSocket.new,
       _enableRealtime = enableRealtime;

  final CredentialsStore _credentialsStore;
  final HomeAssistantApiFactory _apiFactory;
  final HomeAssistantSocketFactory _socketFactory;
  final bool _enableRealtime;

  AquaAppPhase _phase = AquaAppPhase.loading;
  HomeAssistantCredentials? _credentials;
  HomeAssistantConfig? _config;
  HomeAssistantApi? _api;
  HomeAssistantSocket? _socket;
  StreamSubscription<HaEntityState>? _stateSubscription;
  StreamSubscription<HomeAssistantSocketStatus>? _statusSubscription;
  HomeAssistantSocketStatus _socketStatus =
      HomeAssistantSocketStatus.disconnected;
  Map<String, HaEntityState> _entities = <String, HaEntityState>{};
  List<HistorySample> _history = const <HistorySample>[];
  String? _historyEntityId;
  Duration? _historyPeriod;
  String? _errorMessage;
  String? _operationMessage;
  final Set<String> _busyOperations = <String>{};
  var _initialized = false;
  var _disposed = false;
  var _refreshing = false;
  var _loadingHistory = false;

  AquaAppPhase get phase => _phase;
  HomeAssistantCredentials? get credentials => _credentials;
  HomeAssistantConfig? get config => _config;
  HomeAssistantSocketStatus get socketStatus => _socketStatus;
  Map<String, HaEntityState> get entities =>
      Map<String, HaEntityState>.unmodifiable(_entities);
  AquariumSnapshot get snapshot => AquariumSnapshot.fromEntities(_entities);
  List<HistorySample> get history => _history;
  String? get historyEntityId => _historyEntityId;
  Duration? get historyPeriod => _historyPeriod;
  String? get errorMessage => _errorMessage;
  String? get operationMessage => _operationMessage;
  bool get refreshing => _refreshing;
  bool get loadingHistory => _loadingHistory;
  bool get hasStoredConnection => _credentials != null;

  bool isBusy(String operation) => _busyOperations.contains(operation);

  Future<void> initialize() async {
    if (_initialized || _disposed) {
      return;
    }
    _initialized = true;
    _setPhase(AquaAppPhase.loading);
    final HomeAssistantCredentials? stored;
    try {
      stored = await _credentialsStore.load();
    } on Object {
      if (!_disposed) {
        _errorMessage =
            'Nie udało się odczytać bezpiecznej konfiguracji aplikacji.';
        _setPhase(AquaAppPhase.setup);
      }
      return;
    }
    if (_disposed) {
      return;
    }
    if (stored == null) {
      _setPhase(AquaAppPhase.setup);
      return;
    }
    _credentials = stored;
    try {
      await _establishConnection(stored, persist: false);
    } on HomeAssistantFailure catch (error) {
      if (!_disposed) {
        _errorMessage = error.message;
        _setPhase(AquaAppPhase.failure);
      }
    } on Object {
      if (!_disposed) {
        _errorMessage =
            'Nie udało się uruchomić połączenia z Home Assistantem.';
        _setPhase(AquaAppPhase.failure);
      }
    }
  }

  Future<bool> configure({
    required String baseUrl,
    required String accessToken,
  }) async {
    if (_busyOperations.contains('configure')) {
      return false;
    }
    final HomeAssistantCredentials parsed;
    try {
      parsed = HomeAssistantCredentials.parse(
        baseUrl: baseUrl,
        accessToken: accessToken,
      );
    } on FormatException catch (error) {
      _errorMessage = error.message;
      notifyListeners();
      return false;
    }

    _busyOperations.add('configure');
    _errorMessage = null;
    _operationMessage = null;
    _setPhase(AquaAppPhase.connecting);
    try {
      await _establishConnection(parsed, persist: true);
      _operationMessage = 'Połączono z Home Assistantem.';
      return true;
    } on HomeAssistantFailure catch (error) {
      _errorMessage = error.message;
      _setPhase(AquaAppPhase.setup);
      return false;
    } on Object {
      _errorMessage =
          'Nie udało się bezpiecznie zapisać konfiguracji połączenia.';
      _setPhase(AquaAppPhase.setup);
      return false;
    } finally {
      _busyOperations.remove('configure');
      _notify();
    }
  }

  Future<bool> retryConnection() async {
    final current = _credentials;
    if (current == null) {
      _setPhase(AquaAppPhase.setup);
      return false;
    }
    try {
      await _establishConnection(current, persist: false);
      return true;
    } on HomeAssistantFailure catch (error) {
      _errorMessage = error.message;
      _setPhase(AquaAppPhase.failure);
      return false;
    } on Object {
      _errorMessage = 'Nie udało się wznowić połączenia.';
      _setPhase(AquaAppPhase.failure);
      return false;
    }
  }

  void beginReconfiguration() {
    _errorMessage = null;
    _operationMessage = null;
    _setPhase(AquaAppPhase.setup);
  }

  void cancelReconfiguration() {
    if (_credentials != null && _api != null) {
      _setPhase(AquaAppPhase.ready);
    }
  }

  Future<void> logout() async {
    _busyOperations.add('logout');
    _notify();
    try {
      await _credentialsStore.clear();
      await _disposeConnection();
      _credentials = null;
      _config = null;
      _entities = <String, HaEntityState>{};
      _history = const <HistorySample>[];
      _historyEntityId = null;
      _historyPeriod = null;
      _errorMessage = null;
      _operationMessage = null;
      _setPhase(AquaAppPhase.setup);
    } finally {
      _busyOperations.remove('logout');
      _notify();
    }
  }

  Future<bool> refresh({bool showMessage = false}) async {
    final api = _api;
    if (api == null || _refreshing) {
      return false;
    }
    _refreshing = true;
    _errorMessage = null;
    _notify();
    try {
      _entities = await api.fetchAquaStates();
      if (showMessage) {
        _operationMessage = 'Dane zostały odświeżone.';
      }
      return true;
    } on HomeAssistantFailure catch (error) {
      _errorMessage = error.message;
      return false;
    } finally {
      _refreshing = false;
      _notify();
    }
  }

  Future<bool> setOutput({
    required String operation,
    required String script,
    required int overrideMinutes,
  }) {
    if (overrideMinutes < 1 || overrideMinutes > 1440) {
      throw ArgumentError.value(
        overrideMinutes,
        'overrideMinutes',
        'Dozwolony zakres wynosi od 1 do 1440 minut.',
      );
    }
    return _perform(operation, () async {
      final api = _requireApi();
      await api.callService('input_number', 'set_value', <String, Object?>{
        'entity_id': 'input_number.aquacyd_override_minutes',
        'value': overrideMinutes,
      });
      await api.callScript(script);
      await _refreshAfterCommand();
    });
  }

  Future<bool> startFeeding(int minutes) {
    if (minutes < 1 || minutes > 60) {
      throw ArgumentError.value(
        minutes,
        'minutes',
        'Dozwolony zakres wynosi od 1 do 60 minut.',
      );
    }
    return _perform('feeding', () async {
      final api = _requireApi();
      await api.callService('input_number', 'set_value', <String, Object?>{
        'entity_id': 'input_number.aquacyd_feeding_minutes',
        'value': minutes,
      });
      await api.callScript(AquaScripts.startFeeding);
      await _refreshAfterCommand();
    });
  }

  Future<bool> requestSnapshot() {
    return _perform('snapshot', () async {
      await _requireApi().callScript(AquaScripts.requestSnapshot);
      await _refreshAfterCommand();
    });
  }

  Future<bool> saveSchedule(AquaSchedule schedule) {
    const modes = <String>['schedule', 'always_on', 'always_off'];
    const profiles = <String>['auto', 'day', 'daybreak', 'night'];
    if (!AquaEntityIds.scheduleTargets.contains(schedule.target) ||
        schedule.mode < 0 ||
        schedule.mode >= modes.length ||
        schedule.profile < 0 ||
        schedule.profile >= profiles.length ||
        schedule.startMinute < 0 ||
        schedule.startMinute > 1439 ||
        schedule.endMinute < 0 ||
        schedule.endMinute > 1439) {
      throw ArgumentError.value(
        schedule,
        'schedule',
        'Nieprawidłowy harmonogram',
      );
    }
    return _perform('schedule_${schedule.target}', () async {
      await _requireApi()
          .callScript(AquaScripts.saveSchedule, <String, Object?>{
            'schedule_target': schedule.target,
            'schedule_mode': modes[schedule.mode],
            'schedule_profile': profiles[schedule.profile],
            'schedule_start': schedule.startText,
            'schedule_end': schedule.endText,
          });
      await _refreshAfterCommand();
    });
  }

  Future<bool> saveThermostat({
    required bool enabled,
    required double targetTemperature,
    required double hysteresis,
  }) {
    if (!targetTemperature.isFinite ||
        targetTemperature < 18 ||
        targetTemperature > 30 ||
        !hysteresis.isFinite ||
        hysteresis < 0.1 ||
        hysteresis > 5) {
      throw ArgumentError('Nieprawidłowe parametry termostatu.');
    }
    return _perform('thermostat', () async {
      await _requireApi().callScript(
        AquaScripts.saveThermostat,
        <String, Object?>{
          'heater_mode': enabled ? 'threshold' : 'off',
          'target_temperature': double.parse(
            targetTemperature.toStringAsFixed(1),
          ),
          'temperature_hysteresis': double.parse(hysteresis.toStringAsFixed(1)),
        },
      );
      await _refreshAfterCommand();
    });
  }

  Future<bool> loadHistory(String entityId, Duration period) async {
    if (_loadingHistory) {
      return false;
    }
    _loadingHistory = true;
    _historyEntityId = entityId;
    _historyPeriod = period;
    _errorMessage = null;
    _notify();
    try {
      _history = await _requireApi().fetchHistory(entityId, period);
      return true;
    } on HomeAssistantFailure catch (error) {
      _history = const <HistorySample>[];
      _errorMessage = error.message;
      return false;
    } finally {
      _loadingHistory = false;
      _notify();
    }
  }

  String? takeOperationMessage() {
    final message = _operationMessage;
    _operationMessage = null;
    return message;
  }

  void clearError() {
    if (_errorMessage != null) {
      _errorMessage = null;
      _notify();
    }
  }

  @override
  void dispose() {
    if (_disposed) {
      return;
    }
    _disposed = true;
    unawaited(_disposeConnection());
    super.dispose();
  }

  Future<void> _establishConnection(
    HomeAssistantCredentials credentials, {
    required bool persist,
  }) async {
    _errorMessage = null;
    _setPhase(AquaAppPhase.connecting);
    final api = _apiFactory(credentials);
    try {
      final config = await api.fetchConfig();
      final states = await api.fetchAquaStates();
      if (persist) {
        await _credentialsStore.save(credentials);
      }
      await _disposeConnection();
      if (_disposed) {
        api.close();
        return;
      }
      _credentials = credentials;
      _config = config;
      _entities = states;
      _api = api;
      if (_enableRealtime) {
        final socket = _socketFactory(credentials);
        _socket = socket;
        _stateSubscription = socket.states.listen(_onEntityState);
        _statusSubscription = socket.statuses.listen(_onSocketStatus);
        _socketStatus = socket.status;
        unawaited(socket.connect());
      } else {
        _socketStatus = HomeAssistantSocketStatus.disconnected;
      }
      _setPhase(AquaAppPhase.ready);
    } on Object {
      api.close();
      rethrow;
    }
  }

  Future<void> _disposeConnection() async {
    await _stateSubscription?.cancel();
    _stateSubscription = null;
    await _statusSubscription?.cancel();
    _statusSubscription = null;
    final socket = _socket;
    _socket = null;
    await socket?.dispose();
    final api = _api;
    _api = null;
    api?.close();
    _socketStatus = HomeAssistantSocketStatus.disconnected;
  }

  Future<bool> _perform(
    String operation,
    Future<void> Function() action,
  ) async {
    if (_busyOperations.contains(operation)) {
      return false;
    }
    _busyOperations.add(operation);
    _errorMessage = null;
    _operationMessage = null;
    _notify();
    try {
      await action();
      _operationMessage = 'Polecenie zostało przyjęte przez Home Assistanta.';
      return true;
    } on HomeAssistantFailure catch (error) {
      _errorMessage = error.message;
      return false;
    } on Object {
      _errorMessage = 'Nie udało się wykonać polecenia.';
      return false;
    } finally {
      _busyOperations.remove(operation);
      _notify();
    }
  }

  Future<void> _refreshAfterCommand() async {
    await Future<void>.delayed(const Duration(milliseconds: 600));
    await refresh();
  }

  HomeAssistantApi _requireApi() {
    final api = _api;
    if (api == null) {
      throw const HomeAssistantFailure(
        HomeAssistantFailureType.network,
        'Brak aktywnego połączenia z Home Assistantem.',
      );
    }
    return api;
  }

  void _onEntityState(HaEntityState state) {
    _entities = <String, HaEntityState>{..._entities, state.entityId: state};
    _notify();
  }

  void _onSocketStatus(HomeAssistantSocketStatus status) {
    _socketStatus = status;
    if (status == HomeAssistantSocketStatus.unauthorized) {
      _errorMessage = 'Token wygasł lub został unieważniony.';
    }
    _notify();
  }

  void _setPhase(AquaAppPhase value) {
    _phase = value;
    _notify();
  }

  void _notify() {
    if (!_disposed) {
      notifyListeners();
    }
  }
}
