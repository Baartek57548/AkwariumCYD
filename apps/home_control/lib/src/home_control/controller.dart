import 'dart:async';

import 'package:flutter/material.dart';
import 'package:home_entities/home_entities.dart';
import 'package:secure_connectivity/secure_connectivity.dart';

import '../aquahub/controller.dart';
import '../aquahub/credentials_store.dart';
import '../data/credentials_store.dart';
import '../domain/models.dart';
import 'aquahub_data_source.dart';
import 'biometric_gate.dart';
import 'data_source.dart';
import 'demo_data_source.dart';
import 'home_assistant_data_source.dart';
import 'preferences.dart';
import 'snapshot_cache.dart';

enum HomeControlPhase {
  booting,
  onboarding,
  aquaHubSetup,
  connecting,
  ready,
  failure,
}

enum HomeSetupStep { sourceSelection, homeAssistant }

typedef HomeAssistantSourceFactory =
    HomeDataSource Function(
      HomeAssistantCredentials credentials,
      String instanceId,
    );

final class HomeControlController extends ChangeNotifier {
  HomeControlController({
    required HomeControlPreferences preferences,
    required HubCredentialsStore hubCredentialsStore,
    required CredentialsStore homeAssistantCredentialsStore,
    required HomeSnapshotCache snapshotCache,
    BiometricAuthenticator? biometricAuthenticator,
    HomeAssistantSourceFactory? homeAssistantSourceFactory,
    RetryPolicy? retryPolicy,
    this.enablePolling = true,
  }) : _preferences = preferences,
       _hubCredentialsStore = hubCredentialsStore,
       _homeAssistantCredentialsStore = homeAssistantCredentialsStore,
       _snapshotCache = snapshotCache,
       _biometricAuthenticator =
           biometricAuthenticator ?? DeviceBiometricAuthenticator(),
       _homeAssistantSourceFactory =
           homeAssistantSourceFactory ??
           ((credentials, instanceId) => HomeAssistantDataSource(
             credentials: credentials,
             credentialsStore: homeAssistantCredentialsStore,
             instanceId: instanceId,
           )),
       _retryPolicy = retryPolicy ?? RetryPolicy();

  final HomeControlPreferences _preferences;
  final HubCredentialsStore _hubCredentialsStore;
  final CredentialsStore _homeAssistantCredentialsStore;
  final HomeSnapshotCache _snapshotCache;
  final BiometricAuthenticator _biometricAuthenticator;
  final HomeAssistantSourceFactory _homeAssistantSourceFactory;
  final RetryPolicy _retryPolicy;
  final bool enablePolling;

  HomeControlPhase _phase = HomeControlPhase.booting;
  HomeSetupStep _setupStep = HomeSetupStep.sourceSelection;
  HomeDataSource? _source;
  HomeSnapshot? _snapshot;
  StreamSubscription<HomeEntity>? _stateSubscription;
  HubController? _hubSetupController;
  Timer? _pollTimer;
  Timer? _entityNotificationTimer;
  CancellationToken _cancellation = CancellationToken();
  AppFailure? _failure;
  String? _noticeKey;
  ThemeMode _themeMode = ThemeMode.system;
  Locale _locale = const Locale('pl');
  DashboardPreferences _dashboard = const DashboardPreferences.defaults();
  bool _biometricProtectionEnabled = false;
  bool _biometricBusy = false;
  BiometricAvailability _biometricAvailability =
      BiometricAvailability.unavailable;
  final Set<String> _pending = <String>{};
  List<HistoryPoint> _history = const <HistoryPoint>[];
  SourceScopedId? _historyEntityId;
  bool _historyLoading = false;
  bool _refreshing = false;
  bool _setupBusy = false;
  bool _appActive = true;
  bool _disposed = false;
  bool _promotingHub = false;
  bool _setupFromActiveSource = false;
  bool _recoveringFromCache = false;
  int _consecutiveFailures = 0;
  int _sourceGeneration = 0;
  int _sourceIntentGeneration = 0;
  int _setupGeneration = 0;
  Future<void> _transitionCommitTail = Future<void>.value();
  List<HomeAssistantProfile> _homeAssistantProfiles =
      const <HomeAssistantProfile>[];
  String? _selectedHomeAssistantProfileId;
  String? _activeHomeAssistantProfileId;

  HomeControlPhase get phase => _phase;
  HomeSetupStep get setupStep => _setupStep;
  HomeSnapshot? get snapshot => _snapshot;
  AppFailure? get failure => _failure;
  String? get noticeKey => _noticeKey;
  ThemeMode get themeMode => _themeMode;
  Locale get locale => _locale;
  DashboardPreferences get dashboard => _dashboard;
  bool get biometricProtectionEnabled => _biometricProtectionEnabled;
  bool get biometricBusy => _biometricBusy;
  BiometricAvailability get biometricAvailability => _biometricAvailability;
  HubController? get hubSetupController => _hubSetupController;
  bool get refreshing => _refreshing;
  bool get setupBusy => _setupBusy;
  bool get historyLoading => _historyLoading;
  List<HistoryPoint> get history => _history;
  SourceScopedId? get historyEntityId => _historyEntityId;
  HomeSourceKind? get activeSourceKind => _source?.kind;
  bool get isDemo => _source is DemoDataSource;
  List<HomeAssistantProfile> get homeAssistantProfiles =>
      _homeAssistantProfiles;
  String? get selectedHomeAssistantProfileId => _selectedHomeAssistantProfileId;
  bool isPending(SourceScopedId id) => _pending.contains(id.value);

  Future<void> initialize() async {
    try {
      await _preferences.migrate();
      final loaded = await Future.wait<Object>(<Future<Object>>[
        _preferences.loadThemeMode(),
        _preferences.loadLocale(),
        _preferences.loadDashboard(),
        _preferences.loadBiometricProtection(),
        _biometricAuthenticator.availability(),
      ]);
      _themeMode = loaded[0] as ThemeMode;
      _locale = loaded[1] as Locale;
      _dashboard = loaded[2] as DashboardPreferences;
      _biometricProtectionEnabled = loaded[3] as bool;
      _biometricAvailability = loaded[4] as BiometricAvailability;
      await _reloadHomeAssistantProfiles();
      final active = await _preferences.loadActiveSource();
      if (_disposed) return;
      if (active == null) {
        _phase = HomeControlPhase.onboarding;
        _notify();
        return;
      }
      await _restoreSource(active);
    } on Object {
      if (_disposed) return;
      _failure = const AppFailure(
        code: AppFailureCode.storage,
        messageKey: 'errorStorage',
      );
      _phase = HomeControlPhase.failure;
      _notify();
    }
  }

  Future<void> selectDemo() async {
    _sourceIntentGeneration++;
    await _activate(DemoDataSource(), persistSelection: true);
  }

  void beginHomeAssistantSetup() {
    _sourceIntentGeneration++;
    _setupFromActiveSource = _source != null;
    _failure = null;
    _setupStep = HomeSetupStep.homeAssistant;
    _phase = HomeControlPhase.onboarding;
    _notify();
  }

  Future<bool> configureHomeAssistant({
    required String baseUrl,
    required String accessToken,
    String? profileName,
  }) async {
    final HomeAssistantCredentials credentials;
    try {
      credentials = HomeAssistantCredentials.parse(
        baseUrl: baseUrl,
        accessToken: accessToken,
      );
    } on FormatException {
      _failure = const AppFailure(
        code: AppFailureCode.invalidResponse,
        messageKey: 'errorInvalidCredentials',
      );
      _notify();
      return false;
    }
    final profileStore = _profileStore;
    final profileId = profileStore == null ? 'ha-main' : _newProfileId();
    final sourceIntentGeneration = ++_sourceIntentGeneration;
    final setupGeneration = ++_setupGeneration;
    _setupBusy = true;
    _failure = null;
    _notify();
    HomeDataSource? source;
    try {
      final previousSelectedProfileId = profileStore == null
          ? null
          : await profileStore.selectedProfileId();
      final previousLegacyCredentials = profileStore == null
          ? await _homeAssistantCredentialsStore.load()
          : null;
      if (_setupGeneration != setupGeneration ||
          _sourceIntentGeneration != sourceIntentGeneration ||
          !_setupBusy) {
        return false;
      }
      final configuredSource = _homeAssistantSourceFactory(
        credentials,
        profileId,
      );
      source = configuredSource;
      final connected = await _activate(
        configuredSource,
        persistSelection: false,
        showConnecting: false,
        failurePhase: HomeControlPhase.onboarding,
      );
      if (!connected) return false;
      final generation = _sourceGeneration;
      final cancellation = _cancellation;
      return await _serializeTransitionCommit(() async {
        if (!_isCurrentSource(configuredSource, generation, cancellation)) {
          return false;
        }
        if (profileStore == null) {
          await _homeAssistantCredentialsStore.save(credentials);
        } else {
          await profileStore.saveProfile(
            credentials: credentials,
            name: _normalizedProfileName(profileName, credentials.baseUri),
            profileId: profileId,
          );
        }
        if (!_isCurrentSource(configuredSource, generation, cancellation)) {
          await _rollbackConfiguredHomeAssistant(
            profileStore: profileStore,
            profileId: profileId,
            previousSelectedProfileId: previousSelectedProfileId,
            previousLegacyCredentials: previousLegacyCredentials,
          );
          return false;
        }
        await _preferences.saveActiveSource(HomeSourceKind.homeAssistant);
        if (!_isCurrentSource(configuredSource, generation, cancellation)) {
          await _rollbackConfiguredHomeAssistant(
            profileStore: profileStore,
            profileId: profileId,
            previousSelectedProfileId: previousSelectedProfileId,
            previousLegacyCredentials: previousLegacyCredentials,
          );
          await _repairActiveSourcePreference();
          return false;
        }
        _activeHomeAssistantProfileId = profileId;
        await _reloadHomeAssistantProfiles();
        if (!_isCurrentSource(configuredSource, generation, cancellation)) {
          return false;
        }
        _notify();
        return true;
      });
    } on Object {
      if (_setupGeneration != setupGeneration) {
        await source?.close();
        return false;
      }
      if (identical(_source, source)) {
        _invalidateSource('Home Assistant configuration could not be saved.');
        await _closeActiveSource();
        _failure = const AppFailure(
          code: AppFailureCode.storage,
          messageKey: 'errorStorage',
        );
        _phase = HomeControlPhase.failure;
        _notify();
      } else {
        await source?.close();
      }
      return false;
    } finally {
      if (_setupGeneration == setupGeneration) {
        _setupBusy = false;
        _notify();
      }
    }
  }

  Future<bool> selectHomeAssistantProfile(String profileId) async {
    final sourceIntentGeneration = ++_sourceIntentGeneration;
    final store = _profileStore;
    if (store == null || profileId == _activeHomeAssistantProfileId) {
      return false;
    }
    final credentials = await store.loadProfile(profileId);
    if (credentials == null ||
        _sourceIntentGeneration != sourceIntentGeneration) {
      return false;
    }
    final previousProfileId =
        _activeHomeAssistantProfileId ?? _selectedHomeAssistantProfileId;
    final source = _homeAssistantSourceFactory(credentials, profileId);
    final connected = await _activate(source, persistSelection: false);
    if (!connected) return false;
    if (_sourceIntentGeneration != sourceIntentGeneration) {
      if (identical(_source, source)) {
        _invalidateSource('A newer source selection was requested.');
        await _closeActiveSource();
      }
      return false;
    }
    final generation = _sourceGeneration;
    final cancellation = _cancellation;
    try {
      return await _serializeTransitionCommit(() async {
        if (!_isCurrentSource(source, generation, cancellation)) return false;
        await store.selectProfile(profileId);
        if (!_isCurrentSource(source, generation, cancellation)) {
          await _restoreSelectedProfile(store, previousProfileId);
          return false;
        }
        await _preferences.saveActiveSource(HomeSourceKind.homeAssistant);
        if (!_isCurrentSource(source, generation, cancellation)) {
          await _restoreSelectedProfile(store, previousProfileId);
          await _repairActiveSourcePreference();
          return false;
        }
        _activeHomeAssistantProfileId = profileId;
        await _reloadHomeAssistantProfiles();
        if (!_isCurrentSource(source, generation, cancellation)) return false;
        return true;
      });
    } on Object {
      if (_isCurrentSource(source, generation, cancellation)) {
        _setStorageFailure();
      }
      return false;
    }
  }

  Future<void> deleteHomeAssistantProfile(String profileId) async {
    final store = _profileStore;
    if (store == null) return;
    final deletingActive = profileId == _activeHomeAssistantProfileId;
    await store.deleteProfile(profileId);
    await _snapshotCache.clear(
      HomeSourceKind.homeAssistant,
      sourceId: profileId,
    );
    await _reloadHomeAssistantProfiles();
    if (!deletingActive) return;
    if (_homeAssistantProfiles.isEmpty) {
      await switchSource();
      return;
    }
    await selectHomeAssistantProfile(_homeAssistantProfiles.first.id);
  }

  Future<void> beginAquaHubSetup() async {
    _sourceIntentGeneration++;
    final generation = _invalidateSource('AquaHub setup started.');
    final cancellation = _cancellation;
    await _closeActiveSource();
    if (!_isCurrentGeneration(generation, cancellation)) return;
    await _disposeHubSetup();
    if (!_isCurrentGeneration(generation, cancellation)) return;
    _failure = null;
    _phase = HomeControlPhase.aquaHubSetup;
    final controller = HubController(
      credentialsStore: _hubCredentialsStore,
      enablePolling: false,
    );
    _hubSetupController = controller;
    controller.addListener(_handleHubSetupChange);
    _notify();
    unawaited(controller.initialize());
  }

  Future<void> cancelSetup() async {
    _sourceIntentGeneration++;
    _setupGeneration++;
    await _disposeHubSetup();
    if (_setupBusy) {
      _invalidateSource('Home Assistant setup cancelled.');
      await _closeActiveSource();
      _setupBusy = false;
    }
    _failure = null;
    if (_setupFromActiveSource && _source != null && _snapshot != null) {
      _setupFromActiveSource = false;
      _phase = HomeControlPhase.ready;
      _notify();
      return;
    }
    if (_setupFromActiveSource) {
      _setupFromActiveSource = false;
      final active = await _preferences.loadActiveSource();
      if (active != null) {
        await _restoreSource(active);
        return;
      }
    }
    _setupFromActiveSource = false;
    _setupStep = HomeSetupStep.sourceSelection;
    _phase = HomeControlPhase.onboarding;
    _notify();
  }

  Future<void> retry() async {
    _sourceIntentGeneration++;
    final active = await _preferences.loadActiveSource();
    if (active == null) {
      await cancelSetup();
      return;
    }
    await _restoreSource(active);
  }

  Future<void> switchSource() async {
    _sourceIntentGeneration++;
    final generation = _invalidateSource('Source switched.');
    final cancellation = _cancellation;
    await _closeActiveSource();
    if (!_isCurrentGeneration(generation, cancellation)) return;
    await _disposeHubSetup();
    if (!_isCurrentGeneration(generation, cancellation)) return;
    final cleared = await _serializeTransitionCommit(() async {
      if (!_isCurrentGeneration(generation, cancellation)) return false;
      await _preferences.clearActiveSource();
      return _isCurrentGeneration(generation, cancellation);
    });
    if (!cleared) return;
    _failure = null;
    _noticeKey = null;
    _setupFromActiveSource = false;
    _setupStep = HomeSetupStep.sourceSelection;
    _phase = HomeControlPhase.onboarding;
    _notify();
  }

  Future<void> removeActiveSource() async {
    final source = _source;
    final kind = source?.kind ?? await _preferences.loadActiveSource();
    final credentialsCleaner = source is HomeCredentialsCleaner
        ? source as HomeCredentialsCleaner
        : null;
    if (credentialsCleaner != null) {
      await credentialsCleaner.clearCredentials();
    } else {
      final active = await _preferences.loadActiveSource();
      if (active == HomeSourceKind.aquaHub) {
        await _hubCredentialsStore.clear();
      } else if (active == HomeSourceKind.homeAssistant) {
        await _homeAssistantCredentialsStore.clear();
      }
    }
    if (kind != null) {
      final sourceId =
          source?.sourceId ??
          switch (kind) {
            HomeSourceKind.homeAssistant => _selectedHomeAssistantProfileId,
            HomeSourceKind.aquaHub => 'aquahub',
            HomeSourceKind.demo => 'demo',
          };
      await _snapshotCache.clear(kind, sourceId: sourceId);
    }
    await switchSource();
  }

  Future<bool> refresh({bool announce = false}) async {
    final source = _source;
    if (source == null || _refreshing) return false;
    if (_recoveringFromCache) return _recoverConnection();
    final generation = _sourceGeneration;
    final cancellation = _cancellation;
    _refreshing = true;
    _failure = null;
    _notify();
    try {
      final snapshot = await source.refresh(cancellation);
      if (!_isCurrentSource(source, generation, cancellation)) return false;
      _snapshot = snapshot;
      await _saveCache(snapshot);
      if (!_isCurrentSource(source, generation, cancellation)) return false;
      _consecutiveFailures = 0;
      if (announce) _noticeKey = 'noticeRefreshed';
      _schedulePoll();
      return true;
    } on AppFailure catch (failure) {
      if (!_isCurrentSource(source, generation, cancellation)) return false;
      _failure = failure;
      _consecutiveFailures++;
      _snapshot = _snapshot == null ? null : _markOffline(_snapshot!);
      _schedulePoll();
      return false;
    } on OperationCancelled {
      return false;
    } finally {
      if (_sourceGeneration == generation) {
        _refreshing = false;
        _notify();
      }
    }
  }

  Future<bool> sendCommand(HomeEntity entity, Object? value) async {
    final source = _source;
    final snapshot = _snapshot;
    if (source == null || snapshot == null || isPending(entity.id)) {
      return false;
    }
    final generation = _sourceGeneration;
    final cancellation = _cancellation;
    if (!entity.available || !entity.writable || snapshot.isOffline) {
      _failure = const AppFailure(
        code: AppFailureCode.offline,
        messageKey: 'errorCommandUnavailable',
      );
      _notify();
      return false;
    }
    if (entity.risk == HomeCommandRisk.critical &&
        !await authorizeCriticalOperation()) {
      return false;
    }
    if (!_isCurrentSource(source, generation, cancellation) ||
        entity.id.sourceId != source.sourceId) {
      return false;
    }
    _pending.add(entity.id.value);
    _failure = null;
    _snapshot = snapshot.replaceEntity(
      entity.copyWith(state: value, updatedAt: DateTime.now()),
    );
    _notify();
    try {
      await source.sendCommand(entity, value, cancellation);
      if (!_isCurrentSource(source, generation, cancellation)) return false;
      final current = _snapshot;
      if (current != null) await _saveCache(current);
      _noticeKey = 'noticeCommandAccepted';
      await Future<void>.delayed(const Duration(milliseconds: 250));
      if (_isCurrentSource(source, generation, cancellation)) await refresh();
      return true;
    } on AppFailure catch (failure) {
      if (!_isCurrentSource(source, generation, cancellation)) return false;
      _failure = failure;
      _snapshot = _snapshot?.replaceEntity(entity);
      return false;
    } on OperationCancelled {
      _snapshot = _snapshot?.replaceEntity(entity);
      return false;
    } finally {
      if (_sourceGeneration == generation) {
        _pending.remove(entity.id.value);
        _notify();
      }
    }
  }

  Future<bool> loadHistory(HomeEntity entity, Duration period) async {
    final source = _source;
    if (source == null || _historyLoading) return false;
    final generation = _sourceGeneration;
    final cancellation = _cancellation;
    _historyLoading = true;
    _historyEntityId = entity.id;
    _history = const <HistoryPoint>[];
    _failure = null;
    _notify();
    try {
      final history = await source.loadHistory(entity, period, cancellation);
      if (!_isCurrentSource(source, generation, cancellation)) return false;
      _history = history;
      return true;
    } on AppFailure catch (failure) {
      if (!_isCurrentSource(source, generation, cancellation)) return false;
      _failure = failure;
      return false;
    } on OperationCancelled {
      return false;
    } finally {
      if (_sourceGeneration == generation) {
        _historyLoading = false;
        _notify();
      }
    }
  }

  Future<bool> installUpdate(HomeUpdate update) async {
    final source = _source;
    if (source == null || _pending.contains(update.id.value)) return false;
    if (_snapshot?.isOffline != false) {
      _failure = const AppFailure(
        code: AppFailureCode.offline,
        messageKey: 'errorCommandUnavailable',
      );
      _notify();
      return false;
    }
    final generation = _sourceGeneration;
    final cancellation = _cancellation;
    if (!await authorizeCriticalOperation()) return false;
    if (!_isCurrentSource(source, generation, cancellation) ||
        update.id.sourceId != source.sourceId) {
      return false;
    }
    _pending.add(update.id.value);
    _failure = null;
    _notify();
    try {
      await source.installUpdate(update, cancellation);
      if (!_isCurrentSource(source, generation, cancellation)) return false;
      _noticeKey = 'noticeUpdateStarted';
      await refresh();
      return true;
    } on AppFailure catch (failure) {
      if (!_isCurrentSource(source, generation, cancellation)) return false;
      _failure = failure;
      return false;
    } on OperationCancelled {
      return false;
    } finally {
      if (_sourceGeneration == generation) {
        _pending.remove(update.id.value);
        _notify();
      }
    }
  }

  void setDemoOffline(bool value) {
    final source = _source;
    if (source is DemoDataSource) {
      source.setOffline(value);
      unawaited(refresh());
    }
  }

  void setDemoAlarm(bool value) {
    final source = _source;
    if (source is DemoDataSource) source.setAlarm(value);
  }

  bool get demoOffline => switch (_source) {
    DemoDataSource(:final offline) => offline,
    _ => false,
  };

  bool get demoAlarm => switch (_source) {
    DemoDataSource(:final alarm) => alarm,
    _ => false,
  };

  Future<void> setThemeMode(ThemeMode value) async {
    final previous = _themeMode;
    _themeMode = value;
    _notify();
    try {
      await _preferences.saveThemeMode(value);
    } on Object {
      _themeMode = previous;
      _setStorageFailure();
    }
  }

  Future<void> setLocale(Locale value) async {
    if (value.languageCode != 'pl' && value.languageCode != 'en') return;
    final previous = _locale;
    _locale = value;
    _notify();
    try {
      await _preferences.saveLocale(value);
    } on Object {
      _locale = previous;
      _setStorageFailure();
    }
  }

  Future<bool> setBiometricProtection(bool enabled) async {
    if (enabled == _biometricProtectionEnabled) return true;
    if (!await _authenticateBiometrically()) return false;
    try {
      await _preferences.saveBiometricProtection(enabled);
      _biometricProtectionEnabled = enabled;
      _failure = null;
      _noticeKey = enabled
          ? 'noticeBiometricEnabled'
          : 'noticeBiometricDisabled';
      _notify();
      return true;
    } on Object {
      _failure = const AppFailure(
        code: AppFailureCode.storage,
        messageKey: 'errorStorage',
      );
      _notify();
      return false;
    }
  }

  Future<bool> authorizeCriticalOperation() async {
    if (!_biometricProtectionEnabled) return true;
    return _authenticateBiometrically();
  }

  Future<bool> _authenticateBiometrically() async {
    if (_biometricBusy) return false;
    _biometricBusy = true;
    _failure = null;
    _notify();
    try {
      _biometricAvailability = await _biometricAuthenticator.availability();
      if (_biometricAvailability != BiometricAvailability.available) {
        _setBiometricFailure(
          _biometricAvailability == BiometricAvailability.unavailable
              ? BiometricAuthorization.unavailable
              : BiometricAuthorization.failed,
        );
        return false;
      }
      final result = await _biometricAuthenticator.authenticate(
        localizedReason: _locale.languageCode == 'en'
            ? 'Confirm a critical Home Control operation.'
            : 'Potwierdź krytyczną operację w Home Control.',
      );
      if (result == BiometricAuthorization.authorized) return true;
      _setBiometricFailure(result);
      return false;
    } on Object {
      _biometricAvailability = BiometricAvailability.failed;
      _setBiometricFailure(BiometricAuthorization.failed);
      return false;
    } finally {
      _biometricBusy = false;
      _notify();
    }
  }

  void _setBiometricFailure(BiometricAuthorization result) {
    final (code, messageKey) = switch (result) {
      BiometricAuthorization.cancelled => (
        AppFailureCode.cancelled,
        'errorBiometricCancelled',
      ),
      BiometricAuthorization.unavailable => (
        AppFailureCode.unsupported,
        'errorBiometricUnavailable',
      ),
      BiometricAuthorization.lockedOut => (
        AppFailureCode.authentication,
        'errorBiometricLocked',
      ),
      _ => (AppFailureCode.authentication, 'errorBiometricFailed'),
    };
    _failure = AppFailure(code: code, messageKey: messageKey);
  }

  Future<void> saveDashboard(DashboardPreferences value) async {
    final previous = _dashboard;
    _dashboard = value;
    _notify();
    try {
      await _preferences.saveDashboard(value);
    } on Object {
      _dashboard = previous;
      _setStorageFailure();
    }
  }

  Future<void> resetDashboard() async {
    final previous = _dashboard;
    _dashboard = const DashboardPreferences.defaults();
    _notify();
    try {
      await _preferences.resetDashboard();
    } on Object {
      _dashboard = previous;
      _setStorageFailure();
    }
  }

  Future<void> toggleFavorite(HomeEntity entity) async {
    final favorites = Set<String>.from(_dashboard.favorites);
    if (!favorites.add(entity.id.value)) favorites.remove(entity.id.value);
    await saveDashboard(_dashboard.copyWith(favorites: favorites));
  }

  String? takeNoticeKey() {
    final value = _noticeKey;
    _noticeKey = null;
    return value;
  }

  void clearFailure() {
    if (_failure == null) return;
    _failure = null;
    _notify();
  }

  void _setStorageFailure() {
    _failure = const AppFailure(
      code: AppFailureCode.storage,
      messageKey: 'errorStorage',
    );
    _notify();
  }

  void setAppActive(bool active) {
    if (_disposed || _appActive == active) return;
    _appActive = active;
    _pollTimer?.cancel();
    _pollTimer = null;
    if (active && _phase == HomeControlPhase.ready) {
      unawaited(_recoveringFromCache ? _recoverConnection() : refresh());
    }
  }

  Future<void> _restoreSource(HomeSourceKind kind) async {
    switch (kind) {
      case HomeSourceKind.demo:
        await _activate(DemoDataSource(), persistSelection: false);
        break;
      case HomeSourceKind.aquaHub:
        final credentials = await _hubCredentialsStore.load();
        if (credentials == null) {
          await _preferences.clearActiveSource();
          await beginAquaHubSetup();
          return;
        }
        await _activate(
          AquaHubDataSource(
            credentials: credentials,
            credentialsStore: _hubCredentialsStore,
          ),
          persistSelection: false,
        );
        break;
      case HomeSourceKind.homeAssistant:
        await _reloadHomeAssistantProfiles();
        final profileId = _selectedHomeAssistantProfileId;
        final credentials = profileId == null
            ? await _homeAssistantCredentialsStore.load()
            : await _profileStore?.loadProfile(profileId);
        if (credentials == null) {
          await _preferences.clearActiveSource();
          beginHomeAssistantSetup();
          return;
        }
        final restoredProfileId = profileId ?? 'ha-main';
        final connected = await _activate(
          _homeAssistantSourceFactory(credentials, restoredProfileId),
          persistSelection: false,
        );
        if (connected ||
            (_source?.kind == HomeSourceKind.homeAssistant &&
                _source?.sourceId == restoredProfileId)) {
          _activeHomeAssistantProfileId = restoredProfileId;
        }
        break;
    }
  }

  Future<bool> _activate(
    HomeDataSource source, {
    required bool persistSelection,
    bool showConnecting = true,
    HomeControlPhase failurePhase = HomeControlPhase.failure,
  }) async {
    final generation = _invalidateSource(
      'A new data source is being activated.',
    );
    final cancellation = _cancellation;
    await _closeActiveSource();
    if (!_isCurrentGeneration(generation, cancellation)) {
      await source.close();
      return false;
    }
    _source = source;
    if (showConnecting) _phase = HomeControlPhase.connecting;
    _failure = null;
    _noticeKey = null;
    _notify();
    try {
      final snapshot = await source.connect(cancellation);
      if (!_isCurrentSource(source, generation, cancellation)) {
        await source.close();
        return false;
      }
      _snapshot = snapshot;
      await _saveCache(snapshot);
      if (!_isCurrentSource(source, generation, cancellation)) {
        await source.close();
        return false;
      }
      _stateSubscription = source.stateChanges.listen(
        (entity) => _handleEntityChange(entity, generation),
      );
      if (persistSelection) {
        final persisted = await _serializeTransitionCommit(() async {
          if (!_isCurrentSource(source, generation, cancellation)) {
            return false;
          }
          await _preferences.saveActiveSource(source.kind);
          if (!_isCurrentSource(source, generation, cancellation)) {
            await _repairActiveSourcePreference();
            return false;
          }
          return true;
        });
        if (!persisted) return false;
      }
      if (!_isCurrentSource(source, generation, cancellation)) return false;
      _phase = HomeControlPhase.ready;
      _recoveringFromCache = false;
      _consecutiveFailures = 0;
      _schedulePoll();
      _notify();
      return true;
    } on AppFailure catch (failure) {
      if (!_isCurrentSource(source, generation, cancellation)) return false;
      _failure = failure;
      if (!await _restoreCachedSnapshot(source.kind, source.sourceId)) {
        _phase = failurePhase;
        await _discardFailedSource(source, generation);
      }
    } on OperationCancelled {
      await _discardFailedSource(source, generation);
      return false;
    } on Object {
      if (!_isCurrentSource(source, generation, cancellation)) return false;
      _failure = const AppFailure(
        code: AppFailureCode.unknown,
        messageKey: 'errorUnknown',
      );
      if (!await _restoreCachedSnapshot(source.kind, source.sourceId)) {
        _phase = failurePhase;
        await _discardFailedSource(source, generation);
      }
    }
    _notify();
    return false;
  }

  Future<bool> _restoreCachedSnapshot(
    HomeSourceKind kind,
    String expectedSourceId,
  ) async {
    if (kind == HomeSourceKind.demo) return false;
    try {
      final cached = await _snapshotCache.load(kind, expectedSourceId);
      if (cached == null || cached.sourceId != expectedSourceId) return false;
      _snapshot = cached;
      _phase = HomeControlPhase.ready;
      _recoveringFromCache = true;
      _consecutiveFailures = 1;
      _schedulePoll();
      return true;
    } on Object {
      return false;
    }
  }

  Future<void> _saveCache(HomeSnapshot snapshot) async {
    if (snapshot.sourceKind == HomeSourceKind.demo) return;
    try {
      await _snapshotCache.save(snapshot);
    } on Object {
      _failure ??= const AppFailure(
        code: AppFailureCode.storage,
        messageKey: 'errorStorage',
      );
    }
  }

  void _handleEntityChange(HomeEntity entity, int generation) {
    final snapshot = _snapshot;
    if (_disposed ||
        generation != _sourceGeneration ||
        snapshot == null ||
        entity.id.sourceId != snapshot.sourceId) {
      return;
    }
    _snapshot = snapshot.replaceEntity(entity);
    _entityNotificationTimer ??= Timer(const Duration(milliseconds: 16), () {
      _entityNotificationTimer = null;
      _notify();
    });
  }

  void _handleHubSetupChange() {
    final controller = _hubSetupController;
    if (_disposed || controller == null) return;
    _notify();
    if (controller.phase == HubAppPhase.ready && !_promotingHub) {
      _promotingHub = true;
      scheduleMicrotask(_promoteHubSession);
    }
  }

  Future<void> _promoteHubSession() async {
    final controller = _hubSetupController;
    final credentials = controller?.credentials;
    if (controller == null || credentials == null || _disposed) {
      _promotingHub = false;
      return;
    }
    final connected = await _activate(
      AquaHubDataSource(
        credentials: credentials,
        credentialsStore: _hubCredentialsStore,
      ),
      persistSelection: true,
    );
    if (connected) await _disposeHubSetup();
    _promotingHub = false;
  }

  void _schedulePoll() {
    _pollTimer?.cancel();
    _pollTimer = null;
    if (!enablePolling || !_appActive || _phase != HomeControlPhase.ready) {
      return;
    }
    final delay = _consecutiveFailures == 0
        ? const Duration(seconds: 10)
        : _retryPolicy.delayForAttempt(
            (_consecutiveFailures - 1).clamp(
              0,
              _retryPolicy.maximumAttempts - 1,
            ),
          );
    _pollTimer = Timer(
      delay,
      () => unawaited(_recoveringFromCache ? _recoverConnection() : refresh()),
    );
  }

  Future<bool> _recoverConnection() async {
    final source = _source;
    if (source == null || _refreshing || !_recoveringFromCache) return false;
    final generation = _sourceGeneration;
    final cancellation = _cancellation;
    _refreshing = true;
    _notify();
    try {
      final snapshot = await source.connect(cancellation);
      if (!_isCurrentSource(source, generation, cancellation)) return false;
      await _stateSubscription?.cancel();
      _stateSubscription = source.stateChanges.listen(
        (entity) => _handleEntityChange(entity, generation),
      );
      _snapshot = snapshot;
      await _saveCache(snapshot);
      if (!_isCurrentSource(source, generation, cancellation)) return false;
      _failure = null;
      _recoveringFromCache = false;
      _consecutiveFailures = 0;
      _schedulePoll();
      return true;
    } on AppFailure catch (failure) {
      if (!_isCurrentSource(source, generation, cancellation)) return false;
      _failure = failure;
      _consecutiveFailures++;
      final snapshot = _snapshot;
      if (snapshot != null) _snapshot = _markOffline(snapshot);
      _schedulePoll();
      return false;
    } on OperationCancelled {
      return false;
    } on Object {
      if (!_isCurrentSource(source, generation, cancellation)) return false;
      _failure = const AppFailure(
        code: AppFailureCode.unknown,
        messageKey: 'errorUnknown',
      );
      _consecutiveFailures++;
      _schedulePoll();
      return false;
    } finally {
      if (_sourceGeneration == generation) {
        _refreshing = false;
        _notify();
      }
    }
  }

  HomeSnapshot _markOffline(HomeSnapshot value) => HomeSnapshot(
    schemaVersion: value.schemaVersion,
    sourceId: value.sourceId,
    sourceName: value.sourceName,
    sourceKind: value.sourceKind,
    areas: value.areas,
    devices: value.devices,
    entities: value.entities,
    automations: value.automations,
    updates: value.updates,
    synchronizedAt: value.synchronizedAt,
    isPartial: value.isPartial,
    isOffline: true,
  );

  Future<void> _closeActiveSource() async {
    _pollTimer?.cancel();
    _pollTimer = null;
    _entityNotificationTimer?.cancel();
    _entityNotificationTimer = null;
    final stateSubscription = _stateSubscription;
    _stateSubscription = null;
    final source = _source;
    _source = null;
    _snapshot = null;
    _history = const <HistoryPoint>[];
    _historyEntityId = null;
    _pending.clear();
    _refreshing = false;
    _historyLoading = false;
    _recoveringFromCache = false;
    _activeHomeAssistantProfileId = null;
    await stateSubscription?.cancel();
    await source?.close();
  }

  Future<void> _discardFailedSource(
    HomeDataSource source,
    int generation,
  ) async {
    if (_sourceGeneration == generation && identical(_source, source)) {
      _source = null;
    }
    await source.close();
  }

  int _invalidateSource(String reason) {
    _sourceGeneration++;
    _cancellation.cancel(reason);
    _cancellation = CancellationToken();
    return _sourceGeneration;
  }

  bool _isCurrentGeneration(int generation, CancellationToken cancellation) =>
      !_disposed &&
      generation == _sourceGeneration &&
      identical(cancellation, _cancellation) &&
      !cancellation.isCancelled;

  bool _isCurrentSource(
    HomeDataSource source,
    int generation,
    CancellationToken cancellation,
  ) =>
      _isCurrentGeneration(generation, cancellation) &&
      identical(_source, source);

  Future<T> _serializeTransitionCommit<T>(Future<T> Function() action) {
    final previous = _transitionCommitTail;
    final result = Completer<T>();
    _transitionCommitTail = () async {
      try {
        await previous;
      } on Object {
        // Every queued operation reports its own error. A previous failure
        // must not prevent newer source state from being committed.
      }
      try {
        result.complete(await action());
      } on Object catch (error, stackTrace) {
        result.completeError(error, stackTrace);
      }
    }();
    return result.future;
  }

  Future<void> _disposeHubSetup() async {
    final controller = _hubSetupController;
    _hubSetupController = null;
    if (controller != null) {
      controller.removeListener(_handleHubSetupChange);
      controller.dispose();
    }
  }

  HomeAssistantProfileStore? get _profileStore =>
      _homeAssistantCredentialsStore is HomeAssistantProfileStore
      ? _homeAssistantCredentialsStore as HomeAssistantProfileStore
      : null;

  Future<void> _reloadHomeAssistantProfiles() async {
    final store = _profileStore;
    if (store == null) return;
    _homeAssistantProfiles = await store.listProfiles();
    _selectedHomeAssistantProfileId = await store.selectedProfileId();
  }

  Future<void> _rollbackConfiguredHomeAssistant({
    required HomeAssistantProfileStore? profileStore,
    required String profileId,
    required String? previousSelectedProfileId,
    required HomeAssistantCredentials? previousLegacyCredentials,
  }) async {
    if (profileStore != null) {
      await profileStore.deleteProfile(profileId);
      await _restoreSelectedProfile(profileStore, previousSelectedProfileId);
      await _reloadHomeAssistantProfiles();
      return;
    }
    if (previousLegacyCredentials == null) {
      await _homeAssistantCredentialsStore.clear();
    } else {
      await _homeAssistantCredentialsStore.save(previousLegacyCredentials);
    }
  }

  Future<void> _restoreSelectedProfile(
    HomeAssistantProfileStore store,
    String? profileId,
  ) async {
    if (profileId == null || await store.loadProfile(profileId) == null) return;
    await store.selectProfile(profileId);
  }

  Future<void> _repairActiveSourcePreference() async {
    final source = _source;
    if (source == null) {
      await _preferences.clearActiveSource();
    } else {
      await _preferences.saveActiveSource(source.kind);
    }
  }

  static String _normalizedProfileName(String? value, Uri baseUri) {
    final normalized = value?.trim() ?? '';
    if (normalized.isNotEmpty) return normalized;
    return baseUri.host.isNotEmpty ? baseUri.host : 'Home Assistant';
  }

  static String _newProfileId() {
    final micros = DateTime.now().microsecondsSinceEpoch.toRadixString(36);
    return 'ha-${micros.padLeft(8, '0')}';
  }

  void _notify() {
    if (!_disposed) notifyListeners();
  }

  @override
  void dispose() {
    if (_disposed) return;
    _disposed = true;
    _sourceGeneration++;
    _cancellation.cancel('Home Control disposed.');
    _pollTimer?.cancel();
    _entityNotificationTimer?.cancel();
    unawaited(_stateSubscription?.cancel());
    unawaited(_source?.close());
    final hub = _hubSetupController;
    if (hub != null) {
      hub.removeListener(_handleHubSetupChange);
      hub.dispose();
    }
    super.dispose();
  }
}
