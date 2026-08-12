#include "ble_pairing_policy.h"

namespace aquarium {

BlePairingPolicy::BlePairingPolicy()
    : state_{
          false,
          kInvalidConnectionHandle,
          0U,
          0U,
          0U},
      previous_passkey_(0U) {
}

uint32_t BlePairingPolicy::begin(uint16_t connection_handle,
                                 uint32_t generation,
                                 uint32_t now_ms,
                                 uint32_t random_word) {
    if (connection_handle == kInvalidConnectionHandle || generation == 0U) {
        clear_active_attempt();
        return 0U;
    }

    uint32_t passkey = kMinimumPasskey + (random_word % kPasskeyCount);
    if (passkey == previous_passkey_) {
        passkey = passkey == kMaximumPasskey
                      ? kMinimumPasskey
                      : passkey + 1U;
    }

    state_ = {
        true,
        connection_handle,
        generation,
        passkey,
        now_ms};
    previous_passkey_ = passkey;
    return passkey;
}

uint32_t BlePairingPolicy::displayed_passkey(uint32_t now_ms) const {
    if (!state_.active || is_expired(state_, now_ms)) {
        return 0U;
    }
    return state_.passkey;
}

bool BlePairingPolicy::expire(uint32_t now_ms,
                              BlePairingAttemptState *expired_attempt) {
    if (!state_.active || !is_expired(state_, now_ms)) {
        return false;
    }
    if (expired_attempt != nullptr) {
        *expired_attempt = state_;
    }
    clear_active_attempt();
    return true;
}

bool BlePairingPolicy::complete(uint16_t connection_handle,
                                uint32_t generation) {
    if (!state_.active ||
        state_.connection_handle != connection_handle ||
        state_.generation != generation) {
        return false;
    }
    clear_active_attempt();
    return true;
}

void BlePairingPolicy::clear() {
    clear_active_attempt();
}

const BlePairingAttemptState &BlePairingPolicy::state() const {
    return state_;
}

bool BlePairingPolicy::is_expired(const BlePairingAttemptState &state,
                                  uint32_t now_ms) {
    return static_cast<uint32_t>(now_ms - state.started_ms) >=
           kAttemptTimeoutMs;
}

void BlePairingPolicy::clear_active_attempt() {
    state_ = {
        false,
        kInvalidConnectionHandle,
        0U,
        0U,
        0U};
}

} // namespace aquarium
