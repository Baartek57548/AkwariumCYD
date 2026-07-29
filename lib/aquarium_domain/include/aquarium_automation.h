#ifndef AQUARIUM_AUTOMATION_H
#define AQUARIUM_AUTOMATION_H

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
};

enum AlarmFlags {
    AlarmNone = 0,
    AlarmTemperatureHigh = 1 << 0,
    AlarmTemperatureLow = 1 << 1,
    AlarmPhOutOfRange = 1 << 2,
    AlarmWaterLevelLow = 1 << 3,
    AlarmLeak = 1 << 4,
    AlarmSupplyLow = 1 << 5
};

bool thermostat_next_state(const ThermostatInput &input);
GasControlOutput evaluate_gas_control(const GasControlInput &input);
bool evaluate_ato_control(const AtoControlInput &input);
unsigned int evaluate_alarm_flags(const AlarmInput &input);
unsigned int alarm_count(unsigned int flags);

} // namespace aquarium

#endif // AQUARIUM_AUTOMATION_H
