import 'package:flutter/material.dart';

import '../controller_session.dart';
import '../data_access.dart';
import '../widgets.dart';

class DashboardView extends StatelessWidget {
  const DashboardView({
    super.key,
    required this.session,
    required this.onOpenCharts,
  });

  final ControllerSession session;
  final VoidCallback onOpenCharts;

  @override
  Widget build(BuildContext context) {
    final status = session.status;
    final sensors = status.section('sensors');
    final alarms = status.section('alarms');
    final modules = status.section('modules');
    final config = status.section('config');
    final temperature = status.section('temperature');
    final network = status.section('network');
    final system = status.section('system');
    final colors = Theme.of(context).colorScheme;
    final alarmCount = alarms.integer('activeCount');
    final hasAlarm = alarmCount > 0 || alarms.integer('flags') != 0;

    return ControllerPageBody(
      maxWidth: 820,
      padding: const EdgeInsets.fromLTRB(14, 14, 14, 28),
      onRefresh: () => session.refresh(includeHistory: true),
      children: [
        if (hasAlarm) ...[
          StatusBanner(
            icon: Icons.warning_amber_rounded,
            title: 'System wymaga uwagi',
            message: _alarmDescription(alarms),
            isError: true,
          ),
          const SizedBox(height: 12),
        ],
        RepaintBoundary(
          child: _TemperatureCard(
            valid: sensors.flag('temp_valid'),
            value: sensors.number('temp_c'),
            target: config.number('target_temp', 25),
            hysteresis: config.number('temp_hysteresis', 0.5),
            thermostatEnabled: temperature.integer('heaterMode') == 0,
            heaterOn: modules.flag('heater_on'),
            alarm:
                alarms.flag('temperatureHigh') || alarms.flag('temperatureLow'),
          ),
        ),
        const SizedBox(height: 12),
        RepaintBoundary(
          child: ResponsiveGrid(
            minimumChildWidth: 145,
            spacing: 10,
            children: [
              _CompactMetricCard(
                icon: Icons.science_outlined,
                label: 'pH',
                value: sensors.flag('ph_valid')
                    ? sensors.number('ph').toStringAsFixed(2)
                    : '—',
                detail: sensors.flag('ph_valid')
                    ? alarms.flag('phOutOfRange')
                          ? 'Poza zakresem'
                          : 'Pomiar prawidłowy'
                    : 'Brak pomiaru',
                tone: alarms.flag('phOutOfRange') ? colors.error : null,
              ),
              _CompactMetricCard(
                icon: Icons.bolt_outlined,
                label: 'EC',
                value: sensors.flag('ec_valid')
                    ? sensors.number('ec').toStringAsFixed(0)
                    : '—',
                unit: sensors.flag('ec_valid') ? 'µS/cm' : null,
                detail: modules.flag('ec_enabled')
                    ? 'Czujnik aktywny'
                    : 'Czujnik wyłączony',
              ),
              _CompactMetricCard(
                icon: Icons.light_mode_outlined,
                label: 'Jasność względna',
                value: sensors.flag('ldr_valid')
                    ? '${_ldrPercent(sensors.integer('ldr'))}'
                    : '—',
                unit: sensors.flag('ldr_valid') ? '%' : null,
                detail: sensors.flag('ldr_valid')
                    ? 'ADC ${sensors.integer('ldr').clamp(0, 4095)} / 4095'
                    : 'Brak pomiaru',
              ),
              _CompactMetricCard(
                icon: Icons.waves_rounded,
                label: 'Przepływ',
                value: _flowValue(sensors),
                detail: _flowDetail(sensors),
                tone: _flowTone(context, sensors),
              ),
              _CompactMetricCard(
                icon: Icons.water_drop_outlined,
                label: 'Poziom wody',
                value: !sensors.flag('water_level_valid')
                    ? 'Błąd'
                    : sensors.flag('water_level_high')
                    ? 'OK'
                    : 'Niski',
                detail: sensors.flag('water_level_valid')
                    ? 'Czujnik poziomu'
                    : 'Brak wiarygodnych danych',
                tone: alarms.flag('waterLevelLow') ? colors.error : null,
              ),
              _CompactMetricCard(
                icon: Icons.water_damage_outlined,
                label: 'Wyciek',
                value: !sensors.flag('leak_valid')
                    ? 'Nieznany'
                    : sensors.flag('leak_detected')
                    ? 'Alarm'
                    : 'Brak',
                detail: sensors.flag('leak_valid')
                    ? 'Czujnik wycieku'
                    : 'Brak wiarygodnych danych',
                tone: alarms.flag('leak') ? colors.error : null,
              ),
            ],
          ),
        ),
        const SizedBox(height: 12),
        Card(
          child: Padding(
            padding: const EdgeInsets.all(18),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const Row(
                  children: [
                    Icon(Icons.memory_rounded),
                    SizedBox(width: 10),
                    Expanded(
                      child: Text(
                        'Urządzenie',
                        style: TextStyle(
                          fontSize: 18,
                          fontWeight: FontWeight.w800,
                        ),
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 12),
                _DeviceGrid(
                  entries: [
                    ('Tryb', status.text('mode', session.displayName)),
                    ('Adres IP', network.text('ip', status.text('ip', '—'))),
                    (
                      'Firmware',
                      status.section('firmware').text('version', '—'),
                    ),
                    (
                      'Pamięć',
                      formatBytes(
                        system.integer('freeHeap', status.integer('heap_free')),
                      ),
                    ),
                  ],
                ),
              ],
            ),
          ),
        ),
        const SizedBox(height: 12),
        Card(
          clipBehavior: Clip.antiAlias,
          child: ListTile(
            contentPadding: const EdgeInsets.symmetric(
              horizontal: 18,
              vertical: 8,
            ),
            leading: const Icon(Icons.show_chart_rounded, size: 30),
            title: const Text(
              'Historia i wykresy',
              style: TextStyle(fontWeight: FontWeight.w800),
            ),
            subtitle: const Text('Temperatura, pH, LDR i historia pracy'),
            trailing: const Icon(Icons.chevron_right_rounded),
            onTap: onOpenCharts,
          ),
        ),
        if (session.isDevelopment) ...[
          const SizedBox(height: 12),
          const StatusBanner(
            icon: Icons.science_outlined,
            title: 'Tryb deweloperski',
            message: 'Przekaźniki fizyczne nie są sterowane.',
            isError: false,
          ),
        ],
      ],
    );
  }

  static int _ldrPercent(int raw) =>
      ((raw.clamp(0, 4095) / 4095) * 100).round();

  static String _flowValue(JsonMap sensors) {
    if (!sensors.containsKey('flow_active') ||
        !sensors.containsKey('flow_valid')) {
      return 'Nieznany';
    }
    if (!sensors.flag('flow_valid')) return 'Błąd';
    return sensors.flag('flow_active') ? 'Aktywny' : 'Brak';
  }

  static String _flowDetail(JsonMap sensors) {
    if (!sensors.containsKey('flow_active')) return 'Brak danych';
    if (!sensors.flag('flow_valid')) return 'Błąd czujnika';
    return sensors.flag('flow_active')
        ? 'Wykryto przepływ'
        : 'Nie wykryto przepływu';
  }

  static Color? _flowTone(BuildContext context, JsonMap sensors) {
    if (!sensors.containsKey('flow_valid') || !sensors.flag('flow_valid')) {
      return Theme.of(context).colorScheme.error;
    }
    return null;
  }

  static String _alarmDescription(JsonMap alarms) {
    final active = <String>[
      if (alarms.flag('temperatureHigh')) 'temperatura za wysoka',
      if (alarms.flag('temperatureLow')) 'temperatura za niska',
      if (alarms.flag('phOutOfRange')) 'pH poza zakresem',
      if (alarms.flag('waterLevelLow')) 'niski poziom wody',
      if (alarms.flag('leak')) 'wyciek',
      if (alarms.flag('supplyLow')) 'niskie napięcie',
    ];
    return active.isEmpty
        ? 'Aktywne flagi: ${alarms.integer('flags')}'
        : active.join(' · ');
  }
}

class _TemperatureCard extends StatelessWidget {
  const _TemperatureCard({
    required this.valid,
    required this.value,
    required this.target,
    required this.hysteresis,
    required this.thermostatEnabled,
    required this.heaterOn,
    required this.alarm,
  });

  final bool valid;
  final double value;
  final double target;
  final double hysteresis;
  final bool thermostatEnabled;
  final bool heaterOn;
  final bool alarm;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    final thermostatLabel = thermostatEnabled
        ? heaterOn
              ? 'Grzanie'
              : 'Termostat'
        : 'Wyłączona';
    final thermostatChip = Semantics(
      label: 'Stan termostatu: $thermostatLabel',
      child: ExcludeSemantics(
        child: DecoratedBox(
          decoration: BoxDecoration(
            color: colors.surfaceContainerHighest,
            borderRadius: BorderRadius.circular(5),
          ),
          child: Padding(
            padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 7),
            child: Text(
              thermostatLabel,
              style: const TextStyle(fontWeight: FontWeight.w700),
            ),
          ),
        ),
      ),
    );
    return Card(
      child: Padding(
        padding: const EdgeInsets.fromLTRB(20, 20, 20, 22),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            LayoutBuilder(
              builder: (context, constraints) {
                final textScale = MediaQuery.textScalerOf(context).scale(1);
                final title = Row(
                  children: [
                    Icon(
                      Icons.thermostat_rounded,
                      color: alarm ? colors.error : colors.onSurface,
                    ),
                    const SizedBox(width: 10),
                    const Expanded(
                      child: Text(
                        'Temperatura wody',
                        style: TextStyle(
                          fontSize: 19,
                          fontWeight: FontWeight.w700,
                        ),
                      ),
                    ),
                  ],
                );
                if (constraints.maxWidth < 360 || textScale > 1.35) {
                  return Column(
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: [
                      title,
                      const SizedBox(height: 10),
                      Align(
                        alignment: Alignment.centerLeft,
                        child: thermostatChip,
                      ),
                    ],
                  );
                }
                return Row(
                  children: [
                    Expanded(child: title),
                    const SizedBox(width: 12),
                    thermostatChip,
                  ],
                );
              },
            ),
            const SizedBox(height: 18),
            Wrap(
              crossAxisAlignment: WrapCrossAlignment.end,
              spacing: 12,
              runSpacing: 6,
              children: [
                Text(
                  valid ? value.toStringAsFixed(1) : '—',
                  style: Theme.of(context).textTheme.displayMedium?.copyWith(
                    fontWeight: FontWeight.w500,
                    height: 0.95,
                    color: alarm ? colors.error : null,
                  ),
                ),
                Padding(
                  padding: const EdgeInsets.only(bottom: 4),
                  child: Text(
                    valid
                        ? '°C   cel ${target.toStringAsFixed(1)}°C ± ${hysteresis.toStringAsFixed(1)}'
                        : 'Brak wiarygodnego pomiaru',
                    style: Theme.of(context).textTheme.titleMedium?.copyWith(
                      color: colors.onSurfaceVariant,
                    ),
                  ),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}

class _CompactMetricCard extends StatelessWidget {
  const _CompactMetricCard({
    required this.icon,
    required this.label,
    required this.value,
    required this.detail,
    this.unit,
    this.tone,
  });

  final IconData icon;
  final String label;
  final String value;
  final String detail;
  final String? unit;
  final Color? tone;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    final foreground = tone ?? colors.onSurface;
    final textScale = MediaQuery.textScalerOf(context).scale(1);
    final accessibilityLayout = textScale > 1.4;
    return MergeSemantics(
      child: Card(
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  Icon(icon, size: 20, color: tone ?? colors.onSurfaceVariant),
                  const SizedBox(width: 8),
                  Expanded(
                    child: Text(
                      label,
                      maxLines: accessibilityLayout ? null : 2,
                      overflow: accessibilityLayout
                          ? TextOverflow.visible
                          : TextOverflow.ellipsis,
                      style: TextStyle(color: colors.onSurfaceVariant),
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 16),
              Text.rich(
                TextSpan(
                  style: Theme.of(context).textTheme.headlineMedium?.copyWith(
                    color: foreground,
                    fontWeight: FontWeight.w500,
                  ),
                  children: [
                    TextSpan(text: value),
                    if (unit != null)
                      TextSpan(
                        text: ' $unit',
                        style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                          color: colors.onSurfaceVariant,
                        ),
                      ),
                  ],
                ),
              ),
              const SizedBox(height: 3),
              Text(
                detail,
                maxLines: accessibilityLayout ? null : 2,
                overflow: accessibilityLayout
                    ? TextOverflow.visible
                    : TextOverflow.ellipsis,
                style: Theme.of(
                  context,
                ).textTheme.bodySmall?.copyWith(color: colors.onSurfaceVariant),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _DeviceGrid extends StatelessWidget {
  const _DeviceGrid({required this.entries});

  final List<(String, String)> entries;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return LayoutBuilder(
      builder: (context, constraints) {
        final textScale = MediaQuery.textScalerOf(context).scale(1);
        final columns = constraints.maxWidth >= 480 && textScale <= 1.3 ? 2 : 1;
        final width = (constraints.maxWidth - 16 * (columns - 1)) / columns;
        return Wrap(
          spacing: 16,
          runSpacing: 12,
          children: [
            for (final entry in entries)
              SizedBox(
                width: width,
                child: MergeSemantics(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        entry.$1,
                        style: TextStyle(color: colors.onSurfaceVariant),
                      ),
                      const SizedBox(height: 2),
                      Text(
                        entry.$2,
                        maxLines: columns == 1 ? null : 1,
                        overflow: columns == 1
                            ? TextOverflow.visible
                            : TextOverflow.ellipsis,
                        style: const TextStyle(fontWeight: FontWeight.w700),
                      ),
                    ],
                  ),
                ),
              ),
          ],
        );
      },
    );
  }
}
