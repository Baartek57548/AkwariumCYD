import 'package:flutter/material.dart';

import '../../design/app_theme.dart';
import '../../domain/entity_ids.dart';
import '../../state/aquacyd_controller.dart';
import '../widgets/common.dart';

class ControlPage extends StatefulWidget {
  const ControlPage({required this.controller, super.key});

  final AquaCydController controller;

  @override
  State<ControlPage> createState() => _ControlPageState();
}

class _ControlPageState extends State<ControlPage> {
  var _overrideMinutes = 15.0;
  var _feedingMinutes = 10.0;

  static const _outputs = <_OutputDefinition>[
    _OutputDefinition(
      keyName: 'light_primary',
      label: 'Światło główne',
      entityId: AquaEntityIds.lightPrimary,
      onScript: AquaScripts.lightOn,
      offScript: AquaScripts.lightOff,
      icon: Icons.lightbulb_outline_rounded,
    ),
    _OutputDefinition(
      keyName: 'light_secondary',
      label: 'Światło roślinne',
      entityId: AquaEntityIds.lightSecondary,
      onScript: AquaScripts.plantLightOn,
      offScript: AquaScripts.plantLightOff,
      icon: Icons.eco_outlined,
    ),
    _OutputDefinition(
      keyName: 'filter',
      label: 'Filtr',
      entityId: AquaEntityIds.filter,
      onScript: AquaScripts.filterOn,
      offScript: AquaScripts.filterOff,
      icon: Icons.filter_alt_outlined,
      warning: 'Wyłączenie filtra może pogorszyć jakość wody.',
    ),
    _OutputDefinition(
      keyName: 'aerator',
      label: 'Napowietrzanie',
      entityId: AquaEntityIds.aerator,
      onScript: AquaScripts.aeratorOn,
      offScript: AquaScripts.aeratorOff,
      icon: Icons.bubble_chart_outlined,
    ),
  ];

  @override
  Widget build(BuildContext context) {
    final snapshot = widget.controller.snapshot;
    return AquaPage(
      children: <Widget>[
        const SectionTitle(
          title: 'Sterowanie ręczne',
          subtitle:
              'Każda zmiana ma limit czasu i przechodzi przez walidację CYD',
        ),
        const SizedBox(height: 16),
        _DurationCard(
          value: _overrideMinutes,
          onChanged: (value) => setState(() => _overrideMinutes = value),
        ),
        const SizedBox(height: 22),
        LayoutBuilder(
          builder: (context, constraints) {
            final columns = constraints.maxWidth >= 900
                ? 2
                : constraints.maxWidth >= 620
                ? 2
                : 1;
            return GridView.count(
              crossAxisCount: columns,
              shrinkWrap: true,
              physics: const NeverScrollableScrollPhysics(),
              mainAxisSpacing: 12,
              crossAxisSpacing: 12,
              childAspectRatio: columns == 1 ? 1.9 : 1.65,
              children: <Widget>[
                for (final output in _outputs)
                  _OutputControlCard(
                    output: output,
                    state: snapshot.binary(output.entityId),
                    busy:
                        widget.controller.isBusy('${output.keyName}_on') ||
                        widget.controller.isBusy('${output.keyName}_off'),
                    onSet: (enabled) => _setOutput(output, enabled),
                  ),
              ],
            );
          },
        ),
        const SizedBox(height: 28),
        const SectionTitle(
          title: 'Tryb karmienia',
          subtitle: 'Uruchamia przygotowaną sekwencję bez zmiany harmonogramów',
        ),
        const SizedBox(height: 12),
        Card(
          child: Padding(
            padding: const EdgeInsets.all(20),
            child: LayoutBuilder(
              builder: (context, constraints) {
                final vertical = constraints.maxWidth < 520;
                final slider = Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: <Widget>[
                    Text(
                      'Czas: ${_feedingMinutes.round()} min',
                      style: Theme.of(context).textTheme.titleMedium?.copyWith(
                        fontWeight: FontWeight.w800,
                      ),
                    ),
                    Slider(
                      value: _feedingMinutes,
                      min: 1,
                      max: 60,
                      divisions: 59,
                      label: '${_feedingMinutes.round()} min',
                      onChanged: (value) =>
                          setState(() => _feedingMinutes = value),
                    ),
                  ],
                );
                final button = FilledButton.icon(
                  onPressed: widget.controller.isBusy('feeding')
                      ? null
                      : _startFeeding,
                  icon: const Icon(Icons.set_meal_outlined),
                  label: const Text('Rozpocznij'),
                );
                if (vertical) {
                  return Column(
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: <Widget>[
                      slider,
                      const SizedBox(height: 12),
                      button,
                    ],
                  );
                }
                return Row(
                  children: <Widget>[
                    Expanded(child: slider),
                    const SizedBox(width: 18),
                    button,
                  ],
                );
              },
            ),
          ),
        ),
        const SizedBox(height: 22),
        Card(
          child: ListTile(
            contentPadding: const EdgeInsets.symmetric(
              horizontal: 20,
              vertical: 10,
            ),
            leading: const Icon(Icons.local_fire_department_outlined),
            title: const Text(
              'Grzałka jest sterowana automatycznie',
              style: TextStyle(fontWeight: FontWeight.w700),
            ),
            subtitle: Text(
              snapshot.binary(AquaEntityIds.heater) == true
                  ? 'Aktualnie grzeje. Parametry zmienisz w Automatyce.'
                  : 'Aktualnie wyłączona. Parametry zmienisz w Automatyce.',
            ),
          ),
        ),
      ],
    );
  }

  Future<void> _setOutput(_OutputDefinition output, bool enabled) async {
    final confirmed =
        await showDialog<bool>(
          context: context,
          builder: (context) => AlertDialog(
            title: Text('${enabled ? 'Włączyć' : 'Wyłączyć'} ${output.label}?'),
            content: Text(
              '${output.warning == null ? '' : '${output.warning}\n\n'}'
              'Ręczne ustawienie będzie aktywne przez '
              '${_overrideMinutes.round()} min, po czym sterownik wróci do '
              'normalnej automatyki.',
            ),
            actions: <Widget>[
              TextButton(
                onPressed: () => Navigator.pop(context, false),
                child: const Text('Anuluj'),
              ),
              FilledButton(
                onPressed: () => Navigator.pop(context, true),
                child: Text(enabled ? 'Włącz' : 'Wyłącz'),
              ),
            ],
          ),
        ) ??
        false;
    if (!confirmed) {
      return;
    }
    await widget.controller.setOutput(
      operation: '${output.keyName}_${enabled ? 'on' : 'off'}',
      script: enabled ? output.onScript : output.offScript,
      overrideMinutes: _overrideMinutes.round(),
    );
  }

  Future<void> _startFeeding() async {
    final confirmed =
        await showDialog<bool>(
          context: context,
          builder: (context) => AlertDialog(
            title: const Text('Rozpocząć tryb karmienia?'),
            content: Text(
              'Sekwencja będzie aktywna przez ${_feedingMinutes.round()} min.',
            ),
            actions: <Widget>[
              TextButton(
                onPressed: () => Navigator.pop(context, false),
                child: const Text('Anuluj'),
              ),
              FilledButton(
                onPressed: () => Navigator.pop(context, true),
                child: const Text('Uruchom'),
              ),
            ],
          ),
        ) ??
        false;
    if (confirmed) {
      await widget.controller.startFeeding(_feedingMinutes.round());
    }
  }
}

class _DurationCard extends StatelessWidget {
  const _DurationCard({required this.value, required this.onChanged});

  final double value;
  final ValueChanged<double> onChanged;

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(20),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: <Widget>[
            Row(
              children: <Widget>[
                Icon(
                  Icons.timer_outlined,
                  color: Theme.of(context).colorScheme.primary,
                ),
                const SizedBox(width: 10),
                Expanded(
                  child: Text(
                    'Limit ręcznego sterowania',
                    style: Theme.of(context).textTheme.titleMedium?.copyWith(
                      fontWeight: FontWeight.w800,
                    ),
                  ),
                ),
                Text(
                  '${value.round()} min',
                  style: const TextStyle(fontWeight: FontWeight.w800),
                ),
              ],
            ),
            Slider(
              value: value,
              min: 1,
              max: 180,
              divisions: 179,
              label: '${value.round()} min',
              onChanged: onChanged,
            ),
          ],
        ),
      ),
    );
  }
}

class _OutputControlCard extends StatelessWidget {
  const _OutputControlCard({
    required this.output,
    required this.state,
    required this.busy,
    required this.onSet,
  });

  final _OutputDefinition output;
  final bool? state;
  final bool busy;
  final ValueChanged<bool> onSet;

  @override
  Widget build(BuildContext context) {
    final activeColor = state == true
        ? AquaColors.green
        : Theme.of(context).colorScheme.onSurfaceVariant;
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(20),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: <Widget>[
            Row(
              children: <Widget>[
                Container(
                  padding: const EdgeInsets.all(11),
                  decoration: BoxDecoration(
                    color: activeColor.withValues(alpha: 0.13),
                    borderRadius: BorderRadius.circular(14),
                  ),
                  child: Icon(output.icon, color: activeColor),
                ),
                const Spacer(),
                if (busy)
                  const SizedBox(
                    width: 22,
                    height: 22,
                    child: CircularProgressIndicator(strokeWidth: 2),
                  )
                else
                  Text(
                    state == null
                        ? 'BRAK DANYCH'
                        : state!
                        ? 'WŁĄCZONE'
                        : 'WYŁĄCZONE',
                    style: TextStyle(
                      color: activeColor,
                      fontSize: 12,
                      fontWeight: FontWeight.w800,
                    ),
                  ),
              ],
            ),
            const SizedBox(height: 16),
            Text(
              output.label,
              style: Theme.of(
                context,
              ).textTheme.titleLarge?.copyWith(fontWeight: FontWeight.w800),
            ),
            const Spacer(),
            Row(
              children: <Widget>[
                Expanded(
                  child: OutlinedButton(
                    onPressed: busy ? null : () => onSet(false),
                    child: const Text('Wyłącz'),
                  ),
                ),
                const SizedBox(width: 10),
                Expanded(
                  child: FilledButton(
                    onPressed: busy ? null : () => onSet(true),
                    child: const Text('Włącz'),
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

class _OutputDefinition {
  const _OutputDefinition({
    required this.keyName,
    required this.label,
    required this.entityId,
    required this.onScript,
    required this.offScript,
    required this.icon,
    this.warning,
  });

  final String keyName;
  final String label;
  final String entityId;
  final String onScript;
  final String offScript;
  final IconData icon;
  final String? warning;
}
