import 'data_access.dart';

enum OutputControlMode { automatic, forcedOn, forcedOff }

class OutputScheduleState {
  const OutputScheduleState({
    required this.mode,
    required this.start,
    required this.end,
    this.profile = 'day',
    this.profileCycle = false,
  });

  final OutputControlMode mode;
  final String start;
  final String end;
  final String profile;
  final bool profileCycle;

  String get modeLabel => switch (mode) {
    OutputControlMode.automatic => 'AUTO',
    OutputControlMode.forcedOn => 'WYMUSZONE ON',
    OutputControlMode.forcedOff => 'WYMUSZONE OFF',
  };

  String get windowLabel => switch (mode) {
    OutputControlMode.automatic => '$start–$end',
    OutputControlMode.forcedOn => 'Praca ciągła',
    OutputControlMode.forcedOff => 'Wyłączone trwale',
  };
}

OutputScheduleState readOutputSchedule(JsonMap status, String channel) {
  final schedules = status.section('schedules');
  final legacy = status.section('schedule');
  final config = switch (channel) {
    'light1' =>
      schedules.section('light1').isNotEmpty
          ? schedules.section('light1')
          : schedules.section('light'),
    'light2' =>
      schedules.section('light2').isNotEmpty
          ? schedules.section('light2')
          : schedules.section('plant_light'),
    _ => schedules.section(channel),
  };
  final fallbackMode = switch (channel) {
    'light1' => legacy.integer('lightMode'),
    'light2' => legacy.integer('plantLightMode'),
    'filter' => legacy.integer('filterMode'),
    'air' => legacy.integer('airMode'),
    _ => 0,
  };
  final fallbackStart = switch (channel) {
    'light1' => formatClock(
      legacy.integer('dayStartHour', 10),
      legacy.integer('dayStartMin'),
    ),
    'light2' => formatClock(
      legacy.integer('plantStartHour', 10),
      legacy.integer('plantStartMin'),
    ),
    'filter' => formatClock(
      legacy.integer('filterStartHour', 9),
      legacy.integer('filterStartMin', 30),
    ),
    'air' => formatClock(
      legacy.integer('airStartHour', 22),
      legacy.integer('airStartMin'),
    ),
    _ => '00:00',
  };
  final fallbackEnd = switch (channel) {
    'light1' => formatClock(
      legacy.integer('dayEndHour', 22),
      legacy.integer('dayEndMin'),
    ),
    'light2' => formatClock(
      legacy.integer('plantEndHour', 22),
      legacy.integer('plantEndMin'),
    ),
    'filter' => formatClock(
      legacy.integer('filterEndHour', 22),
      legacy.integer('filterEndMin', 30),
    ),
    'air' => formatClock(
      legacy.integer('airEndHour', 9),
      legacy.integer('airEndMin'),
    ),
    _ => '23:59',
  };
  final mode = switch (config.text('mode')) {
    'always_on' => OutputControlMode.forcedOn,
    'always_off' => OutputControlMode.forcedOff,
    'schedule' => OutputControlMode.automatic,
    _ => switch (fallbackMode.clamp(0, 2)) {
      1 => OutputControlMode.forcedOn,
      2 => OutputControlMode.forcedOff,
      _ => OutputControlMode.automatic,
    },
  };
  return OutputScheduleState(
    mode: mode,
    start: _safeClock(config.text('start', fallbackStart), fallbackStart),
    end: _safeClock(config.text('end', fallbackEnd), fallbackEnd),
    profile: config.text('profile', 'day'),
    profileCycle: config.flag('profileCycle'),
  );
}

bool heaterAutomationEnabled(JsonMap status) {
  final schedule = status.section('schedule');
  final temperature = status.section('temperature');
  if (schedule.containsKey('heaterMode')) {
    return schedule.integer('heaterMode') != 1;
  }
  if (temperature.containsKey('heaterMode')) {
    return temperature.integer('heaterMode') != 1;
  }
  return status.section('modules').flag('heater_enabled', true);
}

Map<String, Object?> buildScheduleModePatch(
  String channel,
  OutputControlMode mode,
) {
  final field = switch (channel) {
    'light1' => 'light1Mode',
    'light2' => 'light2Mode',
    'filter' => 'filterMode',
    'air' => 'aerationMode',
    _ => throw ArgumentError.value(
      channel,
      'channel',
      'Nieobsługiwany kanał harmonogramu.',
    ),
  };
  final encodedMode = switch (mode) {
    OutputControlMode.automatic => 0,
    OutputControlMode.forcedOn => 1,
    OutputControlMode.forcedOff => 2,
  };
  return <String, Object?>{field: encodedMode};
}

Map<String, Object?> buildSchedulePayload(
  JsonMap status, {
  String? overrideChannel,
  OutputControlMode? overrideMode,
}) {
  final light1 = readOutputSchedule(status, 'light1');
  final light2 = readOutputSchedule(status, 'light2');
  final filter = readOutputSchedule(status, 'filter');
  final air = readOutputSchedule(status, 'air');
  final feeding = status.section('feeding');

  int modeFor(String channel, OutputScheduleState value) {
    final selected = channel == overrideChannel && overrideMode != null
        ? overrideMode
        : value.mode;
    return switch (selected) {
      OutputControlMode.automatic => 0,
      OutputControlMode.forcedOn => 1,
      OutputControlMode.forcedOff => 2,
    };
  }

  return <String, Object?>{
    'lightMode': modeFor('light1', light1),
    'dayStart': light1.start,
    'dayEnd': light1.end,
    'lightStart': light1.start,
    'lightEnd': light1.end,
    'lightProfile': light1.profileCycle ? 'day' : light1.profile,
    'lightProfileCycle': light1.profileCycle,
    'light1Mode': modeFor('light1', light1),
    'light1Start': light1.start,
    'light1End': light1.end,
    'light1Profile': light1.profileCycle ? 'day' : light1.profile,
    'light1ProfileCycle': light1.profileCycle,
    'plantLightMode': modeFor('light2', light2),
    'plantLightStart': light2.start,
    'plantLightEnd': light2.end,
    'plantLightProfile': light2.profileCycle ? 'day' : light2.profile,
    'plantLightProfileCycle': light2.profileCycle,
    'light2Mode': modeFor('light2', light2),
    'light2Start': light2.start,
    'light2End': light2.end,
    'light2Profile': light2.profileCycle ? 'day' : light2.profile,
    'light2ProfileCycle': light2.profileCycle,
    'filterMode': modeFor('filter', filter),
    'filterOn': filter.start,
    'filterOff': filter.end,
    'aerationMode': modeFor('air', air),
    'airOn': air.start,
    'airOff': air.end,
    'heaterMode': heaterAutomationEnabled(status) ? 0 : 1,
    'heaterStart': '00:00',
    'heaterEnd': '23:59',
    'feedFreq': feeding.integer('freq', 1).clamp(0, 4),
    'feedTime': formatClock(
      feeding.integer('hour', 14),
      feeding.integer('minute'),
    ),
  };
}

String _safeClock(String value, String fallback) {
  final match = RegExp(r'^(\d{1,2}):(\d{2})$').firstMatch(value.trim());
  if (match == null) return fallback;
  final hour = int.tryParse(match.group(1)!);
  final minute = int.tryParse(match.group(2)!);
  if (hour == null ||
      minute == null ||
      hour < 0 ||
      hour > 23 ||
      minute < 0 ||
      minute > 59) {
    return fallback;
  }
  return formatClock(hour, minute);
}
