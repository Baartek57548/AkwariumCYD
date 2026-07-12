import 'package:flutter/material.dart';
import 'package:shared_preferences/shared_preferences.dart';

class AppSettings {
  static final themeModeNotifier = ValueNotifier<ThemeMode>(ThemeMode.system);
  static final languageNotifier = ValueNotifier<Locale>(const Locale('pl'));
  static final usernameNotifier = ValueNotifier<String>('Użytkownik');

  static Future<void> init() async {
    final prefs = await SharedPreferences.getInstance();
    final theme = prefs.getString('theme_mode') ?? 'system';
    final lang = prefs.getString('selected_language') ?? 'pl';
    final user = prefs.getString('username') ?? 'Użytkownik';

    themeModeNotifier.value = _parseThemeMode(theme);
    languageNotifier.value = Locale(lang);
    usernameNotifier.value = user;
  }

  static Future<void> saveSettings(ThemeMode themeMode, String languageCode, String username) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString('theme_mode', themeMode.name);
    await prefs.setString('selected_language', languageCode);
    await prefs.setString('username', username);

    themeModeNotifier.value = themeMode;
    languageNotifier.value = Locale(languageCode);
    usernameNotifier.value = username;
  }

  static ThemeMode _parseThemeMode(String name) {
    switch (name) {
      case 'light':
        return ThemeMode.light;
      case 'dark':
        return ThemeMode.dark;
      default:
        return ThemeMode.system;
    }
  }
}
