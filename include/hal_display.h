#ifndef HAL_DISPLAY_H
#define HAL_DISPLAY_H

#include <Arduino.h>

void hal_display_init(void);

bool hal_display_draw_rgb565_file(const char *path, uint16_t width, uint16_t height);

bool hal_display_play_rgb565_sequence(const char *frame_pattern,
                                      uint16_t frame_count,
                                      uint16_t width,
                                      uint16_t height,
                                      uint16_t fps);

bool hal_display_get_touch(int16_t *x, int16_t *y);

void hal_display_loop_cb(void);

#endif // HAL_DISPLAY_H
