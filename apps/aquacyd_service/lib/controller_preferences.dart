import 'package:shared_preferences/shared_preferences.dart';

import 'controller_address.dart';

class ControllerPreferences {
  ControllerPreferences({SharedPreferencesAsync? preferences})
    : _injectedPreferences = preferences;

  static const _addressKey = 'controller_base_url';
  static const _autoReconnectKey = 'controller_auto_reconnect';
  final SharedPreferencesAsync? _injectedPreferences;
  SharedPreferencesAsync? _defaultPreferences;

  SharedPreferencesAsync get _preferences =>
      _injectedPreferences ??
      (_defaultPreferences ??= SharedPreferencesAsync());

  Future<Uri> loadAddress() async {
    return await loadSavedAddress() ??
        Uri.parse(ControllerAddress.defaultValue);
  }

  Future<Uri?> loadSavedAddress() async {
    final value = await _preferences.getString(_addressKey);
    if (value == null) return null;
    try {
      return ControllerAddress.parse(value);
    } on FormatException {
      await _preferences.remove(_addressKey);
      return null;
    }
  }

  Future<void> saveAddress(Uri address) async {
    await _preferences.setString(_addressKey, address.toString());
  }

  Future<bool> loadAutoReconnect() async {
    return await _preferences.getBool(_autoReconnectKey) ?? true;
  }

  Future<void> saveAutoReconnect(bool enabled) async {
    await _preferences.setBool(_autoReconnectKey, enabled);
  }

  Future<void> forgetController() async {
    await _preferences.remove(_addressKey);
  }
}
