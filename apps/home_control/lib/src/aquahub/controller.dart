import 'dart:async';

import 'package:flutter/foundation.dart';

import 'api.dart';
import 'credentials_store.dart';
import 'domain.dart';

enum HubAppPhase { initializing, setup, connecting, ready, failure }

typedef AuthenticatedHubApiFactory =
    HubApi Function(HubCredentials credentials);
typedef BootstrapHubApiFactory =
    HubApi Function(
      Uri baseUri,
      void Function(String fingerprint) onCertificate,
    );

final class HubController extends ChangeNotifier {
  HubController({
    required HubCredentialsStore credentialsStore,
    AuthenticatedHubApiFactory? apiFactory,
    BootstrapHubApiFactory? bootstrapFactory,
    this.enablePolling = true,
  }) : _credentialsStore = credentialsStore,
       _apiFactory = apiFactory ?? HubApi.authenticated,
       _bootstrapFactory =
           bootstrapFactory ??
           ((uri, callback) => HubApi.bootstrap(uri, onCertificate: callback));

  final HubCredentialsStore _credentialsStore;
  final AuthenticatedHubApiFactory _apiFactory;
  final BootstrapHubApiFactory _bootstrapFactory;
  final bool enablePolling;

  HubAppPhase _phase = HubAppPhase.initializing;
  HubCredentials? _credentials;
  HubApi? _api;
  HubApi? _bootstrapApi;
  HubInfo? _discoveredInfo;
  HubSystem? _system;
  HubUpdateStatus? _updateStatus;
  HubAutomationCollection _automations = const HubAutomationCollection(
    capacity: 8,
    rules: <HubAutomationRule>[],
  );
  List<HubDevice> _devices = const <HubDevice>[];
  List<HubEntity> _entities = const <HubEntity>[];
  String? _observedFingerprint;
  String? _errorMessage;
  String? _errorKey;
  Timer? _pollTimer;
  bool _refreshing = false;
  bool _updating = false;
  bool _appActive = true;
  DateTime? _lastSuccessfulRefresh;
  int _consecutiveRefreshFailures = 0;
  final Set<String> _commanding = <String>{};
  final Set<String> _editingAutomations = <String>{};
  bool _disposed = false;

  HubAppPhase get phase => _phase;
  HubCredentials? get credentials => _credentials;
  HubInfo? get discoveredInfo => _discoveredInfo;
  HubSystem? get system => _system;
  HubUpdateStatus? get updateStatus => _updateStatus;
  HubAutomationCollection get automations => _automations;
  List<HubDevice> get devices => _devices;
  List<HubEntity> get entities => _entities;
  String? get observedFingerprint => _observedFingerprint;
  String? get errorMessage => _errorMessage;
  String? get errorKey => _errorKey;
  bool get refreshing => _refreshing;
  bool get updating => _updating;
  bool get connectionHealthy => _consecutiveRefreshFailures == 0;
  DateTime? get lastSuccessfulRefresh => _lastSuccessfulRefresh;
  bool isCommanding(String entityId) => _commanding.contains(entityId);
  bool isEditingAutomation(String id) => _editingAutomations.contains(id);

  Future<void> initialize() async {
    try {
      final stored = await _credentialsStore.load();
      if (_disposed) return;
      if (stored == null) {
        _phase = HubAppPhase.setup;
        notifyListeners();
        return;
      }
      _credentials = stored;
      await _connect(stored);
    } on Object {
      if (_disposed) return;
      _errorMessage = 'Nie udało się odczytać bezpiecznej sesji AquaHub.';
      _errorKey = 'hubErrorSession';
      _phase = HubAppPhase.failure;
      notifyListeners();
    }
  }

  Future<bool> discover(String baseUrl) async {
    _closeBootstrap();
    _errorMessage = null;
    _errorKey = null;
    _discoveredInfo = null;
    _observedFingerprint = null;
    notifyListeners();
    try {
      final uri = Uri.tryParse(baseUrl.trim());
      if (uri == null) throw const FormatException();
      _bootstrapApi = _bootstrapFactory(
        uri,
        (fingerprint) =>
            _observedFingerprint = normalizeFingerprint(fingerprint),
      );
      final info = await _bootstrapApi!.fetchInfo();
      if (_observedFingerprint != null &&
          _observedFingerprint != info.tlsFingerprint) {
        throw const HubFailure(
          HubFailureType.security,
          'Odcisk certyfikatu TLS nie zgadza się z tożsamością AquaHub.',
        );
      }
      if (!info.pairingAvailable) {
        throw const HubFailure(
          HubFailureType.authentication,
          'Parowanie jest teraz zamknięte. Otwórz je na panelu AquaHub.',
        );
      }
      _discoveredInfo = info;
      notifyListeners();
      return true;
    } on FormatException {
      _errorMessage =
          'Podaj pełny adres HTTPS, np. https://aquahub.local:8443.';
      _errorKey = 'hubErrorHttpsAddress';
    } on HubFailure catch (error) {
      _errorMessage = error.message;
      _errorKey = _hubFailureKey(error.type);
    } on Object {
      _errorMessage = 'Nie udało się odnaleźć AquaHub w sieci lokalnej.';
      _errorKey = 'hubErrorDiscovery';
    }
    _closeBootstrap();
    notifyListeners();
    return false;
  }

  void resetDiscovery() {
    _closeBootstrap();
    _discoveredInfo = null;
    _observedFingerprint = null;
    _errorMessage = null;
    _errorKey = null;
    notifyListeners();
  }

  Future<bool> pair({
    required String baseUrl,
    required String code,
    required bool fingerprintConfirmed,
  }) async {
    final info = _discoveredInfo;
    final api = _bootstrapApi;
    if (info == null || api == null) {
      _errorMessage = 'Najpierw sprawdź połączenie z AquaHub.';
      _errorKey = 'hubErrorDiscoverFirst';
      notifyListeners();
      return false;
    }
    if (!fingerprintConfirmed) {
      _errorMessage = 'Porównaj odcisk z panelem i potwierdź jego zgodność.';
      _errorKey = 'hubErrorFingerprintConfirm';
      notifyListeners();
      return false;
    }
    final parsedCode = int.tryParse(code);
    if (parsedCode == null || code.length != 6) {
      _errorMessage = 'Kod parowania musi mieć dokładnie sześć cyfr.';
      _errorKey = 'hubErrorPairingCode';
      notifyListeners();
      return false;
    }
    _errorMessage = null;
    _errorKey = null;
    notifyListeners();
    try {
      final result = await api.pair(parsedCode);
      if (result.tlsFingerprint != info.tlsFingerprint ||
          (_observedFingerprint != null &&
              result.tlsFingerprint != _observedFingerprint)) {
        throw const HubFailure(
          HubFailureType.security,
          'Tożsamość AquaHub zmieniła się podczas parowania.',
        );
      }
      final credentials = HubCredentials.parse(
        baseUrl: baseUrl,
        accessToken: result.token,
        tlsFingerprint: result.tlsFingerprint,
      );
      await _credentialsStore.save(credentials);
      _closeBootstrap();
      _credentials = credentials;
      await _connect(credentials);
      return _phase == HubAppPhase.ready;
    } on HubFailure catch (error) {
      _errorMessage = error.message;
      _errorKey = _hubFailureKey(error.type);
    } on Object {
      _errorMessage = 'Nie udało się bezpiecznie zapisać sesji AquaHub.';
      _errorKey = 'hubErrorSaveSession';
    }
    notifyListeners();
    return false;
  }

  Future<void> _connect(HubCredentials credentials) async {
    _phase = HubAppPhase.connecting;
    _errorMessage = null;
    _errorKey = null;
    notifyListeners();
    _api?.close();
    _api = _apiFactory(credentials);
    try {
      await _refresh(failConnection: true);
      if (_disposed) return;
      _phase = HubAppPhase.ready;
      _startPolling();
    } on HubFailure catch (error) {
      _errorMessage = error.message;
      _errorKey = _hubFailureKey(error.type);
      _phase = HubAppPhase.failure;
    } on Object {
      _errorMessage = 'AquaHub jest nieosiągalny.';
      _errorKey = 'hubErrorNetwork';
      _phase = HubAppPhase.failure;
    }
    notifyListeners();
  }

  Future<void> refresh() => _refresh(failConnection: false);

  Future<void> _refresh({required bool failConnection}) async {
    if (_refreshing) return;
    final api = _api;
    if (api == null) return;
    _refreshing = true;
    if (!failConnection) notifyListeners();
    try {
      final results = await Future.wait<Object>(<Future<Object>>[
        api.fetchSystem(),
        api.fetchDevices(),
        api.fetchEntities(),
        api.fetchUpdateStatus(),
        api.fetchAutomations(),
      ]);
      _system = results[0] as HubSystem;
      _devices = results[1] as List<HubDevice>;
      _entities = results[2] as List<HubEntity>;
      _updateStatus = results[3] as HubUpdateStatus;
      _automations = results[4] as HubAutomationCollection;
      _errorMessage = null;
      _consecutiveRefreshFailures = 0;
      _lastSuccessfulRefresh = DateTime.now();
    } on HubFailure catch (error) {
      _errorMessage = error.message;
      _consecutiveRefreshFailures++;
      if (failConnection || error.type == HubFailureType.authentication) {
        rethrow;
      }
    } finally {
      _refreshing = false;
      if (!failConnection && !_disposed) notifyListeners();
    }
  }

  Future<bool> sendCommand(HubEntity entity, Object? value) async {
    final api = _api;
    if (api == null || !entity.writable || entity.critical) return false;
    _commanding.add(entity.id);
    _errorMessage = null;
    notifyListeners();
    try {
      await api.sendCommand(entity.id, value);
      await Future<void>.delayed(const Duration(milliseconds: 350));
      await refresh();
      return true;
    } on HubFailure catch (error) {
      _errorMessage = error.message;
      return false;
    } finally {
      _commanding.remove(entity.id);
      if (!_disposed) notifyListeners();
    }
  }

  Future<List<HubHistoryPoint>> loadHistory(String entityId) async {
    final api = _api;
    if (api == null) return const <HubHistoryPoint>[];
    return api.fetchHistory(entityId);
  }

  Future<bool> checkForUpdates() async {
    final api = _api;
    if (api == null || _updating || _updateStatus?.phase.busy == true) {
      return false;
    }
    _updating = true;
    _errorMessage = null;
    notifyListeners();
    try {
      await api.checkForUpdates();
      await Future<void>.delayed(const Duration(milliseconds: 500));
      _updateStatus = await api.fetchUpdateStatus();
      return true;
    } on HubFailure catch (error) {
      _errorMessage = error.message;
      return false;
    } finally {
      _updating = false;
      if (!_disposed) notifyListeners();
    }
  }

  Future<bool> installUpdate() async {
    final api = _api;
    if (api == null ||
        _updating ||
        _updateStatus?.phase != HubUpdatePhase.available) {
      return false;
    }
    _updating = true;
    _errorMessage = null;
    notifyListeners();
    try {
      await api.installUpdate();
      await Future<void>.delayed(const Duration(milliseconds: 500));
      _updateStatus = await api.fetchUpdateStatus();
      return true;
    } on HubFailure catch (error) {
      _errorMessage = error.message;
      return false;
    } finally {
      _updating = false;
      if (!_disposed) notifyListeners();
    }
  }

  Future<bool> saveAutomation(HubAutomationRule rule) async {
    final api = _api;
    if (api == null || _editingAutomations.contains(rule.id)) return false;
    _editingAutomations.add(rule.id);
    _errorMessage = null;
    notifyListeners();
    try {
      await api.saveAutomation(rule);
      _automations = await api.fetchAutomations();
      return true;
    } on HubFailure catch (error) {
      _errorMessage = error.message;
      return false;
    } finally {
      _editingAutomations.remove(rule.id);
      if (!_disposed) notifyListeners();
    }
  }

  Future<bool> deleteAutomation(String id) async {
    final api = _api;
    if (api == null || _editingAutomations.contains(id)) return false;
    _editingAutomations.add(id);
    _errorMessage = null;
    notifyListeners();
    try {
      await api.deleteAutomation(id);
      _automations = await api.fetchAutomations();
      return true;
    } on HubFailure catch (error) {
      _errorMessage = error.message;
      return false;
    } finally {
      _editingAutomations.remove(id);
      if (!_disposed) notifyListeners();
    }
  }

  Future<void> retry() async {
    final credentials = _credentials;
    if (credentials == null) {
      _phase = HubAppPhase.setup;
      notifyListeners();
      return;
    }
    await _connect(credentials);
  }

  Future<void> disconnect() async {
    _pollTimer?.cancel();
    _api?.close();
    _api = null;
    _credentials = null;
    _system = null;
    _updateStatus = null;
    _automations = const HubAutomationCollection(
      capacity: 8,
      rules: <HubAutomationRule>[],
    );
    _devices = const <HubDevice>[];
    _entities = const <HubEntity>[];
    _lastSuccessfulRefresh = null;
    _consecutiveRefreshFailures = 0;
    await _credentialsStore.clear();
    _phase = HubAppPhase.setup;
    _errorMessage = null;
    notifyListeners();
  }

  void _startPolling() {
    _pollTimer?.cancel();
    if (!enablePolling || !_appActive) return;
    _pollTimer = Timer.periodic(
      const Duration(seconds: 5),
      (_) => unawaited(refresh()),
    );
  }

  void setAppActive(bool active) {
    if (_disposed || _appActive == active) return;
    _appActive = active;
    if (!active) {
      _pollTimer?.cancel();
      _pollTimer = null;
      return;
    }
    if (_phase == HubAppPhase.ready) {
      _startPolling();
      unawaited(refresh());
    }
  }

  void _closeBootstrap() {
    _bootstrapApi?.close();
    _bootstrapApi = null;
  }

  static String _hubFailureKey(HubFailureType type) => switch (type) {
    HubFailureType.authentication => 'hubErrorAuthentication',
    HubFailureType.network => 'hubErrorNetwork',
    HubFailureType.invalidResponse => 'hubErrorInvalidResponse',
    HubFailureType.server => 'hubErrorServer',
    HubFailureType.security => 'hubErrorSecurity',
  };

  @override
  void dispose() {
    _disposed = true;
    _pollTimer?.cancel();
    _api?.close();
    _closeBootstrap();
    super.dispose();
  }
}
