#ifndef AQUABOX_UI_PLAN_H
#define AQUABOX_UI_PLAN_H

#include "ui_model.h"

namespace ui_plan {
struct Interval {
    uint16_t start;
    uint16_t end;
};
struct DayIntervals {
    Interval items[2]{};
    uint8_t count = 0;
};
struct LightSegment {
    uint16_t start;
    uint16_t end;
    ui_model::LightMode mode;
};
struct LightDaySegments {
    LightSegment items[8]{};
    uint8_t count = 0;
};
// The final pixel represents 24:00; a real clock minute stays inside this closed axis.
uint16_t minutePosition(uint16_t minute, uint16_t width);
DayIntervals intervalsForDay(const ui_model::Schedule &schedule, uint8_t day);
LightDaySegments lightSegmentsForDay(const ui_model::Schedule &schedule,
                                     const ui_model::LightProgram &program, uint8_t day);
} // namespace ui_plan

#endif
