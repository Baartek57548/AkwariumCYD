#include "temp_control.h"
#include "hardware_i2c.h"
#include "wemos_config.h"
#include "uart_protocol.h"
#include <OneWire.h>
#include <DallasTemperature.h>

namespace {
OneWire one_wire_bus(WemosConfig::PIN_ONEWIRE);
DallasTemperature dallas_sensors(&one_wire_bus);
}

TempControl g_temp_control;

TempControl::TempControl()
    : current_temp_(25.0f),
      target_temp_(WemosConfig::DEFAULT_TEMP_TARGET),
      hysteresis_(WemosConfig::DEFAULT_TEMP_HYSTERESIS),
      heater_active_(false),
      sensor_ok_(false),
      consecutive_read_errors_(0),
      heater_on_start_ms_(0),
      last_read_ms_(0),
      alarm_flags_(AquaProtocol::ALARM_NONE)
{
}

bool TempControl::begin()
{
    dallas_sensors.begin();
    dallas_sensors.setWaitForConversion(false);
    dallas_sensors.requestTemperatures();
    sensor_ok_ = (dallas_sensors.getDeviceCount() > 0);
    return sensor_ok_;
}

void TempControl::setTarget(float target, float hyst)
{
    if (target >= 18.0f && target <= 32.0f) {
        target_temp_ = target;
    }
    if (hyst >= 0.1f && hyst <= 2.0f) {
        hysteresis_ = hyst;
    }
}

void TempControl::clearAlarms()
{
    alarm_flags_ = AquaProtocol::ALARM_NONE;
}

void TempControl::update(uint32_t now_ms)
{
    // Odczyt temperatury co 1000 ms
    if (now_ms - last_read_ms_ >= 1000UL) {
        last_read_ms_ = now_ms;

        float t = dallas_sensors.getTempCByIndex(0);
        dallas_sensors.requestTemperatures(); // Żądanie kolejnej konwersji asynchronicznie

        if (t > -50.0f && t < 80.0f && t != 85.0f) {
            current_temp_ = t;
            sensor_ok_ = true;
            consecutive_read_errors_ = 0;
            alarm_flags_ &= ~AquaProtocol::ALARM_TEMP_SENSOR_FAIL;
        } else {
            ++consecutive_read_errors_;
            if (consecutive_read_errors_ >= 5) {
                sensor_ok_ = false;
                alarm_flags_ |= AquaProtocol::ALARM_TEMP_SENSOR_FAIL;
            }
        }
    }

    // Bezpieczeństwo awaryjne - brak czujnika
    if (!sensor_ok_) {
        if (heater_active_) {
            heater_active_ = false;
            g_hardware_i2c.setRelay(WemosConfig::RELAY_CH_HEATER, false);
        }
        return;
    }

    // Sprawdzenie przegrzania (Overheat)
    if (current_temp_ >= WemosConfig::TEMP_OVERHEAT_LIMIT) {
        alarm_flags_ |= AquaProtocol::ALARM_OVERHEAT;
        if (heater_active_) {
            heater_active_ = false;
            g_hardware_i2c.setRelay(WemosConfig::RELAY_CH_HEATER, false);
        }
        return;
    } else {
        alarm_flags_ &= ~AquaProtocol::ALARM_OVERHEAT;
    }

    // Sprawdzenie przechłodzenia (Underheat)
    if (current_temp_ <= WemosConfig::TEMP_UNDERHEAT_LIMIT) {
        alarm_flags_ |= AquaProtocol::ALARM_UNDERHEAT;
    } else {
        alarm_flags_ &= ~AquaProtocol::ALARM_UNDERHEAT;
    }

    // Logika termostatu z histerezą
    if (!heater_active_) {
        if (current_temp_ <= (target_temp_ - hysteresis_)) {
            heater_active_ = true;
            heater_on_start_ms_ = now_ms;
            g_hardware_i2c.setRelay(WemosConfig::RELAY_CH_HEATER, true);
        }
    } else {
        // Zabezpieczenie limitu ciągłego czasu pracy grzałki
        if (now_ms - heater_on_start_ms_ >= WemosConfig::HEATER_MAX_RUN_MS) {
            alarm_flags_ |= AquaProtocol::ALARM_HEATER_TIMEOUT;
            heater_active_ = false;
            g_hardware_i2c.setRelay(WemosConfig::RELAY_CH_HEATER, false);
            return;
        }

        if (current_temp_ >= target_temp_) {
            heater_active_ = false;
            g_hardware_i2c.setRelay(WemosConfig::RELAY_CH_HEATER, false);
        }
    }
}
