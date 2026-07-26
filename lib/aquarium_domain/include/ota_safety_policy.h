#ifndef AQUARIUM_OTA_SAFETY_POLICY_H
#define AQUARIUM_OTA_SAFETY_POLICY_H

#include <stdint.h>

namespace aquarium {

enum class OtaPreflightResult : uint8_t {
    Ready = 0U,
    ImageTooSmall = 1U,
    ImageTooLarge = 2U,
    InsufficientHeap = 3U,
    UnsafeActivity = 4U,
    BackupFailed = 5U,
    RollbackUnavailable = 6U
};

struct OtaPreflightInput {
    uint32_t image_bytes;
    uint32_t update_partition_bytes;
    uint32_t free_heap_bytes;
    bool feeder_active;
    bool configuration_backed_up;
    bool rollback_available;
};

OtaPreflightResult evaluate_ota_preflight(const OtaPreflightInput &input);
const char *ota_preflight_code(OtaPreflightResult result);

enum class BootValidationDecision : uint8_t {
    Wait = 0U,
    MarkValid = 1U,
    Rollback = 2U
};

class BootValidationPolicy {
public:
    static constexpr uint32_t kValidationWindowMs = 30U * 1000U;
    static constexpr uint32_t kMinimumHealthyHeapBytes = 12000U;

    BootValidationPolicy();
    void begin(uint32_t now_ms, bool pending_verify);
    BootValidationDecision evaluate(uint32_t now_ms,
                                    bool runtime_ready,
                                    uint32_t free_heap_bytes);

private:
    bool pending_;
    uint32_t started_ms_;
};

} // namespace aquarium

#endif
