#ifndef AQUARIUM_ADMIN_SESSION_H
#define AQUARIUM_ADMIN_SESSION_H

#include <stddef.h>
#include <stdint.h>

namespace aquarium {

enum class AuthenticationResult : uint8_t {
    Authenticated = 0U,
    InvalidPin = 1U,
    RateLimited = 2U,
    InvalidToken = 3U
};

struct AuthenticationStatus {
    AuthenticationResult result;
    uint32_t retry_after_seconds;
};

class AdminSessionManager {
public:
    static constexpr size_t kMaxSessions = 2U;
    static constexpr size_t kTokenBytes = 33U;
    static constexpr uint32_t kSessionTtlMs = 5U * 60U * 1000U;
    static constexpr uint8_t kMaxFailedPinAttempts = 5U;
    static constexpr uint32_t kLockoutMs = 60U * 1000U;

    AdminSessionManager();

    AuthenticationStatus authenticate(bool pin_matches,
                                      uint32_t now_ms,
                                      const uint32_t entropy[4],
                                      char *out_token,
                                      size_t out_size);
    bool validate(const char *token, uint32_t now_ms);
    void revoke(const char *token);
    void clear();
    uint32_t token_remaining_seconds(const char *token, uint32_t now_ms);

private:
    struct Session {
        bool active;
        char token[kTokenBytes];
        uint32_t issued_ms;
        uint32_t deadline_ms;
    };

    Session sessions_[kMaxSessions];
    uint8_t failed_attempts_;
    bool lockout_active_;
    uint32_t locked_until_ms_;

    void expire(uint32_t now_ms);
    bool is_locked(uint32_t now_ms) const;
    static bool secure_equal(const char *left, const char *right);
    static uint32_t remaining_seconds(uint32_t deadline_ms, uint32_t now_ms);
};

} // namespace aquarium

#endif
