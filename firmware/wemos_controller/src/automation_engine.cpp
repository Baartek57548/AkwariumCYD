#include "automation_engine.h"
#include "hardware_i2c.h"
#include "wemos_config.h"

AutomationEngine g_automation_engine;

AutomationEngine::AutomationEngine()
    : manual_mask_(0),
      manual_states_(0),
      feeding_in_progress_(false),
      feeding_start_ms_(0),
      aquael_sync_in_progress_(false),
      aquael_sync_step_ms_(0),
      aquael_sync_step_(0),
      aquael_mode_(1)
{
    // Domyślne harmonogramy
    // 0: Światło (08:00 - 20:00)
    schedules_[0] = { true, 8, 0, 20, 0 };
    // 1: Filtr (Zawsze włączony)
    schedules_[1] = { true, 0, 0, 23, 59 };
    // 2: Napowietrzacz (22:00 - 06:00)
    schedules_[2] = { true, 22, 0, 6, 0 };
    // 3: CO2 (07:30 - 19:30)
    schedules_[3] = { true, 7, 30, 19, 30 };
}

void AutomationEngine::begin()
{
    // Wstępne załączenie filtra jeśli włączony w harmonogramie
    g_hardware_i2c.setRelay(WemosConfig::RELAY_CH_FILTER, true);
}

void AutomationEngine::setSchedule(uint8_t dev_id, const DeviceSchedule &sched)
{
    if (dev_id < 4) {
        schedules_[dev_id] = sched;
    }
}

const DeviceSchedule &AutomationEngine::getSchedule(uint8_t dev_id) const
{
    static const DeviceSchedule empty_sched = { false, 0, 0, 0, 0 };
    if (dev_id < 4) {
        return schedules_[dev_id];
    }
    return empty_sched;
}

void AutomationEngine::setManualOverride(uint8_t channel, bool state)
{
    if (channel < 8) {
        manual_mask_ |= (1U << channel);
        if (state) {
            manual_states_ |= (1U << channel);
        } else {
            manual_states_ &= ~(1U << channel);
        }
        g_hardware_i2c.setRelay(channel, state);
    }
}

void AutomationEngine::clearManualOverride(uint8_t channel)
{
    if (channel < 8) {
        manual_mask_ &= ~(1U << channel);
    }
}

bool AutomationEngine::isTimeInRange(uint8_t cur_h, uint8_t cur_m, uint8_t on_h, uint8_t on_m, uint8_t off_h, uint8_t off_m)
{
    uint16_t cur_mins = cur_h * 60 + cur_m;
    uint16_t on_mins  = on_h * 60 + on_m;
    uint16_t off_mins = off_h * 60 + off_m;

    if (on_mins == off_mins) {
        return true; // 24h aktywne
    }

    if (on_mins < off_mins) {
        return (cur_mins >= on_mins && cur_mins < off_mins);
    } else {
        // Przejście przez północ (np. 22:00 do 06:00)
        return (cur_mins >= on_mins || cur_mins < off_mins);
    }
}

void AutomationEngine::triggerFeeder()
{
    feeding_in_progress_ = true;
    feeding_start_ms_ = millis();
    // Załączenie silnika karmnika
    g_hardware_i2c.setRelay(WemosConfig::RELAY_CH_FEEDER, true);
    // Wyłączenie filtra na czas karmienia (5 minut)
    g_hardware_i2c.setRelay(WemosConfig::RELAY_CH_FILTER, false);
}

void AutomationEngine::triggerAquaelSync()
{
    aquael_sync_in_progress_ = true;
    aquael_sync_step_ = 0;
    aquael_sync_step_ms_ = millis();
    // Krok 0: Wyłączenie zasilania obu lamp
    g_hardware_i2c.setRelay(WemosConfig::RELAY_CH_LIGHT_A, false);
    g_hardware_i2c.setRelay(WemosConfig::RELAY_CH_LIGHT_B, false);
}

void AutomationEngine::updateAquaelSync(uint32_t now_ms)
{
    if (!aquael_sync_in_progress_) return;

    uint32_t elapsed = now_ms - aquael_sync_step_ms_;

    // Krok 0: 10 sekund wyłączenia zasilania lamp dla twardego resetu do trybu 1 (Day)
    if (aquael_sync_step_ == 0) {
        if (elapsed >= 10000UL) {
            aquael_sync_step_ = 1;
            aquael_sync_step_ms_ = now_ms;
            // Włączenie lamp (są teraz w trybie 1)
            g_hardware_i2c.setRelay(WemosConfig::RELAY_CH_LIGHT_A, true);
            g_hardware_i2c.setRelay(WemosConfig::RELAY_CH_LIGHT_B, true);
            aquael_mode_ = 1;
            aquael_sync_in_progress_ = false;
        }
    }
}

void AutomationEngine::update(uint32_t current_epoch, uint32_t now_ms)
{
    // Obsługa synchronizacji lamp Aquael
    updateAquaelSync(now_ms);

    // Obsługa karmienia
    if (feeding_in_progress_) {
        // Wyłączenie silnika karmnika po 1.5 sekundy impulsu
        if (now_ms - feeding_start_ms_ >= 1500UL) {
            g_hardware_i2c.setRelay(WemosConfig::RELAY_CH_FEEDER, false);
        }
        // Przywrócenie filtra po 5 minutach (300 000 ms)
        if (now_ms - feeding_start_ms_ >= 300000UL) {
            feeding_in_progress_ = false;
            if ((manual_mask_ & (1U << WemosConfig::RELAY_CH_FILTER)) == 0) {
                g_hardware_i2c.setRelay(WemosConfig::RELAY_CH_FILTER, true);
            }
        }
    }

    if (aquael_sync_in_progress_) {
        return; // Podczas synchronizacji lamp nie zmieniamy stanów świateł harmonogramem
    }

    // Wyliczenie aktualnej godziny i minuty z epoch
    uint32_t secs_in_day = current_epoch % 86400UL;
    uint8_t cur_h = secs_in_day / 3600UL;
    uint8_t cur_m = (secs_in_day % 3600UL) / 60UL;

    // 1. Światło (Light A i Light B)
    if ((manual_mask_ & (1U << WemosConfig::RELAY_CH_LIGHT_A)) == 0) {
        bool light_on = schedules_[0].enabled && isTimeInRange(cur_h, cur_m,
            schedules_[0].on_hour, schedules_[0].on_minute,
            schedules_[0].off_hour, schedules_[0].off_minute);
        g_hardware_i2c.setRelay(WemosConfig::RELAY_CH_LIGHT_A, light_on);
        g_hardware_i2c.setRelay(WemosConfig::RELAY_CH_LIGHT_B, light_on);
    }

    // 2. Filtr (jeśli nie trwa karmienie)
    if (!feeding_in_progress_ && ((manual_mask_ & (1U << WemosConfig::RELAY_CH_FILTER)) == 0)) {
        bool filter_on = schedules_[1].enabled && isTimeInRange(cur_h, cur_m,
            schedules_[1].on_hour, schedules_[1].on_minute,
            schedules_[1].off_hour, schedules_[1].off_minute);
        g_hardware_i2c.setRelay(WemosConfig::RELAY_CH_FILTER, filter_on);
    }

    // 3. Napowietrzacz
    if ((manual_mask_ & (1U << WemosConfig::RELAY_CH_AERATOR)) == 0) {
        bool aerator_on = schedules_[2].enabled && isTimeInRange(cur_h, cur_m,
            schedules_[2].on_hour, schedules_[2].on_minute,
            schedules_[2].off_hour, schedules_[2].off_minute);
        g_hardware_i2c.setRelay(WemosConfig::RELAY_CH_AERATOR, aerator_on);
    }

    // 4. CO2
    if ((manual_mask_ & (1U << WemosConfig::RELAY_CH_CO2)) == 0) {
        bool co2_on = schedules_[3].enabled && isTimeInRange(cur_h, cur_m,
            schedules_[3].on_hour, schedules_[3].on_minute,
            schedules_[3].off_hour, schedules_[3].off_minute);
        g_hardware_i2c.setRelay(WemosConfig::RELAY_CH_CO2, co2_on);
    }
}
