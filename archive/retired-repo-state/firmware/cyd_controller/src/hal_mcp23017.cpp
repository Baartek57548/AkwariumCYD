#include "hal_mcp23017.h"
#include "uart_client.h"

// CYD jest czystym terminalem HMI - nie posiada lokalnego układu MCP23017.
// Wszystkie operacje są stubami lub przekierowaniami do protokołu UART.

bool hal_mcp_init(void) {
    return true;
}

bool hal_mcp_is_present(void) {
    return g_uart_client.isControllerOnline();
}

bool hal_mcp_write_channel(HwConfig::McpChannel channel, bool on) {
    if (static_cast<uint8_t>(channel) < 8U) {
        return g_uart_client.sendSetRelay(static_cast<uint8_t>(channel), on);
    }
    return true;
}

bool hal_mcp_read_channel(HwConfig::McpChannel, bool *out) {
    if (out != nullptr) *out = false;
    return false;
}

bool hal_mcp_read_all(uint16_t *out) {
    if (out != nullptr) {
        if (g_uart_client.isControllerOnline()) {
            const auto &t = g_uart_client.getLatestTelemetry();
            if (t.water_level_max_present && !t.water_level_max_valid) {
                *out = 0U;
                return false;
            }
            uint8_t inputs = 0;
            if (!t.water_level_max_reached) inputs |= (1U << 1);
            if (!t.water_level_low) inputs |= (1U << 2);
            *out = static_cast<uint16_t>(t.relay_states) | (static_cast<uint16_t>(inputs) << 8);
            return true;
        }
        *out = 0U;
    }
    return false;
}

bool hal_mcp_all_relays_safe(void) {
    return true;
}

bool hal_mcp_latch_all_relays_safe(void) {
    return true;
}
