import 'dart:async';
import 'dart:io';

import 'package:flutter/foundation.dart';

import 'app_update_background.dart';
import 'app_update_models.dart';
import 'app_update_platform.dart';
import 'app_update_preferences.dart';
import 'github_update_service.dart';

enum AppUpdatePhase {
  disabled,
  idle,
  checking,
  upToDate,
  available,
  downloading,
  verifying,
  readyToInstall,
  awaitingInstallPermission,
  installerOpened,
  failed,
}

@immutable
class AppUpdateState {
  const AppUpdateState({
    required this.phase,
    required this.eventId,
    this.installedApp,
    this.release,
    this.progress = 0,
    this.message,
    this.isManual = false,
  });

  const AppUpdateState.idle()
    : phase = AppUpdatePhase.idle,
      eventId = 0,
      installedApp = null,
      release = null,
      progress = 0,
      message = null,
      isManual = false;

  final AppUpdatePhase phase;
  final int eventId;
  final InstalledAppInfo? installedApp;
  final AppRelease? release;
  final double progress;
  final String? message;
  final bool isManual;
}

class AppUpdateController extends ChangeNotifier {
  AppUpdateController({
    AppUpdateRepository? service,
    AppUpdatePlatform? platform,
    Future<AppUpdatePreferences> Function()? preferencesLoader,
    DateTime Function()? clock,
  }) : _service = service ?? GitHubUpdateService(),
       _platform = platform ?? const MethodChannelAppUpdatePlatform(),
       _preferencesLoader = preferencesLoader ?? AppUpdatePreferences.load,
       _clock = clock ?? DateTime.now;

  static const String productionPackageName =
      'pl.cydakwarium.cyd_aquarium_mobile';
  static const Duration automaticCheckInterval = Duration(hours: 12);
  static const Duration remindLaterDuration = Duration(hours: 24);

  final AppUpdateRepository _service;
  final AppUpdatePlatform _platform;
  final Future<AppUpdatePreferences> Function() _preferencesLoader;
  final DateTime Function() _clock;

  AppUpdateState _state = const AppUpdateState.idle();
  AppUpdatePreferences? _preferences;
  InstalledAppInfo? _installedApp;
  Future<void>? _initialization;
  Future<void>? _checkOperation;
  Future<void>? _installOperation;
  bool _disposed = false;
  bool _cancelDownload = false;
  bool _isForeground = true;
  int _eventId = 0;
  String? _downloadedApkPath;

  AppUpdateState get state => _state;

  AppUpdateBackgroundOptions get backgroundOptions =>
      _preferences?.backgroundOptions ?? const AppUpdateBackgroundOptions();

  bool get isEnabled =>
      _installedApp?.packageName == productionPackageName &&
      _state.phase != AppUpdatePhase.disabled;

  Future<void> initialize() {
    return _initialization ??= _initialize();
  }

  Future<void> start() async {
    await initialize();
    await checkForUpdates();
  }

  Future<void> _initialize() async {
    try {
      final results = await Future.wait<Object>([
        _platform.getInstallState(),
        _preferencesLoader(),
      ]);
      _installedApp = results[0] as InstalledAppInfo;
      _preferences = results[1] as AppUpdatePreferences;

      final installedVersion = _installedApp!.semanticVersion;
      if (_installedApp!.packageName != productionPackageName ||
          installedVersion == null) {
        _emit(
          AppUpdatePhase.disabled,
          message: 'Ten wariant aplikacji nie korzysta z aktualizacji current.',
        );
        return;
      }
      await _preferences!.cleanForInstalledVersion(installedVersion);
      await AppUpdateBackgroundService.applyOptions(
        _preferences!.backgroundOptions,
      );
      _emit(AppUpdatePhase.idle);
    } on AppUpdateException {
      _emit(AppUpdatePhase.disabled);
    } catch (_) {
      _emit(AppUpdatePhase.disabled);
    }
  }

  Future<void> checkForUpdates({bool manual = false}) async {
    await initialize();
    final running = _checkOperation;
    if (running != null) return running;
    final operation = _check(manual: manual);
    _checkOperation = operation;
    unawaited(
      operation.then<void>(
        (_) => _clearCheckOperation(operation),
        onError: (Object _, StackTrace _) => _clearCheckOperation(operation),
      ),
    );
    return operation;
  }

  Future<void> _check({required bool manual}) async {
    if (_installedApp == null || _preferences == null) return;
    if (!isEnabled || _isInstallFlowActive) return;

    final now = _clock().toUtc();
    if (!manual &&
        !_preferences!.shouldRunAutomaticCheck(now, automaticCheckInterval)) {
      return;
    }

    _emit(AppUpdatePhase.checking, isManual: manual);
    try {
      final release = await _service.fetchLatestMobileRelease();
      await _preferences!.recordCheck(now);
      final installedVersion = _installedApp!.semanticVersion!;
      if (release == null || release.version <= installedVersion) {
        _emit(AppUpdatePhase.upToDate, isManual: manual);
        return;
      }

      if (!manual &&
          (_preferences!.isSkipped(release.version) ||
              _preferences!.isDeferred(release.version, now))) {
        _emit(AppUpdatePhase.idle, release: release);
        return;
      }
      await _adoptBackgroundDownload(release);
      _emit(AppUpdatePhase.available, release: release, isManual: manual);
    } on AppUpdateException catch (error) {
      _emit(
        manual ? AppUpdatePhase.failed : AppUpdatePhase.idle,
        message: manual ? error.userMessage : null,
        isManual: manual,
      );
    } catch (_) {
      _emit(
        manual ? AppUpdatePhase.failed : AppUpdatePhase.idle,
        message: manual ? 'Nie udało się sprawdzić aktualizacji.' : null,
        isManual: manual,
      );
    }
  }

  void _clearCheckOperation(Future<void> operation) {
    if (identical(_checkOperation, operation)) _checkOperation = null;
  }

  Future<void> remindLater() async {
    final release = _state.release;
    final preferences = _preferences;
    if (release == null || preferences == null) return;
    await preferences.remindLater(
      release.version,
      _clock(),
      remindLaterDuration,
    );
    _emit(AppUpdatePhase.idle, release: release);
  }

  Future<void> saveBackgroundOptions(AppUpdateBackgroundOptions options) async {
    await initialize();
    final preferences = _preferences;
    if (preferences == null) return;
    await preferences.saveBackgroundOptions(options);
    await AppUpdateBackgroundService.applyOptions(options);
    notifyListeners();
  }

  Future<void> skipCurrentVersion() async {
    final release = _state.release;
    final preferences = _preferences;
    if (release == null || preferences == null) return;
    await preferences.skip(release.version);
    _emit(AppUpdatePhase.idle, release: release);
  }

  Future<void> downloadAndInstall() {
    final running = _installOperation;
    if (running != null) return running;
    final operation = _downloadAndInstall();
    _installOperation = operation;
    unawaited(
      operation.then<void>(
        (_) => _clearInstallOperation(operation),
        onError: (Object _, StackTrace _) => _clearInstallOperation(operation),
      ),
    );
    return operation;
  }

  Future<void> _downloadAndInstall() async {
    final release = _state.release;
    if (release == null || _isInstallFlowActive) return;
    _cancelDownload = false;
    try {
      if (await _hasUsableDownloadedApk(release)) {
        await _launchInstaller(release);
        return;
      }
      _emit(AppUpdatePhase.downloading, release: release, progress: 0);
      final updateDirectory = await _platform.getUpdateDirectory();
      final progressStopwatch = Stopwatch()..start();
      var lastProgress = 0.0;
      var lastProgressAt = 0;
      final apkPath = await _service.downloadApk(
        release: release,
        updateDirectory: updateDirectory,
        onProgress: (progress) {
          if (_disposed) return;
          final normalized = progress.clamp(0.0, 1.0);
          final elapsed = progressStopwatch.elapsedMilliseconds;
          if (normalized < 1 &&
              normalized - lastProgress < 0.01 &&
              elapsed - lastProgressAt < 100) {
            return;
          }
          lastProgress = normalized;
          lastProgressAt = elapsed;
          _emit(
            AppUpdatePhase.downloading,
            release: release,
            progress: normalized,
          );
        },
        isCanceled: () => _cancelDownload || _disposed,
      );
      _downloadedApkPath = apkPath;
      await _preferences?.recordDownloaded(
        version: release.version,
        path: apkPath,
        sha256: release.asset.sha256,
      );
      if (!_isForeground) {
        _emit(
          AppUpdatePhase.readyToInstall,
          release: release,
          progress: 1,
          message:
              'Aktualizacja została pobrana. Instalator otworzy się po powrocie do aplikacji.',
        );
        return;
      }
      await _launchInstaller(release);
    } on AppUpdateCanceledException {
      _emit(AppUpdatePhase.available, release: release);
    } on AppUpdateException catch (error) {
      if (error.code == 'INSTALL_PERMISSION_REQUIRED') {
        _emit(
          AppUpdatePhase.awaitingInstallPermission,
          release: release,
          message:
              'Włącz zgodę „Zezwalaj z tego źródła”, a następnie wróć do AquaCYD.',
        );
        try {
          await _platform.openUnknownSourcesSettings();
        } on AppUpdateException catch (settingsError) {
          _emit(
            AppUpdatePhase.failed,
            release: release,
            message: settingsError.userMessage,
          );
        }
      } else {
        _emit(
          AppUpdatePhase.failed,
          release: release,
          message: error.userMessage,
        );
      }
    } catch (_) {
      _emit(
        AppUpdatePhase.failed,
        release: release,
        message: 'Nie udało się przygotować aktualizacji.',
      );
    }
  }

  void _clearInstallOperation(Future<void> operation) {
    if (identical(_installOperation, operation)) _installOperation = null;
  }

  Future<void> _launchInstaller(AppRelease release) async {
    final apkPath = _downloadedApkPath;
    if (apkPath == null) {
      throw const AppUpdateException(
        'FILE_NOT_FOUND',
        'Pobrany plik aktualizacji nie istnieje.',
      );
    }
    _emit(AppUpdatePhase.verifying, release: release, progress: 1);
    await _platform.installApk(
      path: apkPath,
      expectedSha256: release.asset.sha256,
      expectedVersionName: release.version.toString(),
    );
    _emit(
      AppUpdatePhase.installerOpened,
      release: release,
      progress: 1,
      message:
          'Instalator Androida został otwarty. Potwierdź instalację nowej wersji.',
    );
  }

  Future<void> _adoptBackgroundDownload(AppRelease release) async {
    final preferences = _preferences;
    if (preferences == null ||
        !preferences.hasMatchingDownload(release.asset, release.version)) {
      return;
    }
    final path = preferences.downloadedPath;
    if (path != null && await File(path).exists()) {
      _downloadedApkPath = path;
      return;
    }
    await preferences.clearDownloaded();
  }

  Future<bool> _hasUsableDownloadedApk(AppRelease release) async {
    await _adoptBackgroundDownload(release);
    final path = _downloadedApkPath;
    return path != null && await File(path).exists();
  }

  Future<void> onAppResumed() async {
    _isForeground = true;
    await initialize();
    if (_state.phase == AppUpdatePhase.readyToInstall &&
        _state.release != null &&
        _downloadedApkPath != null) {
      try {
        await _launchInstaller(_state.release!);
      } on AppUpdateException catch (error) {
        _emit(
          AppUpdatePhase.failed,
          release: _state.release,
          message: error.userMessage,
        );
      }
      return;
    }
    if (_state.phase == AppUpdatePhase.awaitingInstallPermission &&
        _state.release != null &&
        _downloadedApkPath != null) {
      try {
        final installState = await _platform.getInstallState();
        _installedApp = installState;
        if (!installState.canRequestPackageInstalls) {
          _emit(
            AppUpdatePhase.awaitingInstallPermission,
            release: _state.release,
            message:
                'Zgoda nadal jest wyłączona. Otwórz ustawienia ponownie albo anuluj aktualizację.',
          );
          return;
        }
        await _launchInstaller(_state.release!);
      } on AppUpdateException catch (error) {
        _emit(
          AppUpdatePhase.failed,
          release: _state.release,
          message: error.userMessage,
        );
      }
      return;
    }
    await checkForUpdates();
  }

  void onAppBackgrounded() {
    _isForeground = false;
  }

  Future<void> reopenInstallPermissionSettings() async {
    try {
      await _platform.openUnknownSourcesSettings();
    } on AppUpdateException catch (error) {
      _emit(
        AppUpdatePhase.failed,
        release: _state.release,
        message: error.userMessage,
      );
    }
  }

  void cancelDownload() {
    if (_state.phase == AppUpdatePhase.downloading) {
      _cancelDownload = true;
    }
  }

  void closeInstallFlow() {
    if (_state.phase == AppUpdatePhase.downloading ||
        _state.phase == AppUpdatePhase.verifying) {
      return;
    }
    _emit(AppUpdatePhase.idle, release: _state.release);
  }

  Future<void> retryInstallFlow() async {
    final release = _state.release;
    if (release == null) return;
    if (_downloadedApkPath != null) {
      try {
        await _launchInstaller(release);
        return;
      } on AppUpdateException catch (error) {
        if (error.code == 'INSTALL_PERMISSION_REQUIRED') {
          _emit(
            AppUpdatePhase.awaitingInstallPermission,
            release: release,
            message:
                'Włącz zgodę „Zezwalaj z tego źródła”, a następnie wróć do AquaCYD.',
          );
          await reopenInstallPermissionSettings();
          return;
        }
      }
    }
    await downloadAndInstall();
  }

  bool get _isInstallFlowActive {
    return _state.phase == AppUpdatePhase.downloading ||
        _state.phase == AppUpdatePhase.verifying ||
        _state.phase == AppUpdatePhase.readyToInstall ||
        _state.phase == AppUpdatePhase.awaitingInstallPermission;
  }

  void _emit(
    AppUpdatePhase phase, {
    AppRelease? release,
    double progress = 0,
    String? message,
    bool isManual = false,
  }) {
    if (_disposed) return;
    _eventId++;
    _state = AppUpdateState(
      phase: phase,
      eventId: _eventId,
      installedApp: _installedApp,
      release: release,
      progress: progress,
      message: message,
      isManual: isManual,
    );
    notifyListeners();
  }

  @override
  void dispose() {
    _disposed = true;
    _cancelDownload = true;
    _service.dispose();
    super.dispose();
  }
}
