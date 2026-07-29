#include "aquarium_automation.h"

#include <math.h>

namespace aquarium {

AlarmStabilityFilter::AlarmStabilityFilter(
    uint8_t raise_samples,
    uint8_t clear_samples,
    unsigned int immediate_raise_mask)
    : raise_samples_(raise_samples == 0U ? 1U : raise_samples),
      clear_samples_(clear_samples == 0U ? 1U : clear_samples),
      immediate_raise_mask_(immediate_raise_mask),
      stable_flags_(AlarmNone),
      raise_counts_{},
      clear_counts_{} {
}

void AlarmStabilityFilter::reset(unsigned int stable_flags) {
    const unsigned int known_mask =
        (1U << kTrackedFlagCount) - 1U;
    stable_flags_ = stable_flags & known_mask;
    for (uint8_t index = 0U;
         index < kTrackedFlagCount;
         ++index) {
        raise_counts_[index] = 0U;
        clear_counts_[index] = 0U;
    }
}

unsigned int AlarmStabilityFilter::update(
    unsigned int observed_flags) {
    const unsigned int known_mask =
        (1U << kTrackedFlagCount) - 1U;
    observed_flags &= known_mask;

    for (uint8_t index = 0U;
         index < kTrackedFlagCount;
         ++index) {
        const unsigned int flag = 1U << index;
        const bool observed = (observed_flags & flag) != 0U;
        const bool stable = (stable_flags_ & flag) != 0U;
        if (observed == stable) {
            raise_counts_[index] = 0U;
            clear_counts_[index] = 0U;
            continue;
        }

        if (observed) {
            clear_counts_[index] = 0U;
            if ((immediate_raise_mask_ & flag) != 0U) {
                stable_flags_ |= flag;
                raise_counts_[index] = 0U;
                continue;
            }
            if (raise_counts_[index] < raise_samples_) {
                ++raise_counts_[index];
            }
            if (raise_counts_[index] >= raise_samples_) {
                stable_flags_ |= flag;
                raise_counts_[index] = 0U;
            }
            continue;
        }

        raise_counts_[index] = 0U;
        if (clear_counts_[index] < clear_samples_) {
            ++clear_counts_[index];
        }
        if (clear_counts_[index] >= clear_samples_) {
            stable_flags_ &= ~flag;
            clear_counts_[index] = 0U;
        }
    }
    return stable_flags_;
}

unsigned int AlarmStabilityFilter::stable_flags() const {
    return stable_flags_;
}

bool thermostat_next_state(const ThermostatInput &input) {
    if (!input.runtimeAvailable || !input.enabled || !input.thresholdMode ||
        !input.temperatureValid || !isfinite(input.temperatureC) ||
        !isfinite(input.targetC) || !isfinite(input.hysteresisC) || input.hysteresisC <= 0.0f) {
        return false;
    }

    if (input.temperatureC < input.targetC - input.hysteresisC) {
        return true;
    }
    if (input.temperatureC >= input.targetC) {
        return false;
    }
    return input.previousOn;
}

GasControlOutput evaluate_gas_control(const GasControlInput &input) {
    GasControlOutput output = {};
    if (!input.runtimeAvailable || input.leakDetected) {
        return output;
    }

    output.co2On = input.co2Enabled && input.co2WindowActive &&
                   input.phValid && isfinite(input.ph) &&
                   isfinite(input.targetPh) && input.ph > input.targetPh;
    output.aeratorOn = input.aeratorEnabled && input.aeratorWindowActive && !output.co2On;
    return output;
}

bool evaluate_ato_control(const AtoControlInput &input) {
    return input.runtimeAvailable && input.enabled && input.waterLevelValid &&
           !input.waterLevelHigh && !input.leakDetected && !input.timeoutLatched;
}

unsigned int evaluate_alarm_flags(const AlarmInput &input) {
    unsigned int flags = AlarmNone;
    if (input.temperatureValid && isfinite(input.temperatureC)) {
        if (input.temperatureC > 28.0f) {
            flags |= AlarmTemperatureHigh;
        } else if (input.temperatureC < 20.0f) {
            flags |= AlarmTemperatureLow;
        }
    }
    if (input.phValid && isfinite(input.ph) && (input.ph < 6.0f || input.ph > 8.0f)) {
        flags |= AlarmPhOutOfRange;
    }
    if (input.waterLevelValid && !input.waterLevelHigh) {
        flags |= AlarmWaterLevelLow;
    }
    if (input.leakValid && input.leakDetected) {
        flags |= AlarmLeak;
    }
    if (input.supplyValid && isfinite(input.supplyVoltage) && input.supplyVoltage < 4.65f) {
        flags |= AlarmSupplyLow;
    }
    if ((input.requiredSensors & ~input.presentSensors) != 0U) {
        flags |= AlarmSensorMissing;
    }
    if ((input.requiredSensors & input.staleSensors) != 0U) {
        flags |= AlarmSensorStale;
    }
    if (input.sensorBusFault) {
        flags |= AlarmSensorBusFault;
    }
    if (input.actuatorWriteFailed) {
        flags |= AlarmActuatorWriteFailed;
    }
    return flags;
}

unsigned int alarm_count(unsigned int flags) {
    unsigned int count = 0U;
    while (flags != 0U) {
        count += flags & 1U;
        flags >>= 1U;
    }
    return count;
}

} // namespace aquarium
