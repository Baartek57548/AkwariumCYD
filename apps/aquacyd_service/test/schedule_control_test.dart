import 'package:cyd_aquarium_mobile/full_controller/schedule_control.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  group('readOutputSchedule', () {
    test('reads canonical schedule fields and normalizes clock values', () {
      final status = <String, dynamic>{
        'schedules': <String, dynamic>{
          'light1': <String, dynamic>{
            'mode': 'always_on',
            'start': '7:05',
            'end': '23:09',
            'profile': 'night',
            'profileCycle': true,
          },
        },
      };

      final result = readOutputSchedule(status, 'light1');

      expect(result.mode, OutputControlMode.forcedOn);
      expect(result.start, '07:05');
      expect(result.end, '23:09');
      expect(result.profile, 'night');
      expect(result.profileCycle, isTrue);
      expect(result.modeLabel, 'WYMUSZONE ON');
      expect(result.windowLabel, 'Praca ciągła');
    });

    test('supports light aliases emitted by older firmware', () {
      final status = <String, dynamic>{
        'schedules': <String, dynamic>{
          'light': <String, dynamic>{
            'mode': 'schedule',
            'start': '08:15',
            'end': '21:45',
            'profile': 'daybreak',
          },
          'plant_light': <String, dynamic>{
            'mode': 'always_off',
            'start': '09:30',
            'end': '20:10',
            'profile': 'day',
          },
        },
      };

      final light1 = readOutputSchedule(status, 'light1');
      final light2 = readOutputSchedule(status, 'light2');

      expect(light1.mode, OutputControlMode.automatic);
      expect(light1.start, '08:15');
      expect(light1.end, '21:45');
      expect(light1.profile, 'daybreak');
      expect(light1.windowLabel, '08:15–21:45');
      expect(light2.mode, OutputControlMode.forcedOff);
      expect(light2.start, '09:30');
      expect(light2.end, '20:10');
      expect(light2.windowLabel, 'Wyłączone trwale');
    });

    test('prefers canonical light channel over its compatibility alias', () {
      final status = <String, dynamic>{
        'schedules': <String, dynamic>{
          'light1': <String, dynamic>{
            'mode': 'always_off',
            'start': '06:00',
            'end': '07:00',
          },
          'light': <String, dynamic>{
            'mode': 'always_on',
            'start': '10:00',
            'end': '22:00',
          },
        },
      };

      final result = readOutputSchedule(status, 'light1');

      expect(result.mode, OutputControlMode.forcedOff);
      expect(result.start, '06:00');
      expect(result.end, '07:00');
    });

    test('falls back to legacy modes and clock fields', () {
      final status = <String, dynamic>{
        'schedule': <String, dynamic>{
          'lightMode': 1,
          'dayStartHour': 6,
          'dayStartMin': 5,
          'dayEndHour': 22,
          'dayEndMin': 40,
          'plantLightMode': 2,
          'plantStartHour': 7,
          'plantStartMin': 10,
          'plantEndHour': 21,
          'plantEndMin': 15,
          'filterMode': 0,
          'filterStartHour': 8,
          'filterStartMin': 20,
          'filterEndHour': 23,
          'filterEndMin': 30,
          'airMode': 2,
          'airStartHour': 22,
          'airStartMin': 45,
          'airEndHour': 5,
          'airEndMin': 35,
        },
      };

      expect(
        readOutputSchedule(status, 'light1').mode,
        OutputControlMode.forcedOn,
      );
      expect(readOutputSchedule(status, 'light1').start, '06:05');
      expect(readOutputSchedule(status, 'light1').end, '22:40');
      expect(
        readOutputSchedule(status, 'light2').mode,
        OutputControlMode.forcedOff,
      );
      expect(readOutputSchedule(status, 'light2').start, '07:10');
      expect(readOutputSchedule(status, 'filter').start, '08:20');
      expect(readOutputSchedule(status, 'filter').end, '23:30');
      expect(
        readOutputSchedule(status, 'air').mode,
        OutputControlMode.forcedOff,
      );
      expect(readOutputSchedule(status, 'air').end, '05:35');
    });

    test('rejects malformed modern clocks and uses safe legacy values', () {
      final status = <String, dynamic>{
        'schedules': <String, dynamic>{
          'filter': <String, dynamic>{
            'mode': 'schedule',
            'start': '24:00',
            'end': '12:60',
          },
        },
        'schedule': <String, dynamic>{
          'filterStartHour': 9,
          'filterStartMin': 12,
          'filterEndHour': 22,
          'filterEndMin': 34,
        },
      };

      final result = readOutputSchedule(status, 'filter');

      expect(result.start, '09:12');
      expect(result.end, '22:34');
    });

    test('uses deterministic safe defaults for an incomplete response', () {
      final light = readOutputSchedule(<String, dynamic>{}, 'light1');
      final filter = readOutputSchedule(<String, dynamic>{}, 'filter');
      final unknown = readOutputSchedule(<String, dynamic>{}, 'unknown');

      expect(light.mode, OutputControlMode.automatic);
      expect(light.start, '10:00');
      expect(light.end, '22:00');
      expect(filter.start, '09:30');
      expect(filter.end, '22:30');
      expect(unknown.start, '00:00');
      expect(unknown.end, '23:59');
    });
  });

  group('heaterAutomationEnabled', () {
    test('uses schedule value before all compatibility fallbacks', () {
      final status = <String, dynamic>{
        'schedule': <String, dynamic>{'heaterMode': 1},
        'temperature': <String, dynamic>{'heaterMode': 0},
        'modules': <String, dynamic>{'heater_enabled': true},
      };

      expect(heaterAutomationEnabled(status), isFalse);
    });

    test('uses temperature and module fallbacks for older responses', () {
      expect(
        heaterAutomationEnabled(<String, dynamic>{
          'temperature': <String, dynamic>{'heaterMode': 0},
          'modules': <String, dynamic>{'heater_enabled': false},
        }),
        isTrue,
      );
      expect(
        heaterAutomationEnabled(<String, dynamic>{
          'modules': <String, dynamic>{'heater_enabled': false},
        }),
        isFalse,
      );
      expect(heaterAutomationEnabled(<String, dynamic>{}), isTrue);
    });
  });

  group('buildScheduleModePatch', () {
    test('encodes one channel without copying stale schedule fields', () {
      expect(buildScheduleModePatch('light1', OutputControlMode.automatic), {
        'light1Mode': 0,
      });
      expect(buildScheduleModePatch('light2', OutputControlMode.forcedOn), {
        'light2Mode': 1,
      });
      expect(buildScheduleModePatch('filter', OutputControlMode.forcedOff), {
        'filterMode': 2,
      });
      expect(buildScheduleModePatch('air', OutputControlMode.automatic), {
        'aerationMode': 0,
      });
    });

    test('rejects unknown channels instead of mutating another output', () {
      expect(
        () => buildScheduleModePatch('heater', OutputControlMode.automatic),
        throwsArgumentError,
      );
    });
  });

  group('buildSchedulePayload', () {
    test('builds a complete firmware payload without losing channel state', () {
      final status = <String, dynamic>{
        'schedules': <String, dynamic>{
          'light1': <String, dynamic>{
            'mode': 'schedule',
            'start': '06:10',
            'end': '20:20',
            'profile': 'night',
            'profileCycle': true,
          },
          'light2': <String, dynamic>{
            'mode': 'always_on',
            'start': '07:30',
            'end': '19:40',
            'profile': 'daybreak',
            'profileCycle': false,
          },
          'filter': <String, dynamic>{
            'mode': 'always_off',
            'start': '08:50',
            'end': '18:55',
          },
          'air': <String, dynamic>{
            'mode': 'schedule',
            'start': '21:00',
            'end': '06:00',
          },
        },
        'schedule': <String, dynamic>{'heaterMode': 1},
        'feeding': <String, dynamic>{'freq': 3, 'hour': 13, 'minute': 7},
      };

      final payload = buildSchedulePayload(status);

      expect(payload, hasLength(33));
      expect(payload['lightMode'], 0);
      expect(payload['dayStart'], '06:10');
      expect(payload['dayEnd'], '20:20');
      expect(payload['lightProfile'], 'day');
      expect(payload['lightProfileCycle'], isTrue);
      expect(payload['light1Mode'], 0);
      expect(payload['light1Start'], '06:10');
      expect(payload['light1End'], '20:20');
      expect(payload['light1Profile'], 'day');
      expect(payload['plantLightMode'], 1);
      expect(payload['plantLightStart'], '07:30');
      expect(payload['plantLightEnd'], '19:40');
      expect(payload['plantLightProfile'], 'daybreak');
      expect(payload['light2Mode'], 1);
      expect(payload['filterMode'], 2);
      expect(payload['filterOn'], '08:50');
      expect(payload['filterOff'], '18:55');
      expect(payload['aerationMode'], 0);
      expect(payload['airOn'], '21:00');
      expect(payload['airOff'], '06:00');
      expect(payload['heaterMode'], 1);
      expect(payload['heaterStart'], '00:00');
      expect(payload['heaterEnd'], '23:59');
      expect(payload['feedFreq'], 3);
      expect(payload['feedTime'], '13:07');
    });

    test('overrides only the selected channel mode', () {
      final status = <String, dynamic>{
        'schedules': <String, dynamic>{
          'light1': <String, dynamic>{'mode': 'schedule'},
          'light2': <String, dynamic>{'mode': 'always_on'},
          'filter': <String, dynamic>{'mode': 'always_off'},
          'air': <String, dynamic>{'mode': 'schedule'},
        },
      };

      final payload = buildSchedulePayload(
        status,
        overrideChannel: 'filter',
        overrideMode: OutputControlMode.forcedOn,
      );

      expect(payload['lightMode'], 0);
      expect(payload['light1Mode'], 0);
      expect(payload['plantLightMode'], 1);
      expect(payload['light2Mode'], 1);
      expect(payload['filterMode'], 1);
      expect(payload['aerationMode'], 0);
    });

    test(
      'clamps feeding frequency and supplies safe missing-data defaults',
      () {
        final high = buildSchedulePayload(<String, dynamic>{
          'feeding': <String, dynamic>{'freq': 99, 'hour': 99, 'minute': -4},
        });
        final low = buildSchedulePayload(<String, dynamic>{
          'feeding': <String, dynamic>{'freq': -7},
        });

        expect(high['feedFreq'], 4);
        expect(high['feedTime'], '23:00');
        expect(low['feedFreq'], 0);
        expect(low['feedTime'], '14:00');
        expect(high['heaterMode'], 0);
        expect(high['light1Start'], '10:00');
        expect(high['light2End'], '22:00');
        expect(high['filterOn'], '09:30');
        expect(high['airOff'], '09:00');
      },
    );
  });
}
