#include "admin_session.h"

#include <stdio.h>
#include <string.h>

namespace aquarium {

AdminSessionManager::AdminSessionManager() {
    clear();
}

void AdminSessionManager::clear() {
    memset(sessions_, 0, sizeof(sessions_));
    failed_attempts_ = 0U;
    lockout_active_ = false;
    locked_until_ms_ = 0U;
}

bool AdminSessionManager::is_locked(uint32_t now_ms) const {
    return lockout_active_ &&
           static_cast<int32_t>(locked_until_ms_ - now_ms) > 0;
}

uint32_t AdminSessionManager::remaining_seconds(uint32_t deadline_ms,
                                                uint32_t now_ms) {
    if (static_cast<int32_t>(deadline_ms - now_ms) <= 0) {
        return 0U;
    }
    return ((deadline_ms - now_ms) + 999U) / 1000U;
}

void AdminSessionManager::expire(uint32_t now_ms) {
    for (size_t index = 0U; index < kMaxSessions; ++index) {
        if (sessions_[index].active &&
            static_cast<int32_t>(sessions_[index].deadline_ms - now_ms) <= 0) {
            memset(&sessions_[index], 0, sizeof(sessions_[index]));
        }
    }
    if (lockout_active_ &&
        static_cast<int32_t>(locked_until_ms_ - now_ms) <= 0) {
        lockout_active_ = false;
        locked_until_ms_ = 0U;
        failed_attempts_ = 0U;
    }
}

AuthenticationStatus AdminSessionManager::authenticate(
    bool pin_matches,
    uint32_t now_ms,
    const uint32_t entropy[4],
    char *out_token,
    size_t out_size) {
    expire(now_ms);
    if (out_token != nullptr && out_size > 0U) {
        out_token[0] = '\0';
    }
    if (is_locked(now_ms)) {
        return {AuthenticationResult::RateLimited,
                remaining_seconds(locked_until_ms_, now_ms)};
    }
    if (!pin_matches) {
        if (failed_attempts_ < UINT8_MAX) {
            ++failed_attempts_;
        }
        if (failed_attempts_ >= kMaxFailedPinAttempts) {
            lockout_active_ = true;
            locked_until_ms_ = now_ms + kLockoutMs;
            return {AuthenticationResult::RateLimited, kLockoutMs / 1000U};
        }
        return {AuthenticationResult::InvalidPin, 0U};
    }
    if (entropy == nullptr || out_token == nullptr || out_size < kTokenBytes) {
        return {AuthenticationResult::InvalidToken, 0U};
    }

    failed_attempts_ = 0U;
    lockout_active_ = false;
    locked_until_ms_ = 0U;
    size_t destination = kMaxSessions;
    uint32_t oldest_age = 0U;
    for (size_t index = 0U; index < kMaxSessions; ++index) {
        if (!sessions_[index].active) {
            destination = index;
            break;
        }
        const uint32_t age = now_ms - sessions_[index].issued_ms;
        if (destination == kMaxSessions || age > oldest_age) {
            destination = index;
            oldest_age = age;
        }
    }
    if (destination >= kMaxSessions) {
        return {AuthenticationResult::InvalidToken, 0U};
    }

    Session &session = sessions_[destination];
    snprintf(session.token, sizeof(session.token),
             "%08lx%08lx%08lx%08lx",
             static_cast<unsigned long>(entropy[0]),
             static_cast<unsigned long>(entropy[1]),
             static_cast<unsigned long>(entropy[2]),
             static_cast<unsigned long>(entropy[3]));
    session.active = true;
    session.issued_ms = now_ms;
    session.deadline_ms = now_ms + kSessionTtlMs;
    snprintf(out_token, out_size, "%s", session.token);
    return {AuthenticationResult::Authenticated, 0U};
}

bool AdminSessionManager::secure_equal(const char *left, const char *right) {
    if (left == nullptr || right == nullptr) {
        return false;
    }
    const size_t left_length = strlen(left);
    const size_t right_length = strlen(right);
    unsigned char difference = static_cast<unsigned char>(left_length ^ right_length);
    const size_t length = left_length > right_length ? left_length : right_length;
    for (size_t index = 0U; index < length; ++index) {
        const unsigned char a = index < left_length
                                    ? static_cast<unsigned char>(left[index])
                                    : 0U;
        const unsigned char b = index < right_length
                                    ? static_cast<unsigned char>(right[index])
                                    : 0U;
        difference = static_cast<unsigned char>(difference | (a ^ b));
    }
    return difference == 0U;
}

bool AdminSessionManager::validate(const char *token, uint32_t now_ms) {
    expire(now_ms);
    if (token == nullptr || strlen(token) != kTokenBytes - 1U) {
        return false;
    }
    bool valid = false;
    for (size_t index = 0U; index < kMaxSessions; ++index) {
        const bool equal = secure_equal(token, sessions_[index].token);
        valid = valid || (sessions_[index].active && equal);
    }
    return valid;
}

void AdminSessionManager::revoke(const char *token) {
    if (token == nullptr) {
        return;
    }
    for (size_t index = 0U; index < kMaxSessions; ++index) {
        if (sessions_[index].active && secure_equal(token, sessions_[index].token)) {
            memset(&sessions_[index], 0, sizeof(sessions_[index]));
        }
    }
}

uint32_t AdminSessionManager::token_remaining_seconds(const char *token,
                                                      uint32_t now_ms) {
    expire(now_ms);
    if (token == nullptr) {
        return 0U;
    }
    for (size_t index = 0U; index < kMaxSessions; ++index) {
        if (sessions_[index].active && secure_equal(token, sessions_[index].token)) {
            return remaining_seconds(sessions_[index].deadline_ms, now_ms);
        }
    }
    return 0U;
}

} // namespace aquarium
