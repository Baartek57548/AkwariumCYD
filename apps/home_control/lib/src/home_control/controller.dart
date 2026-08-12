import 'dart:async';

import 'package:flutter/material.dart';
import 'package:home_entities/home_entities.dart';
import 'package:secure_connectivity/secure_connectivity.dart';

import '../aquahub/controller.dart';
import '../aquahub/credentials_store.dart';
import '../data/credentials_store.dart';
import '../domain/models.dart';
import 'aquahub_data_source.dart';
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

final class HomeControlController extends ChangeNotifier {
  HomeControlController({
    required HomeControlPreferences preferences,
    required HubCredentialsStore hubCredentialsStore,
    required CredentialsStore homeAssistantCredentialsStore,
    required HomeSnapshotCache snapshotCache,
    RetryPolicy? retryPolicy,
    this.enablePolling = true,
  }) : _preferences = preferences,
       _hubCredentialsStore = hubCredentialsStore,
       _homeAssistantCredentialsStore = homeAssistantCredentialsStore,
       _snapshotCache = snapshotCache,
       _retryPolicy = retryPolicy ?? RetryPolicy();

  final HomeControlPreferences _preferences;
  final HubCredentialsStore _hubCredentialsStore;
  final CredentialsStore _homeAssistantCredentialsStore;
  final HomeSnapshotCache _snapshotCache;
  final RetryPolicy _retryPolicy;
  final bool enablePolling;

  HomeControlPhase _phase = HomeControlPhase.booting;
  HomeSetupStep _setupStep = HomeSetupStep.sourceSelection;
  HomeDataSource? _source;
  HomeSnapshot? _snapshot;
  StreamSubscription<HomeEntity>? _stateSubscription;
  HubController? _hubSetupController;
  Timer? _pollTimer;
  CancellationToken _cancellation = CancellationToken();
  AppFailure? _failure;
  String? _noticeKey;
  ThemeMode _themeMode = ThemeMode.system;
  Locale _locale = const Locale('pl');
  DashboardPreferences _dashboard = const DashboardPreferences.defaults();
  final Set<String> _pending = <String>{};
  List<HistoryPoint> _history = const <HistoryPoint>[];
  SourceScopedId? _historyEntityId;
  bool _historyLoading = false;
  bool _refreshing = false;
  bool _appActive = true;
  bool _disposed = false;
  bool _promotingHub = false;
  int _consecutiveFailures = 0;

  HomeControlPhase get phase => _phase;
  HomeSetupStep get setupStep => _setupStep;
  HomeSnapshot? get snapshot => _snapshot;
  AppFailure? get failure => _failure;
  String? get noticeKey => _noticeKey;
  ThemeMode get themeMode => _themeMode;
  Locale get locale => _locale;
  DashboardPreferences get dashboard => _dashboard;
  HubController? get hubSetupController => _hubSetupController;
  bool get refreshing => _refreshing;
  bool get historyLoading => _historyLoading;
  List<HistoryPoint> get history => _history;
  SourceScopedId? get historyEntityId => _historyEntityId;
  HomeSourceKind? get activeSourceKind => _source?.kind;
  bool get isDemo => _source is DemoDataSource;
  bool isPending(SourceScopedId id) => _pending.contains(id.value);

  Future<void> initialize() async {
    try {
      await _preferences.migrate();
      final loaded = await Future.wait<Object>(<Future<Object>>[
        _preferences.loadThemeMode(),
        _preferences.loadLocale(),
        _preferences.loadDashboard(),
      ]);
      _themeMode = loaded[0] as ThemeMode;
      _locale = loaded[1] as Locale;
      _dashboard = loaded[2] as DashboardPreferences;
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
    await _activate(DemoDataSource(), persistSelection: true);
  }

  void beginHomeAssistantSetup() {
    _failure = null;
    _setupStep = HomeSetupStep.homeAssistant;
    _phase = HomeControlPhase.onboarding;
    _notify();
  }

  Future<bool> configureHomeAssistant({
    required String baseUrl,
    required String accessToken,
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
    final source = HomeAssistantDataSource(
      credentials: credentials,
      credentialsStore: _homeAssistantCredentialsStore,
    );
    final connected = await _activate(source, persistSelection: false);
    if (!connected) return false;
    try {
      await _homeAssistantCredentialsStore.save(credentials);
      await _preferences.saveActiveSource(HomeSourceKind.homeAssistant);
      return true;
    } on Object {
      await source.close();
      _source = null;
      _snapshot = null;
      _failure = const AppFailure(
        code: AppFailureCode.storage,
        messageKey: 'errorStorage',
      );
      _phase = HomeControlPhase.failure;
      _notify();
      return false;
    }
  }

  Future<void> beginAquaHubSetup() async {
    await _closeActiveSource();
    await _disposeHubSetup();
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
    await _disposeHubSetup();
    _failure = null;
    _setupStep = HomeSetupStep.sourceSelection;
    _phase = HomeControlPhase.onboarding;
    _notify();
  }

  Future<void> retry() async {
    final active = await _preferences.loadActiveSource();
    if (active == null) {
      await cancelSetup();
      return;
    }
    await _restoreSource(active);
  }

  Future<void> switchSource() async {
    _cancellation.cancel('Source switched.');
    _cancellation = CancellationToken();
    await _closeActiveSource();
    await _disposeHubSetup();
    await _preferences.clearActiveSource();
    _failure = null;
    _noticeKey = null;
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
    if (kind != null) await _snapshotCache.clear(kind);
    await switchSource();
  }

  Future<bool> refresh({bool announce = false}) async {
    final source = _source;
    if (source == null || _refreshing) return false;
    _refreshing = true;
    _failure = null;
    _notify();
    try {
      _snapshot = await source.refresh(_cancellation);
      await _saveCache(_snapshot!);
      _consecutiveFailures = 0;
      if (announce) _noticeKey = 'noticeRefreshed';
      _schedulePoll();
      return true;
    } on AppFailure catch (failure) {
      _failure = failure;
      _consecutiveFailures++;
      _snapshot = _snapshot == null ? null : _markOffline(_snapshot!);
      _schedulePoll();
      return false;
    } on OperationCancelled {
      return false;
    } finally {
      _refreshing = false;
      _notify();
    }
  }

  Future<bool> sendCommand(HomeEntity entity, Object? value) async {
    final source = _source;
    final snapshot = _snapshot;
    if (source == null || snapshot == null || isPending(entity.id)) {
      return false;
    }
    if (!entity.available || !entity.writable || snapshot.isOffline) {
      _failure = const AppFailure(
        code: AppFailureCode.offline,
        messageKey: 'errorCommandUnavailable',
      );
      _notify();
      return false;
    }
    _pending.add(entity.id.value);
    _failure = null;
    _snapshot = snapshot.replaceEntity(
      entity.copyWith(state: value, updatedAt: DateTime.now()),
    );
    _notify();
    try {
      await source.sendCommand(entity, value, _cancellation);
      final current = _snapshot;
      if (current != null) await _saveCache(current);
      _noticeKey = 'noticeCommandAccepted';
      await Future<void>.delayed(const Duration(milliseconds: 250));
      await refresh();
      return true;
    } on AppFailure catch (failure) {
      _failure = failure;
      _snapshot = _snapshot?.replaceEntity(entity);
      return false;
    } on OperationCancelled {
      _snapshot = _snapshot?.replaceEntity(entity);
      return false;
    } finally {
      _pending.remove(entity.id.value);
      _notify();
    }
  }

  Future<bool> loadHistory(HomeEntity entity, Duration period) async {
    final source = _source;
    if (source == null || _historyLoading) return false;
    _historyLoading = true;
    _historyEntityId = entity.id;
    _history = const <HistoryPoint>[];
    _failure = null;
    _notify();
    try {
      _history = await source.loadHistory(entity, period, _cancellation);
      return true;
    } on AppFailure catch (failure) {
      _failure = failure;
      return false;
    } finally {
      _historyLoading = false;
      _notify();
    }
  }

  Future<bool> installUpdate(HomeUpdate update) async {
    final source = _source;
    if (source == null || _pending.contains(update.id.value)) return false;
    _pending.add(update.id.value);
    _failure = null;
    _notify();
    try {
      await source.installUpdate(update, _cancellation);
      _noticeKey = 'noticeUpdateStarted';
      await refresh();
      return true;
    } on AppFailure catch (failure) {
      _failure = failure;
      return false;
    } finally {
      _pending.remove(update.id.value);
      _notify();
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
    _themeMode = value;
    _notify();
    await _preferences.saveThemeMode(value);
  }

  Future<void> setLocale(Locale value) async {
    if (value.languageCode != 'pl' && value.languageCode != 'en') return;
    _locale = value;
    _notify();
    await _preferences.saveLocale(value);
  }

  Future<void> saveDashboard(DashboardPreferences value) async {
    _dashboard = value;
    _notify();
    await _preferences.saveDashboard(value);
  }

  Future<void> resetDashboard() async {
    await _preferences.resetDashboard();
    _dashboard = const DashboardPreferences.defaults();
    _notify();
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

  void setAppActive(bool active) {
    if (_disposed || _appActive == active) return;
    _appActive = active;
    _pollTimer?.cancel();
    _pollTimer = null;
    if (active && _phase == HomeControlPhase.ready) {
      unawaited(refresh());
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
        final credentials = await _homeAssistantCredentialsStore.load();
        if (credentials == null) {
          await _preferences.clearActiveSource();
          beginHomeAssistantSetup();
          return;
        }
        await _activate(
          HomeAssistantDataSource(
            credentials: credentials,
            credentialsStore: _homeAssistantCredentialsStore,
          ),
          persistSelection: false,
        );
        break;
    }
  }

  Future<bool> _activate(
    HomeDataSource source, {
    required bool persistSelection,
  }) async {
    _cancellation.cancel('A new data source is being activated.');
    _cancellation = CancellationToken();
    await _closeActiveSource();
    _source = source;
    _phase = HomeControlPhase.connecting;
    _failure = null;
    _noticeKey = null;
    _notify();
    try {
      final snapshot = await source.connect(_cancellation);
      if (_disposed) {
        await source.close();
        return false;
      }
      _snapshot = snapshot;
      await _saveCache(snapshot);
      _stateSubscription = source.stateChanges.listen(_handleEntityChange);
      if (persistSelection) await _preferences.saveActiveSource(source.kind);
      _phase = HomeControlPhase.ready;
      _consecutiveFailures = 0;
      _schedulePoll();
      _notify();
      return true;
    } on AppFailure catch (failure) {
      _failure = failure;
      if (!await _restoreCachedSnapshot(source.kind)) {
        _phase = HomeControlPhase.failure;
      }
    } on OperationCancelled {
      return false;
    } on Object {
      _failure = const AppFailure(
        code: AppFailureCode.unknown,
        messageKey: 'errorUnknown',
      );
      if (!await _restoreCachedSnapshot(source.kind)) {
        _phase = HomeControlPhase.failure;
      }
    }
    _notify();
    return false;
  }

  Future<bool> _restoreCachedSnapshot(HomeSourceKind kind) async {
    if (kind == HomeSourceKind.demo) return false;
    try {
      final cached = await _snapshotCache.load(kind);
      if (cached == null) return false;
      _snapshot = cached;
      _phase = HomeControlPhase.ready;
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

  void _handleEntityChange(HomeEntity entity) {
    final snapshot = _snapshot;
    if (_disposed ||
        snapshot == null ||
        entity.id.sourceId != snapshot.sourceId) {
      return;
    }
    _snapshot = snapshot.replaceEntity(entity);
    _notify();
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
    _pollTimer = Timer(delay, () => unawaited(refresh()));
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
    await _stateSubscription?.cancel();
    _stateSubscription = null;
    final source = _source;
    _source = null;
    await source?.close();
    _snapshot = null;
    _history = const <HistoryPoint>[];
    _historyEntityId = null;
    _pending.clear();
  }

  Future<void> _disposeHubSetup() async {
    final controller = _hubSetupController;
    _hubSetupController = null;
    if (controller != null) {
      controller.removeListener(_handleHubSetupChange);
      controller.dispose();
    }
  }

  void _notify() {
    if (!_disposed) notifyListeners();
  }

  @override
  void dispose() {
    if (_disposed) return;
    _disposed = true;
    _cancellation.cancel('Home Control disposed.');
    _pollTimer?.cancel();
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
