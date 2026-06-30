#include "wifi_retry_policy.h"

namespace aquarium {

WifiRetryPolicy::WifiRetryPolicy(uint8_t capacity_reason, uint32_t capacity_cooldown_ms)
    : capacityReason_(capacity_reason),
      capacityCooldownMs_(capacity_cooldown_ms),
      retryAfterMs_(0U) {
}

void WifiRetryPolicy::on_disconnect(uint8_t reason, uint32_t now_ms) {
    if (is_capacity_rejection(reason)) {
        retryAfterMs_ = now_ms + capacityCooldownMs_;
    }
}

void WifiRetryPolicy::on_connected() {
    retryAfterMs_ = 0U;
}

bool WifiRetryPolicy::retry_allowed(uint32_t now_ms) const {
    return remaining_ms(now_ms) == 0U;
}

uint32_t WifiRetryPolicy::remaining_ms(uint32_t now_ms) const {
    if (retryAfterMs_ == 0U || static_cast<int32_t>(now_ms - retryAfterMs_) >= 0) {
        return 0U;
    }
    return static_cast<uint32_t>(retryAfterMs_ - now_ms);
}

bool WifiRetryPolicy::is_capacity_rejection(uint8_t reason) const {
    return reason == capacityReason_;
}

} // namespace aquarium
