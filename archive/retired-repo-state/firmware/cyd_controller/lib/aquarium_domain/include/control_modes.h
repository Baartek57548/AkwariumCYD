#ifndef AQUARIUM_CONTROL_MODES_H
#define AQUARIUM_CONTROL_MODES_H

#include <stddef.h>
#include <stdint.h>

namespace aquarium {

enum class OutputTarget : uint8_t {
    Light1 = 0U,
    Light2 = 1U,
    Filter = 2U,
    Heater = 3U,
    Aeration = 4U,
    Co2 = 5U,
    WaterDosing = 6U,
    Count = 7U,
    Invalid = 255U
};

enum class ControlModeResult : uint8_t {
    Applied = 0U,
    InvalidTarget = 1U,
    InvalidDuration = 2U,
    ModeConflict = 3U
};

struct TimedOverrideSnapshot {
    bool active;
    bool state;
    uint32_t remaining_seconds;
};

struct OperatingModeSnapshot {
    bool feeding_active;
    uint32_t feeding_remaining_seconds;
    bool service_active;
    uint32_t service_remaining_seconds;
};

/**
 * Fixed-size, millis-wrap-safe controller for temporary manual control.
 *
 * Service mode has the highest priority and switches every managed output off.
 * Feeding mode switches the filter, CO2 and water dosing off. Timed overrides
 * are evaluated only when no safety mode owns the selected output.
 */
class ControlModeManager {
public:
    static constexpr uint32_t kOverrideMinSeconds = 30U;
    static constexpr uint32_t kOverrideMaxSeconds = 86400U;
    static constexpr uint32_t kFeedingMinSeconds = 60U;
    static constexpr uint32_t kFeedingMaxSeconds = 3600U;
    static constexpr uint32_t kServiceMinSeconds = 60U;
    static constexpr uint32_t kServiceMaxSeconds = 7200U;

    ControlModeManager();

    ControlModeResult set_override(OutputTarget target,
                                   bool state,
                                   uint32_t duration_seconds,
                                   uint32_t now_ms);
    ControlModeResult clear_override(OutputTarget target);
    void clear_all_overrides();

    ControlModeResult start_feeding(uint32_t duration_seconds, uint32_t now_ms);
    void stop_feeding();
    ControlModeResult start_service(uint32_t duration_seconds, uint32_t now_ms);
    void stop_service();
    void reset();

    bool resolve(OutputTarget target,
                 bool automatic_state,
                 uint32_t now_ms,
                 bool *out_is_managed = nullptr);

    TimedOverrideSnapshot override_snapshot(OutputTarget target, uint32_t now_ms);
    OperatingModeSnapshot mode_snapshot(uint32_t now_ms);

    static OutputTarget parse_target(const char *value);
    static const char *target_name(OutputTarget target);

private:
    struct TimedState {
        bool active;
        bool state;
        uint32_t deadline_ms;
    };

    TimedState overrides_[static_cast<size_t>(OutputTarget::Count)];
    TimedState feeding_;
    TimedState service_;

    static bool deadline_active(uint32_t deadline_ms, uint32_t now_ms);
    static uint32_t remaining_seconds(uint32_t deadline_ms, uint32_t now_ms);
    static bool valid_target(OutputTarget target);
    void expire(uint32_t now_ms);
};

} // namespace aquarium

#endif
