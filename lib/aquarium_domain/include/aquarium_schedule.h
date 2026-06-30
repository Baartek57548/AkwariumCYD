#ifndef AQUARIUM_SCHEDULE_H
#define AQUARIUM_SCHEDULE_H

#include <stdint.h>

namespace aquarium {

enum class ScheduleMode : uint8_t {
    Schedule = 0,
    AlwaysOn = 1,
    AlwaysOff = 2
};

enum class LightProfile : uint8_t {
    Day = 0,
    Daybreak = 1,
    Night = 2
};

struct TimeOfDay {
    uint8_t hour;
    uint8_t minute;
};

struct TimeWindow {
    TimeOfDay start;
    TimeOfDay end;
};

struct FactoryScheduleState {
    bool lightOn;
    LightProfile lightProfile;
    bool filterOn;
    bool gasWindowActive;
    bool feedingDue;
    bool heaterMonitoringActive;
    bool waterLevelMonitoringActive;
    bool leakMonitoringActive;
};

namespace factory {

constexpr TimeWindow kLightWindow = {{10U, 0U}, {22U, 0U}};
constexpr TimeWindow kMorningDaybreak = {{10U, 0U}, {10U, 30U}};
constexpr TimeWindow kDay = {{10U, 30U}, {20U, 0U}};
constexpr TimeWindow kEveningDaybreak = {{20U, 0U}, {21U, 0U}};
constexpr TimeWindow kNight = {{21U, 0U}, {22U, 0U}};
constexpr TimeWindow kFilter = {{10U, 30U}, {20U, 30U}};
constexpr TimeWindow kGas = {{10U, 0U}, {19U, 0U}};
constexpr TimeOfDay kFeeding = {14U, 0U};
constexpr float kCo2TargetPh = 6.80f;

} // namespace factory

bool is_valid_time(TimeOfDay value);
uint16_t minutes_since_midnight(TimeOfDay value);
bool is_within_window(uint16_t now_minutes, TimeWindow window);
bool schedule_active(ScheduleMode mode, uint16_t now_minutes, TimeWindow window);
bool factory_light_profile_at(uint16_t now_minutes, LightProfile *profile);
FactoryScheduleState factory_schedule_at(uint16_t now_minutes, uint8_t second);
bool feeding_due(uint16_t now_minutes, uint8_t second, TimeOfDay feeding_time);
const char *light_profile_code(LightProfile profile);

} // namespace aquarium

#endif // AQUARIUM_SCHEDULE_H
