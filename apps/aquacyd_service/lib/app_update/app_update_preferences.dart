import 'package:shared_preferences/shared_preferences.dart';

import 'app_update_models.dart';

class AppUpdateBackgroundOptions {
  const AppUpdateBackgroundOptions({
    this.systemNotificationsEnabled = true,
    this.downloadOnWifiEnabled = false,
  });

  final bool systemNotificationsEnabled;
  final bool downloadOnWifiEnabled;

  AppUpdateBackgroundOptions copyWith({
    bool? systemNotificationsEnabled,
    bool? downloadOnWifiEnabled,
  }) {
    return AppUpdateBackgroundOptions(
      systemNotificationsEnabled:
          systemNotificationsEnabled ?? this.systemNotificationsEnabled,
      downloadOnWifiEnabled:
          downloadOnWifiEnabled ?? this.downloadOnWifiEnabled,
    );
  }
}

class AppUpdatePreferences {
  AppUpdatePreferences(this._preferences);

  static const String _skippedVersionKey = 'app_update.skipped_version';
  static const String _remindVersionKey = 'app_update.remind_version';
  static const String _remindAfterKey = 'app_update.remind_after_utc_ms';
  static const String _lastCheckKey = 'app_update.last_check_utc_ms';
  static const String _backgroundNotificationKey =
      'app_update.background_notification_enabled';
  static const String _backgroundDownloadKey =
      'app_update.background_download_wifi_enabled';
  static const String _installedVersionKey = 'app_update.installed_version';
  static const String _lastNotifiedVersionKey =
      'app_update.last_notified_version';
  static const String _downloadedVersionKey = 'app_update.downloaded_version';
  static const String _downloadedPathKey = 'app_update.downloaded_path';
  static const String _downloadedSha256Key = 'app_update.downloaded_sha256';
  static const Duration maximumReminder = Duration(days: 31);
  static const Duration clockTolerance = Duration(minutes: 5);

  final SharedPreferences _preferences;

  static Future<AppUpdatePreferences> load() async {
    return AppUpdatePreferences(await SharedPreferences.getInstance());
  }

  String? get skippedVersion => _preferences.getString(_skippedVersionKey);

  String? get remindVersion => _preferences.getString(_remindVersionKey);

  DateTime? get remindAfter => _readUtcTimestamp(_remindAfterKey);

  DateTime? get lastCheck => _readUtcTimestamp(_lastCheckKey);

  AppUpdateBackgroundOptions get backgroundOptions =>
      AppUpdateBackgroundOptions(
        systemNotificationsEnabled:
            _preferences.getBool(_backgroundNotificationKey) ?? true,
        downloadOnWifiEnabled:
            _preferences.getBool(_backgroundDownloadKey) ?? false,
      );

  SemanticVersion? get installedVersion => SemanticVersion.tryParse(
    _preferences.getString(_installedVersionKey) ?? '',
  );

  SemanticVersion? get lastNotifiedVersion => SemanticVersion.tryParse(
    _preferences.getString(_lastNotifiedVersionKey) ?? '',
  );

  SemanticVersion? get downloadedVersion => SemanticVersion.tryParse(
    _preferences.getString(_downloadedVersionKey) ?? '',
  );

  String? get downloadedPath => _preferences.getString(_downloadedPathKey);

  String? get downloadedSha256 => _preferences.getString(_downloadedSha256Key);

  bool shouldRunAutomaticCheck(DateTime now, Duration interval) {
    final checkedAt = lastCheck;
    if (checkedAt == null) return true;
    final normalizedNow = now.toUtc();
    if (checkedAt.isAfter(normalizedNow.add(clockTolerance))) return true;
    return normalizedNow.difference(checkedAt) >= interval;
  }

  bool isSkipped(SemanticVersion version) {
    return skippedVersion == version.toString();
  }

  bool isDeferred(SemanticVersion version, DateTime now) {
    if (remindVersion != version.toString()) return false;
    final after = remindAfter;
    if (after == null) return false;
    final normalizedNow = now.toUtc();
    if (after.isAfter(normalizedNow.add(maximumReminder))) return false;
    return after.isAfter(normalizedNow);
  }

  Future<void> recordCheck(DateTime now) {
    return _preferences.setInt(
      _lastCheckKey,
      now.toUtc().millisecondsSinceEpoch,
    );
  }

  Future<void> saveBackgroundOptions(AppUpdateBackgroundOptions value) async {
    await Future.wait<bool>(<Future<bool>>[
      _preferences.setBool(
        _backgroundNotificationKey,
        value.systemNotificationsEnabled,
      ),
      _preferences.setBool(_backgroundDownloadKey, value.downloadOnWifiEnabled),
    ]);
  }

  Future<void> recordInstalledVersion(SemanticVersion version) {
    return _preferences.setString(_installedVersionKey, version.toString());
  }

  bool shouldNotify(SemanticVersion release) {
    final installed = installedVersion;
    if (installed == null || release <= installed) return false;
    final notified = lastNotifiedVersion;
    return notified == null || release > notified;
  }

  Future<void> recordNotified(SemanticVersion version) {
    return _preferences.setString(_lastNotifiedVersionKey, version.toString());
  }

  bool hasMatchingDownload(ReleaseAsset asset, SemanticVersion version) {
    final path = downloadedPath;
    final digest = downloadedSha256;
    return path != null &&
        path.isNotEmpty &&
        downloadedVersion == version &&
        digest == asset.sha256;
  }

  Future<void> recordDownloaded({
    required SemanticVersion version,
    required String path,
    required String sha256,
  }) async {
    if (path.trim().isEmpty || !RegExp(r'^[0-9a-f]{64}$').hasMatch(sha256)) {
      throw ArgumentError('Nieprawidłowe metadane pobranej aktualizacji.');
    }
    await Future.wait<bool>(<Future<bool>>[
      _preferences.setString(_downloadedVersionKey, version.toString()),
      _preferences.setString(_downloadedPathKey, path),
      _preferences.setString(_downloadedSha256Key, sha256),
    ]);
  }

  Future<void> clearDownloaded() async {
    await Future.wait<bool>(<Future<bool>>[
      _preferences.remove(_downloadedVersionKey),
      _preferences.remove(_downloadedPathKey),
      _preferences.remove(_downloadedSha256Key),
    ]);
  }

  Future<void> skip(SemanticVersion version) async {
    await _preferences.setString(_skippedVersionKey, version.toString());
    await clearReminder();
  }

  Future<void> remindLater(
    SemanticVersion version,
    DateTime now,
    Duration delay,
  ) async {
    final safeDelay = delay > maximumReminder ? maximumReminder : delay;
    await _preferences.setString(_remindVersionKey, version.toString());
    await _preferences.setInt(
      _remindAfterKey,
      now.toUtc().add(safeDelay).millisecondsSinceEpoch,
    );
  }

  Future<void> clearReminder() async {
    await _preferences.remove(_remindVersionKey);
    await _preferences.remove(_remindAfterKey);
  }

  Future<void> cleanForInstalledVersion(SemanticVersion installed) async {
    await recordInstalledVersion(installed);
    final skipped = SemanticVersion.tryParse(skippedVersion ?? '');
    if (skipped == null || skipped <= installed) {
      await _preferences.remove(_skippedVersionKey);
    }

    final reminded = SemanticVersion.tryParse(remindVersion ?? '');
    final after = remindAfter;
    final now = DateTime.now().toUtc();
    final invalidReminder =
        reminded == null ||
        reminded <= installed ||
        after == null ||
        after.isAfter(now.add(maximumReminder));
    if (invalidReminder) await clearReminder();

    final notified = lastNotifiedVersion;
    if (notified == null || notified <= installed) {
      await _preferences.remove(_lastNotifiedVersionKey);
    }
    final downloaded = downloadedVersion;
    if (downloaded == null || downloaded <= installed) {
      await clearDownloaded();
    }

    final checkedAt = lastCheck;
    if (checkedAt != null && checkedAt.isAfter(now.add(clockTolerance))) {
      await _preferences.remove(_lastCheckKey);
    }
  }

  DateTime? _readUtcTimestamp(String key) {
    final milliseconds = _preferences.getInt(key);
    if (milliseconds == null || milliseconds < 0) return null;
    try {
      return DateTime.fromMillisecondsSinceEpoch(milliseconds, isUtc: true);
    } on RangeError {
      return null;
    }
  }
}
