#include "aquahub_automation.h"

#include <math.h>
#include <string.h>

namespace aquahub {
namespace {

bool valid_value(const StateValue &value) {
    if (!value.valid || value.type == ValueType::None) {
        return false;
    }
    if (value.type == ValueType::Number) {
        return isfinite(value.number_value);
    }
    if (value.type == ValueType::Text) {
        return strnlen(value.text_value, sizeof(value.text_value)) <
               sizeof(value.text_value);
    }
    return true;
}

bool valid_comparison(Comparison comparison, const StateValue &expected) {
    if (comparison == Comparison::Changed) {
        return true;
    }
    if (!valid_value(expected)) {
        return false;
    }
    if ((comparison == Comparison::Above || comparison == Comparison::Below) &&
        expected.type != ValueType::Number) {
        return false;
    }
    return true;
}

bool compare(const StateValue &current,
             const StateValue &previous,
             Comparison comparison,
             const StateValue &expected) {
    if (comparison == Comparison::Changed) {
        return !state_values_equal(current, previous);
    }
    if (!current.valid || current.type != expected.type) {
        return false;
    }
    switch (comparison) {
    case Comparison::Equals:
        return state_values_equal(current, expected);
    case Comparison::Above:
        return current.type == ValueType::Number &&
               current.number_value > expected.number_value;
    case Comparison::Below:
        return current.type == ValueType::Number &&
               current.number_value < expected.number_value;
    case Comparison::Changed:
        return !state_values_equal(current, previous);
    }
    return false;
}

bool valid_rule(const AutomationRule &rule) {
    if (!valid_identifier(rule.id, sizeof(rule.id)) ||
        rule.name[0] == '\0' ||
        strnlen(rule.name, sizeof(rule.name)) >= sizeof(rule.name) ||
        !valid_identifier(rule.trigger_entity,
                          sizeof(rule.trigger_entity)) ||
        !valid_identifier(rule.action_entity,
                          sizeof(rule.action_entity)) ||
        !valid_value(rule.action_value) ||
        !valid_comparison(rule.trigger_comparison, rule.trigger_value)) {
        return false;
    }
    if (rule.condition_enabled &&
        (!valid_identifier(rule.condition_entity,
                           sizeof(rule.condition_entity)) ||
         !valid_comparison(rule.condition_comparison,
                           rule.condition_value))) {
        return false;
    }
    return true;
}

} // namespace

AutomationEngine::AutomationEngine() {
    clear();
}

void AutomationEngine::clear() {
    memset(rules_, 0, sizeof(rules_));
    count_ = 0U;
}

AutomationStatus AutomationEngine::upsert(const AutomationRule &rule) {
    if (!valid_rule(rule)) {
        return AutomationStatus::InvalidRule;
    }
    RuleRecord *free_record = nullptr;
    for (RuleRecord &record : rules_) {
        if (record.occupied &&
            strncmp(record.rule.id, rule.id, kAutomationIdBytes) == 0) {
            const uint64_t last_fired = record.last_fired_ms;
            record.rule = rule;
            record.last_fired_ms = last_fired;
            return AutomationStatus::Ok;
        }
        if (!record.occupied && free_record == nullptr) {
            free_record = &record;
        }
    }
    if (free_record == nullptr) {
        return AutomationStatus::CapacityReached;
    }
    memset(free_record, 0, sizeof(*free_record));
    free_record->occupied = true;
    free_record->rule = rule;
    ++count_;
    return AutomationStatus::Ok;
}

AutomationStatus AutomationEngine::remove(const char *id) {
    if (id == nullptr) {
        return AutomationStatus::NotFound;
    }
    for (RuleRecord &record : rules_) {
        if (record.occupied &&
            strncmp(record.rule.id, id, kAutomationIdBytes) == 0) {
            memset(&record, 0, sizeof(record));
            --count_;
            return AutomationStatus::Ok;
        }
    }
    return AutomationStatus::NotFound;
}

size_t AutomationEngine::evaluate(
    const Registry &registry,
    const char *changed_entity,
    const StateValue &previous_value,
    uint64_t now_ms,
    PendingAction *output,
    size_t output_capacity) {
    if (changed_entity == nullptr || output == nullptr ||
        output_capacity == 0U) {
        return 0U;
    }
    const EntityRecord *trigger = registry.entity(changed_entity);
    if (trigger == nullptr) {
        return 0U;
    }

    size_t emitted = 0U;
    for (RuleRecord &record : rules_) {
        const AutomationRule &rule = record.rule;
        if (!record.occupied || !rule.enabled ||
            strncmp(rule.trigger_entity,
                    changed_entity,
                    kEntityIdBytes) != 0 ||
            (record.last_fired_ms != 0U &&
             now_ms - record.last_fired_ms < rule.cooldown_ms) ||
            !compare(trigger->state,
                     previous_value,
                     rule.trigger_comparison,
                     rule.trigger_value)) {
            continue;
        }

        if (rule.condition_enabled) {
            const EntityRecord *condition =
                registry.entity(rule.condition_entity);
            const StateValue empty = {};
            if (condition == nullptr ||
                !compare(condition->state,
                         empty,
                         rule.condition_comparison,
                         rule.condition_value)) {
                continue;
            }
        }

        PendingAction &action = output[emitted];
        memset(&action, 0, sizeof(action));
        memcpy(action.automation_id,
               rule.id,
               strnlen(rule.id, sizeof(action.automation_id) - 1U));
        memcpy(action.entity_id,
               rule.action_entity,
               strnlen(rule.action_entity, sizeof(action.entity_id) - 1U));
        action.value = rule.action_value;
        record.last_fired_ms = now_ms;
        ++emitted;
        if (emitted >= output_capacity ||
            emitted >= kMaximumAutomationActionsPerEvaluation) {
            break;
        }
    }
    return emitted;
}

const AutomationRule *AutomationEngine::rule_at(size_t index) const {
    size_t occupied_index = 0U;
    for (const RuleRecord &record : rules_) {
        if (!record.occupied) {
            continue;
        }
        if (occupied_index == index) {
            return &record.rule;
        }
        ++occupied_index;
    }
    return nullptr;
}

size_t AutomationEngine::count() const {
    return count_;
}

} // namespace aquahub
