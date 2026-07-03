import 'package:shared_preferences/shared_preferences.dart';

import 'controller_address.dart';

class ControllerPreferences {
  ControllerPreferences({SharedPreferencesAsync? preferences})
    : _preferences = preferences ?? SharedPreferencesAsync();

  static const _addressKey = 'controller_base_url';
  final SharedPreferencesAsync _preferences;

  Future<Uri> loadAddress() async {
    final value = await _preferences.getString(_addressKey);
    if (value == null) {
      return Uri.parse(ControllerAddress.defaultValue);
    }

    try {
      return ControllerAddress.parse(value);
    } on FormatException {
      await _preferences.remove(_addressKey);
      return Uri.parse(ControllerAddress.defaultValue);
    }
  }

  Future<void> saveAddress(Uri address) async {
    await _preferences.setString(_addressKey, address.toString());
  }
}
