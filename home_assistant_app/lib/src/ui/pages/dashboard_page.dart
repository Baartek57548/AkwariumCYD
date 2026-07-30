import 'package:flutter/material.dart';

import '../../design/app_theme.dart';
import '../../domain/entity_ids.dart';
import '../../domain/models.dart';
import '../../state/aquacyd_controller.dart';
import '../widgets/common.dart';

class DashboardPage extends StatelessWidget {
  const DashboardPage({
    required this.controller,
    required this.showAlarms,
    super.key,
  });

  final AquaCydController controller;
  final VoidCallback showAlarms;

  @override
  Widget build(BuildContext context) {
    final snapshot = controller.snapshot;
    return AquaPage(
      children: <Widget>[
        _HealthHero(snapshot: snapshot, onTap: showAlarms),
        const SizedBox(height: 26),
        const SectionTitle(
          title: 'Parametry wody',
          subtitle: 'Dane przesyłane przez CYD i bramkę ESP32-C6',
        ),
        const SizedBox(height: 14),
        _MetricsGrid(snapshot: snapshot),
        const SizedBox(height: 28),
        const SectionTitle(
          title: 'Urządzenia',
          subtitle: 'Aktualne stany wyjść sterownika',
        ),
        const SizedBox(height: 14),
        _OutputsGrid(snapshot: snapshot),
        const SizedBox(height: 28),
        SectionTitle(
          title: 'Szybkie akcje',
          subtitle: 'Bezpieczne polecenia wykonywane przez skrypty HA',
          trailing: TextButton.icon(
            onPressed: controller.isBusy('snapshot')
                ? null
                : controller.requestSnapshot,
            icon: const Icon(Icons.sync_rounded),
            label: const Text('Pełny stan'),
          ),
        ),
        const SizedBox(height: 14),
        _QuickActions(controller: controller),
      ],
    );
  }
}

class _HealthHero extends StatelessWidget {
  const _HealthHero({required this.snapshot, required this.onTap});

  final AquariumSnapshot snapshot;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final healthy =
        snapshot.safe == true &&
        !snapshot.hasCriticalAlarm &&
        snapshot.alarmFlags == 0 &&
        !snapshot.stale;
    final warning = !snapshot.hasCriticalAlarm && snapshot.alarmFlags == 0;
    final color = healthy
        ? AquaColors.green
        : warning
        ? AquaColors.amber
        : AquaColors.red;
    final title = healthy
        ? 'Akwarium działa prawidłowo'
        : snapshot.stale
        ? 'Dane wymagają odświeżenia'
        : snapshot.hasCriticalAlarm
        ? 'Wymagana natychmiastowa kontrola'
        : 'Sterownik zgłasza ostrzeżenie';
    final subtitle =
        'Ostatnia aktualizacja: ${formatUpdated(snapshot.lastUpdated)}';

    return InkWell(
      onTap: onTap,
      borderRadius: BorderRadius.circular(24),
      child: Ink(
        padding: const EdgeInsets.all(22),
        decoration: BoxDecoration(
          gradient: LinearGradient(
            begin: Alignment.topLeft,
            end: Alignment.bottomRight,
            colors: <Color>[
              color.withValues(alpha: 0.23),
              theme.colorScheme.surfaceContainerHighest.withValues(alpha: 0.45),
            ],
          ),
          borderRadius: BorderRadius.circular(24),
          border: Border.all(color: color.withValues(alpha: 0.42)),
        ),
        child: Row(
          children: <Widget>[
            Container(
              width: 60,
              height: 60,
              decoration: BoxDecoration(
                color: color.withValues(alpha: 0.16),
                borderRadius: BorderRadius.circular(19),
              ),
              child: Icon(
                healthy
                    ? Icons.check_circle_outline_rounded
                    : Icons.warning_amber_rounded,
                color: color,
                size: 34,
              ),
            ),
            const SizedBox(width: 18),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: <Widget>[
                  Text(
                    title,
                    style: theme.textTheme.titleLarge?.copyWith(
                      fontWeight: FontWeight.w800,
                    ),
                  ),
                  const SizedBox(height: 5),
                  Text(
                    subtitle,
                    style: theme.textTheme.bodyMedium?.copyWith(
                      color: theme.colorScheme.onSurfaceVariant,
                    ),
                  ),
                ],
              ),
            ),
            const SizedBox(width: 10),
            Icon(Icons.chevron_right_rounded, color: color),
          ],
        ),
      ),
    );
  }
}

class _MetricsGrid extends StatelessWidget {
  const _MetricsGrid({required this.snapshot});

  final AquariumSnapshot snapshot;

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(
      builder: (context, constraints) {
        final count = constraints.maxWidth >= 900
            ? 4
            : constraints.maxWidth >= 540
            ? 2
            : 2;
        return GridView.count(
          crossAxisCount: count,
          shrinkWrap: true,
          physics: const NeverScrollableScrollPhysics(),
          mainAxisSpacing: 12,
          crossAxisSpacing: 12,
          childAspectRatio: constraints.maxWidth < 400 ? 0.96 : 1.18,
          children: <Widget>[
            MetricCard(
              label: 'Temperatura',
              value: formatMeasurement(snapshot.temperature, '°C'),
              caption: snapshot.targetTemperature == null
                  ? null
                  : 'Cel ${snapshot.targetTemperature!.toStringAsFixed(1)} °C',
              icon: Icons.thermostat_rounded,
              color: AquaColors.red,
            ),
            MetricCard(
              label: 'Odczyn pH',
              value: formatMeasurement(snapshot.ph, '', decimals: 2),
              icon: Icons.science_outlined,
              color: AquaColors.blue,
            ),
            MetricCard(
              label: 'Przewodność EC',
              value: formatMeasurement(snapshot.ec, 'µS/cm', decimals: 0),
              icon: Icons.bolt_rounded,
              color: AquaColors.amber,
            ),
            MetricCard(
              label: 'Natężenie światła',
              value: formatMeasurement(snapshot.ldr, 'lx', decimals: 0),
              icon: Icons.light_mode_outlined,
              color: AquaColors.green,
            ),
          ],
        );
      },
    );
  }
}

class _OutputsGrid extends StatelessWidget {
  const _OutputsGrid({required this.snapshot});

  final AquariumSnapshot snapshot;

  @override
  Widget build(BuildContext context) {
    final items = <({String label, IconData icon, String entity})>[
      (
        label: 'Światło główne',
        icon: Icons.lightbulb_outline_rounded,
        entity: AquaEntityIds.lightPrimary,
      ),
      (
        label: 'Światło roślinne',
        icon: Icons.eco_outlined,
        entity: AquaEntityIds.lightSecondary,
      ),
      (
        label: 'Filtr',
        icon: Icons.filter_alt_outlined,
        entity: AquaEntityIds.filter,
      ),
      (
        label: 'Napowietrzanie',
        icon: Icons.bubble_chart_outlined,
        entity: AquaEntityIds.aerator,
      ),
      (
        label: 'Grzałka',
        icon: Icons.local_fire_department_outlined,
        entity: AquaEntityIds.heater,
      ),
    ];
    return LayoutBuilder(
      builder: (context, constraints) {
        final columns = constraints.maxWidth >= 820
            ? 3
            : constraints.maxWidth >= 560
            ? 2
            : 1;
        return GridView.count(
          crossAxisCount: columns,
          shrinkWrap: true,
          physics: const NeverScrollableScrollPhysics(),
          mainAxisSpacing: 10,
          crossAxisSpacing: 10,
          childAspectRatio: columns == 1 ? 3.8 : 3.2,
          children: [
            for (final item in items)
              StateTile(
                label: item.label,
                icon: item.icon,
                state: snapshot.binary(item.entity),
              ),
          ],
        );
      },
    );
  }
}

class _QuickActions extends StatelessWidget {
  const _QuickActions({required this.controller});

  final AquaCydController controller;

  @override
  Widget build(BuildContext context) {
    return Wrap(
      spacing: 12,
      runSpacing: 12,
      children: <Widget>[
        FilledButton.tonalIcon(
          onPressed: controller.isBusy('feeding') ? null : () => _feed(context),
          icon: const Icon(Icons.set_meal_outlined),
          label: const Text('Tryb karmienia'),
        ),
        OutlinedButton.icon(
          onPressed: controller.refreshing
              ? null
              : () => controller.refresh(showMessage: true),
          icon: const Icon(Icons.refresh_rounded),
          label: const Text('Odśwież dane'),
        ),
      ],
    );
  }

  Future<void> _feed(BuildContext context) async {
    var minutes = 10.0;
    final result = await showDialog<int>(
      context: context,
      builder: (context) => StatefulBuilder(
        builder: (context, setState) => AlertDialog(
          title: const Text('Rozpocząć karmienie?'),
          content: SizedBox(
            width: 380,
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: <Widget>[
                const Text(
                  'Sterownik tymczasowo zastosuje bezpieczny tryb karmienia.',
                ),
                const SizedBox(height: 20),
                Text(
                  'Czas: ${minutes.round()} min',
                  style: const TextStyle(fontWeight: FontWeight.w700),
                ),
                Slider(
                  value: minutes,
                  min: 1,
                  max: 60,
                  divisions: 59,
                  label: '${minutes.round()} min',
                  onChanged: (value) => setState(() => minutes = value),
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
              onPressed: () => Navigator.pop(context, minutes.round()),
              child: const Text('Uruchom'),
            ),
          ],
        ),
      ),
    );
    if (result != null) {
      await controller.startFeeding(result);
    }
  }
}
