import 'dart:convert';

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
  bool _dirty = false;
  bool _remoteChangedWhileEditing = false;
  String _sourceFingerprint = '';

  @override
  void initState() {
    super.initState();
    _loadFromStatus();
  }

  void _loadFromStatus() {
    final status = widget.session.status;
    final schedules = status.section('schedules');
    final legacy = status.section('schedule');
    final lightSchedule = schedules.section('light1').isNotEmpty
        ? schedules.section('light1')
        : schedules.section('light');
    final light2Schedule = schedules.section('light2').isNotEmpty
        ? schedules.section('light2')
        : schedules.section('plant_light');
    light = _ScheduleEntry.fromJson(
      lightSchedule,
      fallbackStart: formatClock(
        legacy.integer('dayStartHour', 10),
        legacy.integer('dayStartMin'),
      ),
      fallbackEnd: formatClock(
        legacy.integer('dayEndHour', 22),
        legacy.integer('dayEndMin'),
      ),
      fallbackMode: legacy.integer('lightMode'),
      profile: lightSchedule.flag('profileCycle')
          ? 'cycle'
          : lightSchedule.text('profile', 'day'),
    );
    plant = _ScheduleEntry.fromJson(
      light2Schedule,
      fallbackStart: formatClock(
        legacy.integer('plantStartHour', 10),
        legacy.integer('plantStartMin'),
      ),
      fallbackEnd: formatClock(
        legacy.integer('plantEndHour', 22),
        legacy.integer('plantEndMin'),
      ),
      fallbackMode: legacy.integer('plantLightMode'),
      profile: light2Schedule.flag('profileCycle')
          ? 'cycle'
          : light2Schedule.text('profile', 'day'),
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
    final feeding = status.section('feeding');
    feederEnabled = feeding.integer('freq', 1) > 0;
    feedTime = TimeOfDay(
      hour: feeding.integer('hour', 14).clamp(0, 23),
      minute: feeding.integer('minute').clamp(0, 59),
    );
    _sourceFingerprint = _fingerprint(status);
    _dirty = false;
    _remoteChangedWhileEditing = false;
  }

  @override
  void didUpdateWidget(SchedulesView oldWidget) {
    super.didUpdateWidget(oldWidget);
    final currentFingerprint = _fingerprint(widget.session.status);
    if (currentFingerprint == _sourceFingerprint) return;
    if (_dirty || saving) {
      _remoteChangedWhileEditing = true;
    } else {
      _loadFromStatus();
    }
  }

  void _edit(VoidCallback change) {
    setState(() {
      change();
      _dirty = true;
      statusMessage = null;
    });
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
          'lightProfile': light.profile == 'cycle' ? 'day' : light.profile,
          'lightProfileCycle': light.profile == 'cycle',
          'light1Mode': light.mode,
          'light1Start': _timeText(light.start),
          'light1End': _timeText(light.end),
          'light1Profile': light.profile == 'cycle' ? 'day' : light.profile,
          'light1ProfileCycle': light.profile == 'cycle',
          'plantLightMode': plant.mode,
          'plantLightStart': _timeText(plant.start),
          'plantLightEnd': _timeText(plant.end),
          'plantLightProfile': plant.profile == 'cycle' ? 'day' : plant.profile,
          'plantLightProfileCycle': plant.profile == 'cycle',
          'light2Mode': plant.mode,
          'light2Start': _timeText(plant.start),
          'light2End': _timeText(plant.end),
          'light2Profile': plant.profile == 'cycle' ? 'day' : plant.profile,
          'light2ProfileCycle': plant.profile == 'cycle',
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
        confirmation: _remoteChangedWhileEditing
            ? 'Konfiguracja sterownika zmieniła się podczas edycji. '
                  'Zapisać ten kompletny plan i zastąpić nowsze wartości?'
            : 'Zapisać kompletny plan dobowy w sterowniku?',
      );
      if (mounted) {
        setState(() {
          _loadFromStatus();
          statusMessage = 'Harmonogramy zapisane i zsynchronizowane.';
        });
      }
    } on ControllerApiException catch (error) {
      if (mounted) setState(() => statusMessage = error.message);
    } finally {
      if (mounted) setState(() => saving = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final canEdit = widget.session.canIssueCommands;
    final hasStoredData = widget.session.hasStatusData;
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
        if (!canEdit) ...[
          StatusBanner(
            icon: Icons.visibility_rounded,
            title: widget.session.hasCachedSnapshot
                ? 'Harmonogram tylko do podglądu'
                : 'Brak zapisanego harmonogramu',
            message: widget.session.hasCachedSnapshot
                ? widget.session.commandBlockReason ??
                      'Połącz sterownik, aby edytować i zapisać plan dobowy.'
                : 'Widoczny układ pokazuje dostępne funkcje, ale wartości '
                      'formularza nie pochodzą ze sterownika.',
            isError: false,
          ),
          const SizedBox(height: 10),
        ],
        if (hasStoredData)
          _ScheduleTimeline(
            light: light,
            plant: plant,
            filter: filter,
            air: air,
            heaterEnabled: heaterMode == 0,
            feederEnabled: feederEnabled,
            feedTime: feedTime,
          )
        else
          const StatePanel.empty(
            title: 'Brak zapisanej osi czasu',
            message:
                'Po pierwszej synchronizacji zobaczysz tutaj pełny plan dobowy.',
            icon: Icons.calendar_month_outlined,
          ),
        const SizedBox(height: 10),
        _ScheduleCard(
          title: 'Światło 1',
          icon: Icons.lightbulb_rounded,
          entry: light,
          profileEnabled: true,
          enabled: canEdit,
          dataAvailable: hasStoredData,
          onChanged: (value) => _edit(() => light = value),
        ),
        const SizedBox(height: 10),
        _ScheduleCard(
          title: 'Światło 2',
          icon: Icons.lightbulb_outline_rounded,
          entry: plant,
          profileEnabled: true,
          enabled: canEdit,
          dataAvailable: hasStoredData,
          onChanged: (value) => _edit(() => plant = value),
        ),
        const SizedBox(height: 10),
        _ScheduleCard(
          title: 'Filtr',
          icon: Icons.filter_alt_rounded,
          entry: filter,
          enabled: canEdit,
          dataAvailable: hasStoredData,
          onChanged: (value) => _edit(() => filter = value),
        ),
        const SizedBox(height: 10),
        _ScheduleCard(
          title: 'Napowietrzanie',
          icon: Icons.air_rounded,
          entry: air,
          enabled: canEdit,
          dataAvailable: hasStoredData,
          onChanged: (value) => _edit(() => air = value),
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
                  initialValue: hasStoredData ? heaterMode : null,
                  isExpanded: true,
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
                  selectedItemBuilder: (context) => const [
                    Text('Automatycznie', overflow: TextOverflow.ellipsis),
                    Text('Zawsze wyłączona', overflow: TextOverflow.ellipsis),
                  ],
                  onChanged: canEdit
                      ? (value) => _edit(() => heaterMode = value ?? heaterMode)
                      : null,
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
                if (hasStoredData)
                  LabeledSwitch(
                    label: 'Automatyczny karmnik',
                    subtitle: 'Jedna dawka dziennie zgodnie z logiką firmware.',
                    value: feederEnabled,
                    onChanged: canEdit
                        ? (value) => _edit(() => feederEnabled = value)
                        : null,
                  )
                else
                  const ListTile(
                    contentPadding: EdgeInsets.zero,
                    leading: Icon(Icons.set_meal_outlined),
                    title: Text('Automatyczny karmnik'),
                    subtitle: Text('Brak zapisanego stanu'),
                  ),
                ListTile(
                  contentPadding: EdgeInsets.zero,
                  leading: const Icon(Icons.set_meal_rounded),
                  title: const Text('Godzina karmienia'),
                  trailing: TextButton(
                    onPressed: canEdit && feederEnabled
                        ? () async {
                            final selected = await showTimePicker(
                              context: context,
                              initialTime: feedTime,
                            );
                            if (selected != null && mounted) {
                              _edit(() => feedTime = selected);
                            }
                          }
                        : null,
                    child: Text(
                      hasStoredData ? _timeText(feedTime) : '—',
                      style: const TextStyle(fontSize: 18),
                    ),
                  ),
                ),
              ],
            ),
          ),
        ),
        const SizedBox(height: 16),
        if (_remoteChangedWhileEditing) ...[
          const StatusBanner(
            icon: Icons.sync_problem_rounded,
            title: 'Sterownik ma nowszą konfigurację',
            message:
                'Lokalny szkic nie został nadpisany. Przywróć dane '
                'sterownika albo świadomie potwierdź zapis całego planu.',
            isError: false,
          ),
          const SizedBox(height: 12),
        ],
        SaveButton(
          onPressed: canEdit ? _save : null,
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

  static String _fingerprint(JsonMap status) => jsonEncode([
    status.section('schedules'),
    status.section('schedule'),
    status.section('feeding'),
  ]);
}

class _ScheduleTimeline extends StatelessWidget {
  const _ScheduleTimeline({
    required this.light,
    required this.plant,
    required this.filter,
    required this.air,
    required this.heaterEnabled,
    required this.feederEnabled,
    required this.feedTime,
  });

  final _ScheduleEntry light;
  final _ScheduleEntry plant;
  final _ScheduleEntry filter;
  final _ScheduleEntry air;
  final bool heaterEnabled;
  final bool feederEnabled;
  final TimeOfDay feedTime;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return Card(
      child: Padding(
        padding: const EdgeInsets.fromLTRB(12, 12, 12, 14),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            const Text(
              'Harmonogram dobowy 24 h',
              style: TextStyle(fontWeight: FontWeight.w800, fontSize: 16),
            ),
            const SizedBox(height: 10),
            const _TimelineAxis(),
            _TimelineRow(
              label: 'Światło 1',
              entry: light,
              color: const Color(0xFFFFB020),
              lightProfiles: true,
            ),
            _TimelineRow(
              label: 'Światło 2',
              entry: plant,
              color: const Color(0xFFFACC15),
              lightProfiles: true,
            ),
            const Padding(
              padding: EdgeInsets.only(left: 82, bottom: 4),
              child: _ProfileLegend(),
            ),
            _TimelineRow(
              label: 'Filtr',
              entry: filter,
              color: const Color(0xFF38BDF8),
            ),
            _TimelineRow(
              label: 'Napowietrzanie',
              entry: air,
              color: const Color(0xFF8B5CF6),
            ),
            _TimelineRow(
              label: 'Grzałka',
              entry: _ScheduleEntry(
                mode: heaterEnabled ? 1 : 2,
                start: const TimeOfDay(hour: 0, minute: 0),
                end: const TimeOfDay(hour: 23, minute: 59),
              ),
              color: const Color(0xFFEF4444),
              statusOverride: heaterEnabled ? 'AUTOMATYKA 24H' : 'OFF',
            ),
            const SizedBox(height: 4),
            Row(
              children: [
                SizedBox(
                  width: 82,
                  child: Text(
                    'Karmnik',
                    style: Theme.of(context).textTheme.labelSmall,
                  ),
                ),
                Expanded(
                  child: _FeederTimeline(
                    enabled: feederEnabled,
                    time: feedTime,
                    color: colors.tertiary,
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

class _ProfileLegend extends StatelessWidget {
  const _ProfileLegend();

  @override
  Widget build(BuildContext context) {
    return const Wrap(
      spacing: 10,
      runSpacing: 3,
      children: [
        _ProfileLegendItem(label: 'DAYBREAK', color: Color(0xFFF59E0B)),
        _ProfileLegendItem(label: 'DAY', color: Color(0xFFFACC15)),
        _ProfileLegendItem(label: 'NIGHT', color: Color(0xFF3B82F6)),
      ],
    );
  }
}

class _ProfileLegendItem extends StatelessWidget {
  const _ProfileLegendItem({required this.label, required this.color});

  final String label;
  final Color color;

  @override
  Widget build(BuildContext context) {
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        ColoredBox(color: color, child: SizedBox.square(dimension: 7)),
        SizedBox(width: 3),
        Text(label, style: TextStyle(fontSize: 8, fontWeight: FontWeight.w700)),
      ],
    );
  }
}

class _TimelineAxis extends StatelessWidget {
  const _TimelineAxis();

  @override
  Widget build(BuildContext context) {
    final style = Theme.of(context).textTheme.labelSmall?.copyWith(
      color: Theme.of(context).colorScheme.onSurfaceVariant,
      fontSize: 10,
    );
    return Padding(
      padding: const EdgeInsets.only(left: 82, bottom: 3),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.spaceBetween,
        children: [
          Text('00', style: style),
          Text('06', style: style),
          Text('12', style: style),
          Text('18', style: style),
          Text('24', style: style),
        ],
      ),
    );
  }
}

class _TimelineRow extends StatelessWidget {
  const _TimelineRow({
    required this.label,
    required this.entry,
    required this.color,
    this.statusOverride,
    this.lightProfiles = false,
  });

  final String label;
  final _ScheduleEntry entry;
  final Color color;
  final String? statusOverride;
  final bool lightProfiles;

  @override
  Widget build(BuildContext context) {
    final modeText = switch (entry.mode) {
      1 => 'ON 24H',
      2 => 'OFF',
      _ => '${_timeText(entry.start)}–${_timeText(entry.end)}',
    };
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 3),
      child: Row(
        children: [
          SizedBox(
            width: 82,
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  label,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: const TextStyle(
                    fontSize: 11,
                    fontWeight: FontWeight.w700,
                  ),
                ),
                Text(
                  statusOverride ?? modeText,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: Theme.of(context).textTheme.labelSmall?.copyWith(
                    fontSize: 8,
                    color: Theme.of(context).colorScheme.onSurfaceVariant,
                  ),
                ),
              ],
            ),
          ),
          Expanded(
            child: _TimelineTrack(
              entry: entry,
              color: color,
              lightProfiles: lightProfiles,
            ),
          ),
        ],
      ),
    );
  }
}

class _TimelineTrack extends StatelessWidget {
  const _TimelineTrack({
    required this.entry,
    required this.color,
    this.lightProfiles = false,
  });

  final _ScheduleEntry entry;
  final Color color;
  final bool lightProfiles;

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      height: 24,
      child: LayoutBuilder(
        builder: (context, constraints) {
          final segments = _segments();
          final activeColor = lightProfiles
              ? _profileColor(entry.profile)
              : color;
          return ClipRRect(
            borderRadius: BorderRadius.circular(5),
            child: Stack(
              children: [
                Positioned.fill(
                  child: ColoredBox(
                    color: Theme.of(
                      context,
                    ).colorScheme.surfaceContainerHighest,
                  ),
                ),
                for (final fraction in const [0.25, 0.5, 0.75])
                  Positioned(
                    left: constraints.maxWidth * fraction,
                    top: 0,
                    bottom: 0,
                    child: Container(
                      width: 0.7,
                      color: Theme.of(context).colorScheme.outlineVariant,
                    ),
                  ),
                if (lightProfiles &&
                    entry.mode == 0 &&
                    entry.profile == 'cycle') ...[
                  _profileSegment(
                    constraints.maxWidth,
                    600,
                    30,
                    const Color(0xFFF59E0B),
                  ),
                  _profileSegment(
                    constraints.maxWidth,
                    630,
                    570,
                    const Color(0xFFFACC15),
                  ),
                  _profileSegment(
                    constraints.maxWidth,
                    1200,
                    60,
                    const Color(0xFFF59E0B),
                  ),
                  _profileSegment(
                    constraints.maxWidth,
                    1260,
                    60,
                    const Color(0xFF3B82F6),
                  ),
                ] else
                  for (final segment in segments)
                    Positioned(
                      left: constraints.maxWidth * segment.$1,
                      width: constraints.maxWidth * segment.$2,
                      top: 2,
                      bottom: 2,
                      child: ColoredBox(color: activeColor),
                    ),
                if (entry.mode == 2)
                  const Center(
                    child: Text(
                      'OFF',
                      style: TextStyle(
                        fontSize: 9,
                        fontWeight: FontWeight.w800,
                      ),
                    ),
                  ),
              ],
            ),
          );
        },
      ),
    );
  }

  Positioned _profileSegment(
    double width,
    int startMinute,
    int durationMinutes,
    Color segmentColor,
  ) {
    return Positioned(
      left: width * startMinute / 1440,
      width: width * durationMinutes / 1440,
      top: 2,
      bottom: 2,
      child: ColoredBox(color: segmentColor),
    );
  }

  Color _profileColor(String profile) => switch (profile) {
    'daybreak' => const Color(0xFFF59E0B),
    'night' => const Color(0xFF3B82F6),
    _ => const Color(0xFFFACC15),
  };

  List<(double, double)> _segments() {
    if (entry.mode == 1) return const [(0, 1)];
    if (entry.mode == 2) return const [];
    final start = (entry.start.hour * 60 + entry.start.minute) / 1440;
    final end = (entry.end.hour * 60 + entry.end.minute) / 1440;
    if ((start - end).abs() < 0.0001) return const [];
    if (end > start) return [(start, end - start)];
    return [(0, end), (start, 1 - start)];
  }
}

class _FeederTimeline extends StatelessWidget {
  const _FeederTimeline({
    required this.enabled,
    required this.time,
    required this.color,
  });

  final bool enabled;
  final TimeOfDay time;
  final Color color;

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      height: 28,
      child: LayoutBuilder(
        builder: (context, constraints) {
          final position =
              constraints.maxWidth * (time.hour * 60 + time.minute) / 1440;
          return Stack(
            clipBehavior: Clip.none,
            children: [
              Positioned(
                left: 0,
                right: 0,
                top: 13,
                child: Container(
                  height: 2,
                  color: Theme.of(context).colorScheme.outlineVariant,
                ),
              ),
              if (enabled)
                Positioned(
                  left: (position - 3).clamp(0, constraints.maxWidth - 6),
                  top: 5,
                  child: Container(width: 6, height: 18, color: color),
                ),
              Center(
                child: Text(
                  enabled ? _timeText(time) : 'OFF',
                  style: const TextStyle(
                    fontSize: 9,
                    fontWeight: FontWeight.w800,
                  ),
                ),
              ),
            ],
          );
        },
      ),
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
    this.enabled = true,
    required this.dataAvailable,
  });

  final String title;
  final IconData icon;
  final _ScheduleEntry entry;
  final ValueChanged<_ScheduleEntry> onChanged;
  final bool profileEnabled;
  final bool enabled;
  final bool dataAvailable;

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
    final scheduled = dataAvailable && entry.mode == 0;
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          children: [
            LayoutBuilder(
              builder: (context, constraints) {
                final heading = Row(
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
                  ],
                );
                final mode = DropdownButtonFormField<int>(
                  initialValue: dataAvailable ? entry.mode : null,
                  isExpanded: true,
                  decoration: const InputDecoration(
                    labelText: 'Tryb',
                    border: OutlineInputBorder(),
                  ),
                  items: const [
                    DropdownMenuItem(value: 0, child: Text('Harmonogram')),
                    DropdownMenuItem(value: 1, child: Text('Zawsze ON')),
                    DropdownMenuItem(value: 2, child: Text('Zawsze OFF')),
                  ],
                  onChanged: enabled
                      ? (value) =>
                            onChanged(entry.copyWith(mode: value ?? entry.mode))
                      : null,
                );
                if (constraints.maxWidth < 520) {
                  return Column(
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: [heading, const SizedBox(height: 10), mode],
                  );
                }
                return Row(
                  children: [
                    Expanded(child: heading),
                    const SizedBox(width: 12),
                    SizedBox(width: 190, child: mode),
                  ],
                );
              },
            ),
            if (profileEnabled) ...[
              const SizedBox(height: 12),
              DropdownButtonFormField<String>(
                initialValue: dataAvailable ? entry.profile : null,
                isExpanded: true,
                decoration: const InputDecoration(
                  labelText: 'Profil Aquael Day & Night',
                  border: OutlineInputBorder(),
                ),
                items: const [
                  DropdownMenuItem(
                    value: 'cycle',
                    child: Text('AUTO — DAYBREAK → DAY → NIGHT'),
                  ),
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
                selectedItemBuilder: (context) => const [
                  Text('AUTO — 3 tryby', overflow: TextOverflow.ellipsis),
                  Text('DAY', overflow: TextOverflow.ellipsis),
                  Text('DAYBREAK', overflow: TextOverflow.ellipsis),
                  Text('NIGHT', overflow: TextOverflow.ellipsis),
                ],
                onChanged: enabled
                    ? (value) => onChanged(
                        entry.copyWith(profile: value ?? entry.profile),
                      )
                    : null,
              ),
            ],
            const SizedBox(height: 12),
            LayoutBuilder(
              builder: (context, constraints) {
                final startButton = OutlinedButton.icon(
                  onPressed: enabled && scheduled
                      ? () => _pickTime(context, true)
                      : null,
                  icon: const Icon(Icons.play_arrow_rounded),
                  label: Text(
                    dataAvailable
                        ? 'Start ${_timeText(entry.start)}'
                        : 'Start —',
                  ),
                );
                final endButton = OutlinedButton.icon(
                  onPressed: enabled && scheduled
                      ? () => _pickTime(context, false)
                      : null,
                  icon: const Icon(Icons.stop_rounded),
                  label: Text(
                    dataAvailable
                        ? 'Koniec ${_timeText(entry.end)}'
                        : 'Koniec —',
                  ),
                );
                if (constraints.maxWidth < 400) {
                  return Column(
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: [
                      startButton,
                      const SizedBox(height: 8),
                      endButton,
                    ],
                  );
                }
                return Row(
                  children: [
                    Expanded(child: startButton),
                    const SizedBox(width: 10),
                    Expanded(child: endButton),
                  ],
                );
              },
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
      profile: data.flag('profileCycle') ? 'cycle' : profile,
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
