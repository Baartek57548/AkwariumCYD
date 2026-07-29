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
      text: config.nullableNumber('target_temp')?.toStringAsFixed(1) ?? '',
    );
    hysteresis = TextEditingController(
      text: config.nullableNumber('temp_hysteresis')?.toStringAsFixed(1) ?? '',
    );
    targetPh = TextEditingController(
      text: config.nullableNumber('co2TargetPh')?.toStringAsFixed(2) ?? '',
    );
    co2Limit = TextEditingController(
      text: config['co2MaxTimeMin'] == null
          ? ''
          : '${config.integer('co2MaxTimeMin')}',
    );
    waterTimeout = TextEditingController(
      text: status.section('water')['timeoutSec'] == null
          ? ''
          : '${status.section('water').integer('timeoutSec')}',
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
          targetTemperature.text =
              config.nullableNumber('target_temp')?.toStringAsFixed(1) ?? '';
          hysteresis.text =
              config.nullableNumber('temp_hysteresis')?.toStringAsFixed(1) ??
              '';
          heaterEnabled = modules.flag('heater_enabled');
          break;
        case 'co2':
          targetPh.text =
              config.nullableNumber('co2TargetPh')?.toStringAsFixed(2) ?? '';
          co2Limit.text = config['co2MaxTimeMin'] == null
              ? ''
              : '${config.integer('co2MaxTimeMin')}';
          co2Enabled = modules.flag('co2_enabled');
          break;
        case 'water':
          waterTimeout.text = status.section('water')['timeoutSec'] == null
              ? ''
              : '${status.section('water').integer('timeoutSec')}';
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
    final canEdit = widget.session.canIssueCommands;
    final hasStoredData = widget.session.hasStatusData;
    return ControllerPageBody(
      children: [
        SectionHeader(
          title: 'Automatyka',
          description:
              'Termostat, dozowanie CO₂, automatyczna dolewka oraz reakcja na wyciek.',
          trailing: IconButton(
            onPressed: hasStoredData ? _restoreFromController : null,
            icon: const Icon(Icons.restore_rounded),
            tooltip: 'Przywróć dane sterownika',
          ),
        ),
        if (!canEdit) ...[
          StatusBanner(
            icon: Icons.visibility_rounded,
            title: widget.session.hasCachedSnapshot
                ? 'Automatyka tylko do podglądu'
                : 'Brak zapisanych reguł automatyki',
            message: widget.session.hasCachedSnapshot
                ? widget.session.commandBlockReason ??
                      'Połącz sterownik, aby edytować reguły automatyki.'
                : 'Połącz sterownik przez Wi‑Fi albo Bluetooth, aby pobrać '
                      'konfigurację. Sekcje zostaną udostępnione po pierwszej '
                      'synchronizacji.',
            isError: false,
          ),
          const SizedBox(height: 12),
        ],
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
          minimumChildWidth: 390,
          children: [
            Form(
              key: temperatureForm,
              child: _AutomationCard(
                icon: Icons.thermostat_rounded,
                title: 'Termostat',
                summary: hasStoredData
                    ? _sectionSummary(
                        'temperature',
                        '${heaterEnabled ? 'Włączony' : 'Wyłączony'} · '
                            'cel ${_decimalValue(targetTemperature, 1)} °C · '
                            'histereza ${_decimalValue(hysteresis, 1)} °C',
                      )
                    : null,
                enabled: hasStoredData,
                children: [
                  if (hasStoredData) ...[
                    LabeledSwitch(
                      label: 'Włącz sterowanie grzałką',
                      value: heaterEnabled,
                      onChanged: canEdit
                          ? (value) => _edit(
                              'temperature',
                              () => heaterEnabled = value,
                            )
                          : null,
                    ),
                    Row(
                      children: [
                        Expanded(
                          child: TextFormField(
                            controller: targetTemperature,
                            enabled: canEdit,
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
                            enabled: canEdit,
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
                      onPressed: canEdit ? _saveTemperature : null,
                      label: 'Zapisz termostat',
                      busy: saving.contains('temperature'),
                    ),
                  ],
                ],
              ),
            ),
            Form(
              key: co2Form,
              child: _AutomationCard(
                icon: Icons.bubble_chart_rounded,
                title: 'Automatyka CO₂',
                summary: hasStoredData
                    ? _sectionSummary(
                        'co2',
                        '${co2Enabled ? 'Włączona' : 'Wyłączona'} · '
                            'cel pH ${_decimalValue(targetPh, 2)} · '
                            'limit ${_integerValue(co2Limit)} min',
                      )
                    : null,
                enabled: hasStoredData,
                children: [
                  if (hasStoredData) ...[
                    LabeledSwitch(
                      label: 'Włącz dozowanie CO₂',
                      subtitle:
                          'Sterowanie jest blokowane przy niewiarygodnym odczycie pH.',
                      value: co2Enabled,
                      onChanged: canEdit
                          ? (value) => _edit('co2', () => co2Enabled = value)
                          : null,
                    ),
                    Row(
                      children: [
                        Expanded(
                          child: TextFormField(
                            controller: targetPh,
                            enabled: canEdit,
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
                            enabled: canEdit,
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
                      onPressed: canEdit ? _saveCo2 : null,
                      label: 'Zapisz CO₂',
                      busy: saving.contains('co2'),
                    ),
                  ],
                ],
              ),
            ),
            Form(
              key: waterForm,
              child: _AutomationCard(
                icon: Icons.water_drop_rounded,
                title: 'Automatyczna dolewka ATO',
                summary: hasStoredData
                    ? _sectionSummary(
                        'water',
                        '${waterEnabled ? 'Włączona' : 'Wyłączona'} · '
                            'limit ${_integerValue(waterTimeout)} s',
                      )
                    : null,
                enabled: hasStoredData,
                children: [
                  if (hasStoredData) ...[
                    LabeledSwitch(
                      label: 'Włącz kontrolę poziomu wody',
                      subtitle:
                          'Po wyłączeniu firmware natychmiast zatrzymuje dolewkę.',
                      value: waterEnabled,
                      onChanged: canEdit
                          ? (value) =>
                                _edit('water', () => waterEnabled = value)
                          : null,
                    ),
                    TextFormField(
                      controller: waterTimeout,
                      enabled: canEdit,
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
                      onPressed: canEdit ? _saveWater : null,
                      label: 'Zapisz ATO',
                      busy: saving.contains('water'),
                    ),
                  ],
                ],
              ),
            ),
            _AutomationCard(
              icon: Icons.health_and_safety_rounded,
              title: 'Reakcja na wyciek',
              summary: hasStoredData
                  ? _sectionSummary(
                      'leak',
                      '${leakEnabled ? 'Włączona' : 'Wyłączona'} · '
                          '${_leakActionLabel(leakAction)}',
                    )
                  : null,
              enabled: hasStoredData,
              children: [
                if (hasStoredData) ...[
                  LabeledSwitch(
                    label: 'Włącz czujnik wycieku',
                    value: leakEnabled,
                    onChanged: canEdit
                        ? (value) => _edit('leak', () => leakEnabled = value)
                        : null,
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
                    onChanged: canEdit
                        ? (value) => _edit(
                            'leak',
                            () => leakAction = value ?? leakAction,
                          )
                        : null,
                  ),
                  SaveButton(
                    onPressed: canEdit ? _saveLeak : null,
                    label: 'Zapisz zabezpieczenia',
                    busy: saving.contains('leak'),
                  ),
                ],
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

  String _sectionSummary(String section, String configuredSummary) {
    if (saving.contains(section)) return 'Zapisywanie…';
    if (_remoteChangedSections.contains(section)) {
      return 'Konflikt · sterownik ma nowsze ustawienia';
    }
    if (_dirtySections.contains(section)) return 'Niezapisane zmiany';
    return configuredSummary;
  }

  String _decimalValue(TextEditingController controller, int fractionDigits) {
    final value = double.tryParse(controller.text.replaceAll(',', '.'));
    return value?.toStringAsFixed(fractionDigits) ?? '—';
  }

  String _integerValue(TextEditingController controller) {
    final value = int.tryParse(controller.text.trim());
    return value?.toString() ?? '—';
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
    required this.summary,
    required this.enabled,
    required this.children,
  });

  final IconData icon;
  final String title;
  final String? summary;
  final bool enabled;
  final List<Widget> children;

  @override
  Widget build(BuildContext context) {
    return Card(
      margin: EdgeInsets.zero,
      clipBehavior: Clip.antiAlias,
      child: ExpansionTile(
        initiallyExpanded: false,
        maintainState: true,
        enabled: enabled,
        leading: Icon(icon),
        title: Text(
          title,
          style: const TextStyle(fontWeight: FontWeight.w800, fontSize: 17),
        ),
        subtitle: summary == null
            ? null
            : Text(summary!, maxLines: 2, overflow: TextOverflow.ellipsis),
        trailing: enabled ? null : const Icon(Icons.cloud_off_outlined),
        childrenPadding: const EdgeInsets.fromLTRB(16, 0, 16, 16),
        children: [
          Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              for (var index = 0; index < children.length; index++) ...[
                children[index],
                if (index < children.length - 1) const SizedBox(height: 12),
              ],
            ],
          ),
        ],
      ),
    );
  }
}
