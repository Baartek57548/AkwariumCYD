import 'package:flutter/material.dart';

import '../controller_api.dart';
import '../controller_session.dart';
import '../controller_shell.dart';
import '../data_access.dart';
import '../widgets.dart';

class SchedulesView extends StatefulWidget {
  const SchedulesView({
    super.key,
    required this.session,
    required this.runAction,
  });

  final ControllerSession session;
  final RunControllerAction runAction;

  @override
  State<SchedulesView> createState() => _SchedulesViewState();
}

class _SchedulesViewState extends State<SchedulesView> {
  late _ScheduleEntry light;
  late _ScheduleEntry plant;
  late _ScheduleEntry filter;
  late _ScheduleEntry air;
  late int heaterMode;
  late bool feederEnabled;
  late TimeOfDay feedTime;
  bool saving = false;
  String? statusMessage;

  @override
  void initState() {
    super.initState();
    _loadFromStatus();
  }

  void _loadFromStatus() {
    final schedules = widget.session.status.section('schedules');
    final legacy = widget.session.status.section('schedule');
    light = _ScheduleEntry.fromJson(
      schedules.section('light'),
      fallbackStart: formatClock(
        legacy.integer('dayStartHour', 10),
        legacy.integer('dayStartMin'),
      ),
      fallbackEnd: formatClock(
        legacy.integer('dayEndHour', 22),
        legacy.integer('dayEndMin'),
      ),
      fallbackMode: legacy.integer('lightMode'),
      profile: schedules.section('light').text('profile', 'day'),
    );
    plant = _ScheduleEntry.fromJson(
      schedules.section('plant_light'),
      fallbackStart: formatClock(
        legacy.integer('plantStartHour', 10),
        legacy.integer('plantStartMin', 30),
      ),
      fallbackEnd: formatClock(
        legacy.integer('plantEndHour', 21),
        legacy.integer('plantEndMin', 30),
      ),
      fallbackMode: legacy.integer('plantLightMode'),
      profile: schedules.section('plant_light').text('profile', 'day'),
    );
    filter = _ScheduleEntry.fromJson(
      schedules.section('filter'),
      fallbackStart: formatClock(
        legacy.integer('filterStartHour', 9),
        legacy.integer('filterStartMin', 30),
      ),
      fallbackEnd: formatClock(
        legacy.integer('filterEndHour', 22),
        legacy.integer('filterEndMin', 30),
      ),
      fallbackMode: legacy.integer('filterMode'),
    );
    air = _ScheduleEntry.fromJson(
      schedules.section('air'),
      fallbackStart: formatClock(
        legacy.integer('airStartHour', 22),
        legacy.integer('airStartMin'),
      ),
      fallbackEnd: formatClock(
        legacy.integer('airEndHour', 9),
        legacy.integer('airEndMin'),
      ),
      fallbackMode: legacy.integer('airMode'),
    );
    heaterMode = legacy.integer('heaterMode') == 1 ? 1 : 0;
    final feeding = widget.session.status.section('feeding');
    feederEnabled = feeding.integer('freq', 1) > 0;
    feedTime = TimeOfDay(
      hour: feeding.integer('hour', 14).clamp(0, 23),
      minute: feeding.integer('minute').clamp(0, 59),
    );
  }

  Future<void> _save() async {
    setState(() {
      saving = true;
      statusMessage = 'Zapisywanie harmonogramów…';
    });
    try {
      await widget.runAction(
        'save_schedule',
        payload: {
          'lightMode': light.mode,
          'dayStart': _timeText(light.start),
          'dayEnd': _timeText(light.end),
          'lightStart': _timeText(light.start),
          'lightEnd': _timeText(light.end),
          'lightProfile': light.profile,
          'plantLightMode': plant.mode,
          'plantLightStart': _timeText(plant.start),
          'plantLightEnd': _timeText(plant.end),
          'plantLightProfile': plant.profile,
          'aerationMode': air.mode,
          'airOn': _timeText(air.start),
          'airOff': _timeText(air.end),
          'filterMode': filter.mode,
          'filterOn': _timeText(filter.start),
          'filterOff': _timeText(filter.end),
          'heaterMode': heaterMode,
          'heaterStart': '00:00',
          'heaterEnd': '23:59',
          'feedFreq': feederEnabled ? 1 : 0,
          'feedTime': _timeText(feedTime),
        },
      );
      if (mounted) {
        setState(
          () => statusMessage = 'Harmonogramy zapisane i zsynchronizowane.',
        );
      }
    } on ControllerApiException catch (error) {
      if (mounted) setState(() => statusMessage = error.message);
    } finally {
      if (mounted) setState(() => saving = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    return ControllerPageBody(
      children: [
        SectionHeader(
          title: 'Plan dobowy 24 h',
          description:
              'Tryby i profile odpowiadają bezpośrednio funkcji save_schedule panelu WWW.',
          trailing: IconButton(
            onPressed: () => setState(_loadFromStatus),
            icon: const Icon(Icons.restore_rounded),
            tooltip: 'Przywróć dane sterownika',
          ),
        ),
        _ScheduleCard(
          title: 'Światło główne',
          icon: Icons.lightbulb_rounded,
          entry: light,
          profileEnabled: true,
          onChanged: (value) => setState(() => light = value),
        ),
        const SizedBox(height: 10),
        _ScheduleCard(
          title: 'Światło roślinne',
          icon: Icons.local_florist_rounded,
          entry: plant,
          profileEnabled: true,
          onChanged: (value) => setState(() => plant = value),
        ),
        const SizedBox(height: 10),
        _ScheduleCard(
          title: 'Filtr',
          icon: Icons.filter_alt_rounded,
          entry: filter,
          onChanged: (value) => setState(() => filter = value),
        ),
        const SizedBox(height: 10),
        _ScheduleCard(
          title: 'Napowietrzanie',
          icon: Icons.air_rounded,
          entry: air,
          onChanged: (value) => setState(() => air = value),
        ),
        const SizedBox(height: 10),
        Card(
          child: Padding(
            padding: const EdgeInsets.all(16),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                const Row(
                  children: [
                    Icon(Icons.thermostat_rounded),
                    SizedBox(width: 10),
                    Text(
                      'Grzałka',
                      style: TextStyle(
                        fontWeight: FontWeight.w800,
                        fontSize: 17,
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 10),
                DropdownButtonFormField<int>(
                  initialValue: heaterMode,
                  decoration: const InputDecoration(
                    labelText: 'Tryb termostatu',
                    border: OutlineInputBorder(),
                  ),
                  items: const [
                    DropdownMenuItem(
                      value: 0,
                      child: Text('Automatycznie według temperatury'),
                    ),
                    DropdownMenuItem(value: 1, child: Text('Zawsze wyłączona')),
                  ],
                  onChanged: (value) =>
                      setState(() => heaterMode = value ?? heaterMode),
                ),
              ],
            ),
          ),
        ),
        const SizedBox(height: 10),
        Card(
          child: Padding(
            padding: const EdgeInsets.all(16),
            child: Column(
              children: [
                LabeledSwitch(
                  label: 'Automatyczny karmnik',
                  subtitle: 'Jedna dawka dziennie zgodnie z logiką firmware.',
                  value: feederEnabled,
                  onChanged: (value) => setState(() => feederEnabled = value),
                ),
                ListTile(
                  contentPadding: EdgeInsets.zero,
                  leading: const Icon(Icons.set_meal_rounded),
                  title: const Text('Godzina karmienia'),
                  trailing: TextButton(
                    onPressed: feederEnabled
                        ? () async {
                            final selected = await showTimePicker(
                              context: context,
                              initialTime: feedTime,
                            );
                            if (selected != null && mounted) {
                              setState(() => feedTime = selected);
                            }
                          }
                        : null,
                    child: Text(
                      _timeText(feedTime),
                      style: const TextStyle(fontSize: 18),
                    ),
                  ),
                ),
              ],
            ),
          ),
        ),
        const SizedBox(height: 16),
        SaveButton(
          onPressed: _save,
          label: 'Zapisz kompletny harmonogram',
          busy: saving,
        ),
        if (statusMessage != null)
          Padding(
            padding: const EdgeInsets.only(top: 10),
            child: Text(statusMessage!),
          ),
      ],
    );
  }
}

class _ScheduleCard extends StatelessWidget {
  const _ScheduleCard({
    required this.title,
    required this.icon,
    required this.entry,
    required this.onChanged,
    this.profileEnabled = false,
  });

  final String title;
  final IconData icon;
  final _ScheduleEntry entry;
  final ValueChanged<_ScheduleEntry> onChanged;
  final bool profileEnabled;

  Future<void> _pickTime(BuildContext context, bool start) async {
    final selected = await showTimePicker(
      context: context,
      initialTime: start ? entry.start : entry.end,
    );
    if (selected != null) {
      onChanged(
        start ? entry.copyWith(start: selected) : entry.copyWith(end: selected),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    final scheduled = entry.mode == 0;
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          children: [
            Row(
              children: [
                Icon(icon),
                const SizedBox(width: 10),
                Expanded(
                  child: Text(
                    title,
                    style: const TextStyle(
                      fontWeight: FontWeight.w800,
                      fontSize: 17,
                    ),
                  ),
                ),
                SizedBox(
                  width: 190,
                  child: DropdownButtonFormField<int>(
                    initialValue: entry.mode,
                    decoration: const InputDecoration(
                      labelText: 'Tryb',
                      border: OutlineInputBorder(),
                    ),
                    items: const [
                      DropdownMenuItem(value: 0, child: Text('Harmonogram')),
                      DropdownMenuItem(value: 1, child: Text('Zawsze ON')),
                      DropdownMenuItem(value: 2, child: Text('Zawsze OFF')),
                    ],
                    onChanged: (value) =>
                        onChanged(entry.copyWith(mode: value ?? entry.mode)),
                  ),
                ),
              ],
            ),
            if (profileEnabled) ...[
              const SizedBox(height: 12),
              DropdownButtonFormField<String>(
                initialValue: entry.profile,
                decoration: const InputDecoration(
                  labelText: 'Profil Aquael Day & Night',
                  border: OutlineInputBorder(),
                ),
                items: const [
                  DropdownMenuItem(
                    value: 'day',
                    child: Text('DAY — pełne światło dzienne'),
                  ),
                  DropdownMenuItem(
                    value: 'daybreak',
                    child: Text('DAYBREAK — świt / zachód'),
                  ),
                  DropdownMenuItem(
                    value: 'night',
                    child: Text('NIGHT — światło nocne'),
                  ),
                ],
                onChanged: (value) =>
                    onChanged(entry.copyWith(profile: value ?? entry.profile)),
              ),
            ],
            const SizedBox(height: 12),
            Row(
              children: [
                Expanded(
                  child: OutlinedButton.icon(
                    onPressed: scheduled
                        ? () => _pickTime(context, true)
                        : null,
                    icon: const Icon(Icons.play_arrow_rounded),
                    label: Text('Start ${_timeText(entry.start)}'),
                  ),
                ),
                const SizedBox(width: 10),
                Expanded(
                  child: OutlinedButton.icon(
                    onPressed: scheduled
                        ? () => _pickTime(context, false)
                        : null,
                    icon: const Icon(Icons.stop_rounded),
                    label: Text('Koniec ${_timeText(entry.end)}'),
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

class _ScheduleEntry {
  const _ScheduleEntry({
    required this.mode,
    required this.start,
    required this.end,
    this.profile = 'day',
  });

  factory _ScheduleEntry.fromJson(
    JsonMap data, {
    required String fallbackStart,
    required String fallbackEnd,
    required int fallbackMode,
    String profile = 'day',
  }) {
    final modeName = data.text('mode');
    final mode = switch (modeName) {
      'always_on' => 1,
      'always_off' => 2,
      'schedule' => 0,
      _ => fallbackMode.clamp(0, 2),
    };
    return _ScheduleEntry(
      mode: mode,
      start: _parseTime(data.text('start', fallbackStart)),
      end: _parseTime(data.text('end', fallbackEnd)),
      profile: profile,
    );
  }

  final int mode;
  final TimeOfDay start;
  final TimeOfDay end;
  final String profile;

  _ScheduleEntry copyWith({
    int? mode,
    TimeOfDay? start,
    TimeOfDay? end,
    String? profile,
  }) {
    return _ScheduleEntry(
      mode: mode ?? this.mode,
      start: start ?? this.start,
      end: end ?? this.end,
      profile: profile ?? this.profile,
    );
  }
}

TimeOfDay _parseTime(String value) {
  final parts = value.split(':');
  if (parts.length != 2) return const TimeOfDay(hour: 0, minute: 0);
  return TimeOfDay(
    hour: (int.tryParse(parts[0]) ?? 0).clamp(0, 23),
    minute: (int.tryParse(parts[1]) ?? 0).clamp(0, 59),
  );
}

String _timeText(TimeOfDay value) =>
    '${value.hour.toString().padLeft(2, '0')}:${value.minute.toString().padLeft(2, '0')}';
