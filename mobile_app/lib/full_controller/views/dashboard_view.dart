import 'package:flutter/material.dart';

import '../../design_system.dart';
import '../command_center_models.dart';
import '../controller_session.dart';
import '../data_access.dart';
import '../widgets.dart';

class DashboardView extends StatelessWidget {
  const DashboardView({
    super.key,
    required this.session,
    required this.onOpenCharts,
    this.onOpenControls,
  });

  final ControllerSession session;
  final VoidCallback onOpenCharts;
  final VoidCallback? onOpenControls;

  @override
  Widget build(BuildContext context) {
    final status = session.status;
    final model = CommandCenterModel.fromStatus(
      status,
      session.sessionKind,
      connected: session.connectionHealth.isOnline,
    );
    final temperature = model.sensor(CommandCenterSensorKind.temperature);
    final config = status.section('config');
    final network = status.section('network');
    final system = status.section('system');
    final alarms = model.activeAlarms;

    return ControllerPageBody(
      maxWidth: 1180,
      padding: const EdgeInsets.fromLTRB(14, 16, 14, 28),
      onRefresh: () => session.refresh(includeHistory: true),
      children: [
        _SafetyHero(model: model, session: session),
        const SizedBox(height: AquaSpacing.md),
        LayoutBuilder(
          builder: (context, constraints) {
            final twoColumns =
                constraints.maxWidth >= 820 &&
                MediaQuery.textScalerOf(context).scale(1) <= 1.35;
            final primary = _TemperatureOverview(
              sensor: temperature,
              target: config.number('target_temp', 25),
              hysteresis: config.number('temp_hysteresis', 0.5),
              heater: model.output(CommandCenterOutputKind.heater),
              onOpenCharts: onOpenCharts,
            );
            final operations = _OperationsOverview(
              event: model.nextScheduleEvent,
              rssi: network.integer('rssi', -130),
              uptime: formatUptime(system.integer('uptime')),
              freeHeap: formatBytes(
                system.integer('freeHeap', status.integer('heap_free')),
              ),
              onOpenControls: onOpenControls,
            );
            if (!twoColumns) {
              return Column(
                children: [
                  primary,
                  const SizedBox(height: AquaSpacing.md),
                  operations,
                ],
              );
            }
            return Row(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Expanded(flex: 3, child: primary),
                const SizedBox(width: AquaSpacing.md),
                Expanded(flex: 2, child: operations),
              ],
            );
          },
        ),
        const SizedBox(height: AquaSpacing.lg),
        SectionHeader(
          title: 'Telemetria na żywo',
          description:
              'Ostatni potwierdzony pakiet · '
              '${session.connectionHealth.ageLabel(DateTime.now())}',
          trailing: TextButton.icon(
            onPressed: onOpenCharts,
            icon: const Icon(Icons.show_chart_rounded),
            label: const Text('Trendy'),
          ),
        ),
        ResponsiveGrid(
          minimumChildWidth: 190,
          spacing: AquaSpacing.sm,
          children: [
            for (final sensor in model.sensors.where(
              (item) =>
                  item.kind != CommandCenterSensorKind.temperature &&
                  item.kind != CommandCenterSensorKind.supplyVoltage &&
                  item.kind != CommandCenterSensorKind.ioBus,
            ))
              _SensorCard(sensor: sensor),
          ],
        ),
        const SizedBox(height: AquaSpacing.lg),
        const SectionHeader(
          title: 'Urządzenia wykonawcze',
          description:
              'Pierwsza linia pokazuje faktyczny stan wyjścia, druga aktywny tryb sterowania.',
        ),
        ResponsiveGrid(
          minimumChildWidth: 168,
          spacing: AquaSpacing.sm,
          children: [
            for (final output in model.outputs)
              _OutputStatusCard(output: output),
          ],
        ),
        if (alarms.isNotEmpty) ...[
          const SizedBox(height: AquaSpacing.lg),
          SectionHeader(
            title: 'Aktywne alarmy',
            description:
                '${alarms.length} ${alarms.length == 1 ? "zdarzenie wymaga" : "zdarzenia wymagają"} reakcji operatora.',
          ),
          Card(
            clipBehavior: Clip.antiAlias,
            child: Column(
              children: [
                for (var index = 0; index < alarms.length; index++) ...[
                  _AlarmRow(alarm: alarms[index]),
                  if (index != alarms.length - 1) const Divider(height: 1),
                ],
              ],
            ),
          ),
        ],
        if (session.isDevelopment) ...[
          const SizedBox(height: AquaSpacing.md),
          const StatusBanner(
            icon: Icons.science_rounded,
            title: 'Środowisko symulacyjne',
            message:
                'Dane i polecenia pozostają w pamięci telefonu. Fizyczne '
                'wyjścia sterownika nie są aktywowane.',
            isError: false,
          ),
        ],
      ],
    );
  }
}

class _SafetyHero extends StatelessWidget {
  const _SafetyHero({required this.model, required this.session});

  final CommandCenterModel model;
  final ControllerSession session;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    final status = context.statusColors;
    final (tone, container, foreground, icon) = switch (model.safety.state) {
      CommandCenterSafetyState.ok => (
        status.success,
        status.successContainer,
        status.onSuccessContainer,
        Icons.verified_user_rounded,
      ),
      CommandCenterSafetyState.warning => (
        status.warning,
        status.warningContainer,
        status.onWarningContainer,
        Icons.warning_amber_rounded,
      ),
      CommandCenterSafetyState.critical => (
        colors.error,
        colors.errorContainer,
        colors.onErrorContainer,
        Icons.crisis_alert_rounded,
      ),
      CommandCenterSafetyState.offline => (
        colors.outline,
        colors.surfaceContainerHighest,
        colors.onSurface,
        Icons.cloud_off_rounded,
      ),
      CommandCenterSafetyState.service => (
        status.info,
        status.infoContainer,
        status.onInfoContainer,
        Icons.build_circle_rounded,
      ),
    };

    return Semantics(
      container: true,
      label:
          '${model.safety.title}. ${model.safety.message}. '
          'Transport ${model.capabilities.label}.',
      child: DecoratedBox(
        decoration: BoxDecoration(
          color: container,
          borderRadius: BorderRadius.circular(AquaRadius.hero),
          border: Border.all(color: tone.withValues(alpha: 0.7)),
        ),
        child: Padding(
          padding: const EdgeInsets.all(AquaSpacing.lg),
          child: LayoutBuilder(
            builder: (context, constraints) {
              final compact =
                  constraints.maxWidth < 620 ||
                  MediaQuery.textScalerOf(context).scale(1) > 1.35;
              final summary = Row(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Container(
                    width: 52,
                    height: 52,
                    decoration: BoxDecoration(
                      color: tone.withValues(alpha: 0.16),
                      borderRadius: BorderRadius.circular(AquaRadius.control),
                    ),
                    child: Icon(icon, color: tone, size: 30),
                  ),
                  const SizedBox(width: AquaSpacing.md),
                  Expanded(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(
                          model.safety.title,
                          style: Theme.of(context).textTheme.headlineSmall
                              ?.copyWith(
                                color: foreground,
                                fontWeight: FontWeight.w900,
                              ),
                        ),
                        const SizedBox(height: AquaSpacing.xxs),
                        Text(
                          model.safety.message,
                          style: Theme.of(context).textTheme.bodyMedium
                              ?.copyWith(color: foreground, height: 1.4),
                        ),
                      ],
                    ),
                  ),
                ],
              );
              final metrics = Wrap(
                spacing: AquaSpacing.xs,
                runSpacing: AquaSpacing.xs,
                children: [
                  _HeroBadge(
                    icon: Icons.cable_rounded,
                    label: model.capabilities.label,
                    foreground: foreground,
                  ),
                  _HeroBadge(
                    icon: Icons.notifications_active_outlined,
                    label: model.activeAlarms.isEmpty
                        ? 'Brak alarmów'
                        : '${model.activeAlarms.length} alarmów',
                    foreground: foreground,
                  ),
                  _HeroBadge(
                    icon: session.isAdmin
                        ? Icons.lock_open_rounded
                        : Icons.lock_outline_rounded,
                    label: session.isAdmin ? 'Administrator' : 'Podgląd',
                    foreground: foreground,
                  ),
                ],
              );
              if (compact) {
                return Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    summary,
                    const SizedBox(height: AquaSpacing.md),
                    metrics,
                  ],
                );
              }
              return Row(
                children: [
                  Expanded(child: summary),
                  const SizedBox(width: AquaSpacing.lg),
                  Flexible(child: metrics),
                ],
              );
            },
          ),
        ),
      ),
    );
  }
}

class _HeroBadge extends StatelessWidget {
  const _HeroBadge({
    required this.icon,
    required this.label,
    required this.foreground,
  });

  final IconData icon;
  final String label;
  final Color foreground;

  @override
  Widget build(BuildContext context) {
    final largeText = MediaQuery.textScalerOf(context).scale(1) > 1.45;
    return Container(
      width: largeText ? double.infinity : null,
      decoration: BoxDecoration(
        color: foreground.withValues(alpha: 0.08),
        borderRadius: BorderRadius.circular(AquaRadius.control),
        border: Border.all(color: foreground.withValues(alpha: 0.22)),
      ),
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 7),
        child: Row(
          mainAxisSize: largeText ? MainAxisSize.max : MainAxisSize.min,
          children: [
            Icon(icon, size: 16, color: foreground),
            const SizedBox(width: 6),
            if (largeText)
              Expanded(
                child: Text(
                  label,
                  style: TextStyle(
                    color: foreground,
                    fontWeight: FontWeight.w800,
                  ),
                ),
              )
            else
              Flexible(
                child: Text(
                  label,
                  style: TextStyle(
                    color: foreground,
                    fontWeight: FontWeight.w800,
                  ),
                ),
              ),
          ],
        ),
      ),
    );
  }
}

class _TemperatureOverview extends StatelessWidget {
  const _TemperatureOverview({
    required this.sensor,
    required this.target,
    required this.hysteresis,
    required this.heater,
    required this.onOpenCharts,
  });

  final CommandCenterSensor sensor;
  final double target;
  final double hysteresis;
  final CommandCenterOutput heater;
  final VoidCallback onOpenCharts;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    final tone = _sensorTone(context, sensor.state);
    final progress = sensor.numericValue == null
        ? 0.0
        : ((sensor.numericValue! - 18) / 12).clamp(0.0, 1.0);
    return Card(
      child: InkWell(
        borderRadius: BorderRadius.circular(AquaRadius.card),
        onTap: onOpenCharts,
        child: Padding(
          padding: const EdgeInsets.all(AquaSpacing.lg),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  Icon(Icons.thermostat_rounded, color: tone),
                  const SizedBox(width: AquaSpacing.xs),
                  Expanded(
                    child: Text(
                      'Temperatura wody',
                      style: Theme.of(context).textTheme.titleLarge?.copyWith(
                        fontWeight: FontWeight.w900,
                      ),
                    ),
                  ),
                  Icon(
                    Icons.arrow_forward_rounded,
                    color: colors.onSurfaceVariant,
                  ),
                ],
              ),
              const SizedBox(height: AquaSpacing.lg),
              Row(
                children: [
                  SizedBox.square(
                    dimension: 96,
                    child: Stack(
                      fit: StackFit.expand,
                      children: [
                        CircularProgressIndicator(
                          value: sensor.valid ? progress : null,
                          strokeWidth: 9,
                          strokeCap: StrokeCap.round,
                          color: tone,
                          backgroundColor: colors.surfaceContainerHighest,
                        ),
                        Center(
                          child: Icon(
                            heater.isEnergized
                                ? Icons.local_fire_department_rounded
                                : Icons.water_drop_outlined,
                            color: heater.isEnergized
                                ? colors.error
                                : colors.primary,
                            size: 32,
                          ),
                        ),
                      ],
                    ),
                  ),
                  const SizedBox(width: AquaSpacing.lg),
                  Expanded(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text.rich(
                          TextSpan(
                            style: Theme.of(context).textTheme.displaySmall
                                ?.copyWith(
                                  color: tone,
                                  fontWeight: FontWeight.w900,
                                  letterSpacing: -1.2,
                                ),
                            children: [
                              TextSpan(text: sensor.displayValue),
                              if (sensor.unit != null)
                                TextSpan(
                                  text: ' ${sensor.unit}',
                                  style: Theme.of(context).textTheme.titleLarge,
                                ),
                            ],
                          ),
                        ),
                        const SizedBox(height: AquaSpacing.xs),
                        Text(
                          'Cel ${target.toStringAsFixed(1)} °C  ·  '
                          'histereza ±${hysteresis.toStringAsFixed(1)} °C',
                          style: TextStyle(color: colors.onSurfaceVariant),
                        ),
                        const SizedBox(height: AquaSpacing.xs),
                        Text(
                          heater.controlMode ==
                                  CommandCenterControlMode.disabled
                              ? 'Termostat wyłączony'
                              : heater.isEnergized
                              ? 'Grzałka pracuje'
                              : 'Termostat czuwa',
                          style: const TextStyle(fontWeight: FontWeight.w800),
                        ),
                      ],
                    ),
                  ),
                ],
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _OperationsOverview extends StatelessWidget {
  const _OperationsOverview({
    required this.event,
    required this.rssi,
    required this.uptime,
    required this.freeHeap,
    required this.onOpenControls,
  });

  final CommandCenterScheduleEvent? event;
  final int rssi;
  final String uptime;
  final String freeHeap;
  final VoidCallback? onOpenControls;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(AquaSpacing.lg),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Row(
              children: [
                Icon(Icons.upcoming_rounded, color: colors.secondary),
                const SizedBox(width: AquaSpacing.xs),
                Expanded(
                  child: Text(
                    'Teraz i następne',
                    style: Theme.of(context).textTheme.titleLarge?.copyWith(
                      fontWeight: FontWeight.w900,
                    ),
                  ),
                ),
              ],
            ),
            const SizedBox(height: AquaSpacing.md),
            if (event == null)
              Text(
                'Brak aktywnych zdarzeń harmonogramu.',
                style: TextStyle(color: colors.onSurfaceVariant),
              )
            else ...[
              Text(
                event!.label,
                style: Theme.of(
                  context,
                ).textTheme.titleMedium?.copyWith(fontWeight: FontWeight.w900),
              ),
              const SizedBox(height: AquaSpacing.xxs),
              Text(
                '${_clock(event!.scheduledAt)} · za ${_duration(event!.timeUntil)}',
                style: TextStyle(
                  color: colors.secondary,
                  fontWeight: FontWeight.w800,
                ),
              ),
            ],
            const SizedBox(height: AquaSpacing.md),
            Divider(color: colors.outlineVariant),
            const SizedBox(height: AquaSpacing.sm),
            _OperationMetric(
              icon: _signalIcon(rssi),
              label: 'Sygnał Wi‑Fi',
              value: rssi <= -130 ? 'Brak danych' : '$rssi dBm',
            ),
            const SizedBox(height: AquaSpacing.sm),
            _OperationMetric(
              icon: Icons.schedule_rounded,
              label: 'Czas pracy',
              value: uptime,
            ),
            const SizedBox(height: AquaSpacing.sm),
            _OperationMetric(
              icon: Icons.memory_rounded,
              label: 'Wolna pamięć',
              value: freeHeap,
            ),
            if (onOpenControls != null) ...[
              const SizedBox(height: AquaSpacing.md),
              OutlinedButton.icon(
                onPressed: onOpenControls,
                icon: const Icon(Icons.tune_rounded),
                label: const Text('Otwórz sterowanie'),
              ),
            ],
          ],
        ),
      ),
    );
  }

  static String _clock(DateTime value) =>
      '${value.hour.toString().padLeft(2, '0')}:'
      '${value.minute.toString().padLeft(2, '0')}';

  static String _duration(Duration value) {
    if (value.inMinutes < 1) return 'mniej niż minutę';
    if (value.inHours < 1) return '${value.inMinutes} min';
    final minutes = value.inMinutes.remainder(60);
    return minutes == 0
        ? '${value.inHours} h'
        : '${value.inHours} h $minutes min';
  }

  static IconData _signalIcon(int rssi) {
    if (rssi >= -55) return Icons.signal_wifi_4_bar_rounded;
    if (rssi >= -67) return Icons.network_wifi_3_bar_rounded;
    if (rssi >= -75) return Icons.network_wifi_2_bar_rounded;
    if (rssi > -130) return Icons.network_wifi_1_bar_rounded;
    return Icons.signal_wifi_off_rounded;
  }
}

class _OperationMetric extends StatelessWidget {
  const _OperationMetric({
    required this.icon,
    required this.label,
    required this.value,
  });

  final IconData icon;
  final String label;
  final String value;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    final labelWidget = Row(
      children: [
        Icon(icon, size: 19, color: colors.primary),
        const SizedBox(width: AquaSpacing.xs),
        Expanded(
          child: Text(label, style: TextStyle(color: colors.onSurfaceVariant)),
        ),
      ],
    );
    return LayoutBuilder(
      builder: (context, constraints) {
        final textScale = MediaQuery.textScalerOf(context).scale(1);
        if (constraints.maxWidth < 260 || textScale > 1.45) {
          return Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              labelWidget,
              const SizedBox(height: AquaSpacing.xxs),
              Padding(
                padding: const EdgeInsets.only(left: 27),
                child: Text(
                  value,
                  style: const TextStyle(fontWeight: FontWeight.w800),
                ),
              ),
            ],
          );
        }
        return Row(
          children: [
            Expanded(child: labelWidget),
            const SizedBox(width: AquaSpacing.xs),
            Flexible(
              child: Text(
                value,
                textAlign: TextAlign.end,
                style: const TextStyle(fontWeight: FontWeight.w800),
              ),
            ),
          ],
        );
      },
    );
  }
}

class _SensorCard extends StatelessWidget {
  const _SensorCard({required this.sensor});

  final CommandCenterSensor sensor;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    final tone = _sensorTone(context, sensor.state);
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(AquaSpacing.md),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Icon(_sensorIcon(sensor.kind), color: tone, size: 21),
                const SizedBox(width: AquaSpacing.xs),
                Expanded(
                  child: Text(
                    sensor.label,
                    maxLines: 2,
                    overflow: TextOverflow.ellipsis,
                    style: TextStyle(color: colors.onSurfaceVariant),
                  ),
                ),
              ],
            ),
            const SizedBox(height: AquaSpacing.md),
            Text.rich(
              TextSpan(
                style: Theme.of(context).textTheme.headlineSmall?.copyWith(
                  color: tone,
                  fontWeight: FontWeight.w900,
                ),
                children: [
                  TextSpan(text: sensor.displayValue),
                  if (sensor.unit != null)
                    TextSpan(
                      text: ' ${sensor.unit}',
                      style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                        color: colors.onSurfaceVariant,
                      ),
                    ),
                ],
              ),
            ),
            const SizedBox(height: AquaSpacing.xxs),
            Text(
              sensor.detail ?? _sensorStateLabel(sensor.state),
              maxLines: 2,
              overflow: TextOverflow.ellipsis,
              style: Theme.of(
                context,
              ).textTheme.bodySmall?.copyWith(color: colors.onSurfaceVariant),
            ),
          ],
        ),
      ),
    );
  }
}

class _OutputStatusCard extends StatelessWidget {
  const _OutputStatusCard({required this.output});

  final CommandCenterOutput output;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    final status = context.statusColors;
    final active = output.physicalState == CommandCenterPhysicalState.on;
    final unknown = output.physicalState == CommandCenterPhysicalState.unknown;
    final tone = unknown
        ? status.warning
        : active
        ? status.success
        : colors.onSurfaceVariant;
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(AquaSpacing.md),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Icon(_outputIcon(output.kind), color: tone),
                const Spacer(),
                Container(
                  width: 9,
                  height: 9,
                  decoration: BoxDecoration(
                    color: tone,
                    shape: BoxShape.circle,
                  ),
                ),
              ],
            ),
            const SizedBox(height: AquaSpacing.sm),
            Text(
              output.label,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: const TextStyle(fontWeight: FontWeight.w900),
            ),
            const SizedBox(height: AquaSpacing.xs),
            Text(
              switch (output.physicalState) {
                CommandCenterPhysicalState.on => 'WYJŚCIE ON',
                CommandCenterPhysicalState.off => 'WYJŚCIE OFF',
                CommandCenterPhysicalState.unknown => 'STAN NIEZNANY',
              },
              style: Theme.of(context).textTheme.labelSmall?.copyWith(
                color: tone,
                fontWeight: FontWeight.w900,
                letterSpacing: 0.45,
              ),
            ),
            const SizedBox(height: AquaSpacing.xxs),
            Text(
              _controlModeLabel(output.controlMode),
              style: Theme.of(
                context,
              ).textTheme.bodySmall?.copyWith(color: colors.onSurfaceVariant),
            ),
          ],
        ),
      ),
    );
  }
}

class _AlarmRow extends StatelessWidget {
  const _AlarmRow({required this.alarm});

  final CommandCenterAlarm alarm;

  @override
  Widget build(BuildContext context) {
    final critical = alarm.severity == CommandCenterAlarmSeverity.critical;
    final tone = critical
        ? Theme.of(context).colorScheme.error
        : context.statusColors.warning;
    return ListTile(
      contentPadding: const EdgeInsets.symmetric(
        horizontal: AquaSpacing.md,
        vertical: AquaSpacing.xs,
      ),
      leading: Icon(
        critical ? Icons.error_rounded : Icons.warning_amber_rounded,
        color: tone,
      ),
      title: Text(
        alarm.title,
        style: const TextStyle(fontWeight: FontWeight.w900),
      ),
      subtitle: Text(alarm.message),
    );
  }
}

Color _sensorTone(BuildContext context, CommandCenterSensorState state) {
  final colors = Theme.of(context).colorScheme;
  final status = context.statusColors;
  return switch (state) {
    CommandCenterSensorState.ok => status.success,
    CommandCenterSensorState.warning => status.warning,
    CommandCenterSensorState.critical => colors.error,
    CommandCenterSensorState.unavailable => colors.outline,
    CommandCenterSensorState.disabled => colors.onSurfaceVariant,
  };
}

String _sensorStateLabel(CommandCenterSensorState state) => switch (state) {
  CommandCenterSensorState.ok => 'Pomiar prawidłowy',
  CommandCenterSensorState.warning => 'Wymaga uwagi',
  CommandCenterSensorState.critical => 'Alarm krytyczny',
  CommandCenterSensorState.unavailable => 'Brak wiarygodnego pomiaru',
  CommandCenterSensorState.disabled => 'Czujnik wyłączony',
};

IconData _sensorIcon(CommandCenterSensorKind kind) => switch (kind) {
  CommandCenterSensorKind.temperature => Icons.thermostat_rounded,
  CommandCenterSensorKind.ph => Icons.science_rounded,
  CommandCenterSensorKind.conductivity => Icons.bolt_rounded,
  CommandCenterSensorKind.ambientLight => Icons.light_mode_rounded,
  CommandCenterSensorKind.waterLevel => Icons.water_drop_rounded,
  CommandCenterSensorKind.leak => Icons.water_damage_rounded,
  CommandCenterSensorKind.flow => Icons.waves_rounded,
  CommandCenterSensorKind.supplyVoltage => Icons.power_rounded,
  CommandCenterSensorKind.ioBus => Icons.memory_rounded,
};

IconData _outputIcon(CommandCenterOutputKind kind) => switch (kind) {
  CommandCenterOutputKind.light1 => Icons.lightbulb_rounded,
  CommandCenterOutputKind.light2 => Icons.wb_twilight_rounded,
  CommandCenterOutputKind.filter => Icons.filter_alt_rounded,
  CommandCenterOutputKind.heater => Icons.thermostat_rounded,
  CommandCenterOutputKind.co2 => Icons.bubble_chart_rounded,
  CommandCenterOutputKind.aeration => Icons.air_rounded,
  CommandCenterOutputKind.waterDosing => Icons.water_drop_rounded,
  CommandCenterOutputKind.feeder => Icons.set_meal_rounded,
};

String _controlModeLabel(CommandCenterControlMode mode) => switch (mode) {
  CommandCenterControlMode.auto => 'Tryb AUTO',
  CommandCenterControlMode.forcedOn => 'Wymuszone ON',
  CommandCenterControlMode.forcedOff => 'Wymuszone OFF',
  CommandCenterControlMode.disabled => 'Automatyka wyłączona',
};
