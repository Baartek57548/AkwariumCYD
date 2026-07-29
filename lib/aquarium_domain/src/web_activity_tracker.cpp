#include "web_activity_tracker.h"

#include <limits.h>
#include <string.h>

namespace aquarium {

WebActivityTracker::WebActivityTracker(uint32_t timeout_ms)
    : timeoutMs_(timeout_ms == 0U ? 1U : timeout_ms) {
    clear();
}

bool WebActivityTracker::valid_session_id(const char *session_id) {
    if (session_id == nullptr || session_id[0] == '\0') {
        return false;
    }
    size_t length = 0U;
    while (session_id[length] != '\0') {
        const char c = session_id[length];
        const bool allowed = (c >= '0' && c <= '9') ||
                             (c >= 'A' && c <= 'Z') ||
                             (c >= 'a' && c <= 'z') || c == '-' || c == '_';
        if (!allowed || ++length > kMaxSessionIdLength) {
            return false;
        }
    }
    return length >= 6U;
}

void WebActivityTracker::prune(uint32_t now_ms) {
    for (uint8_t i = 0U; i < kCapacity; ++i) {
        Slot &slot = slots_[i];
        if (slot.active && static_cast<uint32_t>(now_ms - slot.lastSeenMs) > timeoutMs_) {
            slot.active = false;
            slot.lastSeenMs = 0U;
            slot.id[0] = '\0';
        }
    }
}

bool WebActivityTracker::touch(const char *session_id, uint32_t now_ms) {
    if (!valid_session_id(session_id)) {
        return false;
    }
    prune(now_ms);

    int free_slot = -1;
    uint8_t oldest_slot = 0U;
    uint32_t oldest_seen = UINT_MAX;
    for (uint8_t i = 0U; i < kCapacity; ++i) {
        Slot &slot = slots_[i];
        if (slot.active && strncmp(slot.id, session_id, kMaxSessionIdLength) == 0) {
            slot.lastSeenMs = now_ms;
            return true;
        }
        if (!slot.active && free_slot < 0) {
            free_slot = static_cast<int>(i);
        }
        if (slot.lastSeenMs < oldest_seen) {
            oldest_seen = slot.lastSeenMs;
            oldest_slot = i;
        }
    }

    Slot &target = slots_[free_slot >= 0 ? static_cast<uint8_t>(free_slot) : oldest_slot];
    strncpy(target.id, session_id, kMaxSessionIdLength);
    target.id[kMaxSessionIdLength] = '\0';
    target.lastSeenMs = now_ms;
    target.active = true;
    return true;
}

bool WebActivityTracker::release(const char *session_id) {
    if (!valid_session_id(session_id)) {
        return false;
    }
    for (uint8_t i = 0U; i < kCapacity; ++i) {
        Slot &slot = slots_[i];
        if (slot.active && strncmp(slot.id, session_id, kMaxSessionIdLength) == 0) {
            slot.active = false;
            slot.lastSeenMs = 0U;
            slot.id[0] = '\0';
            return true;
        }
    }
    return false;
}

uint8_t WebActivityTracker::active_count(uint32_t now_ms) {
    prune(now_ms);
    uint8_t count = 0U;
    for (uint8_t i = 0U; i < kCapacity; ++i) {
        if (slots_[i].active) {
            ++count;
        }
    }
    return count;
}

void WebActivityTracker::clear() {
    memset(slots_, 0, sizeof(slots_));
}

} // namespace aquarium
