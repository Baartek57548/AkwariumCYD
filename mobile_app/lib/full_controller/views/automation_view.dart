import 'dart:convert';

import 'package:flutter/material.dart';

import '../controller_api.dart';
import '../controller_session.dart';
import '../controller_shell.dart';
import '../data_access.dart';
import '../widgets.dart';

class AutomationView extends StatefulWidget {
  const AutomationView({
    super.key,
    required this.session,
    required this.runAction,
  });

  final ControllerSession session;
  final RunControllerAction runAction;

  @override
  State<AutomationView> createState() => _AutomationViewState();
}

class _AutomationViewState extends State<AutomationView> {
  final temperatureForm = GlobalKey<FormState>();
  final co2Form = GlobalKey<FormState>();
  final waterForm = GlobalKey<FormState>();
  late final TextEditingController targetTemperature;
  late final TextEditingController hysteresis;
  late final TextEditingController targetPh;
  late final TextEditingController co2Limit;
  late final TextEditingController waterTimeout;
  late bool heaterEnabled;
  late bool co2Enabled;
  late bool waterEnabled;
  late bool leakEnabled;
  late String leakAction;
  final Set<String> saving = {};
  final Set<String> _dirtySections = {};
  final Set<String> _remoteChangedSections = {};
  final Map<String, String> _sourceFingerprints = {};
  bool _syncingFromStatus = false;
  String? message;

  @override
  void initState() {
    super.initState();
    final status = widget.session.status;
    final config = status.section('config');
    final modules = status.section('modules');
    targetTemperature = TextEditingController(
      text: config.number('target_temp', 25).toStringAsFixed(1),
    );
    hysteresis = TextEditingController(
      text: config.number('temp_hysteresis', 0.5).toStringAsFixed(1),
    );
    targetPh = TextEditingController(
      text: config.number('co2TargetPh', 6.8).toStringAsFixed(2),
    );
    co2Limit = TextEditingController(
      text: '${config.integer('co2MaxTimeMin', 180)}',
    );
    waterTimeout = TextEditingController(
      text: '${status.section('water').integer('timeoutSec', 120)}',
    );
    heaterEnabled = modules.flag('heater_enabled');
    co2Enabled = modules.flag('co2_enabled');
    waterEnabled = modules.flag('water_level_enabled');
    leakEnabled = modules.flag('leak_enabled');
    leakAction = status.section('leak').text('action', 'disable_all');
    _sourceFingerprints.addAll(_fingerprints(status));
    _watch('temperature', targetTemperature);
    _watch('temperature', hysteresis);
    _watch('co2', targetPh);
    _watch('co2', co2Limit);
    _watch('water', waterTimeout);
  }

  @override
  void didUpdateWidget(AutomationView oldWidget) {
    super.didUpdateWidget(oldWidget);
    final status = widget.session.status;
    final current = _fingerprints(status);
    for (final entry in current.entries) {
      if (_sourceFingerprints[entry.key] == entry.value) continue;
      if (_dirtySections.contains(entry.key) || saving.contains(entry.key)) {
        _remoteChangedSections.add(entry.key);
      } else {
        _syncSection(entry.key, status, entry.value);
      }
    }
  }

  @override
  void dispose() {
    targetTemperature.dispose();
    hysteresis.dispose();
    targetPh.dispose();
    co2Limit.dispose();
    waterTimeout.dispose();
    super.dispose();
  }

  void _watch(String section, TextEditingController controller) {
    controller.addListener(() {
      if (_syncingFromStatus || _dirtySections.contains(section) || !mounted) {
        return;
      }
      setState(() {
        _dirtySections.add(section);
        message = null;
      });
    });
  }

  void _edit(String section, VoidCallback change) {
    setState(() {
      change();
      _dirtySections.add(section);
      message = null;
    });
  }

  void _syncSection(String section, JsonMap status, [String? fingerprint]) {
    final config = status.section('config');
    final modules = status.section('modules');
    _syncingFromStatus = true;
    try {
      switch (section) {
        case 'temperature':
          targetTemperature.text = config
              .number('target_temp', 25)
              .toStringAsFixed(1);
          hysteresis.text = config
              .number('temp_hysteresis', 0.5)
              .toStringAsFixed(1);
          heaterEnabled = modules.flag('heater_enabled');
          break;
        case 'co2':
          targetPh.text = config.number('co2TargetPh', 6.8).toStringAsFixed(2);
          co2Limit.text = '${config.integer('co2MaxTimeMin', 180)}';
          co2Enabled = modules.flag('co2_enabled');
          break;
        case 'water':
          waterTimeout.text =
              '${status.section('water').integer('timeoutSec', 120)}';
          waterEnabled = modules.flag('water_level_enabled');
          break;
        case 'leak':
          leakEnabled = modules.flag('leak_enabled');
          leakAction = status.section('leak').text('action', 'disable_all');
          break;
      }
    } finally {
      _syncingFromStatus = false;
    }
    _sourceFingerprints[section] =
        fingerprint ?? _fingerprints(status)[section]!;
    _dirtySections.remove(section);
    _remoteChangedSections.remove(section);
  }

  void _restoreFromController() {
    setState(() {
      final status = widget.session.status;
      for (final section in _sourceFingerprints.keys.toList()) {
        _syncSection(section, status);
      }
      message = 'Przywrócono aktualną konfigurację sterownika.';
    });
  }

  Future<void> _saveTemperature() async {
    if (temperatureForm.currentState?.validate() != true) return;
    await _runSave('temperature', 'save_temperature', {
      'heaterMode': heaterEnabled ? 0 : 1,
      'target': targetTemperature.text.replaceAll(',', '.'),
      'hysteresis': hysteresis.text.replaceAll(',', '.'),
      'targetTemp': targetTemperature.text.replaceAll(',', '.'),
      'tempHyst': hysteresis.text.replaceAll(',', '.'),
    });
  }

  Future<void> _saveCo2() async {
    if (co2Form.currentState?.validate() != true) return;
    await _runSave('co2', 'save_co2', {
      'co2Enabled': co2Enabled,
      'targetPh': targetPh.text.replaceAll(',', '.'),
      'co2Limit': co2Limit.text,
    });
  }

  Future<void> _saveWater() async {
    if (waterForm.currentState?.validate() != true) return;
    await _runSave('water', 'save_water', {
      'waterEnabled': waterEnabled,
      'waterTimeout': waterTimeout.text,
    });
  }

  Future<void> _saveLeak() {
    return _runSave('leak', 'save_leak', {
      'leakEnabled': leakEnabled,
      'leakAction': leakAction,
    });
  }

  Future<void> _runSave(
    String key,
    String action,
    Map<String, Object?> payload,
  ) async {
    setState(() {
      saving.add(key);
      message = 'Zapisywanie konfiguracji…';
    });
    try {
      final result = await widget.runAction(
        action,
        payload: payload,
        confirmation: _remoteChangedSections.contains(key)
            ? 'Sterownik ma nowszą konfigurację tej sekcji. '
                  'Zastąpić ją wartościami z lokalnego szkicu?'
            : null,
      );
      if (mounted) {
        setState(() {
          _syncSection(key, widget.session.status);
          message = result.message;
        });
      }
    } on ControllerApiException catch (error) {
      if (mounted) setState(() => message = error.message);
    } finally {
      if (mounted) setState(() => saving.remove(key));
    }
  }

  @override
  Widget build(BuildContext context) {
    final sensors = widget.session.status.section('sensors');
    final modules = widget.session.status.section('modules');
    final water = widget.session.status.section('water');
    return ControllerPageBody(
      children: [
        SectionHeader(
          title: 'Automatyka',
          description:
              'Termostat, dozowanie CO₂, automatyczna dolewka oraz reakcja na wyciek.',
          trailing: IconButton(
            onPressed: _restoreFromController,
            icon: const Icon(Icons.restore_rounded),
            tooltip: 'Przywróć dane sterownika',
          ),
        ),
        if (_remoteChangedSections.isNotEmpty) ...[
          const StatusBanner(
            icon: Icons.sync_problem_rounded,
            title: 'Sterownik ma nowsze ustawienia',
            message:
                'Lokalny szkic pozostał bez zmian. Przywróć dane albo '
                'potwierdź zastąpienie wybranej sekcji podczas zapisu.',
            isError: false,
          ),
          const SizedBox(height: 12),
        ],
        ResponsiveGrid(
          children: [
            MetricTile(
              icon: Icons.thermostat_rounded,
              label: 'Grzałka',
              value: modules.flag('heater_on') ? 'ON' : 'OFF',
              detail: sensors.flag('temp_valid')
                  ? '${sensors.number('temp_c').toStringAsFixed(2)} °C'
                  : 'Brak odczytu',
            ),
            MetricTile(
              icon: Icons.bubble_chart_rounded,
              label: 'CO₂',
              value: modules.flag('co2_on') ? 'DOZOWANIE' : 'OFF',
              detail: sensors.flag('ph_valid')
                  ? 'pH ${sensors.number('ph').toStringAsFixed(2)}'
                  : 'Brak odczytu pH',
            ),
            MetricTile(
              icon: Icons.water_drop_rounded,
              label: 'Automatyczna dolewka',
              value: water.flag('active') ? 'AKTYWNA' : 'OCZEKUJE',
              detail: water.flag('timeoutLatched')
                  ? 'Blokada czasowa aktywna'
                  : 'Limit ${water.integer('timeoutSec')} s',
            ),
            MetricTile(
              icon: Icons.health_and_safety_rounded,
              label: 'Zabezpieczenie wycieku',
              value: sensors.flag('leak_detected') ? 'ALARM' : 'GOTOWE',
              detail: 'Akcja: ${_leakActionLabel(leakAction)}',
            ),
          ],
        ),
        const SizedBox(height: 12),
        ResponsiveGrid(
          minimumChildWidth: 390,
          children: [
            Form(
              key: temperatureForm,
              child: _AutomationCard(
                icon: Icons.thermostat_rounded,
                title: 'Termostat',
                children: [
                  LabeledSwitch(
                    label: 'Włącz sterowanie grzałką',
                    value: heaterEnabled,
                    onChanged: (value) =>
                        _edit('temperature', () => heaterEnabled = value),
                  ),
                  Row(
                    children: [
                      Expanded(
                        child: TextFormField(
                          controller: targetTemperature,
                          keyboardType: const TextInputType.numberWithOptions(
                            decimal: true,
                          ),
                          decoration: const InputDecoration(
                            labelText: 'Temperatura docelowa [°C]',
                            border: OutlineInputBorder(),
                          ),
                          validator: (value) => validateNumber(
                            value,
                            label: 'Temperatura',
                            minimum: 18,
                            maximum: 30,
                          ),
                        ),
                      ),
                      const SizedBox(width: 10),
                      Expanded(
                        child: TextFormField(
                          controller: hysteresis,
                          keyboardType: const TextInputType.numberWithOptions(
                            decimal: true,
                          ),
                          decoration: const InputDecoration(
                            labelText: 'Histereza [°C]',
                            border: OutlineInputBorder(),
                          ),
                          validator: (value) => validateNumber(
                            value,
                            label: 'Histereza',
                            minimum: 0.1,
                            maximum: 5,
                          ),
                        ),
                      ),
                    ],
                  ),
                  SaveButton(
                    onPressed: _saveTemperature,
                    label: 'Zapisz termostat',
                    busy: saving.contains('temperature'),
                  ),
                ],
              ),
            ),
            Form(
              key: co2Form,
              child: _AutomationCard(
                icon: Icons.bubble_chart_rounded,
                title: 'Automatyka CO₂',
                children: [
                  LabeledSwitch(
                    label: 'Włącz dozowanie CO₂',
                    subtitle:
                        'Sterowanie jest blokowane przy niewiarygodnym odczycie pH.',
                    value: co2Enabled,
                    onChanged: (value) =>
                        _edit('co2', () => co2Enabled = value),
                  ),
                  Row(
                    children: [
                      Expanded(
                        child: TextFormField(
                          controller: targetPh,
                          keyboardType: const TextInputType.numberWithOptions(
                            decimal: true,
                          ),
                          decoration: const InputDecoration(
                            labelText: 'Docelowe pH',
                            border: OutlineInputBorder(),
                          ),
                          validator: (value) => validateNumber(
                            value,
                            label: 'Docelowe pH',
                            minimum: 5,
                            maximum: 8.5,
                          ),
                        ),
                      ),
                      const SizedBox(width: 10),
                      Expanded(
                        child: TextFormField(
                          controller: co2Limit,
                          keyboardType: TextInputType.number,
                          decoration: const InputDecoration(
                            labelText: 'Limit czasu [min]',
                            border: OutlineInputBorder(),
                          ),
                          validator: (value) => validateNumber(
                            value,
                            label: 'Limit CO₂',
                            minimum: 1,
                            maximum: 1440,
                          ),
                        ),
                      ),
                    ],
                  ),
                  SaveButton(
                    onPressed: _saveCo2,
                    label: 'Zapisz CO₂',
                    busy: saving.contains('co2'),
                  ),
                ],
              ),
            ),
            Form(
              key: waterForm,
              child: _AutomationCard(
                icon: Icons.water_drop_rounded,
                title: 'Automatyczna dolewka ATO',
                children: [
                  LabeledSwitch(
                    label: 'Włącz kontrolę poziomu wody',
                    subtitle:
                        'Po wyłączeniu firmware natychmiast zatrzymuje dolewkę.',
                    value: waterEnabled,
                    onChanged: (value) =>
                        _edit('water', () => waterEnabled = value),
                  ),
                  TextFormField(
                    controller: waterTimeout,
                    keyboardType: TextInputType.number,
                    decoration: const InputDecoration(
                      labelText: 'Maksymalny czas dolewania [s]',
                      border: OutlineInputBorder(),
                    ),
                    validator: (value) => validateNumber(
                      value,
                      label: 'Limit ATO',
                      minimum: 5,
                      maximum: 300,
                    ),
                  ),
                  SaveButton(
                    onPressed: _saveWater,
                    label: 'Zapisz ATO',
                    busy: saving.contains('water'),
                  ),
                ],
              ),
            ),
            _AutomationCard(
              icon: Icons.health_and_safety_rounded,
              title: 'Reakcja na wyciek',
              children: [
                LabeledSwitch(
                  label: 'Włącz czujnik wycieku',
                  value: leakEnabled,
                  onChanged: (value) =>
                      _edit('leak', () => leakEnabled = value),
                ),
                DropdownButtonFormField<String>(
                  initialValue: leakAction,
                  isExpanded: true,
                  decoration: const InputDecoration(
                    labelText: 'Akcja awaryjna',
                    border: OutlineInputBorder(),
                  ),
                  items: const [
                    DropdownMenuItem(
                      value: 'alarm',
                      child: Text('Tylko alarm'),
                    ),
                    DropdownMenuItem(
                      value: 'disable_valves',
                      child: Text('Wyłącz zawory i dozowanie'),
                    ),
                    DropdownMenuItem(
                      value: 'disable_all',
                      child: Text('Wyłącz wszystkie wyjścia'),
                    ),
                  ],
                  onChanged: (value) =>
                      _edit('leak', () => leakAction = value ?? leakAction),
                ),
                SaveButton(
                  onPressed: _saveLeak,
                  label: 'Zapisz zabezpieczenia',
                  busy: saving.contains('leak'),
                ),
              ],
            ),
          ],
        ),
        if (message != null)
          Padding(
            padding: const EdgeInsets.only(top: 12),
            child: Text(message!),
          ),
      ],
    );
  }

  String _leakActionLabel(String action) => switch (action) {
    'alarm' => 'tylko alarm',
    'disable_valves' => 'wyłącz zawory',
    _ => 'wyłącz wszystko',
  };

  static Map<String, String> _fingerprints(JsonMap status) {
    final config = status.section('config');
    final modules = status.section('modules');
    return {
      'temperature': jsonEncode([
        config['target_temp'],
        config['temp_hysteresis'],
        modules['heater_enabled'],
      ]),
      'co2': jsonEncode([
        config['co2TargetPh'],
        config['co2MaxTimeMin'],
        modules['co2_enabled'],
      ]),
      'water': jsonEncode([
        status.section('water')['timeoutSec'],
        modules['water_level_enabled'],
      ]),
      'leak': jsonEncode([
        modules['leak_enabled'],
        status.section('leak')['action'],
      ]),
    };
  }
}

class _AutomationCard extends StatelessWidget {
  const _AutomationCard({
    required this.icon,
    required this.title,
    required this.children,
  });

  final IconData icon;
  final String title;
  final List<Widget> children;

  @override
  Widget build(BuildContext context) {
    return Card(
      margin: EdgeInsets.zero,
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Row(
              children: [
                Icon(icon),
                const SizedBox(width: 10),
                Expanded(
                  child: Text(
                    title,
                    style: const TextStyle(
                      fontWeight: FontWeight.w800,
                      fontSize: 17,
                    ),
                  ),
                ),
              ],
            ),
            const SizedBox(height: 14),
            for (var index = 0; index < children.length; index++) ...[
              children[index],
              if (index < children.length - 1) const SizedBox(height: 12),
            ],
          ],
        ),
      ),
    );
  }
}
