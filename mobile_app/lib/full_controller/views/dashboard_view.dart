import 'dart:async';

import 'package:flutter/material.dart';

import '../controller_api.dart';
import '../controller_session.dart';
import '../controller_shell.dart';
import '../data_access.dart';
import '../widgets.dart';

class DashboardView extends StatefulWidget {
  const DashboardView({
    super.key,
    required this.session,
    required this.runAction,
  });

  final ControllerSession session;
  final RunControllerAction runAction;

  @override
  State<DashboardView> createState() => _DashboardViewState();
}

class _DashboardViewState extends State<DashboardView> {
  final Set<String> busyOutputs = {};
  bool feeding = false;

  Future<void> _toggle(String action, String key, bool value) async {
    setState(() => busyOutputs.add(key));
    try {
      await widget.runAction(action, payload: {'state': value});
    } on ControllerApiException {
      // Komunikat jest prezentowany centralnie przez powłokę aplikacji.
    } finally {
      if (mounted) setState(() => busyOutputs.remove(key));
    }
  }

  Future<void> _feed() async {
    setState(() => feeding = true);
    try {
      await widget.runAction('feed_now', refreshAfter: true);
    } on ControllerApiException {
      // Komunikat jest prezentowany centralnie przez powłokę aplikacji.
    } finally {
      if (mounted) setState(() => feeding = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final status = widget.session.status;
    final sensors = status.section('sensors');
    final alarms = status.section('alarms');
    final modules = status.section('modules');
    final system = status.section('system');
    final battery = status.section('battery');
    final network = status.section('network');
    final feedingData = status.section('feeding');
    final alarmCount = alarms.integer('activeCount');
    final hasAlarm = alarmCount > 0 || alarms.integer('flags') != 0;
    final colors = Theme.of(context).colorScheme;

    return ControllerPageBody(
      children: [
        if (widget.session.isDevelopment) ...[
          const StatusBanner(
            icon: Icons.science_outlined,
            title: 'Tryb deweloperski',
            message:
                'Czujniki, harmonogramy, przekaźniki i operacje administracyjne są symulowane w RAM.',
            isError: false,
          ),
          const SizedBox(height: 12),
        ],
        StatusBanner(
          icon: hasAlarm ? Icons.warning_rounded : Icons.verified_rounded,
          title: hasAlarm ? 'System wymaga uwagi' : 'System stabilny',
          message: hasAlarm
              ? _alarmDescription(alarms)
              : 'Nie wykryto aktywnych alarmów. Poziom wody i czujniki są prawidłowe.',
          isError: hasAlarm,
        ),
        const SectionHeader(
          title: 'Parametry akwarium',
          description: 'Te same dane bieżące, które prezentuje pulpit WWW.',
        ),
        ResponsiveGrid(
          children: [
            MetricTile(
              icon: Icons.thermostat_rounded,
              label: 'Temperatura',
              value: sensors.flag('temp_valid')
                  ? '${sensors.number('temp_c').toStringAsFixed(2)} °C'
                  : '--',
              detail:
                  'Cel ${status.section('config').number('target_temp', 25).toStringAsFixed(1)} °C',
              tone:
                  hasAlarm &&
                      (alarms.flag('temperatureHigh') ||
                          alarms.flag('temperatureLow'))
                  ? colors.error
                  : null,
            ),
            MetricTile(
              icon: Icons.science_rounded,
              label: 'Odczyn wody',
              value: sensors.flag('ph_valid')
                  ? 'pH ${sensors.number('ph').toStringAsFixed(2)}'
                  : 'pH --',
              detail: alarms.flag('phOutOfRange')
                  ? 'Poza bezpiecznym zakresem'
                  : 'Odczyt stabilny',
              tone: alarms.flag('phOutOfRange') ? colors.error : null,
            ),
            MetricTile(
              icon: Icons.water_drop_outlined,
              label: 'Przewodność EC',
              value: sensors.flag('ec_valid')
                  ? '${sensors.number('ec').toStringAsFixed(0)} µS/cm'
                  : '--',
              detail:
                  'Czujnik ${modules.flag('ec_enabled') ? 'aktywny' : 'wyłączony'}',
            ),
            MetricTile(
              icon: Icons.light_mode_outlined,
              label: 'Światło otoczenia',
              value: sensors.flag('ldr_valid')
                  ? '${sensors.integer('ldr')}'
                  : '--',
              detail: 'Surowy odczyt LDR',
            ),
            MetricTile(
              icon: Icons.battery_5_bar_rounded,
              label: 'Zasilanie',
              value: battery.nullableNumber('voltage') == null
                  ? '--'
                  : '${battery.number('voltage').toStringAsFixed(2)} V',
              detail: battery['percent'] == null
                  ? 'Brak pomiaru baterii'
                  : '${battery.integer('percent')}%',
              tone: alarms.flag('supplyLow') ? colors.error : null,
            ),
            MetricTile(
              icon: Icons.memory_rounded,
              label: 'Sterownik',
              value: formatBytes(
                system.integer('freeHeap', status.integer('heap_free')),
              ),
              detail: 'Uptime ${formatUptime(system.integer('uptime'))}',
            ),
          ],
        ),
        const SectionHeader(
          title: 'Stan bezpieczeństwa',
          description: 'Wejścia MCP23017, ATO, wyciek i przepływ.',
        ),
        ResponsiveGrid(
          minimumChildWidth: 250,
          children: [
            _SafetyCard(
              icon: Icons.water_rounded,
              title: 'Poziom wody',
              active: sensors.flag('water_level_high'),
              valid: sensors.flag('water_level_valid'),
              goodText: 'Poziom prawidłowy',
              badText: 'Niski poziom wody',
              inverted: false,
            ),
            _SafetyCard(
              icon: Icons.warning_amber_rounded,
              title: 'Czujnik wycieku',
              active: sensors.flag('leak_detected'),
              valid: sensors.flag('leak_valid'),
              goodText: 'Brak wycieku',
              badText: 'Wykryto wyciek',
              inverted: true,
            ),
            _SafetyCard(
              icon: Icons.waves_rounded,
              title: 'Przepływ',
              active: sensors.flag('flow_active'),
              valid: sensors.flag('flow_valid'),
              goodText: 'Przepływ aktywny',
              badText: 'Brak przepływu',
              inverted: false,
            ),
          ],
        ),
        const SectionHeader(
          title: 'Centrum sterowania',
          description:
              'Zmiana stanu przełącza ten sam tryb Always On/Off co przyciski WWW.',
        ),
        Card(
          child: Column(
            children: [
              _OutputSwitch(
                icon: Icons.lightbulb_rounded,
                title: 'Światło główne',
                value: modules.flag('light_on'),
                busy: busyOutputs.contains('light'),
                onChanged: (value) => _toggle('set_light', 'light', value),
              ),
              const Divider(height: 1),
              _OutputSwitch(
                icon: Icons.local_florist_rounded,
                title: 'Światło roślinne',
                value: modules.flag('plant_light_on'),
                busy: busyOutputs.contains('plant'),
                onChanged: (value) => _toggle('set_plant', 'plant', value),
              ),
              const Divider(height: 1),
              _OutputSwitch(
                icon: Icons.filter_alt_rounded,
                title: 'Filtr',
                value: modules.flag('filter_on'),
                busy: busyOutputs.contains('filter'),
                onChanged: (value) => _toggle('set_filter', 'filter', value),
              ),
              const Divider(height: 1),
              _OutputSwitch(
                icon: Icons.thermostat_rounded,
                title: 'Grzałka / termostat',
                value: modules.flag('heater_on'),
                busy: busyOutputs.contains('heater'),
                onChanged: (value) => _toggle('set_heater', 'heater', value),
              ),
              const Divider(height: 1),
              _OutputSwitch(
                icon: Icons.air_rounded,
                title: 'Napowietrzanie',
                value: modules.flag('air_on'),
                busy: busyOutputs.contains('aeration'),
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
            child: Row(
              children: [
                const Icon(Icons.set_meal_rounded, size: 34),
                const SizedBox(width: 14),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      const Text(
                        'Karmnik',
                        style: TextStyle(
                          fontWeight: FontWeight.w800,
                          fontSize: 17,
                        ),
                      ),
                      Text(
                        feedingData.flag('active')
                            ? 'Trwa podawanie pokarmu'
                            : 'Ostatni wynik: ${feedingData.text('lastResult', 'brak danych')}',
                      ),
                    ],
                  ),
                ),
                FilledButton.icon(
                  onPressed: feeding || feedingData.flag('active')
                      ? null
                      : _feed,
                  icon: feeding
                      ? const SizedBox.square(
                          dimension: 18,
                          child: CircularProgressIndicator(strokeWidth: 2),
                        )
                      : const Icon(Icons.play_arrow_rounded),
                  label: const Text('Nakarm teraz'),
                ),
              ],
            ),
          ),
        ),
        const SectionHeader(title: 'Połączenie i urządzenie'),
        Card(
          child: Padding(
            padding: const EdgeInsets.all(16),
            child: Column(
              children: [
                InfoRow(
                  label: 'Sieć',
                  value: network.text('ssid', '—'),
                  icon: Icons.wifi_rounded,
                ),
                InfoRow(
                  label: 'Adres IP',
                  value: network.text('ip', status.text('ip', '—')),
                ),
                InfoRow(
                  label: 'Sygnał',
                  value: '${network.integer('rssi')} dBm',
                ),
                InfoRow(
                  label: 'Firmware',
                  value: status.section('firmware').text('version', 'dev'),
                ),
                InfoRow(
                  label: 'Karta SD',
                  value: status.flag('sd_mounted')
                      ? 'zamontowana'
                      : 'niedostępna',
                ),
              ],
            ),
          ),
        ),
      ],
    );
  }

  String _alarmDescription(JsonMap alarms) {
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

class _SafetyCard extends StatelessWidget {
  const _SafetyCard({
    required this.icon,
    required this.title,
    required this.active,
    required this.valid,
    required this.goodText,
    required this.badText,
    required this.inverted,
  });

  final IconData icon;
  final String title;
  final bool active;
  final bool valid;
  final String goodText;
  final String badText;
  final bool inverted;

  @override
  Widget build(BuildContext context) {
    final good = valid && (inverted ? !active : active);
    final colors = Theme.of(context).colorScheme;
    return Card(
      margin: EdgeInsets.zero,
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Row(
          children: [
            CircleAvatar(
              backgroundColor: good
                  ? colors.primaryContainer
                  : colors.errorContainer,
              child: Icon(icon),
            ),
            const SizedBox(width: 12),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    title,
                    style: const TextStyle(fontWeight: FontWeight.w700),
                  ),
                  Text(
                    !valid
                        ? 'Brak wiarygodnego odczytu'
                        : (good ? goodText : badText),
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
