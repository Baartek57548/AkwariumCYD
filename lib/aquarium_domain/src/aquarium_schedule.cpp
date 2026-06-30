#include "aquarium_schedule.h"

namespace aquarium {

bool is_valid_time(TimeOfDay value) {
    return value.hour < 24U && value.minute < 60U;
}

uint16_t minutes_since_midnight(TimeOfDay value) {
    if (!is_valid_time(value)) {
        return 0U;
    }
    return static_cast<uint16_t>(value.hour) * 60U + static_cast<uint16_t>(value.minute);
}

bool is_within_window(uint16_t now_minutes, TimeWindow window) {
    if (now_minutes >= 24U * 60U || !is_valid_time(window.start) || !is_valid_time(window.end)) {
        return false;
    }

    const uint16_t start = minutes_since_midnight(window.start);
    const uint16_t end = minutes_since_midnight(window.end);
    if (start == end) {
        return false;
    }
    if (start < end) {
        return now_minutes >= start && now_minutes < end;
    }
    return now_minutes >= start || now_minutes < end;
}

bool schedule_active(ScheduleMode mode, uint16_t now_minutes, TimeWindow window) {
    if (mode == ScheduleMode::AlwaysOn) {
        return true;
    }
    if (mode == ScheduleMode::AlwaysOff) {
        return false;
    }
    return is_within_window(now_minutes, window);
}

bool factory_light_profile_at(uint16_t now_minutes, LightProfile *profile) {
    if (profile == nullptr) {
        return false;
    }

    if (is_within_window(now_minutes, factory::kMorningDaybreak) ||
        is_within_window(now_minutes, factory::kEveningDaybreak)) {
        *profile = LightProfile::Daybreak;
        return true;
    }
    if (is_within_window(now_minutes, factory::kDay)) {
        *profile = LightProfile::Day;
        return true;
    }
    if (is_within_window(now_minutes, factory::kNight)) {
        *profile = LightProfile::Night;
        return true;
    }

    *profile = LightProfile::Day;
    return false;
}

bool feeding_due(uint16_t now_minutes, uint8_t second, TimeOfDay feeding_time) {
    return second == 0U && is_valid_time(feeding_time) &&
           now_minutes == minutes_since_midnight(feeding_time);
}

FactoryScheduleState factory_schedule_at(uint16_t now_minutes, uint8_t second) {
    FactoryScheduleState state = {};
    state.lightOn = factory_light_profile_at(now_minutes, &state.lightProfile);
    state.filterOn = is_within_window(now_minutes, factory::kFilter);
    state.gasWindowActive = is_within_window(now_minutes, factory::kGas);
    state.feedingDue = feeding_due(now_minutes, second, factory::kFeeding);
    state.heaterMonitoringActive = true;
    state.waterLevelMonitoringActive = true;
    state.leakMonitoringActive = true;
    return state;
}

const char *light_profile_code(LightProfile profile) {
    switch (profile) {
    case LightProfile::Daybreak:
        return "daybreak";
    case LightProfile::Night:
        return "night";
    case LightProfile::Day:
    default:
        return "day";
    }
}

} // namespace aquarium
