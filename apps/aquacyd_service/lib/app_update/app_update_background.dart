import 'dart:io';

import 'package:connectivity_plus/connectivity_plus.dart';
import 'package:flutter/foundation.dart';
import 'package:path_provider/path_provider.dart';
import 'package:workmanager/workmanager.dart';

import '../alarm_center/alarm_notifications.dart';
import 'app_update_preferences.dart';
import 'github_update_service.dart';

abstract interface class AppUpdateBackgroundConnectivity {
  Future<bool> get isWifi;
}

final class PlatformAppUpdateBackgroundConnectivity
    implements AppUpdateBackgroundConnectivity {
  PlatformAppUpdateBackgroundConnectivity({Connectivity? connectivity})
    : _connectivity = connectivity ?? Connectivity();

  final Connectivity _connectivity;

  @override
  Future<bool> get isWifi async {
    final results = await _connectivity.checkConnectivity();
    return results.contains(ConnectivityResult.wifi);
  }
}

final class AppUpdateBackgroundResult {
  const AppUpdateBackgroundResult({
    required this.succeeded,
    required this.reason,
    this.notified = false,
    this.downloaded = false,
  });

  final bool succeeded;
  final String reason;
  final bool notified;
  final bool downloaded;
}

final class AppUpdateBackgroundRunner {
  AppUpdateBackgroundRunner({
    Future<AppUpdatePreferences> Function()? preferencesLoader,
    AppUpdateRepository? repository,
    AlarmNotificationSink? notifications,
    AppUpdateBackgroundConnectivity? connectivity,
    Future<Directory> Function()? supportDirectory,
  }) : _preferencesLoader = preferencesLoader ?? AppUpdatePreferences.load,
       _repository = repository ?? GitHubUpdateService(),
       _ownsRepository = repository == null,
       _notifications = notifications ?? LocalAlarmNotificationSink(),
       _connectivity =
           connectivity ?? PlatformAppUpdateBackgroundConnectivity(),
       _supportDirectory = supportDirectory ?? getApplicationSupportDirectory;

  final Future<AppUpdatePreferences> Function() _preferencesLoader;
  final AppUpdateRepository _repository;
  final bool _ownsRepository;
  final AlarmNotificationSink _notifications;
  final AppUpdateBackgroundConnectivity _connectivity;
  final Future<Directory> Function() _supportDirectory;

  Future<AppUpdateBackgroundResult> run() async {
    try {
      final preferences = await _preferencesLoader();
      final options = preferences.backgroundOptions;
      if (!options.systemNotificationsEnabled &&
          !options.downloadOnWifiEnabled) {
        return const AppUpdateBackgroundResult(
          succeeded: true,
          reason: 'disabled',
        );
      }
      final installed = preferences.installedVersion;
      if (installed == null) {
        return const AppUpdateBackgroundResult(
          succeeded: true,
          reason: 'installed_version_unknown',
        );
      }
      final release = await _repository.fetchLatestMobileRelease();
      await preferences.recordCheck(DateTime.now());
      if (release == null || release.version <= installed) {
        return const AppUpdateBackgroundResult(
          succeeded: true,
          reason: 'up_to_date',
        );
      }

      var downloaded = preferences.hasMatchingDownload(
        release.asset,
        release.version,
      );
      if (downloaded) {
        final downloadedPath = preferences.downloadedPath;
        downloaded =
            downloadedPath != null && await File(downloadedPath).exists();
        if (!downloaded) await preferences.clearDownloaded();
      }
      if (!downloaded && options.downloadOnWifiEnabled) {
        final wifi = await _connectivity.isWifi;
        if (wifi) {
          final root = await _supportDirectory();
          final updateDirectory = Directory(
            '${root.path}${Platform.pathSeparator}updates',
          );
          final path = await _repository.downloadApk(
            release: release,
            updateDirectory: updateDirectory.path,
            onProgress: (_) {},
            isCanceled: () => false,
          );
          await preferences.recordDownloaded(
            version: release.version,
            path: path,
            sha256: release.asset.sha256,
          );
          downloaded = true;
        }
      }

      var notified = false;
      if (options.systemNotificationsEnabled &&
          preferences.shouldNotify(release.version) &&
          !preferences.isSkipped(release.version) &&
          !preferences.isDeferred(release.version, DateTime.now())) {
        await _notifications.showAppUpdate(
          tagName: release.tagName,
          version: release.version.toString(),
          downloaded: downloaded,
        );
        await preferences.recordNotified(release.version);
        notified = true;
      }
      return AppUpdateBackgroundResult(
        succeeded: true,
        reason: notified || downloaded ? 'update_available' : 'deduplicated',
        notified: notified,
        downloaded: downloaded,
      );
    } on Object {
      return const AppUpdateBackgroundResult(
        succeeded: false,
        reason: 'transient_failure',
      );
    } finally {
      if (_ownsRepository) _repository.dispose();
    }
  }
}

abstract final class AppUpdateBackgroundService {
  static const String uniqueTaskName = 'aquacyd-app-update-check-v1';
  static const String workerTaskName = 'aquacydAppUpdateCheck';

  static bool handles(String task) {
    return task == uniqueTaskName || task == workerTaskName;
  }

  static Future<void> applyOptions(AppUpdateBackgroundOptions options) async {
    if (kIsWeb || !Platform.isAndroid) return;
    if (!options.systemNotificationsEnabled && !options.downloadOnWifiEnabled) {
      await Workmanager().cancelByUniqueName(uniqueTaskName);
      return;
    }
    await Workmanager().registerPeriodicTask(
      uniqueTaskName,
      workerTaskName,
      frequency: const Duration(hours: 12),
      flexInterval: const Duration(hours: 2),
      constraints: Constraints(
        networkType: NetworkType.connected,
        requiresBatteryNotLow: true,
        requiresStorageNotLow: true,
      ),
      existingWorkPolicy: ExistingPeriodicWorkPolicy.update,
      backoffPolicy: BackoffPolicy.exponential,
      backoffPolicyDelay: const Duration(minutes: 30),
    );
  }
}
