import 'dart:async';

import 'package:flutter/material.dart';

import '../../design_system.dart';
import '../controller_api.dart';
import '../controller_session.dart';
import '../controller_shell.dart';
import '../data_access.dart';
import '../schedule_control.dart';
import '../widgets.dart';

class ControlHubView extends StatefulWidget {
  const ControlHubView({
    super.key,
    required this.session,
    required this.runAction,
    required this.ensureAdmin,
  });

  final ControllerSession session;
  final RunControllerAction runAction;
  final Future<bool> Function() ensureAdmin;

  @override
  State<ControlHubView> createState() => _ControlHubViewState();
}

class _ControlHubViewState extends State<ControlHubView> {
  final Set<String> _busyOutputs = {};
  bool _feeding = false;

  Future<void> _setOutputMode(
    _OutputDefinition output,
    OutputControlMode current,
    OutputControlMode selected,
  ) async {
    if (selected == current || _busyOutputs.contains(output.id)) return;
    setState(() => _busyOutputs.add(output.id));
    try {
      if (output.heater) {
        await widget.runAction(
          output.action,
          payload: {'state': selected != OutputControlMode.forcedOff},
          confirmation: selected == OutputControlMode.forcedOff
              ? 'Wyłączyć termostat? Grzałka pozostanie wyłączona niezależnie '
                    'od temperatury wody.'
              : 'Włączyć regulację termostatyczną według temperatury docelowej?',
        );
      } else if (selected == OutputControlMode.automatic) {
        await widget.runAction(
          'save_schedule',
          payload: buildScheduleModePatch(
            output.scheduleChannel,
            OutputControlMode.automatic,
          ),
          confirmation:
              'Przywrócić tryb AUTO dla „${output.label}”? Wyjście będzie '
              'ponownie sterowane harmonogramem.',
        );
      } else {
        final enabled = selected == OutputControlMode.forcedOn;
        await widget.runAction(
          output.action,
          payload: {'state': enabled},
          confirmation:
              'Ustawić „${output.label}” na ${enabled ? "ciągłe ON" : "ciągłe OFF"}? '
              'Ta zmiana zastąpi tryb harmonogramu do czasu przywrócenia AUTO.',
        );
      }
    } on ControllerApiException {
      // Powłoka pokazuje precyzyjny komunikat i zachowuje stan potwierdzony.
    } finally {
      if (mounted) setState(() => _busyOutputs.remove(output.id));
    }
  }

  Future<void> _feed() async {
    if (_feeding) return;
    setState(() => _feeding = true);
    try {
      await widget.runAction(
        'feed_now',
        confirmation:
            'Uruchomić teraz jedną dawkę karmnika? Kolejne kliknięcia będą '
            'zablokowane do zakończenia cyklu.',
      );
    } on ControllerApiException {
      // Powłoka prezentuje komunikat zwrócony przez sterownik.
    } finally {
      if (mounted) setState(() => _feeding = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final status = widget.session.status;
    final modules = status.section('modules');
    final sensors = status.section('sensors');
    final feeding = status.section('feeding');
    final hardwareReady =
        widget.session.isDevelopment ||
        (sensors.flag('mcp_present', true) && sensors.flag('mcp_valid', true));
    final commandReady = widget.session.canIssueCommands && hardwareReady;
    final blockReason = !widget.session.canIssueCommands
        ? widget.session.commandBlockReason
        : !hardwareReady
        ? 'Moduł wyjść MCP23017 jest niedostępny. Sterowanie pozostaje '
              'zablokowane do odzyskania magistrali.'
        : null;
    const outputs = <_OutputDefinition>[
      _OutputDefinition(
        id: 'light1',
        scheduleChannel: 'light1',
        action: 'set_light1',
        moduleKey: 'light_on',
        label: 'Światło główne',
        icon: Icons.lightbulb_rounded,
      ),
      _OutputDefinition(
        id: 'light2',
        scheduleChannel: 'light2',
        action: 'set_light2',
        moduleKey: 'plant_light_on',
        label: 'Światło roślinne',
        icon: Icons.wb_twilight_rounded,
      ),
      _OutputDefinition(
        id: 'filter',
        scheduleChannel: 'filter',
        action: 'set_filter',
        moduleKey: 'filter_on',
        label: 'Filtracja',
        icon: Icons.filter_alt_rounded,
      ),
      _OutputDefinition(
        id: 'air',
        scheduleChannel: 'air',
        action: 'set_aeration',
        moduleKey: 'air_on',
        label: 'Napowietrzanie',
        icon: Icons.air_rounded,
      ),
      _OutputDefinition(
        id: 'heater',
        scheduleChannel: 'heater',
        action: 'set_heater',
        moduleKey: 'heater_on',
        label: 'Termostat',
        icon: Icons.thermostat_rounded,
        heater: true,
      ),
    ];

    return ControllerPageBody(
      maxWidth: 1180,
      padding: const EdgeInsets.fromLTRB(14, 16, 14, 28),
      onRefresh: () => widget.session.refresh(),
      children: [
        const SectionHeader(
          title: 'Sterowanie operacyjne',
          description:
              'Stan fizyczny jest oddzielony od trybu pracy. AUTO zachowuje '
              'automatykę, a wymuszenie ON/OFF trwale zastępuje harmonogram.',
        ),
        if (blockReason != null) ...[
          StatusBanner(
            icon: Icons.lock_clock_rounded,
            title: 'Sterowanie chwilowo zablokowane',
            message: blockReason,
            isError: !hardwareReady,
          ),
          const SizedBox(height: AquaSpacing.md),
        ],
        ResponsiveGrid(
          minimumChildWidth: 320,
          spacing: AquaSpacing.sm,
          children: [
            for (final output in outputs)
              _OutputModeCard(
                definition: output,
                physicalOn: modules.flag(output.moduleKey),
                schedule: output.heater
                    ? OutputScheduleState(
                        mode: heaterAutomationEnabled(status)
                            ? OutputControlMode.automatic
                            : OutputControlMode.forcedOff,
                        start: '00:00',
                        end: '23:59',
                      )
                    : readOutputSchedule(status, output.scheduleChannel),
                busy: _busyOutputs.contains(output.id),
                enabled: commandReady,
                legacyBluetooth: widget.session.isLegacyBluetooth,
                onChanged: (current, selected) =>
                    unawaited(_setOutputMode(output, current, selected)),
              ),
          ],
        ),
        const SizedBox(height: AquaSpacing.lg),
        const SectionHeader(
          title: 'Procesy wykonawcze',
          description:
              'Karmienie ręczne oraz automatyczne układy dozowania i dolewki.',
        ),
        ResponsiveGrid(
          minimumChildWidth: 300,
          spacing: AquaSpacing.sm,
          children: [
            _FeederCard(
              active: feeding.flag('active'),
              busy: _feeding,
              enabled: commandReady,
              lastResult: feeding.text('lastResult', 'brak danych'),
              onFeed: _feed,
            ),
            _ReadOnlyActuatorCard(
              icon: Icons.bubble_chart_rounded,
              title: 'Dozowanie CO₂',
              physicalOn: modules.flag('co2_on'),
              automationEnabled: modules.flag('co2_enabled'),
              detail: sensors.flag('ph_valid')
                  ? 'Aktualne pH ${sensors.number('ph').toStringAsFixed(2)}'
                  : 'Brak wiarygodnego pomiaru pH',
            ),
            _ReadOnlyActuatorCard(
              icon: Icons.water_drop_rounded,
              title: 'Dolewka ATO',
              physicalOn: status.section('water').flag('active'),
              automationEnabled: modules.flag('water_level_enabled'),
              detail: sensors.flag('water_level_valid')
                  ? sensors.flag('water_level_high')
                        ? 'Poziom wody prawidłowy'
                        : 'Wymagane uzupełnienie'
                  : 'Brak wiarygodnego czujnika poziomu',
            ),
          ],
        ),
        if (widget.session.isLegacyBluetooth) ...[
          const SizedBox(height: AquaSpacing.md),
          const StatusBanner(
            icon: Icons.bluetooth_rounded,
            title: 'Ograniczony protokół BLE v1',
            message:
                'Kanał pozwala wymuszać ON/OFF, ale przywrócenie harmonogramu '
                'AUTO wymaga Wi‑Fi albo firmware z BLE v2.',
            isError: false,
          ),
        ],
      ],
    );
  }
}

class _OutputModeCard extends StatelessWidget {
  const _OutputModeCard({
    required this.definition,
    required this.physicalOn,
    required this.schedule,
    required this.busy,
    required this.enabled,
    required this.legacyBluetooth,
    required this.onChanged,
  });

  final _OutputDefinition definition;
  final bool physicalOn;
  final OutputScheduleState schedule;
  final bool busy;
  final bool enabled;
  final bool legacyBluetooth;
  final void Function(OutputControlMode current, OutputControlMode selected)
  onChanged;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    final status = context.statusColors;
    final activeTone = physicalOn ? status.success : colors.onSurfaceVariant;
    final selectable = enabled && !busy;
    final options = definition.heater
        ? const [OutputControlMode.automatic, OutputControlMode.forcedOff]
        : legacyBluetooth
        ? const [OutputControlMode.forcedOn, OutputControlMode.forcedOff]
        : OutputControlMode.values;

    return Card(
      child: Padding(
        padding: const EdgeInsets.all(AquaSpacing.md),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Row(
              children: [
                Container(
                  width: 46,
                  height: 46,
                  decoration: BoxDecoration(
                    color: activeTone.withValues(alpha: 0.14),
                    borderRadius: BorderRadius.circular(AquaRadius.control),
                  ),
                  child: Icon(definition.icon, color: activeTone),
                ),
                const SizedBox(width: AquaSpacing.sm),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        definition.label,
                        style: Theme.of(context).textTheme.titleMedium
                            ?.copyWith(fontWeight: FontWeight.w900),
                      ),
                      Text(
                        physicalOn
                            ? 'WYJŚCIE FIZYCZNE ON'
                            : 'WYJŚCIE FIZYCZNE OFF',
                        style: Theme.of(context).textTheme.labelSmall?.copyWith(
                          color: activeTone,
                          fontWeight: FontWeight.w900,
                          letterSpacing: 0.55,
                        ),
                      ),
                    ],
                  ),
                ),
                if (busy)
                  const SizedBox.square(
                    dimension: 22,
                    child: CircularProgressIndicator(strokeWidth: 2.5),
                  ),
              ],
            ),
            const SizedBox(height: AquaSpacing.md),
            Row(
              children: [
                Icon(
                  schedule.mode == OutputControlMode.automatic
                      ? Icons.schedule_rounded
                      : Icons.pan_tool_alt_rounded,
                  size: 18,
                  color: colors.onSurfaceVariant,
                ),
                const SizedBox(width: AquaSpacing.xs),
                Expanded(
                  child: Text(
                    '${schedule.modeLabel} · ${schedule.windowLabel}',
                    style: TextStyle(
                      color: colors.onSurfaceVariant,
                      fontWeight: FontWeight.w600,
                    ),
                  ),
                ),
              ],
            ),
            const SizedBox(height: AquaSpacing.md),
            _ModeSelector(
              options: options,
              selected: schedule.mode,
              enabled: selectable,
              onSelected: (selected) => onChanged(schedule.mode, selected),
            ),
          ],
        ),
      ),
    );
  }
}

class _ModeSelector extends StatelessWidget {
  const _ModeSelector({
    required this.options,
    required this.selected,
    required this.enabled,
    required this.onSelected,
  });

  final List<OutputControlMode> options;
  final OutputControlMode selected;
  final bool enabled;
  final ValueChanged<OutputControlMode> onSelected;

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(
      builder: (context, constraints) {
        final largeText = MediaQuery.textScalerOf(context).scale(1) > 1.25;
        if (constraints.maxWidth < 300 || largeText) {
          return DropdownButtonFormField<OutputControlMode>(
            initialValue: options.contains(selected) ? selected : null,
            isExpanded: true,
            decoration: const InputDecoration(labelText: 'Tryb sterowania'),
            items: [
              for (final option in options)
                DropdownMenuItem(
                  value: option,
                  child: Text(_shortModeLabel(option)),
                ),
            ],
            onChanged: enabled
                ? (value) {
                    if (value != null) onSelected(value);
                  }
                : null,
          );
        }
        return SegmentedButton<OutputControlMode>(
          segments: [
            for (final option in options)
              ButtonSegment(
                value: option,
                icon: Icon(_modeIcon(option), size: 18),
                label: Text(_shortModeLabel(option)),
              ),
          ],
          selected: options.contains(selected) ? {selected} : const {},
          emptySelectionAllowed: true,
          showSelectedIcon: false,
          onSelectionChanged: enabled
              ? (selection) {
                  if (selection.isNotEmpty) onSelected(selection.first);
                }
              : null,
        );
      },
    );
  }

  static String _shortModeLabel(OutputControlMode mode) => switch (mode) {
    OutputControlMode.automatic => 'AUTO',
    OutputControlMode.forcedOn => 'ON',
    OutputControlMode.forcedOff => 'OFF',
  };

  static IconData _modeIcon(OutputControlMode mode) => switch (mode) {
    OutputControlMode.automatic => Icons.schedule_rounded,
    OutputControlMode.forcedOn => Icons.power_rounded,
    OutputControlMode.forcedOff => Icons.power_off_rounded,
  };
}

class _FeederCard extends StatelessWidget {
  const _FeederCard({
    required this.active,
    required this.busy,
    required this.enabled,
    required this.lastResult,
    required this.onFeed,
  });

  final bool active;
  final bool busy;
  final bool enabled;
  final String lastResult;
  final VoidCallback onFeed;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    final tone = active ? context.statusColors.warning : colors.primary;
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(AquaSpacing.md),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Row(
              children: [
                Icon(Icons.set_meal_rounded, color: tone, size: 30),
                const SizedBox(width: AquaSpacing.sm),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        'Karmnik',
                        style: Theme.of(context).textTheme.titleMedium
                            ?.copyWith(fontWeight: FontWeight.w900),
                      ),
                      Text(
                        active ? 'Trwa podawanie pokarmu' : 'Gotowy do dawki',
                        style: TextStyle(color: colors.onSurfaceVariant),
                      ),
                    ],
                  ),
                ),
              ],
            ),
            const SizedBox(height: AquaSpacing.sm),
            Text(
              'Ostatni wynik: $lastResult',
              style: Theme.of(
                context,
              ).textTheme.bodySmall?.copyWith(color: colors.onSurfaceVariant),
            ),
            const SizedBox(height: AquaSpacing.md),
            FilledButton.icon(
              onPressed: !enabled || busy || active ? null : onFeed,
              icon: busy || active
                  ? const SizedBox.square(
                      dimension: 18,
                      child: CircularProgressIndicator(strokeWidth: 2),
                    )
                  : const Icon(Icons.play_arrow_rounded),
              label: Text(active ? 'Cykl w toku' : 'Podaj jedną dawkę'),
            ),
          ],
        ),
      ),
    );
  }
}

class _ReadOnlyActuatorCard extends StatelessWidget {
  const _ReadOnlyActuatorCard({
    required this.icon,
    required this.title,
    required this.physicalOn,
    required this.automationEnabled,
    required this.detail,
  });

  final IconData icon;
  final String title;
  final bool physicalOn;
  final bool automationEnabled;
  final String detail;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    final status = context.statusColors;
    final tone = physicalOn ? status.success : colors.onSurfaceVariant;
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(AquaSpacing.md),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Row(
              children: [
                Icon(icon, color: tone, size: 30),
                const SizedBox(width: AquaSpacing.sm),
                Expanded(
                  child: Text(
                    title,
                    style: Theme.of(context).textTheme.titleMedium?.copyWith(
                      fontWeight: FontWeight.w900,
                    ),
                  ),
                ),
                Chip(
                  avatar: Icon(
                    automationEnabled
                        ? Icons.auto_mode_rounded
                        : Icons.block_rounded,
                    size: 16,
                  ),
                  label: Text(automationEnabled ? 'AUTO' : 'WYŁĄCZONE'),
                ),
              ],
            ),
            const SizedBox(height: AquaSpacing.sm),
            Text(
              physicalOn ? 'Wyjście aktywne' : 'Wyjście nieaktywne',
              style: TextStyle(color: tone, fontWeight: FontWeight.w800),
            ),
            const SizedBox(height: AquaSpacing.xxs),
            Text(detail, style: TextStyle(color: colors.onSurfaceVariant)),
            const SizedBox(height: AquaSpacing.md),
            OutlinedButton.icon(
              onPressed: null,
              icon: const Icon(Icons.shield_outlined),
              label: const Text('Sterowane przez automatykę'),
            ),
          ],
        ),
      ),
    );
  }
}

class _OutputDefinition {
  const _OutputDefinition({
    required this.id,
    required this.scheduleChannel,
    required this.action,
    required this.moduleKey,
    required this.label,
    required this.icon,
    this.heater = false,
  });

  final String id;
  final String scheduleChannel;
  final String action;
  final String moduleKey;
  final String label;
  final IconData icon;
  final bool heater;
}
