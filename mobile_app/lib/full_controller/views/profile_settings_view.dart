import 'package:flutter/material.dart';
import '../../../app_settings.dart';
import '../../../app_update/app_update_ui.dart';
import '../widgets.dart';

class ProfileSettingsView extends StatefulWidget {
  const ProfileSettingsView({super.key});

  @override
  State<ProfileSettingsView> createState() => _ProfileSettingsViewState();
}

class _ProfileSettingsViewState extends State<ProfileSettingsView> {
  final _formKey = GlobalKey<FormState>();
  late final TextEditingController _usernameController;
  late ThemeMode _selectedThemeMode;
  late String _selectedLanguage;

  @override
  void initState() {
    super.initState();
    _usernameController = TextEditingController(
      text: AppSettings.usernameNotifier.value,
    );
    _selectedThemeMode = AppSettings.themeModeNotifier.value;
    _selectedLanguage = AppSettings.languageNotifier.value.languageCode;
  }

  @override
  void dispose() {
    _usernameController.dispose();
    super.dispose();
  }

  Future<void> _save() async {
    if (_formKey.currentState!.validate()) {
      await AppSettings.saveSettings(
        _selectedThemeMode,
        _selectedLanguage,
        _usernameController.text.trim(),
      );
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(
            content: Text('Ustawienia profilu zostały zapisane!'),
            behavior: SnackBarBehavior.floating,
          ),
        );
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    final updateController = AppUpdateScope.maybeOf(context);
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
                      SegmentedButton<ThemeMode>(
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
                          setState(() {
                            _selectedThemeMode = newSelection.first;
                          });
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
                      DropdownButtonFormField<String>(
                        initialValue: _selectedLanguage,
                        decoration: const InputDecoration(
                          prefixIcon: Icon(Icons.language_rounded),
                        ),
                        items: const [
                          DropdownMenuItem(value: 'pl', child: Text('Polski')),
                          DropdownMenuItem(value: 'en', child: Text('English')),
                        ],
                        onChanged: (String? newValue) {
                          if (newValue != null) {
                            setState(() {
                              _selectedLanguage = newValue;
                            });
                          }
                        },
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
              const SizedBox(height: 24),
              FilledButton.icon(
                onPressed: _save,
                icon: const Icon(Icons.save_rounded),
                label: const Text('Zapisz ustawienia'),
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
