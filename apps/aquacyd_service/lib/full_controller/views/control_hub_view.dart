import 'dart:async';

import 'package:flutter/material.dart';

import '../../design_system.dart';
import '../controller_api.dart';
import '../controller_session.dart';
import '../data_access.dart';
import '../schedule_control.dart';
import '../widgets.dart';

typedef RunControlHubAction =
    Future<ControllerActionResult> Function(
      String name, {
      Map<String, Object?> payload,
      String? confirmation,
      bool refreshAfter,
    });

class ControlHubView extends StatefulWidget {
  const ControlHubView({
    super.key,
    required this.session,
    required this.runAction,
    required this.ensureAdmin,
  });

  final ControllerSession session;
  final RunControlHubAction runAction;
  final Future<bool> Function() ensureAdmin;

  @override
  State<ControlHubView> createState() => _ControlHubViewState();
}

class _ControlHubViewState extends State<ControlHubView> {
  final Set<String> _busyOutputs = {};
  final Set<String> _busyModes = {};
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

  Future<void> _setTimedOverride(
    _OutputDefinition output,
    bool physicalOn,
  ) async {
    if (_busyOutputs.contains(output.id)) return;
    final selection = await showModalBottomSheet<_TimedOverrideChoice>(
      context: context,
      showDragHandle: true,
      isScrollControlled: true,
      builder: (context) => _TimedOverrideSheet(
        outputLabel: output.label,
        initialState: !physicalOn,
      ),
    );
    if (selection == null || !mounted) return;
    setState(() => _busyOutputs.add(output.id));
    try {
      await widget.runAction(
        'set_timed_override',
        payload: {
          'target': output.protocolTarget,
          'state': selection.enabled,
          'durationSec': selection.duration.inSeconds,
        },
        confirmation:
            'Ustawić „${output.label}” na '
            '${selection.enabled ? "ON" : "OFF"} przez '
            '${_formatDuration(selection.duration)}? Po tym czasie sterownik '
            'automatycznie wróci do wcześniejszego trybu.',
      );
    } on ControllerApiException {
      // Powłoka prezentuje błąd i nie zmienia lokalnie stanu potwierdzonego.
    } finally {
      if (mounted) setState(() => _busyOutputs.remove(output.id));
    }
  }

  Future<void> _clearTimedOverride(_OutputDefinition output) async {
    if (_busyOutputs.contains(output.id)) return;
    setState(() => _busyOutputs.add(output.id));
    try {
      await widget.runAction(
        'clear_timed_override',
        payload: {'target': output.protocolTarget},
        confirmation:
            'Zakończyć sterowanie czasowe dla „${output.label}” i natychmiast '
            'przywrócić wcześniejszy tryb?',
      );
    } on ControllerApiException {
      // Powłoka prezentuje błąd i zachowuje stan potwierdzony przez sterownik.
    } finally {
      if (mounted) setState(() => _busyOutputs.remove(output.id));
    }
  }

  Future<void> _setLightProfile(
    _OutputDefinition output,
    String profile,
  ) async {
    final target = output.aquaelProfileTarget;
    if (target == null || _busyOutputs.contains(output.id)) return;
    setState(() => _busyOutputs.add(output.id));
    try {
      await widget.runAction(
        'set_light_profile',
        payload: {'target': target, 'profile': profile},
        confirmation:
            'Ustawić świetlówkę ${target == "front" ? "przednią" : "tylną"} '
            'w tryb ${profile.toUpperCase()}? Sterownik wykona bezpieczną '
            'sekwencję zasilania Aquael; podczas zmiany światło na chwilę '
            'zgaśnie.',
      );
    } on ControllerApiException {
      // Powłoka prezentuje wynik sekwencji potwierdzony przez sterownik.
    } finally {
      if (mounted) setState(() => _busyOutputs.remove(output.id));
    }
  }

  Future<void> _toggleProcessMode({
    required String id,
    required bool active,
  }) async {
    if (_busyModes.contains(id)) return;
    setState(() => _busyModes.add(id));
    final feedingMode = id == 'feeding';
    final action = active
        ? feedingMode
              ? 'stop_feeding_mode'
              : 'stop_service_mode'
        : feedingMode
        ? 'start_feeding_mode'
        : 'start_service_mode';
    final duration = feedingMode
        ? const Duration(minutes: 10)
        : const Duration(minutes: 30);
    try {
      await widget.runAction(
        action,
        payload: active
            ? const {}
            : {
                'durationSec': duration.inSeconds,
                if (feedingMode) 'dispense': false,
              },
        confirmation: active
            ? 'Zakończyć ${feedingMode ? "tryb karmienia" : "tryb serwisowy"} '
                  'przed upływem ustawionego czasu?'
            : feedingMode
            ? 'Włączyć tryb karmienia na 10 minut? Sterownik bezpiecznie '
                  'wstrzyma skonfigurowane urządzenia; pokarm podasz osobnym '
                  'przyciskiem.'
            : 'Włączyć tryb serwisowy na 30 minut? Sterownik zastosuje '
                  'bezpieczną sekwencję wyjść i wróci automatycznie do pracy.',
      );
    } on ControllerApiException {
      // Powłoka pokazuje komunikat zwrócony przez warstwę komunikacyjną.
    } finally {
      if (mounted) setState(() => _busyModes.remove(id));
    }
  }

  @override
  Widget build(BuildContext context) {
    final status = widget.session.status;
    final modules = status.section('modules');
    final sensors = status.section('sensors');
    final feeding = status.section('feeding');
    final controlState = status.section('controlState');
    final lights = status.section('lights');
    final feedingMode = controlState.section('feedingMode');
    final serviceMode = controlState.section('serviceMode');
    final timedOverrides = controlState.list('overrides');
    final hasStoredData = widget.session.hasStatusData;
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
        label: 'Świetlówka przednia',
        icon: Icons.lightbulb_rounded,
        aquaelProfileTarget: 'front',
      ),
      _OutputDefinition(
        id: 'light2',
        scheduleChannel: 'light2',
        action: 'set_light2',
        moduleKey: 'plant_light_on',
        label: 'Świetlówka tylna',
        icon: Icons.wb_twilight_rounded,
        aquaelProfileTarget: 'rear',
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
        protocolTarget: 'aeration',
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
          title: 'Sterowanie',
          description: 'Nakarm ryby lub zmień tryb urządzenia.',
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
          minimumChildWidth: 300,
          spacing: AquaSpacing.sm,
          children: [
            _FeederCard(
              key: const Key('manual-feed-card'),
              active: feeding.flag('active'),
              busy: _feeding,
              enabled: commandReady,
              dataAvailable: hasStoredData,
              lastResult: feeding.text('lastResult', 'brak danych'),
              onFeed: _feed,
            ),
          ],
        ),
        if (widget.session.supportsFeature('feedingMode') ||
            widget.session.supportsFeature('serviceMode')) ...[
          const SizedBox(height: AquaSpacing.lg),
          const SectionHeader(
            title: 'Tryby czasowe',
            description:
                'Bezpieczne procedury, które zakończą się automatycznie.',
          ),
          ResponsiveGrid(
            minimumChildWidth: 300,
            spacing: AquaSpacing.sm,
            children: [
              if (widget.session.supportsFeature('feedingMode'))
                _TimedProcessCard(
                  key: const Key('feeding-mode-card'),
                  icon: Icons.restaurant_rounded,
                  title: 'Tryb karmienia',
                  description:
                      'Wstrzymuje skonfigurowane urządzenia na 10 minut.',
                  active: feedingMode.flag('active'),
                  remainingSeconds: feedingMode.integer('remainingSec'),
                  busy: _busyModes.contains('feeding'),
                  enabled: commandReady,
                  onToggle: () => unawaited(
                    _toggleProcessMode(
                      id: 'feeding',
                      active: feedingMode.flag('active'),
                    ),
                  ),
                ),
              if (widget.session.supportsFeature('serviceMode'))
                _TimedProcessCard(
                  key: const Key('service-mode-card'),
                  icon: Icons.handyman_rounded,
                  title: 'Tryb serwisowy',
                  description:
                      'Bezpieczny stan wyjść podczas prac przy akwarium.',
                  active: serviceMode.flag('active'),
                  remainingSeconds: serviceMode.integer('remainingSec'),
                  busy: _busyModes.contains('service'),
                  enabled: commandReady,
                  onToggle: () => unawaited(
                    _toggleProcessMode(
                      id: 'service',
                      active: serviceMode.flag('active'),
                    ),
                  ),
                ),
            ],
          ),
        ],
        const SizedBox(height: AquaSpacing.lg),
        const SectionHeader(
          title: 'Urządzenia',
          description: 'Dotknij urządzenia, aby wybrać AUTO, ON lub OFF.',
        ),
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
                dataAvailable: hasStoredData,
                legacyBluetooth: widget.session.isLegacyBluetooth,
                timedOverrideSupported: widget.session.supportsFeature(
                  'timedOverrides',
                ),
                timedOverride: _findTimedOverride(
                  timedOverrides,
                  output.protocolTarget,
                ),
                aquaelProfileSupported:
                    output.aquaelProfileTarget != null &&
                    widget.session.supportsFeature('aquaelLightProfiles'),
                lightState: _readLightState(
                  lights,
                  output.aquaelProfileTarget,
                  output.id,
                ),
                onChanged: (current, selected) =>
                    unawaited(_setOutputMode(output, current, selected)),
                onSetTimedOverride: () => unawaited(
                  _setTimedOverride(output, modules.flag(output.moduleKey)),
                ),
                onClearTimedOverride: () =>
                    unawaited(_clearTimedOverride(output)),
                onLightProfileChanged: (profile) =>
                    unawaited(_setLightProfile(output, profile)),
              ),
          ],
        ),
        const SizedBox(height: AquaSpacing.lg),
        const SectionHeader(
          title: 'Automatyka w tle',
          description: 'Stan dozowania CO₂ i automatycznej dolewki.',
        ),
        ResponsiveGrid(
          minimumChildWidth: 300,
          spacing: AquaSpacing.sm,
          children: [
            _ReadOnlyActuatorCard(
              icon: Icons.bubble_chart_rounded,
              title: 'Dozowanie CO₂',
              physicalOn: modules.flag('co2_on'),
              automationEnabled: modules.flag('co2_enabled'),
              dataAvailable: hasStoredData,
              detail: sensors.flag('ph_valid')
                  ? 'Aktualne pH ${sensors.number('ph').toStringAsFixed(2)}'
                  : 'Brak wiarygodnego pomiaru pH',
            ),
            _ReadOnlyActuatorCard(
              icon: Icons.water_drop_rounded,
              title: 'Dolewka ATO',
              physicalOn: status.section('water').flag('active'),
              automationEnabled: modules.flag('water_level_enabled'),
              dataAvailable: hasStoredData,
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
    required this.dataAvailable,
    required this.legacyBluetooth,
    required this.timedOverrideSupported,
    required this.timedOverride,
    required this.aquaelProfileSupported,
    required this.lightState,
    required this.onChanged,
    required this.onSetTimedOverride,
    required this.onClearTimedOverride,
    required this.onLightProfileChanged,
  });

  final _OutputDefinition definition;
  final bool physicalOn;
  final OutputScheduleState schedule;
  final bool busy;
  final bool enabled;
  final bool dataAvailable;
  final bool legacyBluetooth;
  final bool timedOverrideSupported;
  final JsonMap? timedOverride;
  final bool aquaelProfileSupported;
  final _LightState? lightState;
  final void Function(OutputControlMode current, OutputControlMode selected)
  onChanged;
  final VoidCallback onSetTimedOverride;
  final VoidCallback onClearTimedOverride;
  final ValueChanged<String> onLightProfileChanged;

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
      clipBehavior: Clip.antiAlias,
      child: ExpansionTile(
        key: PageStorageKey<String>('output-card-${definition.id}'),
        minTileHeight: 72,
        tilePadding: const EdgeInsets.symmetric(
          horizontal: AquaSpacing.md,
          vertical: AquaSpacing.xxs,
        ),
        childrenPadding: const EdgeInsets.fromLTRB(
          AquaSpacing.md,
          0,
          AquaSpacing.md,
          AquaSpacing.md,
        ),
        leading: Container(
          width: 48,
          height: 48,
          decoration: BoxDecoration(
            color: activeTone.withValues(alpha: 0.14),
            borderRadius: BorderRadius.circular(AquaRadius.control),
          ),
          child: Icon(definition.icon, color: activeTone),
        ),
        title: Row(
          children: [
            Expanded(
              child: Text(
                definition.label,
                style: Theme.of(
                  context,
                ).textTheme.titleMedium?.copyWith(fontWeight: FontWeight.w900),
              ),
            ),
            if (busy) ...[
              const SizedBox(width: AquaSpacing.xs),
              const SizedBox.square(
                dimension: 22,
                child: CircularProgressIndicator(strokeWidth: 2.5),
              ),
            ],
          ],
        ),
        subtitle: Padding(
          padding: const EdgeInsets.only(top: AquaSpacing.xxs),
          child: dataAvailable
              ? Text.rich(
                  TextSpan(
                    children: [
                      TextSpan(
                        text: physicalOn ? 'WYJŚCIE ON' : 'WYJŚCIE OFF',
                        style: TextStyle(
                          color: activeTone,
                          fontWeight: FontWeight.w900,
                        ),
                      ),
                      TextSpan(
                        text:
                            ' · ${schedule.modeLabel} · ${schedule.windowLabel}',
                        style: TextStyle(
                          color: colors.onSurfaceVariant,
                          fontWeight: FontWeight.w600,
                        ),
                      ),
                    ],
                  ),
                  maxLines: 2,
                  overflow: TextOverflow.ellipsis,
                )
              : Text(
                  'Brak zapisanego stanu, trybu i harmonogramu',
                  style: TextStyle(
                    color: colors.onSurfaceVariant,
                    fontWeight: FontWeight.w600,
                  ),
                  maxLines: 2,
                  overflow: TextOverflow.ellipsis,
                ),
        ),
        children: [
          Divider(height: 1, color: colors.outlineVariant),
          const SizedBox(height: AquaSpacing.md),
          Row(
            children: [
              Icon(
                schedule.mode == OutputControlMode.automatic
                    ? Icons.schedule_rounded
                    : Icons.pan_tool_alt_rounded,
                size: 20,
                color: colors.onSurfaceVariant,
              ),
              const SizedBox(width: AquaSpacing.xs),
              Expanded(
                child: Text(
                  'Wybierz tryb sterowania',
                  style: Theme.of(context).textTheme.labelLarge?.copyWith(
                    color: colors.onSurfaceVariant,
                    fontWeight: FontWeight.w700,
                  ),
                ),
              ),
            ],
          ),
          const SizedBox(height: AquaSpacing.sm),
          _ModeSelector(
            key: ValueKey<String>('output-mode-selector-${definition.id}'),
            options: options,
            selected: schedule.mode,
            enabled: selectable,
            dataAvailable: dataAvailable,
            onSelected: (selected) => onChanged(schedule.mode, selected),
          ),
          if (aquaelProfileSupported) ...[
            const SizedBox(height: AquaSpacing.md),
            Divider(height: 1, color: colors.outlineVariant),
            const SizedBox(height: AquaSpacing.md),
            DropdownButtonFormField<String>(
              key: ValueKey<String>(
                'aquael-profile-${definition.aquaelProfileTarget}',
              ),
              isExpanded: true,
              initialValue:
                  lightState?.known == true &&
                      const {
                        'day',
                        'daybreak',
                        'night',
                      }.contains(lightState?.profile)
                  ? lightState?.profile
                  : null,
              decoration: InputDecoration(
                labelText: 'Tryb Aquael Day & Night',
                prefixIcon: lightState?.transitioning == true
                    ? const Padding(
                        padding: EdgeInsets.all(14),
                        child: SizedBox.square(
                          dimension: 18,
                          child: CircularProgressIndicator(strokeWidth: 2),
                        ),
                      )
                    : const Icon(Icons.light_mode_outlined),
                helperText: lightState?.transitioning == true
                    ? 'Trwa bezpieczna sekwencja wyłączenie–włączenie.'
                    : lightState?.known == false
                    ? 'Pierwsza zmiana rozpocznie się kalibracją do trybu DAY.'
                    : 'Krótka przerwa ≤5 s zmienia tryb; przerwa >5 s '
                          'resetuje lampę do DAY.',
              ),
              items: const [
                DropdownMenuItem(
                  value: 'day',
                  child: Text('DAY — pełne światło'),
                ),
                DropdownMenuItem(
                  value: 'daybreak',
                  child: Text('DAYBREAK — 50% + niebieskie'),
                ),
                DropdownMenuItem(
                  value: 'night',
                  child: Text('NIGHT — światło nocne'),
                ),
              ],
              selectedItemBuilder: (context) => const [
                Text('DAY', overflow: TextOverflow.ellipsis),
                Text('DAYBREAK', overflow: TextOverflow.ellipsis),
                Text('NIGHT', overflow: TextOverflow.ellipsis),
              ],
              onChanged: selectable && lightState?.transitioning != true
                  ? (value) {
                      if (value != null && value != lightState?.profile) {
                        onLightProfileChanged(value);
                      }
                    }
                  : null,
            ),
          ],
          if (timedOverrideSupported) ...[
            const SizedBox(height: AquaSpacing.md),
            Divider(height: 1, color: colors.outlineVariant),
            const SizedBox(height: AquaSpacing.md),
            if (timedOverride case final override?)
              Container(
                key: ValueKey<String>('timed-override-active-${definition.id}'),
                padding: const EdgeInsets.all(AquaSpacing.sm),
                decoration: BoxDecoration(
                  color: colors.tertiaryContainer.withValues(alpha: 0.6),
                  borderRadius: BorderRadius.circular(AquaRadius.control),
                ),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    Row(
                      children: [
                        Icon(
                          Icons.timer_rounded,
                          color: colors.onTertiaryContainer,
                        ),
                        const SizedBox(width: AquaSpacing.xs),
                        Expanded(
                          child: Text(
                            'Czasowo ${override.flag('state') ? "ON" : "OFF"} · '
                            '${_formatRemaining(override.integer('remainingSec'))}',
                            style: TextStyle(
                              color: colors.onTertiaryContainer,
                              fontWeight: FontWeight.w900,
                            ),
                          ),
                        ),
                      ],
                    ),
                    const SizedBox(height: AquaSpacing.sm),
                    OutlinedButton.icon(
                      onPressed: selectable ? onClearTimedOverride : null,
                      icon: const Icon(Icons.restore_rounded),
                      label: const Text('Zakończ i przywróć automatykę'),
                    ),
                  ],
                ),
              )
            else
              OutlinedButton.icon(
                key: ValueKey<String>('timed-override-button-${definition.id}'),
                onPressed: selectable ? onSetTimedOverride : null,
                icon: const Icon(Icons.timer_outlined),
                label: const Text('Ustaw ON/OFF na określony czas'),
              ),
          ],
        ],
      ),
    );
  }
}

class _ModeSelector extends StatelessWidget {
  const _ModeSelector({
    super.key,
    required this.options,
    required this.selected,
    required this.enabled,
    required this.dataAvailable,
    required this.onSelected,
  });

  final List<OutputControlMode> options;
  final OutputControlMode selected;
  final bool enabled;
  final bool dataAvailable;
  final ValueChanged<OutputControlMode> onSelected;

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(
      builder: (context, constraints) {
        final largeText = MediaQuery.textScalerOf(context).scale(1) > 1.25;
        if (constraints.maxWidth < 300 || largeText) {
          return DropdownButtonFormField<OutputControlMode>(
            initialValue: dataAvailable && options.contains(selected)
                ? selected
                : null,
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
          style: const ButtonStyle(
            minimumSize: WidgetStatePropertyAll(Size(0, 48)),
            tapTargetSize: MaterialTapTargetSize.padded,
          ),
          segments: [
            for (final option in options)
              ButtonSegment(
                value: option,
                icon: Icon(_modeIcon(option), size: 18),
                label: Text(_shortModeLabel(option)),
              ),
          ],
          selected: dataAvailable && options.contains(selected)
              ? {selected}
              : const {},
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
    super.key,
    required this.active,
    required this.busy,
    required this.enabled,
    required this.dataAvailable,
    required this.lastResult,
    required this.onFeed,
  });

  final bool active;
  final bool busy;
  final bool enabled;
  final bool dataAvailable;
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
                        !dataAvailable
                            ? 'Brak zapisanego stanu'
                            : active
                            ? 'Trwa podawanie pokarmu'
                            : 'Gotowy do dawki',
                        style: TextStyle(color: colors.onSurfaceVariant),
                      ),
                    ],
                  ),
                ),
              ],
            ),
            const SizedBox(height: AquaSpacing.sm),
            Text(
              dataAvailable
                  ? 'Ostatni wynik: $lastResult'
                  : 'Połącz sterownik, aby pobrać stan karmnika.',
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

class _TimedProcessCard extends StatelessWidget {
  const _TimedProcessCard({
    super.key,
    required this.icon,
    required this.title,
    required this.description,
    required this.active,
    required this.remainingSeconds,
    required this.busy,
    required this.enabled,
    required this.onToggle,
  });

  final IconData icon;
  final String title;
  final String description;
  final bool active;
  final int remainingSeconds;
  final bool busy;
  final bool enabled;
  final VoidCallback onToggle;

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
                Container(
                  width: 48,
                  height: 48,
                  decoration: BoxDecoration(
                    color: tone.withValues(alpha: 0.14),
                    borderRadius: BorderRadius.circular(AquaRadius.control),
                  ),
                  child: Icon(icon, color: tone),
                ),
                const SizedBox(width: AquaSpacing.sm),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        title,
                        style: Theme.of(context).textTheme.titleMedium
                            ?.copyWith(fontWeight: FontWeight.w900),
                      ),
                      Text(
                        active
                            ? 'Aktywny · ${_formatRemaining(remainingSeconds)}'
                            : description,
                        style: TextStyle(color: colors.onSurfaceVariant),
                      ),
                    ],
                  ),
                ),
              ],
            ),
            const SizedBox(height: AquaSpacing.md),
            FilledButton.tonalIcon(
              onPressed: enabled && !busy ? onToggle : null,
              icon: busy
                  ? const SizedBox.square(
                      dimension: 18,
                      child: CircularProgressIndicator(strokeWidth: 2),
                    )
                  : Icon(
                      active ? Icons.stop_rounded : Icons.play_arrow_rounded,
                    ),
              label: Text(active ? 'Zakończ teraz' : 'Uruchom'),
            ),
          ],
        ),
      ),
    );
  }
}

class _TimedOverrideSheet extends StatefulWidget {
  const _TimedOverrideSheet({
    required this.outputLabel,
    required this.initialState,
  });

  final String outputLabel;
  final bool initialState;

  @override
  State<_TimedOverrideSheet> createState() => _TimedOverrideSheetState();
}

class _TimedOverrideSheetState extends State<_TimedOverrideSheet> {
  static const _durations = [
    Duration(minutes: 15),
    Duration(hours: 1),
    Duration(hours: 4),
  ];

  late bool _enabled;
  Duration _duration = _durations.first;

  @override
  void initState() {
    super.initState();
    _enabled = widget.initialState;
  }

  @override
  Widget build(BuildContext context) {
    final bottomPadding = MediaQuery.viewPaddingOf(context).bottom;
    return SafeArea(
      top: false,
      child: SingleChildScrollView(
        padding: EdgeInsets.fromLTRB(
          AquaSpacing.lg,
          AquaSpacing.xs,
          AquaSpacing.lg,
          AquaSpacing.lg + bottomPadding,
        ),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Text(
              'Sterowanie czasowe',
              style: Theme.of(
                context,
              ).textTheme.headlineSmall?.copyWith(fontWeight: FontWeight.w900),
            ),
            const SizedBox(height: AquaSpacing.xs),
            Text(
              '${widget.outputLabel} wróci automatycznie do wcześniejszego '
              'trybu po upływie czasu.',
            ),
            const SizedBox(height: AquaSpacing.lg),
            Text(
              'Stan wyjścia',
              style: Theme.of(
                context,
              ).textTheme.titleSmall?.copyWith(fontWeight: FontWeight.w800),
            ),
            const SizedBox(height: AquaSpacing.xs),
            SegmentedButton<bool>(
              segments: const [
                ButtonSegment(
                  value: true,
                  icon: Icon(Icons.power_rounded),
                  label: Text('ON'),
                ),
                ButtonSegment(
                  value: false,
                  icon: Icon(Icons.power_off_rounded),
                  label: Text('OFF'),
                ),
              ],
              selected: {_enabled},
              onSelectionChanged: (selection) {
                setState(() => _enabled = selection.first);
              },
            ),
            const SizedBox(height: AquaSpacing.lg),
            Text(
              'Czas',
              style: Theme.of(
                context,
              ).textTheme.titleSmall?.copyWith(fontWeight: FontWeight.w800),
            ),
            const SizedBox(height: AquaSpacing.xs),
            SegmentedButton<Duration>(
              segments: [
                for (final duration in _durations)
                  ButtonSegment(
                    value: duration,
                    label: Text(_formatDuration(duration)),
                  ),
              ],
              selected: {_duration},
              onSelectionChanged: (selection) {
                setState(() => _duration = selection.first);
              },
            ),
            const SizedBox(height: AquaSpacing.lg),
            FilledButton.icon(
              key: const Key('confirm-timed-override-button'),
              onPressed: () => Navigator.pop(
                context,
                _TimedOverrideChoice(enabled: _enabled, duration: _duration),
              ),
              icon: const Icon(Icons.timer_rounded),
              label: const Text('Ustaw sterowanie czasowe'),
            ),
          ],
        ),
      ),
    );
  }
}

class _TimedOverrideChoice {
  const _TimedOverrideChoice({required this.enabled, required this.duration});

  final bool enabled;
  final Duration duration;
}

class _ReadOnlyActuatorCard extends StatelessWidget {
  const _ReadOnlyActuatorCard({
    required this.icon,
    required this.title,
    required this.physicalOn,
    required this.automationEnabled,
    required this.dataAvailable,
    required this.detail,
  });

  final IconData icon;
  final String title;
  final bool physicalOn;
  final bool automationEnabled;
  final bool dataAvailable;
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
                    !dataAvailable
                        ? Icons.help_outline_rounded
                        : automationEnabled
                        ? Icons.auto_mode_rounded
                        : Icons.block_rounded,
                    size: 16,
                  ),
                  label: Text(
                    !dataAvailable
                        ? 'BRAK DANYCH'
                        : automationEnabled
                        ? 'AUTO'
                        : 'WYŁĄCZONE',
                  ),
                ),
              ],
            ),
            const SizedBox(height: AquaSpacing.sm),
            Text(
              !dataAvailable
                  ? 'Brak zapisanego stanu wyjścia'
                  : physicalOn
                  ? 'Wyjście aktywne'
                  : 'Wyjście nieaktywne',
              style: TextStyle(color: tone, fontWeight: FontWeight.w800),
            ),
            const SizedBox(height: AquaSpacing.xxs),
            Text(detail, style: TextStyle(color: colors.onSurfaceVariant)),
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
    String? protocolTarget,
    this.aquaelProfileTarget,
    this.heater = false,
  }) : protocolTarget = protocolTarget ?? id;

  final String id;
  final String scheduleChannel;
  final String action;
  final String moduleKey;
  final String label;
  final IconData icon;
  final String protocolTarget;
  final String? aquaelProfileTarget;
  final bool heater;
}

class _LightState {
  const _LightState({
    required this.profile,
    required this.known,
    required this.transitioning,
  });

  final String profile;
  final bool known;
  final bool transitioning;
}

JsonMap? _findTimedOverride(List<dynamic> overrides, String target) {
  for (final raw in overrides) {
    final override = jsonMap(raw);
    if (override.text('target') == target) return override;
  }
  return null;
}

_LightState? _readLightState(
  JsonMap lights,
  String? canonicalTarget,
  String compatibilityKey,
) {
  if (canonicalTarget == null) return null;
  final canonical = lights.section(canonicalTarget);
  final fallback = lights.section(compatibilityKey);
  final source = canonical.isNotEmpty ? canonical : fallback;
  if (source.isEmpty) {
    return const _LightState(
      profile: 'day',
      known: false,
      transitioning: false,
    );
  }
  final profile = source.text('profile', 'day').toLowerCase();
  return _LightState(
    profile: profile,
    known: source.flag(
      'known',
      const {'day', 'daybreak', 'night'}.contains(profile),
    ),
    transitioning: source.flag('transitioning'),
  );
}

String _formatDuration(Duration duration) {
  if (duration.inHours >= 1 && duration.inMinutes % 60 == 0) {
    return duration.inHours == 1 ? '1 godz.' : '${duration.inHours} godz.';
  }
  return '${duration.inMinutes} min';
}

String _formatRemaining(int seconds) {
  final safe = seconds.clamp(0, 86400);
  final hours = safe ~/ 3600;
  final minutes = (safe % 3600) ~/ 60;
  final remainder = safe % 60;
  if (hours > 0) {
    return '$hours h ${minutes.toString().padLeft(2, '0')} min';
  }
  return '${minutes.toString().padLeft(2, '0')}:'
      '${remainder.toString().padLeft(2, '0')}';
}
