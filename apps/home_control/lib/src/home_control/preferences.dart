import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:home_entities/home_entities.dart';
import 'package:shared_preferences/shared_preferences.dart';

final class DashboardPreferences {
  const DashboardPreferences({
    required this.order,
    required this.hidden,
    required this.largeCards,
    required this.favorites,
  });

  const DashboardPreferences.defaults()
    : order = const <String>['aquarium', 'favorites', 'areas', 'activity'],
      hidden = const <String>{},
      largeCards = const <String>{'aquarium'},
      favorites = const <String>{};

  final List<String> order;
  final Set<String> hidden;
  final Set<String> largeCards;
  final Set<String> favorites;

  DashboardPreferences copyWith({
    List<String>? order,
    Set<String>? hidden,
    Set<String>? largeCards,
    Set<String>? favorites,
  }) => DashboardPreferences(
    order: order ?? this.order,
    hidden: hidden ?? this.hidden,
    largeCards: largeCards ?? this.largeCards,
    favorites: favorites ?? this.favorites,
  );
}

final class HomeControlPreferences {
  HomeControlPreferences({
    SharedPreferencesAsync? storage,
    Locale? fallbackLocale,
  }) : _storage = storage ?? SharedPreferencesAsync(),
       _fallbackLocale =
           fallbackLocale ?? ui.PlatformDispatcher.instance.locale;

  static const _schemaVersion = 2;
  static const _schemaKey = 'home_control_schema_version';
  static const _sourceKindKey = 'home_control_active_source_kind';
  static const _themeKey = 'home_control_theme';
  static const _localeKey = 'home_control_locale';
  static const _dashboardOrderKey = 'home_control_dashboard_order';
  static const _dashboardHiddenKey = 'home_control_dashboard_hidden';
  static const _dashboardLargeKey = 'home_control_dashboard_large';
  static const _favoritesKey = 'home_control_favorites';
  static const _biometricProtectionKey =
      'home_control_biometric_critical_actions';

  final SharedPreferencesAsync _storage;
  final Locale _fallbackLocale;

  Future<void> migrate() async {
    var current = await _storage.getInt(_schemaKey) ?? 0;
    if (current > _schemaVersion) {
      throw const FormatException('Unsupported Home Control settings schema.');
    }
    if (current < 1) {
      current = 1;
    }
    if (current < 2) {
      if (await _storage.getBool(_biometricProtectionKey) == null) {
        await _storage.setBool(_biometricProtectionKey, false);
      }
      current = 2;
    }
    await _storage.setInt(_schemaKey, current);
  }

  Future<HomeSourceKind?> loadActiveSource() async {
    final value = await _storage.getString(_sourceKindKey);
    for (final kind in HomeSourceKind.values) {
      if (kind.name == value) return kind;
    }
    return null;
  }

  Future<void> saveActiveSource(HomeSourceKind kind) =>
      _storage.setString(_sourceKindKey, kind.name);

  Future<void> clearActiveSource() => _storage.remove(_sourceKindKey);

  Future<ThemeMode> loadThemeMode() async =>
      switch (await _storage.getString(_themeKey)) {
        'light' => ThemeMode.light,
        'dark' => ThemeMode.dark,
        _ => ThemeMode.system,
      };

  Future<void> saveThemeMode(ThemeMode mode) =>
      _storage.setString(_themeKey, mode.name);

  Future<Locale> loadLocale() async {
    final language = await _storage.getString(_localeKey);
    if (language == 'pl' || language == 'en') return Locale(language!);
    return Locale(_fallbackLocale.languageCode == 'en' ? 'en' : 'pl');
  }

  Future<void> saveLocale(Locale locale) =>
      _storage.setString(_localeKey, locale.languageCode);

  Future<bool> loadBiometricProtection() async =>
      await _storage.getBool(_biometricProtectionKey) ?? false;

  Future<void> saveBiometricProtection(bool enabled) =>
      _storage.setBool(_biometricProtectionKey, enabled);

  Future<DashboardPreferences> loadDashboard() async {
    const defaults = DashboardPreferences.defaults();
    final order = await _storage.getStringList(_dashboardOrderKey);
    final seen = <String>{};
    final storedOrder = order ?? defaults.order;
    final validOrder = <String>[
      for (final id in storedOrder)
        if (defaults.order.contains(id) && seen.add(id)) id,
      for (final id in defaults.order)
        if (seen.add(id)) id,
    ];
    return DashboardPreferences(
      order: validOrder,
      hidden: Set<String>.from(
        await _storage.getStringList(_dashboardHiddenKey) ?? const <String>[],
      )..removeWhere((id) => !defaults.order.contains(id)),
      largeCards: Set<String>.from(
        await _storage.getStringList(_dashboardLargeKey) ?? defaults.largeCards,
      )..removeWhere((id) => !defaults.order.contains(id)),
      favorites: Set<String>.from(
        await _storage.getStringList(_favoritesKey) ?? const <String>[],
      ),
    );
  }

  Future<void> saveDashboard(DashboardPreferences value) async {
    await Future.wait<void>(<Future<void>>[
      _storage.setStringList(_dashboardOrderKey, value.order),
      _storage.setStringList(
        _dashboardHiddenKey,
        value.hidden.toList(growable: false)..sort(),
      ),
      _storage.setStringList(
        _dashboardLargeKey,
        value.largeCards.toList(growable: false)..sort(),
      ),
      _storage.setStringList(
        _favoritesKey,
        value.favorites.toList(growable: false)..sort(),
      ),
    ]);
  }

  Future<void> resetDashboard() async {
    await Future.wait<void>(<Future<void>>[
      _storage.remove(_dashboardOrderKey),
      _storage.remove(_dashboardHiddenKey),
      _storage.remove(_dashboardLargeKey),
      _storage.remove(_favoritesKey),
    ]);
  }
}
