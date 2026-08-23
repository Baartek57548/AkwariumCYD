#ifndef AQUARIUM_AUTOMATION_H
#define AQUARIUM_AUTOMATION_H

#include <stdint.h>

namespace aquarium {

struct ThermostatInput {
    bool runtimeAvailable;
    bool enabled;
    bool thresholdMode;
    bool temperatureValid;
    float temperatureC;
    float targetC;
    float hysteresisC;
    bool previousOn;
};

struct GasControlInput {
    bool runtimeAvailable;
    bool co2Enabled;
    bool aeratorEnabled;
    bool co2WindowActive;
    bool aeratorWindowActive;
    bool phValid;
    float ph;
    float targetPh;
    bool leakDetected;
};

struct GasControlOutput {
    bool co2On;
    bool aeratorOn;
};

struct AtoControlInput {
    bool runtimeAvailable;
    bool enabled;
    bool waterLevelValid;
    bool waterLevelHigh;
    bool leakDetected;
    bool timeoutLatched;
};

struct AlarmInput {
    bool temperatureValid;
    float temperatureC;
    bool phValid;
    float ph;
    bool waterLevelValid;
    bool waterLevelHigh;
    bool leakValid;
    bool leakDetected;
    bool supplyValid;
    float supplyVoltage;
    unsigned int requiredSensors;
    unsigned int presentSensors;
    unsigned int staleSensors;
    bool sensorBusFault;
    bool actuatorWriteFailed;
};

enum AlarmFlags {
    AlarmNone = 0,
    AlarmTemperatureHigh = 1 << 0,
    AlarmTemperatureLow = 1 << 1,
    AlarmPhOutOfRange = 1 << 2,
    AlarmWaterLevelLow = 1 << 3,
    AlarmLeak = 1 << 4,
    AlarmSupplyLow = 1 << 5,
    AlarmSensorMissing = 1 << 6,
    AlarmSensorStale = 1 << 7,
    AlarmSensorBusFault = 1 << 8,
    AlarmActuatorWriteFailed = 1 << 9
};

enum AlarmSensorFlags {
    AlarmSensorTemperature = 1 << 0,
    AlarmSensorPh = 1 << 1,
    AlarmSensorEc = 1 << 2,
    AlarmSensorWaterLevel = 1 << 3,
    AlarmSensorLeak = 1 << 4
};

/**
 * Stabilizes independently evaluated alarm bits before they are persisted or
 * forwarded. Short threshold chatter is ignored, while alarms that represent
 * immediate physical danger can bypass only the raise confirmation.
 */
class AlarmStabilityFilter {
public:
    AlarmStabilityFilter(
        uint8_t raise_samples = 2U,
        uint8_t clear_samples = 3U,
        unsigned int immediate_raise_mask =
            AlarmLeak | AlarmActuatorWriteFailed);

    void reset(unsigned int stable_flags = AlarmNone);
    unsigned int update(unsigned int observed_flags);
    unsigned int stable_flags() const;

private:
    static const uint8_t kTrackedFlagCount = 10U;

    uint8_t raise_samples_;
    uint8_t clear_samples_;
    unsigned int immediate_raise_mask_;
    unsigned int stable_flags_;
    uint8_t raise_counts_[kTrackedFlagCount];
    uint8_t clear_counts_[kTrackedFlagCount];
};

bool thermostat_next_state(const ThermostatInput &input);
GasControlOutput evaluate_gas_control(const GasControlInput &input);
bool evaluate_ato_control(const AtoControlInput &input);
unsigned int evaluate_alarm_flags(const AlarmInput &input);
unsigned int alarm_count(unsigned int flags);

} // namespace aquarium

#endif // AQUARIUM_AUTOMATION_H
