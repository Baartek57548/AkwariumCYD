#include "aquahub_registry.h"

#include <ctype.h>
#include <math.h>
#include <string.h>

namespace aquahub {
namespace {

template <size_t Capacity>
bool terminated(const char (&value)[Capacity]) {
    return strnlen(value, Capacity) < Capacity;
}

template <size_t Capacity>
void copy_text(char (&destination)[Capacity], const char *source) {
    if (source == nullptr) {
        destination[0] = '\0';
        return;
    }
    const size_t length = strnlen(source, Capacity - 1U);
    memcpy(destination, source, length);
    destination[length] = '\0';
}

bool valid_device_descriptor(const DeviceDescriptor &descriptor) {
    return terminated(descriptor.node_id) &&
           terminated(descriptor.name) &&
           terminated(descriptor.model) &&
           terminated(descriptor.manufacturer) &&
           terminated(descriptor.firmware_version) &&
           terminated(descriptor.area) &&
           valid_identifier(descriptor.node_id, sizeof(descriptor.node_id)) &&
           descriptor.name[0] != '\0';
}

bool valid_options(const DiscoveryDescriptor &descriptor) {
    if (descriptor.option_count > kMaximumOptions) {
        return false;
    }
    for (size_t index = 0U; index < descriptor.option_count; ++index) {
        if (!terminated(descriptor.options[index]) ||
            descriptor.options[index][0] == '\0') {
            return false;
        }
        for (size_t other = index + 1U;
             other < descriptor.option_count;
             ++other) {
            if (strncmp(descriptor.options[index],
                        descriptor.options[other],
                        kOptionBytes) == 0) {
                return false;
            }
        }
    }
    return descriptor.kind != EntityKind::Select ||
           descriptor.option_count > 0U;
}

bool valid_state_for_descriptor(const StateValue &value,
                                const DiscoveryDescriptor &descriptor) {
    if (descriptor.kind == EntityKind::Button) {
        return !value.valid && value.type == ValueType::None;
    }
    if (!value.valid) {
        return value.type == ValueType::None ||
               value.type == descriptor.value_type;
    }
    if (value.type != descriptor.value_type) {
        return false;
    }
    if (value.type == ValueType::Number) {
        return isfinite(value.number_value) &&
               value.number_value >= descriptor.minimum &&
               value.number_value <= descriptor.maximum;
    }
    if (value.type == ValueType::Text) {
        if (strnlen(value.text_value, sizeof(value.text_value)) >=
            sizeof(value.text_value)) {
            return false;
        }
        if (descriptor.kind == EntityKind::Select) {
            for (size_t index = 0U;
                 index < descriptor.option_count;
                 ++index) {
                if (strncmp(value.text_value,
                            descriptor.options[index],
                            kOptionBytes) == 0) {
                    return true;
                }
            }
            return false;
        }
    }
    return true;
}

} // namespace

bool valid_identifier(const char *value, size_t capacity) {
    if (value == nullptr || capacity < 2U) {
        return false;
    }
    const size_t length = strnlen(value, capacity);
    if (length == 0U || length >= capacity ||
        value[0] == '-' || value[0] == '_' || value[length - 1U] == '-' ||
        value[length - 1U] == '_') {
        return false;
    }
    for (size_t index = 0U; index < length; ++index) {
        const unsigned char current =
            static_cast<unsigned char>(value[index]);
        if (!(islower(current) || isdigit(current) || current == '_' ||
              current == '-')) {
            return false;
        }
    }
    return true;
}

bool valid_topic(const char *value, bool allow_empty) {
    if (value == nullptr) {
        return false;
    }
    const size_t length = strnlen(value, kTopicBytes);
    if (length >= kTopicBytes) {
        return false;
    }
    if (length == 0U) {
        return allow_empty;
    }
    if (value[0] == '/' || value[length - 1U] == '/') {
        return false;
    }
    for (size_t index = 0U; index < length; ++index) {
        const unsigned char current =
            static_cast<unsigned char>(value[index]);
        if (current < 0x20U || current == 0x7FU ||
            current == '+' || current == '#') {
            return false;
        }
    }
    return true;
}

bool state_values_equal(const StateValue &left, const StateValue &right) {
    if (left.valid != right.valid || left.type != right.type) {
        return false;
    }
    if (!left.valid) {
        return true;
    }
    switch (left.type) {
    case ValueType::Boolean:
        return left.boolean_value == right.boolean_value;
    case ValueType::Number:
        return left.number_value == right.number_value;
    case ValueType::Text:
        return strncmp(left.text_value,
                       right.text_value,
                       kTextValueBytes) == 0;
    case ValueType::None:
        return true;
    }
    return false;
}

Registry::Registry() {
    clear();
}

void Registry::clear() {
    memset(devices_, 0, sizeof(devices_));
    memset(entities_, 0, sizeof(entities_));
    device_count_ = 0U;
    entity_count_ = 0U;
}

DeviceRecord *Registry::allocate_device(
    const DeviceDescriptor &descriptor) {
    if (device_count_ >= kMaximumDevices) {
        return nullptr;
    }
    for (DeviceRecord &record : devices_) {
        if (!record.occupied) {
            memset(&record, 0, sizeof(record));
            record.occupied = true;
            record.descriptor = descriptor;
            ++device_count_;
            return &record;
        }
    }
    return nullptr;
}

RegistryStatus Registry::upsert_discovery(
    const DiscoveryDescriptor &descriptor) {
    if (descriptor.schema_version != kDiscoverySchemaVersion) {
        return RegistryStatus::InvalidSchema;
    }
    if (!valid_device_descriptor(descriptor.device) ||
        !terminated(descriptor.unique_id) ||
        !valid_identifier(descriptor.unique_id, sizeof(descriptor.unique_id))) {
        return RegistryStatus::InvalidIdentifier;
    }
    if (!terminated(descriptor.name) || descriptor.name[0] == '\0' ||
        !terminated(descriptor.value_key) ||
        (descriptor.kind != EntityKind::Button &&
         descriptor.value_key[0] == '\0') ||
        !terminated(descriptor.unit) ||
        !valid_options(descriptor)) {
        return RegistryStatus::InvalidDescriptor;
    }
    if (!terminated(descriptor.discovery_topic) ||
        !terminated(descriptor.state_topic) ||
        !terminated(descriptor.command_topic) ||
        !terminated(descriptor.availability_topic) ||
        !valid_topic(descriptor.discovery_topic, false) ||
        !valid_topic(descriptor.state_topic,
                     descriptor.kind == EntityKind::Button) ||
        !valid_topic(descriptor.command_topic, !descriptor.writable) ||
        !valid_topic(descriptor.availability_topic, false)) {
        return RegistryStatus::InvalidTopic;
    }
    if (descriptor.writable && descriptor.command_topic[0] == '\0') {
        return RegistryStatus::InvalidDescriptor;
    }
    if (descriptor.kind == EntityKind::Button &&
        (!descriptor.writable || descriptor.value_type != ValueType::None)) {
        return RegistryStatus::InvalidDescriptor;
    }
    if (descriptor.value_type == ValueType::Number &&
        (!isfinite(descriptor.minimum) || !isfinite(descriptor.maximum) ||
         !isfinite(descriptor.step) || descriptor.minimum > descriptor.maximum ||
         descriptor.step <= 0.0)) {
        return RegistryStatus::InvalidDescriptor;
    }

    EntityRecord *existing = entity(descriptor.unique_id);
    if (existing != nullptr) {
        if (strncmp(existing->descriptor.device.node_id,
                    descriptor.device.node_id,
                    kNodeIdBytes) != 0) {
            return RegistryStatus::IdentityConflict;
        }
        DeviceRecord *existing_device = device(descriptor.device.node_id);
        if (existing_device == nullptr) {
            return RegistryStatus::DeviceNotFound;
        }
        existing_device->descriptor = descriptor.device;
        const StateValue previous_state = existing->state;
        const uint64_t changed_at = existing->state_changed_ms;
        const uint64_t updated_at = existing->state_updated_ms;
        existing->descriptor = descriptor;
        existing->state = previous_state;
        existing->state_changed_ms = changed_at;
        existing->state_updated_ms = updated_at;
        return RegistryStatus::Ok;
    }

    DeviceRecord *device_record = device(descriptor.device.node_id);
    const bool allocated_device = device_record == nullptr;
    if (allocated_device) {
        device_record = allocate_device(descriptor.device);
        if (device_record == nullptr) {
            return RegistryStatus::DeviceCapacityReached;
        }
    } else {
        device_record->descriptor = descriptor.device;
    }

    if (entity_count_ >= kMaximumEntities) {
        if (allocated_device) {
            memset(device_record, 0, sizeof(*device_record));
            --device_count_;
        }
        return RegistryStatus::EntityCapacityReached;
    }
    for (EntityRecord &record : entities_) {
        if (!record.occupied) {
            memset(&record, 0, sizeof(record));
            record.occupied = true;
            record.descriptor = descriptor;
            record.state.type = descriptor.value_type;
            record.state.valid = false;
            ++entity_count_;
            return RegistryStatus::Ok;
        }
    }
    if (allocated_device) {
        memset(device_record, 0, sizeof(*device_record));
        --device_count_;
    }
    return RegistryStatus::EntityCapacityReached;
}

RegistryStatus Registry::remove_entity(const char *unique_id) {
    EntityRecord *record = entity(unique_id);
    if (record == nullptr) {
        return RegistryStatus::EntityNotFound;
    }
    char node_id[kNodeIdBytes] = {};
    copy_text(node_id, record->descriptor.device.node_id);
    memset(record, 0, sizeof(*record));
    --entity_count_;

    bool node_used = false;
    for (const EntityRecord &candidate : entities_) {
        if (candidate.occupied &&
            strncmp(candidate.descriptor.device.node_id,
                    node_id,
                    kNodeIdBytes) == 0) {
            node_used = true;
            break;
        }
    }
    if (!node_used) {
        DeviceRecord *orphan = device(node_id);
        if (orphan != nullptr) {
            memset(orphan, 0, sizeof(*orphan));
            --device_count_;
        }
    }
    return RegistryStatus::Ok;
}

RegistryStatus Registry::accept_message(const char *node_id,
                                        uint32_t boot_id,
                                        uint32_t sequence,
                                        uint64_t received_at_ms) {
    DeviceRecord *record = device(node_id);
    if (record == nullptr) {
        return RegistryStatus::DeviceNotFound;
    }
    if (boot_id == 0U || sequence == 0U) {
        return RegistryStatus::InvalidValue;
    }
    if (record->boot_id == boot_id && record->newest_sequence != 0U &&
        static_cast<int32_t>(sequence - record->newest_sequence) <= 0) {
        return RegistryStatus::ReplayRejected;
    }
    record->boot_id = boot_id;
    record->newest_sequence = sequence;
    record->last_seen_ms = received_at_ms;
    record->online = true;
    return RegistryStatus::Ok;
}

RegistryStatus Registry::update_availability(const char *node_id,
                                             bool online,
                                             uint64_t received_at_ms) {
    DeviceRecord *record = device(node_id);
    if (record == nullptr) {
        return RegistryStatus::DeviceNotFound;
    }
    record->online = online;
    record->last_seen_ms = received_at_ms;
    return RegistryStatus::Ok;
}

RegistryStatus Registry::update_state(const char *unique_id,
                                      const StateValue &value,
                                      uint64_t received_at_ms,
                                      bool *changed) {
    EntityRecord *record = entity(unique_id);
    if (record == nullptr) {
        return RegistryStatus::EntityNotFound;
    }
    if (!valid_state_for_descriptor(value, record->descriptor)) {
        return RegistryStatus::InvalidValue;
    }
    const bool state_changed = !state_values_equal(record->state, value);
    record->state = value;
    record->state_updated_ms = received_at_ms;
    if (state_changed) {
        record->state_changed_ms = received_at_ms;
    }
    if (changed != nullptr) {
        *changed = state_changed;
    }
    return RegistryStatus::Ok;
}

const DeviceRecord *Registry::device(const char *node_id) const {
    if (node_id == nullptr) {
        return nullptr;
    }
    for (const DeviceRecord &record : devices_) {
        if (record.occupied &&
            strncmp(record.descriptor.node_id, node_id, kNodeIdBytes) == 0) {
            return &record;
        }
    }
    return nullptr;
}

DeviceRecord *Registry::device(const char *node_id) {
    return const_cast<DeviceRecord *>(
        static_cast<const Registry *>(this)->device(node_id));
}

const EntityRecord *Registry::entity(const char *unique_id) const {
    if (unique_id == nullptr) {
        return nullptr;
    }
    for (const EntityRecord &record : entities_) {
        if (record.occupied &&
            strncmp(record.descriptor.unique_id,
                    unique_id,
                    kEntityIdBytes) == 0) {
            return &record;
        }
    }
    return nullptr;
}

EntityRecord *Registry::entity(const char *unique_id) {
    return const_cast<EntityRecord *>(
        static_cast<const Registry *>(this)->entity(unique_id));
}

const DeviceRecord *Registry::device_at(size_t index) const {
    size_t occupied_index = 0U;
    for (const DeviceRecord &record : devices_) {
        if (!record.occupied) {
            continue;
        }
        if (occupied_index == index) {
            return &record;
        }
        ++occupied_index;
    }
    return nullptr;
}

const EntityRecord *Registry::entity_at(size_t index) const {
    size_t occupied_index = 0U;
    for (const EntityRecord &record : entities_) {
        if (!record.occupied) {
            continue;
        }
        if (occupied_index == index) {
            return &record;
        }
        ++occupied_index;
    }
    return nullptr;
}

size_t Registry::device_count() const {
    return device_count_;
}

size_t Registry::entity_count() const {
    return entity_count_;
}

} // namespace aquahub
