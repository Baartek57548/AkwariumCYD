import 'package:cyd_aquarium_mobile/full_controller/command_center_models.dart';
import 'package:cyd_aquarium_mobile/full_controller/controller_session.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  group('CommandCenterModel capabilities', () {
    test('distinguishes Wi-Fi, BLE v1, BLE v2 and development sessions', () {
      expect(
        _project(sessionKind: ControllerSessionKind.wifi).capabilities.primary,
        CommandCenterCapability.wifi,
      );
      expect(
        _project(
          sessionKind: ControllerSessionKind.bluetooth,
          mutate: (status) => status['mode'] = 'BLE_V1',
        ).capabilities.primary,
        CommandCenterCapability.bleV1,
      );
      expect(
        _project(
          sessionKind: ControllerSessionKind.bluetooth,
        ).capabilities.primary,
        CommandCenterCapability.bleV2,
      );
      expect(
        _project(
          sessionKind: ControllerSessionKind.development,
        ).capabilities.primary,
        CommandCenterCapability.development,
      );
    });

    test('reports capability limits for the legacy BLE protocol', () {
      final capabilities = CommandCenterModel.fromStatus({
        'mode': 'BLE_V1',
      }, ControllerSessionKind.bluetooth).capabilities;

      expect(capabilities.isBluetooth, isTrue);
      expect(capabilities.isLegacyBluetooth, isTrue);
      expect(capabilities.supportsExtendedStatus, isFalse);
      expect(capabilities.supportsSchedules, isFalse);
      expect(capabilities.supportsDiagnostics, isFalse);
      expect(capabilities.supportsFirmwareUpdate, isFalse);
    });
  });

  group('CommandCenterModel safety', () {
    test('projects an empty or disconnected status as offline', () {
      final empty = CommandCenterModel.fromStatus(
        <String, dynamic>{},
        ControllerSessionKind.wifi,
      );
      final disconnected = _project(connected: false);

      expect(empty.safety.state, CommandCenterSafetyState.offline);
      expect(disconnected.safety.state, CommandCenterSafetyState.offline);
      expect(disconnected.safety.requiresAttention, isTrue);
    });

    test('prioritizes critical alarms and keeps all active alarm details', () {
      final model = _project(
        mutate: (status) {
          status['alarms'] = {'flags': 20, 'phOutOfRange': true, 'leak': true};
        },
      );

      expect(model.safety.state, CommandCenterSafetyState.critical);
      expect(model.activeAlarms, hasLength(2));
      expect(model.activeAlarms.first.kind, CommandCenterAlarmKind.leak);
      expect(
        model.sensor(CommandCenterSensorKind.leak).state,
        CommandCenterSensorState.critical,
      );
      expect(
        model.sensor(CommandCenterSensorKind.ph).state,
        CommandCenterSensorState.warning,
      );
    });

    test('uses warning for unavailable safety telemetry', () {
      final model = _project(
        mutate: (status) {
          final sensors = status['sensors']! as Map<String, dynamic>;
          sensors['temp_valid'] = false;
          sensors['temp_c'] = double.nan;
        },
      );

      expect(model.safety.state, CommandCenterSafetyState.warning);
      expect(
        model.sensor(CommandCenterSensorKind.temperature).numericValue,
        isNull,
      );
      expect(
        model.sensor(CommandCenterSensorKind.temperature).displayValue,
        '—',
      );
    });

    test('exposes service mode only after alarm and telemetry checks', () {
      final service = _project(
        mutate: (status) {
          final network = status['network']! as Map<String, dynamic>;
          network['serviceMode'] = true;
        },
      );
      final development = _project(
        sessionKind: ControllerSessionKind.development,
      );

      expect(service.safety.state, CommandCenterSafetyState.service);
      expect(development.safety.state, CommandCenterSafetyState.service);
      expect(service.safety.requiresAttention, isFalse);
    });

    test('maps unknown alarm bits without throwing', () {
      final model = _project(
        mutate: (status) => status['alarms'] = {'flags': 128},
      );

      expect(model.safety.state, CommandCenterSafetyState.warning);
      expect(model.activeAlarms.single.kind, CommandCenterAlarmKind.unknown);
      expect(model.activeAlarms.single.rawFlags, 128);
    });
  });

  group('CommandCenterModel sensors and outputs', () {
    test('projects typed, display-ready sensor values', () {
      final model = _project();

      final temperature = model.sensor(CommandCenterSensorKind.temperature);
      final light = model.sensor(CommandCenterSensorKind.ambientLight);
      final water = model.sensor(CommandCenterSensorKind.waterLevel);

      expect(temperature.numericValue, 24.62);
      expect(temperature.displayValue, '24.6');
      expect(temperature.unit, '°C');
      expect(light.displayValue, '50');
      expect(light.detail, 'ADC 2048 / 4095');
      expect(water.binaryValue, isTrue);
      expect(water.displayValue, 'Prawidłowy');
    });

    test('does not turn malformed physical values into false', () {
      final model = _project(
        mutate: (status) {
          final relays = Map<String, dynamic>.from(status['relays']! as Map);
          final modules = Map<String, dynamic>.from(status['modules']! as Map);
          relays['heater'] = 'broken';
          modules['heater_on'] = 7;
          status['relays'] = relays;
          status['modules'] = modules;
        },
      );

      expect(
        model.output(CommandCenterOutputKind.heater).physicalState,
        CommandCenterPhysicalState.unknown,
      );
    });

    test(
      'projects physical state separately from the selected control mode',
      () {
        final model = _project(
          mutate: (status) {
            final schedules = status['schedules']! as Map<String, dynamic>;
            schedules['light1'] = {
              'mode': 'always_off',
              'start': '08:00',
              'end': '20:00',
            };
            schedules['filter'] = {
              'mode': 'always_on',
              'start': '09:00',
              'end': '21:00',
            };
          },
        );

        final light = model.output(CommandCenterOutputKind.light1);
        final filter = model.output(CommandCenterOutputKind.filter);
        final co2 = model.output(CommandCenterOutputKind.co2);

        expect(light.physicalState, CommandCenterPhysicalState.on);
        expect(light.controlMode, CommandCenterControlMode.forcedOff);
        expect(filter.physicalState, CommandCenterPhysicalState.off);
        expect(filter.controlMode, CommandCenterControlMode.forcedOn);
        expect(co2.controlMode, CommandCenterControlMode.disabled);
      },
    );

    test('marks an absent output unavailable and disabled', () {
      final model = CommandCenterModel.fromStatus({
        'mode': 'BLE_V1',
        'modules': {'heater_on': false},
        'relays': {'heater': false},
        'sensors': <String, dynamic>{},
        'alarms': {'flags': 0},
      }, ControllerSessionKind.bluetooth);

      final heater = model.output(CommandCenterOutputKind.heater);
      final dosing = model.output(CommandCenterOutputKind.waterDosing);

      expect(heater.available, isTrue);
      expect(heater.physicalState, CommandCenterPhysicalState.off);
      expect(dosing.available, isFalse);
      expect(dosing.controlMode, CommandCenterControlMode.disabled);
      expect(dosing.physicalState, CommandCenterPhysicalState.unknown);
    });

    test(
      'disabled sensors stay disabled even when stale values are present',
      () {
        final model = _project(
          mutate: (status) {
            final modules = status['modules']! as Map<String, dynamic>;
            modules['ec_enabled'] = false;
          },
        );

        final ec = model.sensor(CommandCenterSensorKind.conductivity);
        expect(ec.enabled, isFalse);
        expect(ec.valid, isFalse);
        expect(ec.state, CommandCenterSensorState.disabled);
        expect(ec.numericValue, isNull);
      },
    );
  });

  group('CommandCenterModel schedule projection', () {
    test('finds the nearest event from modern schedules', () {
      final model = _project(now: DateTime(2026, 7, 26, 13, 20));

      expect(model.nextScheduleEvent, isNotNull);
      expect(
        model.nextScheduleEvent!.kind,
        CommandCenterScheduleEventKind.feed,
      );
      expect(model.nextScheduleEvent!.scheduledAt, DateTime(2026, 7, 26, 14));
      expect(model.nextScheduleEvent!.timeUntil, const Duration(minutes: 40));
    });

    test('rolls a passed event to the following day', () {
      final model = _project(
        now: DateTime(2026, 7, 26, 23),
        mutate: (status) {
          final schedules = status['schedules']! as Map<String, dynamic>;
          schedules
            ..clear()
            ..['filter'] = {
              'mode': 'schedule',
              'start': '09:30',
              'end': '22:30',
            };
          final modules = status['modules']! as Map<String, dynamic>;
          modules['feeder_enabled'] = false;
          status['schedule'] = {
            'filterMode': 0,
            'filterStartHour': 9,
            'filterStartMin': 30,
            'filterEndHour': 22,
            'filterEndMin': 30,
          };
        },
      );

      expect(
        model.nextScheduleEvent!.scheduledAt,
        DateTime(2026, 7, 27, 9, 30),
      );
      expect(
        model.nextScheduleEvent!.kind,
        CommandCenterScheduleEventKind.turnOn,
      );
    });

    test('falls back to the legacy schedule representation', () {
      final model = _project(
        now: DateTime(2026, 7, 26, 9),
        mutate: (status) {
          final schedules = status['schedules']! as Map<String, dynamic>;
          schedules.clear();
          final modules = status['modules']! as Map<String, dynamic>;
          modules['feeder_enabled'] = false;
          status['schedule'] = {
            'lightMode': 0,
            'dayStartHour': 10,
            'dayStartMin': 15,
            'dayEndHour': 21,
            'dayEndMin': 45,
          };
        },
      );

      expect(
        model.nextScheduleEvent!.outputKind,
        CommandCenterOutputKind.light1,
      );
      expect(
        model.nextScheduleEvent!.scheduledAt,
        DateTime(2026, 7, 26, 10, 15),
      );
    });

    test('ignores malformed clocks and forced schedules', () {
      final model = _project(
        now: DateTime(2026, 7, 26, 12),
        mutate: (status) {
          status['schedules'] = {
            'light1': {'mode': 'always_on', 'start': '13:00', 'end': '24:90'},
            'filter': {'mode': 'schedule', 'start': 'invalid', 'end': '99:00'},
            'feeder': {'enabled': false, 'count': 2, 'time1': '13:30'},
          };
          final modules = status['modules']! as Map<String, dynamic>;
          modules['feeder_enabled'] = false;
          status['schedule'] = <String, dynamic>{};
        },
      );

      expect(model.nextScheduleEvent, isNull);
    });
  });
}

CommandCenterModel _project({
  ControllerSessionKind sessionKind = ControllerSessionKind.wifi,
  bool connected = true,
  DateTime? now,
  void Function(Map<String, dynamic> status)? mutate,
}) {
  final status = _validStatus();
  mutate?.call(status);
  return CommandCenterModel.fromStatus(
    status,
    sessionKind,
    connected: connected,
    now: now ?? DateTime(2026, 7, 26, 13, 20),
  );
}

Map<String, dynamic> _validStatus() {
  return {
    'device': 'cydAkwarium',
    'mode': 'STA',
    'ota_active': false,
    'sensors': {
      'temp_c': 24.62,
      'temp_valid': true,
      'ph': 6.81,
      'ph_valid': true,
      'ec': 452.0,
      'ec_valid': true,
      'ldr': 2048,
      'ldr_valid': true,
      'mcp_present': true,
      'mcp_valid': true,
      'mcp_ok': true,
      'water_level_high': true,
      'water_level_valid': true,
      'leak_detected': false,
      'leak_valid': true,
      'flow_active': true,
      'flow_valid': true,
      'supply_voltage': 5.08,
      'supply_valid': true,
    },
    'alarms': {
      'flags': 0,
      'activeCount': 0,
      'temperatureHigh': false,
      'temperatureLow': false,
      'phOutOfRange': false,
      'waterLevelLow': false,
      'leak': false,
      'supplyLow': false,
    },
    'config': {'dev_mode': false},
    'water': {'timeoutLatched': false},
    'modules': {
      'light1_on': true,
      'light2_on': false,
      'filter_on': false,
      'heater_on': true,
      'co2_on': false,
      'air_on': true,
      'water_dosing_on': false,
      'heater_enabled': true,
      'ph_sensor_enabled': true,
      'co2_enabled': false,
      'ec_enabled': true,
      'water_level_enabled': true,
      'leak_enabled': true,
      'flow_enabled': true,
      'feeder_enabled': true,
    },
    'schedules': {
      'light1': {'mode': 'schedule', 'start': '10:00', 'end': '22:00'},
      'light2': {'mode': 'schedule', 'start': '10:00', 'end': '22:00'},
      'filter': {'mode': 'schedule', 'start': '09:30', 'end': '22:30'},
      'air': {'mode': 'schedule', 'start': '22:00', 'end': '09:00'},
      'feeder': {
        'enabled': true,
        'count': 2,
        'time1': '14:00',
        'time2': '20:00',
      },
    },
    'temperature': {'heaterMode': 0},
    'network': {'serviceMode': false, 'serviceModePending': false},
    'relays': {
      'light1': true,
      'light2': false,
      'pump': false,
      'heater': true,
      'co2': false,
      'aeration': true,
      'waterDosing': false,
    },
    'schedule': {
      'lightMode': 0,
      'dayStartHour': 10,
      'dayStartMin': 0,
      'dayEndHour': 22,
      'dayEndMin': 0,
      'plantLightMode': 0,
      'plantStartHour': 10,
      'plantStartMin': 0,
      'plantEndHour': 22,
      'plantEndMin': 0,
      'filterMode': 0,
      'filterStartHour': 9,
      'filterStartMin': 30,
      'filterEndHour': 22,
      'filterEndMin': 30,
      'airMode': 0,
      'airStartHour': 22,
      'airStartMin': 0,
      'airEndHour': 9,
      'airEndMin': 0,
      'heaterMode': 0,
    },
    'feeding': {'active': false, 'freq': 1, 'hour': 14, 'minute': 0},
    'clock': {'valid': false},
  };
}
