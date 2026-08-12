abstract final class AquaEntityIds {
  static const temperature = 'sensor.aquacyd_aquarium_temperature';
  static const ph = 'sensor.aquacyd_aquarium_ph';
  static const ec = 'sensor.aquacyd_aquarium_ec';
  static const ldr = 'sensor.aquacyd_aquarium_ldr';
  static const targetTemperature = 'sensor.aquacyd_aquarium_target_temperature';
  static const temperatureHysteresis =
      'sensor.aquacyd_aquarium_temperature_hysteresis';
  static const alarms = 'sensor.aquacyd_aquarium_alarms';
  static const wifiRssi = 'sensor.aquacyd_aquarium_wifi_rssi';
  static const espNowRssi = 'sensor.aquacyd_aquarium_espnow_rssi';
  static const uptime = 'sensor.aquacyd_aquarium_uptime';
  static const freeHeap = 'sensor.aquacyd_aquarium_free_heap';
  static const configurationRevision =
      'sensor.aquacyd_aquarium_configuration_revision';
  static const heaterMode = 'sensor.aquacyd_aquarium_heater_mode';

  static const controllerSafe =
      'binary_sensor.aquacyd_aquarium_controller_safe';
  static const leak = 'binary_sensor.aquacyd_aquarium_leak';
  static const waterLow = 'binary_sensor.aquacyd_aquarium_water_low';
  static const configurationValid =
      'binary_sensor.aquacyd_aquarium_configuration_valid';
  static const lightPrimary = 'binary_sensor.aquacyd_aquarium_light_primary';
  static const lightSecondary =
      'binary_sensor.aquacyd_aquarium_light_secondary';
  static const filter = 'binary_sensor.aquacyd_aquarium_filter';
  static const aerator = 'binary_sensor.aquacyd_aquarium_aerator';
  static const heater = 'binary_sensor.aquacyd_aquarium_heater';

  static const all = <String>{
    temperature,
    ph,
    ec,
    ldr,
    targetTemperature,
    temperatureHysteresis,
    alarms,
    wifiRssi,
    espNowRssi,
    uptime,
    freeHeap,
    configurationRevision,
    heaterMode,
    controllerSafe,
    leak,
    waterLow,
    configurationValid,
    lightPrimary,
    lightSecondary,
    filter,
    aerator,
    heater,
    ...scheduleEntities,
  };

  static const scheduleTargets = <String>[
    'light_primary',
    'light_secondary',
    'filter',
    'aerator',
  ];

  static const scheduleEntities = <String>{
    'sensor.aquacyd_aquarium_light_primary_schedule_mode',
    'sensor.aquacyd_aquarium_light_primary_schedule_profile',
    'sensor.aquacyd_aquarium_light_primary_schedule_start',
    'sensor.aquacyd_aquarium_light_primary_schedule_end',
    'sensor.aquacyd_aquarium_light_secondary_schedule_mode',
    'sensor.aquacyd_aquarium_light_secondary_schedule_profile',
    'sensor.aquacyd_aquarium_light_secondary_schedule_start',
    'sensor.aquacyd_aquarium_light_secondary_schedule_end',
    'sensor.aquacyd_aquarium_filter_schedule_mode',
    'sensor.aquacyd_aquarium_filter_schedule_profile',
    'sensor.aquacyd_aquarium_filter_schedule_start',
    'sensor.aquacyd_aquarium_filter_schedule_end',
    'sensor.aquacyd_aquarium_aerator_schedule_mode',
    'sensor.aquacyd_aquarium_aerator_schedule_profile',
    'sensor.aquacyd_aquarium_aerator_schedule_start',
    'sensor.aquacyd_aquarium_aerator_schedule_end',
  };

  static String schedule(String target, String field) {
    if (!scheduleTargets.contains(target)) {
      throw ArgumentError.value(target, 'target', 'Nieznany harmonogram');
    }
    const fields = {'mode', 'profile', 'start', 'end'};
    if (!fields.contains(field)) {
      throw ArgumentError.value(field, 'field', 'Nieznane pole harmonogramu');
    }
    return 'sensor.aquacyd_aquarium_${target}_schedule_$field';
  }
}

abstract final class AquaScripts {
  static const lightOn = 'aquacyd_light_on';
  static const lightOff = 'aquacyd_light_off';
  static const plantLightOn = 'aquacyd_plant_light_on';
  static const plantLightOff = 'aquacyd_plant_light_off';
  static const filterOn = 'aquacyd_filter_on';
  static const filterOff = 'aquacyd_filter_off';
  static const aeratorOn = 'aquacyd_aerator_on';
  static const aeratorOff = 'aquacyd_aerator_off';
  static const startFeeding = 'aquacyd_start_feeding';
  static const requestSnapshot = 'aquacyd_request_snapshot';
  static const saveSchedule = 'aquacyd_save_schedule';
  static const saveThermostat = 'aquacyd_save_thermostat';
}
