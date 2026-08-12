#ifndef AQUARIUM_AQUAEL_LIGHT_CONTROLLER_H
#define AQUARIUM_AQUAEL_LIGHT_CONTROLLER_H

#include <stdint.h>

namespace aquarium {

enum class AquaelProfile : uint8_t {
    Day = 0U,
    Daybreak = 1U,
    Night = 2U
};

enum class AquaelTransitionStage : uint8_t {
    Off = 0U,
    ResetOff = 1U,
    ResetOnSettle = 2U,
    Stable = 3U,
    CycleOff = 4U,
    CycleOnSettle = 5U,
    RetryWait = 6U
};

struct AquaelDriveDecision {
    bool write_required;
    bool relay_on;
};

struct AquaelLightSnapshot {
    bool relay_on;
    bool known;
    bool transitioning;
    AquaelProfile profile;
    AquaelProfile desired_profile;
    AquaelTransitionStage stage;
};

/**
 * Non-blocking driver state for Aquael LEDDY TUBE Day&Night.
 *
 * Aquael documents that each OFF->ON toggle performed within five seconds
 * advances DAY -> DAYBREAK -> NIGHT -> DAY, while an OFF interval longer than
 * five seconds resets the next ON to DAY. We use a conservative 1000 ms cycle
 * pulse and a 6000 ms cold-start calibration interval. A normal OFF request
 * preserves the last known profile for at most the documented five-second
 * window, so a quick OFF->ON request advances exactly one mode. State is
 * deliberately unknown after construction, therefore every post-boot profile
 * request first calibrates through the >5 s OFF -> ON DAY baseline.
 */
class AquaelLightController {
public:
    static constexpr uint32_t kCycleOffMs = 1000U;
    static constexpr uint32_t kProfileToggleMaxOffMs = 5000U;
    static constexpr uint32_t kResetOffMs = 6000U;
    static constexpr uint32_t kOnSettleMs = 500U;
    static constexpr uint32_t kRetryDelayMs = 250U;

    AquaelLightController();

    void request(bool enabled, AquaelProfile profile);
    AquaelDriveDecision poll(uint32_t now_ms);
    void acknowledge_write(bool success, uint32_t now_ms);
    AquaelLightSnapshot snapshot(uint32_t now_ms);
    void reset_unknown(uint32_t now_ms);

    static bool parse_profile(const char *value, AquaelProfile *out);
    static const char *profile_code(AquaelProfile profile);
    static const char *profile_name(AquaelProfile profile);

private:
    bool desired_on_;
    AquaelProfile desired_profile_;
    bool relay_on_;
    bool known_;
    AquaelProfile active_profile_;
    AquaelTransitionStage stage_;
    uint32_t deadline_ms_;
    uint32_t off_started_ms_;
    bool off_timer_started_;
    bool reset_baseline_available_;
    bool pending_write_;
    bool pending_relay_on_;

    static bool deadline_reached(uint32_t now_ms, uint32_t deadline_ms);
    static AquaelProfile next_profile(AquaelProfile profile);
    AquaelDriveDecision request_write(bool relay_on);
};

} // namespace aquarium

#endif
