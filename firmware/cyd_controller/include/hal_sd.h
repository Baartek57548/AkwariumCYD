#ifndef HAL_SD_H
#define HAL_SD_H

#include <Arduino.h>

bool hal_sd_init(void);
bool hal_sd_is_mounted(void);
uint64_t hal_sd_card_size_bytes(void);
bool hal_sd_lock(uint32_t timeout_ms = 1000U);
void hal_sd_unlock(void);
void hal_sd_unmount(void);
bool hal_sd_check_health(void);
void hal_sd_notify_io_error(void);

#endif // HAL_SD_H
