import 'package:flutter/material.dart';

import '../../design/app_theme.dart';
import '../../domain/entity_ids.dart';
import '../../domain/models.dart';
import '../../state/aquacyd_controller.dart';
import '../widgets/common.dart';

class AutomationPage extends StatelessWidget {
  const AutomationPage({required this.controller, super.key});

  final AquaCydController controller;

  static const _targets = <_ScheduleTarget>[
    _ScheduleTarget(
      'light_primary',
      'Światło główne',
      Icons.lightbulb_outline_rounded,
    ),
    _ScheduleTarget('light_secondary', 'Światło roślinne', Icons.eco_outlined),
    _ScheduleTarget('filter', 'Filtr', Icons.filter_alt_outlined),
    _ScheduleTarget('aerator', 'Napowietrzanie', Icons.bubble_chart_outlined),
  ];

  @override
  Widget build(BuildContext context) {
    final snapshot = controller.snapshot;
    return AquaPage(
      children: <Widget>[
        const SectionTitle(
          title: 'Automatyka lokalna',
          subtitle:
              'Ustawienia są zapisywane w CYD i działają nawet bez Home Assistanta',
        ),
        const SizedBox(height: 16),
        _ThermostatCard(
          enabled: snapshot.entity(AquaEntityIds.heaterMode)?.integer != 1,
          currentTemperature: snapshot.temperature,
          targetTemperature: snapshot.targetTemperature,
          hysteresis: snapshot.hysteresis,
          busy: controller.isBusy('thermostat'),
          onEdit: () => _editThermostat(context, snapshot),
        ),
        const SizedBox(height: 28),
        const SectionTitle(
          title: 'Harmonogramy dobowe',
          subtitle: 'Tryb, profil oraz godziny startu i zakończenia',
        ),
        const SizedBox(height: 14),
        LayoutBuilder(
          builder: (context, constraints) {
            final columns = constraints.maxWidth >= 820 ? 2 : 1;
            return GridView.count(
              crossAxisCount: columns,
              shrinkWrap: true,
              physics: const NeverScrollableScrollPhysics(),
              mainAxisSpacing: 12,
              crossAxisSpacing: 12,
              childAspectRatio: columns == 1 ? 2.25 : 1.75,
              children: <Widget>[
                for (final target in _targets)
                  _ScheduleCard(
                    target: target,
                    schedule: snapshot.schedules[target.id]!,
                    available: controller.entities.containsKey(
                      AquaEntityIds.schedule(target.id, 'mode'),
                    ),
                    busy: controller.isBusy('schedule_${target.id}'),
                    onEdit: () => _editSchedule(
                      context,
                      target,
                      snapshot.schedules[target.id]!,
                    ),
                  ),
              ],
            );
          },
        ),
        const SizedBox(height: 22),
        Card(
          child: Padding(
            padding: const EdgeInsets.all(18),
            child: Row(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: <Widget>[
                Icon(
                  Icons.offline_bolt_outlined,
                  color: Theme.of(context).colorScheme.primary,
                ),
                const SizedBox(width: 12),
                const Expanded(
                  child: Text(
                    'Home Assistant służy tu jako interfejs konfiguracji. '
                    'Zegar, harmonogramy, histereza oraz zabezpieczenia są '
                    'wykonywane autonomicznie w sterowniku CYD.',
                  ),
                ),
              ],
            ),
          ),
        ),
      ],
    );
  }

  Future<void> _editSchedule(
    BuildContext context,
    _ScheduleTarget target,
    AquaSchedule current,
  ) async {
    final result = await showDialog<AquaSchedule>(
      context: context,
      builder: (context) => _ScheduleDialog(target: target, initial: current),
    );
    if (result != null) {
      await controller.saveSchedule(result);
    }
  }

  Future<void> _editThermostat(
    BuildContext context,
    AquariumSnapshot snapshot,
  ) async {
    final result = await showDialog<_ThermostatValues>(
      context: context,
      builder: (context) => _ThermostatDialog(
        initial: _ThermostatValues(
          enabled: snapshot.entity(AquaEntityIds.heaterMode)?.integer != 1,
          target: snapshot.targetTemperature ?? 24,
          hysteresis: snapshot.hysteresis ?? 0.5,
        ),
      ),
    );
    if (result != null) {
      await controller.saveThermostat(
        enabled: result.enabled,
        targetTemperature: result.target,
        hysteresis: result.hysteresis,
      );
    }
  }
}

class _ThermostatCard extends StatelessWidget {
  const _ThermostatCard({
    required this.enabled,
    required this.currentTemperature,
    required this.targetTemperature,
    required this.hysteresis,
    required this.busy,
    required this.onEdit,
  });

  final bool enabled;
  final double? currentTemperature;
  final double? targetTemperature;
  final double? hysteresis;
  final bool busy;
  final VoidCallback onEdit;

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(22),
        child: LayoutBuilder(
          builder: (context, constraints) {
            final compact = constraints.maxWidth < 560;
            final icon = Container(
              padding: const EdgeInsets.all(14),
              decoration: BoxDecoration(
                color: AquaColors.red.withValues(alpha: 0.13),
                borderRadius: BorderRadius.circular(17),
              ),
              child: const Icon(
                Icons.thermostat_rounded,
                color: AquaColors.red,
                size: 32,
              ),
            );
            final details = Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: <Widget>[
                Text(
                  'Termostat',
                  style: Theme.of(
                    context,
                  ).textTheme.titleLarge?.copyWith(fontWeight: FontWeight.w800),
                ),
                const SizedBox(height: 5),
                Text(
                  enabled
                      ? 'Cel ${formatMeasurement(targetTemperature, '°C')} '
                            '• histereza ${formatMeasurement(hysteresis, '°C')}'
                      : 'Regulacja temperatury wyłączona',
                ),
                const SizedBox(height: 3),
                Text(
                  'Aktualnie ${formatMeasurement(currentTemperature, '°C')}',
                  style: Theme.of(context).textTheme.bodySmall,
                ),
              ],
            );
            final button = FilledButton.tonalIcon(
              onPressed: busy ? null : onEdit,
              icon: busy
                  ? const SizedBox(
                      width: 18,
                      height: 18,
                      child: CircularProgressIndicator(strokeWidth: 2),
                    )
                  : const Icon(Icons.edit_outlined),
              label: const Text('Ustaw'),
            );
            if (compact) {
              return Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: <Widget>[
                  Align(alignment: Alignment.centerLeft, child: icon),
                  const SizedBox(height: 14),
                  details,
                  const SizedBox(height: 16),
                  button,
                ],
              );
            }
            return Row(
              children: <Widget>[
                icon,
                const SizedBox(width: 18),
                Expanded(child: details),
                const SizedBox(width: 18),
                button,
              ],
            );
          },
        ),
      ),
    );
  }
}

class _ScheduleCard extends StatelessWidget {
  const _ScheduleCard({
    required this.target,
    required this.schedule,
    required this.available,
    required this.busy,
    required this.onEdit,
  });

  final _ScheduleTarget target;
  final AquaSchedule schedule;
  final bool available;
  final bool busy;
  final VoidCallback onEdit;

  static const _modeLabels = <String>[
    'Według harmonogramu',
    'Zawsze włączone',
    'Zawsze wyłączone',
  ];
  static const _profileLabels = <String>['Auto', 'Day', 'Daybreak', 'Night'];

  @override
  Widget build(BuildContext context) {
    final light = target.id.startsWith('light_');
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(20),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: <Widget>[
            Row(
              children: <Widget>[
                Container(
                  padding: const EdgeInsets.all(10),
                  decoration: BoxDecoration(
                    color: Theme.of(
                      context,
                    ).colorScheme.primaryContainer.withValues(alpha: 0.65),
                    borderRadius: BorderRadius.circular(13),
                  ),
                  child: Icon(target.icon),
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: Text(
                    target.label,
                    style: Theme.of(context).textTheme.titleMedium?.copyWith(
                      fontWeight: FontWeight.w800,
                    ),
                  ),
                ),
                IconButton(
                  tooltip: 'Edytuj harmonogram',
                  onPressed: busy ? null : onEdit,
                  icon: busy
                      ? const SizedBox(
                          width: 19,
                          height: 19,
                          child: CircularProgressIndicator(strokeWidth: 2),
                        )
                      : const Icon(Icons.edit_outlined),
                ),
              ],
            ),
            const SizedBox(height: 16),
            if (!available)
              Text(
                'Brak telemetrii harmonogramu — pokazano wartości bezpieczne.',
                style: TextStyle(
                  color: Theme.of(context).colorScheme.error,
                  fontSize: 12,
                ),
              )
            else ...<Widget>[
              Text(
                _modeLabels[schedule.mode],
                style: const TextStyle(fontWeight: FontWeight.w700),
              ),
              const SizedBox(height: 4),
              Text('${schedule.startText} — ${schedule.endText}'),
              if (light) ...<Widget>[
                const SizedBox(height: 4),
                Text('Profil: ${_profileLabels[schedule.profile]}'),
              ],
            ],
          ],
        ),
      ),
    );
  }
}

class _ScheduleDialog extends StatefulWidget {
  const _ScheduleDialog({required this.target, required this.initial});

  final _ScheduleTarget target;
  final AquaSchedule initial;

  @override
  State<_ScheduleDialog> createState() => _ScheduleDialogState();
}

class _ScheduleDialogState extends State<_ScheduleDialog> {
  late int _mode;
  late int _profile;
  late int _start;
  late int _end;

  @override
  void initState() {
    super.initState();
    _mode = widget.initial.mode;
    _profile = widget.initial.profile;
    _start = widget.initial.startMinute;
    _end = widget.initial.endMinute;
  }

  @override
  Widget build(BuildContext context) {
    final isLight = widget.target.id.startsWith('light_');
    return AlertDialog(
      title: Text(widget.target.label),
      content: SizedBox(
        width: 440,
        child: SingleChildScrollView(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: <Widget>[
              DropdownButtonFormField<int>(
                initialValue: _mode,
                decoration: const InputDecoration(labelText: 'Tryb'),
                items: const <DropdownMenuItem<int>>[
                  DropdownMenuItem(value: 0, child: Text('Harmonogram')),
                  DropdownMenuItem(value: 1, child: Text('Zawsze włączone')),
                  DropdownMenuItem(value: 2, child: Text('Zawsze wyłączone')),
                ],
                onChanged: (value) => setState(() => _mode = value ?? 0),
              ),
              if (isLight) ...<Widget>[
                const SizedBox(height: 14),
                DropdownButtonFormField<int>(
                  initialValue: _profile,
                  decoration: const InputDecoration(
                    labelText: 'Profil światła',
                  ),
                  items: const <DropdownMenuItem<int>>[
                    DropdownMenuItem(value: 0, child: Text('Auto')),
                    DropdownMenuItem(value: 1, child: Text('Day')),
                    DropdownMenuItem(value: 2, child: Text('Daybreak')),
                    DropdownMenuItem(value: 3, child: Text('Night')),
                  ],
                  onChanged: (value) => setState(() => _profile = value ?? 0),
                ),
              ],
              const SizedBox(height: 18),
              Row(
                children: <Widget>[
                  Expanded(
                    child: _TimeButton(
                      label: 'Początek',
                      minute: _start,
                      onTap: () => _pickTime(start: true),
                    ),
                  ),
                  const SizedBox(width: 12),
                  Expanded(
                    child: _TimeButton(
                      label: 'Koniec',
                      minute: _end,
                      onTap: () => _pickTime(start: false),
                    ),
                  ),
                ],
              ),
            ],
          ),
        ),
      ),
      actions: <Widget>[
        TextButton(
          onPressed: () => Navigator.pop(context),
          child: const Text('Anuluj'),
        ),
        FilledButton(
          onPressed: () => Navigator.pop(
            context,
            AquaSchedule(
              target: widget.target.id,
              mode: _mode,
              profile: _profile,
              startMinute: _start,
              endMinute: _end,
            ),
          ),
          child: const Text('Zapisz w CYD'),
        ),
      ],
    );
  }

  Future<void> _pickTime({required bool start}) async {
    final minute = start ? _start : _end;
    final selected = await showTimePicker(
      context: context,
      initialTime: TimeOfDay(hour: minute ~/ 60, minute: minute % 60),
      helpText: start ? 'Początek harmonogramu' : 'Koniec harmonogramu',
      cancelText: 'Anuluj',
      confirmText: 'Ustaw',
    );
    if (selected != null) {
      setState(() {
        final value = selected.hour * 60 + selected.minute;
        if (start) {
          _start = value;
        } else {
          _end = value;
        }
      });
    }
  }
}

class _TimeButton extends StatelessWidget {
  const _TimeButton({
    required this.label,
    required this.minute,
    required this.onTap,
  });

  final String label;
  final int minute;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final time = TimeOfDay(hour: minute ~/ 60, minute: minute % 60);
    return OutlinedButton.icon(
      onPressed: onTap,
      icon: const Icon(Icons.schedule_rounded),
      label: Column(
        mainAxisSize: MainAxisSize.min,
        children: <Widget>[
          Text(label, style: const TextStyle(fontSize: 11)),
          Text(
            time.format(context),
            style: const TextStyle(fontSize: 16, fontWeight: FontWeight.w800),
          ),
        ],
      ),
    );
  }
}

class _ThermostatDialog extends StatefulWidget {
  const _ThermostatDialog({required this.initial});

  final _ThermostatValues initial;

  @override
  State<_ThermostatDialog> createState() => _ThermostatDialogState();
}

class _ThermostatDialogState extends State<_ThermostatDialog> {
  late bool _enabled;
  late double _target;
  late double _hysteresis;

  @override
  void initState() {
    super.initState();
    _enabled = widget.initial.enabled;
    _target = widget.initial.target.clamp(18, 30);
    _hysteresis = widget.initial.hysteresis.clamp(0.1, 5);
  }

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      title: const Text('Ustawienia termostatu'),
      content: SizedBox(
        width: 440,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: <Widget>[
            SwitchListTile(
              contentPadding: EdgeInsets.zero,
              title: const Text('Regulacja progowa'),
              subtitle: const Text('CYD sam steruje grzałką z histerezą'),
              value: _enabled,
              onChanged: (value) => setState(() => _enabled = value),
            ),
            const SizedBox(height: 12),
            _LabeledSlider(
              label: 'Temperatura docelowa',
              valueText: '${_target.toStringAsFixed(1)} °C',
              value: _target,
              min: 18,
              max: 30,
              divisions: 120,
              enabled: _enabled,
              onChanged: (value) => setState(() => _target = value),
            ),
            const SizedBox(height: 14),
            _LabeledSlider(
              label: 'Histereza',
              valueText: '${_hysteresis.toStringAsFixed(1)} °C',
              value: _hysteresis,
              min: 0.1,
              max: 5,
              divisions: 49,
              enabled: _enabled,
              onChanged: (value) => setState(() => _hysteresis = value),
            ),
          ],
        ),
      ),
      actions: <Widget>[
        TextButton(
          onPressed: () => Navigator.pop(context),
          child: const Text('Anuluj'),
        ),
        FilledButton(
          onPressed: () => Navigator.pop(
            context,
            _ThermostatValues(
              enabled: _enabled,
              target: _target,
              hysteresis: _hysteresis,
            ),
          ),
          child: const Text('Zapisz w CYD'),
        ),
      ],
    );
  }
}

class _LabeledSlider extends StatelessWidget {
  const _LabeledSlider({
    required this.label,
    required this.valueText,
    required this.value,
    required this.min,
    required this.max,
    required this.divisions,
    required this.enabled,
    required this.onChanged,
  });

  final String label;
  final String valueText;
  final double value;
  final double min;
  final double max;
  final int divisions;
  final bool enabled;
  final ValueChanged<double> onChanged;

  @override
  Widget build(BuildContext context) {
    return Column(
      children: <Widget>[
        Row(
          children: <Widget>[
            Expanded(child: Text(label)),
            Text(
              valueText,
              style: const TextStyle(fontWeight: FontWeight.w800),
            ),
          ],
        ),
        Slider(
          value: value,
          min: min,
          max: max,
          divisions: divisions,
          label: valueText,
          onChanged: enabled ? onChanged : null,
        ),
      ],
    );
  }
}

class _ScheduleTarget {
  const _ScheduleTarget(this.id, this.label, this.icon);

  final String id;
  final String label;
  final IconData icon;
}

class _ThermostatValues {
  const _ThermostatValues({
    required this.enabled,
    required this.target,
    required this.hysteresis,
  });

  final bool enabled;
  final double target;
  final double hysteresis;
}
