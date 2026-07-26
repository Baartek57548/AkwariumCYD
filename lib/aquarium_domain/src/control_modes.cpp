#include "control_modes.h"

#include <string.h>

namespace aquarium {

namespace {

uint32_t seconds_to_deadline(uint32_t now_ms, uint32_t seconds) {
    return now_ms + seconds * 1000U;
}

} // namespace

ControlModeManager::ControlModeManager() {
    reset();
}

bool ControlModeManager::deadline_active(uint32_t deadline_ms, uint32_t now_ms) {
    return static_cast<int32_t>(deadline_ms - now_ms) > 0;
}

uint32_t ControlModeManager::remaining_seconds(uint32_t deadline_ms, uint32_t now_ms) {
    if (!deadline_active(deadline_ms, now_ms)) {
        return 0U;
    }
    const uint32_t remaining_ms = deadline_ms - now_ms;
    return (remaining_ms + 999U) / 1000U;
}

bool ControlModeManager::valid_target(OutputTarget target) {
    return static_cast<uint8_t>(target) < static_cast<uint8_t>(OutputTarget::Count);
}

void ControlModeManager::expire(uint32_t now_ms) {
    for (size_t index = 0U; index < static_cast<size_t>(OutputTarget::Count); ++index) {
        if (overrides_[index].active &&
            !deadline_active(overrides_[index].deadline_ms, now_ms)) {
            overrides_[index].active = false;
        }
    }
    if (feeding_.active && !deadline_active(feeding_.deadline_ms, now_ms)) {
        feeding_.active = false;
    }
    if (service_.active && !deadline_active(service_.deadline_ms, now_ms)) {
        service_.active = false;
    }
}

ControlModeResult ControlModeManager::set_override(OutputTarget target,
                                                   bool state,
                                                   uint32_t duration_seconds,
                                                   uint32_t now_ms) {
    expire(now_ms);
    if (!valid_target(target)) {
        return ControlModeResult::InvalidTarget;
    }
    if (duration_seconds < kOverrideMinSeconds ||
        duration_seconds > kOverrideMaxSeconds) {
        return ControlModeResult::InvalidDuration;
    }
    if (service_.active) {
        return ControlModeResult::ModeConflict;
    }
    TimedState &entry = overrides_[static_cast<size_t>(target)];
    entry.active = true;
    entry.state = state;
    entry.deadline_ms = seconds_to_deadline(now_ms, duration_seconds);
    return ControlModeResult::Applied;
}

ControlModeResult ControlModeManager::clear_override(OutputTarget target) {
    if (!valid_target(target)) {
        return ControlModeResult::InvalidTarget;
    }
    overrides_[static_cast<size_t>(target)].active = false;
    return ControlModeResult::Applied;
}

void ControlModeManager::clear_all_overrides() {
    for (size_t index = 0U; index < static_cast<size_t>(OutputTarget::Count); ++index) {
        overrides_[index].active = false;
    }
}

ControlModeResult ControlModeManager::start_feeding(uint32_t duration_seconds,
                                                   uint32_t now_ms) {
    expire(now_ms);
    if (duration_seconds < kFeedingMinSeconds ||
        duration_seconds > kFeedingMaxSeconds) {
        return ControlModeResult::InvalidDuration;
    }
    if (service_.active) {
        return ControlModeResult::ModeConflict;
    }
    feeding_.active = true;
    feeding_.state = true;
    feeding_.deadline_ms = seconds_to_deadline(now_ms, duration_seconds);
    return ControlModeResult::Applied;
}

void ControlModeManager::stop_feeding() {
    feeding_.active = false;
}

ControlModeResult ControlModeManager::start_service(uint32_t duration_seconds,
                                                   uint32_t now_ms) {
    expire(now_ms);
    if (duration_seconds < kServiceMinSeconds ||
        duration_seconds > kServiceMaxSeconds) {
        return ControlModeResult::InvalidDuration;
    }
    feeding_.active = false;
    clear_all_overrides();
    service_.active = true;
    service_.state = true;
    service_.deadline_ms = seconds_to_deadline(now_ms, duration_seconds);
    return ControlModeResult::Applied;
}

void ControlModeManager::stop_service() {
    service_.active = false;
}

void ControlModeManager::reset() {
    for (size_t index = 0U; index < static_cast<size_t>(OutputTarget::Count); ++index) {
        overrides_[index] = {false, false, 0U};
    }
    feeding_ = {false, false, 0U};
    service_ = {false, false, 0U};
}

bool ControlModeManager::resolve(OutputTarget target,
                                 bool automatic_state,
                                 uint32_t now_ms,
                                 bool *out_is_managed) {
    expire(now_ms);
    if (out_is_managed != nullptr) {
        *out_is_managed = false;
    }
    if (!valid_target(target)) {
        return automatic_state;
    }
    if (service_.active) {
        if (out_is_managed != nullptr) {
            *out_is_managed = true;
        }
        return false;
    }
    if (feeding_.active &&
        (target == OutputTarget::Filter ||
         target == OutputTarget::Co2 ||
         target == OutputTarget::WaterDosing)) {
        if (out_is_managed != nullptr) {
            *out_is_managed = true;
        }
        return false;
    }
    const TimedState &entry = overrides_[static_cast<size_t>(target)];
    if (entry.active) {
        if (out_is_managed != nullptr) {
            *out_is_managed = true;
        }
        return entry.state;
    }
    return automatic_state;
}

TimedOverrideSnapshot ControlModeManager::override_snapshot(OutputTarget target,
                                                            uint32_t now_ms) {
    expire(now_ms);
    if (!valid_target(target)) {
        return {false, false, 0U};
    }
    const TimedState &entry = overrides_[static_cast<size_t>(target)];
    return {
        entry.active,
        entry.state,
        entry.active ? remaining_seconds(entry.deadline_ms, now_ms) : 0U
    };
}

OperatingModeSnapshot ControlModeManager::mode_snapshot(uint32_t now_ms) {
    expire(now_ms);
    return {
        feeding_.active,
        feeding_.active ? remaining_seconds(feeding_.deadline_ms, now_ms) : 0U,
        service_.active,
        service_.active ? remaining_seconds(service_.deadline_ms, now_ms) : 0U
    };
}

OutputTarget ControlModeManager::parse_target(const char *value) {
    if (value == nullptr) {
        return OutputTarget::Invalid;
    }
    if (strcmp(value, "light1") == 0 || strcmp(value, "light") == 0) {
        return OutputTarget::Light1;
    }
    if (strcmp(value, "light2") == 0 || strcmp(value, "plant") == 0 ||
        strcmp(value, "plant_light") == 0) {
        return OutputTarget::Light2;
    }
    if (strcmp(value, "filter") == 0 || strcmp(value, "pump") == 0) {
        return OutputTarget::Filter;
    }
    if (strcmp(value, "heater") == 0) {
        return OutputTarget::Heater;
    }
    if (strcmp(value, "aeration") == 0 || strcmp(value, "air") == 0) {
        return OutputTarget::Aeration;
    }
    if (strcmp(value, "co2") == 0) {
        return OutputTarget::Co2;
    }
    if (strcmp(value, "water_dosing") == 0 || strcmp(value, "ato") == 0) {
        return OutputTarget::WaterDosing;
    }
    return OutputTarget::Invalid;
}

const char *ControlModeManager::target_name(OutputTarget target) {
    switch (target) {
    case OutputTarget::Light1:
        return "light1";
    case OutputTarget::Light2:
        return "light2";
    case OutputTarget::Filter:
        return "filter";
    case OutputTarget::Heater:
        return "heater";
    case OutputTarget::Aeration:
        return "aeration";
    case OutputTarget::Co2:
        return "co2";
    case OutputTarget::WaterDosing:
        return "water_dosing";
    default:
        return "invalid";
    }
}

} // namespace aquarium
