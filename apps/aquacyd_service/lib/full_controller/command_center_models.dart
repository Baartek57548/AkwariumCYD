import 'dart:collection';

import 'controller_session.dart';
import 'data_access.dart';

enum CommandCenterSafetyState { ok, warning, critical, offline, service }

enum CommandCenterCapability { wifi, bleV1, bleV2, offline, development }

enum CommandCenterAlarmSeverity { warning, critical }

enum CommandCenterAlarmKind {
  temperatureHigh,
  temperatureLow,
  phOutOfRange,
  waterLevelLow,
  leak,
  supplyLow,
  waterTimeout,
  unknown,
}

enum CommandCenterSensorKind {
  temperature,
  ph,
  conductivity,
  ambientLight,
  waterLevel,
  leak,
  flow,
  supplyVoltage,
  ioBus,
}

enum CommandCenterSensorState { ok, warning, critical, unavailable, disabled }

enum CommandCenterOutputKind {
  light1,
  light2,
  filter,
  heater,
  co2,
  aeration,
  waterDosing,
  feeder,
}

enum CommandCenterPhysicalState { on, off, unknown }

enum CommandCenterControlMode { auto, forcedOn, forcedOff, disabled }

enum CommandCenterScheduleEventKind { turnOn, turnOff, feed }

final class CommandCenterCapabilities {
  const CommandCenterCapabilities._(this.primary);

  final CommandCenterCapability primary;

  bool get isWifi => primary == CommandCenterCapability.wifi;
  bool get isBluetooth =>
      primary == CommandCenterCapability.bleV1 ||
      primary == CommandCenterCapability.bleV2;
  bool get isLegacyBluetooth => primary == CommandCenterCapability.bleV1;
  bool get isOffline => primary == CommandCenterCapability.offline;
  bool get isDevelopment => primary == CommandCenterCapability.development;
  bool get supportsExtendedStatus => primary != CommandCenterCapability.bleV1;
  bool get supportsSchedules => primary != CommandCenterCapability.bleV1;
  bool get supportsDiagnostics =>
      primary == CommandCenterCapability.wifi ||
      primary == CommandCenterCapability.development;
  bool get supportsFirmwareUpdate =>
      primary == CommandCenterCapability.wifi ||
      primary == CommandCenterCapability.development;

  String get label => switch (primary) {
    CommandCenterCapability.wifi => 'Wi‑Fi',
    CommandCenterCapability.bleV1 => 'BLE v1',
    CommandCenterCapability.bleV2 => 'BLE v2',
    CommandCenterCapability.offline => 'Offline',
    CommandCenterCapability.development => 'DEV',
  };

  static CommandCenterCapabilities project(
    JsonMap status,
    ControllerSessionKind sessionKind, {
    bool offline = false,
  }) {
    final primary = offline
        ? CommandCenterCapability.offline
        : switch (sessionKind) {
            ControllerSessionKind.wifi => CommandCenterCapability.wifi,
            ControllerSessionKind.development =>
              CommandCenterCapability.development,
            ControllerSessionKind.offline => CommandCenterCapability.offline,
            ControllerSessionKind.bluetooth =>
              _isLegacyBle(status)
                  ? CommandCenterCapability.bleV1
                  : CommandCenterCapability.bleV2,
          };
    return CommandCenterCapabilities._(primary);
  }

  static bool _isLegacyBle(JsonMap status) {
    final mode = _safeText(status['mode']).toUpperCase();
    if (mode == 'BLE_V1' || mode == 'BLE1' || mode == 'BLE_LEGACY') {
      return true;
    }

    final ble = _safeMap(status['ble']);
    final protocolVersion =
        _safeInt(ble['protocolVersion']) ??
        _safeInt(ble['protocol_version']) ??
        _safeInt(status['bleProtocolVersion']);
    return protocolVersion != null && protocolVersion <= 1;
  }
}

final class CommandCenterAlarm {
  const CommandCenterAlarm({
    required this.kind,
    required this.severity,
    required this.title,
    required this.message,
    this.rawFlags = 0,
  });

  final CommandCenterAlarmKind kind;
  final CommandCenterAlarmSeverity severity;
  final String title;
  final String message;
  final int rawFlags;
}

final class CommandCenterSensor {
  const CommandCenterSensor({
    required this.kind,
    required this.label,
    required this.displayValue,
    required this.state,
    required this.valid,
    required this.enabled,
    this.numericValue,
    this.binaryValue,
    this.unit,
    this.detail,
  });

  final CommandCenterSensorKind kind;
  final String label;
  final String displayValue;
  final String? unit;
  final String? detail;
  final double? numericValue;
  final bool? binaryValue;
  final CommandCenterSensorState state;
  final bool valid;
  final bool enabled;

  bool get isAvailable => enabled && valid;
}

final class CommandCenterOutput {
  const CommandCenterOutput({
    required this.kind,
    required this.label,
    required this.physicalState,
    required this.controlMode,
    required this.available,
  });

  final CommandCenterOutputKind kind;
  final String label;
  final CommandCenterPhysicalState physicalState;
  final CommandCenterControlMode controlMode;
  final bool available;

  bool get isEnergized => physicalState == CommandCenterPhysicalState.on;
  bool get canBeControlled =>
      available && controlMode != CommandCenterControlMode.disabled;
}

final class CommandCenterScheduleEvent {
  const CommandCenterScheduleEvent({
    required this.kind,
    required this.label,
    required this.scheduledAt,
    required this.referenceTime,
    this.outputKind,
  });

  final CommandCenterScheduleEventKind kind;
  final CommandCenterOutputKind? outputKind;
  final String label;
  final DateTime scheduledAt;
  final DateTime referenceTime;

  Duration get timeUntil => scheduledAt.difference(referenceTime);
}

final class CommandCenterSafetySummary {
  const CommandCenterSafetySummary({
    required this.state,
    required this.title,
    required this.message,
    required this.activeAlarmCount,
  });

  final CommandCenterSafetyState state;
  final String title;
  final String message;
  final int activeAlarmCount;

  bool get requiresAttention =>
      state == CommandCenterSafetyState.warning ||
      state == CommandCenterSafetyState.critical ||
      state == CommandCenterSafetyState.offline;
}

/// Immutable, display-ready projection of the controller's mutable JSON state.
///
/// Missing, malformed and non-finite values are represented as unavailable
/// instead of silently becoming zero or `false`. This keeps a partial BLE
/// payload or a damaged network response from looking like a safe state.
final class CommandCenterModel {
  CommandCenterModel._({
    required this.safety,
    required this.capabilities,
    required List<CommandCenterSensor> sensors,
    required List<CommandCenterOutput> outputs,
    required List<CommandCenterAlarm> activeAlarms,
    required this.nextScheduleEvent,
  }) : sensors = UnmodifiableListView(sensors),
       outputs = UnmodifiableListView(outputs),
       activeAlarms = UnmodifiableListView(activeAlarms);

  factory CommandCenterModel.fromStatus(
    JsonMap status,
    ControllerSessionKind sessionKind, {
    bool? connected,
    bool offline = false,
    DateTime? now,
  }) {
    final isConnected = connected ?? status.isNotEmpty;
    final capabilities = CommandCenterCapabilities.project(
      status,
      sessionKind,
      offline: offline,
    );
    final alarms = _projectAlarms(status);
    final sensors = _projectSensors(status, alarms);
    final outputs = _projectOutputs(status);
    final referenceTime = _controllerTime(status, now ?? DateTime.now());
    final nextScheduleEvent = _projectNextScheduleEvent(
      status,
      outputs,
      referenceTime,
    );
    final safety = _projectSafety(
      status: status,
      connected: isConnected,
      capabilities: capabilities,
      alarms: alarms,
      sensors: sensors,
    );

    return CommandCenterModel._(
      safety: safety,
      capabilities: capabilities,
      sensors: sensors,
      outputs: outputs,
      activeAlarms: alarms,
      nextScheduleEvent: nextScheduleEvent,
    );
  }

  final CommandCenterSafetySummary safety;
  final CommandCenterCapabilities capabilities;
  final UnmodifiableListView<CommandCenterSensor> sensors;
  final UnmodifiableListView<CommandCenterOutput> outputs;
  final UnmodifiableListView<CommandCenterAlarm> activeAlarms;
  final CommandCenterScheduleEvent? nextScheduleEvent;

  CommandCenterSensor sensor(CommandCenterSensorKind kind) =>
      sensors.firstWhere((sensor) => sensor.kind == kind);

  CommandCenterOutput output(CommandCenterOutputKind kind) =>
      outputs.firstWhere((output) => output.kind == kind);
}

List<CommandCenterAlarm> _projectAlarms(JsonMap status) {
  final alarms = _safeMap(status['alarms']);
  final flags = (_safeInt(alarms['flags']) ?? 0).clamp(0, 0x7fffffff);
  final result = <CommandCenterAlarm>[];

  void addKnown({
    required int mask,
    required String key,
    required CommandCenterAlarmKind kind,
    required CommandCenterAlarmSeverity severity,
    required String title,
    required String message,
  }) {
    if (_safeBool(alarms[key]) == true || flags & mask != 0) {
      result.add(
        CommandCenterAlarm(
          kind: kind,
          severity: severity,
          title: title,
          message: message,
          rawFlags: mask,
        ),
      );
    }
  }

  addKnown(
    mask: 1,
    key: 'temperatureHigh',
    kind: CommandCenterAlarmKind.temperatureHigh,
    severity: CommandCenterAlarmSeverity.critical,
    title: 'Temperatura za wysoka',
    message: 'Temperatura wody przekroczyła bezpieczny zakres.',
  );
  addKnown(
    mask: 2,
    key: 'temperatureLow',
    kind: CommandCenterAlarmKind.temperatureLow,
    severity: CommandCenterAlarmSeverity.critical,
    title: 'Temperatura za niska',
    message: 'Temperatura wody spadła poniżej bezpiecznego zakresu.',
  );
  addKnown(
    mask: 4,
    key: 'phOutOfRange',
    kind: CommandCenterAlarmKind.phOutOfRange,
    severity: CommandCenterAlarmSeverity.warning,
    title: 'pH poza zakresem',
    message: 'Odczyt pH wymaga weryfikacji lub korekty dozowania.',
  );
  addKnown(
    mask: 8,
    key: 'waterLevelLow',
    kind: CommandCenterAlarmKind.waterLevelLow,
    severity: CommandCenterAlarmSeverity.critical,
    title: 'Niski poziom wody',
    message: 'Poziom wody jest zbyt niski dla bezpiecznej pracy urządzeń.',
  );
  addKnown(
    mask: 16,
    key: 'leak',
    kind: CommandCenterAlarmKind.leak,
    severity: CommandCenterAlarmSeverity.critical,
    title: 'Wykryto wyciek',
    message: 'Czujnik wykrył wodę poza zbiornikiem.',
  );
  addKnown(
    mask: 32,
    key: 'supplyLow',
    kind: CommandCenterAlarmKind.supplyLow,
    severity: CommandCenterAlarmSeverity.warning,
    title: 'Niskie napięcie zasilania',
    message: 'Zasilanie sterownika jest poniżej oczekiwanego poziomu.',
  );

  if (_safeBool(_safeMap(status['water'])['timeoutLatched']) == true) {
    result.add(
      const CommandCenterAlarm(
        kind: CommandCenterAlarmKind.waterTimeout,
        severity: CommandCenterAlarmSeverity.critical,
        title: 'Przekroczony czas dolewania',
        message: 'Automatyczne dolewanie zostało zatrzymane przez limit czasu.',
      ),
    );
  }

  const knownMask = 1 | 2 | 4 | 8 | 16 | 32;
  final unknownFlags = flags & ~knownMask;
  if (unknownFlags != 0) {
    result.add(
      CommandCenterAlarm(
        kind: CommandCenterAlarmKind.unknown,
        severity: CommandCenterAlarmSeverity.warning,
        title: 'Nieznany alarm sterownika',
        message: 'Sterownik zgłosił dodatkową flagę alarmową: $unknownFlags.',
        rawFlags: unknownFlags,
      ),
    );
  }

  result.sort((left, right) {
    final leftRank = left.severity == CommandCenterAlarmSeverity.critical
        ? 0
        : 1;
    final rightRank = right.severity == CommandCenterAlarmSeverity.critical
        ? 0
        : 1;
    return leftRank.compareTo(rightRank);
  });
  return result;
}

List<CommandCenterSensor> _projectSensors(
  JsonMap status,
  List<CommandCenterAlarm> alarms,
) {
  final sensors = _safeMap(status['sensors']);
  final modules = _safeMap(status['modules']);
  final alarmKinds = alarms.map((alarm) => alarm.kind).toSet();

  final temperature = _numericSensor(
    kind: CommandCenterSensorKind.temperature,
    label: 'Temperatura',
    value: _safeDouble(sensors['temp_c']),
    valid: _safeBool(sensors['temp_valid']) == true,
    enabled: true,
    unit: '°C',
    fractionDigits: 1,
    alarmState:
        alarmKinds.contains(CommandCenterAlarmKind.temperatureHigh) ||
            alarmKinds.contains(CommandCenterAlarmKind.temperatureLow)
        ? CommandCenterSensorState.critical
        : null,
  );
  final ph = _numericSensor(
    kind: CommandCenterSensorKind.ph,
    label: 'Odczyn pH',
    value: _safeDouble(sensors['ph']),
    valid: _safeBool(sensors['ph_valid']) == true,
    enabled: !_explicitlyDisabled(modules, 'ph_sensor_enabled'),
    fractionDigits: 2,
    alarmState: alarmKinds.contains(CommandCenterAlarmKind.phOutOfRange)
        ? CommandCenterSensorState.warning
        : null,
  );
  final conductivity = _numericSensor(
    kind: CommandCenterSensorKind.conductivity,
    label: 'Przewodność EC',
    value: _safeDouble(sensors['ec']),
    valid: _safeBool(sensors['ec_valid']) == true,
    enabled: !_explicitlyDisabled(modules, 'ec_enabled'),
    unit: 'µS/cm',
    fractionDigits: 0,
  );

  final ldrValue = _safeDouble(sensors['ldr']);
  final ldrValid = _safeBool(sensors['ldr_valid']) == true && ldrValue != null;
  final ambientLight = CommandCenterSensor(
    kind: CommandCenterSensorKind.ambientLight,
    label: 'Światło otoczenia',
    displayValue: ldrValid
        ? '${((ldrValue.clamp(0, 4095) / 4095) * 100).round()}'
        : '—',
    unit: ldrValid ? '%' : null,
    detail: ldrValid ? 'ADC ${ldrValue.round().clamp(0, 4095)} / 4095' : null,
    numericValue: ldrValid ? ldrValue : null,
    state: ldrValid
        ? CommandCenterSensorState.ok
        : CommandCenterSensorState.unavailable,
    valid: ldrValid,
    enabled: true,
  );

  final waterLevel = _binarySensor(
    kind: CommandCenterSensorKind.waterLevel,
    label: 'Poziom wody',
    value: _safeBool(sensors['water_level_high']),
    valid: _safeBool(sensors['water_level_valid']) == true,
    enabled: !_explicitlyDisabled(modules, 'water_level_enabled'),
    positiveText: 'Prawidłowy',
    negativeText: 'Niski',
    alarmState: alarmKinds.contains(CommandCenterAlarmKind.waterLevelLow)
        ? CommandCenterSensorState.critical
        : null,
  );
  final leak = _binarySensor(
    kind: CommandCenterSensorKind.leak,
    label: 'Wyciek',
    value: _safeBool(sensors['leak_detected']),
    valid: _safeBool(sensors['leak_valid']) == true,
    enabled: !_explicitlyDisabled(modules, 'leak_enabled'),
    positiveText: 'Wykryto',
    negativeText: 'Brak',
    alarmState: alarmKinds.contains(CommandCenterAlarmKind.leak)
        ? CommandCenterSensorState.critical
        : null,
  );
  final flow = _binarySensor(
    kind: CommandCenterSensorKind.flow,
    label: 'Przepływ',
    value: _safeBool(sensors['flow_active']),
    valid: _safeBool(sensors['flow_valid']) == true,
    enabled: !_explicitlyDisabled(modules, 'flow_enabled'),
    positiveText: 'Aktywny',
    negativeText: 'Brak',
  );
  final supplyVoltage = _numericSensor(
    kind: CommandCenterSensorKind.supplyVoltage,
    label: 'Napięcie zasilania',
    value: _safeDouble(sensors['supply_voltage']),
    valid: _safeBool(sensors['supply_valid']) == true,
    enabled: true,
    unit: 'V',
    fractionDigits: 2,
    alarmState: alarmKinds.contains(CommandCenterAlarmKind.supplyLow)
        ? CommandCenterSensorState.warning
        : null,
  );

  final mcpPresent = _safeBool(sensors['mcp_present']);
  final mcpValid =
      _safeBool(sensors['mcp_valid']) ?? _safeBool(sensors['mcp_ok']);
  final ioValid = mcpPresent == true && mcpValid == true;
  final ioBus = CommandCenterSensor(
    kind: CommandCenterSensorKind.ioBus,
    label: 'Magistrala wejść/wyjść',
    displayValue: ioValid ? 'Online' : '—',
    detail: mcpPresent == false
        ? 'Ekspander MCP23017 nie został wykryty'
        : ioValid
        ? 'MCP23017 odpowiada prawidłowo'
        : 'Brak wiarygodnej odpowiedzi MCP23017',
    state: ioValid
        ? CommandCenterSensorState.ok
        : CommandCenterSensorState.unavailable,
    valid: ioValid,
    enabled: true,
  );

  return [
    temperature,
    ph,
    conductivity,
    ambientLight,
    waterLevel,
    leak,
    flow,
    supplyVoltage,
    ioBus,
  ];
}

CommandCenterSensor _numericSensor({
  required CommandCenterSensorKind kind,
  required String label,
  required double? value,
  required bool valid,
  required bool enabled,
  required int fractionDigits,
  String? unit,
  CommandCenterSensorState? alarmState,
}) {
  final available = enabled && valid && value != null;
  return CommandCenterSensor(
    kind: kind,
    label: label,
    displayValue: available ? value.toStringAsFixed(fractionDigits) : '—',
    unit: available ? unit : null,
    numericValue: available ? value : null,
    state: !enabled
        ? CommandCenterSensorState.disabled
        : !available
        ? CommandCenterSensorState.unavailable
        : alarmState ?? CommandCenterSensorState.ok,
    valid: available,
    enabled: enabled,
  );
}

CommandCenterSensor _binarySensor({
  required CommandCenterSensorKind kind,
  required String label,
  required bool? value,
  required bool valid,
  required bool enabled,
  required String positiveText,
  required String negativeText,
  CommandCenterSensorState? alarmState,
}) {
  final available = enabled && valid && value != null;
  return CommandCenterSensor(
    kind: kind,
    label: label,
    displayValue: available
        ? value
              ? positiveText
              : negativeText
        : '—',
    binaryValue: available ? value : null,
    state: !enabled
        ? CommandCenterSensorState.disabled
        : !available
        ? CommandCenterSensorState.unavailable
        : alarmState ?? CommandCenterSensorState.ok,
    valid: available,
    enabled: enabled,
  );
}

List<CommandCenterOutput> _projectOutputs(JsonMap status) {
  final modules = _safeMap(status['modules']);
  final relays = _safeMap(status['relays']);
  final schedules = _safeMap(status['schedules']);
  final legacySchedule = _safeMap(status['schedule']);
  final temperature = _safeMap(status['temperature']);
  final feeding = _safeMap(status['feeding']);

  return _outputDefinitions
      .map((definition) {
        final physicalValue = _firstBool([
          for (final key in definition.relayKeys) relays[key],
          for (final key in definition.moduleStateKeys) modules[key],
          if (definition.kind == CommandCenterOutputKind.feeder)
            feeding['active'],
        ]);
        final schedule = _firstNonEmptyMap(
          definition.scheduleKeys.map((key) => schedules[key]),
        );
        final hasModeData =
            schedule.isNotEmpty ||
            (definition.legacyModeKey != null &&
                legacySchedule.containsKey(definition.legacyModeKey)) ||
            (definition.kind == CommandCenterOutputKind.heater &&
                temperature.containsKey('heaterMode'));
        final hasEnabledData =
            definition.enabledKey != null &&
            modules.containsKey(definition.enabledKey);
        final available =
            physicalValue != null || hasModeData || hasEnabledData;
        final enabled = definition.enabledKey == null
            ? true
            : _safeBool(modules[definition.enabledKey]) ?? true;
        final mode = _outputControlMode(
          definition: definition,
          schedule: schedule,
          legacySchedule: legacySchedule,
          temperature: temperature,
          enabled: enabled,
          available: available,
        );

        return CommandCenterOutput(
          kind: definition.kind,
          label: definition.label,
          physicalState: switch (physicalValue) {
            true => CommandCenterPhysicalState.on,
            false => CommandCenterPhysicalState.off,
            null => CommandCenterPhysicalState.unknown,
          },
          controlMode: mode,
          available: available,
        );
      })
      .toList(growable: false);
}

CommandCenterControlMode _outputControlMode({
  required _OutputDefinition definition,
  required JsonMap schedule,
  required JsonMap legacySchedule,
  required JsonMap temperature,
  required bool enabled,
  required bool available,
}) {
  if (!available || !enabled) {
    return CommandCenterControlMode.disabled;
  }

  if (definition.kind == CommandCenterOutputKind.heater) {
    final heaterMode =
        _safeInt(temperature['heaterMode']) ??
        _safeInt(legacySchedule['heaterMode']);
    return heaterMode == 1
        ? CommandCenterControlMode.disabled
        : CommandCenterControlMode.auto;
  }

  if (definition.kind == CommandCenterOutputKind.co2 ||
      definition.kind == CommandCenterOutputKind.waterDosing ||
      definition.kind == CommandCenterOutputKind.feeder) {
    return CommandCenterControlMode.auto;
  }

  final modernMode = _safeText(schedule['mode']).toLowerCase();
  if (modernMode == 'always_on' || modernMode == 'on') {
    return CommandCenterControlMode.forcedOn;
  }
  if (modernMode == 'always_off' || modernMode == 'off') {
    return CommandCenterControlMode.forcedOff;
  }
  if (modernMode == 'schedule' || modernMode == 'auto') {
    return CommandCenterControlMode.auto;
  }

  final legacyMode = definition.legacyModeKey == null
      ? null
      : _safeInt(legacySchedule[definition.legacyModeKey]);
  return switch (legacyMode) {
    1 => CommandCenterControlMode.forcedOn,
    2 => CommandCenterControlMode.forcedOff,
    _ => CommandCenterControlMode.auto,
  };
}

CommandCenterScheduleEvent? _projectNextScheduleEvent(
  JsonMap status,
  List<CommandCenterOutput> outputs,
  DateTime referenceTime,
) {
  final schedules = _safeMap(status['schedules']);
  final legacy = _safeMap(status['schedule']);
  final outputByKind = {for (final output in outputs) output.kind: output};
  final candidates = <CommandCenterScheduleEvent>[];

  void addOutputSchedule({
    required CommandCenterOutputKind kind,
    required String label,
    required List<String> scheduleKeys,
    required String legacyStartPrefix,
    required String legacyEndPrefix,
  }) {
    final output = outputByKind[kind];
    if (output == null ||
        output.controlMode != CommandCenterControlMode.auto ||
        !output.available) {
      return;
    }

    final schedule = _firstNonEmptyMap(
      scheduleKeys.map((key) => schedules[key]),
    );
    final start =
        _parseClockText(schedule['start']) ??
        _parseLegacyClock(legacy, legacyStartPrefix);
    final end =
        _parseClockText(schedule['end']) ??
        _parseLegacyClock(legacy, legacyEndPrefix);
    if (start != null) {
      candidates.add(
        _eventAtNextOccurrence(
          kind: CommandCenterScheduleEventKind.turnOn,
          outputKind: kind,
          label: '$label — włączenie',
          clock: start,
          referenceTime: referenceTime,
        ),
      );
    }
    if (end != null) {
      candidates.add(
        _eventAtNextOccurrence(
          kind: CommandCenterScheduleEventKind.turnOff,
          outputKind: kind,
          label: '$label — wyłączenie',
          clock: end,
          referenceTime: referenceTime,
        ),
      );
    }
  }

  addOutputSchedule(
    kind: CommandCenterOutputKind.light1,
    label: 'Świetlówka przednia',
    scheduleKeys: const ['light1', 'light'],
    legacyStartPrefix: 'dayStart',
    legacyEndPrefix: 'dayEnd',
  );
  addOutputSchedule(
    kind: CommandCenterOutputKind.light2,
    label: 'Świetlówka tylna',
    scheduleKeys: const ['light2', 'plant_light'],
    legacyStartPrefix: 'plantStart',
    legacyEndPrefix: 'plantEnd',
  );
  addOutputSchedule(
    kind: CommandCenterOutputKind.filter,
    label: 'Filtr',
    scheduleKeys: const ['filter'],
    legacyStartPrefix: 'filterStart',
    legacyEndPrefix: 'filterEnd',
  );
  addOutputSchedule(
    kind: CommandCenterOutputKind.aeration,
    label: 'Napowietrzanie',
    scheduleKeys: const ['air'],
    legacyStartPrefix: 'airStart',
    legacyEndPrefix: 'airEnd',
  );

  final feederOutput = outputByKind[CommandCenterOutputKind.feeder];
  if (feederOutput != null &&
      feederOutput.available &&
      feederOutput.controlMode == CommandCenterControlMode.auto) {
    final feederSchedule = _safeMap(schedules['feeder']);
    final feederEnabled =
        _safeBool(feederSchedule['enabled']) ??
        (_safeInt(_safeMap(status['feeding'])['freq']) ?? 0) > 0;
    if (feederEnabled) {
      final count = (_safeInt(feederSchedule['count']) ?? 1).clamp(1, 2);
      final firstTime =
          _parseClockText(feederSchedule['time1']) ??
          _parseLegacyFeedingClock(status);
      final secondTime = count > 1
          ? _parseClockText(feederSchedule['time2'])
          : null;
      for (final clock in [firstTime, secondTime]) {
        if (clock == null) continue;
        candidates.add(
          _eventAtNextOccurrence(
            kind: CommandCenterScheduleEventKind.feed,
            outputKind: CommandCenterOutputKind.feeder,
            label: 'Automatyczne karmienie',
            clock: clock,
            referenceTime: referenceTime,
          ),
        );
      }
    }
  }

  if (candidates.isEmpty) return null;
  candidates.sort(
    (left, right) => left.scheduledAt.compareTo(right.scheduledAt),
  );
  return candidates.first;
}

CommandCenterScheduleEvent _eventAtNextOccurrence({
  required CommandCenterScheduleEventKind kind,
  required CommandCenterOutputKind outputKind,
  required String label,
  required _ClockValue clock,
  required DateTime referenceTime,
}) {
  var scheduledAt = DateTime(
    referenceTime.year,
    referenceTime.month,
    referenceTime.day,
    clock.hour,
    clock.minute,
  );
  if (!scheduledAt.isAfter(referenceTime)) {
    scheduledAt = scheduledAt.add(const Duration(days: 1));
  }
  return CommandCenterScheduleEvent(
    kind: kind,
    outputKind: outputKind,
    label: label,
    scheduledAt: scheduledAt,
    referenceTime: referenceTime,
  );
}

CommandCenterSafetySummary _projectSafety({
  required JsonMap status,
  required bool connected,
  required CommandCenterCapabilities capabilities,
  required List<CommandCenterAlarm> alarms,
  required List<CommandCenterSensor> sensors,
}) {
  if (!connected || status.isEmpty) {
    return const CommandCenterSafetySummary(
      state: CommandCenterSafetyState.offline,
      title: 'Sterownik offline',
      message: 'Brak aktualnych danych. Polecenia sterujące są niedostępne.',
      activeAlarmCount: 0,
    );
  }

  final criticalCount = alarms
      .where((alarm) => alarm.severity == CommandCenterAlarmSeverity.critical)
      .length;
  if (criticalCount > 0) {
    return CommandCenterSafetySummary(
      state: CommandCenterSafetyState.critical,
      title: 'Wymagana natychmiastowa reakcja',
      message: criticalCount == 1
          ? alarms
                .firstWhere(
                  (alarm) =>
                      alarm.severity == CommandCenterAlarmSeverity.critical,
                )
                .title
          : 'Aktywne alarmy krytyczne: $criticalCount.',
      activeAlarmCount: alarms.length,
    );
  }

  final unavailableSafetySensors = sensors.where(
    (sensor) =>
        sensor.enabled &&
        sensor.state == CommandCenterSensorState.unavailable &&
        const {
          CommandCenterSensorKind.temperature,
          CommandCenterSensorKind.waterLevel,
          CommandCenterSensorKind.leak,
          CommandCenterSensorKind.ioBus,
        }.contains(sensor.kind),
  );
  if (alarms.isNotEmpty || unavailableSafetySensors.isNotEmpty) {
    final missingCount = unavailableSafetySensors.length;
    return CommandCenterSafetySummary(
      state: CommandCenterSafetyState.warning,
      title: 'System wymaga uwagi',
      message: alarms.isNotEmpty
          ? alarms.first.title
          : missingCount == 1
          ? 'Jeden czujnik bezpieczeństwa nie dostarcza danych.'
          : '$missingCount czujniki bezpieczeństwa nie dostarczają danych.',
      activeAlarmCount: alarms.length,
    );
  }

  final network = _safeMap(status['network']);
  final config = _safeMap(status['config']);
  final serviceMode =
      capabilities.isDevelopment ||
      _safeBool(status['ota_active']) == true ||
      _safeBool(network['serviceMode']) == true ||
      _safeBool(network['serviceModePending']) == true ||
      _safeBool(config['dev_mode']) == true;
  if (serviceMode) {
    return const CommandCenterSafetySummary(
      state: CommandCenterSafetyState.service,
      title: 'Tryb serwisowy',
      message: 'Sterownik działa, ale znajduje się w trybie serwisowym.',
      activeAlarmCount: 0,
    );
  }

  return const CommandCenterSafetySummary(
    state: CommandCenterSafetyState.ok,
    title: 'System bezpieczny',
    message: 'Brak aktywnych alarmów i błędów czujników bezpieczeństwa.',
    activeAlarmCount: 0,
  );
}

DateTime _controllerTime(JsonMap status, DateTime fallback) {
  final clock = _safeMap(status['clock']);
  if (_safeBool(clock['valid']) != true) return fallback;

  final year = _safeInt(clock['year']);
  final month = _safeInt(clock['month']);
  final day = _safeInt(clock['day']);
  final hour = _safeInt(clock['hour']);
  final minute = _safeInt(clock['minute']);
  final second = _safeInt(clock['second']) ?? 0;
  if (year == null ||
      year < 2000 ||
      year > 2200 ||
      month == null ||
      month < 1 ||
      month > 12 ||
      day == null ||
      day < 1 ||
      day > 31 ||
      hour == null ||
      hour < 0 ||
      hour > 23 ||
      minute == null ||
      minute < 0 ||
      minute > 59 ||
      second < 0 ||
      second > 59) {
    return fallback;
  }

  final projected = DateTime(year, month, day, hour, minute, second);
  if (projected.year != year ||
      projected.month != month ||
      projected.day != day) {
    return fallback;
  }
  return projected;
}

_ClockValue? _parseClockText(Object? value) {
  if (value is! String) return null;
  final parts = value.trim().split(':');
  if (parts.length != 2) return null;
  final hour = int.tryParse(parts[0]);
  final minute = int.tryParse(parts[1]);
  if (hour == null ||
      hour < 0 ||
      hour > 23 ||
      minute == null ||
      minute < 0 ||
      minute > 59) {
    return null;
  }
  return _ClockValue(hour, minute);
}

_ClockValue? _parseLegacyClock(JsonMap legacy, String prefix) {
  final hour = _safeInt(legacy['${prefix}Hour']);
  final minute = _safeInt(legacy['${prefix}Min']);
  if (hour == null ||
      hour < 0 ||
      hour > 23 ||
      minute == null ||
      minute < 0 ||
      minute > 59) {
    return null;
  }
  return _ClockValue(hour, minute);
}

_ClockValue? _parseLegacyFeedingClock(JsonMap status) {
  final feeding = _safeMap(status['feeding']);
  final hour = _safeInt(feeding['hour']);
  final minute = _safeInt(feeding['minute']);
  if (hour == null ||
      hour < 0 ||
      hour > 23 ||
      minute == null ||
      minute < 0 ||
      minute > 59) {
    return null;
  }
  return _ClockValue(hour, minute);
}

bool _explicitlyDisabled(JsonMap map, String key) =>
    map.containsKey(key) && _safeBool(map[key]) == false;

JsonMap _safeMap(Object? value) {
  if (value is Map<String, dynamic>) return value;
  if (value is! Map) return <String, dynamic>{};
  final result = <String, dynamic>{};
  for (final entry in value.entries) {
    if (entry.key is String) {
      result[entry.key as String] = entry.value;
    }
  }
  return result;
}

JsonMap _firstNonEmptyMap(Iterable<Object?> values) {
  for (final value in values) {
    final map = _safeMap(value);
    if (map.isNotEmpty) return map;
  }
  return <String, dynamic>{};
}

bool? _firstBool(Iterable<Object?> values) {
  for (final value in values) {
    final parsed = _safeBool(value);
    if (parsed != null) return parsed;
  }
  return null;
}

bool? _safeBool(Object? value) {
  if (value is bool) return value;
  if (value is num) {
    if (value == 0) return false;
    if (value == 1) return true;
    return null;
  }
  if (value is String) {
    return switch (value.trim().toLowerCase()) {
      '1' || 'true' || 'on' || 'yes' || 'tak' => true,
      '0' || 'false' || 'off' || 'no' || 'nie' => false,
      _ => null,
    };
  }
  return null;
}

int? _safeInt(Object? value) {
  if (value is int) return value;
  if (value is num) {
    final number = value.toDouble();
    if (!number.isFinite || number != number.truncateToDouble()) return null;
    return number.toInt();
  }
  return int.tryParse(value?.toString().trim() ?? '');
}

double? _safeDouble(Object? value) {
  final parsed = value is num
      ? value.toDouble()
      : double.tryParse(value?.toString().trim() ?? '');
  return parsed != null && parsed.isFinite ? parsed : null;
}

String _safeText(Object? value) => value is String ? value.trim() : '';

final class _ClockValue {
  const _ClockValue(this.hour, this.minute);

  final int hour;
  final int minute;
}

final class _OutputDefinition {
  const _OutputDefinition({
    required this.kind,
    required this.label,
    required this.relayKeys,
    required this.moduleStateKeys,
    required this.scheduleKeys,
    this.enabledKey,
    this.legacyModeKey,
  });

  final CommandCenterOutputKind kind;
  final String label;
  final List<String> relayKeys;
  final List<String> moduleStateKeys;
  final List<String> scheduleKeys;
  final String? enabledKey;
  final String? legacyModeKey;
}

const List<_OutputDefinition> _outputDefinitions = [
  _OutputDefinition(
    kind: CommandCenterOutputKind.light1,
    label: 'Świetlówka przednia',
    relayKeys: ['light1', 'light'],
    moduleStateKeys: ['light1_on', 'light_on'],
    scheduleKeys: ['light1', 'light'],
    legacyModeKey: 'lightMode',
  ),
  _OutputDefinition(
    kind: CommandCenterOutputKind.light2,
    label: 'Świetlówka tylna',
    relayKeys: ['light2', 'plantLight'],
    moduleStateKeys: ['light2_on', 'plant_light_on'],
    scheduleKeys: ['light2', 'plant_light'],
    legacyModeKey: 'plantLightMode',
  ),
  _OutputDefinition(
    kind: CommandCenterOutputKind.filter,
    label: 'Filtr',
    relayKeys: ['pump', 'filter'],
    moduleStateKeys: ['filter_on'],
    scheduleKeys: ['filter'],
    legacyModeKey: 'filterMode',
  ),
  _OutputDefinition(
    kind: CommandCenterOutputKind.heater,
    label: 'Grzałka',
    relayKeys: ['heater'],
    moduleStateKeys: ['heater_on'],
    scheduleKeys: [],
    enabledKey: 'heater_enabled',
    legacyModeKey: 'heaterMode',
  ),
  _OutputDefinition(
    kind: CommandCenterOutputKind.co2,
    label: 'Dozowanie CO₂',
    relayKeys: ['co2'],
    moduleStateKeys: ['co2_on'],
    scheduleKeys: [],
    enabledKey: 'co2_enabled',
  ),
  _OutputDefinition(
    kind: CommandCenterOutputKind.aeration,
    label: 'Napowietrzanie',
    relayKeys: ['aeration'],
    moduleStateKeys: ['air_on'],
    scheduleKeys: ['air'],
    legacyModeKey: 'airMode',
  ),
  _OutputDefinition(
    kind: CommandCenterOutputKind.waterDosing,
    label: 'Dolewanie wody',
    relayKeys: ['waterDosing'],
    moduleStateKeys: ['water_dosing_on'],
    scheduleKeys: [],
    enabledKey: 'water_level_enabled',
  ),
  _OutputDefinition(
    kind: CommandCenterOutputKind.feeder,
    label: 'Karmnik',
    relayKeys: ['feeder'],
    moduleStateKeys: [],
    scheduleKeys: ['feeder'],
    enabledKey: 'feeder_enabled',
  ),
];
