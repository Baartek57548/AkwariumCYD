import 'package:cyd_aquarium_mobile/full_controller/controller_api.dart';
import 'package:cyd_aquarium_mobile/full_controller/controller_session.dart';
import 'package:cyd_aquarium_mobile/full_controller/data_access.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('development session exposes complete web-compatible status', () async {
    final session = ControllerSession.development();
    addTearDown(session.dispose);

    await session.connect();

    expect(session.connected, isTrue);
    expect(session.status.section('sensors').flag('temp_valid'), isTrue);
    expect(session.status.section('schedules').section('light'), isNotEmpty);
    expect(
      session.status.section('network').text('configuredStaSsid'),
      isNotEmpty,
    );
    expect(
      session.status.section('display').integer('brightness'),
      inInclusiveRange(10, 100),
    );
  });

  test('development session authenticates and applies web actions', () async {
    final session = ControllerSession.development();
    addTearDown(session.dispose);
    await session.connect();
    await session.login('1234');

    await session.action('set_light', payload: const {'state': false});
    expect(session.status.section('modules').flag('light_on'), isFalse);

    await session.action(
      'save_temperature',
      payload: const {'heaterMode': 0, 'target': '26.2', 'hysteresis': '0.7'},
    );
    expect(session.status.section('temperature').number('target'), 26.2);
    expect(session.status.section('config').number('temp_hysteresis'), 0.7);

    await session.action(
      'save_display',
      payload: const {
        'autoBrightness': false,
        'profile': 'timeout_60s',
        'brightness': 65,
      },
    );
    expect(session.status.section('display').flag('autoBrightness'), isFalse);
    expect(session.status.section('display').text('profile'), 'timeout_60s');
    expect(session.status.section('display').integer('brightness'), 65);
  });

  test('development session rejects an invalid PIN', () async {
    final session = ControllerSession.development();
    addTearDown(session.dispose);

    expect(
      () => session.login('9999'),
      throwsA(
        isA<ControllerApiException>().having(
          (error) => error.code,
          'code',
          'invalid_pin',
        ),
      ),
    );
  });

  test('relay profile validation requires a full payload', () async {
    final session = ControllerSession.development();
    addTearDown(session.dispose);
    await session.connect();
    await session.login('1234');

    expect(
      () => session.action('save_relays', payload: const {'data': '{}'}),
      throwsA(
        isA<ControllerApiException>().having(
          (error) => error.code,
          'code',
          'invalid_relay_profile',
        ),
      ),
    );
  });
}
