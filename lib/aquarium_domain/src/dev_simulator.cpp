#include "dev_simulator.h"

#include <math.h>
#include <string.h>

namespace aquarium {

DevSimulator::DevSimulator(uint32_t seed) {
    reset(seed);
}

void DevSimulator::reset(uint32_t seed) {
    randomState_ = seed == 0U ? 0x5A17C0DEUL : seed;
    lastStepMs_ = 0U;
    initialized_ = false;
    memset(&snapshot_, 0, sizeof(snapshot_));
    snapshot_.temperatureC = 25.25f;
    snapshot_.ph = 6.92f;
    snapshot_.ecConductivity = 442.0f;
    snapshot_.phVoltage = ((snapshot_.ph - 4.0f) / 6.0f) * 4.096f;
    snapshot_.ecVoltage = snapshot_.ecConductivity / 1000.0f;
    snapshot_.phRaw = voltage_to_ads_raw(snapshot_.phVoltage);
    snapshot_.ecRaw = voltage_to_ads_raw(snapshot_.ecVoltage);
    snapshot_.ldr = 900;
    snapshot_.waterLevelHigh = true;
    snapshot_.supplyVoltage = 5.02f;
    snapshot_.batteryVoltage = 3.25f;
    snapshot_.batteryPercent = 82U;
    snapshot_.lightProfile = LightProfile::Day;
}

float DevSimulator::noise() {
    randomState_ = randomState_ * 1664525UL + 1013904223UL;
    const uint16_t raw = static_cast<uint16_t>((randomState_ >> 16U) & 0x03FFU);
    return static_cast<float>(raw) / 511.5f - 1.0f;
}

float DevSimulator::clamp(float value, float low, float high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

int16_t DevSimulator::voltage_to_ads_raw(float voltage) {
    const float bounded = clamp(voltage, 0.0f, 4.095f);
    return static_cast<int16_t>(lroundf((bounded * 32767.0f) / 4.096f));
}

const DevSnapshot &DevSimulator::step(uint32_t now_ms, uint16_t minute_of_day, uint8_t second) {
    float dt = 1.0f;
    if (initialized_) {
        dt = static_cast<float>(static_cast<uint32_t>(now_ms - lastStepMs_)) * 0.001f;
        dt = clamp(dt, 0.05f, 5.0f);
    }
    initialized_ = true;
    lastStepMs_ = now_ms;

    const float time_seconds = static_cast<float>(now_ms) * 0.001f;
    const FactoryScheduleState schedule = factory_schedule_at(minute_of_day, second);

    // Incydenty są krótkie i deterministyczne, dzięki czemu UI alarmów można powtarzalnie testować.
    const uint32_t scenario_second = now_ms / 1000U;
    const uint32_t water_scenario = scenario_second % 1200U;
    snapshot_.waterLevelHigh = !(water_scenario >= 600U && water_scenario < 625U);
    snapshot_.leakDetected = scenario_second % 1800U >= 900U && scenario_second % 1800U < 920U;
    snapshot_.flowActive = schedule.filterOn && ((scenario_second / 4U) % 2U == 0U);
    snapshot_.supplyVoltage = 5.01f + 0.035f * sinf(time_seconds / 83.0f);
    if (scenario_second % 2700U >= 1350U && scenario_second % 2700U < 1370U) {
        snapshot_.supplyVoltage = 4.55f;
    }

    const GasControlInput gas_input = {
        true,
        true,
        true,
        schedule.gasWindowActive,
        schedule.gasWindowActive,
        true,
        snapshot_.ph,
        factory::kCo2TargetPh,
        snapshot_.leakDetected
    };
    const GasControlOutput gas = evaluate_gas_control(gas_input);
    snapshot_.co2On = gas.co2On;
    snapshot_.aeratorOn = gas.aeratorOn;

    const float ambient = 24.35f + 0.18f * sinf(time_seconds / 240.0f);
    const ThermostatInput thermostat_input = {
        true,
        true,
        true,
        true,
        snapshot_.temperatureC,
        26.0f,
        0.45f,
        snapshot_.heaterOn
    };
    snapshot_.heaterOn = thermostat_next_state(thermostat_input);
    const float thermal_rate = snapshot_.heaterOn
                                   ? 0.010f
                                   : (ambient - snapshot_.temperatureC) * 0.004f;
    snapshot_.temperatureC += thermal_rate * dt + noise() * 0.008f;
    snapshot_.temperatureC = clamp(snapshot_.temperatureC, 23.8f, 26.2f);

    const float ph_target = snapshot_.co2On ? 6.72f : 6.94f;
    snapshot_.ph += (ph_target - snapshot_.ph) * 0.006f * dt + noise() * 0.0018f;
    snapshot_.ph = clamp(snapshot_.ph, 6.55f, 7.15f);
    snapshot_.ecConductivity = 442.0f + 21.0f * sinf(time_seconds / 97.0f) + 3.0f * noise();
    snapshot_.ecConductivity = clamp(snapshot_.ecConductivity, 390.0f, 500.0f);

    snapshot_.lightOn = schedule.lightOn;
    snapshot_.lightProfile = schedule.lightProfile;
    snapshot_.filterOn = schedule.filterOn;
    float ldr_base = 95.0f;
    if (snapshot_.lightOn) {
        if (snapshot_.lightProfile == LightProfile::Day) ldr_base = 1280.0f;
        if (snapshot_.lightProfile == LightProfile::Daybreak) ldr_base = 690.0f;
        if (snapshot_.lightProfile == LightProfile::Night) ldr_base = 330.0f;
    }
    snapshot_.ldr = static_cast<int>(lroundf(clamp(ldr_base + 24.0f * noise(), 60.0f, 1550.0f)));

    snapshot_.phVoltage = clamp(((snapshot_.ph - 4.0f) / 6.0f) * 4.096f, 0.0f, 4.095f);
    snapshot_.ecVoltage = clamp(snapshot_.ecConductivity / 1000.0f, 0.0f, 4.095f);
    snapshot_.phRaw = voltage_to_ads_raw(snapshot_.phVoltage);
    snapshot_.ecRaw = voltage_to_ads_raw(snapshot_.ecVoltage);
    snapshot_.batteryVoltage = 3.25f + 0.025f * sinf(time_seconds / 310.0f);
    snapshot_.batteryPercent = static_cast<uint8_t>(clamp(80.0f + 7.0f * sinf(time_seconds / 420.0f), 0.0f, 100.0f));

    const AlarmInput alarms = {
        true,
        snapshot_.temperatureC,
        true,
        snapshot_.ph,
        true,
        snapshot_.waterLevelHigh,
        true,
        snapshot_.leakDetected,
        true,
        snapshot_.supplyVoltage
    };
    snapshot_.alarmFlags = evaluate_alarm_flags(alarms);
    return snapshot_;
}

const DevSnapshot &DevSimulator::latest() const {
    return snapshot_;
}

DevSimulator &dev_simulator() {
    static DevSimulator instance;
    return instance;
}

} // namespace aquarium
