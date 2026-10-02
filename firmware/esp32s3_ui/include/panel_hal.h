#ifndef AQUARIUM_S3_PANEL_HAL_H
#define AQUARIUM_S3_PANEL_HAL_H

#include <stdint.h>

bool panel_hal_init();
void panel_hal_service();
bool panel_hal_touch_ready();
uint32_t panel_hal_touch_count();
bool panel_hal_begin_flash_write();
bool panel_hal_end_flash_write();

#endif
