import 'package:flutter/material.dart';

import '../../design/app_theme.dart';
import '../../domain/models.dart';
import '../../state/aquacyd_controller.dart';
import '../widgets/common.dart';

class AlarmsPage extends StatelessWidget {
  const AlarmsPage({required this.controller, super.key});

  final AquaCydController controller;

  static const _alarms = <_AlarmDefinition>[
    _AlarmDefinition(1 << 0, 'Za wysoka temperatura', Icons.thermostat_rounded),
    _AlarmDefinition(1 << 1, 'Za niska temperatura', Icons.ac_unit_rounded),
    _AlarmDefinition(1 << 2, 'pH poza zakresem', Icons.science_outlined),
    _AlarmDefinition(1 << 3, 'Niski poziom wody', Icons.water_drop_outlined),
    _AlarmDefinition(1 << 4, 'Wykryto wyciek', Icons.warning_amber_rounded),
    _AlarmDefinition(1 << 5, 'Niskie napięcie zasilania', Icons.battery_alert),
    _AlarmDefinition(1 << 6, 'Brak czujnika', Icons.sensors_off_outlined),
    _AlarmDefinition(
      1 << 7,
      'Nieaktualne dane czujnika',
      Icons.timer_off_outlined,
    ),
    _AlarmDefinition(1 << 8, 'Błąd magistrali czujników', Icons.cable_outlined),
    _AlarmDefinition(
      1 << 9,
      'Błąd sterowania wyjściem',
      Icons.power_off_outlined,
    ),
  ];

  @override
  Widget build(BuildContext context) {
    final snapshot = controller.snapshot;
    final active = _alarms
        .where((alarm) => snapshot.alarmFlags & alarm.mask != 0)
        .toList(growable: false);
    return AquaPage(
      children: <Widget>[
        SectionTitle(
          title: 'Bezpieczeństwo',
          subtitle: 'Alarmy są wykrywane i egzekwowane lokalnie przez CYD',
          trailing: IconButton.filledTonal(
            tooltip: 'Pobierz pełny stan',
            onPressed: controller.isBusy('snapshot')
                ? null
                : controller.requestSnapshot,
            icon: const Icon(Icons.sync_rounded),
          ),
        ),
        const SizedBox(height: 16),
        _SafetySummary(snapshot: snapshot, activeCount: active.length),
        const SizedBox(height: 24),
        const SectionTitle(title: 'Kontrole krytyczne'),
        const SizedBox(height: 12),
        StateTile(
          label: 'Brak wycieku',
          icon: Icons.water_damage_outlined,
          state: snapshot.leak == null ? null : !snapshot.leak!,
          subtitle: snapshot.leak == true
              ? 'Sprawdź akwarium natychmiast'
              : null,
        ),
        const SizedBox(height: 10),
        StateTile(
          label: 'Poziom wody prawidłowy',
          icon: Icons.water_rounded,
          state: snapshot.waterLow == null ? null : !snapshot.waterLow!,
          subtitle: snapshot.waterLow == true
              ? 'Uzupełnij wodę po ustaleniu przyczyny'
              : null,
        ),
        const SizedBox(height: 10),
        StateTile(
          label: 'Konfiguracja sterownika',
          icon: Icons.verified_outlined,
          state: snapshot.configValid,
        ),
        const SizedBox(height: 26),
        SectionTitle(
          title: 'Aktywne alarmy',
          subtitle: active.isEmpty
              ? 'Brak aktywnych flag alarmowych'
              : '${active.length} aktywnych zdarzeń',
        ),
        const SizedBox(height: 12),
        if (active.isEmpty)
          const _NoAlarms()
        else
          for (final alarm in active) ...<Widget>[
            _AlarmTile(alarm: alarm),
            const SizedBox(height: 10),
          ],
        const SizedBox(height: 18),
        Card(
          child: Padding(
            padding: const EdgeInsets.all(18),
            child: Row(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: <Widget>[
                Icon(
                  Icons.info_outline_rounded,
                  color: Theme.of(context).colorScheme.primary,
                ),
                const SizedBox(width: 12),
                const Expanded(
                  child: Text(
                    'Aplikacja nie kasuje lokalnych blokad bezpieczeństwa. '
                    'Najpierw usuń fizyczną przyczynę alarmu; sterownik sam '
                    'potwierdzi stabilny powrót do bezpiecznego stanu.',
                  ),
                ),
              ],
            ),
          ),
        ),
      ],
    );
  }
}

class _SafetySummary extends StatelessWidget {
  const _SafetySummary({required this.snapshot, required this.activeCount});

  final AquariumSnapshot snapshot;
  final int activeCount;

  @override
  Widget build(BuildContext context) {
    final safe =
        snapshot.safe == true &&
        activeCount == 0 &&
        !snapshot.hasCriticalAlarm &&
        !snapshot.stale;
    final color = safe ? AquaColors.green : AquaColors.red;
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(22),
        child: Row(
          children: <Widget>[
            Container(
              padding: const EdgeInsets.all(14),
              decoration: BoxDecoration(
                color: color.withValues(alpha: 0.13),
                borderRadius: BorderRadius.circular(17),
              ),
              child: Icon(
                safe ? Icons.shield_rounded : Icons.gpp_maybe_rounded,
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
                    safe ? 'System bezpieczny' : 'Sprawdź stan systemu',
                    style: Theme.of(context).textTheme.titleLarge?.copyWith(
                      fontWeight: FontWeight.w800,
                    ),
                  ),
                  const SizedBox(height: 4),
                  Text(
                    snapshot.stale
                        ? 'Telemetria jest nieaktualna'
                        : 'Maska alarmów: 0x${snapshot.alarmFlags.toRadixString(16).padLeft(4, '0').toUpperCase()}',
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _AlarmTile extends StatelessWidget {
  const _AlarmTile({required this.alarm});

  final _AlarmDefinition alarm;

  @override
  Widget build(BuildContext context) {
    return Card(
      child: ListTile(
        contentPadding: const EdgeInsets.symmetric(horizontal: 18, vertical: 8),
        leading: Container(
          padding: const EdgeInsets.all(10),
          decoration: BoxDecoration(
            color: AquaColors.red.withValues(alpha: 0.13),
            borderRadius: BorderRadius.circular(13),
          ),
          child: Icon(alarm.icon, color: AquaColors.red),
        ),
        title: Text(
          alarm.label,
          style: const TextStyle(fontWeight: FontWeight.w800),
        ),
        subtitle: Text('Flaga 0x${alarm.mask.toRadixString(16).toUpperCase()}'),
        trailing: const Icon(Icons.error_rounded, color: AquaColors.red),
      ),
    );
  }
}

class _NoAlarms extends StatelessWidget {
  const _NoAlarms();

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(26),
        child: Row(
          children: <Widget>[
            const Icon(
              Icons.check_circle_rounded,
              color: AquaColors.green,
              size: 34,
            ),
            const SizedBox(width: 16),
            Expanded(
              child: Text(
                'Sterownik nie raportuje aktywnych alarmów.',
                style: Theme.of(
                  context,
                ).textTheme.titleMedium?.copyWith(fontWeight: FontWeight.w700),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _AlarmDefinition {
  const _AlarmDefinition(this.mask, this.label, this.icon);

  final int mask;
  final String label;
  final IconData icon;
}
