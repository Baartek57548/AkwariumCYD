#ifndef AUTOMATION_ENGINE_H
#define AUTOMATION_ENGINE_H

#include <Arduino.h>
#include <stdint.h>

struct DeviceSchedule {
    bool enabled;
    uint8_t on_hour;
    uint8_t on_minute;
    uint8_t off_hour;
    uint8_t off_minute;
};

class AutomationEngine {
public:
    AutomationEngine();

    void begin();
    void update(uint32_t current_epoch, uint32_t now_ms);

    void setSchedule(uint8_t dev_id, const DeviceSchedule &sched);
    const DeviceSchedule &getSchedule(uint8_t dev_id) const;

    void triggerFeeder();
    void triggerAquaelSync();

    bool isFeedingActive() const { return feeding_in_progress_; }
    uint8_t getAquaelLightMode() const { return aquael_mode_; }

    void setManualOverride(uint8_t channel, bool state);
    void clearManualOverride(uint8_t channel);
    bool isManualOverride(uint8_t channel) const { return (manual_mask_ & (1U << channel)) != 0; }

private:
    DeviceSchedule schedules_[4]; // 0=Light, 1=Filter, 2=Aerator, 3=CO2
    uint8_t manual_mask_;
    uint8_t manual_states_;

    bool feeding_in_progress_;
    uint32_t feeding_start_ms_;

    bool aquael_sync_in_progress_;
    uint32_t aquael_sync_step_ms_;
    uint8_t aquael_sync_step_;
    uint8_t aquael_mode_;

    bool isTimeInRange(uint8_t cur_h, uint8_t cur_m, uint8_t on_h, uint8_t on_m, uint8_t off_h, uint8_t off_m);
    void updateAquaelSync(uint32_t now_ms);
};

extern AutomationEngine g_automation_engine;

#endif // AUTOMATION_ENGINE_H
