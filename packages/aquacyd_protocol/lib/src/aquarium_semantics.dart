import 'package:home_entities/home_entities.dart';

enum AquariumSemanticRole {
  temperature,
  targetTemperature,
  temperatureHysteresis,
  ph,
  ec,
  waterLevel,
  leak,
  alarm,
  controllerSafe,
  frontLight,
  rearLight,
  filter,
  aeration,
  heater,
  topUp,
  co2,
  dosing,
  feeder,
  firmwareUpdate,
  unknown,
}

abstract final class AquariumSemantics {
  static const int contractVersion = 1;

  static bool isAquariumDevice(HomeDevice device) {
    final haystack =
        '${device.id.localId} ${device.name} ${device.model} '
                '${device.manufacturer}'
            .toLowerCase();
    return device.isAquariumController ||
        haystack.contains('aquacyd') ||
        haystack.contains('aquarium') ||
        haystack.contains('akwarium');
  }

  static AquariumSemanticRole classify(HomeEntity entity) {
    final value = '${entity.id.localId} ${entity.name}'.toLowerCase();
    bool any(Iterable<String> tokens) => tokens.any(value.contains);
    if (any(const <String>['target_temperature', 'temperatura zadana'])) {
      return AquariumSemanticRole.targetTemperature;
    }
    if (any(const <String>['hysteresis', 'histereza'])) {
      return AquariumSemanticRole.temperatureHysteresis;
    }
    if (any(const <String>['temperature', 'temperatura'])) {
      return AquariumSemanticRole.temperature;
    }
    if (RegExp(r'(^|[._ -])ph($|[._ -])').hasMatch(value)) {
      return AquariumSemanticRole.ph;
    }
    if (RegExp(r'(^|[._ -])ec($|[._ -])').hasMatch(value)) {
      return AquariumSemanticRole.ec;
    }
    if (any(const <String>['water_level', 'water low', 'poziom wody'])) {
      return AquariumSemanticRole.waterLevel;
    }
    if (any(const <String>['leak', 'wyciek'])) return AquariumSemanticRole.leak;
    if (any(const <String>['alarm'])) return AquariumSemanticRole.alarm;
    if (any(const <String>['controller_safe', 'safe'])) {
      return AquariumSemanticRole.controllerSafe;
    }
    if (any(const <String>['front_light', 'light_front', 'lampa przednia'])) {
      return AquariumSemanticRole.frontLight;
    }
    if (any(const <String>['rear_light', 'light_rear', 'lampa tylna'])) {
      return AquariumSemanticRole.rearLight;
    }
    if (any(const <String>['filter', 'filtr']))
      return AquariumSemanticRole.filter;
    if (any(const <String>['aeration', 'napowietrz'])) {
      return AquariumSemanticRole.aeration;
    }
    if (any(const <String>['heater', 'grzał']))
      return AquariumSemanticRole.heater;
    if (any(const <String>['top_up', 'ato', 'dolew']))
      return AquariumSemanticRole.topUp;
    if (any(const <String>['co2'])) return AquariumSemanticRole.co2;
    if (any(const <String>['dosing', 'dozow']))
      return AquariumSemanticRole.dosing;
    if (any(const <String>['feeder', 'karm']))
      return AquariumSemanticRole.feeder;
    if (entity.type == HomeEntityType.update &&
        any(const <String>['cyd', 'aquarium', 'aquacyd'])) {
      return AquariumSemanticRole.firmwareUpdate;
    }
    return AquariumSemanticRole.unknown;
  }

  static HomeCommandRisk riskFor(AquariumSemanticRole role) => switch (role) {
    AquariumSemanticRole.heater ||
    AquariumSemanticRole.topUp ||
    AquariumSemanticRole.co2 ||
    AquariumSemanticRole.dosing ||
    AquariumSemanticRole.firmwareUpdate => HomeCommandRisk.critical,
    AquariumSemanticRole.filter ||
    AquariumSemanticRole.aeration ||
    AquariumSemanticRole.feeder => HomeCommandRisk.consequential,
    _ => HomeCommandRisk.routine,
  };
}

final class AquariumOverview {
  AquariumOverview.fromSnapshot(HomeSnapshot snapshot)
    : devices = snapshot.devices
          .where(AquariumSemantics.isAquariumDevice)
          .toList(growable: false),
      entities = snapshot.entities
          .where(
            (entity) =>
                AquariumSemantics.classify(entity) !=
                AquariumSemanticRole.unknown,
          )
          .toList(growable: false);

  final List<HomeDevice> devices;
  final List<HomeEntity> entities;

  bool get present => devices.isNotEmpty || entities.isNotEmpty;

  HomeEntity? byRole(AquariumSemanticRole role) {
    for (final entity in entities) {
      if (AquariumSemantics.classify(entity) == role) return entity;
    }
    return null;
  }

  bool get hasAlarm => <AquariumSemanticRole>{
    AquariumSemanticRole.alarm,
    AquariumSemanticRole.leak,
    AquariumSemanticRole.waterLevel,
  }.any((role) => byRole(role)?.booleanValue == true);
}
