import 'package:shared_preferences/shared_preferences.dart';

import 'app_update_models.dart';

class AppUpdatePreferences {
  AppUpdatePreferences(this._preferences);

  static const String _skippedVersionKey = 'app_update.skipped_version';
  static const String _remindVersionKey = 'app_update.remind_version';
  static const String _remindAfterKey = 'app_update.remind_after_utc_ms';
  static const String _lastCheckKey = 'app_update.last_check_utc_ms';
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
