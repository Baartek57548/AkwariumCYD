import 'package:shared_preferences/shared_preferences.dart';

final class AlarmNotificationPreferences {
  const AlarmNotificationPreferences({
    this.enabled = false,
    this.criticalEnabled = true,
    this.warningEnabled = true,
    this.resolvedEnabled = false,
    this.cooldown = const Duration(minutes: 30),
    this.backgroundSyncEnabled = false,
    this.webhookRelayEnabled = false,
  });

  final bool enabled;
  final bool criticalEnabled;
  final bool warningEnabled;
  final bool resolvedEnabled;
  final Duration cooldown;

  /// Opt-in. WorkManager nie jest uruchamiany bez świadomej zgody użytkownika.
  final bool backgroundSyncEnabled;

  /// Opt-in. Sam sekret webhooka jest przechowywany poza SharedPreferences.
  final bool webhookRelayEnabled;

  AlarmNotificationPreferences copyWith({
    bool? enabled,
    bool? criticalEnabled,
    bool? warningEnabled,
    bool? resolvedEnabled,
    Duration? cooldown,
    bool? backgroundSyncEnabled,
    bool? webhookRelayEnabled,
  }) {
    return AlarmNotificationPreferences(
      enabled: enabled ?? this.enabled,
      criticalEnabled: criticalEnabled ?? this.criticalEnabled,
      warningEnabled: warningEnabled ?? this.warningEnabled,
      resolvedEnabled: resolvedEnabled ?? this.resolvedEnabled,
      cooldown: cooldown ?? this.cooldown,
      backgroundSyncEnabled:
          backgroundSyncEnabled ?? this.backgroundSyncEnabled,
      webhookRelayEnabled: webhookRelayEnabled ?? this.webhookRelayEnabled,
    );
  }
}

final class AlarmPreferencesStore {
  AlarmPreferencesStore({SharedPreferencesAsync? preferences})
    : _injectedPreferences = preferences;

  static const _prefix = 'alarm_center_v1_';
  final SharedPreferencesAsync? _injectedPreferences;
  SharedPreferencesAsync? _defaultPreferences;

  SharedPreferencesAsync get _preferences =>
      _injectedPreferences ??
      (_defaultPreferences ??= SharedPreferencesAsync());

  Future<AlarmNotificationPreferences> load() async {
    try {
      final cooldownMinutes =
          await _preferences.getInt('${_prefix}cooldown_minutes') ?? 30;
      return AlarmNotificationPreferences(
        enabled: await _preferences.getBool('${_prefix}enabled') ?? false,
        criticalEnabled:
            await _preferences.getBool('${_prefix}critical_enabled') ?? true,
        warningEnabled:
            await _preferences.getBool('${_prefix}warning_enabled') ?? true,
        resolvedEnabled:
            await _preferences.getBool('${_prefix}resolved_enabled') ?? false,
        cooldown: Duration(minutes: cooldownMinutes.clamp(5, 1440)),
        backgroundSyncEnabled:
            await _preferences.getBool('${_prefix}background_sync') ?? false,
        webhookRelayEnabled:
            await _preferences.getBool('${_prefix}webhook_relay') ?? false,
      );
    } on Object {
      return const AlarmNotificationPreferences();
    }
  }

  Future<void> save(AlarmNotificationPreferences value) async {
    final minutes = value.cooldown.inMinutes.clamp(5, 1440);
    await Future.wait(<Future<void>>[
      _preferences.setBool('${_prefix}enabled', value.enabled),
      _preferences.setBool('${_prefix}critical_enabled', value.criticalEnabled),
      _preferences.setBool('${_prefix}warning_enabled', value.warningEnabled),
      _preferences.setBool('${_prefix}resolved_enabled', value.resolvedEnabled),
      _preferences.setInt('${_prefix}cooldown_minutes', minutes),
      _preferences.setBool(
        '${_prefix}background_sync',
        value.backgroundSyncEnabled,
      ),
      _preferences.setBool(
        '${_prefix}webhook_relay',
        value.webhookRelayEnabled,
      ),
    ]);
  }
}
