import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../../../app_settings.dart';
import '../../../app_update/app_update_ui.dart';
import '../../../controller_preferences.dart';
import '../widgets.dart';

class ProfileSettingsView extends StatefulWidget {
  const ProfileSettingsView({super.key});

  @override
  State<ProfileSettingsView> createState() => _ProfileSettingsViewState();
}

class _ProfileSettingsViewState extends State<ProfileSettingsView> {
  final _formKey = GlobalKey<FormState>();
  final _controllerPreferences = ControllerPreferences();
  late final TextEditingController _usernameController;
  late ThemeMode _selectedThemeMode;
  Uri? _savedController;
  bool _autoReconnect = true;
  bool _connectionPreferencesLoaded = false;
  bool _forgettingController = false;
  bool _saving = false;

  @override
  void initState() {
    super.initState();
    _usernameController = TextEditingController(
      text: AppSettings.usernameNotifier.value,
    );
    _selectedThemeMode = AppSettings.themeModeNotifier.value;
    unawaited(_loadConnectionPreferences());
  }

  @override
  void dispose() {
    _usernameController.dispose();
    super.dispose();
  }

  Future<void> _loadConnectionPreferences() async {
    try {
      final address = await _controllerPreferences.loadSavedAddress();
      final autoReconnect = await _controllerPreferences.loadAutoReconnect();
      if (!mounted) return;
      setState(() {
        _savedController = address;
        _autoReconnect = autoReconnect;
        _connectionPreferencesLoaded = true;
      });
    } on Object {
      if (!mounted) return;
      setState(() => _connectionPreferencesLoaded = true);
    }
  }

  Future<void> _save() async {
    if (_saving || _formKey.currentState?.validate() != true) return;
    setState(() => _saving = true);
    try {
      await AppSettings.saveSettings(
        _selectedThemeMode,
        'pl',
        _usernameController.text.trim(),
      );
      await _controllerPreferences.saveAutoReconnect(_autoReconnect);
      if (mounted) {
        try {
          await HapticFeedback.lightImpact();
        } on MissingPluginException {
          // Haptyka jest dodatkiem; jej brak nie może zmienić wyniku zapisu.
        } on PlatformException {
          // Niektóre urządzenia nie udostępniają wybranego efektu haptycznego.
        }
        if (!mounted) return;
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(
            content: Text('Ustawienia profilu zostały zapisane!'),
            behavior: SnackBarBehavior.floating,
            showCloseIcon: true,
          ),
        );
      }
    } on Object {
      if (mounted) {
        final colors = Theme.of(context).colorScheme;
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text(
              'Nie udało się zapisać ustawień profilu.',
              style: TextStyle(color: colors.onError),
            ),
            backgroundColor: colors.error,
            behavior: SnackBarBehavior.floating,
            showCloseIcon: true,
            closeIconColor: colors.onError,
          ),
        );
      }
    } finally {
      if (mounted) setState(() => _saving = false);
    }
  }

  Future<void> _forgetController() async {
    if (_forgettingController || _savedController == null) return;
    final accepted = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('Zapomnieć sterownik?'),
        content: Text(
          'Adres ${_savedController!.host} zostanie usunięty. '
          'Przy następnym uruchomieniu aplikacja pokaże wybór połączenia.',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context, false),
            child: const Text('Anuluj'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(context, true),
            child: const Text('Zapomnij'),
          ),
        ],
      ),
    );
    if (accepted != true || !mounted) return;

    setState(() => _forgettingController = true);
    try {
      await _controllerPreferences.forgetController();
      await _controllerPreferences.saveAutoReconnect(false);
      if (!mounted) return;
      setState(() {
        _savedController = null;
        _autoReconnect = false;
      });
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('Zapisany sterownik został usunięty.'),
          behavior: SnackBarBehavior.floating,
        ),
      );
    } on Object {
      if (!mounted) return;
      final colors = Theme.of(context).colorScheme;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text(
            'Nie udało się usunąć zapisanego sterownika.',
            style: TextStyle(color: colors.onError),
          ),
          backgroundColor: colors.error,
          behavior: SnackBarBehavior.floating,
        ),
      );
    } finally {
      if (mounted) setState(() => _forgettingController = false);
    }
  }

  Future<void> _disableExpertMode() async {
    try {
      await AppSettings.setExpertMode(false);
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text(
            'Włączono tryb prosty. Funkcje serwisowe zostały ukryte.',
          ),
          behavior: SnackBarBehavior.floating,
        ),
      );
    } on Object {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('Nie udało się zmienić poziomu interfejsu.'),
          behavior: SnackBarBehavior.floating,
        ),
      );
    }
  }

  Future<void> _resetOnboarding() async {
    try {
      await AppSettings.resetOnboarding();
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text(
            'Przewodnik uruchomi się ponownie przy następnym starcie aplikacji.',
          ),
          behavior: SnackBarBehavior.floating,
        ),
      );
    } on Object {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('Nie udało się zresetować przewodnika.'),
          behavior: SnackBarBehavior.floating,
        ),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    final updateController = AppUpdateScope.maybeOf(context, listen: false);
    return ControllerPageBody(
      children: [
        Form(
          key: _formKey,
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              const SectionHeader(title: 'Ustawienia Profilu'),
              Card(
                child: Padding(
                  padding: const EdgeInsets.all(16.0),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        'Nazwa użytkownika',
                        style: Theme.of(context).textTheme.titleSmall?.copyWith(
                          fontWeight: FontWeight.bold,
                        ),
                      ),
                      const SizedBox(height: 8),
                      TextFormField(
                        controller: _usernameController,
                        decoration: const InputDecoration(
                          hintText: 'Wpisz swoją nazwę',
                          prefixIcon: Icon(Icons.person_outline_rounded),
                        ),
                        validator: (value) {
                          if (value == null || value.trim().isEmpty) {
                            return 'Nazwa użytkownika nie może być pusta';
                          }
                          return null;
                        },
                      ),
                    ],
                  ),
                ),
              ),
              const SizedBox(height: 16),
              const SectionHeader(title: 'Połączenie przy starcie'),
              Card(
                child: Padding(
                  padding: const EdgeInsets.all(16),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: [
                      SwitchListTile(
                        contentPadding: EdgeInsets.zero,
                        secondary: const Icon(Icons.autorenew_rounded),
                        title: const Text('Łącz automatycznie przez Wi‑Fi'),
                        subtitle: Text(
                          _savedController == null
                              ? 'Brak zapisanego sterownika Wi‑Fi'
                              : 'Sterownik: ${_savedController!.host}',
                        ),
                        value: _autoReconnect,
                        onChanged:
                            !_connectionPreferencesLoaded ||
                                _savedController == null
                            ? null
                            : (value) => setState(() => _autoReconnect = value),
                      ),
                      const SizedBox(height: 8),
                      OutlinedButton.icon(
                        onPressed:
                            _savedController == null || _forgettingController
                            ? null
                            : _forgetController,
                        icon: _forgettingController
                            ? const SizedBox.square(
                                dimension: 18,
                                child: CircularProgressIndicator(
                                  strokeWidth: 2,
                                ),
                              )
                            : const Icon(Icons.link_off_rounded),
                        label: const Text('Zapomnij zapisany sterownik'),
                      ),
                    ],
                  ),
                ),
              ),
              const SizedBox(height: 16),
              const SectionHeader(title: 'Poziom interfejsu'),
              ValueListenableBuilder<bool>(
                valueListenable: AppSettings.expertModeNotifier,
                builder: (context, expertMode, _) => Card(
                  child: Padding(
                    padding: const EdgeInsets.all(16),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.stretch,
                      children: [
                        ListTile(
                          contentPadding: EdgeInsets.zero,
                          leading: Icon(
                            expertMode
                                ? Icons.admin_panel_settings_rounded
                                : Icons.shield_outlined,
                          ),
                          title: Text(
                            expertMode ? 'Tryb ekspercki' : 'Tryb prosty',
                            style: const TextStyle(fontWeight: FontWeight.w800),
                          ),
                          subtitle: Text(
                            expertMode
                                ? 'Widoczne są diagnostyka, OTA i operacje '
                                      'serwisowe sterownika.'
                                : 'Codzienny pulpit pozostaje czytelny, a '
                                      'ryzykowne operacje są ukryte.',
                          ),
                        ),
                        const SizedBox(height: 8),
                        if (expertMode)
                          OutlinedButton.icon(
                            key: const Key('disable-expert-mode-button'),
                            onPressed: _disableExpertMode,
                            icon: const Icon(Icons.visibility_off_outlined),
                            label: const Text('Włącz tryb prosty'),
                          )
                        else
                          const Text(
                            'Tryb ekspercki odblokujesz PIN-em w sekcji '
                            '„Więcej → Narzędzia serwisowe”.',
                            textAlign: TextAlign.center,
                          ),
                      ],
                    ),
                  ),
                ),
              ),
              const SizedBox(height: 16),
              const SectionHeader(title: 'Wygląd i język'),
              Card(
                child: Padding(
                  padding: const EdgeInsets.all(16.0),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        'Motyw aplikacji',
                        style: Theme.of(context).textTheme.titleSmall?.copyWith(
                          fontWeight: FontWeight.bold,
                        ),
                      ),
                      const SizedBox(height: 12),
                      LayoutBuilder(
                        builder: (context, constraints) {
                          final textScale = MediaQuery.textScalerOf(
                            context,
                          ).scale(1);
                          if (constraints.maxWidth < 390 || textScale > 1.3) {
                            return DropdownButtonFormField<ThemeMode>(
                              initialValue: _selectedThemeMode,
                              isExpanded: true,
                              decoration: const InputDecoration(
                                labelText: 'Motyw',
                                prefixIcon: Icon(Icons.contrast_rounded),
                              ),
                              items: const [
                                DropdownMenuItem(
                                  value: ThemeMode.system,
                                  child: Text('Zgodny z systemem'),
                                ),
                                DropdownMenuItem(
                                  value: ThemeMode.light,
                                  child: Text('Jasny'),
                                ),
                                DropdownMenuItem(
                                  value: ThemeMode.dark,
                                  child: Text('Ciemny'),
                                ),
                              ],
                              onChanged: (value) {
                                if (value != null) {
                                  setState(() => _selectedThemeMode = value);
                                }
                              },
                            );
                          }
                          return SegmentedButton<ThemeMode>(
                            segments: const [
                              ButtonSegment<ThemeMode>(
                                value: ThemeMode.system,
                                icon: Icon(Icons.phone_android_rounded),
                                label: Text('System'),
                              ),
                              ButtonSegment<ThemeMode>(
                                value: ThemeMode.light,
                                icon: Icon(Icons.wb_sunny_rounded),
                                label: Text('Jasny'),
                              ),
                              ButtonSegment<ThemeMode>(
                                value: ThemeMode.dark,
                                icon: Icon(Icons.nightlight_round),
                                label: Text('Ciemny'),
                              ),
                            ],
                            selected: {_selectedThemeMode},
                            onSelectionChanged: (Set<ThemeMode> newSelection) {
                              setState(
                                () => _selectedThemeMode = newSelection.first,
                              );
                            },
                          );
                        },
                      ),
                      const SizedBox(height: 20),
                      Text(
                        'Język aplikacji',
                        style: Theme.of(context).textTheme.titleSmall?.copyWith(
                          fontWeight: FontWeight.bold,
                        ),
                      ),
                      const SizedBox(height: 8),
                      Semantics(
                        label: 'Język aplikacji: polski',
                        readOnly: true,
                        child: const ExcludeSemantics(
                          child: ListTile(
                            contentPadding: EdgeInsets.zero,
                            leading: Icon(Icons.language_rounded),
                            title: Text('Polski'),
                            subtitle: Text(
                              'Interfejs i systemowe okna aplikacji',
                            ),
                          ),
                        ),
                      ),
                    ],
                  ),
                ),
              ),
              if (updateController != null) ...[
                const SizedBox(height: 16),
                const SectionHeader(title: 'Aplikacja'),
                AppUpdateSettingsCard(controller: updateController),
              ],
              const SizedBox(height: 16),
              const SectionHeader(title: 'Pomoc'),
              Card(
                child: Padding(
                  padding: const EdgeInsets.all(16),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: [
                      const ListTile(
                        contentPadding: EdgeInsets.zero,
                        leading: Icon(Icons.explore_outlined),
                        title: Text(
                          'Przewodnik startowy',
                          style: TextStyle(fontWeight: FontWeight.w800),
                        ),
                        subtitle: Text(
                          'Ponownie wyjaśni pracę offline, alarmy i sposoby '
                          'połączenia ze sterownikiem.',
                        ),
                      ),
                      const SizedBox(height: 8),
                      OutlinedButton.icon(
                        key: const Key('reset-onboarding-button'),
                        onPressed: _resetOnboarding,
                        icon: const Icon(Icons.restart_alt_rounded),
                        label: const Text(
                          'Pokaż przewodnik przy następnym starcie',
                        ),
                      ),
                    ],
                  ),
                ),
              ),
              const SizedBox(height: 24),
              FilledButton.icon(
                onPressed: _saving ? null : _save,
                icon: _saving
                    ? const SizedBox.square(
                        dimension: 18,
                        child: CircularProgressIndicator(
                          strokeWidth: 2,
                          semanticsLabel: 'Zapisywanie ustawień profilu',
                        ),
                      )
                    : const Icon(Icons.save_rounded),
                label: Text(_saving ? 'Zapisywanie…' : 'Zapisz ustawienia'),
                style: FilledButton.styleFrom(
                  padding: const EdgeInsets.symmetric(vertical: 16),
                ),
              ),
            ],
          ),
        ),
      ],
    );
  }
}
