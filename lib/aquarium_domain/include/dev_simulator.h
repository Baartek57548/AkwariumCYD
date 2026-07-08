#ifndef AQUARIUM_DEV_SIMULATOR_H
#define AQUARIUM_DEV_SIMULATOR_H

#include <stdint.h>

#include "aquarium_automation.h"
#include "aquarium_schedule.h"

namespace aquarium {

struct DevSnapshot {
    float temperatureC;
    float ph;
    float phVoltage;
    int16_t phRaw;
    float ecConductivity;
    float ecVoltage;
    int16_t ecRaw;
    int ldr;
    float batteryVoltage;
    uint8_t batteryPercent;
    float supplyVoltage;
    bool waterLevelHigh;
    bool leakDetected;
    bool flowActive;
    bool heaterOn;
    bool co2On;
    bool aeratorOn;
    bool filterOn;
    bool lightOn;
    LightProfile lightProfile;
    bool light2On;
    LightProfile light2Profile;
    unsigned int alarmFlags;
};

class DevSimulator {
public:
    explicit DevSimulator(uint32_t seed = 0x5A17C0DEUL);

    void reset(uint32_t seed = 0x5A17C0DEUL);
    const DevSnapshot &step(uint32_t now_ms, uint16_t minute_of_day, uint8_t second);
    const DevSnapshot &latest() const;

private:
    float noise();
    static float clamp(float value, float low, float high);
    static int16_t voltage_to_ads_raw(float voltage);

    uint32_t randomState_;
    uint32_t lastStepMs_;
    bool initialized_;
    DevSnapshot snapshot_;
};

DevSimulator &dev_simulator();

} // namespace aquarium

#endif // AQUARIUM_DEV_SIMULATOR_H
