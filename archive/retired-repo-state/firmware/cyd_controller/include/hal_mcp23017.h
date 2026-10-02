#ifndef HAL_MCP23017_H
#define HAL_MCP23017_H

#include <Arduino.h>

#include "config.h"

bool hal_mcp_init(void);
bool hal_mcp_is_present(void);
bool hal_mcp_write_channel(HwConfig::McpChannel channel, bool on);
bool hal_mcp_read_channel(HwConfig::McpChannel channel, bool *out);
bool hal_mcp_read_all(uint16_t *out);
bool hal_mcp_all_relays_safe(void);
/**
 * Permanently blocks ON writes until the next boot and drives every output to
 * the safe state. Used by the supervisor before a controlled restart.
 */
bool hal_mcp_latch_all_relays_safe(void);

#endif
