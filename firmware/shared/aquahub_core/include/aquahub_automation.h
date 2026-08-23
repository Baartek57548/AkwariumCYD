#ifndef AQUAHUB_AUTOMATION_H
#define AQUAHUB_AUTOMATION_H

#include <stddef.h>
#include <stdint.h>

#include "aquahub_registry.h"

namespace aquahub {

constexpr size_t kMaximumAutomations = 32U;
constexpr size_t kMaximumAutomationActionsPerEvaluation = 8U;
constexpr size_t kAutomationIdBytes = 33U;

enum class Comparison : uint8_t {
    Changed = 0U,
    Equals,
    Above,
    Below
};

enum class AutomationStatus : uint8_t {
    Ok = 0U,
    InvalidRule,
    CapacityReached,
    NotFound,
    PersistenceFailed
};

struct AutomationRule {
    char id[kAutomationIdBytes];
    char name[kNameBytes];
    bool enabled;
    char trigger_entity[kEntityIdBytes];
    Comparison trigger_comparison;
    StateValue trigger_value;
    bool condition_enabled;
    char condition_entity[kEntityIdBytes];
    Comparison condition_comparison;
    StateValue condition_value;
    char action_entity[kEntityIdBytes];
    StateValue action_value;
    uint32_t cooldown_ms;
};

struct PendingAction {
    char automation_id[kAutomationIdBytes];
    char entity_id[kEntityIdBytes];
    StateValue value;
};

class AutomationEngine {
public:
    AutomationEngine();

    void clear();
    AutomationStatus upsert(const AutomationRule &rule);
    AutomationStatus remove(const char *id);
    size_t evaluate(const Registry &registry,
                    const char *changed_entity,
                    const StateValue &previous_value,
                    uint64_t now_ms,
                    PendingAction *output,
                    size_t output_capacity);
    const AutomationRule *rule_at(size_t index) const;
    size_t count() const;

private:
    struct RuleRecord {
        bool occupied;
        AutomationRule rule;
        uint64_t last_fired_ms;
    };

    RuleRecord rules_[kMaximumAutomations];
    size_t count_;
};

} // namespace aquahub

#endif
