import 'dart:async';
import 'dart:math';

import 'package:flutter/foundation.dart';

import 'controller_api.dart';
import 'connection_health.dart';
import 'data_access.dart';
import 'history_data.dart';

enum ControllerSessionKind { wifi, bluetooth, development }

class ControllerSession extends ChangeNotifier {
  static const Duration _onlinePollInterval = Duration(seconds: 3);
  static const Duration _maximumReconnectDelay = Duration(seconds: 30);
  static const Duration _heartbeatInterval = Duration(seconds: 5);
  static const int _offlineFailureThreshold = 3;
  static const int _maximumCachedArchives = 2;
  static const int _maximumArchiveBytes = 8 * 1024 * 1024;

  ControllerSession.wifi(ControllerRemoteApi api)
    : kind = ControllerSessionKind.wifi,
      _api = api,
      _status = <String, dynamic>{};

  ControllerSession.bluetooth(ControllerRemoteApi api)
    : kind = ControllerSessionKind.bluetooth,
      _api = api,
      _status = <String, dynamic>{};

  ControllerSession.development()
    : kind = ControllerSessionKind.development,
      _api = null,
      _status = _createDevelopmentStatus() {
    _logs = _createDevelopmentLogs();
    _diagnostics = _createDevelopmentDiagnostics();
  }

  final ControllerSessionKind kind;
  final ControllerRemoteApi? _api;
  JsonMap _status;
  JsonMap _logs = <String, dynamic>{};
  JsonMap _diagnostics = <String, dynamic>{};
  List<dynamic> _historyFiles = const [];
  final Map<String, List<HistorySample>> _historyArchiveCache = {};
  Timer? _pollTimer;
  Timer? _developmentTimer;
  Timer? _webSessionTimer;
  Future<void>? _connectOperation;
  Future<void>? _refreshOperation;
  Future<void>? _heartbeatOperation;
  final String _webSessionId =
      'm${DateTime.now().microsecondsSinceEpoch.toRadixString(36)}';
  String? _adminPin;
  String? _error;
  String? _activeAction;
  bool _connected = false;
  bool _appActive = true;
  bool _disposed = false;
  int _busyOperations = 0;
  int _failedPolls = 0;
  DateTime? _lastUpdate;
  Duration? _lastRoundTrip;
  ControllerConnectionPhase _connectionPhase =
      ControllerConnectionPhase.connecting;
  final Random _random = Random(7357);

  ControllerSessionKind get sessionKind => kind;
  bool get isDevelopment => kind == ControllerSessionKind.development;
  bool get isBluetooth => kind == ControllerSessionKind.bluetooth;
  bool get connected => _connected;
  bool get busy => _busyOperations > 0;
  bool get isAdmin => _adminPin != null;
  String? get error => _error;
  DateTime? get lastUpdate => _lastUpdate;
  Duration? get roundTrip => _lastRoundTrip;
  ControllerConnectionPhase get connectionPhase => _connectionPhase;
  String? get activeAction => _activeAction;
  bool isActionPending(String name) => _activeAction == name;
  JsonMap get status => _status;
  JsonMap get logsData => _logs;
  JsonMap get diagnostics => _diagnostics;
  List<dynamic> get historyFiles => List.unmodifiable(_historyFiles);
  String get displayName => switch (kind) {
    ControllerSessionKind.development => 'AquaCYD DEV',
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
  bool get supportsFirmwareUpload =>
      isDevelopment || (_api?.supportsFirmwareUpload ?? false);
  bool get supportsFileDownload =>
      isDevelopment || (_api?.supportsFileDownload ?? false);

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
        return;
      }

      await _api!.connect();
      await refresh(includeHistory: _status.isEmpty, reportBusy: false);
      if (!_connected) return;

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

  Future<void> refresh({bool includeHistory = false, bool reportBusy = true}) {
    if (_disposed) return Future<void>.value();
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
      _status = next;
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

  Future<void> _refreshAfterMutation({bool includeHistory = true}) async {
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
      return;
    }

    if (isDevelopment) {
      _startDevelopmentTicker();
      _tickDevelopment();
      return;
    }

    unawaited(
      (_connectionPhase == ControllerConnectionPhase.online
              ? refresh(reportBusy: false)
              : connect(reportBusy: false))
          .whenComplete(_schedulePoll),
    );
    if (_api!.supportsWebSession) {
      unawaited(_sendWebSessionHeartbeat());
      _scheduleHeartbeat();
    }
  }

  void _startDevelopmentTicker() {
    if (!_appActive || _disposed || _developmentTimer != null) return;
    _developmentTimer = Timer.periodic(
      const Duration(seconds: 2),
      (_) => _tickDevelopment(),
    );
  }

  void _schedulePoll([Duration? delay]) {
    _pollTimer?.cancel();
    _pollTimer = null;
    if (_disposed || !_appActive || isDevelopment) return;

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
    _failedPolls += 1;
    _connected = false;
    _error = message;
    _connectionPhase =
        _lastUpdate != null && _failedPolls < _offlineFailureThreshold
        ? ControllerConnectionPhase.reconnecting
        : ControllerConnectionPhase.offline;
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
      _adminPin = normalized;
      notifyListeners();
      return const ControllerActionResult(
        success: true,
        code: 'ok',
        message: 'Tryb administratora aktywny.',
      );
    }
    final result = await _api!.authenticate(normalized);
    _adminPin = normalized;
    notifyListeners();
    return result;
  }

  void logout() {
    _adminPin = null;
    _logs = <String, dynamic>{};
    _diagnostics = <String, dynamic>{};
    notifyListeners();
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
    final pin = _adminPin;
    if (pin == null) {
      throw const ControllerApiException(
        code: 'admin_required',
        message: 'Ta operacja wymaga zalogowania administratora.',
      );
    }
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
      final result = isDevelopment
          ? _performDevelopmentAction(normalizedName, payload, pin)
          : await _api!.action(normalizedName, payload: payload, pin: pin);
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
        _adminPin = null;
      }
      rethrow;
    } finally {
      _activeAction = null;
      _endBusy();
      _notifySafely();
    }
  }

  Future<void> loadLogs() async {
    final pin = _requirePin();
    _beginBusy();
    _notifySafely();
    try {
      _logs = isDevelopment ? _logs : await _api!.logs(pin);
    } on ControllerApiException catch (error) {
      if (error.isAuthenticationError) _adminPin = null;
      rethrow;
    } finally {
      _endBusy();
      _notifySafely();
    }
  }

  Future<void> scanBuses() async {
    final pin = _requirePin();
    _beginBusy();
    _notifySafely();
    try {
      _diagnostics = isDevelopment
          ? _createDevelopmentDiagnostics()
          : await _api!.busDiagnostics(pin);
    } on ControllerApiException catch (error) {
      if (error.isAuthenticationError) _adminPin = null;
      rethrow;
    } finally {
      _endBusy();
      _notifySafely();
    }
  }

  Future<void> loadHistoryFiles() async {
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

  static String _durationLabel(Duration value) {
    if (value.inHours >= 24 && value.inHours % 24 == 0) {
      return '${value.inDays} d';
    }
    if (value.inHours >= 1) return '${value.inHours} h';
    return '${value.inMinutes} min';
  }

  Future<Uint8List> downloadCurrentHistory() async {
    if (isDevelopment) {
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
    final pin = _requirePin();
    if (isDevelopment) {
      _applyDevelopmentClock(DateTime.now());
      notifyListeners();
      return;
    }
    await _api!.setBrowserTime(
      DateTime.now().millisecondsSinceEpoch ~/ 1000,
      pin,
    );
    await refresh(reportBusy: false);
  }

  Future<ControllerActionResult> uploadFirmware(
    Uint8List bytes,
    String fileName, {
    void Function(int sent, int total)? onProgress,
  }) async {
    final pin = _requirePin();
    if (isDevelopment) {
      if (bytes.isEmpty || !fileName.toLowerCase().endsWith('.bin')) {
        throw const ControllerApiException(
          code: 'invalid_firmware',
          message: 'Wybierz niepusty plik firmware .bin.',
        );
      }
      onProgress?.call(bytes.length, bytes.length);
      return const ControllerActionResult(
        success: true,
        code: 'dev_simulated',
        message: 'Aktualizacja OTA została zasymulowana w trybie DEV.',
      );
    }
    if (!_api!.supportsFirmwareUpload) {
      throw const ControllerApiException(
        code: 'transport_unsupported',
        message: 'Aktualizacja OTA wymaga połączenia Wi-Fi.',
      );
    }
    return _api.uploadFirmware(bytes, fileName, pin, onProgress: onProgress);
  }

  String _requirePin() {
    final pin = _adminPin;
    if (pin == null) {
      throw const ControllerApiException(
        code: 'admin_required',
        message: 'Zaloguj administratora, aby wykonać tę operację.',
      );
    }
    return pin;
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
    _applyDevelopmentClock(DateTime.now());
    _applyDevelopmentLightProfiles(DateTime.now());
    final history = temperature.list('history');
    history.add({
      'value': sensors['temp_c'],
      'epoch': DateTime.now().millisecondsSinceEpoch ~/ 1000,
    });
    if (history.length > 144) history.removeAt(0);
    _lastUpdate = DateTime.now();
    notifyListeners();
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
        moduleLegacy: 'light_on',
        moduleCanonical: 'light1_on',
        relayLegacy: 'light',
        relayCanonical: 'light1',
      ),
      (
        schedule: schedules.section('plant_light'),
        light: lights.section('light2'),
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
        'light1': {
          'on': true,
          'profile': 'day',
          'profileName': 'DAY',
          'profileCycle': true,
        },
        'light2': {
          'on': true,
          'profile': 'day',
          'profileName': 'DAY',
          'profileCycle': true,
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
    };
  }

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
    _disposed = true;
    _pollTimer?.cancel();
    _pollTimer = null;
    _developmentTimer?.cancel();
    _developmentTimer = null;
    _webSessionTimer?.cancel();
    _webSessionTimer = null;
    _historyArchiveCache.clear();

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
