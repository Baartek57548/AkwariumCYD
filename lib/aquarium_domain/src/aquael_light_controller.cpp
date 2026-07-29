#include "aquael_light_controller.h"

#include <string.h>

namespace aquarium {

AquaelLightController::AquaelLightController()
    : desired_on_(false),
      desired_profile_(AquaelProfile::Day),
      relay_on_(false),
      known_(false),
      active_profile_(AquaelProfile::Day),
      stage_(AquaelTransitionStage::Off),
      deadline_ms_(0U),
      off_started_ms_(0U),
      off_timer_started_(false),
      reset_baseline_available_(false),
      pending_write_(false),
      pending_relay_on_(false) {
}

bool AquaelLightController::deadline_reached(uint32_t now_ms,
                                             uint32_t deadline_ms) {
    return static_cast<int32_t>(now_ms - deadline_ms) >= 0;
}

AquaelProfile AquaelLightController::next_profile(AquaelProfile profile) {
    return static_cast<AquaelProfile>(
        (static_cast<uint8_t>(profile) + 1U) % 3U);
}

void AquaelLightController::request(bool enabled, AquaelProfile profile) {
    desired_on_ = enabled;
    desired_profile_ =
        static_cast<uint8_t>(profile) <= static_cast<uint8_t>(AquaelProfile::Night)
            ? profile
            : AquaelProfile::Day;
}

AquaelDriveDecision AquaelLightController::request_write(bool relay_on) {
    pending_write_ = true;
    pending_relay_on_ = relay_on;
    return {true, relay_on};
}

AquaelDriveDecision AquaelLightController::poll(uint32_t now_ms) {
    if (pending_write_) {
        return {true, pending_relay_on_};
    }

    if (!desired_on_) {
        if (relay_on_) {
            return request_write(false);
        }
        if (!off_timer_started_) {
            off_started_ms_ = now_ms;
            off_timer_started_ = true;
        }
        if (known_ &&
            static_cast<uint32_t>(now_ms - off_started_ms_) >
                kProfileToggleMaxOffMs) {
            // Once Aquael's documented reset interval has elapsed, keep the
            // state unknown permanently until the next physical power-up.
            // This also prevents a 49-day millis wrap from reviving stale
            // profile knowledge after a very long OFF period.
            known_ = false;
            reset_baseline_available_ = true;
        }
        stage_ = AquaelTransitionStage::Off;
        return {false, false};
    }

    if (stage_ == AquaelTransitionStage::RetryWait &&
        !deadline_reached(now_ms, deadline_ms_)) {
        return {false, relay_on_};
    }
    if (stage_ == AquaelTransitionStage::RetryWait) {
        stage_ = known_ ? AquaelTransitionStage::Stable
                        : AquaelTransitionStage::ResetOff;
    }

    if (stage_ == AquaelTransitionStage::Off && !relay_on_ && known_) {
        const uint32_t off_elapsed_ms = now_ms - off_started_ms_;
        if (off_elapsed_ms <= kProfileToggleMaxOffMs) {
            // A user-visible quick OFF->ON is the same single-step operation
            // documented by Aquael. Keep a minimum one-second pulse so the
            // lamp power supply reliably observes the interruption.
            stage_ = AquaelTransitionStage::CycleOff;
            deadline_ms_ = off_started_ms_ + kCycleOffMs;
            if (!deadline_reached(now_ms, deadline_ms_)) {
                return {false, false};
            }
            return request_write(true);
        }
        // The hardware will reset to DAY on this power-up. Reuse the
        // calibration acknowledgement path so software state follows it.
        known_ = false;
        reset_baseline_available_ = true;
        stage_ = AquaelTransitionStage::ResetOff;
        return request_write(true);
    }

    if (!known_) {
        if (relay_on_) {
            stage_ = AquaelTransitionStage::ResetOff;
            return request_write(false);
        }
        if (reset_baseline_available_) {
            stage_ = AquaelTransitionStage::ResetOff;
            return request_write(true);
        }
        if (!off_timer_started_) {
            off_started_ms_ = now_ms;
            off_timer_started_ = true;
            stage_ = AquaelTransitionStage::ResetOff;
            return {false, false};
        }
        stage_ = AquaelTransitionStage::ResetOff;
        if (static_cast<uint32_t>(now_ms - off_started_ms_) < kResetOffMs) {
            return {false, false};
        }
        return request_write(true);
    }

    if (stage_ == AquaelTransitionStage::ResetOnSettle ||
        stage_ == AquaelTransitionStage::CycleOnSettle) {
        if (!deadline_reached(now_ms, deadline_ms_)) {
            return {false, relay_on_};
        }
        stage_ = AquaelTransitionStage::Stable;
    }

    if (stage_ == AquaelTransitionStage::CycleOff) {
        if (!deadline_reached(now_ms, deadline_ms_)) {
            return {false, false};
        }
        return request_write(true);
    }

    if (active_profile_ == desired_profile_) {
        stage_ = AquaelTransitionStage::Stable;
        return {false, relay_on_};
    }
    stage_ = AquaelTransitionStage::CycleOff;
    return request_write(false);
}

void AquaelLightController::acknowledge_write(bool success, uint32_t now_ms) {
    if (!pending_write_) {
        return;
    }
    const bool requested_on = pending_relay_on_;
    pending_write_ = false;
    if (!success) {
        known_ = false;
        stage_ = AquaelTransitionStage::RetryWait;
        deadline_ms_ = now_ms + kRetryDelayMs;
        return;
    }

    relay_on_ = requested_on;
    if (!requested_on) {
        off_started_ms_ = now_ms;
        off_timer_started_ = true;
        if (!desired_on_) {
            stage_ = AquaelTransitionStage::Off;
        } else if (known_) {
            stage_ = AquaelTransitionStage::CycleOff;
            deadline_ms_ = now_ms + kCycleOffMs;
        } else {
            stage_ = AquaelTransitionStage::ResetOff;
            deadline_ms_ = now_ms + kResetOffMs;
        }
        return;
    }

    off_started_ms_ = 0U;
    off_timer_started_ = false;
    reset_baseline_available_ = false;
    if (!known_) {
        active_profile_ = AquaelProfile::Day;
        known_ = true;
        stage_ = AquaelTransitionStage::ResetOnSettle;
    } else if (stage_ == AquaelTransitionStage::CycleOff) {
        active_profile_ = next_profile(active_profile_);
        stage_ = AquaelTransitionStage::CycleOnSettle;
    } else {
        // Any unclassified power restoration is unsafe to infer. Recalibrate
        // on the next poll instead of reporting a potentially wrong profile.
        known_ = false;
        stage_ = AquaelTransitionStage::ResetOff;
        return;
    }
    deadline_ms_ = now_ms + kOnSettleMs;
}

AquaelLightSnapshot AquaelLightController::snapshot(uint32_t now_ms) {
    // Expired timers are advanced by poll; snapshot remains side-effect free
    // except for reporting whether another poll is needed.
    const bool timer_wait =
        (stage_ == AquaelTransitionStage::ResetOff ||
         stage_ == AquaelTransitionStage::ResetOnSettle ||
         stage_ == AquaelTransitionStage::CycleOff ||
         stage_ == AquaelTransitionStage::CycleOnSettle ||
         stage_ == AquaelTransitionStage::RetryWait) &&
        !deadline_reached(now_ms, deadline_ms_);
    const bool reported_profile_known =
        known_ &&
        (relay_on_ || !off_timer_started_ ||
         static_cast<uint32_t>(now_ms - off_started_ms_) <=
             kProfileToggleMaxOffMs);
    return {
        relay_on_,
        reported_profile_known,
        pending_write_ || timer_wait ||
            (desired_on_ &&
             (!known_ || active_profile_ != desired_profile_ ||
              stage_ != AquaelTransitionStage::Stable)),
        active_profile_,
        desired_profile_,
        stage_
    };
}

void AquaelLightController::reset_unknown(uint32_t now_ms) {
    known_ = false;
    relay_on_ = false;
    stage_ = AquaelTransitionStage::Off;
    deadline_ms_ = 0U;
    off_started_ms_ = now_ms;
    off_timer_started_ = true;
    reset_baseline_available_ = false;
    pending_write_ = false;
}

bool AquaelLightController::parse_profile(const char *value,
                                          AquaelProfile *out) {
    if (value == nullptr || out == nullptr) {
        return false;
    }
    if (strcmp(value, "day") == 0) {
        *out = AquaelProfile::Day;
        return true;
    }
    if (strcmp(value, "daybreak") == 0 || strcmp(value, "dawn") == 0) {
        *out = AquaelProfile::Daybreak;
        return true;
    }
    if (strcmp(value, "night") == 0) {
        *out = AquaelProfile::Night;
        return true;
    }
    return false;
}

const char *AquaelLightController::profile_code(AquaelProfile profile) {
    switch (profile) {
    case AquaelProfile::Daybreak:
        return "daybreak";
    case AquaelProfile::Night:
        return "night";
    case AquaelProfile::Day:
    default:
        return "day";
    }
}

const char *AquaelLightController::profile_name(AquaelProfile profile) {
    switch (profile) {
    case AquaelProfile::Daybreak:
        return "DAYBREAK";
    case AquaelProfile::Night:
        return "NIGHT";
    case AquaelProfile::Day:
    default:
        return "DAY";
    }
}

} // namespace aquarium
