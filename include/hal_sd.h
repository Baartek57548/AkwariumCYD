#ifndef HAL_SD_H
#define HAL_SD_H

#include <Arduino.h>

bool hal_sd_init(void);
bool hal_sd_is_mounted(void);
uint64_t hal_sd_card_size_bytes(void);

#endif // HAL_SD_H
