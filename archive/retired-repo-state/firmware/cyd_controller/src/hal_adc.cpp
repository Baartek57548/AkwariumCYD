#include "hal_adc.h"
#include "uart_client.h"

// CYD jest czystym terminalem HMI - nie posiada lokalnego przetwornika ADS1115.

bool hal_adc_init(void) {
    return true;
}

bool hal_adc_is_present(void) {
    return g_uart_client.isControllerOnline();
}

bool hal_adc_read_raw(uint8_t, int16_t *out) {
    if (out != nullptr) *out = 0;
    return false;
}

bool hal_adc_read_voltage(uint8_t, float *out) {
    if (out != nullptr) *out = 0.0f;
    return false;
}
