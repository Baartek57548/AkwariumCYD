#include "idempotency_ledger.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace aquarium {

IdempotencyLedger::IdempotencyLedger() {
    clear();
}

void IdempotencyLedger::clear() {
    memset(entries_, 0, sizeof(entries_));
}

bool IdempotencyLedger::valid_command_id(const char *command_id) {
    if (command_id == nullptr) {
        return false;
    }
    const size_t length = strlen(command_id);
    if (length < 8U || length >= kCommandIdBytes) {
        return false;
    }
    for (size_t index = 0U; index < length; ++index) {
        const unsigned char value = static_cast<unsigned char>(command_id[index]);
        if (!(isalnum(value) || value == '-' || value == '_' || value == '.')) {
            return false;
        }
    }
    return true;
}

uint32_t IdempotencyLedger::fingerprint(const char *text) {
    uint32_t hash = 2166136261UL;
    if (text == nullptr) {
        return hash;
    }
    for (size_t index = 0U; text[index] != '\0'; ++index) {
        hash ^= static_cast<uint8_t>(text[index]);
        hash *= 16777619UL;
    }
    return hash;
}

void IdempotencyLedger::expire(uint32_t now_ms) {
    for (size_t index = 0U; index < kCapacity; ++index) {
        if (entries_[index].occupied &&
            static_cast<uint32_t>(now_ms - entries_[index].stored_ms) >= kRetentionMs) {
            entries_[index].occupied = false;
        }
    }
}

CommandLookup IdempotencyLedger::lookup(const char *command_id,
                                        uint32_t fingerprint_value,
                                        uint32_t now_ms,
                                        CachedCommandResult *out_result) {
    if (!valid_command_id(command_id)) {
        return CommandLookup::InvalidId;
    }
    expire(now_ms);
    for (size_t index = 0U; index < kCapacity; ++index) {
        const Entry &entry = entries_[index];
        if (entry.occupied && strcmp(entry.command_id, command_id) == 0) {
            if (entry.fingerprint != fingerprint_value) {
                return CommandLookup::Conflict;
            }
            if (out_result != nullptr) {
                *out_result = entry.result;
            }
            return CommandLookup::Duplicate;
        }
    }
    return CommandLookup::NewCommand;
}

bool IdempotencyLedger::remember(const char *command_id,
                                 uint32_t fingerprint_value,
                                 const CachedCommandResult &result,
                                 uint32_t now_ms) {
    if (!valid_command_id(command_id)) {
        return false;
    }
    expire(now_ms);
    size_t destination = kCapacity;
    uint32_t oldest_age = 0U;
    for (size_t index = 0U; index < kCapacity; ++index) {
        if (!entries_[index].occupied) {
            destination = index;
            break;
        }
        if (strcmp(entries_[index].command_id, command_id) == 0) {
            destination = index;
            break;
        }
        const uint32_t age = now_ms - entries_[index].stored_ms;
        if (destination == kCapacity || age > oldest_age) {
            destination = index;
            oldest_age = age;
        }
    }
    if (destination >= kCapacity) {
        return false;
    }
    Entry &entry = entries_[destination];
    memset(&entry, 0, sizeof(entry));
    entry.occupied = true;
    snprintf(entry.command_id, sizeof(entry.command_id), "%s", command_id);
    entry.fingerprint = fingerprint_value;
    entry.stored_ms = now_ms;
    entry.result = result;
    entry.result.code[sizeof(entry.result.code) - 1U] = '\0';
    entry.result.message[sizeof(entry.result.message) - 1U] = '\0';
    return true;
}

} // namespace aquarium
