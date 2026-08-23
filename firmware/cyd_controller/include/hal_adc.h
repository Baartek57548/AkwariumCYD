#ifndef HAL_ADC_H
#define HAL_ADC_H

#include <Arduino.h>

bool hal_adc_init(void);
bool hal_adc_is_present(void);
bool hal_adc_read_raw(uint8_t ch, int16_t *out);
bool hal_adc_read_voltage(uint8_t ch, float *out);

#endif
