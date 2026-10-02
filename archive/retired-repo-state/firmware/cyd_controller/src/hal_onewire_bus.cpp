#include "hal_onewire_bus.h"
#include "uart_client.h"

// CYD jest czystym terminalem HMI - nie posiada lokalnej magistrali OneWire DS18B20.

bool hal_onewire_bus_scan(HalOneWireScanResult *result) {
    if (result != nullptr) {
        result->count = 0;
        result->truncated = false;
    }
    return true;
}

bool hal_temperature_init(void) {
    return true;
}

bool hal_temperature_poll(uint32_t, HalTemperatureReading *out) {
    if (out != nullptr) {
        if (g_uart_client.isControllerOnline()) {
            const auto &t = g_uart_client.getLatestTelemetry();
            out->celsius = t.temperature;
            out->valid = (t.temperature > -50.0f && t.temperature < 80.0f);
            out->present = true;
            out->stale = false;
            out->age_ms = 0;
            out->error_count = 0;
        } else {
            out->celsius = NAN;
            out->valid = false;
            out->present = false;
            out->stale = true;
            out->age_ms = 0;
            out->error_count = 0;
        }
    }
    return g_uart_client.isControllerOnline();
}
