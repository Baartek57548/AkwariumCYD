#ifndef AQUARIUM_WIFI_RETRY_POLICY_H
#define AQUARIUM_WIFI_RETRY_POLICY_H

#include <stdint.h>

namespace aquarium {

class WifiRetryPolicy {
public:
    WifiRetryPolicy(uint8_t capacity_reason, uint32_t capacity_cooldown_ms);

    void on_disconnect(uint8_t reason, uint32_t now_ms);
    void on_connected();
    bool retry_allowed(uint32_t now_ms) const;
    uint32_t remaining_ms(uint32_t now_ms) const;
    bool is_capacity_rejection(uint8_t reason) const;

private:
    uint8_t capacityReason_;
    uint32_t capacityCooldownMs_;
    uint32_t retryAfterMs_;
};

} // namespace aquarium

#endif // AQUARIUM_WIFI_RETRY_POLICY_H
