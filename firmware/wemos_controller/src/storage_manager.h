#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include <Arduino.h>
#include "automation_engine.h"

struct ControllerConfig {
    uint32_t magic;
    uint16_t version;
    float target_temp;
    float hysteresis;
    float ph_cal_offset;
    float ph_cal_slope;
    DeviceSchedule schedules[4];
    uint32_t checksum;
};

class StorageManager {
public:
    StorageManager();

    bool begin();
    bool saveConfig(const ControllerConfig &cfg);
    bool loadConfig(ControllerConfig &cfg);
    void resetToDefaults(ControllerConfig &cfg);

private:
    uint32_t calculateChecksum(const ControllerConfig &cfg);
};

extern StorageManager g_storage_manager;

#endif // STORAGE_MANAGER_H
