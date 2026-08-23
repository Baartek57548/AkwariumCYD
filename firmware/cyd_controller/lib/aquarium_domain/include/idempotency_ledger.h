#ifndef AQUARIUM_IDEMPOTENCY_LEDGER_H
#define AQUARIUM_IDEMPOTENCY_LEDGER_H

#include <stddef.h>
#include <stdint.h>

namespace aquarium {

enum class CommandLookup : uint8_t {
    NewCommand = 0U,
    Duplicate = 1U,
    Conflict = 2U,
    InvalidId = 3U
};

struct CachedCommandResult {
    bool success;
    char code[40];
    char message[128];
};

/**
 * Bounded idempotency cache. It keeps no heap-owned strings and evicts the
 * oldest entry, which makes memory use deterministic on ESP32.
 */
class IdempotencyLedger {
public:
    static constexpr size_t kCapacity = 8U;
    static constexpr size_t kCommandIdBytes = 49U;
    static constexpr uint32_t kRetentionMs = 10U * 60U * 1000U;

    IdempotencyLedger();

    CommandLookup lookup(const char *command_id,
                         uint32_t fingerprint,
                         uint32_t now_ms,
                         CachedCommandResult *out_result);
    bool remember(const char *command_id,
                  uint32_t fingerprint,
                  const CachedCommandResult &result,
                  uint32_t now_ms);
    void clear();

    static bool valid_command_id(const char *command_id);
    static uint32_t fingerprint(const char *text);

private:
    struct Entry {
        bool occupied;
        char command_id[kCommandIdBytes];
        uint32_t fingerprint;
        uint32_t stored_ms;
        CachedCommandResult result;
    };

    Entry entries_[kCapacity];

    void expire(uint32_t now_ms);
};

} // namespace aquarium

#endif
