#include "ui_history_time.h"
#include <cstdio>
namespace ui_history_time {
namespace {
bool leap(int year) { return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0); }
int monthDays(int year, int month) {
    static const int lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return lengths[month - 1] + (month == 2 && leap(year));
}
} // namespace
uint32_t encode(int year, int month, int day, int hour, int minute, int second) {
    if (year < 2024 || year > 2099 || month < 1 || month > 12 || day < 1 || day > monthDays(year, month) ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59)
        return 0;
    uint32_t days = 0;
    for (int current = 2000; current < year; ++current)
        days += leap(current) ? 366 : 365;
    for (int current = 1; current < month; ++current)
        days += monthDays(year, current);
    days += day - 1;
    return days * 86400u + hour * 3600u + minute * 60u + second;
}
void format(uint32_t stamp, char *text, size_t capacity, bool date) {
    if (!text || !capacity)
        return;
    if (!stamp) {
        std::snprintf(text, capacity, "--:--:--");
        return;
    }
    uint32_t days = stamp / 86400u;
    int year = 2000, month = 1;
    while (year <= 2099 && days >= static_cast<unsigned>(leap(year) ? 366 : 365)) {
        days -= leap(year) ? 366 : 365;
        ++year;
    }
    if (year < 2024 || year > 2099) {
        std::snprintf(text, capacity, "--:--:--");
        return;
    }
    while (month < 12 && days >= static_cast<unsigned>(monthDays(year, month))) {
        days -= monthDays(year, month);
        ++month;
    }
    const unsigned hour = (stamp / 3600u) % 24, minute = (stamp / 60u) % 60, second = stamp % 60;
    if (date)
        std::snprintf(text, capacity, "%02u.%02d %02u:%02u:%02u", days + 1, month, hour, minute, second);
    else
        std::snprintf(text, capacity, "%02u:%02u:%02u", hour, minute, second);
}
bool continuous(uint32_t previousStamp, uint32_t stamp, uint32_t elapsedSeconds) {
    if (!previousStamp || !stamp)
        return true;
    const int64_t difference = static_cast<int64_t>(stamp) - previousStamp - elapsedSeconds;
    return difference >= -2 && difference <= 2;
}
} // namespace ui_history_time
