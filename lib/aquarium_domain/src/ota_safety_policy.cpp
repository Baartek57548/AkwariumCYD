#include "ota_safety_policy.h"

namespace aquarium {

namespace {

constexpr uint32_t kMinimumImageBytes = 64U * 1024U;
constexpr uint32_t kMinimumHeapBytes = 32000U;

} // namespace

OtaPreflightResult evaluate_ota_preflight(const OtaPreflightInput &input) {
    if (!input.rollback_available || input.update_partition_bytes == 0U) {
        return OtaPreflightResult::RollbackUnavailable;
    }
    if (input.image_bytes != 0U && input.image_bytes < kMinimumImageBytes) {
        return OtaPreflightResult::ImageTooSmall;
    }
    if (input.image_bytes > input.update_partition_bytes) {
        return OtaPreflightResult::ImageTooLarge;
    }
    if (input.free_heap_bytes < kMinimumHeapBytes) {
        return OtaPreflightResult::InsufficientHeap;
    }
    if (input.feeder_active) {
        return OtaPreflightResult::UnsafeActivity;
    }
    if (!input.configuration_backed_up) {
        return OtaPreflightResult::BackupFailed;
    }
    return OtaPreflightResult::Ready;
}

const char *ota_preflight_code(OtaPreflightResult result) {
    switch (result) {
    case OtaPreflightResult::Ready:
        return "ota_ready";
    case OtaPreflightResult::ImageTooSmall:
        return "image_too_small";
    case OtaPreflightResult::ImageTooLarge:
        return "image_too_large";
    case OtaPreflightResult::InsufficientHeap:
        return "insufficient_heap";
    case OtaPreflightResult::UnsafeActivity:
        return "unsafe_activity";
    case OtaPreflightResult::BackupFailed:
        return "config_backup_failed";
    case OtaPreflightResult::RollbackUnavailable:
    default:
        return "rollback_unavailable";
    }
}

BootValidationPolicy::BootValidationPolicy()
    : pending_(false), started_ms_(0U) {
}

void BootValidationPolicy::begin(uint32_t now_ms, bool pending_verify) {
    pending_ = pending_verify;
    started_ms_ = now_ms;
}

BootValidationDecision BootValidationPolicy::evaluate(uint32_t now_ms,
                                                      bool runtime_ready,
                                                      uint32_t free_heap_bytes) {
    if (!pending_) {
        return BootValidationDecision::Wait;
    }
    if (free_heap_bytes < kMinimumHealthyHeapBytes) {
        pending_ = false;
        return BootValidationDecision::Rollback;
    }
    if (!runtime_ready) {
        if (static_cast<uint32_t>(now_ms - started_ms_) >= kValidationWindowMs) {
            pending_ = false;
            return BootValidationDecision::Rollback;
        }
        return BootValidationDecision::Wait;
    }
    if (static_cast<uint32_t>(now_ms - started_ms_) >= kValidationWindowMs) {
        pending_ = false;
        return BootValidationDecision::MarkValid;
    }
    return BootValidationDecision::Wait;
}

} // namespace aquarium
