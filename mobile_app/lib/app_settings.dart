import 'package:flutter/material.dart';
import 'package:shared_preferences/shared_preferences.dart';

class AppSettings {
  static const _themeModeKey = 'theme_mode';
  static const _languageKey = 'selected_language';
  static const _usernameKey = 'username';
  static const _expertModeKey = 'expert_mode';
  static const _onboardingCompletedKey = 'onboarding_completed';

  static final themeModeNotifier = ValueNotifier<ThemeMode>(ThemeMode.dark);
  static final languageNotifier = ValueNotifier<Locale>(const Locale('pl'));
  static final usernameNotifier = ValueNotifier<String>('Użytkownik');
  static final expertModeNotifier = ValueNotifier<bool>(false);
  static final onboardingCompletedNotifier = ValueNotifier<bool>(false);

  static Future<void> init() async {
    final prefs = await SharedPreferences.getInstance();
    final theme = prefs.getString(_themeModeKey) ?? 'dark';
    final lang = prefs.getString(_languageKey) ?? 'pl';
    final user = prefs.getString(_usernameKey) ?? 'Użytkownik';

    themeModeNotifier.value = _parseThemeMode(theme);
    languageNotifier.value = Locale(lang);
    usernameNotifier.value = user;
    expertModeNotifier.value = prefs.getBool(_expertModeKey) ?? false;
    onboardingCompletedNotifier.value =
        prefs.getBool(_onboardingCompletedKey) ?? false;
  }

  static Future<void> saveSettings(
    ThemeMode themeMode,
    String languageCode,
    String username,
  ) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(_themeModeKey, themeMode.name);
    await prefs.setString(_languageKey, languageCode);
    await prefs.setString(_usernameKey, username);

    themeModeNotifier.value = themeMode;
    languageNotifier.value = Locale(languageCode);
    usernameNotifier.value = username;
  }

  static Future<void> setExpertMode(bool enabled) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setBool(_expertModeKey, enabled);
    expertModeNotifier.value = enabled;
  }

  static Future<void> completeOnboarding() async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setBool(_onboardingCompletedKey, true);
    onboardingCompletedNotifier.value = true;
  }

  static Future<void> resetOnboarding() async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setBool(_onboardingCompletedKey, false);
    onboardingCompletedNotifier.value = false;
  }

  static ThemeMode _parseThemeMode(String name) {
    switch (name) {
      case 'light':
        return ThemeMode.light;
      case 'dark':
        return ThemeMode.dark;
      default:
        return ThemeMode.dark;
    }
  }
}
