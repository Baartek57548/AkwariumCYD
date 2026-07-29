#ifndef AQUARIUM_SENSOR_CALIBRATION_STORE_H
#define AQUARIUM_SENSOR_CALIBRATION_STORE_H

#include <Arduino.h>

#include "sensor_calibration.h"

/**
 * Loads the versioned calibration record from NVS. Invalid or missing records
 * are replaced with conservative legacy-compatible defaults.
 */
bool sensor_calibration_store_initialize(void);

/** Returns a coherent calibration snapshot safe to consume from Core 0. */
aquarium::SensorCalibration sensor_calibration_store_snapshot(void);

/** Atomically validates and persists a complete calibration. */
bool sensor_calibration_store_save(
    const aquarium::SensorCalibration &calibration);

bool sensor_calibration_store_save_ph(int16_t low_raw,
                                      float low_reference,
                                      int16_t high_raw,
                                      float high_reference);

bool sensor_calibration_store_save_ec(int16_t reference_raw,
                                      float reference_us_cm,
                                      float temperature_coefficient,
                                      float reference_temperature_c);

bool sensor_calibration_store_reset_defaults(void);

#endif // AQUARIUM_SENSOR_CALIBRATION_STORE_H
