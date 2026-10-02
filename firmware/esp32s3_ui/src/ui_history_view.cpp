#include "ui_history_view.h"
#include "ui_design.h"
#include "ui_fonts.h"
#include "ui_history_time.h"
#include "ui_model.h"
#include "ui_theme.h"
#include <Arduino.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace ui_history_view {
namespace {
Colors palette{};
Handler handler = nullptr;
unsigned sensor = 0, rangeIndex = 0;
unsigned savedRanges[2]{};
uint8_t savedLayers = 0, focus = UINT8_MAX;
constexpr uint32_t windows[] = {3600, 43200, 86400, 172800, 259200};
constexpr const char *rangeNames[] = {"1 h", "12 h", "24 h", "48 h", "72 h"};
struct View {
    lv_obj_t *plot = nullptr, *caption = nullptr, *value = nullptr, *count = nullptr;
    lv_obj_t *minimum = nullptr, *maximum = nullptr, *coverage = nullptr, *heater = nullptr;
    lv_obj_t *empty = nullptr;
    lv_obj_t *axes[3]{}, *times[3]{}, *live = nullptr;
    unsigned kind = 0;
    bool expanded = false;
    float low = 0, high = 1;
};
View compact, detail, comparison[2];
lv_obj_t *list = nullptr, *fullscreen = nullptr, *popup = nullptr, *rangeButton = nullptr;
lv_coord_t scroll = 0;
uint32_t selection = UINT32_MAX, revision = UINT32_MAX, first = 0, last = 0;
const ui_model::HistorySample *samples[ui_model::kHistoryArchiveCapacity + ui_model::kHistoryCapacity]{};
unsigned sampleCount = 0;
uint8_t layers = 0;
enum class Popup : uint8_t { None, Range, Information };
Popup pendingPopup = Popup::None;
bool pendingDetail = false, pendingClose = false, pendingRange = false, pendingPopupClose = false;
uint32_t lastPointerUpdate = 0;
lv_point_t compactPress{};
bool compactMoved = false;
const ui_theme::Palette &theme() { return palette.theme; }
const char *waterName(ui_model::WaterState state) {
    switch (state) {
    case ui_model::WaterState::Normal:
        return "Norma / MAX";
    case ui_model::WaterState::Between:
        return "Między pływakami";
    case ui_model::WaterState::Low:
        return "Niski / MIN";
    case ui_model::WaterState::Conflict:
        return "Sprzeczne pływaki";
    case ui_model::WaterState::Unknown:
        return "Brak poziomu";
    }
    return "Brak poziomu";
}

lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius = 6) {
    lv_obj_t *object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, w, h);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(object, radius, 0);
    return object;
}
uint32_t legibleColor(lv_obj_t *parent, uint32_t desired) {
    for (lv_obj_t *node = parent; node; node = lv_obj_get_parent(node)) {
        if (lv_obj_get_style_bg_opa(node, LV_PART_MAIN) == LV_OPA_COVER) {
            const uint32_t background = lv_color_to32(lv_obj_get_style_bg_color(node, LV_PART_MAIN)) & 0xFFFFFFu;
            return ui_theme::readableForeground(desired, background);
        }
    }
    return ui_theme::readableForeground(desired, palette.background);
}
void setLegibleText(lv_obj_t *object, uint32_t desired) {
    lv_obj_set_style_text_color(object, lv_color_hex(legibleColor(lv_obj_get_parent(object), desired)), 0);
}
lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y, int w, uint32_t color,
                const lv_font_t *font = &ui_font_14) {
    lv_obj_t *object = lv_label_create(parent);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_width(object, w);
    lv_label_set_text(object, text);
    setLegibleText(object, color);
    lv_obj_set_style_text_font(object, font, 0);
    lv_obj_set_style_text_line_space(object, 4, 0);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_CLICKABLE);
    return object;
}
enum Button : unsigned {
    ClearLog = 5,
    Latest = 8,
    Back = 9,
    Range = 10,
    ChooseRange = 20,
    Ph = 21,
    Ec = 22,
    Heater = 23,
    Information = 24,
    ClosePopup = 25,
    FocusTemperature = 30,
    FocusPh = 31,
    FocusEc = 32,
    FocusAll = 33
};
void onButton(lv_event_t *event) {
    const unsigned action = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
    if (action == Latest)
        latest();
    else if (action == Back) {
        if (focus != UINT8_MAX && sensor != 4) {
            focus = UINT8_MAX;
            pendingRange = true;
        } else
            pendingClose = true;
    } else if (action == ChooseRange)
        pendingPopup = Popup::Range;
    else if (action == Information)
        pendingPopup = Popup::Information;
    else if (action == ClosePopup)
        pendingPopupClose = true;
    else if (action >= Ph && action <= Heater) {
        layers ^= 1u << (action - Ph);
        savedLayers = layers;
        focus = UINT8_MAX;
        pendingRange = true;
    } else if (action >= Range && action < Range + 5) {
        rangeIndex = action - Range;
        savedRanges[sensor == 4 ? 1 : 0] = rangeIndex;
        pendingRange = true;
    } else if (action >= FocusTemperature && action <= FocusAll) {
        focus = action == FocusAll ? UINT8_MAX : static_cast<uint8_t>(action - FocusTemperature);
        pendingRange = true;
    } else if (handler)
        handler(action <= 4 ? Command::Sensor : Command::ClearLog, action <= 4 ? action : 0);
}
lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int y, int w, unsigned action,
                 bool active = false, int height = 40) {
    lv_obj_t *object = box(parent, x, y, w, height, active ? palette.selected : theme().surface);
    lv_obj_add_flag(object, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_bg_color(object, lv_color_hex(palette.selected), LV_STATE_PRESSED);
    lv_obj_set_style_transform_zoom(object, ui_design::pressedZoom, LV_STATE_PRESSED);
    lv_obj_set_style_outline_color(object, lv_color_hex(theme().accent), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_width(object, 2, LV_STATE_FOCUS_KEY);
    // The 40 px chart controls have enough separation for a larger invisible
    // hit area; leave dense editor grids untouched to prevent overlapping hits.
    if (height < ui_design::minimumTouchHeight)
        lv_obj_set_ext_click_area(object, (ui_design::minimumTouchHeight - height + 1) / 2);
    lv_obj_set_style_opa(object, LV_OPA_70, LV_STATE_DISABLED);
    lv_obj_t *textLabel = label(object, text, 6, 0, w - 12, palette.text, &ui_font_16);
    lv_obj_set_style_text_align(textLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(textLabel);
    lv_obj_add_event_cb(object, onButton, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(action)));
    return object;
}
const ui_model::Measurement *reading(const ui_model::HistorySample *sample, unsigned kind = 0) {
    return !sample ? nullptr : kind == 0 ? &sample->temperature : kind == 1 ? &sample->ph : &sample->ec;
}
bool valid(const ui_model::Measurement *item) {
    return item && item->valid && !item->stale && std::isfinite(item->value);
}
void number(char *text, size_t size, float amount, unsigned kind = 0) {
    std::snprintf(text, size,
                  kind == 2   ? "%.0f"
                  : kind == 1 ? "%.2f"
                              : "%.1f",
                  static_cast<double>(amount));
    for (char *character = text; *character; ++character)
        if (*character == '.')
            *character = ',';
}
void offset(char *text, size_t size, uint32_t seconds) {
    if (seconds >= 3600)
        std::snprintf(text, size, "-%lu:%02lu h", static_cast<unsigned long>(seconds / 3600),
                      static_cast<unsigned long>((seconds / 60) % 60));
    else if (seconds >= 60)
        std::snprintf(text, size, "-%lu:%02lu min", static_cast<unsigned long>(seconds / 60),
                      static_cast<unsigned long>(seconds % 60));
    else
        std::snprintf(text, size, "-%lu s", static_cast<unsigned long>(seconds));
}
int xAt(uint32_t at, int width) {
    if (last == first)
        return width - 1;
    at = std::max(first, std::min(last, at));
    return static_cast<int>(static_cast<uint64_t>(at - first) * (width - 1) / (last - first));
}
int yAt(const View &view, float amount, int height) {
    const float fraction = std::max(0.0f, std::min(1.0f, (amount - view.low) / (view.high - view.low)));
    return static_cast<int>((1.0f - fraction) * (height - 1));
}
void gather() {
    sampleCount = 0;
    const auto *newest =
        ui_model::historyCount() ? ui_model::historyAt(ui_model::historyCount() - 1) : nullptr;
    last = newest ? newest->atSeconds : 0;
    if (sensor == 4) {
        const auto *water =
            ui_model::waterEventAt(ui_model::waterEventCount() ? ui_model::waterEventCount() - 1 : 0);
        const auto *dose = ui_model::atoDoseAt(ui_model::atoDoseCount() ? ui_model::atoDoseCount() - 1 : 0);
        if (water)
            last = std::max(last, water->atSeconds);
        if (dose)
            last = std::max(last, dose->endSeconds);
        first = last > windows[rangeIndex] ? last - windows[rangeIndex] : 0;
        return;
    }
    first = last > windows[rangeIndex] ? last - windows[rangeIndex] : 0;
    const auto *recent = ui_model::historyAt(0);
    // Merge coarse and fast rings without duplicate timestamps or pointer ownership allocations.
    for (unsigned index = 0; index < ui_model::historyArchiveCount(); ++index) {
        const auto *item = ui_model::historyArchiveAt(index);
        if (item->atSeconds >= first && (!recent || item->atSeconds < recent->atSeconds))
            samples[sampleCount++] = item;
    }
    for (unsigned index = 0; index < ui_model::historyCount(); ++index) {
        const auto *item = ui_model::historyAt(index);
        if (item->atSeconds >= first)
            samples[sampleCount++] = item;
    }
}
void drawPlot(lv_event_t *event) {
    const View &view = *static_cast<View *>(lv_event_get_user_data(event));
    lv_area_t area;
    lv_obj_get_coords(lv_event_get_target(event), &area);
    const int width = lv_area_get_width(&area), height = lv_area_get_height(&area);
    lv_draw_ctx_t *context = lv_event_get_draw_ctx(event);
    const auto &range = ui_model::snapshot().ranges[view.kind];
    lv_draw_rect_dsc_t fill;
    lv_draw_rect_dsc_init(&fill);
    fill.bg_color = lv_color_hex(palette.surface);
    lv_draw_rect(context, &fill, &area);
    fill.bg_color = lv_color_hex(palette.normalBand);
    fill.bg_opa = LV_OPA_40;
    const lv_area_t norm{area.x1, static_cast<lv_coord_t>(area.y1 + yAt(view, range.maximum, height)),
                         area.x2, static_cast<lv_coord_t>(area.y1 + yAt(view, range.minimum, height))};
    lv_draw_rect(context, &fill, &norm);
    lv_draw_line_dsc_t stroke;
    lv_draw_line_dsc_init(&stroke);
    stroke.color = lv_color_hex(palette.grid);
    stroke.opa = LV_OPA_50;
    for (int row = 0; row < 3; ++row) {
        const lv_coord_t y = area.y1 + row * (height - 1) / 2;
        const lv_point_t a{area.x1, y}, b{area.x2, y};
        lv_draw_line(context, &stroke, &a, &b);
    }
    for (int column = 0; column < 5; ++column) {
        const lv_coord_t x = area.x1 + column * (width - 1) / 4;
        const lv_point_t a{x, area.y1}, b{x, area.y2};
        lv_draw_line(context, &stroke, &a, &b);
    }
    if (view.expanded && view.kind == 0 && (layers & 4u)) {
        stroke.color = lv_color_hex(theme().warning);
        stroke.opa = LV_OPA_COVER;
        stroke.dash_width = 6;
        stroke.dash_gap = 5;
        const auto &heater = ui_model::snapshot().heater;
        for (float threshold : {heater.target - heater.hysteresis, heater.target}) {
            const lv_coord_t y = area.y1 + yAt(view, threshold, height);
            const lv_point_t a{area.x1, y}, b{area.x2, y};
            lv_draw_line(context, &stroke, &a, &b);
        }
        stroke.dash_width = stroke.dash_gap = 0;
    }
    bool previousValid = false, previousOut = false, previousCritical = false;
    uint32_t previousAt = 0, previousWall = 0;
    lv_point_t previous{};
    for (unsigned index = 0; index < sampleCount; ++index) {
        const auto *sample = samples[index];
        const auto *item = reading(sample, view.kind);
        if (!valid(item)) {
            previousValid = false;
            previousOut = false;
            previousCritical = false;
            continue;
        }
        const bool out = item->value < range.minimum || item->value > range.maximum;
        const lv_point_t point{static_cast<lv_coord_t>(area.x1 + xAt(sample->atSeconds, width)),
                               static_cast<lv_coord_t>(area.y1 + yAt(view, item->value, height))};
        stroke.width = view.kind ? 2 : 3;
        stroke.opa = LV_OPA_COVER;
        stroke.round_start = stroke.round_end = 1;
        const bool critical = view.kind == 0 && item->value >= 28.5f;
        stroke.color = lv_color_hex(critical || previousCritical ? palette.bad
                                            : out || previousOut ? palette.warning : theme().accent);
        // Keep chronology monotonic, but never bridge a clock correction as if it were ordinary time.
        if (previousValid && sample->atSeconds - previousAt <= 330u &&
            ui_history_time::continuous(previousWall, sample->wallSeconds, sample->atSeconds - previousAt))
            lv_draw_line(context, &stroke, &previous, &point);
        if (!previousValid || index + 1 == sampleCount ||
            (view.expanded && sample->atSeconds == selection)) {
            lv_draw_rect_dsc_t dot;
            lv_draw_rect_dsc_init(&dot);
            dot.radius = LV_RADIUS_CIRCLE;
            dot.bg_color = stroke.color;
            dot.border_color = lv_color_hex(palette.surface);
            dot.border_width = 1;
            const lv_area_t bounds{
                static_cast<lv_coord_t>(point.x - 3), static_cast<lv_coord_t>(point.y - 3),
                static_cast<lv_coord_t>(point.x + 3), static_cast<lv_coord_t>(point.y + 3)};
            lv_draw_rect(context, &dot, &bounds);
        }
        previous = point;
        previousAt = sample->atSeconds;
        previousWall = sample->wallSeconds;
        previousValid = true;
        previousOut = out;
        previousCritical = critical;
    }
    if (view.expanded && selection != UINT32_MAX) {
        lv_draw_line_dsc_t cursor;
        lv_draw_line_dsc_init(&cursor);
        cursor.color = lv_color_hex(palette.text);
        cursor.dash_width = cursor.dash_gap = 3;
        const lv_coord_t x = area.x1 + xAt(selection, width);
        const lv_point_t top{x, area.y1}, bottom{x, area.y2};
        lv_draw_line(context, &cursor, &top, &bottom);
    }
}
void drawHeater(lv_event_t *event) {
    lv_area_t area;
    lv_obj_get_coords(lv_event_get_target(event), &area);
    lv_draw_ctx_t *context = lv_event_get_draw_ctx(event);
    lv_draw_rect_dsc_t fill;
    lv_draw_rect_dsc_init(&fill);
    fill.bg_color = lv_color_hex(theme().surface);
    lv_draw_rect(context, &fill, &area);
    const int width = lv_area_get_width(&area);
    for (unsigned index = 0; index < ui_model::heaterEventCount(); ++index) {
        const auto *item = ui_model::heaterEventAt(index);
        const auto *next = ui_model::heaterEventAt(index + 1);
        const uint32_t end = next ? std::min(last, next->atSeconds) : last;
        if (end < first || item->atSeconds > last ||
            (item->state != ui_model::State::On && item->state != ui_model::State::Off))
            continue;
        const bool on = item->state == ui_model::State::On;
        fill.bg_color = lv_color_hex(on ? theme().accent : palette.grid);
        fill.bg_opa = on ? LV_OPA_70 : LV_OPA_40;
        const lv_area_t segment{
            static_cast<lv_coord_t>(area.x1 + xAt(std::max(first, item->atSeconds), width)),
            static_cast<lv_coord_t>(area.y1 + (on ? 2 : 15)),
            static_cast<lv_coord_t>(area.x1 + xAt(end, width)), static_cast<lv_coord_t>(area.y2 - 2)};
        lv_draw_rect(context, &fill, &segment);
    }
    if (selection != UINT32_MAX) {
        lv_draw_line_dsc_t cursor;
        lv_draw_line_dsc_init(&cursor);
        cursor.color = lv_color_hex(palette.text);
        const lv_coord_t x = area.x1 + xAt(selection, width);
        const lv_point_t a{x, area.y1}, b{x, area.y2};
        lv_draw_line(context, &cursor, &a, &b);
    }
}
void drawWaterHistory(lv_event_t *event) {
    lv_area_t area;
    lv_obj_get_coords(lv_event_get_target(event), &area);
    lv_draw_ctx_t *context = lv_event_get_draw_ctx(event);
    const int width = lv_area_get_width(&area), height = lv_area_get_height(&area);
    lv_draw_line_dsc_t stroke;
    lv_draw_line_dsc_init(&stroke);
    stroke.color = lv_color_hex(palette.grid);
    for (int row = 0; row < 3; ++row) {
        const lv_coord_t y = area.y1 + 12 + row * (height - 40) / 2;
        const lv_point_t a{area.x1, y}, b{area.x2, y};
        lv_draw_line(context, &stroke, &a, &b);
    }
    for (unsigned index = 0; index < ui_model::waterEventCount(); ++index) {
        const auto *item = ui_model::waterEventAt(index);
        const auto *next = ui_model::waterEventAt(index + 1);
        const uint32_t end = next ? std::min(last, next->atSeconds) : last;
        if (end < first || item->atSeconds > last)
            continue;
        const bool missing = item->state == ui_model::WaterState::Unknown;
        const bool conflict = item->state == ui_model::WaterState::Conflict;
        if (missing || conflict) {
            stroke.color = lv_color_hex(conflict ? palette.bad : palette.muted);
            stroke.dash_width = stroke.dash_gap = 3;
        } else {
            stroke.color =
                lv_color_hex(item->state == ui_model::WaterState::Low ? theme().warning : theme().accent);
            stroke.dash_width = stroke.dash_gap = 0;
        }
        stroke.width = 3;
        const int row = item->state == ui_model::WaterState::Normal ? 0
                        : item->state == ui_model::WaterState::Low  ? 2
                                                                    : 1;
        const lv_coord_t y = area.y1 + 12 + row * (height - 40) / 2;
        const lv_point_t a{static_cast<lv_coord_t>(area.x1 + xAt(item->atSeconds, width)), y};
        const lv_point_t b{static_cast<lv_coord_t>(area.x1 + xAt(end, width)), y};
        lv_draw_line(context, &stroke, &a, &b);
    }
    lv_draw_rect_dsc_t marker;
    lv_draw_rect_dsc_init(&marker);
    marker.bg_color = lv_color_hex(theme().accent);
    for (unsigned index = 0; index < ui_model::atoDoseCount(); ++index) {
        const auto *dose = ui_model::atoDoseAt(index);
        if (dose->endSeconds < first || dose->startSeconds > last)
            continue;
        const lv_coord_t x = area.x1 + xAt(dose->startSeconds, width);
        const lv_area_t bar{
            x, static_cast<lv_coord_t>(area.y2 - 12),
            static_cast<lv_coord_t>(std::max<int>(x + 2, area.x1 + xAt(dose->endSeconds, width))),
            static_cast<lv_coord_t>(area.y2 - 3)};
        lv_draw_rect(context, &marker, &bar);
    }
    if (selection != UINT32_MAX) {
        stroke.dash_width = stroke.dash_gap = 3;
        stroke.width = 1;
        stroke.color = lv_color_hex(palette.text);
        const lv_coord_t x = area.x1 + xAt(selection, width);
        const lv_point_t a{x, area.y1}, b{x, area.y2};
        lv_draw_line(context, &stroke, &a, &b);
    }
}
void selectPoint(lv_event_t *event) {
    lv_indev_t *input = lv_indev_get_act();
    if (!input)
        return;
    if (!fullscreen && compactMoved)
        return;
    if (lv_event_get_code(event) == LV_EVENT_PRESSING && millis() - lastPointerUpdate < 30)
        return;
    lastPointerUpdate = millis();
    lv_point_t point;
    lv_indev_get_point(input, &point);
    lv_area_t area;
    lv_obj_get_coords(lv_event_get_target(event), &area);
    int distance = INT16_MAX;
    for (unsigned index = 0; index < sampleCount; ++index) {
        const int candidate =
            std::abs(point.x - area.x1 - xAt(samples[index]->atSeconds, lv_area_get_width(&area)));
        if (candidate < distance) {
            distance = candidate;
            selection = samples[index]->atSeconds;
        }
    }
    if (sensor == 4) {
        distance = INT16_MAX;
        for (unsigned index = 0; index < ui_model::atoDoseCount(); ++index) {
            const auto *dose = ui_model::atoDoseAt(index);
            if (dose->endSeconds < first || dose->startSeconds > last)
                continue;
            const int candidate =
                std::abs(point.x - area.x1 - xAt(dose->endSeconds, lv_area_get_width(&area)));
            if (candidate < distance) {
                distance = candidate;
                selection = dose->endSeconds;
            }
        }
    }
    if (!fullscreen)
        pendingDetail = true;
    revision = UINT32_MAX;
}
void createPlot(View &view, lv_obj_t *parent, int x, int y, int w, int h, bool expanded) {
    view.plot = box(parent, x, y, w, h, palette.surface, 0);
    lv_obj_add_flag(view.plot, LV_OBJ_FLAG_CLICKABLE);
    if (expanded)
        lv_obj_clear_flag(view.plot, LV_OBJ_FLAG_GESTURE_BUBBLE);
    else {
        lv_obj_add_flag(view.plot, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_event_cb(
            view.plot,
            [](lv_event_t *event) {
                lv_indev_t *input = lv_indev_get_act();
                if (!input)
                    return;
                lv_point_t point;
                lv_indev_get_point(input, &point);
                if (lv_event_get_code(event) == LV_EVENT_PRESSED) {
                    compactPress = point;
                    compactMoved = false;
                } else if (lv_event_get_code(event) == LV_EVENT_PRESSING)
                    compactMoved |=
                        std::abs(point.x - compactPress.x) > 16 || std::abs(point.y - compactPress.y) > 16;
            },
            LV_EVENT_ALL, nullptr);
    }
    view.expanded = expanded;
    lv_obj_add_event_cb(view.plot, sensor == 4 ? drawWaterHistory : drawPlot, LV_EVENT_DRAW_MAIN, &view);
    lv_obj_add_event_cb(view.plot, selectPoint, LV_EVENT_CLICKED, nullptr);
    if (expanded)
        lv_obj_add_event_cb(view.plot, selectPoint, LV_EVENT_PRESSING, nullptr);
    if (h >= 100) {
        view.empty = label(parent, "", x + 16, y + h / 2 - 12, w - 32, palette.muted, &ui_font_16);
        lv_obj_set_style_text_align(view.empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_add_flag(view.empty, LV_OBJ_FLAG_HIDDEN);
    }
    for (int index = 0; index < 3; ++index) {
        view.axes[index] = label(parent, "", x - 58, y - 8 + index * (h - 1) / 2, 49, palette.muted);
        lv_obj_set_style_text_align(view.axes[index], LV_TEXT_ALIGN_RIGHT, 0);
        if (view.kind && index == 1 && &view != &detail)
            lv_obj_add_flag(view.axes[index], LV_OBJ_FLAG_HIDDEN);
        if (!view.kind || &view == &detail) {
            view.times[index] = label(parent, "", x + index * (w - 150) / 2, y + h + 8, 150, palette.muted);
            if (index == 2)
                lv_obj_set_style_text_align(view.times[index], LV_TEXT_ALIGN_RIGHT, 0);
        }
    }
}
void createDetail() {
    detail = View{};
    detail.kind = sensor == 4 || focus == UINT8_MAX ? 0 : focus;
    comparison[0] = comparison[1] = View{};
    fullscreen = box(lv_layer_top(), 0, 0, 800, 480, palette.background, 0);
    lv_obj_add_flag(fullscreen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(fullscreen, LV_OBJ_FLAG_GESTURE_BUBBLE);
    button(fullscreen, "←", 24, 14, 48, Back);
    label(fullscreen, sensor == 4 ? "Dolewki / ATO" : "Parametry wody", 88, 20, 490, palette.text,
          &ui_font_24);
    button(fullscreen, "Szczegóły", 648, 14, 128, Information);
    detail.value = label(fullscreen, "", 24, 69, 365, palette.text, &ui_font_24);
    if (sensor != 4) {
        lv_obj_add_flag(detail.value, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(detail.value, 8);
        lv_obj_clear_flag(detail.value, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_event_cb(
            detail.value, onButton, LV_EVENT_CLICKED,
            reinterpret_cast<void *>(static_cast<uintptr_t>(FocusTemperature + detail.kind)));
    }
    detail.caption = label(fullscreen, "", 400, 73, 250, palette.muted);
    detail.live = button(fullscreen, "Na żywo", 672, 64, 104, Latest);
    char caption[32];
    std::snprintf(caption, sizeof(caption), "Zakres: %s", rangeNames[rangeIndex]);
    rangeButton = button(fullscreen, caption, 24, 111, 200, ChooseRange, false, 40);
    if (sensor == 4) {
        detail.minimum = label(fullscreen, "", 24, 158, 290, palette.muted);
        detail.maximum = label(fullscreen, "", 400, 158, 376, palette.muted);
        detail.coverage = nullptr;
        createPlot(detail, fullscreen, 80, 196, 696, 218, true);
        label(fullscreen, "Objętość przybliżona / 1 l/min", 80, 461, 696, palette.muted);
        return;
    }
    button(fullscreen, "pH", 236, 111, 120, Ph, (layers & 1u) != 0, 40);
    button(fullscreen, "EC", 368, 111, 120, Ec, (layers & 2u) != 0, 40);
    button(fullscreen, "Grzałka", 500, 111, 164, Heater, (layers & 4u) != 0, 40);
    if ((layers & 4u) && (focus == UINT8_MAX || focus == 0)) {
        char a[24], b[24], text[96];
        const auto &heater = ui_model::snapshot().heater;
        number(a, sizeof(a), heater.target - heater.hysteresis);
        number(b, sizeof(b), heater.target);
        std::snprintf(text, sizeof(text), "Progi bieżące / ON <%s°C / OFF >=%s°C", a, b);
        label(fullscreen, text, 132, 156, 644, theme().warning);
    }
    const unsigned extra = focus == UINT8_MAX ? ((layers & 1u) ? 1u : 0u) + ((layers & 2u) ? 1u : 0u) : 0;
    const bool showHeater = (layers & 4u) && (focus == UINT8_MAX || focus == 0);
    const int primaryHeight = 238 - extra * 66 - (showHeater ? 32 : 0);
    createPlot(detail, fullscreen, 132, 180, 644, primaryHeight, true);
    int y = 180 + primaryHeight;
    for (unsigned index = 0; index < 2; ++index) {
        if (focus != UINT8_MAX || !(layers & (1u << index)))
            continue;
        View &view = comparison[index];
        view.kind = index + 1;
        y += 14;
        view.value = label(fullscreen, "", 24, y + (index ? 0 : 10), 48, palette.text);
        lv_obj_add_flag(view.value, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(view.value, 8);
        lv_obj_clear_flag(view.value, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_event_cb(view.value, onButton, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(static_cast<uintptr_t>(FocusPh + index)));
        lv_obj_set_style_text_line_space(view.value, 1, 0);
        createPlot(view, fullscreen, 132, y, 644, 52, true);
        y += 52;
    }
    if (showHeater) {
        y += 14;
        label(fullscreen, "ON\nOFF", 32, y - 6, 44, palette.muted);
        detail.heater = box(fullscreen, 132, y, 644, 18, theme().surface, 0);
        lv_obj_add_event_cb(detail.heater, drawHeater, LV_EVENT_DRAW_MAIN, nullptr);
    }
    for (lv_obj_t *time : detail.times)
        lv_obj_set_y(time, 430);
}
void closePopup() {
    if (popup)
        lv_obj_del(popup);
    popup = nullptr;
}
void createPopup(Popup kind) {
    closePopup();
    popup = box(fullscreen, 0, 0, 800, 480, theme().veil, 0);
    lv_obj_set_style_bg_opa(popup, LV_OPA_80, 0);
    lv_obj_add_flag(popup, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(popup, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_t *panel = box(popup, 80, 64, 640, 352, palette.background, 12);
    button(panel, "←", 20, 16, 48, ClosePopup);
    label(panel, kind == Popup::Range ? "Zakres czasu" : "Szczegóły pomiarów", 84, 22, 520, palette.text,
          &ui_font_24);
    if (kind == Popup::Range) {
        for (unsigned index = 0; index < 5; ++index)
            button(panel, rangeNames[index], 24 + (index % 2) * 300, 80 + (index / 2) * 72, 288,
                   Range + index, rangeIndex == index, 56);
        return;
    }
    char text[192];
    if (sensor == 4) {
        unsigned count = 0;
        float litres = 0;
        for (unsigned index = 0; index < ui_model::atoDoseCount(); ++index) {
            const auto *dose = ui_model::atoDoseAt(index);
            if (dose->endSeconds >= first && dose->startSeconds <= last) {
                ++count;
                litres += ui_model::estimatedAtoLitres(*dose);
            }
        }
        std::snprintf(text, sizeof(text), "%u dolewek / razem ~%.2f l", count, static_cast<double>(litres));
        for (char *p = text; *p; ++p)
            if (*p == '.')
                *p = ',';
        label(panel, text, 24, 82, 592, palette.text, &ui_font_20);
        label(panel,
              "Szacunek przy 1 l/min, nie pomiar objętości.\nOdstęp liczony od końca poprzedniej "
              "dolewki.\nBrak UART: bez raportów nie ma historii dolewania.",
              24, 138, 592, palette.muted, &ui_font_16);
        return;
    }
    const uint32_t coverage = sampleCount > 1 ? last - samples[0]->atSeconds : 0;
    std::snprintf(text, sizeof(text), "%u próbek / zebrano %lu:%02lu h", sampleCount,
                  static_cast<unsigned long>(coverage / 3600),
                  static_cast<unsigned long>((coverage / 60) % 60));
    label(panel, text, 24, 80, 592, palette.text, &ui_font_20);
    unsigned missingTime = 0, corrections = 0;
    for (unsigned index = 0; index < sampleCount; ++index) {
        missingTime += samples[index]->wallSeconds == 0;
        if (index &&
            !ui_history_time::continuous(samples[index - 1]->wallSeconds, samples[index]->wallSeconds,
                                         samples[index]->atSeconds - samples[index - 1]->atSeconds))
            ++corrections;
    }
    for (unsigned kindIndex = 0, row = 0; kindIndex < 3; ++kindIndex) {
        if (kindIndex && !(layers & (1u << (kindIndex - 1))))
            continue;
        float minimum = 0, maximum = 0;
        unsigned count = 0;
        for (unsigned index = 0; index < sampleCount; ++index) {
            const auto *item = reading(samples[index], kindIndex);
            if (!valid(item))
                continue;
            minimum = count ? std::min(minimum, item->value) : item->value;
            maximum = count ? std::max(maximum, item->value) : item->value;
            ++count;
        }
        char a[24], b[24];
        if (count) {
            number(a, sizeof(a), minimum, kindIndex);
            number(b, sizeof(b), maximum, kindIndex);
        } else {
            std::snprintf(a, sizeof(a), "–");
            std::snprintf(b, sizeof(b), "–");
        }
        std::snprintf(text, sizeof(text), "%s / min: %s / max: %s / %u poprawnych",
                      kindIndex == 0   ? "Temperatura [°C]"
                      : kindIndex == 1 ? "pH"
                                       : "EC [µS/cm]",
                      a, b, count);
        label(panel, text, 24, 126 + row++ * 34, 592, palette.muted);
    }
    std::snprintf(text, sizeof(text), "Bez zapisanej godziny: %u / korekty zegara: %u", missingTime,
                  corrections);
    label(panel, text, 24, 244, 592, palette.muted);
    label(panel,
          "Godziny są zapisywane z pomiarem, nie odtwarzane z obecnego zegara.\nPrzerwa oznacza brak "
          "danych lub zmianę zegara. Historia pozostaje w RAM.",
          24, 280, 592, palette.muted);
}
void createLogs(lv_obj_t *parent) {
    list = box(parent, 0, 54, 752, 272, palette.surface);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_style_pad_all(list, 12, 0);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(list, lv_color_hex(palette.grid), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(list, LV_OPA_50, LV_PART_SCROLLBAR);
    const ui_model::LogEntry *ordered[ui_model::kLogCapacity]{};
    for (unsigned index = 0; index < ui_model::logCount(); ++index)
        ordered[index] = ui_model::logAt(index);
    std::sort(ordered, ordered + ui_model::logCount(),
              [](const ui_model::LogEntry *a, const ui_model::LogEntry *b) {
                  return static_cast<int32_t>(a->sequence - b->sequence) > 0;
              });
    int y = 0;
    for (unsigned group = 0; group < 3; ++group) {
        for (unsigned index = 0; index < ui_model::logCount(); ++index) {
            const auto *entry = ordered[index];
            const bool important = entry->level != ui_model::LogLevel::Info;
            const bool resolved = entry->cause && !entry->active;
            if ((important ? resolved ? 1u : 0u : 2u) != group)
                continue;
            const bool critical = entry->level == ui_model::LogLevel::Alarm;
            const uint32_t severity = critical ? palette.bad : palette.warning;
            lv_obj_t *card = box(list, 0, y, 716, 72,
                                 important && !resolved
                                     ? (critical ? palette.badCard : palette.warningCard)
                                     : palette.background);
            if (important) {
                box(card, 0, 10, critical ? 4 : 3, 48, resolved ? palette.grid : severity, 2);
                label(card, critical ? "!" : "•", 10, 19, 16,
                      resolved ? palette.muted : severity, &ui_font_16);
            }
            char text[112], firstTime[32], lastTime[32];
            ui_history_time::format(entry->lastWallSeconds, lastTime, sizeof(lastTime), true);
            lv_obj_t *time = label(card, lastTime, important ? 28 : 12, 12,
                                   important ? 116 : 132, palette.muted);
            lv_obj_t *message = label(card, entry->message, 156, 10, 544,
                                      important && !resolved ? severity : palette.text, &ui_font_16);
            lv_obj_update_layout(message);
            const int metaY = 12 + lv_obj_get_height(message);
            if (resolved) {
                ui_history_time::format(entry->endedWallSeconds, firstTime, sizeof(firstTime), true);
                std::snprintf(text, sizeof(text), "Ustąpiło: %s", firstTime);
            } else
                std::snprintf(text, sizeof(text), "%s", entry->active ? "Przyczyna nadal aktywna" : "");
            if (entry->repeats > 1) {
                ui_history_time::format(entry->wallSeconds, firstTime, sizeof(firstTime), true);
                const size_t used = std::strlen(text);
                std::snprintf(text + used, sizeof(text) - used, "%s%u wystąpień / pierwsze: %s",
                              used ? " / " : "", entry->repeats, firstTime);
            }
            lv_obj_t *meta = label(card, text, 156, metaY, 544, palette.muted);
            lv_obj_update_layout(meta);
            const int height = std::max(64, metaY + static_cast<int>(lv_obj_get_height(meta)) + 12);
            lv_obj_set_height(card, height);
            y += height + 8;
        }
    }
    if (!ui_model::logCount())
        label(list, "Brak zdarzeń", 12, 60, 700, palette.muted, &ui_font_20);
    lv_obj_update_layout(list);
    lv_obj_scroll_to_y(list, scroll, LV_ANIM_OFF);
}
void updateTimes(View &view) {
    if (!view.times[0])
        return;
    for (unsigned tick = 0; tick < 3; ++tick) {
        const uint32_t target = first + static_cast<uint64_t>(last - first) * tick / 2u;
        const ui_model::HistorySample *nearest = nullptr;
        uint32_t distance = UINT32_MAX;
        for (unsigned index = 0; index < sampleCount; ++index) {
            const auto *sample = samples[index];
            const uint32_t candidate =
                sample->atSeconds > target ? sample->atSeconds - target : target - sample->atSeconds;
            if (candidate < distance) {
                nearest = sample;
                distance = candidate;
            }
        }
        if (!nearest || !nearest->wallSeconds) {
            lv_label_set_text_static(view.times[tick], tick == 2 ? "Czas nieznany" : "");
            continue;
        }
        if (sampleCount == 1 && tick != 2) {
            lv_label_set_text_static(view.times[tick], "");
            continue;
        }
        char text[32];
        ui_history_time::format(nearest->wallSeconds, text, sizeof(text), rangeIndex >= 2);
        lv_label_set_text(view.times[tick], text);
    }
}
void updateView(View &view, bool expanded) {
    if (!view.plot)
        return;
    const auto &range = ui_model::snapshot().ranges[view.kind];
    view.low = range.minimum;
    view.high = range.maximum;
    if (expanded && view.kind == 0 && (layers & 4u)) {
        view.low =
            std::min(view.low, ui_model::snapshot().heater.target - ui_model::snapshot().heater.hysteresis);
        view.high = std::max(view.high, ui_model::snapshot().heater.target);
    }
    const ui_model::HistorySample *selected = nullptr;
    unsigned validCount = 0;
    for (unsigned index = 0; index < sampleCount; ++index) {
        const auto *sample = samples[index];
        if (expanded && sample->atSeconds == selection)
            selected = sample;
        const auto *item = reading(sample, view.kind);
        if (!valid(item))
            continue;
        ++validCount;
        view.low = std::min(view.low, item->value);
        view.high = std::max(view.high, item->value);
    }
    if (view.empty) {
        if (validCount > 1)
            lv_obj_add_flag(view.empty, LV_OBJ_FLAG_HIDDEN);
        else {
            lv_label_set_text_static(view.empty,
                                     validCount ? "Za mało danych do trendu"
                                     : sampleCount ? "Brak wiarygodnych pomiarów"
                                                   : "Brak pomiarów w tym zakresie");
            lv_obj_clear_flag(view.empty, LV_OBJ_FLAG_HIDDEN);
        }
    }
    const float margin = std::max((view.high - view.low) * 0.15f, view.kind == 2 ? 10.0f : 0.1f);
    view.low -= margin;
    view.high += margin;
    if (expanded && &view == &detail && !selected)
        selection = UINT32_MAX;
    const auto *sample = selected ? selected : sampleCount ? samples[sampleCount - 1] : nullptr;
    const auto *item = reading(sample, view.kind);
    char text[128], amount[32];
    if (valid(item)) {
        number(amount, sizeof(amount), item->value, view.kind);
        std::snprintf(text, sizeof(text),
                      !expanded          ? "%s °C"
                      : &view == &detail ? (view.kind == 0   ? "Temperatura / %s °C"
                                            : view.kind == 1 ? "pH wody / %s"
                                                             : "EC / %s µS/cm")
                      : view.kind == 1   ? "pH\n%s"
                                         : "EC\nµS/cm\n%s",
                      amount);
        const bool out = item->value < range.minimum || item->value > range.maximum;
        const bool critical = view.kind == 0 && item->value >= 28.5f;
        setLegibleText(view.value, critical ? palette.bad
                                   : out      ? palette.warning : palette.text);
    } else {
        std::snprintf(text, sizeof(text),
                      view.kind == 0   ? "Brak odczytu"
                      : view.kind == 1 ? "pH\n–"
                                       : "EC\n–");
        setLegibleText(view.value, palette.muted);
    }
    lv_label_set_text(view.value, text);
    if (view.caption && expanded) {
        ui_history_time::format(sample ? sample->wallSeconds : 0, amount, sizeof(amount), true);
        std::snprintf(text, sizeof(text), "%s",
                      sample && sample->wallSeconds ? amount : "Godzina nieznana");
        lv_label_set_text(view.caption, text);
    }
    if (expanded && view.live) {
        if (selected)
            lv_obj_clear_state(view.live, LV_STATE_DISABLED);
        else
            lv_obj_add_state(view.live, LV_STATE_DISABLED);
    }
    for (int index = 0; index < 3; ++index) {
        number(amount, sizeof(amount), view.high - index * (view.high - view.low) / 2.0f, view.kind);
        lv_label_set_text(view.axes[index], amount);
    }
    updateTimes(view);
    lv_obj_invalidate(view.plot);
    if (view.heater)
        lv_obj_invalidate(view.heater);
}
void updateAtoView(View &view, bool expanded) {
    if (!view.plot)
        return;
    lv_obj_update_layout(view.plot);
    const ui_model::AtoDose *selected = nullptr, *previous = nullptr;
    const ui_model::AtoDose *newest = nullptr, *newestPrevious = nullptr;
    unsigned count = 0;
    float litres = 0;
    for (unsigned index = 0; index < ui_model::atoDoseCount(); ++index) {
        const auto *dose = ui_model::atoDoseAt(index);
        // A partially overlapping run is counted in full, not presented as a measured partial volume.
        if (dose->endSeconds < first || dose->startSeconds > last)
            continue;
        ++count;
        litres += ui_model::estimatedAtoLitres(*dose);
        newest = dose;
        newestPrevious = index ? ui_model::atoDoseAt(index - 1) : nullptr;
        if (selection == UINT32_MAX || dose->endSeconds == selection) {
            selected = dose;
            previous = index ? ui_model::atoDoseAt(index - 1) : nullptr;
        }
    }
    char text[128], amount[24];
    if (expanded) {
        if (!selected && newest) {
            selection = UINT32_MAX;
            selected = newest;
            previous = newestPrevious;
        }
        if (selected) {
            std::snprintf(text, sizeof(text), "~%.2f l",
                          static_cast<double>(ui_model::estimatedAtoLitres(*selected)));
            for (char *p = text; *p; ++p)
                if (*p == '.')
                    *p = ',';
            lv_label_set_text(view.value, text);
            offset(amount, sizeof(amount), last - selected->endSeconds);
            std::snprintf(text, sizeof(text), "Dolewka: %s", amount);
            lv_label_set_text(view.caption, text);
            std::snprintf(text, sizeof(text), "Czas: %.1f s", selected->durationMs / 1000.0);
            lv_label_set_text(view.minimum, text);
            if (previous) {
                const uint32_t gap = selected->startSeconds - previous->endSeconds;
                std::snprintf(
                    text, sizeof(text), "Odstęp: %lu:%02lu:%02lu", static_cast<unsigned long>(gap / 3600),
                    static_cast<unsigned long>((gap / 60) % 60), static_cast<unsigned long>(gap % 60));
            } else
                std::snprintf(text, sizeof(text), "Odstęp: –");
            lv_label_set_text(view.maximum, text);
        } else {
            selection = UINT32_MAX;
            lv_label_set_text(view.value, "Brak dolewek");
            lv_label_set_text(view.caption, "Brak telemetrii dolewek");
            lv_label_set_text(view.minimum, "Czas: –");
            lv_label_set_text(view.maximum, "Odstęp: –");
        }
        std::snprintf(text, sizeof(text), "%u dolewek / razem ~%.2f l", count, static_cast<double>(litres));
        for (char *p = text; *p; ++p)
            if (*p == '.')
                *p = ',';
        if (view.coverage)
            lv_label_set_text(view.coverage, text);
        if (selection == UINT32_MAX)
            lv_obj_add_state(view.live, LV_STATE_DISABLED);
        else
            lv_obj_clear_state(view.live, LV_STATE_DISABLED);
    } else {
        lv_label_set_text(view.caption, "Poziom wody / pływaki MIN i MAX");
        lv_label_set_text(view.value, waterName(ui_model::waterState()));
        std::snprintf(text, sizeof(text), "%u dolewek", count);
        lv_label_set_text(view.count, text);
    }
    for (int index = 0; index < 3; ++index) {
        lv_label_set_text(view.axes[index], index == 0 ? "MAX" : index == 1 ? "Między" : "MIN");
        lv_obj_set_y(view.axes[index],
                     lv_obj_get_y(view.plot) + 4 + index * (lv_obj_get_height(view.plot) - 40) / 2);
        if (index == 2)
            std::snprintf(text, sizeof(text), "Teraz");
        else
            offset(text, sizeof(text), (last - first) * (2u - index) / 2u);
        lv_label_set_text(view.times[index], text);
    }
    lv_obj_invalidate(view.plot);
}
} // namespace
void create(lv_obj_t *parent, unsigned selectedSensor, const Colors &colors, Handler callback) {
    reset();
    palette = colors;
    handler = callback;
    sensor = selectedSensor > 4 ? 0 : selectedSensor;
    rangeIndex = savedRanges[sensor == 4 ? 1 : 0];
    button(parent, "Parametry wody", 0, 0, 244, 0, sensor < 3);
    button(parent, "Dolewki / ATO", 254, 0, 244, 4, sensor == 4);
    button(parent, "Zdarzenia", 508, 0, 244, 3, sensor == 3);
    if (sensor == 3) {
        createLogs(parent);
        return;
    }
    sensor = sensor == 4 ? 4 : 0;
    layers = savedLayers;
    focus = UINT8_MAX;
    lv_obj_t *card = box(parent, 0, 54, 752, 272, palette.surface);
    compact.caption =
        label(card, sensor == 4 ? "Poziom wody" : "Temperatura", 16, 12, 340, palette.muted, &ui_font_20);
    compact.value = label(card, "", 394, 12, 340, palette.text, &ui_font_24);
    lv_obj_set_style_text_align(compact.value, LV_TEXT_ALIGN_RIGHT, 0);
    if (sensor == 4)
        compact.count = label(card, "", 16, 41, 160, palette.muted);
    createPlot(compact, card, 66, 64, 668, 172, false);

    selection = UINT32_MAX;
    refresh();
}
void refresh() {
    if (!compact.plot)
        return;
    if (pendingPopupClose) {
        pendingPopupClose = false;
        closePopup();
    }
    if (pendingClose) {
        pendingClose = false;
        closePopup();
        if (fullscreen)
            lv_obj_del(fullscreen);
        fullscreen = nullptr;
        detail = comparison[0] = comparison[1] = View{};
        selection = UINT32_MAX;
        revision = UINT32_MAX;
    }
    if (pendingDetail || pendingRange) {
        pendingDetail = pendingRange = false;
        closePopup();
        if (fullscreen)
            lv_obj_del(fullscreen);
        createDetail();
        revision = UINT32_MAX;
    }
    if (revision != ui_model::historyRevision()) {
        revision = ui_model::historyRevision();
        gather();
        if (sensor == 4) {
            updateAtoView(compact, false);
            updateAtoView(detail, true);
        } else {
            updateView(compact, false);
            updateView(detail, true);
            for (View &view : comparison)
                updateView(view, true);
        }
    }
    if (pendingPopup != Popup::None && fullscreen) {
        const Popup requested = pendingPopup;
        pendingPopup = Popup::None;
        createPopup(requested);
    }
}
void reset() {
    closePopup();
    if (fullscreen)
        lv_obj_del(fullscreen);
    fullscreen = list = rangeButton = nullptr;
    compact = detail = comparison[0] = comparison[1] = View{};
    pendingDetail = pendingRange = pendingClose = pendingPopupClose = false;
    pendingPopup = Popup::None;
    revision = UINT32_MAX;
    sampleCount = 0;
}
void resetSession() {
    reset();
    savedRanges[0] = savedRanges[1] = 0;
    savedLayers = 0;
    focus = UINT8_MAX;
    scroll = 0;
}
void captureScroll() {
    if (list)
        scroll = lv_obj_get_scroll_y(list);
}
void latest() { selection = revision = UINT32_MAX; }
#ifdef AQUARIUM_UI_HOST_PREVIEW
bool previewInspection(bool expanded, unsigned expectedLayers, unsigned seconds) {
    if ((fullscreen != nullptr) != expanded || (expanded && layers != expectedLayers) ||
        windows[rangeIndex] != seconds)
        return false;
    if (!expanded || sensor == 4)
        return true;
    return ((comparison[0].plot != nullptr) == (focus == UINT8_MAX && (layers & 1u) != 0)) &&
           ((comparison[1].plot != nullptr) == (focus == UINT8_MAX && (layers & 2u) != 0)) &&
           ((detail.heater != nullptr) == ((focus == UINT8_MAX || focus == 0) && (layers & 4u) != 0));
}
bool previewFocus(unsigned kind) { return fullscreen && sensor != 4 && focus == kind; }
unsigned previewLayers() { return layers; }
uint32_t previewSelection() { return selection; }
#endif
} // namespace ui_history_view
