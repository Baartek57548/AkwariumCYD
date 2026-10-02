#include "ui_plan.h"

namespace ui_plan {
uint16_t minutePosition(uint16_t minute, uint16_t width) {
    if (width < 2)
        return 0;
    const uint16_t bounded = minute > 1440 ? 1440 : minute;
    return static_cast<uint16_t>(static_cast<uint32_t>(bounded) * (width - 1u) / 1440u);
}
DayIntervals intervalsForDay(const ui_model::Schedule &schedule, uint8_t day) {
    DayIntervals result;
    if (day > 6 || !ui_model::validateSchedule(schedule).ok() ||
        schedule.mode == ui_model::ScheduleMode::Disabled)
        return result;
    const bool today = (schedule.dayMask & (1u << day)) != 0;
    if (schedule.mode == ui_model::ScheduleMode::Always) {
        if (today)
            result.items[result.count++] = {0, 1440};
    } else if (schedule.startMinute < schedule.endMinute) {
        if (today)
            result.items[result.count++] = {schedule.startMinute, schedule.endMinute};
    } else {
        // An overnight interval after midnight belongs to the preceding enabled day.
        const uint8_t previous = static_cast<uint8_t>((day + 6u) % 7u);
        if (schedule.endMinute && (schedule.dayMask & (1u << previous)))
            result.items[result.count++] = {0, schedule.endMinute};
        if (today)
            result.items[result.count++] = {schedule.startMinute, 1440};
    }
    return result;
}
LightDaySegments lightSegmentsForDay(const ui_model::Schedule &schedule,
                                     const ui_model::LightProgram &program, uint8_t day) {
    LightDaySegments result;
    if (day >= 7 || schedule.mode != ui_model::ScheduleMode::Timed ||
        !ui_model::validateLightProgram(schedule, program).ok() || !program.enabled)
        return result;
    const uint16_t duration = static_cast<uint16_t>((schedule.endMinute + 1440u -
                                                     schedule.startMinute) % 1440u);
    const uint16_t points[5] = {
        0,
        static_cast<uint16_t>((program.dayStartMinute + 1440u - schedule.startMinute) % 1440u),
        static_cast<uint16_t>((program.duskStartMinute + 1440u - schedule.startMinute) % 1440u),
        static_cast<uint16_t>((program.nightStartMinute + 1440u - schedule.startMinute) % 1440u),
        duration};
    const ui_model::LightMode modes[4] = {ui_model::LightMode::Daybreak, ui_model::LightMode::Day,
                                          ui_model::LightMode::Daybreak, ui_model::LightMode::Night};
    for (unsigned anchor = 0; anchor < 2; ++anchor) {
        const uint8_t sourceDay = anchor == 0 ? static_cast<uint8_t>((day + 6u) % 7u) : day;
        if (!(schedule.dayMask & (1u << sourceDay)))
            continue;
        const int origin = static_cast<int>(schedule.startMinute) - (anchor == 0 ? 1440 : 0);
        for (unsigned stage = 0; stage < 4; ++stage) {
            int start = origin + points[stage];
            int end = origin + points[stage + 1];
            if (start < 0)
                start = 0;
            if (end > 1440)
                end = 1440;
            if (start < end)
                result.items[result.count++] = {static_cast<uint16_t>(start),
                                                static_cast<uint16_t>(end), modes[stage]};
        }
    }
    return result;
}
} // namespace ui_plan
