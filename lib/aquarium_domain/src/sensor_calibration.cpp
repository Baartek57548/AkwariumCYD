#include "sensor_calibration.h"

#include <math.h>

namespace aquarium {

namespace {

constexpr int32_t kMinimumPhRawSpan = 64;
constexpr float kMinimumEcCompensationFactor = 0.25f;
constexpr float kMaximumEcCompensationFactor = 4.0f;

bool finite_between(float value, float low, float high) {
    return isfinite(value) && value >= low && value <= high;
}

} // namespace

SensorCalibration default_sensor_calibration() {
    // These defaults preserve the legacy voltage mappings until the probes are
    // calibrated by the operator. The explicit version lets persistence reject
    // incompatible future layouts instead of silently applying bad coefficients.
    return {
        kSensorCalibrationVersion,
        0,
        32767,
        4.0f,
        10.0f,
        11304,
        1413.0f,
        0.019f,
        25.0f
    };
}

bool validate_sensor_calibration(const SensorCalibration &calibration) {
    const int32_t ph_span =
        static_cast<int32_t>(calibration.ph_high_raw) -
        static_cast<int32_t>(calibration.ph_low_raw);
    const int32_t absolute_ph_span =
        ph_span < 0 ? -ph_span : ph_span;
    return calibration.version == kSensorCalibrationVersion &&
           absolute_ph_span >= kMinimumPhRawSpan &&
           finite_between(calibration.ph_low_reference, 0.0f, 14.0f) &&
           finite_between(calibration.ph_high_reference, 0.0f, 14.0f) &&
           calibration.ph_high_reference > calibration.ph_low_reference &&
           calibration.ec_reference_raw > 0 &&
           finite_between(calibration.ec_reference_us_cm, 1.0f, 100000.0f) &&
           finite_between(calibration.ec_temperature_coefficient, 0.0f, 0.1f) &&
           finite_between(calibration.ec_reference_temperature_c, 0.0f, 50.0f);
}

float calibrate_ph(int16_t raw, const SensorCalibration &calibration) {
    if (!validate_sensor_calibration(calibration)) {
        return NAN;
    }
    const float raw_span = static_cast<float>(
        static_cast<int32_t>(calibration.ph_high_raw) -
        static_cast<int32_t>(calibration.ph_low_raw));
    const float reference_span =
        calibration.ph_high_reference - calibration.ph_low_reference;
    const float value =
        calibration.ph_low_reference +
        (static_cast<float>(raw) -
         static_cast<float>(calibration.ph_low_raw)) *
            reference_span / raw_span;
    return finite_between(value, 0.0f, 14.0f) ? value : NAN;
}

float calibrate_ec(int16_t raw,
                   float temperature_c,
                   bool temperature_valid,
                   const SensorCalibration &calibration) {
    if (!validate_sensor_calibration(calibration) || raw < 0) {
        return NAN;
    }
    float value =
        static_cast<float>(raw) * calibration.ec_reference_us_cm /
        static_cast<float>(calibration.ec_reference_raw);
    if (temperature_valid && isfinite(temperature_c)) {
        const float compensation =
            1.0f + calibration.ec_temperature_coefficient *
                       (temperature_c -
                        calibration.ec_reference_temperature_c);
        if (!finite_between(compensation,
                            kMinimumEcCompensationFactor,
                            kMaximumEcCompensationFactor)) {
            return NAN;
        }
        value /= compensation;
    }
    return finite_between(value, 0.0f, 100000.0f) ? value : NAN;
}

MedianFilter::MedianFilter() : samples_{}, count_(0U), next_(0U) {
}

void MedianFilter::reset() {
    for (size_t index = 0U; index < kSensorMedianWindow; ++index) {
        samples_[index] = 0;
    }
    count_ = 0U;
    next_ = 0U;
}

float MedianFilter::push(int16_t raw) {
    samples_[next_] = raw;
    next_ = (next_ + 1U) % kSensorMedianWindow;
    if (count_ < kSensorMedianWindow) {
        ++count_;
    }

    int16_t sorted[kSensorMedianWindow] = {};
    for (size_t index = 0U; index < count_; ++index) {
        sorted[index] = samples_[index];
    }
    for (size_t left = 1U; left < count_; ++left) {
        const int16_t value = sorted[left];
        size_t right = left;
        while (right > 0U && sorted[right - 1U] > value) {
            sorted[right] = sorted[right - 1U];
            --right;
        }
        sorted[right] = value;
    }
    if ((count_ & 1U) != 0U) {
        return static_cast<float>(sorted[count_ / 2U]);
    }
    const int32_t pair =
        static_cast<int32_t>(sorted[count_ / 2U - 1U]) +
        static_cast<int32_t>(sorted[count_ / 2U]);
    return static_cast<float>(pair) * 0.5f;
}

size_t MedianFilter::size() const {
    return count_;
}

} // namespace aquarium
