import 'package:home_entities/home_entities.dart';
import 'package:test/test.dart';

void main() {
  test('source-scoped identifiers prevent cross-source collisions', () {
    final first = SourceScopedId(sourceId: 'demo', localId: 'light.kitchen');
    final second = SourceScopedId(
      sourceId: 'ha-main',
      localId: 'light.kitchen',
    );
    expect(first, isNot(second));
    expect(SourceScopedId.parse(first.value), first);
  });

  test('all required Home Assistant domains map to typed entities', () {
    const domains = <String>[
      'light',
      'switch',
      'sensor',
      'binary_sensor',
      'climate',
      'cover',
      'lock',
      'alarm_control_panel',
      'camera',
      'media_player',
      'fan',
      'vacuum',
      'weather',
      'person',
      'device_tracker',
      'scene',
      'script',
      'automation',
      'button',
      'input_button',
      'number',
      'input_number',
      'select',
      'input_select',
      'text',
      'input_text',
      'update',
    ];
    for (final domain in domains) {
      expect(HomeEntityType.fromDomain(domain), isNot(HomeEntityType.unknown));
    }
    expect(HomeEntityType.fromDomain('future_domain'), HomeEntityType.unknown);
  });
}
