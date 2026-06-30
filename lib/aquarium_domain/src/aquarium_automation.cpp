#include "aquarium_automation.h"

#include <math.h>

namespace aquarium {

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
