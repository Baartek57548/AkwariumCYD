import 'dart:async';
import 'dart:math';

import 'package:flutter/foundation.dart';

import 'controller_api.dart';
import 'connection_health.dart';
import 'data_access.dart';
import 'firmware_package.dart';
import 'firmware_release_service.dart';
import 'history_data.dart';

enum ControllerSessionKind { wifi, bluetooth, offline, development }

enum FirmwareUpdatePhase {
  idle,
  validating,
  uploading,
  awaitingRestart,
  succeeded,
  failed,
}

class FirmwareUpdateStatus {
  const FirmwareUpdateStatus({
    required this.phase,
    required this.progress,
    required this.message,
    this.package,
    this.errorCode,
  });

  const FirmwareUpdateStatus.idle()
    : phase = FirmwareUpdatePhase.idle,
      progress = 0,
      message = '',
      package = null,
      errorCode = null;

  final FirmwareUpdatePhase phase;
  final double progress;
  final String message;
  final FirmwarePackage? package;
  final String? errorCode;

  bool get isActive =>
      phase == FirmwareUpdatePhase.validating ||
      phase == FirmwareUpdatePhase.uploading;
  bool get isError => phase == FirmwareUpdatePhase.failed;
}

enum FirmwareReleasePhase {
  idle,
  checking,
  upToDate,
  available,
  downloading,
  canceling,
  readyToInstall,
  installing,
  awaitingRestart,
  failed,
}

class FirmwareReleaseStatus {
  const FirmwareReleaseStatus({
    required this.phase,
    this.release,
    this.package,
    this.progress = 0,
    this.message,
    this.isManual = false,
  });

  const FirmwareReleaseStatus.idle()
    : phase = FirmwareReleasePhase.idle,
      release = null,
      package = null,
      progress = 0,
      message = null,
      isManual = false;

  final FirmwareReleasePhase phase;
  final FirmwareRelease? release;
  final FirmwarePackage? package;
  final double progress;
  final String? message;
  final bool isManual;

  bool get isBusy =>
      phase == FirmwareReleasePhase.checking ||
      phase == FirmwareReleasePhase.downloading ||
      phase == FirmwareReleasePhase.canceling ||
      phase == FirmwareReleasePhase.installing;
  bool get hasUpdate =>
      phase == FirmwareReleasePhase.available ||
      phase == FirmwareReleasePhase.downloading ||
      phase == FirmwareReleasePhase.canceling ||
      phase == FirmwareReleasePhase.readyToInstall ||
      phase == FirmwareReleasePhase.installing ||
      phase == FirmwareReleasePhase.awaitingRestart;
}

class ControllerSession extends ChangeNotifier {
  static const Duration _onlinePollInterval = Duration(seconds: 3);
  static const Duration _maximumReconnectDelay = Duration(seconds: 30);
  static const Duration _heartbeatInterval = Duration(seconds: 5);
  static const Duration _commandFreshnessLimit = Duration(seconds: 15);
  static const Duration _adminSessionTimeout = Duration(minutes: 5);
  static const Duration _firmwareReleaseRefreshInterval = Duration(hours: 6);
  static const Duration _firmwareReleaseRetryBase = Duration(seconds: 30);
  static const Duration _firmwareReleaseRetryMaximum = Duration(minutes: 30);
  static const int _offlineFailureThreshold = 3;
  static const int _maximumCachedArchives = 2;
  static const int _maximumArchiveBytes = 8 * 1024 * 1024;
  static const Set<String> _timedOverrideTargets = {
    'light1',
    'light2',
    'filter',
    'heater',
    'aeration',
    'co2',
    'water_dosing',
  };
  static const Set<String> _protocolV2Actions = {
    'set_light_profile',
    'set_timed_override',
    'clear_timed_override',
    'start_feeding_mode',
    'stop_feeding_mode',
    'start_service_mode',
    'stop_service_mode',
    'save_remote_gateway',
    'clear_remote_gateway',
    'save_espnow_link',
    'clear_espnow_link',
  };

  ControllerSession.wifi(
    ControllerRemoteApi api, {
    JsonMap? initialStatus,
    DateTime? cachedAt,
    FirmwareReleaseRepository? firmwareReleaseRepository,
  }) : kind = ControllerSessionKind.wifi,
       _api = api,
       _firmwareReleaseRepository = firmwareReleaseRepository,
       _offlineMode = false,
       _status = initialStatus ?? _createOfflineStatus() {
    _lastUpdate = cachedAt;
  }

  ControllerSession.bluetooth(
    ControllerRemoteApi api, {
    JsonMap? initialStatus,
    DateTime? cachedAt,
  }) : kind = ControllerSessionKind.bluetooth,
       _api = api,
       _firmwareReleaseRepository = null,
       _offlineMode = false,
       _status = initialStatus ?? _createOfflineStatus() {
    _lastUpdate = cachedAt;
  }

  ControllerSession.development({
    FirmwareReleaseRepository? firmwareReleaseRepository,
  }) : kind = ControllerSessionKind.development,
       _api = null,
       _firmwareReleaseRepository = firmwareReleaseRepository,
       _offlineMode = false,
       _status = _createDevelopmentStatus() {
    _capabilities = _createDevelopmentCapabilities();
    _logs = _createDevelopmentLogs();
    _diagnostics = _createDevelopmentDiagnostics();
  }

  ControllerSession.offline({JsonMap? cachedStatus, DateTime? cachedAt})
    : kind = ControllerSessionKind.offline,
      _api = null,
      _firmwareReleaseRepository = null,
      _offlineMode = true,
      _status = cachedStatus == null || cachedStatus.isEmpty
          ? _createOfflineStatus()
          : cachedStatus {
    _lastUpdate = cachedAt;
    _connectionPhase = ControllerConnectionPhase.offline;
  }

  final ControllerSessionKind kind;
  final ControllerRemoteApi? _api;
  final FirmwareReleaseRepository? _firmwareReleaseRepository;
  final bool _offlineMode;
  JsonMap _status;
  JsonMap _capabilities = <String, dynamic>{};
  JsonMap _logs = <String, dynamic>{};
  JsonMap _diagnostics = <String, dynamic>{};
  List<dynamic> _historyFiles = const [];
  final Map<String, List<HistorySample>> _historyArchiveCache = {};
  Timer? _pollTimer;
  Timer? _developmentTimer;
  Timer? _webSessionTimer;
  Timer? _adminSessionTimer;
  Timer? _firmwareReleaseRetryTimer;
  Future<void>? _connectOperation;
  Future<void>? _refreshOperation;
  Future<void>? _heartbeatOperation;
  final String _webSessionId =
      'm${DateTime.now().microsecondsSinceEpoch.toRadixString(36)}';
  String? _adminPin;
  String? _adminToken;
  DateTime? _adminTokenExpiresAt;
  String? _error;
  String? _activeAction;
  FirmwareUpdateStatus _firmwareUpdateStatus =
      const FirmwareUpdateStatus.idle();
  FirmwareReleaseStatus _firmwareReleaseStatus =
      const FirmwareReleaseStatus.idle();
  Future<void>? _firmwareReleaseCheckOperation;
  Future<FirmwarePackage>? _firmwareReleaseDownloadOperation;
  FirmwareDownloadCancellationToken? _firmwareDownloadCancellationToken;
  Uint8List? _downloadedFirmwareBytes;
  int _firmwareDownloadGeneration = 0;
  int _firmwareReleaseCheckFailures = 0;
  bool _connected = false;
  bool _appActive = true;
  bool _automaticReconnect = true;
  bool _hasCompletedInitialRefresh = false;
  bool _capabilityDiscoveryAttempted = false;
  bool _disposed = false;
  int _busyOperations = 0;
  int _failedPolls = 0;
  DateTime? _lastUpdate;
  Duration? _lastRoundTrip;
  ControllerConnectionPhase _connectionPhase =
      ControllerConnectionPhase.connecting;
  final Random _random = Random(7357);
  final Random _commandRandom = Random.secure();

  ControllerSessionKind get sessionKind => kind;
  bool get isDevelopment => kind == ControllerSessionKind.development;
  bool get isSimulation => isDevelopment && !_offlineMode;
  bool get isOfflineMode => _offlineMode;
  bool get hasCachedSnapshot => _lastUpdate != null;
  bool get hasStatusData =>
      isSimulation ||
      hasCachedSnapshot ||
      _connectionPhase == ControllerConnectionPhase.online;
  bool get isBluetooth => kind == ControllerSessionKind.bluetooth;
  bool get connected => _connected;
  bool get automaticReconnect => _automaticReconnect;
  bool get busy => _busyOperations > 0;
  bool get isAdmin =>
      _validAdminToken != null || (_adminToken == null && _adminPin != null);
  String? get error => _error;
  DateTime? get lastUpdate => _lastUpdate;
  Duration? get roundTrip => _lastRoundTrip;
  ControllerConnectionPhase get connectionPhase => _connectionPhase;
  String? get activeAction => _activeAction;
  FirmwareUpdateStatus get firmwareUpdateStatus => _firmwareUpdateStatus;
  FirmwareReleaseStatus get firmwareReleaseStatus => _firmwareReleaseStatus;
  bool isActionPending(String name) => _activeAction == name;
  JsonMap get status => _status;
  JsonMap get capabilities => Map<String, dynamic>.unmodifiable(_capabilities);
  JsonMap get logsData => _logs;
  JsonMap get diagnostics => _diagnostics;
  List<dynamic> get historyFiles => List.unmodifiable(_historyFiles);
  String get displayName => _offlineMode
      ? 'AquaCYD offline'
      : switch (kind) {
          ControllerSessionKind.development => 'AquaCYD DEV',
          ControllerSessionKind.offline => 'AquaCYD offline',
          ControllerSessionKind.bluetooth => 'AquaCYD BLE',
          ControllerSessionKind.wifi => 'AquaCYD Wi-Fi',
        };
  Uri? get baseUri => _api?.baseUri;
  ControllerConnectionHealth get connectionHealth => ControllerConnectionHealth(
    phase: _connectionPhase,
    failedAttempts: _failedPolls,
    rssi: _readRssi(),
    roundTrip: _lastRoundTrip,
    lastSync: _lastUpdate,
  );
  bool get supportsFirmwareUpload => firmwareUpdateBlockReason == null;
  String? get firmwareUpdateBlockReason {
    if (isDevelopment) return null;
    if (isBluetooth) return 'Aktualizacja firmware wymaga połączenia Wi-Fi.';
    if (!canIssueCommands) {
      return commandBlockReason ?? 'Sterownik nie jest gotowy do aktualizacji.';
    }
    if (!(_api?.supportsFirmwareUpload ?? false)) {
      return 'Ten transport nie obsługuje aktualizacji firmware.';
    }
    if (protocolVersion < 2 || !supportsFeature('safeOta')) {
      return 'Sterownik nie obsługuje bezpiecznych pakietów OTA.';
    }
    try {
      _firmwareValidationContext();
      return null;
    } on ControllerApiException catch (error) {
      return error.message;
    }
  }

  bool get supportsFileDownload =>
      _offlineMode || isSimulation || (_api?.supportsFileDownload ?? false);
  bool get isLegacyBluetooth =>
      isBluetooth && _status.text('mode').toUpperCase() == 'BLE_V1';
  bool get supportsAdvancedConfiguration => isDevelopment || !isLegacyBluetooth;
  int get protocolVersion {
    final versions = _capabilities.list('apiVersions');
    var highest = 1;
    for (final value in versions) {
      final parsed = value is num
          ? value.toInt()
          : int.tryParse(value.toString());
      if (parsed != null && parsed > highest) highest = parsed;
    }
    return highest;
  }

  bool supportsFeature(String name) {
    if (!RegExp(r'^[a-zA-Z][a-zA-Z0-9]{1,47}$').hasMatch(name)) return false;
    return _capabilities.section('features').flag(name);
  }

  bool get telemetryIsFresh {
    if (_offlineMode) return false;
    if (isDevelopment) return true;
    final updatedAt = _lastUpdate;
    if (!_connected ||
        _connectionPhase != ControllerConnectionPhase.online ||
        updatedAt == null) {
      return false;
    }
    final age = DateTime.now().difference(updatedAt);
    return !age.isNegative && age <= _commandFreshnessLimit;
  }

  bool get canIssueCommands => isSimulation || telemetryIsFresh;

  String? get commandBlockReason {
    if (canIssueCommands) return null;
    if (_offlineMode) {
      return hasCachedSnapshot
          ? 'Pracujesz na ostatnio zapisanych danych. Połącz sterownik przez '
                'Wi‑Fi albo Bluetooth, aby wysyłać polecenia.'
          : 'Brak połączenia i zapisanych danych sterownika. Połącz Wi‑Fi '
                'albo Bluetooth, aby pobrać stan i wysyłać polecenia.';
    }
    if (_connectionPhase == ControllerConnectionPhase.connecting) {
      return 'Poczekaj na pierwszą synchronizację ze sterownikiem.';
    }
    if (_connectionPhase == ControllerConnectionPhase.reconnecting) {
      return 'Sterowanie jest zablokowane podczas ponawiania połączenia.';
    }
    if (_connectionPhase == ControllerConnectionPhase.offline) {
      return 'Sterownik jest offline. Polecenia nie zostaną wysłane.';
    }
    return 'Telemetria jest nieaktualna. Odśwież dane przed sterowaniem.';
  }

  Future<void> connect({bool reportBusy = true}) {
    if (_disposed) return Future<void>.value();
    final pending = _connectOperation;
    if (pending != null) return pending;

    late final Future<void> operation;
    operation = _performConnect(reportBusy: reportBusy).whenComplete(() {
      if (identical(_connectOperation, operation)) {
        _connectOperation = null;
      }
    });
    _connectOperation = operation;
    return operation;
  }

  Future<void> _performConnect({required bool reportBusy}) async {
    if (_offlineMode) {
      _connected = false;
      _connectionPhase = ControllerConnectionPhase.offline;
      _error = null;
      _notifySafely();
      return;
    }
    _error = null;
    _connectionPhase = _lastUpdate == null
        ? ControllerConnectionPhase.connecting
        : ControllerConnectionPhase.reconnecting;
    if (reportBusy) _beginBusy();
    _notifySafely();

    try {
      if (isDevelopment) {
        _connected = true;
        _failedPolls = 0;
        _lastUpdate = DateTime.now();
        _lastRoundTrip = Duration.zero;
        _connectionPhase = ControllerConnectionPhase.online;
        _startDevelopmentTicker();
        _startAutomaticFirmwareReleaseCheck();
        return;
      }

      await _api!.connect();
      await refresh(
        includeHistory: !_hasCompletedInitialRefresh,
        reportBusy: false,
      );
      if (!_connected) return;
      await _discoverProtocolCapabilities();
      _startAutomaticFirmwareReleaseCheck();

      if (_api.supportsWebSession) {
        await _sendWebSessionHeartbeat();
        _scheduleHeartbeat();
      }
    } on ControllerApiException catch (error) {
      _recordConnectionFailure(error.message);
    } on Object catch (error) {
      _recordConnectionFailure(
        'Nie udało się połączyć ze sterownikiem: $error',
      );
    } finally {
      if (reportBusy) _endBusy();
      _notifySafely();
      _schedulePoll();
    }
  }

  Future<void> _discoverProtocolCapabilities({bool force = false}) async {
    if (_disposed ||
        isDevelopment ||
        _offlineMode ||
        (!force && _capabilityDiscoveryAttempted)) {
      return;
    }
    _capabilityDiscoveryAttempted = true;
    final api = _api;
    if (api == null || api is! ControllerProtocolV2Api) return;
    final protocolV2Api = api as ControllerProtocolV2Api;
    try {
      final discovered = await protocolV2Api.capabilities();
      if (_disposed || discovered.isEmpty) return;
      _capabilities = Map<String, dynamic>.from(discovered);
    } on ControllerApiException {
      // Firmware v1 nie udostępnia manifestu możliwości. Status i podstawowe
      // sterowanie pozostają dostępne przez istniejący protokół zgodności.
    } on Object {
      // Wykrywanie możliwości nie może zerwać działającej sesji sterownika.
    }
  }

  Future<void> refresh({bool includeHistory = false, bool reportBusy = true}) {
    if (_disposed) return Future<void>.value();
    if (_offlineMode) {
      _connected = false;
      _connectionPhase = ControllerConnectionPhase.offline;
      _notifySafely();
      return Future<void>.value();
    }
    if (isDevelopment) {
      _lastUpdate = DateTime.now();
      _lastRoundTrip = Duration.zero;
      _connected = true;
      _failedPolls = 0;
      _connectionPhase = ControllerConnectionPhase.online;
      _notifySafely();
      return Future<void>.value();
    }

    final pending = _refreshOperation;
    if (pending != null) {
      if (!reportBusy) return pending;
      _beginBusy();
      _notifySafely();
      return pending.whenComplete(() {
        _endBusy();
        _notifySafely();
      });
    }

    late final Future<void> operation;
    operation =
        _performRefresh(
          includeHistory: includeHistory,
          reportBusy: reportBusy,
        ).whenComplete(() {
          if (identical(_refreshOperation, operation)) {
            _refreshOperation = null;
          }
        });
    _refreshOperation = operation;
    return operation;
  }

  Future<void> _performRefresh({
    required bool includeHistory,
    required bool reportBusy,
  }) async {
    if (reportBusy) {
      _beginBusy();
      _notifySafely();
    }
    final stopwatch = Stopwatch()..start();
    try {
      final next = await _api!.status(includeHistory: includeHistory);
      if (_disposed) return;
      if (!includeHistory) _preserveHistory(_status, next);
      _status = next;
      _hasCompletedInitialRefresh = true;
      _connected = true;
      _failedPolls = 0;
      _error = null;
      _lastUpdate = DateTime.now();
      _lastRoundTrip = stopwatch.elapsed;
      _connectionPhase = ControllerConnectionPhase.online;
    } on ControllerApiException catch (error) {
      _recordConnectionFailure(error.message);
    } on Object catch (error) {
      _recordConnectionFailure('Błąd komunikacji ze sterownikiem: $error');
    } finally {
      stopwatch.stop();
      if (reportBusy) _endBusy();
      _notifySafely();
    }
  }

  Future<void> _refreshAfterMutation({bool includeHistory = false}) async {
    final inFlight = _refreshOperation;
    if (inFlight != null) {
      await inFlight;
    }
    if (!_disposed) {
      await refresh(includeHistory: includeHistory, reportBusy: false);
    }
  }

  void setAppActive(bool active) {
    if (_disposed || _appActive == active) return;
    _appActive = active;
    if (!active) {
      _pollTimer?.cancel();
      _pollTimer = null;
      _webSessionTimer?.cancel();
      _webSessionTimer = null;
      _developmentTimer?.cancel();
      _developmentTimer = null;
      _firmwareReleaseRetryTimer?.cancel();
      _firmwareReleaseRetryTimer = null;
      final tokenToRevoke = _validAdminToken;
      _clearAdminSession();
      _revokeAdminSessionBestEffort(tokenToRevoke);
      _notifySafely();
      return;
    }

    if (_offlineMode) {
      _connectionPhase = ControllerConnectionPhase.offline;
      _notifySafely();
      return;
    }
    if (isDevelopment) {
      _startDevelopmentTicker();
      _tickDevelopment();
      _startAutomaticFirmwareReleaseCheck();
      return;
    }

    if (_connectionPhase == ControllerConnectionPhase.online) {
      unawaited(
        refresh(reportBusy: false).whenComplete(() {
          _schedulePoll();
          _startAutomaticFirmwareReleaseCheck();
        }),
      );
    } else if (_automaticReconnect) {
      unawaited(
        connect(reportBusy: false).whenComplete(() {
          _schedulePoll();
          _startAutomaticFirmwareReleaseCheck();
        }),
      );
    } else {
      _pollTimer?.cancel();
      _pollTimer = null;
    }
    if (_connectionPhase == ControllerConnectionPhase.online &&
        _api!.supportsWebSession) {
      unawaited(_sendWebSessionHeartbeat());
      _scheduleHeartbeat();
    } else {
      _webSessionTimer?.cancel();
      _webSessionTimer = null;
    }
  }

  void setAutomaticReconnect(bool enabled) {
    if (_disposed || _offlineMode || isDevelopment) return;
    if (_automaticReconnect == enabled) return;
    _automaticReconnect = enabled;
    if (!enabled && _connectionPhase != ControllerConnectionPhase.online) {
      _pollTimer?.cancel();
      _pollTimer = null;
      _webSessionTimer?.cancel();
      _webSessionTimer = null;
      _notifySafely();
      return;
    }
    if (enabled &&
        _appActive &&
        _connectionPhase != ControllerConnectionPhase.online) {
      unawaited(connect(reportBusy: false));
    }
    _notifySafely();
  }

  void _startDevelopmentTicker() {
    if (_offlineMode || !_appActive || _disposed || _developmentTimer != null) {
      return;
    }
    _developmentTimer = Timer.periodic(
      const Duration(seconds: 2),
      (_) => _tickDevelopment(),
    );
  }

  void _schedulePoll([Duration? delay]) {
    _pollTimer?.cancel();
    _pollTimer = null;
    if (_disposed ||
        !_appActive ||
        isDevelopment ||
        _firmwareUpdateStatus.isActive ||
        (!_automaticReconnect &&
            _connectionPhase != ControllerConnectionPhase.online)) {
      return;
    }

    _pollTimer = Timer(delay ?? _nextPollDelay(), () async {
      _pollTimer = null;
      if (_disposed || !_appActive) return;
      if (_connectionPhase == ControllerConnectionPhase.online) {
        await refresh(reportBusy: false);
      } else {
        await connect(reportBusy: false);
      }
      _schedulePoll();
    });
  }

  Duration _nextPollDelay() {
    if (_failedPolls <= 0) return _onlinePollInterval;
    final exponent = min(_failedPolls - 1, 4);
    final baseMilliseconds = 2000 * (1 << exponent);
    final jitter = 0.85 + (_random.nextDouble() * 0.3);
    final bounded = min(
      _maximumReconnectDelay.inMilliseconds,
      (baseMilliseconds * jitter).round(),
    );
    return Duration(milliseconds: bounded);
  }

  void _scheduleHeartbeat() {
    _webSessionTimer?.cancel();
    _webSessionTimer = null;
    if (_disposed ||
        !_appActive ||
        isDevelopment ||
        _firmwareUpdateStatus.isActive ||
        !_connected ||
        _connectionPhase != ControllerConnectionPhase.online ||
        !_api!.supportsWebSession) {
      return;
    }
    _webSessionTimer = Timer(_heartbeatInterval, () async {
      _webSessionTimer = null;
      await _sendWebSessionHeartbeat();
      _scheduleHeartbeat();
    });
  }

  void _recordConnectionFailure(String message) {
    if (_disposed) return;
    _firmwareReleaseRetryTimer?.cancel();
    _firmwareReleaseRetryTimer = null;
    _failedPolls += 1;
    _connected = false;
    _error = message;
    _webSessionTimer?.cancel();
    _webSessionTimer = null;
    _connectionPhase =
        _lastUpdate != null && _failedPolls < _offlineFailureThreshold
        ? ControllerConnectionPhase.reconnecting
        : ControllerConnectionPhase.offline;
  }

  static void _preserveHistory(JsonMap previous, JsonMap next) {
    final previousHistory = previous.section('temperature').list('history');
    if (previousHistory.isEmpty) return;

    final rawTemperature = next['temperature'];
    if (rawTemperature is Map<String, dynamic>) {
      if (!rawTemperature.containsKey('history')) {
        rawTemperature['history'] = List<dynamic>.of(previousHistory);
      }
      return;
    }
    if (rawTemperature is Map) {
      final temperature = rawTemperature.map(
        (key, value) => MapEntry(key.toString(), value),
      );
      if (!temperature.containsKey('history')) {
        temperature['history'] = List<dynamic>.of(previousHistory);
      }
      next['temperature'] = temperature;
    }
  }

  void _beginBusy() {
    _busyOperations += 1;
  }

  void _endBusy() {
    _busyOperations = max(0, _busyOperations - 1);
  }

  int? _readRssi() {
    final value = _status.section('network').nullableNumber('rssi');
    if (value == null || !value.isFinite || value > 0 || value < -130) {
      return null;
    }
    return value.round();
  }

  void _notifySafely() {
    if (!_disposed) notifyListeners();
  }

  Future<ControllerActionResult> login(String pin) async {
    if (_offlineMode) {
      throw const ControllerApiException(
        code: 'controller_unavailable',
        message:
            'Tryb administratora wymaga aktywnego połączenia ze sterownikiem.',
      );
    }
    _requireLiveController();
    final normalized = pin.trim();
    if (!RegExp(r'^\d{4,8}$').hasMatch(normalized)) {
      throw const ControllerApiException(
        code: 'invalid_pin_format',
        message: 'PIN musi zawierać od 4 do 8 cyfr.',
      );
    }
    if (isDevelopment) {
      if (normalized != '1234') {
        throw const ControllerApiException(
          code: 'invalid_pin',
          statusCode: 403,
          message: 'Nieprawidłowy PIN administratora.',
        );
      }
      _activateAdminSession(normalized);
      notifyListeners();
      return const ControllerActionResult(
        success: true,
        code: 'ok',
        message: 'Tryb administratora aktywny.',
      );
    }
    if (isLegacyBluetooth) {
      _activateAdminSession(normalized);
      notifyListeners();
      return const ControllerActionResult(
        success: true,
        code: 'pin_pending_verification',
        message: 'PIN zostanie zweryfikowany przy pierwszym poleceniu BLE.',
      );
    }
    final api = _api!;
    if (api is ControllerProtocolV2Api) {
      final protocolV2Api = api as ControllerProtocolV2Api;
      try {
        final secureSession = await protocolV2Api.authenticateSession(
          normalized,
        );
        _activateAdminSession(normalized, secureSession: secureSession);
        notifyListeners();
        return const ControllerActionResult(
          success: true,
          code: 'authenticated',
          message: 'Bezpieczna sesja administratora jest aktywna.',
        );
      } on ControllerApiException catch (error) {
        if (!_allowsLegacyPinFallback ||
            !_canFallbackToLegacyAuthentication(error)) {
          rethrow;
        }
      }
    }
    if (!_allowsLegacyPinFallback) {
      throw const ControllerApiException(
        code: 'secure_session_required',
        statusCode: 401,
        message:
            'Sterownik nie zezwala na logowanie PIN-em w starszym protokole. '
            'Wymagana jest bezpieczna sesja administratora.',
      );
    }
    final result = await api.authenticate(normalized);
    _activateAdminSession(normalized);
    notifyListeners();
    return result;
  }

  Future<void> logout() async {
    final token = _validAdminToken;
    final api = _api;
    _clearAdminSession();
    _notifySafely();
    if (!isDevelopment && token != null && api is ControllerProtocolV2Api) {
      try {
        await (api as ControllerProtocolV2Api).revokeSession(token);
      } on Object {
        // Wylogowanie lokalne musi zakończyć się także po utracie połączenia.
        // Token po stronie sterownika ma dodatkowo krótki czas ważności.
      }
    }
  }

  Future<ControllerActionResult> action(
    String name, {
    Map<String, Object?> payload = const {},
    bool refreshAfter = true,
  }) async {
    final normalizedName = name.trim();
    if (!RegExp(r'^[a-z][a-z0-9_]{1,47}$').hasMatch(normalizedName)) {
      throw const ControllerApiException(
        code: 'invalid_action',
        message: 'Nieprawidłowa nazwa polecenia.',
      );
    }
    _requireLiveController();
    if (_activeAction != null) {
      throw ControllerApiException(
        code: 'action_in_progress',
        message: 'Polecenie $_activeAction jest już wykonywane.',
      );
    }

    _activeAction = normalizedName;
    _beginBusy();
    _notifySafely();
    try {
      final api = _api;
      final token = _validAdminToken;
      final authorization = isDevelopment
          ? (sessionToken: null, legacyPin: _requirePin())
          : _protectedRequestAuthorization();
      final protocolV2Api = api is ControllerProtocolV2Api
          ? api as ControllerProtocolV2Api
          : null;
      final result = isDevelopment
          ? _performDevelopmentAction(
              normalizedName,
              payload,
              authorization.legacyPin!,
            )
          : token != null &&
                protocolV2Api != null &&
                _protocolV2Actions.contains(normalizedName)
          ? await protocolV2Api.actionV2(
              normalizedName,
              commandId: _nextCommandId(),
              token: token,
              payload: payload,
            )
          : await api!.action(
              normalizedName,
              payload: payload,
              sessionToken: authorization.sessionToken,
              legacyPin: authorization.legacyPin,
            );
      if (refreshAfter) {
        if (isDevelopment) {
          _lastUpdate = DateTime.now();
        } else {
          await _refreshAfterMutation();
        }
      }
      return result;
    } on ControllerApiException catch (error) {
      if (error.isAuthenticationError) {
        _clearAdminSession();
      }
      rethrow;
    } finally {
      _activeAction = null;
      _endBusy();
      _notifySafely();
    }
  }

  Future<void> loadLogs() async {
    _requireLiveController();
    _beginBusy();
    _notifySafely();
    try {
      if (isDevelopment) {
        _requirePin();
      } else {
        final authorization = _protectedRequestAuthorization();
        _logs = await _api!.logs(
          sessionToken: authorization.sessionToken,
          legacyPin: authorization.legacyPin,
        );
      }
    } on ControllerApiException catch (error) {
      if (error.isAuthenticationError) _clearAdminSession();
      rethrow;
    } finally {
      _endBusy();
      _notifySafely();
    }
  }

  Future<void> scanBuses() async {
    _requireLiveController();
    _beginBusy();
    _notifySafely();
    try {
      if (isDevelopment) {
        _requirePin();
        _diagnostics = _createDevelopmentDiagnostics();
      } else {
        final authorization = _protectedRequestAuthorization();
        _diagnostics = await _api!.busDiagnostics(
          sessionToken: authorization.sessionToken,
          legacyPin: authorization.legacyPin,
        );
      }
    } on ControllerApiException catch (error) {
      if (error.isAuthenticationError) _clearAdminSession();
      rethrow;
    } finally {
      _endBusy();
      _notifySafely();
    }
  }

  Future<void> loadHistoryFiles() async {
    if (_offlineMode) {
      _historyFiles = const [];
      notifyListeners();
      return;
    }
    if (isDevelopment) {
      _historyFiles = [
        {'name': '2026-07.aqh', 'size': 18432, 'type': 'file'},
        {'name': '2026-06.aqh', 'size': 92160, 'type': 'file'},
      ];
      notifyListeners();
      return;
    }
    if (!_api!.supportsFileDownload) {
      _historyFiles = const [];
      notifyListeners();
      return;
    }
    _beginBusy();
    _notifySafely();
    try {
      _historyFiles = await _api.historyFiles();
    } finally {
      _endBusy();
      _notifySafely();
    }
  }

  Future<HistoryLoadResult> loadHistory(Duration range) async {
    if (range <= Duration.zero) {
      throw const ControllerApiException(
        code: 'invalid_history_range',
        message: 'Zakres historii musi być większy od zera.',
      );
    }
    if (_offlineMode) return _offlineHistory(range);
    if (isDevelopment) return _developmentHistory(range);

    await refresh(includeHistory: true, reportBusy: false);
    final cutoff =
        DateTime.now().millisecondsSinceEpoch ~/ 1000 - range.inSeconds;
    final merged = <int, HistorySample>{};
    for (final raw in _status.section('temperature').list('history')) {
      final sample = HistorySample.fromStatus(raw);
      if (sample.epoch > 0) merged[sample.epoch] = sample;
    }

    var usedArchive = false;
    String? warning;
    if (_api?.supportsFileDownload == true) {
      _beginBusy();
      _notifySafely();
      try {
        _historyFiles = await _api!.historyFiles();
        final relevant = _relevantHistoryFiles(_historyFiles, cutoff);
        for (final file in relevant) {
          final path = file.text('path');
          if (path.isEmpty) continue;
          try {
            final samples =
                _historyArchiveCache[path] ??
                HistoryArchiveCodec.decode(
                  await _api.download(
                    '/download',
                    queryParameters: {'path': path},
                    maximumBytes: _maximumArchiveBytes,
                  ),
                );
            _historyArchiveCache[path] = samples;
            while (_historyArchiveCache.length > _maximumCachedArchives) {
              _historyArchiveCache.remove(_historyArchiveCache.keys.first);
            }
            for (final sample in samples) {
              if (sample.epoch >= cutoff) merged[sample.epoch] = sample;
            }
            usedArchive = true;
          } on Object catch (error) {
            warning =
                'Nie udało się odczytać archiwum ${file.text('name')}: $error';
          }
        }
      } on ControllerApiException catch (error) {
        warning = 'Archiwum SD jest niedostępne: ${error.message}';
      } finally {
        _endBusy();
        _notifySafely();
      }
    } else if (isBluetooth) {
      warning = 'BLE udostępnia tylko bieżący bufor historii sterownika.';
    }

    final samples =
        merged.values.where((sample) => sample.epoch >= cutoff).toList()
          ..sort((a, b) => a.epoch.compareTo(b.epoch));
    if (samples.length >= 2 &&
        samples.last.epoch - samples.first.epoch < range.inSeconds * 0.9) {
      warning ??=
          'Dostępne dane obejmują ${_durationLabel(Duration(seconds: samples.last.epoch - samples.first.epoch))} z wybranego zakresu ${_durationLabel(range)}.';
    }
    return HistoryLoadResult(
      samples: List.unmodifiable(samples),
      requestedRange: range,
      usedArchive: usedArchive,
      warning: warning,
    );
  }

  List<JsonMap> _relevantHistoryFiles(List<dynamic> files, int cutoff) {
    final cutoffDate = DateTime.fromMillisecondsSinceEpoch(
      cutoff * 1000,
    ).toLocal();
    final now = DateTime.now();
    final months = <String>{};
    var cursor = DateTime(cutoffDate.year, cutoffDate.month);
    final last = DateTime(now.year, now.month);
    while (!cursor.isAfter(last) && months.length < 24) {
      months.add(
        '${cursor.year.toString().padLeft(4, '0')}-${cursor.month.toString().padLeft(2, '0')}.aqbin',
      );
      cursor = DateTime(cursor.year, cursor.month + 1);
    }
    return files
        .map(jsonMap)
        .where((file) => months.contains(file.text('name')))
        .toList(growable: false);
  }

  HistoryLoadResult _developmentHistory(Duration range) {
    final now = DateTime.now().millisecondsSinceEpoch ~/ 1000;
    final intervalSeconds = range > const Duration(hours: 24) ? 300 : 60;
    final count = (range.inSeconds ~/ intervalSeconds).clamp(2, 4096) + 1;
    final samples = List<HistorySample>.generate(count, (index) {
      final epoch = now - (count - index - 1) * intervalSeconds;
      final phase = epoch / 3600;
      return HistorySample(
        epoch: epoch,
        temperature: 24.7 + sin(phase * 0.8) * 0.45,
        ph: 6.85 + sin(phase * 0.37) * 0.12,
        ldr: (760 + sin(phase * 0.55) * 620).round().clamp(0, 4095),
        heapBytes: 181000 - (index % 20) * 120,
        heaterOn: sin(phase * 0.8) < -0.25,
      );
    });
    return HistoryLoadResult(
      samples: List.unmodifiable(samples),
      requestedRange: range,
      usedArchive: true,
    );
  }

  HistoryLoadResult _offlineHistory(Duration range) {
    final cutoff =
        DateTime.now().millisecondsSinceEpoch ~/ 1000 - range.inSeconds;
    final samples = <HistorySample>[];
    for (final raw in _status.section('temperature').list('history')) {
      final sample = HistorySample.fromStatus(raw);
      if (sample.epoch >= cutoff) samples.add(sample);
    }
    samples.sort((left, right) => left.epoch.compareTo(right.epoch));
    return HistoryLoadResult(
      samples: List.unmodifiable(samples),
      requestedRange: range,
      usedArchive: false,
      warning: samples.isEmpty
          ? 'Brak zapisanych próbek dla wybranego zakresu. Połącz sterownik, '
                'aby odświeżyć historię.'
          : 'Wyświetlane są ostatnie próbki zapisane lokalnie. Połącz '
                'sterownik, aby pobrać pełne archiwum.',
    );
  }

  static String _durationLabel(Duration value) {
    if (value.inHours >= 24 && value.inHours % 24 == 0) {
      return '${value.inDays} d';
    }
    if (value.inHours >= 1) return '${value.inHours} h';
    return '${value.inMinutes} min';
  }

  Future<Uint8List> downloadCurrentHistory() async {
    if (_offlineMode || isDevelopment || !_connected) {
      final history = _status.section('temperature').list('history');
      final buffer = StringBuffer('epoch,temp_c,heater_active\n');
      for (final item in history) {
        final sample = jsonMap(item);
        buffer.writeln(
          '${sample.integer('epoch')},${sample.number('value').toStringAsFixed(2)},0',
        );
      }
      return Uint8List.fromList(buffer.toString().codeUnits);
    }
    if (!_api!.supportsFileDownload) {
      throw const ControllerApiException(
        code: 'transport_unsupported',
        message: 'Eksport plików wymaga połączenia Wi-Fi.',
      );
    }
    return _api.download('/api/history.csv');
  }

  Future<void> setBrowserTime() async {
    _requireLiveController();
    if (isDevelopment) {
      _requirePin();
      _applyDevelopmentClock(DateTime.now());
      notifyListeners();
      return;
    }
    final authorization = _protectedRequestAuthorization();
    await _api!.setBrowserTime(
      DateTime.now().millisecondsSinceEpoch ~/ 1000,
      sessionToken: authorization.sessionToken,
      legacyPin: authorization.legacyPin,
    );
    await refresh(reportBusy: false);
  }

  void _startAutomaticFirmwareReleaseCheck() {
    if (_disposed ||
        !_appActive ||
        _firmwareReleaseRepository == null ||
        !supportsFirmwareUpload ||
        _hasCachedFirmwareReleasePackage ||
        _firmwareReleaseStatus.phase == FirmwareReleasePhase.downloading ||
        _firmwareReleaseStatus.phase == FirmwareReleasePhase.canceling ||
        _firmwareReleaseStatus.phase == FirmwareReleasePhase.installing) {
      return;
    }
    _firmwareReleaseRetryTimer?.cancel();
    _firmwareReleaseRetryTimer = null;
    unawaited(checkForFirmwareUpdates());
  }

  Future<void> checkForFirmwareUpdates({bool manual = false}) {
    final running = _firmwareReleaseCheckOperation;
    if (running != null) return running;
    if (_hasCachedFirmwareReleasePackage) return Future<void>.value();
    _firmwareReleaseRetryTimer?.cancel();
    _firmwareReleaseRetryTimer = null;
    late final Future<void> operation;
    operation = _performFirmwareReleaseCheck(manual: manual).whenComplete(() {
      if (identical(_firmwareReleaseCheckOperation, operation)) {
        _firmwareReleaseCheckOperation = null;
      }
    });
    _firmwareReleaseCheckOperation = operation;
    return operation;
  }

  Future<void> _performFirmwareReleaseCheck({required bool manual}) async {
    final repository = _firmwareReleaseRepository;
    if (repository == null) {
      if (manual) {
        _emitFirmwareRelease(
          FirmwareReleasePhase.failed,
          message: 'Automatyczne sprawdzanie firmware jest niedostępne.',
          isManual: true,
        );
      }
      return;
    }
    if (_firmwareUpdateStatus.isActive ||
        _firmwareReleaseStatus.phase == FirmwareReleasePhase.downloading ||
        _firmwareReleaseStatus.phase == FirmwareReleasePhase.canceling ||
        _firmwareReleaseStatus.phase == FirmwareReleasePhase.installing ||
        _hasCachedFirmwareReleasePackage) {
      return;
    }

    FirmwarePackageValidationContext context;
    try {
      context = _firmwareValidationContext();
    } on ControllerApiException catch (error) {
      _emitFirmwareRelease(
        manual ? FirmwareReleasePhase.failed : FirmwareReleasePhase.idle,
        message: manual ? error.message : null,
        isManual: manual,
      );
      _scheduleFirmwareReleaseCheck(failed: true);
      return;
    }

    final previousStatus = _firmwareReleaseStatus;
    _emitFirmwareRelease(
      FirmwareReleasePhase.checking,
      release: previousStatus.release,
      package: previousStatus.package,
      progress: previousStatus.progress,
      message: 'Sprawdzanie wydań firmware na GitHubie…',
      isManual: manual,
    );
    try {
      final release = await repository.fetchLatestFirmwareRelease(
        context.expectedTarget,
      );
      if (release == null ||
          FirmwarePackageParser.compareSemanticVersions(
                release.version,
                context.currentVersion,
              ) <=
              0) {
        _downloadedFirmwareBytes = null;
        _emitFirmwareRelease(
          FirmwareReleasePhase.upToDate,
          message: 'Sterownik ma najnowszą dostępną wersję firmware.',
          isManual: manual,
        );
        _scheduleFirmwareReleaseCheck(failed: false);
        return;
      }
      _downloadedFirmwareBytes = null;
      _emitFirmwareRelease(
        FirmwareReleasePhase.available,
        release: release,
        message:
            'Dostępny firmware ${release.version} dla '
            '${release.target.label}.',
        isManual: manual,
      );
      _scheduleFirmwareReleaseCheck(failed: false);
    } on FirmwareReleaseException catch (error) {
      _handleFirmwareReleaseCheckFailure(
        previousStatus,
        manual: manual,
        message: error.message,
      );
    } on FirmwarePackageException catch (error) {
      _handleFirmwareReleaseCheckFailure(
        previousStatus,
        manual: manual,
        message: error.message,
      );
    } on Object {
      _handleFirmwareReleaseCheckFailure(
        previousStatus,
        manual: manual,
        message: 'Nie udało się sprawdzić dostępności firmware.',
      );
    }
  }

  void _handleFirmwareReleaseCheckFailure(
    FirmwareReleaseStatus previousStatus, {
    required bool manual,
    required String message,
  }) {
    if (manual) {
      _emitFirmwareRelease(
        FirmwareReleasePhase.failed,
        release: previousStatus.release,
        package: previousStatus.package,
        progress: previousStatus.progress,
        message: message,
        isManual: true,
      );
    } else if (previousStatus.phase != FirmwareReleasePhase.checking &&
        previousStatus.phase != FirmwareReleasePhase.idle) {
      _emitFirmwareRelease(
        previousStatus.phase,
        release: previousStatus.release,
        package: previousStatus.package,
        progress: previousStatus.progress,
        message: previousStatus.message,
      );
    } else {
      _emitFirmwareRelease(FirmwareReleasePhase.idle);
    }
    _scheduleFirmwareReleaseCheck(failed: true);
  }

  void _scheduleFirmwareReleaseCheck({required bool failed}) {
    _firmwareReleaseRetryTimer?.cancel();
    _firmwareReleaseRetryTimer = null;
    if (_disposed ||
        !_appActive ||
        _firmwareReleaseRepository == null ||
        !supportsFirmwareUpload ||
        _hasCachedFirmwareReleasePackage ||
        _firmwareReleaseStatus.phase == FirmwareReleasePhase.downloading ||
        _firmwareReleaseStatus.phase == FirmwareReleasePhase.canceling ||
        _firmwareReleaseStatus.phase == FirmwareReleasePhase.installing) {
      return;
    }

    final Duration delay;
    if (failed) {
      _firmwareReleaseCheckFailures = min(
        _firmwareReleaseCheckFailures + 1,
        16,
      );
      final exponent = min(_firmwareReleaseCheckFailures - 1, 6);
      final milliseconds = min(
        _firmwareReleaseRetryMaximum.inMilliseconds,
        _firmwareReleaseRetryBase.inMilliseconds * (1 << exponent),
      );
      delay = Duration(milliseconds: milliseconds);
    } else {
      _firmwareReleaseCheckFailures = 0;
      delay = _firmwareReleaseRefreshInterval;
    }

    _firmwareReleaseRetryTimer = Timer(delay, () {
      _firmwareReleaseRetryTimer = null;
      _startAutomaticFirmwareReleaseCheck();
    });
  }

  bool get _hasCachedFirmwareReleasePackage {
    final release = _firmwareReleaseStatus.release;
    final package = _firmwareReleaseStatus.package;
    final bytes = _downloadedFirmwareBytes;
    return release != null &&
        package != null &&
        bytes != null &&
        identical(package.bytes, bytes) &&
        package.firmwareVersion == release.version &&
        package.target == release.target;
  }

  Future<FirmwarePackage> downloadAvailableFirmware() {
    final running = _firmwareReleaseDownloadOperation;
    if (running != null) return running;
    _firmwareReleaseRetryTimer?.cancel();
    _firmwareReleaseRetryTimer = null;
    final cancellationToken = FirmwareDownloadCancellationToken();
    final generation = ++_firmwareDownloadGeneration;
    _firmwareDownloadCancellationToken = cancellationToken;
    late final Future<FirmwarePackage> operation;
    operation =
        _performFirmwareReleaseDownload(
          generation: generation,
          cancellationToken: cancellationToken,
        ).whenComplete(() {
          cancellationToken.cancel();
          if (identical(_firmwareReleaseDownloadOperation, operation)) {
            _firmwareReleaseDownloadOperation = null;
          }
          if (identical(
            _firmwareDownloadCancellationToken,
            cancellationToken,
          )) {
            _firmwareDownloadCancellationToken = null;
          }
        });
    _firmwareReleaseDownloadOperation = operation;
    return operation;
  }

  Future<FirmwarePackage> _performFirmwareReleaseDownload({
    required int generation,
    required FirmwareDownloadCancellationToken cancellationToken,
  }) async {
    final repository = _firmwareReleaseRepository;
    final release = _firmwareReleaseStatus.release;
    if (repository == null || release == null) {
      throw const ControllerApiException(
        code: 'firmware_release_unavailable',
        message: 'Nie wybrano dostępnego wydania firmware.',
      );
    }
    if (!supportsFirmwareUpload) {
      throw ControllerApiException(
        code: 'ota_unavailable',
        message:
            firmwareUpdateBlockReason ??
            'Sterownik nie jest gotowy do aktualizacji.',
      );
    }
    if (_hasCachedFirmwareReleasePackage) {
      final package = _firmwareReleaseStatus.package!;
      _emitFirmwareRelease(
        FirmwareReleasePhase.readyToInstall,
        release: release,
        package: package,
        progress: 1,
        message:
            'Integralność i zgodność pakietu są sprawdzone. '
            'Podpis RSA zweryfikuje sterownik przed instalacją.',
      );
      return package;
    }

    _emitFirmwareRelease(
      FirmwareReleasePhase.downloading,
      release: release,
      progress: 0,
      message: 'Pobieranie podpisanego pakietu z GitHuba…',
    );
    try {
      final bytes = await repository.downloadFirmwarePackage(
        release: release,
        onProgress: (progress) {
          if (!_canReportFirmwareDownloadProgress(
            generation,
            cancellationToken,
          )) {
            return;
          }
          _emitFirmwareRelease(
            FirmwareReleasePhase.downloading,
            release: release,
            progress: progress.clamp(0.0, 1.0),
            message: 'Pobieranie podpisanego pakietu z GitHuba…',
          );
        },
        cancellationToken: cancellationToken,
      );
      if (!_ownsFirmwareDownload(generation, cancellationToken) ||
          cancellationToken.isCanceled) {
        throw const FirmwareReleaseException(
          code: 'download_canceled',
          message: 'Pobieranie firmware zostało anulowane.',
        );
      }

      final package = inspectFirmwarePackage(bytes, release.asset.name);
      if (package.firmwareVersion != release.version ||
          package.target != release.target) {
        throw const ControllerApiException(
          code: 'release_package_mismatch',
          message: 'Pobrany pakiet nie odpowiada wybranemu wydaniu.',
        );
      }
      _downloadedFirmwareBytes = bytes;
      _emitFirmwareRelease(
        FirmwareReleasePhase.readyToInstall,
        release: release,
        package: package,
        progress: 1,
        message:
            'Integralność i zgodność pakietu są sprawdzone. '
            'Podpis RSA zweryfikuje sterownik przed instalacją.',
      );
      return package;
    } on FirmwareReleaseException catch (error) {
      if (_ownsFirmwareDownload(generation, cancellationToken)) {
        _downloadedFirmwareBytes = null;
        _emitFirmwareRelease(
          error.code == 'download_canceled'
              ? FirmwareReleasePhase.available
              : FirmwareReleasePhase.failed,
          release: release,
          message: error.message,
        );
      }
      throw ControllerApiException(code: error.code, message: error.message);
    } on ControllerApiException catch (error) {
      if (_ownsFirmwareDownload(generation, cancellationToken)) {
        _downloadedFirmwareBytes = null;
        _emitFirmwareRelease(
          FirmwareReleasePhase.failed,
          release: release,
          message: error.message,
        );
      }
      rethrow;
    } on Object {
      const error = ControllerApiException(
        code: 'firmware_download_failed',
        message: 'Nie udało się bezpiecznie pobrać firmware.',
      );
      if (_ownsFirmwareDownload(generation, cancellationToken)) {
        _downloadedFirmwareBytes = null;
        _emitFirmwareRelease(
          FirmwareReleasePhase.failed,
          release: release,
          message: error.message,
        );
      }
      throw error;
    }
  }

  bool _ownsFirmwareDownload(
    int generation,
    FirmwareDownloadCancellationToken cancellationToken,
  ) {
    return !_disposed &&
        generation == _firmwareDownloadGeneration &&
        identical(_firmwareDownloadCancellationToken, cancellationToken);
  }

  bool _canReportFirmwareDownloadProgress(
    int generation,
    FirmwareDownloadCancellationToken cancellationToken,
  ) {
    return _ownsFirmwareDownload(generation, cancellationToken) &&
        !cancellationToken.isCanceled &&
        _firmwareReleaseStatus.phase == FirmwareReleasePhase.downloading;
  }

  void cancelFirmwareDownload() {
    if (_firmwareReleaseStatus.phase == FirmwareReleasePhase.downloading) {
      final cancellationToken = _firmwareDownloadCancellationToken;
      _emitFirmwareRelease(
        FirmwareReleasePhase.canceling,
        release: _firmwareReleaseStatus.release,
        package: _firmwareReleaseStatus.package,
        progress: _firmwareReleaseStatus.progress,
        message: 'Anulowanie pobierania firmware…',
      );
      cancellationToken?.cancel();
    }
  }

  void _emitFirmwareRelease(
    FirmwareReleasePhase phase, {
    FirmwareRelease? release,
    FirmwarePackage? package,
    double progress = 0,
    String? message,
    bool isManual = false,
  }) {
    if (_disposed) return;
    _firmwareReleaseStatus = FirmwareReleaseStatus(
      phase: phase,
      release: release,
      package: package,
      progress: progress,
      message: message,
      isManual: isManual,
    );
    _notifySafely();
  }

  FirmwarePackage inspectFirmwarePackage(Uint8List bytes, String fileName) {
    try {
      return FirmwarePackageParser.parse(
        bytes,
        fileName: fileName,
        context: _firmwareValidationContext(),
        maximumImageBytes: ControllerApi.maximumFirmwareImageBytes,
      );
    } on FirmwarePackageException catch (error) {
      throw ControllerApiException(code: error.code, message: error.message);
    }
  }

  void clearFirmwareUpdateStatus() {
    if (_firmwareUpdateStatus.isActive) return;
    _firmwareUpdateStatus = const FirmwareUpdateStatus.idle();
    _notifySafely();
  }

  Future<ControllerActionResult> uploadFirmware(
    Uint8List bytes,
    String fileName, {
    void Function(int sent, int total)? onProgress,
  }) async {
    _requireLiveController();
    final pendingRefresh = _refreshOperation;
    if (pendingRefresh != null) await pendingRefresh;
    final pendingHeartbeat = _heartbeatOperation;
    if (pendingHeartbeat != null) await pendingHeartbeat;
    _requireLiveController();
    if (_firmwareUpdateStatus.isActive || _activeAction != null) {
      throw ControllerApiException(
        code: 'action_in_progress',
        message: _activeAction == null
            ? 'Aktualizacja firmware jest już wykonywana.'
            : 'Polecenie $_activeAction jest już wykonywane.',
      );
    }
    final blocked = firmwareUpdateBlockReason;
    if (blocked != null) {
      throw ControllerApiException(code: 'ota_unavailable', message: blocked);
    }

    final selectedRelease = _firmwareReleaseStatus.release;
    final installingDownloadedRelease =
        selectedRelease != null &&
        identical(bytes, _downloadedFirmwareBytes) &&
        fileName == selectedRelease.asset.name;
    final sessionToken = isDevelopment ? null : _requireAdminToken();
    if (isDevelopment) _requirePin();
    _activeAction = 'firmware_update';
    _beginBusy();
    _pollTimer?.cancel();
    _pollTimer = null;
    _webSessionTimer?.cancel();
    _webSessionTimer = null;
    _firmwareUpdateStatus = const FirmwareUpdateStatus(
      phase: FirmwareUpdatePhase.validating,
      progress: 0,
      message: 'Sprawdzanie integralności i zgodności pakietu…',
    );
    if (installingDownloadedRelease) {
      _emitFirmwareRelease(
        FirmwareReleasePhase.installing,
        release: selectedRelease,
        package: _firmwareReleaseStatus.package,
        progress: 0,
        message: 'Instalowanie pakietu po kontroli integralności i zgodności…',
      );
    }
    _notifySafely();

    try {
      final package = inspectFirmwarePackage(bytes, fileName);
      _firmwareUpdateStatus = FirmwareUpdateStatus(
        phase: FirmwareUpdatePhase.uploading,
        progress: 0,
        message: 'Wysyłanie sprawdzonego pakietu do sterownika…',
        package: package,
      );
      _notifySafely();

      if (isDevelopment) {
        onProgress?.call(bytes.length, bytes.length);
        _firmwareUpdateStatus = FirmwareUpdateStatus(
          phase: FirmwareUpdatePhase.succeeded,
          progress: 1,
          message: 'Aktualizacja OTA została zasymulowana w trybie DEV.',
          package: package,
        );
        return const ControllerActionResult(
          success: true,
          code: 'dev_simulated',
          message: 'Aktualizacja OTA została zasymulowana w trybie DEV.',
        );
      }

      final result = await _api!.uploadFirmware(
        bytes,
        fileName,
        sessionToken!,
        onProgress: (sent, total) {
          final progress = total <= 0 ? 0.0 : (sent / total).clamp(0.0, 1.0);
          _firmwareUpdateStatus = FirmwareUpdateStatus(
            phase: FirmwareUpdatePhase.uploading,
            progress: progress,
            message: 'Wysyłanie sprawdzonego pakietu do sterownika…',
            package: package,
          );
          _notifySafely();
          onProgress?.call(sent, total);
        },
      );
      _firmwareUpdateStatus = FirmwareUpdateStatus(
        phase: FirmwareUpdatePhase.awaitingRestart,
        progress: 1,
        message: result.message.isEmpty
            ? 'Sterownik zweryfikował podpis pakietu i uruchamia nowy firmware.'
            : result.message,
        package: package,
      );
      if (installingDownloadedRelease) {
        _emitFirmwareRelease(
          FirmwareReleasePhase.awaitingRestart,
          release: selectedRelease,
          package: package,
          progress: 1,
          message: 'Firmware przyjęty. Sterownik uruchamia się ponownie.',
        );
      }
      _clearAdminSession();
      return result;
    } on ControllerApiException catch (error) {
      if (error.isAuthenticationError) _clearAdminSession();
      _firmwareUpdateStatus = FirmwareUpdateStatus(
        phase: FirmwareUpdatePhase.failed,
        progress: 0,
        message: error.message,
        errorCode: error.code,
      );
      if (installingDownloadedRelease) {
        _emitFirmwareRelease(
          FirmwareReleasePhase.failed,
          release: selectedRelease,
          package: _firmwareReleaseStatus.package,
          message: error.message,
        );
      }
      rethrow;
    } on Object {
      const error = ControllerApiException(
        code: 'ota_internal_error',
        message: 'Nie udało się bezpiecznie przygotować aktualizacji.',
      );
      _firmwareUpdateStatus = const FirmwareUpdateStatus(
        phase: FirmwareUpdatePhase.failed,
        progress: 0,
        message: 'Nie udało się bezpiecznie przygotować aktualizacji.',
        errorCode: 'ota_internal_error',
      );
      if (installingDownloadedRelease) {
        _emitFirmwareRelease(
          FirmwareReleasePhase.failed,
          release: selectedRelease,
          package: _firmwareReleaseStatus.package,
          message: error.message,
        );
      }
      throw error;
    } finally {
      _activeAction = null;
      _endBusy();
      _notifySafely();
      if (!isDevelopment && !_disposed) {
        _schedulePoll(const Duration(seconds: 3));
      }
    }
  }

  FirmwarePackageValidationContext _firmwareValidationContext() {
    if (isDevelopment) {
      return const FirmwarePackageValidationContext(
        expectedTarget: FirmwareTarget.ili9341,
        currentVersion: '0.0.0',
        minimumSecurityVersion: 0,
        bootloaderVersion: 0xffff,
        maximumImageBytes: ControllerApi.maximumFirmwareImageBytes,
      );
    }

    final ota = _capabilities.section('ota');
    if (ota.isEmpty) {
      throw const ControllerApiException(
        code: 'ota_capabilities_missing',
        message: 'Sterownik nie udostępnił parametrów bezpiecznego OTA.',
      );
    }
    final advertisedProduct = ota.text('productId');
    if (advertisedProduct != FirmwarePackageParser.productId) {
      throw const ControllerApiException(
        code: 'ota_product_mismatch',
        message: 'Nie można potwierdzić zgodności produktu sterownika.',
      );
    }
    final advertisedKeyId = ota.text('keyId');
    if (advertisedKeyId != FirmwarePackageParser.trustedKeyId) {
      throw const ControllerApiException(
        code: 'ota_trust_anchor_mismatch',
        message: 'Sterownik używa innego klucza zaufania dla firmware.',
      );
    }

    final target = FirmwareTarget.fromCode(
      ota.text(
        'target',
        _capabilities
            .section('hardware')
            .text('panel', _status.section('firmware').text('target')),
      ),
    );
    if (target == null) {
      throw const ControllerApiException(
        code: 'ota_target_unknown',
        message: 'Sterownik nie podał wariantu wyświetlacza dla firmware.',
      );
    }

    final currentVersion = _capabilities.text(
      'firmwareVersion',
      _status.section('firmware').text('version'),
    );
    try {
      FirmwarePackageParser.compareSemanticVersions(
        currentVersion,
        currentVersion,
      );
    } on FirmwarePackageException {
      throw const ControllerApiException(
        code: 'ota_version_unknown',
        message: 'Sterownik nie podał poprawnej bieżącej wersji firmware.',
      );
    }

    if (!ota.containsKey('minimumSecurityVersion') ||
        !ota.containsKey('bootloaderVersion')) {
      throw const ControllerApiException(
        code: 'ota_security_contract_missing',
        message: 'Sterownik nie podał wersji zabezpieczeń lub bootloadera.',
      );
    }
    final minimumSecurityVersion = ota.integer('minimumSecurityVersion', -1);
    final bootloaderVersion = ota.integer('bootloaderVersion', -1);
    final updatePartitionBytes = ota.integer('updatePartitionBytes', -1);
    if (minimumSecurityVersion < 0 ||
        bootloaderVersion < 1 ||
        updatePartitionBytes < 1) {
      throw const ControllerApiException(
        code: 'ota_security_contract_invalid',
        message: 'Sterownik podał nieprawidłowe parametry bezpieczeństwa OTA.',
      );
    }

    return FirmwarePackageValidationContext(
      expectedTarget: target,
      currentVersion: currentVersion,
      minimumSecurityVersion: minimumSecurityVersion,
      bootloaderVersion: bootloaderVersion,
      maximumImageBytes: min(
        updatePartitionBytes,
        ControllerApi.maximumFirmwareImageBytes,
      ),
    );
  }

  String _requirePin() {
    final pin = _adminPin;
    if (pin == null) {
      throw const ControllerApiException(
        code: 'admin_required',
        message: 'Zaloguj administratora, aby wykonać tę operację.',
      );
    }
    if (_adminToken != null && _validAdminToken == null) {
      _clearAdminSession();
      throw const ControllerApiException(
        code: 'session_expired',
        statusCode: 401,
        message: 'Sesja administratora wygasła. Zaloguj się ponownie.',
      );
    }
    _armAdminSessionTimeout();
    return pin;
  }

  String _requireAdminToken() {
    final token = _validAdminToken;
    if (token == null) {
      if (_adminToken != null) _clearAdminSession();
      throw const ControllerApiException(
        code: 'secure_session_required',
        statusCode: 401,
        message:
            'Bezpieczna aktualizacja wymaga ponownego logowania '
            'administratora.',
      );
    }
    _armAdminSessionTimeout();
    return token;
  }

  ({String? sessionToken, String? legacyPin}) _protectedRequestAuthorization() {
    if (isBluetooth) {
      return (sessionToken: null, legacyPin: _requirePin());
    }
    final token = _validAdminToken;
    if (token != null) {
      _armAdminSessionTimeout();
      return (sessionToken: token, legacyPin: null);
    }
    if (_adminToken != null) {
      _clearAdminSession();
      throw const ControllerApiException(
        code: 'session_expired',
        statusCode: 401,
        message: 'Sesja administratora wygasła. Zaloguj się ponownie.',
      );
    }
    if (_allowsLegacyPinFallback) {
      return (sessionToken: null, legacyPin: _requirePin());
    }
    throw const ControllerApiException(
      code: 'secure_session_required',
      statusCode: 401,
      message:
          'Ta operacja wymaga bezpiecznej sesji administratora. '
          'Zaloguj się ponownie.',
    );
  }

  void _requireLiveController() {
    if (canIssueCommands) return;
    throw ControllerApiException(
      code: 'controller_unavailable',
      message:
          commandBlockReason ??
          'Sterownik nie jest gotowy do wykonania polecenia.',
    );
  }

  String? get _validAdminToken {
    final token = _adminToken;
    final expiresAt = _adminTokenExpiresAt;
    if (token == null ||
        expiresAt == null ||
        !expiresAt.isAfter(DateTime.now())) {
      return null;
    }
    return token;
  }

  bool get _allowsLegacyPinFallback {
    if (isLegacyBluetooth) return true;
    if (_api is! ControllerProtocolV2Api) return true;
    final auth = _capabilities.section('auth');
    if (auth['pinFallbackV1'] == true ||
        _capabilities['pinFallbackV1'] == true) {
      return true;
    }
    if (auth['pinFallbackV1'] == false ||
        _capabilities['pinFallbackV1'] == false ||
        protocolVersion >= 2) {
      return false;
    }
    if (!_capabilityDiscoveryAttempted) return false;

    final reportedVersion = _status
        .section('firmware')
        .text(
          'version',
          _status.text('firmwareVersion', _status.text('version')),
        )
        .trim();
    if (reportedVersion.isEmpty) return false;
    try {
      return FirmwarePackageParser.compareSemanticVersions(
            reportedVersion,
            '5.1.0',
          ) <
          0;
    } on FirmwarePackageException {
      return false;
    }
  }

  static bool _canFallbackToLegacyAuthentication(ControllerApiException error) {
    return error.statusCode == 404 ||
        const {
          'unknown_endpoint',
          'unknown_operation',
          'not_supported',
          'firmware_update_required',
        }.contains(error.code);
  }

  String _nextCommandId() {
    final timestamp = DateTime.now().microsecondsSinceEpoch.toRadixString(36);
    final random = List<int>.generate(
      3,
      (_) => _commandRandom.nextInt(1 << 20),
    ).map((value) => value.toRadixString(36).padLeft(4, '0')).join();
    return 'm${timestamp}_$random';
  }

  void _activateAdminSession(
    String pin, {
    ControllerAdminSession? secureSession,
  }) {
    _adminPin = secureSession == null || isBluetooth ? pin : null;
    _adminToken = secureSession?.token;
    _adminTokenExpiresAt = secureSession?.expiresAt;
    _armAdminSessionTimeout();
  }

  void _armAdminSessionTimeout() {
    _adminSessionTimer?.cancel();
    _adminSessionTimer = Timer(_adminSessionTimeout, () {
      if (_disposed) return;
      _clearAdminSession();
      _notifySafely();
    });
  }

  void _clearAdminSession() {
    _adminSessionTimer?.cancel();
    _adminSessionTimer = null;
    _adminPin = null;
    _adminToken = null;
    _adminTokenExpiresAt = null;
    _logs = <String, dynamic>{};
    _diagnostics = <String, dynamic>{};
  }

  void _revokeAdminSessionBestEffort(String? token) {
    final api = _api;
    if (_disposed ||
        isDevelopment ||
        token == null ||
        api is! ControllerProtocolV2Api) {
      return;
    }
    unawaited(() async {
      try {
        await (api as ControllerProtocolV2Api).revokeSession(token);
      } on Object {
        // Lifecycle nie może blokować zamknięcia widoku. Serwerowy token ma
        // krótki TTL, a jawne unieważnienie jest próbą best-effort.
      }
    }());
  }

  Future<void> _sendWebSessionHeartbeat() {
    if (_disposed ||
        !_appActive ||
        isDevelopment ||
        !_api!.supportsWebSession) {
      return Future<void>.value();
    }
    final pending = _heartbeatOperation;
    if (pending != null) return pending;

    late final Future<void> operation;
    operation = _performWebSessionHeartbeat().whenComplete(() {
      if (identical(_heartbeatOperation, operation)) {
        _heartbeatOperation = null;
      }
    });
    _heartbeatOperation = operation;
    return operation;
  }

  Future<void> _performWebSessionHeartbeat() async {
    try {
      await _api!.webSession(_webSessionId, 'active');
    } on ControllerApiException {
      // Status polling is authoritative. A heartbeat failure must not mark an
      // otherwise responsive local controller as offline.
    } on Object {
      // Platform I/O errors are intentionally isolated from control actions.
    }
  }

  ControllerActionResult _performDevelopmentAction(
    String name,
    Map<String, Object?> payload,
    String pin,
  ) {
    if (pin != '1234') {
      throw const ControllerApiException(
        code: 'invalid_pin',
        statusCode: 403,
        message: 'Nieprawidłowy PIN administratora.',
      );
    }
    final modules = _status.section('modules');
    final relays = _status.section('relays');
    final schedule = _status.section('schedule');
    final schedules = _status.section('schedules');
    final config = _status.section('config');
    final display = _status.section('display');
    final network = _status.section('network');
    final controlState = _status.section('controlState');

    switch (name) {
      case 'auth_check':
        break;
      case 'set_light':
      case 'set_light1':
        _setDevOutput(payload, modules, relays, 'light_on', 'light');
        break;
      case 'set_plant':
      case 'set_light2':
        _setDevOutput(payload, modules, relays, 'plant_light_on', 'plantLight');
        break;
      case 'set_filter':
        _setDevOutput(payload, modules, relays, 'filter_on', 'pump');
        break;
      case 'set_heater':
        _setDevOutput(payload, modules, relays, 'heater_on', 'heater');
        break;
      case 'set_aeration':
        _setDevOutput(payload, modules, relays, 'air_on', 'aeration');
        break;
      case 'feed_now':
        final feeding = _status.section('feeding');
        feeding['active'] = true;
        feeding['lastFeedEpoch'] =
            DateTime.now().millisecondsSinceEpoch ~/ 1000;
        feeding['lastResult'] = 'ok';
        Timer(const Duration(seconds: 3), () {
          if (_disposed) return;
          feeding['active'] = false;
          notifyListeners();
        });
        _addDevelopmentLog('Karmienie ręczne uruchomione z aplikacji.', false);
        break;
      case 'set_timed_override':
        final target = payload['target']?.toString() ?? '';
        final durationSeconds = _toInt(payload['durationSec'], 0);
        if (!_timedOverrideTargets.contains(target) ||
            payload['state'] == null ||
            durationSeconds < 30 ||
            durationSeconds > 86400) {
          throw const ControllerApiException(
            code: 'invalid_timed_override',
            message:
                'Sterowanie czasowe wymaga poprawnego kanału, stanu i czasu '
                'od 30 sekund do 24 godzin.',
          );
        }
        _setDevelopmentTimedOverride(
          controlState,
          target,
          _toBool(payload['state']),
          durationSeconds,
        );
        break;
      case 'clear_timed_override':
        final target = payload['target']?.toString() ?? '';
        if (!_timedOverrideTargets.contains(target)) {
          throw const ControllerApiException(
            code: 'invalid_override_target',
            message: 'Nieprawidłowy kanał sterowania czasowego.',
          );
        }
        _clearDevelopmentTimedOverride(controlState, target);
        break;
      case 'set_light_profile':
        final target = payload['target']?.toString() ?? '';
        final profile = payload['profile']?.toString().toLowerCase() ?? '';
        if (!const {'front', 'rear'}.contains(target) ||
            !const {'day', 'daybreak', 'night'}.contains(profile)) {
          throw const ControllerApiException(
            code: 'invalid_light_profile',
            message:
                'Profil Aquael wymaga lampy przedniej lub tylnej oraz trybu '
                'DAY, DAYBREAK albo NIGHT.',
          );
        }
        _setDevelopmentLightProfile(target, profile);
        _addDevelopmentLog(
          'Ustawiono lampę ${target == "front" ? "przednią" : "tylną"} '
          'w tryb ${profile.toUpperCase()}.',
          false,
        );
        break;
      case 'start_feeding_mode':
        final durationSeconds = _toInt(payload['durationSec'], 0);
        if (durationSeconds < 60 || durationSeconds > 3600) {
          throw const ControllerApiException(
            code: 'invalid_feeding_duration',
            message: 'Tryb karmienia może trwać od 1 do 60 minut.',
          );
        }
        _startDevelopmentControlMode(
          controlState,
          'feedingMode',
          durationSeconds,
        );
        if (_toBool(payload['dispense'])) {
          final feeding = _status.section('feeding');
          feeding['lastFeedEpoch'] =
              DateTime.now().millisecondsSinceEpoch ~/ 1000;
          feeding['lastResult'] = 'ok';
        }
        _addDevelopmentLog(
          'Uruchomiono tryb karmienia na $durationSeconds s.',
          false,
        );
        break;
      case 'stop_feeding_mode':
        _stopDevelopmentControlMode(controlState, 'feedingMode');
        break;
      case 'start_service_mode':
        final durationSeconds = _toInt(payload['durationSec'], 0);
        if (durationSeconds < 60 || durationSeconds > 7200) {
          throw const ControllerApiException(
            code: 'invalid_service_duration',
            message: 'Tryb serwisowy może trwać od 1 do 120 minut.',
          );
        }
        _startDevelopmentControlMode(
          controlState,
          'serviceMode',
          durationSeconds,
        );
        _addDevelopmentLog(
          'Uruchomiono tryb serwisowy na $durationSeconds s.',
          false,
        );
        break;
      case 'stop_service_mode':
        _stopDevelopmentControlMode(controlState, 'serviceMode');
        break;
      case 'save_schedule':
        _applyDevSchedule(payload, schedule, schedules);
        break;
      case 'save_temperature':
        final temperature = _status.section('temperature');
        final enabled = payload['heaterMode']?.toString() != '1';
        config['target_temp'] = _toDouble(payload['target'], 25);
        config['temp_hysteresis'] = _toDouble(payload['hysteresis'], 0.5);
        temperature['target'] = config['target_temp'];
        temperature['hysteresis'] = config['temp_hysteresis'];
        temperature['heaterMode'] = enabled ? 0 : 1;
        modules['heater_enabled'] = enabled;
        break;
      case 'save_co2':
        modules['co2_enabled'] = _toBool(payload['co2Enabled']);
        config['co2TargetPh'] = _toDouble(payload['targetPh'], 6.8);
        config['co2MaxTimeMin'] = _toInt(payload['co2Limit'], 180);
        break;
      case 'save_water':
        modules['water_level_enabled'] = _toBool(payload['waterEnabled']);
        _status.section('water')['timeoutSec'] = _toInt(
          payload['waterTimeout'],
          120,
        ).clamp(5, 300);
        break;
      case 'save_leak':
        modules['leak_enabled'] = _toBool(payload['leakEnabled']);
        _status.section('leak')['action'] =
            payload['leakAction']?.toString() ?? 'disable_all';
        break;
      case 'save_display':
        display['autoBrightness'] = _toBool(payload['autoBrightness']);
        display['profile'] = payload['profile']?.toString() ?? 'always_on';
        display['brightness'] = _toInt(
          payload['brightness'],
          100,
        ).clamp(10, 100);
        display['appliedBrightness'] = display['brightness'];
        break;
      case 'save_network':
        final ssid = payload['staSsid']?.toString().trim() ?? '';
        if (ssid.isEmpty || ssid.length > 32) {
          throw const ControllerApiException(
            code: 'wifi_profile_error',
            message: 'SSID musi zawierać od 1 do 32 znaków.',
          );
        }
        network['configuredStaSsid'] = ssid;
        break;
      case 'wifi_session_start':
        network['staConnecting'] = false;
        network['staConnected'] = true;
        network['serviceMode'] = true;
        break;
      case 'wifi_session_stop':
        network['staConnected'] = false;
        network['serviceMode'] = false;
        break;
      case 'sync_time_ntp':
        _applyDevelopmentClock(DateTime.now());
        network['lastTimeSyncOk'] = true;
        network['lastTimeSyncStatus'] = 'ntp';
        break;
      case 'clear_critical_logs':
        _logs['critical'] = <dynamic>[];
        _logs.section('counts')['critical'] = 0;
        break;
      case 'save_relays':
        final data = payload['data'];
        if (data == null || data.toString().length < 96) {
          throw const ControllerApiException(
            code: 'invalid_relay_profile',
            message: 'Profil musi zawierać kompletną mapę ośmiu kanałów.',
          );
        }
        break;
      case 'test_relay':
        final channel = _toInt(payload['channel'], 0);
        if (channel < 1 || channel > 8) {
          throw const ControllerApiException(
            code: 'invalid_relay_channel',
            message: 'Kanał musi być w zakresie 1–8.',
          );
        }
        break;
      case 'restart_device':
        _status.section('system')['uptime'] = 0;
        _addDevelopmentLog('Zasymulowano restart sterownika.', false);
        break;
      case 'factory_reset':
        _status = _createDevelopmentStatus();
        _logs = _createDevelopmentLogs();
        break;
      default:
        throw ControllerApiException(
          code: 'unknown_action',
          message: 'Nieznana akcja: $name.',
        );
    }
    _lastUpdate = DateTime.now();
    _addDevelopmentLog('DEV: wykonano akcję $name.', false);
    return const ControllerActionResult(
      success: true,
      code: 'dev_simulated',
      message: 'Operacja została zasymulowana w pamięci RAM.',
    );
  }

  void _setDevelopmentTimedOverride(
    JsonMap controlState,
    String target,
    bool state,
    int durationSeconds,
  ) {
    final overrides = controlState.list('overrides');
    _clearDevelopmentTimedOverride(controlState, target, restore: false);
    final previousState = _developmentTargetState(target);
    overrides.add({
      'target': target,
      'state': state,
      'remainingSec': durationSeconds,
      '_endsAtEpoch':
          DateTime.now().millisecondsSinceEpoch ~/ 1000 + durationSeconds,
      '_previousState': previousState,
    });
    _setDevelopmentTargetState(target, state);
  }

  void _clearDevelopmentTimedOverride(
    JsonMap controlState,
    String target, {
    bool restore = true,
  }) {
    final overrides = controlState.list('overrides');
    for (var index = overrides.length - 1; index >= 0; index--) {
      final override = jsonMap(overrides[index]);
      if (override.text('target') != target) continue;
      if (restore) {
        _setDevelopmentTargetState(
          target,
          override.flag('_previousState', _developmentTargetState(target)),
        );
      }
      overrides.removeAt(index);
    }
  }

  void _startDevelopmentControlMode(
    JsonMap controlState,
    String key,
    int durationSeconds,
  ) {
    final mode = controlState.section(key);
    mode
      ..['active'] = true
      ..['remainingSec'] = durationSeconds
      ..['_endsAtEpoch'] =
          DateTime.now().millisecondsSinceEpoch ~/ 1000 + durationSeconds;
  }

  static void _stopDevelopmentControlMode(JsonMap controlState, String key) {
    final mode = controlState.section(key);
    mode
      ..['active'] = false
      ..['remainingSec'] = 0
      ..remove('_endsAtEpoch');
  }

  bool _developmentTargetState(String target) {
    final modules = _status.section('modules');
    return switch (target) {
      'light1' => modules.flag('light_on'),
      'light2' => modules.flag('plant_light_on'),
      'filter' => modules.flag('filter_on'),
      'heater' => modules.flag('heater_on'),
      'aeration' => modules.flag('air_on'),
      'co2' => modules.flag('co2_on'),
      'water_dosing' => modules.flag('water_dosing_on'),
      _ => false,
    };
  }

  void _setDevelopmentTargetState(String target, bool enabled) {
    final modules = _status.section('modules');
    final relays = _status.section('relays');
    switch (target) {
      case 'light1':
        modules['light_on'] = enabled;
        modules['light1_on'] = enabled;
        relays['light'] = enabled;
        relays['light1'] = enabled;
        break;
      case 'light2':
        modules['plant_light_on'] = enabled;
        modules['light2_on'] = enabled;
        relays['plantLight'] = enabled;
        relays['light2'] = enabled;
        break;
      case 'filter':
        modules['filter_on'] = enabled;
        relays['pump'] = enabled;
        break;
      case 'heater':
        modules['heater_on'] = enabled;
        relays['heater'] = enabled;
        break;
      case 'aeration':
        modules['air_on'] = enabled;
        relays['aeration'] = enabled;
        break;
      case 'co2':
        modules['co2_on'] = enabled;
        relays['co2'] = enabled;
        break;
      case 'water_dosing':
        modules['water_dosing_on'] = enabled;
        relays['waterDosing'] = enabled;
        _status.section('water')['active'] = enabled;
        break;
    }
  }

  void _setDevelopmentLightProfile(String target, String profile) {
    final lights = _status.section('lights');
    final canonicalKey = target == 'front' ? 'front' : 'rear';
    final compatibilityKey = target == 'front' ? 'light1' : 'light2';
    final profileName = profile.toUpperCase();
    for (final key in [canonicalKey, compatibilityKey]) {
      final lamp = lights.section(key);
      lamp
        ..['on'] = true
        ..['profile'] = profile
        ..['profileName'] = profileName
        ..['transitioning'] = false
        ..['known'] = true;
    }
    _setDevelopmentTargetState(target == 'front' ? 'light1' : 'light2', true);
  }

  void _setDevOutput(
    Map<String, Object?> payload,
    JsonMap modules,
    JsonMap relays,
    String moduleKey,
    String relayKey,
  ) {
    final enabled = _toBool(payload['state']);
    modules[moduleKey] = enabled;
    relays[relayKey] = enabled;
    if (moduleKey == 'light_on') {
      modules['light1_on'] = enabled;
      relays['light1'] = enabled;
    } else if (moduleKey == 'plant_light_on') {
      modules['light2_on'] = enabled;
      relays['light2'] = enabled;
    }
  }

  void _applyDevSchedule(
    Map<String, Object?> payload,
    JsonMap schedule,
    JsonMap schedules,
  ) {
    schedule['lightMode'] = _toInt(
      payload['light1Mode'] ?? payload['lightMode'],
      schedule.integer('lightMode'),
    );
    schedule['plantLightMode'] = _toInt(
      payload['light2Mode'] ?? payload['plantLightMode'],
      schedule.integer('plantLightMode'),
    );
    schedule['filterMode'] = _toInt(
      payload['filterMode'],
      schedule.integer('filterMode'),
    );
    schedule['airMode'] = _toInt(
      payload['aerationMode'],
      schedule.integer('airMode'),
    );
    schedule['heaterMode'] = _toInt(
      payload['heaterMode'],
      schedule.integer('heaterMode'),
    );
    _applyTimeToSchedule(
      payload['light1Start'] ?? payload['dayStart'],
      schedule,
      'dayStart',
    );
    _applyTimeToSchedule(
      payload['light1End'] ?? payload['dayEnd'],
      schedule,
      'dayEnd',
    );
    _applyTimeToSchedule(
      payload['light2Start'] ?? payload['plantLightStart'],
      schedule,
      'plantStart',
    );
    _applyTimeToSchedule(
      payload['light2End'] ?? payload['plantLightEnd'],
      schedule,
      'plantEnd',
    );
    _applyTimeToSchedule(payload['filterOn'], schedule, 'filterStart');
    _applyTimeToSchedule(payload['filterOff'], schedule, 'filterEnd');
    _applyTimeToSchedule(payload['airOn'], schedule, 'airStart');
    _applyTimeToSchedule(payload['airOff'], schedule, 'airEnd');
    schedules['light'] = _scheduleObject(
      schedule.integer('lightMode'),
      formatClock(
        schedule.integer('dayStartHour'),
        schedule.integer('dayStartMin'),
      ),
      formatClock(
        schedule.integer('dayEndHour'),
        schedule.integer('dayEndMin'),
      ),
      payload['light1Profile']?.toString() ??
          payload['lightProfile']?.toString() ??
          'day',
      _toBool(payload['light1ProfileCycle'] ?? payload['lightProfileCycle']),
    );
    schedules['plant_light'] = _scheduleObject(
      schedule.integer('plantLightMode'),
      formatClock(
        schedule.integer('plantStartHour'),
        schedule.integer('plantStartMin'),
      ),
      formatClock(
        schedule.integer('plantEndHour'),
        schedule.integer('plantEndMin'),
      ),
      payload['light2Profile']?.toString() ??
          payload['plantLightProfile']?.toString() ??
          'day',
      _toBool(
        payload['light2ProfileCycle'] ?? payload['plantLightProfileCycle'],
      ),
    );
    schedules['filter'] = _scheduleObject(
      schedule.integer('filterMode'),
      formatClock(
        schedule.integer('filterStartHour'),
        schedule.integer('filterStartMin'),
      ),
      formatClock(
        schedule.integer('filterEndHour'),
        schedule.integer('filterEndMin'),
      ),
    );
    schedules['air'] = _scheduleObject(
      schedule.integer('airMode'),
      formatClock(
        schedule.integer('airStartHour'),
        schedule.integer('airStartMin'),
      ),
      formatClock(
        schedule.integer('airEndHour'),
        schedule.integer('airEndMin'),
      ),
    );
    final feeding = _status.section('feeding');
    feeding['freq'] = _toInt(payload['feedFreq'], 1);
    final feedTime =
        payload['feedTime']?.toString().split(':') ?? const ['14', '00'];
    feeding['hour'] = int.tryParse(feedTime.first) ?? 14;
    feeding['minute'] = feedTime.length > 1
        ? int.tryParse(feedTime[1]) ?? 0
        : 0;
  }

  static JsonMap _scheduleObject(
    int mode,
    String start,
    String end, [
    String? profile,
    bool profileCycle = false,
  ]) => {
    'mode': switch (mode) {
      1 => 'always_on',
      2 => 'always_off',
      _ => 'schedule',
    },
    'start': start,
    'end': end,
    'profile': ?profile,
    if (profile != null) 'profileCycle': profileCycle,
    if (profile != null) 'supportedProfiles': ['day', 'daybreak', 'night'],
  };

  static void _applyTimeToSchedule(
    Object? value,
    JsonMap schedule,
    String prefix,
  ) {
    final parts = value?.toString().split(':') ?? const [];
    if (parts.length != 2) return;
    final hour = int.tryParse(parts[0]);
    final minute = int.tryParse(parts[1]);
    if (hour == null ||
        minute == null ||
        hour < 0 ||
        hour > 23 ||
        minute < 0 ||
        minute > 59) {
      return;
    }
    schedule['${prefix}Hour'] = hour;
    schedule['${prefix}Min'] = minute;
  }

  void _tickDevelopment() {
    if (_disposed) return;
    final now = DateTime.now();
    final sensors = _status.section('sensors');
    final temperature = _status.section('temperature');
    final system = _status.section('system');
    final current = sensors.number('temp_c', 24.6);
    final target = temperature.number('target', 25);
    final heater = _status.section('relays').flag('heater');
    final next =
        (current +
                (heater ? 0.025 : -0.012) +
                (_random.nextDouble() - 0.5) * 0.018)
            .clamp(target - 2.0, target + 2.0);
    sensors['temp_c'] = double.parse(next.toStringAsFixed(2));
    sensors['ph'] = double.parse(
      (6.8 + sin(DateTime.now().millisecondsSinceEpoch / 90000) * 0.12)
          .toStringAsFixed(3),
    );
    sensors['ec'] =
        455 + sin(DateTime.now().millisecondsSinceEpoch / 120000) * 12;
    sensors['ldr'] =
        340 + (sin(DateTime.now().millisecondsSinceEpoch / 60000) * 80).round();
    temperature['current'] = sensors['temp_c'];
    system['uptime'] = system.integer('uptime') + 2;
    _status['uptime_ms'] = system.integer('uptime') * 1000;
    _applyDevelopmentClock(now);
    _applyDevelopmentLightProfiles(now);
    _updateDevelopmentControlState(now);
    final history = temperature.list('history');
    history.add({
      'value': sensors['temp_c'],
      'epoch': DateTime.now().millisecondsSinceEpoch ~/ 1000,
    });
    if (history.length > 144) history.removeAt(0);
    _lastUpdate = DateTime.now();
    notifyListeners();
  }

  void _updateDevelopmentControlState(DateTime now) {
    final epoch = now.millisecondsSinceEpoch ~/ 1000;
    final controlState = _status.section('controlState');
    for (final key in const ['feedingMode', 'serviceMode']) {
      final mode = controlState.section(key);
      if (!mode.flag('active')) continue;
      final endsAt = mode.integer('_endsAtEpoch');
      final remaining = max(0, endsAt - epoch);
      mode['remainingSec'] = remaining;
      if (remaining == 0) {
        _stopDevelopmentControlMode(controlState, key);
      }
    }

    final overrides = controlState.list('overrides');
    for (var index = overrides.length - 1; index >= 0; index--) {
      final override = jsonMap(overrides[index]);
      final remaining = max(0, override.integer('_endsAtEpoch') - epoch);
      override['remainingSec'] = remaining;
      if (remaining > 0) continue;
      _setDevelopmentTargetState(
        override.text('target'),
        override.flag(
          '_previousState',
          _developmentTargetState(override.text('target')),
        ),
      );
      overrides.removeAt(index);
    }
  }

  void _applyDevelopmentClock(DateTime now) {
    final clock = _status.section('clock');
    clock
      ..['year'] = now.year
      ..['month'] = now.month
      ..['day'] = now.day
      ..['hour'] = now.hour
      ..['minute'] = now.minute
      ..['second'] = now.second
      ..['valid'] = true
      ..['source'] = 'dev';
  }

  void _applyDevelopmentLightProfiles(DateTime now) {
    final minute = now.hour * 60 + now.minute;
    final profile = switch (minute) {
      >= 600 && < 630 => ('daybreak', 'DAYBREAK'),
      >= 630 && < 1200 => ('day', 'DAY'),
      >= 1200 && < 1260 => ('daybreak', 'DAYBREAK'),
      >= 1260 && < 1320 => ('night', 'NIGHT'),
      _ => ('day', 'DAY'),
    };
    final active = minute >= 600 && minute < 1320;
    final schedules = _status.section('schedules');
    final relays = _status.section('relays');
    final modules = _status.section('modules');
    final lights = _status.section('lights');
    for (final entry in [
      (
        schedule: schedules.section('light'),
        light: lights.section('light1'),
        canonicalLight: lights.section('front'),
        moduleLegacy: 'light_on',
        moduleCanonical: 'light1_on',
        relayLegacy: 'light',
        relayCanonical: 'light1',
      ),
      (
        schedule: schedules.section('plant_light'),
        light: lights.section('light2'),
        canonicalLight: lights.section('rear'),
        moduleLegacy: 'plant_light_on',
        moduleCanonical: 'light2_on',
        relayLegacy: 'plantLight',
        relayCanonical: 'light2',
      ),
    ]) {
      if (entry.schedule.flag('profileCycle') &&
          entry.schedule.text('mode') == 'schedule') {
        entry.schedule['profile'] = profile.$1;
        entry.schedule['profileName'] = profile.$2;
        entry.schedule['active'] = active;
        entry.light['profile'] = profile.$1;
        entry.light['profileName'] = profile.$2;
        entry.light['on'] = active;
        entry.canonicalLight['profile'] = profile.$1;
        entry.canonicalLight['profileName'] = profile.$2;
        entry.canonicalLight['on'] = active;
        entry.canonicalLight['transitioning'] = false;
        entry.canonicalLight['known'] = true;
        modules[entry.moduleLegacy] = active;
        modules[entry.moduleCanonical] = active;
        relays[entry.relayLegacy] = active;
        relays[entry.relayCanonical] = active;
      }
    }
  }

  void _addDevelopmentLog(String message, bool critical) {
    final key = critical ? 'critical' : 'normal';
    final entries = _logs.list(key);
    entries.insert(0, {
      'ts': DateTime.now().millisecondsSinceEpoch ~/ 1000,
      'level': critical ? 'error' : 'info',
      'code': critical ? 'wazne' : 'info',
      'message': message,
    });
    if (entries.length > 100) entries.removeLast();
    _logs.section('counts')[key] = entries.length;
  }

  static bool _toBool(Object? value) {
    if (value is bool) return value;
    if (value is num) return value != 0;
    return const {
      '1',
      'true',
      'on',
      'tak',
    }.contains(value?.toString().toLowerCase());
  }

  static int _toInt(Object? value, int fallback) {
    if (value is num) return value.toInt();
    return int.tryParse(value?.toString() ?? '') ?? fallback;
  }

  static double _toDouble(Object? value, double fallback) {
    if (value is num) return value.toDouble();
    return double.tryParse(value?.toString() ?? '') ?? fallback;
  }

  static JsonMap _createOfflineStatus() {
    return <String, dynamic>{
      'device': 'AquaCYD',
      'mode': 'OFFLINE',
      'sensors': <String, dynamic>{},
      'modules': <String, dynamic>{},
      'relays': <String, dynamic>{},
      'alarms': <String, dynamic>{},
      'temperature': <String, dynamic>{'history': <dynamic>[]},
      'network': <String, dynamic>{'connected': false},
      'system': <String, dynamic>{},
      'schedule': <String, dynamic>{},
      'schedules': <String, dynamic>{},
      'feeding': <String, dynamic>{},
    };
  }

  static JsonMap _createDevelopmentStatus() {
    final now = DateTime.now();
    final epoch = now.millisecondsSinceEpoch ~/ 1000;
    final history = List<dynamic>.generate(36, (index) {
      final phase = index / 5;
      return {
        'value': double.parse((24.6 + sin(phase) * 0.35).toStringAsFixed(2)),
        'epoch': epoch - (35 - index) * 300,
      };
    });
    return {
      'device': 'cydAkwarium',
      'mode': 'DEV',
      'portal_ip': '127.0.0.1',
      'ip': '127.0.0.1',
      'hostname': 'akwarium',
      'theme': 'dark',
      'clients': 1,
      'heap_free': 182400,
      'heap_largest': 118400,
      'sd_mounted': true,
      'sd_total_bytes': 8589934592,
      'sd_used_bytes': 148897792,
      'sd_free_bytes': 8441036800,
      'history_points': history.length,
      'uptime_ms': 7325000,
      'ota_active': false,
      'sensors': {
        'temp_c': 24.62,
        'temp_valid': true,
        'ph': 6.81,
        'ph_valid': true,
        'ec': 452.0,
        'ec_valid': true,
        'ldr': 338,
        'ldr_valid': true,
        'mcp_present': true,
        'mcp_valid': true,
        'mcp_ok': true,
        'water_level_high': true,
        'water_level_valid': true,
        'leak_detected': false,
        'leak_valid': true,
        'flow_active': true,
        'flow_valid': true,
        'supply_voltage': 5.08,
        'supply_valid': true,
      },
      'alarms': {
        'flags': 0,
        'activeCount': 0,
        'temperatureHigh': false,
        'temperatureLow': false,
        'phOutOfRange': false,
        'waterLevelLow': false,
        'leak': false,
        'supplyLow': false,
      },
      'config': {
        'target_temp': 25.0,
        'temp_hysteresis': 0.5,
        'co2TargetPh': 6.8,
        'co2MaxTimeMin': 180,
        'dev_mode': true,
        'modem_sleep': false,
        'always_screen_on': true,
        'sound_enabled': true,
        'quiet_hours_enabled': false,
        'quiet_start': '22:00',
        'quiet_end': '07:00',
      },
      'display': {
        'autoBrightness': true,
        'profile': 'always_on',
        'brightness': 100,
        'appliedBrightness': 73,
      },
      'water': {
        'timeoutSec': 120,
        'active': false,
        'timeoutLatched': false,
        'runtimeSec': 0,
      },
      'leak': {'action': 'disable_all'},
      'modules': {
        'light_on': true,
        'plant_light_on': true,
        'light1_on': true,
        'light2_on': true,
        'filter_on': true,
        'air_on': true,
        'co2_on': false,
        'heater_on': false,
        'heater_enabled': true,
        'ph_sensor_enabled': true,
        'co2_enabled': true,
        'ec_enabled': true,
        'water_level_enabled': true,
        'water_dosing_on': false,
        'leak_enabled': true,
        'flow_enabled': true,
        'feeder_enabled': true,
      },
      'schedules': {
        'light': _scheduleObject(0, '10:00', '22:00', 'day', true),
        'plant_light': _scheduleObject(0, '10:00', '22:00', 'day', true),
        'filter': _scheduleObject(0, '09:30', '22:30'),
        'air': _scheduleObject(0, '22:00', '09:00'),
        'feeder': {
          'enabled': true,
          'count': 1,
          'time1': '14:00',
          'time2': '20:00',
        },
      },
      'eco': {
        'safe_active': false,
        'quiet_window': false,
        'deep_ready': true,
        'rtc_ready': true,
        'wake_after_sec': 3600,
        'last_wake_cause': 0,
        'blockers': <dynamic>[],
      },
      'clock': {
        'year': now.year,
        'month': now.month,
        'day': now.day,
        'hour': now.hour,
        'minute': now.minute,
        'second': now.second,
        'valid': true,
        'source': 'dev',
        'staRetryCooldownMs': 0,
      },
      'temperature': {
        'current': 24.62,
        'target': 25.0,
        'hysteresis': 0.5,
        'historyCapacity': 144,
        'historyIntervalMinutes': 1,
        'history': history,
        'heaterMode': 0,
      },
      'battery': {'voltage': 3.24, 'percent': 91},
      'firmware': {
        'version': 'dev-mobile',
        'buildDate': '2026-07-03',
        'buildTime': '20:00:00',
      },
      'network': {
        'staConnected': true,
        'staConnecting': false,
        'apMode': false,
        'serviceMode': true,
        'serviceModePending': false,
        'staSsid': 'DEV-NETWORK',
        'configuredStaSsid': 'DEV-NETWORK',
        'configuredApSsid': 'cydAkwarium_AP',
        'ssid': 'DEV-NETWORK',
        'ip': '192.168.4.44',
        'rssi': -48,
        'clients': 1,
        'lastTimeSyncOk': true,
        'lastTimeSyncStatus': 'dev',
      },
      'web': {
        'focus': false,
        'activeClients': 1,
        'lastSeenMs': 0,
        'timeoutMs': 15000,
        'cpuProfile': 'mobile_dev',
        'localUiDeferred': false,
        'sensorControlIntervalMs': 1000,
      },
      'system': {
        'uptime': 7325,
        'powerMode': 'normal',
        'resetReason': '1',
        'freeHeap': 182400,
        'largestHeap': 118400,
      },
      'relays': {
        'light': true,
        'plantLight': true,
        'light1': true,
        'light2': true,
        'pump': true,
        'heater': false,
        'co2': false,
        'aeration': true,
        'waterDosing': false,
        'aerationPercent': 100,
      },
      'lights': {
        'front': {
          'on': true,
          'profile': 'day',
          'profileName': 'DAY',
          'profileCycle': true,
          'transitioning': false,
          'known': true,
        },
        'rear': {
          'on': true,
          'profile': 'day',
          'profileName': 'DAY',
          'profileCycle': true,
          'transitioning': false,
          'known': true,
        },
        'light1': {
          'on': true,
          'profile': 'day',
          'profileName': 'DAY',
          'profileCycle': true,
          'transitioning': false,
          'known': true,
        },
        'light2': {
          'on': true,
          'profile': 'day',
          'profileName': 'DAY',
          'profileCycle': true,
          'transitioning': false,
          'known': true,
        },
        'supportedProfiles': ['day', 'daybreak', 'night'],
      },
      'schedule': {
        'lightMode': 0,
        'dayStartHour': 10,
        'dayStartMin': 0,
        'dayEndHour': 22,
        'dayEndMin': 0,
        'airMode': 0,
        'airStartHour': 22,
        'airStartMin': 0,
        'airEndHour': 9,
        'airEndMin': 0,
        'filterMode': 0,
        'filterStartHour': 9,
        'filterStartMin': 30,
        'filterEndHour': 22,
        'filterEndMin': 30,
        'heaterMode': 0,
        'lightProfile': 0,
        'lightProfileName': 'DAY',
        'plantLightMode': 0,
        'plantStartHour': 10,
        'plantStartMin': 0,
        'plantEndHour': 22,
        'plantEndMin': 0,
        'plantLightProfile': 0,
        'plantLightProfileName': 'DAY',
      },
      'feeding': {
        'active': false,
        'freq': 1,
        'hour': 14,
        'minute': 0,
        'lastFeedEpoch': epoch - 18000,
        'lastResult': 'ok',
      },
      'controlState': {
        'feedingMode': {'active': false, 'remainingSec': 0},
        'serviceMode': {'active': false, 'remainingSec': 0},
        'overrides': <dynamic>[],
      },
    };
  }

  static JsonMap _createDevelopmentCapabilities() => {
    'apiVersions': [1, 2],
    'transports': ['http', 'ble'],
    'features': {
      'timedOverrides': true,
      'feedingMode': true,
      'serviceMode': true,
      'safeOta': true,
      'idempotency': true,
      'sessionAuth': true,
      'aquaelLightProfiles': true,
    },
    'limits': {
      'timedOverrideMinSec': 30,
      'timedOverrideMaxSec': 86400,
      'feedingModeMinSec': 60,
      'feedingModeMaxSec': 3600,
      'serviceModeMinSec': 60,
      'serviceModeMaxSec': 7200,
      'lightCycleOffMs': 1000,
      'lightResetOffMs': 6000,
    },
    'ota': {
      'target': 'ili9341',
      'productId': FirmwarePackageParser.productId,
      'keyId': FirmwarePackageParser.trustedKeyId,
      'bootloaderVersion': 1,
      'minimumSecurityVersion': 0,
      'updatePartitionBytes': 1966080,
      'rollbackAvailable': true,
      'pendingVerify': false,
      'state': 'valid',
    },
    'targets': _timedOverrideTargets.toList(growable: false),
  };

  static JsonMap _createDevelopmentLogs() => {
    'normal': <dynamic>[
      {
        'ts': DateTime.now().millisecondsSinceEpoch ~/ 1000 - 80,
        'level': 'info',
        'code': 'dev',
        'message': 'Uruchomiono kompletny symulator aplikacji mobilnej.',
      },
      {
        'ts': DateTime.now().millisecondsSinceEpoch ~/ 1000 - 3600,
        'level': 'info',
        'code': 'wifi',
        'message': 'Połączono z symulowaną siecią DEV-NETWORK.',
      },
    ],
    'critical': <dynamic>[],
    'counts': {'normal': 2, 'critical': 0},
  };

  static JsonMap _createDevelopmentDiagnostics() => {
    'ok': true,
    'simulated': true,
    'sda': 21,
    'scl': 22,
    'frequencyHz': 400000,
    'scanMs': 4,
    'count': 2,
    'truncated': false,
    'devices': <dynamic>[
      {'address': 32, 'hex': '0x20', 'type': 'mcp23017', 'configured': true},
      {'address': 72, 'hex': '0x48', 'type': 'ads1115', 'configured': true},
    ],
    'uart': {
      'ports': <dynamic>[
        {
          'port': 0,
          'active': true,
          'role': 'console',
          'tx': 1,
          'rx': 3,
          'baud': 115200,
          'format': '8N1',
        },
      ],
      'discoverySupported': false,
    },
    'oneWire': {
      'dataPin': 17,
      'scanMs': 8,
      'count': 1,
      'truncated': false,
      'devices': <dynamic>[
        {
          'rom': '28-0123456789AB-CD',
          'family': 40,
          'type': 'ds18b20',
          'crcValid': true,
        },
      ],
    },
  };

  @override
  void dispose() {
    if (_disposed) return;
    _revokeAdminSessionBestEffort(_validAdminToken);
    _disposed = true;
    _firmwareDownloadGeneration++;
    _firmwareDownloadCancellationToken?.cancel();
    _firmwareDownloadCancellationToken = null;
    _pollTimer?.cancel();
    _pollTimer = null;
    _developmentTimer?.cancel();
    _developmentTimer = null;
    _webSessionTimer?.cancel();
    _webSessionTimer = null;
    _adminSessionTimer?.cancel();
    _adminSessionTimer = null;
    _firmwareReleaseRetryTimer?.cancel();
    _firmwareReleaseRetryTimer = null;
    _adminPin = null;
    _adminToken = null;
    _adminTokenExpiresAt = null;
    _downloadedFirmwareBytes = null;
    _historyArchiveCache.clear();
    _firmwareReleaseRepository?.dispose();

    final api = _api;
    if (!isDevelopment && api != null && api.supportsWebSession) {
      unawaited(api.webSession(_webSessionId, 'close').catchError((_) {}));
    }
    if (!isDevelopment && api != null) {
      unawaited(api.disconnect().catchError((_) {}));
    }
    super.dispose();
  }
}
