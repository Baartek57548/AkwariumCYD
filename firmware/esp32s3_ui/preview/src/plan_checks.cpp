#include "ui_plan.h"
#include <cstdio>

bool ui_plan_checks() {
    using namespace ui_model;
    int checks = 0, failures = 0;
    auto require = [&](bool valid, const char *name) {
        ++checks;
        if (!valid) {
            ++failures;
            std::fprintf(stderr, "FAIL PLAN: %s\n", name);
        }
    };
    require(ui_plan::minutePosition(0, 386) == 0, "midnight at left edge");
    require(ui_plan::minutePosition(720, 386) == 192, "noon at midpoint");
    require(ui_plan::minutePosition(1439, 386) == 384, "23:59 stays inside axis");
    require(ui_plan::minutePosition(1440, 386) == 385, "24:00 right boundary");
    require(ui_plan::minutePosition(65535, 386) == 385, "invalid minute clamped");
    require(ui_plan::minutePosition(720, 0) == 0 && ui_plan::minutePosition(720, 1) == 0,
            "empty width has no underflow");
    const Schedule normal{480, 1200, ScheduleMode::Timed, 1};
    auto spans = ui_plan::intervalsForDay(normal, 0);
    require(spans.count == 1 && spans.items[0].start == 480 && spans.items[0].end == 1200,
            "enabled daytime interval");
    require(ui_plan::intervalsForDay(normal, 1).count == 0, "disabled day has no bar");
    const Schedule overnight{1320, 360, ScheduleMode::Timed, 1};
    spans = ui_plan::intervalsForDay(overnight, 0);
    require(spans.count == 1 && spans.items[0].start == 1320 && spans.items[0].end == 1440,
            "Monday night starts at 22:00");
    spans = ui_plan::intervalsForDay(overnight, 1);
    require(spans.count == 1 && spans.items[0].start == 0 && spans.items[0].end == 360,
            "Tuesday includes Monday tail");
    require(ui_plan::intervalsForDay(overnight, 2).count == 0, "Wednesday excludes disabled Tuesday");
    spans = ui_plan::intervalsForDay({1320, 360, ScheduleMode::Timed, 127}, 0);
    require(spans.count == 2 && spans.items[0].end == 360 && spans.items[1].start == 1320,
            "everyday overnight interval splits into two ordered bars");
    spans = ui_plan::intervalsForDay({1320, 360, ScheduleMode::Timed, 64}, 0);
    require(spans.count == 1 && spans.items[0].end == 360, "Sunday tail crosses week boundary");
    spans = ui_plan::intervalsForDay({1320, 0, ScheduleMode::Timed, 1}, 1);
    require(spans.count == 0, "exact midnight end does not add empty tail");
    spans = ui_plan::intervalsForDay({0, 0, ScheduleMode::Always, 1}, 0);
    require(spans.count == 1 && spans.items[0].start == 0 && spans.items[0].end == 1440,
            "always mode occupies full enabled day");
    require(ui_plan::intervalsForDay({0, 0, ScheduleMode::Always, 1}, 1).count == 0,
            "always respects day mask");
    require(ui_plan::intervalsForDay({480, 1200, ScheduleMode::Disabled, 127}, 0).count == 0,
            "disabled mode draws no bar");
    require(ui_plan::intervalsForDay(normal, 7).count == 0, "invalid weekday rejected");
    require(ui_plan::intervalsForDay({480, 480, ScheduleMode::Timed, 127}, 0).count == 0,
            "invalid equal endpoints rejected");
    const LightProgram daytime{600, 1080, 1140, true};
    auto light = ui_plan::lightSegmentsForDay({540, 1260, ScheduleMode::Timed, 1}, daytime, 0);
    require(light.count == 4 && light.items[0].start == 540 && light.items[0].end == 600 &&
                light.items[0].mode == LightMode::Daybreak && light.items[1].mode == LightMode::Day &&
                light.items[2].mode == LightMode::Daybreak && light.items[3].mode == LightMode::Night &&
                light.items[3].end == 1260,
            "daylight plan shows all four colored phases");
    light = ui_plan::lightSegmentsForDay(overnight, {1380, 60, 180, true}, 1);
    require(light.count == 3 && light.items[0].start == 0 && light.items[0].end == 60 &&
                light.items[0].mode == LightMode::Day && light.items[1].mode == LightMode::Daybreak &&
                light.items[2].mode == LightMode::Night && light.items[2].end == 360,
            "next day includes prior evening's profile tail");
    require(ui_plan::lightSegmentsForDay(overnight, {1380, 60, 180, true}, 2).count == 0,
            "disabled source day contributes no light segments");
    std::printf("PLAN PASS checks=%d failures=%d\n", checks, failures);
    return failures == 0;
}
