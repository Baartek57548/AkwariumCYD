#ifndef AQUARIUM_WEB_ACTIVITY_TRACKER_H
#define AQUARIUM_WEB_ACTIVITY_TRACKER_H

#include <stddef.h>
#include <stdint.h>

namespace aquarium {

class WebActivityTracker {
public:
    static constexpr uint8_t kCapacity = 4U;
    static constexpr size_t kMaxSessionIdLength = 24U;

    explicit WebActivityTracker(uint32_t timeout_ms);

    bool touch(const char *session_id, uint32_t now_ms);
    bool release(const char *session_id);
    uint8_t active_count(uint32_t now_ms);
    void clear();
    static bool valid_session_id(const char *session_id);

private:
    struct Slot {
        char id[kMaxSessionIdLength + 1U];
        uint32_t lastSeenMs;
        bool active;
    };

    void prune(uint32_t now_ms);

    Slot slots_[kCapacity];
    uint32_t timeoutMs_;
};

} // namespace aquarium

#endif // AQUARIUM_WEB_ACTIVITY_TRACKER_H
