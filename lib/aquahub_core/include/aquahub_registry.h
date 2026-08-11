#ifndef AQUAHUB_REGISTRY_H
#define AQUAHUB_REGISTRY_H

#include <stddef.h>
#include <stdint.h>

namespace aquahub {

constexpr uint16_t kDiscoverySchemaVersion = 1U;
constexpr size_t kMaximumDevices = 16U;
constexpr size_t kMaximumEntities = 128U;
constexpr size_t kNodeIdBytes = 33U;
constexpr size_t kEntityIdBytes = 65U;
constexpr size_t kNameBytes = 49U;
constexpr size_t kModelBytes = 33U;
constexpr size_t kManufacturerBytes = 33U;
constexpr size_t kFirmwareVersionBytes = 24U;
constexpr size_t kAreaBytes = 33U;
constexpr size_t kTopicBytes = 161U;
constexpr size_t kValueKeyBytes = 33U;
constexpr size_t kUnitBytes = 17U;
constexpr size_t kTextValueBytes = 65U;
constexpr size_t kMaximumOptions = 8U;
constexpr size_t kOptionBytes = 25U;

enum class EntityKind : uint8_t {
    Sensor = 0U,
    BinarySensor,
    Switch,
    Number,
    Select,
    Button,
    Light
};

enum class ValueType : uint8_t {
    None = 0U,
    Boolean,
    Number,
    Text
};

enum class RegistryStatus : uint8_t {
    Ok = 0U,
    NullArgument,
    InvalidSchema,
    InvalidIdentifier,
    InvalidTopic,
    InvalidDescriptor,
    InvalidValue,
    DeviceCapacityReached,
    EntityCapacityReached,
    IdentityConflict,
    DeviceNotFound,
    EntityNotFound,
    ReplayRejected
};

struct DeviceDescriptor {
    char node_id[kNodeIdBytes];
    char name[kNameBytes];
    char model[kModelBytes];
    char manufacturer[kManufacturerBytes];
    char firmware_version[kFirmwareVersionBytes];
    char area[kAreaBytes];
};

struct DiscoveryDescriptor {
    uint16_t schema_version;
    DeviceDescriptor device;
    char unique_id[kEntityIdBytes];
    char name[kNameBytes];
    char discovery_topic[kTopicBytes];
    char state_topic[kTopicBytes];
    char command_topic[kTopicBytes];
    char availability_topic[kTopicBytes];
    char value_key[kValueKeyBytes];
    char unit[kUnitBytes];
    EntityKind kind;
    ValueType value_type;
    bool writable;
    bool critical;
    double minimum;
    double maximum;
    double step;
    uint8_t option_count;
    char options[kMaximumOptions][kOptionBytes];
};

struct StateValue {
    ValueType type;
    bool valid;
    bool boolean_value;
    double number_value;
    char text_value[kTextValueBytes];
};

struct DeviceRecord {
    bool occupied;
    DeviceDescriptor descriptor;
    bool online;
    uint32_t boot_id;
    uint32_t newest_sequence;
    uint64_t last_seen_ms;
};

struct EntityRecord {
    bool occupied;
    DiscoveryDescriptor descriptor;
    StateValue state;
    uint64_t state_changed_ms;
    uint64_t state_updated_ms;
};

bool valid_identifier(const char *value, size_t capacity);
bool valid_topic(const char *value, bool allow_empty);
bool state_values_equal(const StateValue &left, const StateValue &right);

class Registry {
public:
    Registry();

    void clear();
    RegistryStatus upsert_discovery(const DiscoveryDescriptor &descriptor);
    RegistryStatus remove_entity(const char *unique_id);
    RegistryStatus accept_message(const char *node_id,
                                  uint32_t boot_id,
                                  uint32_t sequence,
                                  uint64_t received_at_ms);
    RegistryStatus update_availability(const char *node_id,
                                       bool online,
                                       uint64_t received_at_ms);
    RegistryStatus update_state(const char *unique_id,
                                const StateValue &value,
                                uint64_t received_at_ms,
                                bool *changed = nullptr);

    const DeviceRecord *device(const char *node_id) const;
    DeviceRecord *device(const char *node_id);
    const EntityRecord *entity(const char *unique_id) const;
    EntityRecord *entity(const char *unique_id);
    const DeviceRecord *device_at(size_t index) const;
    const EntityRecord *entity_at(size_t index) const;
    size_t device_count() const;
    size_t entity_count() const;

private:
    DeviceRecord devices_[kMaximumDevices];
    EntityRecord entities_[kMaximumEntities];
    size_t device_count_;
    size_t entity_count_;

    DeviceRecord *allocate_device(const DeviceDescriptor &descriptor);
};

} // namespace aquahub

#endif
