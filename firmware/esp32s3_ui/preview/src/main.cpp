#include <Arduino.h>
#include <lvgl.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <direct.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "panel_hal.h"
#include "ui_app.h"
#include "ui_history_view.h"
#include "ui_host_heap.h"
#include "ui_io.h"
#include "ui_navigation.h"
#include "ui_plan.h"
#include "ui_runtime.h"
#include "ui_status.h"
#include "ui_theme.h"
void ui_exploration_init(int variant);
bool ui_model_checks();
bool ui_status_checks();
bool ui_runtime_checks();
bool ui_plan_checks();
bool ui_theme_checks();
bool ui_history_time_checks();

namespace {

constexpr int kWidth = 800;
constexpr int kHeight = 480;
uint32_t elapsedMs = 0;
uint32_t touches = 0;
uint32_t frames = 0;
lv_color_t pixels[kWidth * kHeight];
lv_color_t drawPixels[kWidth * 40];
lv_disp_draw_buf_t drawBuffer;
lv_disp_drv_t displayDriver;
lv_indev_drv_t inputDriver;
lv_point_t pointer = {0, 0};
bool pointerDown = false;
std::string outputDirectory = "output";

void flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *source) {
    for (lv_coord_t y = area->y1; y <= area->y2; ++y) {
        for (lv_coord_t x = area->x1; x <= area->x2; ++x) {
            if (x >= 0 && x < kWidth && y >= 0 && y < kHeight)
                pixels[y * kWidth + x] = *source;
            ++source;
        }
    }
    if (lv_disp_flush_is_last(driver))
        ++frames;
    lv_disp_flush_ready(driver);
}

void readPointer(lv_indev_drv_t *, lv_indev_data_t *data) {
    data->point = pointer;
    data->state = pointerDown ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
    data->continue_reading = false;
}

void drain(uint32_t duration = 160) {
    for (uint32_t passed = 0; passed < duration; passed += 5) {
        elapsedMs += 5;
        lv_tick_inc(5);
        lv_timer_handler();
        ui_app_service();
    }
    lv_obj_update_layout(lv_scr_act());
    lv_refr_now(nullptr);
}

bool onScreen(lv_obj_t *object) {
    if (!lv_obj_is_valid(object))
        return false;
    lv_area_t area;
    lv_obj_get_coords(object, &area);
    for (lv_obj_t *ancestor = object; ancestor; ancestor = lv_obj_get_parent(ancestor)) {
        if (lv_obj_has_flag(ancestor, LV_OBJ_FLAG_HIDDEN))
            return false;
        lv_area_t parentArea;
        lv_obj_get_coords(ancestor, &parentArea);
        area.x1 = std::max(area.x1, parentArea.x1);
        area.y1 = std::max(area.y1, parentArea.y1);
        area.x2 = std::min(area.x2, parentArea.x2);
        area.y2 = std::min(area.y2, parentArea.y2);
    }
    return area.x2 >= 0 && area.y2 >= 0 && area.x1 < kWidth && area.y1 < kHeight && area.x2 >= area.x1 &&
           area.y2 >= area.y1;
}

lv_obj_t *findLabel(lv_obj_t *object, const std::string &caption, bool partial, bool visibleOnly) {
    if (lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN))
        return nullptr;
    // Reverse traversal gives dialogs and newer overlays precedence over covered content.
    for (int32_t i = static_cast<int32_t>(lv_obj_get_child_cnt(object)) - 1; i >= 0; --i) {
        lv_obj_t *found = findLabel(lv_obj_get_child(object, i), caption, partial, visibleOnly);
        if (found)
            return found;
    }
    if (!lv_obj_check_type(object, &lv_label_class) || (visibleOnly && !onScreen(object)))
        return nullptr;
    const char *value = lv_label_get_text(object);
    const bool matches = partial ? std::strstr(value, caption.c_str()) != nullptr : caption == value;
    return matches ? object : nullptr;
}

lv_obj_t *locate(const std::string &caption, bool partial, bool visibleOnly = true) {
    lv_obj_t *object = findLabel(lv_layer_top(), caption, partial, visibleOnly);
    return object ? object : findLabel(lv_scr_act(), caption, partial, visibleOnly);
}

void pointerClick(int x, int y, uint32_t releaseDuration = 180) {
    pointer.x = static_cast<lv_coord_t>(x);
    pointer.y = static_cast<lv_coord_t>(y);
    pointerDown = true;
    ++touches;
    drain(100);
    pointerDown = false;
    drain(releaseDuration);
}

bool pointerSwipe(const std::string &argument) {
    std::istringstream positions(argument);
    int fromX, fromY, toX, toY, duration = 320;
    if (!(positions >> fromX >> fromY >> toX >> toY))
        return false;
    if (positions >> duration) {
        std::string extra;
        if (positions >> extra)
            return false;
    } else if (!positions.eof())
        return false;
    if (fromX < 0 || fromX >= kWidth || toX < 0 || toX >= kWidth || fromY < 0 || fromY >= kHeight ||
        toY < 0 || toY >= kHeight || duration < 160 || duration > 1500)
        return false;
    std::printf("SWIPE (%d,%d) -> (%d,%d) %dms\n", fromX, fromY, toX, toY, duration);
    pointer = {static_cast<lv_coord_t>(fromX), static_cast<lv_coord_t>(fromY)};
    pointerDown = true;
    ++touches;
    drain(40);
    const int steps = (duration + 19) / 20;
    for (int step = 1; step <= steps; ++step) {
        pointer.x = static_cast<lv_coord_t>(fromX + (toX - fromX) * step / steps);
        pointer.y = static_cast<lv_coord_t>(fromY + (toY - fromY) * step / steps);
        drain(20);
    }
    // Holding the finger past the threshold must not trigger a second page or click.
    drain(220);
    pointerDown = false;
    drain(240);
    return true;
}

bool clickLabel(const std::string &caption, bool partial) {
    lv_obj_t *object = locate(caption, partial);
    if (!object) {
        object = locate(caption, partial, false);
        if (object) {
            lv_obj_scroll_to_view_recursive(object, LV_ANIM_OFF);
            drain();
        }
    }
    if (!object) {
        std::fprintf(stderr, "Label not found: %s\n", caption.c_str());
        return false;
    }
    lv_obj_t *clickable = object;
    while (clickable && !lv_obj_has_flag(clickable, LV_OBJ_FLAG_CLICKABLE))
        clickable = lv_obj_get_parent(clickable);
    if (!clickable || lv_obj_has_state(clickable, LV_STATE_DISABLED)) {
        std::fprintf(stderr, "Label is not actionable: %s\n", caption.c_str());
        return false;
    }
    lv_area_t area;
    lv_obj_get_coords(clickable, &area);
    for (lv_obj_t *ancestor = lv_obj_get_parent(clickable); ancestor;
         ancestor = lv_obj_get_parent(ancestor)) {
        lv_area_t clip;
        lv_obj_get_coords(ancestor, &clip);
        area.x1 = std::max(area.x1, clip.x1);
        area.y1 = std::max(area.y1, clip.y1);
        area.x2 = std::min(area.x2, clip.x2);
        area.y2 = std::min(area.y2, clip.y2);
    }
    const int x = std::max(0, std::min(kWidth - 1, (area.x1 + area.x2) / 2));
    const int y = std::max(0, std::min(kHeight - 1, (area.y1 + area.y2) / 2));
    std::printf("TOUCH %s (%d,%d)\n", caption.c_str(), x, y);
    pointerClick(x, y);
    return true;
}

void writeU16(FILE *file, uint16_t value) {
    std::fputc(value & 255, file);
    std::fputc(value >> 8, file);
}

void writeU32(FILE *file, uint32_t value) {
    writeU16(file, static_cast<uint16_t>(value));
    writeU16(file, static_cast<uint16_t>(value >> 16));
}

bool snapshot(const std::string &name) {
    if (_mkdir(outputDirectory.c_str()) != 0 && errno != EEXIST) {
        std::fprintf(stderr, "Cannot create output directory: %s\n", outputDirectory.c_str());
        return false;
    }
    const std::string path = outputDirectory + "/" + name + ".bmp";
    FILE *file = std::fopen(path.c_str(), "wb");
    if (!file) {
        std::fprintf(stderr, "Cannot open snapshot: %s\n", path.c_str());
        return false;
    }
    constexpr uint32_t rowBytes = (kWidth * 3 + 3) & ~3U;
    constexpr uint32_t dataBytes = rowBytes * kHeight;
    std::fputs("BM", file);
    writeU32(file, 54 + dataBytes);
    writeU32(file, 0);
    writeU32(file, 54);
    writeU32(file, 40);
    writeU32(file, kWidth);
    writeU32(file, kHeight);
    writeU16(file, 1);
    writeU16(file, 24);
    writeU32(file, 0);
    writeU32(file, dataBytes);
    writeU32(file, 2835);
    writeU32(file, 2835);
    writeU32(file, 0);
    writeU32(file, 0);
    for (int y = kHeight - 1; y >= 0; --y) {
        for (int x = 0; x < kWidth; ++x) {
            lv_color32_t color;
            color.full = lv_color_to32(pixels[y * kWidth + x]);
            std::fputc(color.ch.blue, file);
            std::fputc(color.ch.green, file);
            std::fputc(color.ch.red, file);
        }
        for (uint32_t pad = kWidth * 3; pad < rowBytes; ++pad)
            std::fputc(0, file);
    }
    const bool okay = !std::ferror(file);
    if (std::fclose(file) != 0 || !okay)
        return false;
    std::printf("SNAPSHOT %s\n", path.c_str());
    return true;
}

size_t objectCount(lv_obj_t *object) {
    size_t count = 1;
    for (uint32_t index = 0; index < lv_obj_get_child_cnt(object); ++index)
        count += objectCount(lv_obj_get_child(object, index));
    return count;
}

void dumpLabels(lv_obj_t *object) {
    if (!onScreen(object))
        return;
    if (lv_obj_check_type(object, &lv_label_class)) {
        lv_area_t area;
        lv_obj_get_coords(object, &area);
        std::printf("LABEL [%d,%d,%d,%d] %s\n", area.x1, area.y1, area.x2, area.y2,
                    lv_label_get_text(object));
    }
    for (uint32_t index = 0; index < lv_obj_get_child_cnt(object); ++index)
        dumpLabels(lv_obj_get_child(object, index));
}

bool command(const std::string &operation, const std::string &argument) {
    if (operation == "advance") {
        char *end = nullptr;
        const unsigned long duration = std::strtoul(argument.c_str(), &end, 10);
        if (!end || *end || !duration || duration > 43200000u)
            return false;
        elapsedMs += duration;
        ++touches;
        drain(400);
        return true;
    }
    if (operation == "scenario") {
        const int value = std::atoi(argument.c_str());
        if (value < 0 || value > 6)
            return false;
        ui_model::setScenario(static_cast<ui_model::Scenario>(value));
        drain(4000);
        return true;
    }
    if (operation == "controller_source") {
        ui_model::setFeederSource(ui_model::FeederSource::Controller);
        return true;
    }
    if (operation == "position") {
        std::istringstream values(argument);
        float degrees;
        int complete;
        std::string extra;
        if (!(values >> degrees >> complete) || values >> extra || complete < 0 || complete > 1)
            return false;
        const bool accepted =
            ui_model::applyFeederPosition(ui_model::snapshot().motor.job, degrees, complete != 0);
        drain(200);
        return accepted;
    }
    if (operation == "assert_feed") {
        std::istringstream values(argument);
        unsigned job, phase;
        std::string extra;
        if (!(values >> job >> phase) || values >> extra)
            return false;
        const auto &motor = ui_model::snapshot().motor;
        return motor.job == job && static_cast<unsigned>(motor.phase) == phase;
    }
    if (operation == "event") {
        const size_t separator = argument.find('|');
        if (separator == std::string::npos || argument[0] < '0' || argument[0] > '2' || separator != 1)
            return false;
        ui_model::recordPanelEvent(argument.substr(2).c_str(),
                                   static_cast<ui_model::LogLevel>(argument[0] - '0'));
        drain(400);
        return true;
    }
    if (operation == "assert_y_before") {
        const size_t separator = argument.find('|');
        if (separator == std::string::npos)
            return false;
        lv_obj_t *a = locate(argument.substr(0, separator), false, false);
        lv_obj_t *b = locate(argument.substr(separator + 1), false, false);
        if (!a || !b)
            return false;
        lv_area_t first, second;
        lv_obj_get_coords(a, &first);
        lv_obj_get_coords(b, &second);
        return first.y2 < second.y1;
    }
    if (operation == "assert_centered") {
        lv_obj_t *screen = lv_scr_act();
        lv_obj_t *content = lv_obj_get_child(screen, 1);
        lv_obj_t *status = lv_obj_get_child(screen, 2);
        const int top = lv_obj_has_flag(status, LV_OBJ_FLAG_HIDDEN) ? 56 : 82;
        const int before = lv_obj_get_y(content) - top;
        const int after = 420 - lv_obj_get_y(content) - lv_obj_get_height(content);
        std::printf("CENTER gaps=%d/%d\n", before, after);
        return before >= 0 && after >= 0 && std::abs(before - after) <= 1;
    }
    if (operation == "assert_chrome") {
        const bool dimmed = argument == "dim";
        if (!dimmed && argument != "normal")
            return false;
        lv_obj_t *screen = lv_scr_act();
        for (unsigned index : {0u, 3u}) {
            lv_obj_t *chrome = lv_obj_get_child(screen, index);
            lv_obj_t *last = lv_obj_get_child(chrome, lv_obj_get_child_cnt(chrome) - 1);
            const bool hasScrim =
                lv_obj_get_width(last) == 800 && lv_obj_get_style_bg_opa(last, 0) == LV_OPA_40;
            if (hasScrim != dimmed)
                return false;
        }
        return lv_obj_get_style_opa(lv_obj_get_child(screen, 1), 0) == LV_OPA_COVER;
    }
    if (operation == "assert_plan_marker") {
        lv_obj_t *body = lv_obj_get_child(lv_scr_act(), 1);
        const auto status = ui_status::read(ui_model::demoMinute());
        const int minute = status.clock.hour * 60 + status.clock.minute;
        const int x = lv_obj_get_x(body) + 180 + ui_plan::minutePosition(minute, 550);
        const int y = lv_obj_get_y(body) + 52;
        const lv_color_t day = lv_color_hex(ui_theme::kTimeMarkerLight),
                         night = lv_color_hex(ui_theme::kTimeMarkerDark);
        unsigned matches = 0;
        for (int row = y + 2; row < y + 264; ++row) {
            const auto pixel = pixels[row * kWidth + x];
            matches += pixel.full == day.full || pixel.full == night.full;
        }
        std::printf("PLAN_LINE minute=%d x=%d pixels=%u\n", minute, x, matches);
        return matches > 190;
    }
    if (operation == "assert_plan_sparse_rails") {
        lv_obj_t *body = lv_obj_get_child(lv_scr_act(), 1);
        const int originX = lv_obj_get_x(body) + 180;
        const int lampY = lv_obj_get_y(body) + 64 + 14 + 4;
        const auto pixelAt = [](int x, int y) { return pixels[y * kWidth + x].full; };
        const int earlyX = originX + ui_plan::minutePosition(60, 550);
        const int rulerX = originX + ui_plan::minutePosition(120, 550);
        const int activeLampX = originX + ui_plan::minutePosition(660, 550);
        const int noonOffX = originX + ui_plan::minutePosition(780, 550);
        const int activeAirX = originX + ui_plan::minutePosition(1380, 550);
        const int airY = lampY + 3 * 44 + 4;
        const int feedingY = lampY + 5 * 44 + 4;
        // Compare against the empty part of each row so user-selected backgrounds remain valid.
        return pixelAt(earlyX, lampY) == pixelAt(earlyX, lampY - 9) &&
               pixelAt(rulerX, lampY) != pixelAt(rulerX, lampY - 9) &&
               pixelAt(activeLampX, lampY) != pixelAt(activeLampX, lampY - 9) &&
               pixelAt(noonOffX, airY) == pixelAt(noonOffX, airY - 9) &&
               pixelAt(activeAirX, airY) != pixelAt(activeAirX, airY - 9) &&
               pixelAt(earlyX, feedingY) == pixelAt(earlyX, feedingY - 9);
    }
    if (operation == "assert_plan_feeding_rail") {
        lv_obj_t *body = lv_obj_get_child(lv_scr_act(), 1);
        const auto &feeding = ui_model::snapshot().feeding;
        if (feeding.count < 1 || feeding.count > 2 ||
            !(feeding.dayMask & (1u << ui_model::weekday())))
            return false;
        const int y = lv_obj_get_y(body) + 64 + 5 * 44 + 14 + 8;
        const lv_color_t day = lv_color_hex(ui_theme::light.warning);
        const lv_color_t night = lv_color_hex(ui_theme::dark.warning);
        for (unsigned index = 0; index < feeding.count; ++index) {
            if (feeding.times[index] >= 1440)
                return false;
            const int x = lv_obj_get_x(body) + 180 + ui_plan::minutePosition(feeding.times[index], 550);
            const lv_color_t pixel = pixels[y * kWidth + x];
            if (pixel.full != day.full && pixel.full != night.full)
                return false;
        }
        return true;
    }
    if (operation == "assert_motion" || operation == "assert_static") {
        std::istringstream values(argument);
        int x1, y1, x2, y2, duration, minimumFrames, maximumFrames;
        std::string extra;
        if (!(values >> x1 >> y1 >> x2 >> y2 >> duration >> minimumFrames >> maximumFrames) ||
            values >> extra || x1 < 0 || y1 < 0 || x2 >= kWidth || y2 >= kHeight || x1 > x2 || y1 > y2 ||
            duration < 30 || duration > 60000 || minimumFrames < 0 || minimumFrames > maximumFrames)
            return false;
        std::vector<lv_color_t> before;
        for (int y = y1; y <= y2; ++y)
            for (int x = x1; x <= x2; ++x)
                before.push_back(pixels[y * kWidth + x]);
        const uint32_t startFrame = frames;
        drain(duration);
        unsigned changed = 0, index = 0;
        for (int y = y1; y <= y2; ++y)
            for (int x = x1; x <= x2; ++x, ++index)
                changed += before[index].full != pixels[y * kWidth + x].full;
        const int rendered = frames - startFrame;
        std::printf("MOTION pixels_changed=%u frames=%d duration=%d\n", changed, rendered, duration);
        return (operation == "assert_static" ? changed == 0 : changed > 0) && rendered >= minimumFrames &&
               rendered <= maximumFrames;
    }
    if (operation == "assert_saves" || operation == "delay_completion") {
        char *end = nullptr;
        const unsigned long value = std::strtoul(argument.c_str(), &end, 10);
        if (!end || *end || value > 60000)
            return false;
        if (operation == "assert_saves")
            return ui_runtime::saveCount() == value;
        ui_io::delayNextCompletion(value);
        return true;
    }
    if (operation == "assert_stored_range") {
        std::istringstream values(argument);
        float minimum, maximum;
        std::string extra;
        if (!(values >> minimum >> maximum) || values >> extra)
            return false;
        ui_runtime::Configuration saved;
        ui_runtime::load(saved);
        return saved.domain.ranges[0].minimum == minimum && saved.domain.ranges[0].maximum == maximum;
    }
    if (operation == "date") {
        std::istringstream values(argument);
        ui_runtime::DateTime value;
        std::string extra;
        if (!(values >> value.year >> value.month >> value.day >> value.hour >> value.minute) ||
            values >> extra)
            return false;
        if (!ui_runtime::setDateTime(value).ok)
            return false;
        drain(1100);
        return true;
    }
    if (operation == "assert_bounds" || operation == "assert_toast") {
        const size_t separator = argument.find('|');
        if (separator == std::string::npos || !separator)
            return false;
        const std::string caption = argument.substr(0, separator);
        std::istringstream limits(argument.substr(separator + 1));
        int first, second, third, fourth;
        std::string extra;
        if (!(limits >> first >> second >> third >> fourth) || limits >> extra)
            return false;
        lv_obj_t *object = operation == "assert_toast" ? findLabel(lv_layer_top(), caption, true, false)
                                                       : locate(caption, false, false);
        if (!object) {
            std::fprintf(stderr, "Missing layout target: %s\n", caption.c_str());
            return false;
        }
        if (operation == "assert_toast") {
            while (lv_obj_get_parent(object) && lv_obj_get_parent(object) != lv_layer_top())
                object = lv_obj_get_parent(object);
            const int y = lv_obj_get_y(object);
            const int opacity = lv_obj_get_style_opa(object, 0);
            std::printf("TOAST y=%d opacity=%d caption=%s\n", y, opacity, caption.c_str());
            return first <= second && third >= 0 && third <= fourth && fourth <= 255 && y >= first &&
                   y <= second && opacity >= third && opacity <= fourth;
        }
        lv_area_t area;
        lv_obj_get_coords(object, &area);
        const bool inside = first <= third && second <= fourth && area.x1 >= first && area.y1 >= second &&
                            area.x2 <= third && area.y2 <= fourth;
        if (!inside)
            std::fprintf(stderr, "Layout bounds [%d,%d,%d,%d] outside [%d,%d,%d,%d]: %s\n", area.x1,
                         area.y1, area.x2, area.y2, first, second, third, fourth, caption.c_str());
        return inside;
    }
    if (operation == "assert_no_toast") {
        const bool absent = !findLabel(lv_layer_top(), argument, true, false);
        if (!absent)
            std::fprintf(stderr, "Unexpected notification object: %s\n", argument.c_str());
        return absent;
    }
    if (operation == "toast_soak") {
        char *end = nullptr;
        const unsigned long count = std::strtoul(argument.c_str(), &end, 10);
        if (!end || *end || !count || count > 500 || !clickLabel("Ponów", false))
            return false;
        // Invalid SSID exercises production validation and repeated notifications without NVS writes.
        const char *message = "Podaj SSID";
        if (!findLabel(lv_layer_top(), message, true, false))
            return false;
        drain(4000);
        const size_t before = ui_host_heap_used();
        for (unsigned long cycle = 0; cycle < count; ++cycle) {
            if (!clickLabel("Ponów", false) || !findLabel(lv_layer_top(), message, true, false))
                return false;
            if (cycle % 3 == 0)
                drain(3400);
            else if (cycle % 3 == 1) {
                drain(300);
                pointerClick(100, 90);
                drain(300);
                if (findLabel(lv_layer_top(), message, true, false))
                    return false;
            }
        }
        drain(4000);
        const size_t after = ui_host_heap_used();
        std::printf("TOAST_SOAK cycles=%lu before=%lu after=%lu\n", count,
                    static_cast<unsigned long>(before), static_cast<unsigned long>(after));
        return !findLabel(lv_layer_top(), message, true, false) && after <= before + 64;
    }
    if (operation == "network") {
        ui_status::WirelessObservation observation;
        std::istringstream values(argument);
        std::string mode, extra;
        int rssi = 0, clients = 0;
        if (!(values >> mode))
            return false;
        if (mode == "unknown")
            observation.available = false;
        else if (mode != "off")
            observation.enabled = true;
        if (mode == "home" || mode == "dual") {
            if (!(values >> rssi) || rssi < -128 || rssi > 127)
                return false;
            observation.stationConnected = true;
            observation.rssi = static_cast<int16_t>(rssi);
            std::snprintf(observation.stationName, sizeof(observation.stationName), "Dom-Bartek");
        }
        if (mode == "ap" || mode == "dual") {
            if (!(values >> clients) || clients < 0 || clients > 255)
                return false;
            observation.apActive = true;
            observation.clients = static_cast<uint8_t>(clients);
            std::snprintf(observation.apName, sizeof(observation.apName), "AquaBox-Setup");
        }
        if (mode != "home" && mode != "ap" && mode != "dual" && mode != "off" && mode != "lost" &&
            mode != "unknown")
            return false;
        if (values >> extra)
            return false;
        ui_status::previewNetwork(observation);
        drain(1100);
        return true;
    }
    if (operation == "clock") {
        std::istringstream values(argument);
        int hour, minute;
        std::string extra;
        if (argument == "interface") {
            if (!ui_status::previewClock(0, 0, false))
                return false;
        } else if (!(values >> hour >> minute) || values >> extra ||
                   !ui_status::previewClock(hour, minute, true))
            return false;
        drain(1100);
        return true;
    }
    if (operation == "swipe_soak") {
        const unsigned count = static_cast<unsigned>(std::strtoul(argument.c_str(), nullptr, 10));
        if (!count || count > 500 || !clickLabel("Panel", false))
            return false;
        auto cycle = []() {
            const char *tabs[] = {"Panel", "Plan dnia", "Historia", "Serwis", "System"};
            for (int index = 1; index < ui_navigation::kPageCount; ++index) {
                if (!pointerSwipe("650 250 180 250") || !command("assert_page", tabs[index]))
                    return false;
            }
            for (int index = ui_navigation::kPageCount - 2; index >= 0; --index) {
                if (!pointerSwipe("180 250 650 250") || !command("assert_page", tabs[index]))
                    return false;
            }
            return true;
        };
        if (!cycle())
            return false;
        drain(4000);
        const size_t before = ui_host_heap_used();
        for (unsigned iteration = 0; iteration < count; ++iteration)
            if (!cycle())
                return false;
        drain(4000);
        const size_t after = ui_host_heap_used();
        std::printf("SWIPE_SOAK cycles=%u before=%lu after=%lu\n", count,
                    static_cast<unsigned long>(before), static_cast<unsigned long>(after));
        return after <= before + 64;
    }
    if (operation == "soak") {
        const unsigned count = static_cast<unsigned>(std::strtoul(argument.c_str(), nullptr, 10));
        if (!count || count > 500)
            return false;
        const char *tabs[] = {"Panel", "Plan dnia", "Historia", "Serwis", "System"};
        for (const char *tab : tabs)
            if (!clickLabel(tab, false))
                return false;
        if (!clickLabel("Panel", false))
            return false;
        drain(4000);
        const size_t before = ui_host_heap_used();
        for (unsigned cycle = 0; cycle < count; ++cycle) {
            for (const char *tab : tabs)
                if (!clickLabel(tab, false))
                    return false;
            if (!clickLabel("Panel", false))
                return false;
        }
        drain(4000);
        const size_t after = ui_host_heap_used();
        std::printf("SOAK cycles=%u before=%lu after=%lu\n", count, static_cast<unsigned long>(before),
                    static_cast<unsigned long>(after));
        return after <= before + 64;
    }
    if (operation == "restart_ui") {
        if (!ui_app_init())
            return false;
        drain(400);
        return true;
    }
    if (operation == "fail_save") {
        ui_runtime::failNextSave();
        return true;
    }
    if (operation == "assert_restarts")
        return ui_runtime::restartCount() == std::strtoul(argument.c_str(), nullptr, 10);
    if (operation == "input") {
        // The production keyboard owns its textarea; tests enter text through that exact widget.
        std::vector<lv_obj_t *> objects{lv_layer_top()};
        for (size_t index = 0; index < objects.size(); ++index) {
            lv_obj_t *object = objects[index];
            if (lv_obj_check_type(object, &lv_keyboard_class)) {
                lv_obj_t *field = lv_keyboard_get_textarea(object);
                if (!field)
                    return false;
                lv_textarea_set_text(field, argument.c_str());
                lv_event_send(object, LV_EVENT_READY, nullptr);
                drain();
                return true;
            }
            for (unsigned child = 0; child < lv_obj_get_child_cnt(object); ++child)
                objects.push_back(lv_obj_get_child(object, child));
        }
        return false;
    }
    if (operation == "assert_nav_order") {
        const char *names[] = {"Panel", "Plan dnia", "Historia", "Serwis", "System"};
        for (unsigned index = 0; index < 5; ++index) {
            lv_obj_t *text = locate(names[index], false);
            if (!text)
                return false;
            lv_area_t area;
            lv_obj_get_coords(text, &area);
            if (area.y1 < 420 || std::abs(area.x1 - (30 + static_cast<int>(index) * 151)) > 1)
                return false;
        }
        return true;
    }
    if (operation == "assert_label_count") {
        const size_t separator = argument.find('|');
        if (separator == std::string::npos)
            return false;
        const std::string text = argument.substr(0, separator);
        const unsigned expected = std::strtoul(argument.substr(separator + 1).c_str(), nullptr, 10);
        unsigned count = 0;
        std::vector<lv_obj_t *> objects{lv_scr_act(), lv_layer_top()};
        for (size_t index = 0; index < objects.size(); ++index) {
            lv_obj_t *object = objects[index];
            if (lv_obj_check_type(object, &lv_label_class) && text == lv_label_get_text(object))
                ++count;
            for (unsigned child = 0; child < lv_obj_get_child_cnt(object); ++child)
                objects.push_back(lv_obj_get_child(object, child));
        }
        return count == expected;
    }
    if (operation == "set_slider") {
        char *end = nullptr;
        const long value = std::strtol(argument.c_str(), &end, 10);
        if (!end || *end || value < -10000 || value > 10000)
            return false;
        std::vector<lv_obj_t *> objects{lv_scr_act()};
        for (size_t index = 0; index < objects.size(); ++index) {
            lv_obj_t *object = objects[index];
            if (lv_obj_check_type(object, &lv_slider_class)) {
                lv_slider_set_value(object, value, LV_ANIM_OFF);
                lv_event_send(object, LV_EVENT_VALUE_CHANGED, nullptr);
                drain();
                return true;
            }
            for (unsigned child = 0; child < lv_obj_get_child_cnt(object); ++child)
                objects.push_back(lv_obj_get_child(object, child));
        }
        return false;
    }
    if (operation == "assert_no_save_popup") {
        return !findLabel(lv_layer_top(), "Zapisano", true, false) &&
               !findLabel(lv_layer_top(), "zapisano", true, false) &&
               !findLabel(lv_layer_top(), "Trwa zapis", true, false);
    }
    if (operation == "assert_history") {
        unsigned expanded = 0, layers = 0, seconds = 0;
        return std::sscanf(argument.c_str(), "%u %u %u", &expanded, &layers, &seconds) == 3 &&
               ui_history_view::previewInspection(expanded != 0, layers, seconds);
    }
    if (operation == "assert_history_focus")
        return ui_history_view::previewFocus(std::strtoul(argument.c_str(), nullptr, 10));
    static uint32_t rememberedHistoryPoint = UINT32_MAX;
    if (operation == "remember_history_point") {
        rememberedHistoryPoint = ui_history_view::previewSelection();
        return rememberedHistoryPoint != UINT32_MAX;
    }
    if (operation == "assert_history_point")
        return ui_history_view::previewSelection() == rememberedHistoryPoint;
    if (operation == "graph_soak") {
        const unsigned count = std::strtoul(argument.c_str(), nullptr, 10);
        if (!count || count > 500 || !clickLabel("Parametry wody", false))
            return false;
        drain(1000);
        const size_t before = ui_host_heap_used();
        for (unsigned index = 0; index < count; ++index) {
            pointerClick(300, 290);
            if (!clickLabel("Zakres:", true) || !clickLabel("72 h", false))
                return false;
            for (unsigned bit = 0; bit < 3; ++bit)
                if (!(ui_history_view::previewLayers() & (1u << bit)) && !clickLabel(bit == 0   ? "pH"
                                                                                     : bit == 1 ? "EC"
                                                                                                : "Grzałka",
                                                                                     false))
                    return false;
            pointerClick(44, 290);
            if (!ui_history_view::previewFocus(1) || !clickLabel("←", false) ||
                !ui_history_view::previewInspection(true, 7, 259200) || !clickLabel("Szczegóły", false) ||
                !clickLabel("←", false) || !clickLabel("←", false) || !clickLabel("Dolewki / ATO", false))
                return false;
            pointerClick(300, 290);
            if (!clickLabel("Zakres:", true) || !clickLabel("12 h", false) || !clickLabel("←", false) ||
                !clickLabel("Parametry wody", false))
                return false;
        }
        drain(1000);
        const size_t after = ui_host_heap_used();
        std::printf("GRAPH_SOAK cycles=%u before=%lu after=%lu\n", count,
                    static_cast<unsigned long>(before), static_cast<unsigned long>(after));
        return after <= before + 128;
    }
    if (operation == "ato_report") {
        char *end = nullptr;
        const unsigned long duration = std::strtoul(argument.c_str(), &end, 10);
        if (!end || *end || !ui_model::recordAtoDose(duration))
            return false;
        drain();
        return true;
    }
    if (operation == "seed_history") {
        const unsigned hours = std::strtoul(argument.c_str(), nullptr, 10);
        if (!hours || hours > 96)
            return false;
        ui_model::clearHistory();
        for (unsigned index = 1; index <= hours * 12; ++index) {
            elapsedMs += 300000u;
            ui_model::tick(elapsedMs);
            ui_model::setScenario(index % 17 == 0  ? ui_model::Scenario::Offline
                                  : index % 7 == 0 ? ui_model::Scenario::Alarm
                                  : index % 3 == 0 ? ui_model::Scenario::LowWater
                                                   : ui_model::Scenario::DemoHealthy);
            if (index % 12 == 0)
                ui_model::recordAtoDose(index % 24 == 0 ? 60000 : 30000);
        }
        // Fixture time jumps must not count as unattended physical screen sleep.
        ++touches;
        drain();
        return true;
    }
    if (operation == "scroll_to") {
        lv_obj_t *object = locate(argument, true, false);
        if (!object)
            return false;
        lv_obj_scroll_to_view_recursive(object, LV_ANIM_OFF);
        drain();
        return true;
    }
    if (operation == "assert_palette") {
        const bool night = argument == "dark";
        if (!night && argument != "light")
            return false;
        ui_runtime::Configuration saved;
        ui_theme::Settings colors{};
        if (ui_runtime::load(saved))
            colors = saved.prefs.colors;
        const auto p = ui_theme::resolve(colors, night);
        lv_obj_t *screen = lv_scr_act();
        return lv_obj_get_style_bg_color(lv_obj_get_child(screen, 0), 0).full ==
                   lv_color_hex(p.chrome).full &&
               lv_obj_get_style_bg_color(lv_obj_get_child(screen, 3), 0).full ==
                   lv_color_hex(p.navigation).full &&
               lv_obj_get_style_bg_color(screen, 0).full == lv_color_hex(p.background).full;
    }
    if (operation == "assert_colors") {
        std::istringstream values(argument);
        int preset, mode, role;
        unsigned color;
        std::string extra;
        if (!(values >> preset >> mode >> role >> std::hex >> color) || values >> extra ||
            preset < 0 || preset >= ui_theme::kPresetCount || mode < 0 || mode > 1 ||
            role < 0 || role >= ui_theme::kRoleCount || color > 0xFFFFFFu)
            return false;
        ui_runtime::Configuration saved;
        return ui_runtime::load(saved) && saved.prefs.colors.preset[mode] == preset &&
               ui_theme::roleColor(saved.prefs.colors, mode != 0,
                                   static_cast<ui_theme::Role>(role)) == color;
    }
    if (operation == "assert_rendered_theme") {
        const bool night = argument == "dark";
        if (!night && argument != "light")
            return false;
        ui_runtime::Configuration saved;
        if (!ui_runtime::load(saved))
            return false;
        const auto palette = ui_theme::resolve(saved.prefs.colors, night);
        lv_obj_t *screen = lv_scr_act();
        return lv_obj_get_style_bg_color(screen, 0).full == lv_color_hex(palette.background).full &&
               lv_obj_get_style_bg_color(lv_obj_get_child(screen, 0), 0).full ==
                   lv_color_hex(palette.chrome).full &&
               lv_obj_get_style_bg_color(lv_obj_get_child(screen, 3), 0).full ==
                   lv_color_hex(palette.navigation).full;
    }
    if (operation == "assert_unified_events") {
        for (const char *text : {"Ważne zdarzenia", "Zwykłe zdarzenia", "WAŻNE", "ZWYKŁE"})
            if (locate(text, true, false))
                return false;
        return true;
    }
    if (operation == "click")
        return clickLabel(argument, false);
    if (operation == "swipe")
        return pointerSwipe(argument);
    if (operation == "contains")
        return clickLabel(argument, true);
    if (operation == "snapshot")
        return snapshot(argument);
    if (operation == "assert") {
        if (locate(argument, false))
            return true;
        std::fprintf(stderr, "Missing expected visible label: %s\n", argument.c_str());
        return false;
    }
    if (operation == "assert_contains") {
        if (locate(argument, true))
            return true;
        std::fprintf(stderr, "Missing expected visible text: %s\n", argument.c_str());
        return false;
    }
    if (operation == "assert_not_contains") {
        if (!locate(argument, true))
            return true;
        std::fprintf(stderr, "Unexpected visible text: %s\n", argument.c_str());
        return false;
    }
    if (operation == "assert_page" || operation == "assert_disabled") {
        lv_obj_t *object = locate(argument, false);
        while (object && !lv_obj_has_flag(object, LV_OBJ_FLAG_CLICKABLE))
            object = lv_obj_get_parent(object);
        const lv_state_t required = operation == "assert_page" ? LV_STATE_CHECKED : LV_STATE_DISABLED;
        if (object && lv_obj_has_state(object, required))
            return true;
        std::fprintf(stderr, "Unexpected navigation state (%s): %s\n", operation.c_str(), argument.c_str());
        return false;
    }
    if (operation == "wait") {
        char *end = nullptr;
        const unsigned long duration = std::strtoul(argument.c_str(), &end, 10);
        if (!end || *end || duration > 60000)
            return false;
        drain(static_cast<uint32_t>(duration));
        return true;
    }
    if (operation == "tap" || operation == "tap_start") {
        std::istringstream position(argument);
        int x = 0;
        int y = 0;
        std::string extra;
        if (!(position >> x >> y) || position >> extra || x < 0 || x >= kWidth || y < 0 || y >= kHeight)
            return false;
        pointerClick(x, y, operation == "tap_start" ? 20 : 180);
        return true;
    }
    if (operation == "dump") {
        dumpLabels(lv_scr_act());
        dumpLabels(lv_layer_top());
        return true;
    }
    std::fprintf(stderr, "Unknown command: %s\n", operation.c_str());
    return false;
}

bool runScript(const char *path) {
    std::ifstream script(path);
    if (!script) {
        std::fprintf(stderr, "Cannot open script: %s\n", path);
        return false;
    }
    std::string line;
    unsigned lineNumber = 0;
    while (std::getline(script, line)) {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (lineNumber == 1 && line.compare(0, 3, "\xEF\xBB\xBF") == 0)
            line.erase(0, 3);
        if (line.empty() || line[0] == '#')
            continue;
        const size_t split = line.find(' ');
        const std::string operation = line.substr(0, split);
        const std::string argument = split == std::string::npos ? "" : line.substr(split + 1);
        if (!command(operation, argument)) {
            std::fprintf(stderr, "Script failed at %s:%u\n", path, lineNumber);
            snapshot("failure");
            dumpLabels(lv_scr_act());
            dumpLabels(lv_layer_top());
            return false;
        }
    }
    return true;
}

} // namespace

PreviewEsp ESP;
uint32_t millis() { return elapsedMs; }
bool panel_hal_touch_ready() { return true; }
uint32_t panel_hal_touch_count() { return touches; }

int main(int argc, char **argv) {
    if (!ui_model_checks() || !ui_status_checks() || !ui_runtime_checks() || !ui_plan_checks() ||
        !ui_theme_checks() || !ui_history_time_checks())
        return EXIT_FAILURE;
    const char *script = nullptr;
    int exploration = 0;
    for (int argument = 1; argument < argc; ++argument) {
        if (std::strcmp(argv[argument], "--out") == 0 && argument + 1 < argc)
            outputDirectory = argv[++argument];
        else if (std::strcmp(argv[argument], "--script") == 0 && argument + 1 < argc)
            script = argv[++argument];
        else if (std::strcmp(argv[argument], "--explore") == 0 && argument + 1 < argc)
            exploration = std::atoi(argv[++argument]);
        else {
            std::fprintf(stderr, "Usage: program.exe [--out directory] [--script scenario.txt]\n");
            return EXIT_FAILURE;
        }
    }
    lv_init();
    lv_disp_draw_buf_init(&drawBuffer, drawPixels, nullptr, kWidth * 40);
    lv_disp_drv_init(&displayDriver);
    displayDriver.hor_res = kWidth;
    displayDriver.ver_res = kHeight;
    displayDriver.draw_buf = &drawBuffer;
    displayDriver.flush_cb = flush;
    if (!lv_disp_drv_register(&displayDriver))
        return EXIT_FAILURE;
    lv_indev_drv_init(&inputDriver);
    inputDriver.type = LV_INDEV_TYPE_POINTER;
    inputDriver.read_cb = readPointer;
    inputDriver.gesture_limit = ui_navigation::kGestureLimit;
    inputDriver.gesture_min_velocity = ui_navigation::kGestureMinVelocity;
    if (!lv_indev_drv_register(&inputDriver))
        return EXIT_FAILURE;
    if (exploration)
        ui_exploration_init(exploration);
    else if (!ui_app_init()) {
        std::fprintf(stderr, "UI initialization failed\n");
        return EXIT_FAILURE;
    }
    drain(250);
    const bool success = script ? runScript(script) : snapshot("home");
    if (success) {
        std::printf("PASS frames=%lu touches=%lu objects=%lu lvgl_heap_used=%lu lvgl_heap_peak=%lu\n",
                    static_cast<unsigned long>(frames), static_cast<unsigned long>(touches),
                    static_cast<unsigned long>(objectCount(lv_scr_act()) + objectCount(lv_layer_top())),
                    static_cast<unsigned long>(ui_host_heap_used()),
                    static_cast<unsigned long>(ui_host_heap_peak()));
    }
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
