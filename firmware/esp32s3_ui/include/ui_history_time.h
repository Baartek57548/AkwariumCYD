#ifndef AQUABOX_HISTORY_TIME_H
#define AQUABOX_HISTORY_TIME_H
#include <stddef.h>
#include <stdint.h>
namespace ui_history_time {
// Local civil seconds since 2000. Zero explicitly means that no clock was available.
uint32_t encode(int year, int month, int day, int hour, int minute, int second);
void format(uint32_t stamp, char *text, size_t capacity, bool date = false);
bool continuous(uint32_t previousStamp, uint32_t stamp, uint32_t elapsedSeconds);
} // namespace ui_history_time
#endif
