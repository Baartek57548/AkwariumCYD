#ifndef AQUARIUM_BLE_PAIRING_POLICY_H
#define AQUARIUM_BLE_PAIRING_POLICY_H

#include <stdint.h>

namespace aquarium {

struct BlePairingAttemptState {
    bool active;
    uint16_t connection_handle;
    uint32_t generation;
    uint32_t passkey;
    uint32_t started_ms;
};

class BlePairingPolicy {
public:
    static constexpr uint16_t kInvalidConnectionHandle = 0xFFFFU;
    static constexpr uint32_t kMinimumPasskey = 100000U;
    static constexpr uint32_t kMaximumPasskey = 999999U;
    static constexpr uint32_t kPasskeyCount = 900000U;
    static constexpr uint32_t kAttemptTimeoutMs = 5U * 60U * 1000U;

    BlePairingPolicy();

    uint32_t begin(uint16_t connection_handle,
                   uint32_t generation,
                   uint32_t now_ms,
                   uint32_t random_word);
    uint32_t displayed_passkey(uint32_t now_ms) const;
    bool expire(uint32_t now_ms, BlePairingAttemptState *expired_attempt);
    bool complete(uint16_t connection_handle, uint32_t generation);
    void clear();

    const BlePairingAttemptState &state() const;

private:
    static bool is_expired(const BlePairingAttemptState &state,
                           uint32_t now_ms);
    void clear_active_attempt();

    BlePairingAttemptState state_;
    uint32_t previous_passkey_;
};

} // namespace aquarium

#endif
