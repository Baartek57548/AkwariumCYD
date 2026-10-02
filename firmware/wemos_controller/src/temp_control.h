#ifndef TEMP_CONTROL_H
#define TEMP_CONTROL_H

#include <Arduino.h>
#include <stdint.h>

class TempControl {
public:
    TempControl();

    bool begin();
    void update(uint32_t now_ms);

    float getTemperature() const { return current_temp_; }
    float getTargetTemp() const { return target_temp_; }
    float getHysteresis() const { return hysteresis_; }

    void setTarget(float target, float hyst);

    bool isHeaterActive() const { return heater_active_; }
    uint16_t getAlarms() const { return alarm_flags_; }
    void clearAlarms();

    bool isSensorValid() const { return sensor_ok_; }

private:
    float current_temp_;
    float target_temp_;
    float hysteresis_;
    bool heater_active_;
    bool sensor_ok_;
    uint8_t consecutive_read_errors_;
    uint32_t heater_on_start_ms_;
    uint32_t last_read_ms_;
    uint16_t alarm_flags_;

    float readOneWireTemp();
};

extern TempControl g_temp_control;

#endif // TEMP_CONTROL_H
