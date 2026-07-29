#ifndef AQUARIUM_SENSOR_CALIBRATION_H
#define AQUARIUM_SENSOR_CALIBRATION_H

#include <stddef.h>
#include <stdint.h>

namespace aquarium {

constexpr uint16_t kSensorCalibrationVersion = 1U;
constexpr size_t kSensorMedianWindow = 5U;

struct SensorCalibration {
    uint16_t version;
    int16_t ph_low_raw;
    int16_t ph_high_raw;
    float ph_low_reference;
    float ph_high_reference;
    int16_t ec_reference_raw;
    float ec_reference_us_cm;
    float ec_temperature_coefficient;
    float ec_reference_temperature_c;
};

SensorCalibration default_sensor_calibration();
bool validate_sensor_calibration(const SensorCalibration &calibration);
float calibrate_ph(int16_t raw, const SensorCalibration &calibration);
float calibrate_ec(int16_t raw,
                   float temperature_c,
                   bool temperature_valid,
                   const SensorCalibration &calibration);

class MedianFilter {
public:
    MedianFilter();

    void reset();
    float push(int16_t raw);
    size_t size() const;

private:
    int16_t samples_[kSensorMedianWindow];
    size_t count_;
    size_t next_;
};

} // namespace aquarium

#endif // AQUARIUM_SENSOR_CALIBRATION_H
