import 'dart:async';

import 'package:flutter/material.dart';

import '../controller_api.dart';
import '../controller_session.dart';
import '../controller_shell.dart';
import '../data_access.dart';
import '../widgets.dart';
import 'automation_view.dart';
import 'relays_view.dart';

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

  Future<void> _toggle(String action, String key, bool value) async {
    setState(() => _busyOutputs.add(key));
    try {
      await widget.runAction(action, payload: {'state': value});
    } on ControllerApiException {
      // Powłoka prezentuje błąd i zachowuje ostatni potwierdzony stan.
    } finally {
      if (mounted) setState(() => _busyOutputs.remove(key));
    }
  }

  Future<void> _feed() async {
    setState(() => _feeding = true);
    try {
      await widget.runAction('feed_now');
    } on ControllerApiException {
      // Powłoka prezentuje komunikat zwrócony przez sterownik.
    } finally {
      if (mounted) setState(() => _feeding = false);
    }
  }

  void _open(String title, Widget Function() builder) {
    Navigator.of(context).push(
      MaterialPageRoute<void>(
        builder: (context) => Scaffold(
          appBar: AppBar(title: Text(title)),
          body: AnimatedBuilder(
            animation: widget.session,
            builder: (context, _) => builder(),
          ),
        ),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final modules = widget.session.status.section('modules');
    final feeding = widget.session.status.section('feeding');
    return ControllerPageBody(
      onRefresh: () => widget.session.refresh(includeHistory: true),
      children: [
        const SectionHeader(
          title: 'Szybkie sterowanie',
          description: 'Ręczna zmiana trybu wyjść sterownika.',
        ),
        Card(
          child: Column(
            children: [
              _OutputSwitch(
                icon: Icons.lightbulb_rounded,
                title: 'Światło 1',
                value: modules.flag('light_on'),
                busy: _busyOutputs.contains('light1'),
                onChanged: (value) => _toggle('set_light1', 'light1', value),
              ),
              const Divider(height: 1),
              _OutputSwitch(
                icon: Icons.lightbulb_outline_rounded,
                title: 'Światło 2',
                value: modules.flag('plant_light_on'),
                busy: _busyOutputs.contains('light2'),
                onChanged: (value) => _toggle('set_light2', 'light2', value),
              ),
              const Divider(height: 1),
              _OutputSwitch(
                icon: Icons.filter_alt_rounded,
                title: 'Filtr',
                value: modules.flag('filter_on'),
                busy: _busyOutputs.contains('filter'),
                onChanged: (value) => _toggle('set_filter', 'filter', value),
              ),
              const Divider(height: 1),
              _OutputSwitch(
                icon: Icons.thermostat_rounded,
                title: 'Grzałka / termostat',
                value: modules.flag('heater_on'),
                busy: _busyOutputs.contains('heater'),
                onChanged: (value) => _toggle('set_heater', 'heater', value),
              ),
              const Divider(height: 1),
              _OutputSwitch(
                icon: Icons.air_rounded,
                title: 'Napowietrzanie',
                value: modules.flag('air_on'),
                busy: _busyOutputs.contains('aeration'),
                onChanged: (value) =>
                    _toggle('set_aeration', 'aeration', value),
              ),
            ],
          ),
        ),
        const SizedBox(height: 12),
        Card(
          child: Padding(
            padding: const EdgeInsets.all(16),
            child: LayoutBuilder(
              builder: (context, constraints) {
                final content = Row(
                  children: [
                    const Icon(Icons.set_meal_rounded, size: 32),
                    const SizedBox(width: 14),
                    Expanded(
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          const Text(
                            'Karmnik',
                            style: TextStyle(
                              fontSize: 17,
                              fontWeight: FontWeight.w800,
                            ),
                          ),
                          Text(
                            feeding.flag('active')
                                ? 'Trwa podawanie pokarmu'
                                : 'Gotowy do karmienia',
                          ),
                        ],
                      ),
                    ),
                  ],
                );
                final button = FilledButton.icon(
                  onPressed: _feeding || feeding.flag('active') ? null : _feed,
                  icon: _feeding
                      ? const SizedBox.square(
                          dimension: 17,
                          child: CircularProgressIndicator(strokeWidth: 2),
                        )
                      : const Icon(Icons.play_arrow_rounded),
                  label: const Text('Nakarm'),
                );
                if (constraints.maxWidth < 320) {
                  return Column(
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: [content, const SizedBox(height: 12), button],
                  );
                }
                return Row(
                  children: [
                    Expanded(child: content),
                    const SizedBox(width: 12),
                    button,
                  ],
                );
              },
            ),
          ),
        ),
        const SectionHeader(
          title: 'Konfiguracja',
          description: 'Automatyka bezpieczeństwa i fizyczne kanały wyjściowe.',
        ),
        _NavigationCard(
          icon: Icons.auto_mode_rounded,
          title: 'Automatyka',
          subtitle: 'Temperatura, CO₂, dolewanie i reakcja na wyciek',
          onTap: () => _open(
            'Automatyka',
            () => AutomationView(
              session: widget.session,
              runAction: widget.runAction,
            ),
          ),
        ),
        const SizedBox(height: 10),
        _NavigationCard(
          icon: Icons.cable_rounded,
          title: 'Przekaźniki',
          subtitle: 'Mapa MCP23017, test kanałów i profil 8CH',
          onTap: () => _open(
            'Przekaźniki',
            () => RelaysView(
              session: widget.session,
              runAction: widget.runAction,
              ensureAdmin: widget.ensureAdmin,
            ),
          ),
        ),
      ],
    );
  }
}

class _OutputSwitch extends StatelessWidget {
  const _OutputSwitch({
    required this.icon,
    required this.title,
    required this.value,
    required this.busy,
    required this.onChanged,
  });

  final IconData icon;
  final String title;
  final bool value;
  final bool busy;
  final ValueChanged<bool> onChanged;

  @override
  Widget build(BuildContext context) {
    return SwitchListTile(
      secondary: busy
          ? const SizedBox.square(
              dimension: 22,
              child: CircularProgressIndicator(strokeWidth: 2),
            )
          : Icon(icon),
      title: Text(title, style: const TextStyle(fontWeight: FontWeight.w700)),
      subtitle: Text(value ? 'Włączony' : 'Wyłączony'),
      value: value,
      onChanged: busy ? null : onChanged,
    );
  }
}

class _NavigationCard extends StatelessWidget {
  const _NavigationCard({
    required this.icon,
    required this.title,
    required this.subtitle,
    required this.onTap,
  });

  final IconData icon;
  final String title;
  final String subtitle;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return Card(
      clipBehavior: Clip.antiAlias,
      child: ListTile(
        contentPadding: const EdgeInsets.symmetric(horizontal: 18, vertical: 8),
        leading: Icon(icon, size: 30),
        title: Text(title, style: const TextStyle(fontWeight: FontWeight.w800)),
        subtitle: Text(subtitle),
        trailing: const Icon(Icons.chevron_right_rounded),
        onTap: onTap,
      ),
    );
  }
}
