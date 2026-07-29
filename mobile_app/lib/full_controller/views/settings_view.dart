import 'package:flutter/material.dart';

import '../controller_api.dart';
import '../controller_session.dart';
import '../controller_shell.dart';
import '../data_access.dart';
import '../widgets.dart';

class SettingsView extends StatefulWidget {
  const SettingsView({
    super.key,
    required this.session,
    required this.runAction,
    required this.ensureAdmin,
  });

  final ControllerSession session;
  final RunControllerAction runAction;
  final Future<bool> Function() ensureAdmin;

  @override
  State<SettingsView> createState() => _SettingsViewState();
}

class _SettingsViewState extends State<SettingsView> {
  final networkForm = GlobalKey<FormState>();
  late final TextEditingController ssid;
  late final TextEditingController password;
  bool? autoBrightness;
  int? brightness;
  String? displayProfile;
  bool _networkConfigurationLoaded = false;
  final Set<String> busy = {};
  String? message;

  @override
  void initState() {
    super.initState();
    final status = widget.session.status;
    final network = status.section('network');
    final display = status.section('display');
    ssid = TextEditingController(text: network.text('configuredStaSsid'));
    password = TextEditingController();
    _networkConfigurationLoaded = network.containsKey('configuredStaSsid');
    _readDisplayConfiguration(display);
  }

  @override
  void didUpdateWidget(SettingsView oldWidget) {
    super.didUpdateWidget(oldWidget);
    final status = widget.session.status;
    final network = status.section('network');
    if (!_networkConfigurationLoaded &&
        network.containsKey('configuredStaSsid')) {
      ssid.text = network.text('configuredStaSsid');
      _networkConfigurationLoaded = true;
    }
    if (!_hasDisplayConfiguration) {
      _readDisplayConfiguration(status.section('display'));
    }
  }

  void _readDisplayConfiguration(JsonMap display) {
    if (display.containsKey('autoBrightness')) {
      autoBrightness = display.flag('autoBrightness');
    }
    final configuredBrightness = display.nullableNumber('brightness');
    if (configuredBrightness != null) {
      brightness = configuredBrightness.round().clamp(10, 100);
    }
    final profile = display['profile']?.toString();
    if (const {'always_on', 'timeout_60s', 'always_off'}.contains(profile)) {
      displayProfile = profile;
    }
  }

  bool get _hasDisplayConfiguration =>
      autoBrightness != null && brightness != null && displayProfile != null;

  @override
  void dispose() {
    ssid.dispose();
    password.dispose();
    super.dispose();
  }

  Future<void> _execute(String key, Future<void> Function() operation) async {
    setState(() {
      busy.add(key);
      message = null;
    });
    try {
      await operation();
    } on ControllerApiException catch (error) {
      if (mounted) setState(() => message = error.message);
    } finally {
      if (mounted) setState(() => busy.remove(key));
    }
  }

  Future<void> _saveNetwork() async {
    if (networkForm.currentState?.validate() != true) return;
    await _execute('network', () async {
      final payload = <String, Object?>{
        'staSsid': ssid.text.trim(),
        'staPassword': password.text,
      };
      final result = await widget.runAction('save_network', payload: payload);
      password.clear();
      if (mounted) setState(() => message = result.message);
    });
  }

  Future<void> _saveDisplay() async {
    if (!_hasDisplayConfiguration) return;
    await _execute('display', () async {
      final result = await widget.runAction(
        'save_display',
        payload: {
          'autoBrightness': autoBrightness!,
          'profile': displayProfile!,
          'brightness': brightness!,
        },
      );
      if (mounted) setState(() => message = result.message);
    });
  }

  Future<void> _setBrowserTime() async {
    await _execute('clock', () async {
      if (!await widget.ensureAdmin()) return;
      await widget.session.setBrowserTime();
      if (mounted) {
        setState(
          () => message = 'Czas sterownika ustawiono zgodnie z telefonem.',
        );
      }
    });
  }

  @override
  Widget build(BuildContext context) {
    final status = widget.session.status;
    final network = status.section('network');
    final clock = status.section('clock');
    final display = status.section('display');
    final firmware = status.section('firmware');
    final canEdit = widget.session.canIssueCommands;
    final hasDisplayConfiguration = _hasDisplayConfiguration;
    return ControllerPageBody(
      children: [
        const SectionHeader(
          title: 'Ustawienia urządzenia',
          description: 'Sieć, ekran CYD, zegar oraz operacje administracyjne.',
        ),
        if (!canEdit) ...[
          StatusBanner(
            icon: Icons.visibility_rounded,
            title: widget.session.hasCachedSnapshot
                ? 'Konfiguracja tylko do podglądu'
                : 'Brak zapisanej konfiguracji',
            message: widget.session.hasCachedSnapshot
                ? widget.session.commandBlockReason ??
                      'Połącz sterownik, aby zmieniać konfigurację urządzenia.'
                : 'Aplikacja pokazuje komplet dostępnych ustawień, ale nie '
                      'wypełnia ich fikcyjnymi danymi urządzenia.',
            isError: false,
          ),
          const SizedBox(height: 10),
        ],
        Card(
          child: ExpansionTile(
            initiallyExpanded: true,
            leading: const Icon(Icons.wifi_rounded),
            title: const Text(
              'Sieć Wi-Fi',
              style: TextStyle(fontWeight: FontWeight.w800),
            ),
            subtitle: Text(
              network.flag('staConnected')
                  ? '${network.text('ssid')} · ${network.text('ip')}'
                  : 'Sterownik nie jest połączony z STA',
            ),
            childrenPadding: const EdgeInsets.fromLTRB(16, 0, 16, 16),
            children: [
              Form(
                key: networkForm,
                child: Column(
                  children: [
                    InfoRow(
                      label: 'Aktywna sieć',
                      value: network.text('staSsid', '—'),
                    ),
                    InfoRow(label: 'Adres IP', value: network.text('ip', '—')),
                    InfoRow(
                      label: 'Access Point',
                      value: network.text('configuredApSsid', '—'),
                    ),
                    const SizedBox(height: 10),
                    TextFormField(
                      controller: ssid,
                      enabled: canEdit,
                      maxLength: 32,
                      decoration: const InputDecoration(
                        labelText: 'SSID profilu STA',
                        border: OutlineInputBorder(),
                      ),
                      validator: (value) {
                        final text = value?.trim() ?? '';
                        if (text.isEmpty) return 'SSID jest wymagane.';
                        if (text.length > 32) {
                          return 'SSID może mieć maksymalnie 32 znaki.';
                        }
                        return null;
                      },
                    ),
                    TextFormField(
                      controller: password,
                      enabled: canEdit,
                      obscureText: true,
                      maxLength: 63,
                      decoration: const InputDecoration(
                        labelText: 'Hasło Wi‑Fi',
                        helperText:
                            'Firmware wymaga hasła przy każdym zapisie profilu.',
                        border: OutlineInputBorder(),
                      ),
                      validator: (value) {
                        final length = value?.length ?? 0;
                        if (length < 8) {
                          return 'Hasło WPA musi mieć co najmniej 8 znaków.';
                        }
                        return null;
                      },
                    ),
                    const SizedBox(height: 10),
                    SaveButton(
                      onPressed: canEdit ? _saveNetwork : null,
                      label: 'Zapisz profil STA',
                      busy: busy.contains('network'),
                    ),
                    const SizedBox(height: 10),
                    Row(
                      children: [
                        Expanded(
                          child: OutlinedButton.icon(
                            onPressed: !canEdit || busy.contains('wifi')
                                ? null
                                : () => _execute('wifi', () async {
                                    await widget.runAction(
                                      'wifi_session_start',
                                    );
                                  }),
                            icon: const Icon(Icons.wifi_rounded),
                            label: const Text('Włącz Wi-Fi'),
                          ),
                        ),
                        const SizedBox(width: 10),
                        Expanded(
                          child: OutlinedButton.icon(
                            onPressed: !canEdit || busy.contains('wifi')
                                ? null
                                : () => _execute('wifi', () async {
                                    await widget.runAction(
                                      'wifi_session_stop',
                                      confirmation:
                                          'Wyłączenie sesji Wi-Fi przerwie bieżące połączenie aplikacji. Kontynuować?',
                                    );
                                  }),
                            icon: const Icon(Icons.wifi_off_rounded),
                            label: const Text('Wyłącz Wi-Fi'),
                          ),
                        ),
                      ],
                    ),
                  ],
                ),
              ),
            ],
          ),
        ),
        const SizedBox(height: 10),
        Card(
          child: ExpansionTile(
            leading: const Icon(Icons.brightness_6_rounded),
            title: const Text(
              'Wyświetlacz CYD',
              style: TextStyle(fontWeight: FontWeight.w800),
            ),
            subtitle: Text(
              hasDisplayConfiguration
                  ? '${display.integer('appliedBrightness', brightness!)}% · ${_profileLabel(displayProfile!)}'
                  : 'Brak zapisanego stanu',
            ),
            childrenPadding: const EdgeInsets.fromLTRB(16, 0, 16, 16),
            children: [
              if (!hasDisplayConfiguration)
                const StatusBanner(
                  icon: Icons.cloud_download_outlined,
                  title: 'Brak zapisanej konfiguracji ekranu',
                  message:
                      'Profil zasilania, automatyczna jasność i limit podświetlenia pojawią się po pierwszej synchronizacji.',
                  isError: false,
                )
              else ...[
                DropdownButtonFormField<String>(
                  initialValue: displayProfile!,
                  decoration: const InputDecoration(
                    labelText: 'Profil zasilania ekranu',
                    border: OutlineInputBorder(),
                  ),
                  items: const [
                    DropdownMenuItem(
                      value: 'always_on',
                      child: Text('Zawsze włączony'),
                    ),
                    DropdownMenuItem(
                      value: 'timeout_60s',
                      child: Text('Wygaszanie po 60 sekundach'),
                    ),
                    DropdownMenuItem(
                      value: 'always_off',
                      child: Text('Zawsze wyłączony'),
                    ),
                  ],
                  onChanged: canEdit
                      ? (value) {
                          if (value != null) {
                            setState(() => displayProfile = value);
                          }
                        }
                      : null,
                ),
                LabeledSwitch(
                  label: 'Automatyczna jasność',
                  subtitle:
                      'LDR skaluje podświetlenie od 15% do ustawionego maksimum.',
                  value: autoBrightness!,
                  onChanged: canEdit
                      ? (value) => setState(() => autoBrightness = value)
                      : null,
                ),
                Row(
                  children: [
                    const Expanded(child: Text('Maksymalna jasność')),
                    Text(
                      '${brightness!}%',
                      style: const TextStyle(fontWeight: FontWeight.w800),
                    ),
                  ],
                ),
                Slider(
                  value: brightness!.toDouble(),
                  min: 10,
                  max: 100,
                  divisions: 18,
                  label: '${brightness!}%',
                  onChanged: canEdit
                      ? (value) =>
                            setState(() => brightness = (value / 5).round() * 5)
                      : null,
                ),
                SaveButton(
                  onPressed: canEdit ? _saveDisplay : null,
                  label: 'Zapisz ustawienia ekranu',
                  busy: busy.contains('display'),
                ),
              ],
            ],
          ),
        ),
        const SizedBox(height: 10),
        Card(
          child: ExpansionTile(
            leading: const Icon(Icons.schedule_rounded),
            title: const Text(
              'Zegar sterownika',
              style: TextStyle(fontWeight: FontWeight.w800),
            ),
            subtitle: Text(_clockText(clock)),
            childrenPadding: const EdgeInsets.fromLTRB(16, 0, 16, 16),
            children: [
              InfoRow(label: 'Źródło czasu', value: clock.text('source', '—')),
              InfoRow(
                label: 'Stan RTC',
                value: clock.flag('valid')
                    ? 'wiarygodny'
                    : 'niesynchronizowany',
              ),
              Row(
                children: [
                  Expanded(
                    child: OutlinedButton.icon(
                      onPressed: !canEdit || busy.contains('clock')
                          ? null
                          : _setBrowserTime,
                      icon: const Icon(Icons.phone_android_rounded),
                      label: const Text('Czas telefonu'),
                    ),
                  ),
                  const SizedBox(width: 10),
                  Expanded(
                    child: OutlinedButton.icon(
                      onPressed: !canEdit || busy.contains('clock')
                          ? null
                          : () => _execute('clock', () async {
                              await widget.runAction('sync_time_ntp');
                            }),
                      icon: const Icon(Icons.public_rounded),
                      label: const Text('Synchronizuj NTP'),
                    ),
                  ),
                ],
              ),
            ],
          ),
        ),
        const SizedBox(height: 10),
        Card(
          child: ExpansionTile(
            leading: Icon(
              Icons.admin_panel_settings_rounded,
              color: Theme.of(context).colorScheme.error,
            ),
            title: const Text(
              'Administracja urządzeniem',
              style: TextStyle(fontWeight: FontWeight.w800),
            ),
            subtitle: Text(
              'Firmware ${firmware.text('version', 'nieznane')} · operacje nieodwracalne',
            ),
            childrenPadding: const EdgeInsets.fromLTRB(16, 0, 16, 16),
            children: [
              SizedBox(
                width: double.infinity,
                child: OutlinedButton.icon(
                  onPressed: !canEdit || busy.contains('device')
                      ? null
                      : () => _execute('device', () async {
                          await widget.runAction(
                            'restart_device',
                            confirmation:
                                'Uruchomić ponownie ESP32? Połączenie zostanie chwilowo przerwane.',
                            refreshAfter: false,
                          );
                        }),
                  icon: const Icon(Icons.restart_alt_rounded),
                  label: const Text('Restart ESP32'),
                ),
              ),
              const SizedBox(height: 10),
              SizedBox(
                width: double.infinity,
                child: FilledButton.tonalIcon(
                  onPressed: !canEdit || busy.contains('device')
                      ? null
                      : () => _execute('device', () async {
                          await widget.runAction(
                            'factory_reset',
                            confirmation:
                                'Reset fabryczny usunie konfigurację harmonogramów, automatyki i sieci. Operacji nie można cofnąć.',
                            refreshAfter: false,
                          );
                        }),
                  icon: const Icon(Icons.warning_amber_rounded),
                  label: const Text('Przywróć ustawienia fabryczne'),
                ),
              ),
            ],
          ),
        ),
        if (message != null)
          Padding(
            padding: const EdgeInsets.only(top: 12),
            child: Text(message!),
          ),
      ],
    );
  }

  String _profileLabel(String value) => switch (value) {
    'always_off' => 'wyłączony',
    'timeout_60s' => 'wygaszanie 60 s',
    _ => 'zawsze włączony',
  };

  String _clockText(JsonMap clock) {
    if (!clock.flag('valid')) return 'Oczekiwanie na synchronizację';
    return '${clock.integer('year')}-${clock.integer('month').toString().padLeft(2, '0')}-'
        '${clock.integer('day').toString().padLeft(2, '0')} '
        '${clock.integer('hour').toString().padLeft(2, '0')}:'
        '${clock.integer('minute').toString().padLeft(2, '0')}:'
        '${clock.integer('second').toString().padLeft(2, '0')}';
  }
}
