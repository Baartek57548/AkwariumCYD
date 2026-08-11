#include "aquahub_service.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <new>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace {

constexpr char kTag[] = "aquahub_service";
constexpr char kDiscoveryPrefix[] = "homeassistant/";
constexpr size_t kMaximumIncomingJsonBytes = 4096U;
constexpr size_t kHistoryCapacity = 512U;
constexpr size_t kDefaultEntityPageSize = 32U;
constexpr size_t kMaximumEntityPageSize = 48U;

struct HistoryRecord {
    bool occupied;
    uint64_t changed_at_ms;
    char entity_id[aquahub::kEntityIdBytes];
    aquahub::StateValue value;
};

aquahub::Registry *registry = nullptr;
SemaphoreHandle_t registry_mutex = nullptr;
HistoryRecord *history = nullptr;
size_t history_head = 0U;
size_t history_count = 0U;
uint32_t accepted_messages = 0U;
uint32_t rejected_messages = 0U;
uint32_t registry_revision = 0U;

class RegistryLock {
public:
    RegistryLock()
        : locked_(registry_mutex != nullptr &&
                  xSemaphoreTake(registry_mutex, pdMS_TO_TICKS(250U)) ==
                      pdTRUE) {
    }

    ~RegistryLock() {
        if (locked_) {
            xSemaphoreGive(registry_mutex);
        }
    }

    bool locked() const {
        return locked_;
    }

private:
    bool locked_;
};

template <size_t Capacity>
bool copy_json_string(cJSON *root,
                      const char *name,
                      char (&output)[Capacity],
                      bool required) {
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (item == nullptr && !required) {
        output[0] = '\0';
        return true;
    }
    if (!cJSON_IsString(item) || item->valuestring == nullptr) {
        return false;
    }
    const size_t length = strnlen(item->valuestring, Capacity);
    if (length == 0U && required) {
        return false;
    }
    if (length >= Capacity) {
        return false;
    }
    memcpy(output, item->valuestring, length + 1U);
    return true;
}

bool copy_text(char *output, size_t capacity, const char *input) {
    if (output == nullptr || capacity == 0U || input == nullptr) {
        return false;
    }
    const size_t length = strnlen(input, capacity);
    if (length >= capacity) {
        return false;
    }
    memcpy(output, input, length + 1U);
    return true;
}

aquahub::EntityKind component_kind(const char *component) {
    if (strcmp(component, "binary_sensor") == 0) {
        return aquahub::EntityKind::BinarySensor;
    }
    if (strcmp(component, "switch") == 0) {
        return aquahub::EntityKind::Switch;
    }
    if (strcmp(component, "number") == 0) {
        return aquahub::EntityKind::Number;
    }
    if (strcmp(component, "select") == 0) {
        return aquahub::EntityKind::Select;
    }
    if (strcmp(component, "button") == 0) {
        return aquahub::EntityKind::Button;
    }
    if (strcmp(component, "light") == 0) {
        return aquahub::EntityKind::Light;
    }
    return aquahub::EntityKind::Sensor;
}

aquahub::ValueType kind_value_type(aquahub::EntityKind kind) {
    switch (kind) {
    case aquahub::EntityKind::BinarySensor:
    case aquahub::EntityKind::Switch:
    case aquahub::EntityKind::Light:
        return aquahub::ValueType::Boolean;
    case aquahub::EntityKind::Sensor:
    case aquahub::EntityKind::Number:
        return aquahub::ValueType::Number;
    case aquahub::EntityKind::Select:
        return aquahub::ValueType::Text;
    case aquahub::EntityKind::Button:
        return aquahub::ValueType::None;
    }
    return aquahub::ValueType::None;
}

bool parse_component(const char *topic,
                     char *output,
                     size_t output_capacity) {
    if (topic == nullptr ||
        strncmp(topic,
                kDiscoveryPrefix,
                sizeof(kDiscoveryPrefix) - 1U) != 0) {
        return false;
    }
    const char *start = topic + sizeof(kDiscoveryPrefix) - 1U;
    const char *end = strchr(start, '/');
    if (end == nullptr || end == start) {
        return false;
    }
    const size_t length = static_cast<size_t>(end - start);
    if (length >= output_capacity) {
        return false;
    }
    memcpy(output, start, length);
    output[length] = '\0';
    return strcmp(output, "sensor") == 0 ||
           strcmp(output, "binary_sensor") == 0 ||
           strcmp(output, "switch") == 0 ||
           strcmp(output, "number") == 0 ||
           strcmp(output, "select") == 0 ||
           strcmp(output, "button") == 0 ||
           strcmp(output, "light") == 0;
}

bool parse_value_key(cJSON *root,
                     aquahub::EntityKind kind,
                     char *output,
                     size_t output_capacity) {
    if (kind == aquahub::EntityKind::Button) {
        output[0] = '\0';
        return true;
    }
    cJSON *explicit_key =
        cJSON_GetObjectItemCaseSensitive(root, "aquahub_value_key");
    if (cJSON_IsString(explicit_key) && explicit_key->valuestring != nullptr) {
        return copy_text(output, output_capacity, explicit_key->valuestring);
    }
    cJSON *value_template =
        cJSON_GetObjectItemCaseSensitive(root, "value_template");
    if (!cJSON_IsString(value_template) ||
        value_template->valuestring == nullptr) {
        return copy_text(output, output_capacity, "value");
    }
    const char *marker = strstr(value_template->valuestring, "value_json.");
    if (marker == nullptr) {
        return false;
    }
    marker += strlen("value_json.");
    size_t length = 0U;
    while ((marker[length] >= 'a' && marker[length] <= 'z') ||
           (marker[length] >= 'A' && marker[length] <= 'Z') ||
           (marker[length] >= '0' && marker[length] <= '9') ||
           marker[length] == '_') {
        ++length;
    }
    if (length == 0U || length >= output_capacity) {
        return false;
    }
    memcpy(output, marker, length);
    output[length] = '\0';
    return true;
}

bool parse_device(cJSON *root, aquahub::DeviceDescriptor *output) {
    cJSON *device = cJSON_GetObjectItemCaseSensitive(root, "device");
    if (!cJSON_IsObject(device)) {
        return false;
    }
    cJSON *identifiers =
        cJSON_GetObjectItemCaseSensitive(device, "identifiers");
    cJSON *node_id = cJSON_IsArray(identifiers)
                         ? cJSON_GetArrayItem(identifiers, 0)
                         : nullptr;
    if (!cJSON_IsString(node_id) || node_id->valuestring == nullptr ||
        !copy_text(output->node_id,
                   sizeof(output->node_id),
                   node_id->valuestring) ||
        !copy_json_string(device, "name", output->name, true) ||
        !copy_json_string(device, "model", output->model, false) ||
        !copy_json_string(device,
                          "manufacturer",
                          output->manufacturer,
                          false) ||
        !copy_json_string(device,
                          "sw_version",
                          output->firmware_version,
                          false) ||
        !copy_json_string(device, "suggested_area", output->area, false)) {
        return false;
    }
    return true;
}

bool parse_discovery(const char *topic,
                     const char *payload,
                     size_t payload_length,
                     aquahub::DiscoveryDescriptor *output) {
    if (payload_length == 0U || payload_length > kMaximumIncomingJsonBytes ||
        output == nullptr) {
        return false;
    }
    char component[24] = {};
    if (!parse_component(topic, component, sizeof(component))) {
        return false;
    }
    cJSON *root = cJSON_ParseWithLength(payload, payload_length);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }
    memset(output, 0, sizeof(*output));
    output->schema_version = aquahub::kDiscoverySchemaVersion;
    output->kind = component_kind(component);
    output->value_type = kind_value_type(output->kind);
    output->minimum = output->value_type == aquahub::ValueType::Number
                          ? -DBL_MAX
                          : 0.0;
    output->maximum = output->value_type == aquahub::ValueType::Number
                          ? DBL_MAX
                          : 0.0;
    output->step = output->value_type == aquahub::ValueType::Number
                       ? 1.0
                       : 0.0;
    bool valid = parse_device(root, &output->device) &&
                 copy_json_string(root,
                                  "unique_id",
                                  output->unique_id,
                                  true) &&
                 copy_json_string(root, "name", output->name, true) &&
                 copy_text(output->discovery_topic,
                           sizeof(output->discovery_topic),
                           topic) &&
                 copy_json_string(root,
                                  "state_topic",
                                  output->state_topic,
                                  output->kind !=
                                      aquahub::EntityKind::Button) &&
                 copy_json_string(root,
                                  "command_topic",
                                  output->command_topic,
                                  false) &&
                 copy_json_string(root,
                                  "availability_topic",
                                  output->availability_topic,
                                  true) &&
                 copy_json_string(root,
                                  "unit_of_measurement",
                                  output->unit,
                                  false) &&
                 parse_value_key(root,
                                 output->kind,
                                 output->value_key,
                                 sizeof(output->value_key));
    output->writable = output->command_topic[0] != '\0';
    cJSON *critical =
        cJSON_GetObjectItemCaseSensitive(root, "aquahub_critical");
    output->critical = cJSON_IsTrue(critical);

    if (output->kind == aquahub::EntityKind::Number) {
        cJSON *minimum = cJSON_GetObjectItemCaseSensitive(root, "min");
        cJSON *maximum = cJSON_GetObjectItemCaseSensitive(root, "max");
        cJSON *step = cJSON_GetObjectItemCaseSensitive(root, "step");
        if (cJSON_IsNumber(minimum)) {
            output->minimum = minimum->valuedouble;
        }
        if (cJSON_IsNumber(maximum)) {
            output->maximum = maximum->valuedouble;
        }
        if (cJSON_IsNumber(step)) {
            output->step = step->valuedouble;
        }
    }
    if (output->kind == aquahub::EntityKind::Select) {
        cJSON *options = cJSON_GetObjectItemCaseSensitive(root, "options");
        if (!cJSON_IsArray(options) ||
            cJSON_GetArraySize(options) <= 0 ||
            cJSON_GetArraySize(options) >
                static_cast<int>(aquahub::kMaximumOptions)) {
            valid = false;
        } else {
            output->option_count =
                static_cast<uint8_t>(cJSON_GetArraySize(options));
            for (size_t index = 0U;
                 valid && index < output->option_count;
                 ++index) {
                cJSON *option = cJSON_GetArrayItem(
                    options, static_cast<int>(index));
                valid = cJSON_IsString(option) &&
                        option->valuestring != nullptr &&
                        copy_text(output->options[index],
                                  sizeof(output->options[index]),
                                  option->valuestring);
            }
        }
    }
    cJSON_Delete(root);
    return valid;
}

aquahub::StateValue json_state_value(
    cJSON *item,
    const aquahub::DiscoveryDescriptor &descriptor) {
    aquahub::StateValue value = {};
    value.type = descriptor.value_type;
    if (item == nullptr || cJSON_IsNull(item)) {
        value.valid = false;
        return value;
    }
    switch (descriptor.value_type) {
    case aquahub::ValueType::Boolean:
        if (cJSON_IsBool(item)) {
            value.valid = true;
            value.boolean_value = cJSON_IsTrue(item);
        } else if (cJSON_IsNumber(item) &&
                   (item->valuedouble == 0.0 || item->valuedouble == 1.0)) {
            value.valid = true;
            value.boolean_value = item->valuedouble == 1.0;
        } else if (cJSON_IsString(item) && item->valuestring != nullptr &&
                   (strcmp(item->valuestring, "ON") == 0 ||
                    strcmp(item->valuestring, "OFF") == 0)) {
            value.valid = true;
            value.boolean_value = strcmp(item->valuestring, "ON") == 0;
        }
        break;
    case aquahub::ValueType::Number:
        if (cJSON_IsNumber(item) && isfinite(item->valuedouble)) {
            value.valid = true;
            value.number_value = item->valuedouble;
        }
        break;
    case aquahub::ValueType::Text:
        if (cJSON_IsString(item) && item->valuestring != nullptr &&
            copy_text(value.text_value,
                      sizeof(value.text_value),
                      item->valuestring)) {
            value.valid = true;
        }
        break;
    case aquahub::ValueType::None:
        break;
    }
    return value;
}

void append_history(const char *entity_id,
                    const aquahub::StateValue &value,
                    uint64_t changed_at_ms) {
    HistoryRecord &record = history[history_head];
    memset(&record, 0, sizeof(record));
    record.occupied = true;
    record.changed_at_ms = changed_at_ms;
    copy_text(record.entity_id, sizeof(record.entity_id), entity_id);
    record.value = value;
    history_head = (history_head + 1U) % kHistoryCapacity;
    if (history_count < kHistoryCapacity) {
        ++history_count;
    }
}

void handle_discovery(const char *topic,
                      const char *payload,
                      size_t payload_length) {
    RegistryLock lock;
    if (!lock.locked()) {
        ++rejected_messages;
        return;
    }
    if (payload_length == 0U) {
        for (size_t index = registry->entity_count(); index > 0U; --index) {
            const aquahub::EntityRecord *record =
                registry->entity_at(index - 1U);
            if (record != nullptr &&
                strcmp(record->descriptor.discovery_topic, topic) == 0) {
                registry->remove_entity(record->descriptor.unique_id);
                ++registry_revision;
                ++accepted_messages;
                return;
            }
        }
        ++rejected_messages;
        return;
    }
    aquahub::DiscoveryDescriptor descriptor = {};
    if (!parse_discovery(topic, payload, payload_length, &descriptor)) {
        ++rejected_messages;
        return;
    }
    const aquahub::RegistryStatus status =
        registry->upsert_discovery(descriptor);
    if (status == aquahub::RegistryStatus::Ok) {
        ++accepted_messages;
        ++registry_revision;
    } else {
        ++rejected_messages;
        ESP_LOGW(kTag,
                 "Rejected discovery %s with status %u",
                 descriptor.unique_id,
                 static_cast<unsigned>(status));
    }
}

void handle_state(const char *topic,
                  const char *payload,
                  size_t payload_length,
                  uint64_t received_at_ms) {
    if (payload_length == 0U || payload_length > kMaximumIncomingJsonBytes) {
        ++rejected_messages;
        return;
    }
    cJSON *root = cJSON_ParseWithLength(payload, payload_length);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        ++rejected_messages;
        return;
    }
    RegistryLock lock;
    if (!lock.locked()) {
        cJSON_Delete(root);
        ++rejected_messages;
        return;
    }

    const aquahub::EntityRecord *first = nullptr;
    for (size_t index = 0U; index < registry->entity_count(); ++index) {
        const aquahub::EntityRecord *record = registry->entity_at(index);
        if (record != nullptr &&
            strcmp(record->descriptor.state_topic, topic) == 0) {
            first = record;
            break;
        }
    }
    if (first == nullptr) {
        cJSON_Delete(root);
        return;
    }

    cJSON *sequence_item =
        cJSON_GetObjectItemCaseSensitive(root, "sequence");
    cJSON *boot_item = cJSON_GetObjectItemCaseSensitive(root, "boot_id");
    const uint32_t sequence = cJSON_IsNumber(sequence_item)
                                  ? static_cast<uint32_t>(
                                        sequence_item->valuedouble)
                                  : 0U;
    const uint32_t boot_id = cJSON_IsNumber(boot_item)
                                 ? static_cast<uint32_t>(
                                       boot_item->valuedouble)
                                 : 1U;
    const aquahub::RegistryStatus message_status =
        registry->accept_message(first->descriptor.device.node_id,
                                boot_id,
                                sequence,
                                received_at_ms);
    if (message_status != aquahub::RegistryStatus::Ok) {
        ++rejected_messages;
        cJSON_Delete(root);
        return;
    }

    uint32_t updated = 0U;
    for (size_t index = 0U; index < registry->entity_count(); ++index) {
        const aquahub::EntityRecord *record = registry->entity_at(index);
        if (record == nullptr ||
            strcmp(record->descriptor.state_topic, topic) != 0 ||
            record->descriptor.kind == aquahub::EntityKind::Button) {
            continue;
        }
        char unique_id[aquahub::kEntityIdBytes] = {};
        copy_text(unique_id,
                  sizeof(unique_id),
                  record->descriptor.unique_id);
        const aquahub::DiscoveryDescriptor descriptor = record->descriptor;
        cJSON *item = cJSON_GetObjectItemCaseSensitive(
            root, descriptor.value_key);
        const aquahub::StateValue value =
            json_state_value(item, descriptor);
        bool changed = false;
        if (registry->update_state(unique_id,
                                  value,
                                  received_at_ms,
                                  &changed) ==
            aquahub::RegistryStatus::Ok) {
            ++updated;
            if (changed) {
                append_history(unique_id, value, received_at_ms);
                ++registry_revision;
            }
        }
    }
    if (updated > 0U) {
        ++accepted_messages;
    } else {
        ++rejected_messages;
    }
    cJSON_Delete(root);
}

void handle_availability(const char *topic,
                         const char *payload,
                         size_t payload_length,
                         uint64_t received_at_ms) {
    if (payload_length == 0U || payload_length > 16U) {
        ++rejected_messages;
        return;
    }
    char value[17] = {};
    memcpy(value, payload, payload_length);
    const bool online = strcmp(value, "online") == 0;
    if (!online && strcmp(value, "offline") != 0) {
        ++rejected_messages;
        return;
    }
    RegistryLock lock;
    if (!lock.locked()) {
        ++rejected_messages;
        return;
    }
    bool matched = false;
    for (size_t index = 0U; index < registry->entity_count(); ++index) {
        const aquahub::EntityRecord *record = registry->entity_at(index);
        if (record != nullptr &&
            strcmp(record->descriptor.availability_topic, topic) == 0) {
            registry->update_availability(record->descriptor.device.node_id,
                                         online,
                                         received_at_ms);
            matched = true;
        }
    }
    if (matched) {
        ++accepted_messages;
        ++registry_revision;
    }
}

void add_state_json(cJSON *root, const aquahub::StateValue &state) {
    if (!state.valid) {
        cJSON_AddNullToObject(root, "state");
        return;
    }
    switch (state.type) {
    case aquahub::ValueType::Boolean:
        cJSON_AddBoolToObject(root, "state", state.boolean_value);
        break;
    case aquahub::ValueType::Number:
        cJSON_AddNumberToObject(root, "state", state.number_value);
        break;
    case aquahub::ValueType::Text:
        cJSON_AddStringToObject(root, "state", state.text_value);
        break;
    case aquahub::ValueType::None:
        cJSON_AddNullToObject(root, "state");
        break;
    }
}

const char *kind_name(aquahub::EntityKind kind) {
    switch (kind) {
    case aquahub::EntityKind::Sensor:
        return "sensor";
    case aquahub::EntityKind::BinarySensor:
        return "binary_sensor";
    case aquahub::EntityKind::Switch:
        return "switch";
    case aquahub::EntityKind::Number:
        return "number";
    case aquahub::EntityKind::Select:
        return "select";
    case aquahub::EntityKind::Button:
        return "button";
    case aquahub::EntityKind::Light:
        return "light";
    }
    return "unknown";
}

bool print_json(cJSON *root, char *output, size_t output_capacity) {
    if (root == nullptr || output == nullptr || output_capacity < 3U ||
        output_capacity > static_cast<size_t>(INT_MAX)) {
        cJSON_Delete(root);
        return false;
    }
    const bool printed = cJSON_PrintPreallocated(
        root, output, static_cast<int>(output_capacity), false);
    cJSON_Delete(root);
    return printed;
}

} // namespace

bool aquahub_service_initialize() {
    if (registry_mutex != nullptr && registry != nullptr && history != nullptr) {
        return true;
    }
    registry = static_cast<aquahub::Registry *>(heap_caps_malloc(
        sizeof(aquahub::Registry), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    history = static_cast<HistoryRecord *>(heap_caps_calloc(
        kHistoryCapacity,
        sizeof(HistoryRecord),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (registry == nullptr || history == nullptr) {
        heap_caps_free(registry);
        heap_caps_free(history);
        registry = nullptr;
        history = nullptr;
        ESP_LOGE(kTag, "PSRAM allocation for AquaHub registry failed");
        return false;
    }
    new (registry) aquahub::Registry();
    registry_mutex = xSemaphoreCreateMutex();
    if (registry_mutex == nullptr) {
        registry->~Registry();
        heap_caps_free(registry);
        heap_caps_free(history);
        registry = nullptr;
        history = nullptr;
        return false;
    }
    registry->clear();
    memset(history, 0, sizeof(HistoryRecord) * kHistoryCapacity);
    history_head = 0U;
    history_count = 0U;
    accepted_messages = 0U;
    rejected_messages = 0U;
    registry_revision = 0U;
    return true;
}

void aquahub_service_handle_mqtt(const char *,
                                 const char *topic,
                                 const char *payload,
                                 size_t payload_length,
                                 int,
                                 bool,
                                 uint64_t received_at_ms) {
    if (topic == nullptr || payload == nullptr || registry_mutex == nullptr ||
        registry == nullptr || history == nullptr) {
        ++rejected_messages;
        return;
    }
    if (strncmp(topic,
                kDiscoveryPrefix,
                sizeof(kDiscoveryPrefix) - 1U) == 0) {
        handle_discovery(topic, payload, payload_length);
        return;
    }

    bool availability_match = false;
    {
        RegistryLock lock;
        if (!lock.locked()) {
            ++rejected_messages;
            return;
        }
        for (size_t index = 0U; index < registry->entity_count(); ++index) {
            const aquahub::EntityRecord *record = registry->entity_at(index);
            if (record != nullptr &&
                strcmp(record->descriptor.availability_topic, topic) == 0) {
                availability_match = true;
                break;
            }
        }
    }
    if (availability_match) {
        handle_availability(topic,
                            payload,
                            payload_length,
                            received_at_ms);
    } else {
        handle_state(topic, payload, payload_length, received_at_ms);
    }
}

AquaHubSummary aquahub_service_summary() {
    AquaHubSummary summary = {};
    RegistryLock lock;
    if (!lock.locked()) {
        return summary;
    }
    summary.device_count = static_cast<uint16_t>(registry->device_count());
    summary.entity_count = static_cast<uint16_t>(registry->entity_count());
    for (size_t index = 0U; index < registry->device_count(); ++index) {
        const aquahub::DeviceRecord *record = registry->device_at(index);
        if (record != nullptr && record->online) {
            ++summary.online_device_count;
        }
    }
    for (size_t index = 0U; index < registry->entity_count(); ++index) {
        const aquahub::EntityRecord *record = registry->entity_at(index);
        if (record != nullptr && record->descriptor.writable) {
            ++summary.writable_entity_count;
        }
    }
    summary.accepted_messages = accepted_messages;
    summary.rejected_messages = rejected_messages;
    summary.registry_revision = registry_revision;
    return summary;
}

bool aquahub_service_write_system_json(char *output,
                                       size_t output_capacity,
                                       uint64_t uptime_ms,
                                       uint32_t free_heap_bytes) {
    const AquaHubSummary summary = aquahub_service_summary();
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "product", "aquahub-p4");
    cJSON_AddNumberToObject(root, "api_version", 1);
    cJSON_AddNumberToObject(root,
                            "uptime_ms",
                            static_cast<double>(uptime_ms));
    cJSON_AddNumberToObject(root, "free_heap_bytes", free_heap_bytes);
    cJSON_AddNumberToObject(root, "devices", summary.device_count);
    cJSON_AddNumberToObject(root,
                            "online_devices",
                            summary.online_device_count);
    cJSON_AddNumberToObject(root, "entities", summary.entity_count);
    cJSON_AddNumberToObject(root,
                            "writable_entities",
                            summary.writable_entity_count);
    cJSON_AddNumberToObject(root,
                            "accepted_messages",
                            summary.accepted_messages);
    cJSON_AddNumberToObject(root,
                            "rejected_messages",
                            summary.rejected_messages);
    cJSON_AddNumberToObject(root,
                            "registry_revision",
                            summary.registry_revision);
    return print_json(root, output, output_capacity);
}

bool aquahub_service_write_devices_json(char *output,
                                        size_t output_capacity) {
    RegistryLock lock;
    if (!lock.locked()) {
        return false;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "revision", registry_revision);
    cJSON *items = cJSON_AddArrayToObject(root, "items");
    for (size_t index = 0U; index < registry->device_count(); ++index) {
        const aquahub::DeviceRecord *record = registry->device_at(index);
        if (record == nullptr) {
            continue;
        }
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item,
                                "id",
                                record->descriptor.node_id);
        cJSON_AddStringToObject(item,
                                "name",
                                record->descriptor.name);
        cJSON_AddStringToObject(item,
                                "model",
                                record->descriptor.model);
        cJSON_AddStringToObject(item,
                                "manufacturer",
                                record->descriptor.manufacturer);
        cJSON_AddStringToObject(item,
                                "firmware_version",
                                record->descriptor.firmware_version);
        cJSON_AddStringToObject(item,
                                "area",
                                record->descriptor.area);
        cJSON_AddBoolToObject(item, "online", record->online);
        cJSON_AddNumberToObject(item,
                                "last_seen_ms",
                                static_cast<double>(record->last_seen_ms));
        cJSON_AddItemToArray(items, item);
    }
    return print_json(root, output, output_capacity);
}

bool aquahub_service_write_entities_json(char *output,
                                         size_t output_capacity,
                                         size_t offset,
                                         size_t limit) {
    if (limit == 0U) {
        limit = kDefaultEntityPageSize;
    }
    if (limit > kMaximumEntityPageSize) {
        limit = kMaximumEntityPageSize;
    }
    RegistryLock lock;
    if (!lock.locked()) {
        return false;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "revision", registry_revision);
    cJSON_AddNumberToObject(root, "total", registry->entity_count());
    cJSON_AddNumberToObject(root, "offset", offset);
    cJSON_AddNumberToObject(root, "limit", limit);
    cJSON *items = cJSON_AddArrayToObject(root, "items");
    const size_t end = offset + limit < registry->entity_count()
                           ? offset + limit
                           : registry->entity_count();
    for (size_t index = offset; index < end; ++index) {
        const aquahub::EntityRecord *record = registry->entity_at(index);
        if (record == nullptr) {
            continue;
        }
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item,
                                "id",
                                record->descriptor.unique_id);
        cJSON_AddStringToObject(item,
                                "device_id",
                                record->descriptor.device.node_id);
        cJSON_AddStringToObject(item,
                                "name",
                                record->descriptor.name);
        cJSON_AddStringToObject(item,
                                "kind",
                                kind_name(record->descriptor.kind));
        cJSON_AddStringToObject(item,
                                "unit",
                                record->descriptor.unit);
        cJSON_AddBoolToObject(item,
                              "writable",
                              record->descriptor.writable);
        cJSON_AddBoolToObject(item,
                              "critical",
                              record->descriptor.critical);
        if (record->descriptor.kind == aquahub::EntityKind::Number) {
            cJSON_AddNumberToObject(item,
                                    "minimum",
                                    record->descriptor.minimum);
            cJSON_AddNumberToObject(item,
                                    "maximum",
                                    record->descriptor.maximum);
            cJSON_AddNumberToObject(item,
                                    "step",
                                    record->descriptor.step);
        }
        if (record->descriptor.kind == aquahub::EntityKind::Select) {
            cJSON *options = cJSON_AddArrayToObject(item, "options");
            for (size_t option_index = 0U;
                 option_index < record->descriptor.option_count;
                 ++option_index) {
                cJSON_AddItemToArray(
                    options,
                    cJSON_CreateString(
                        record->descriptor.options[option_index]));
            }
        }
        cJSON_AddNumberToObject(item,
                                "changed_at_ms",
                                static_cast<double>(
                                    record->state_changed_ms));
        cJSON_AddNumberToObject(item,
                                "updated_at_ms",
                                static_cast<double>(
                                    record->state_updated_ms));
        add_state_json(item, record->state);
        cJSON_AddItemToArray(items, item);
    }
    return print_json(root, output, output_capacity);
}

bool aquahub_service_write_history_json(char *output,
                                        size_t output_capacity,
                                        const char *entity_id,
                                        size_t limit) {
    if (entity_id == nullptr || limit == 0U || limit > kHistoryCapacity) {
        return false;
    }
    RegistryLock lock;
    if (!lock.locked()) {
        return false;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "entity_id", entity_id);
    cJSON *items = cJSON_AddArrayToObject(root, "items");
    size_t emitted = 0U;
    for (size_t relative = 0U;
         relative < history_count && emitted < limit;
         ++relative) {
        const size_t index =
            (history_head + kHistoryCapacity - 1U - relative) %
            kHistoryCapacity;
        const HistoryRecord &record = history[index];
        if (!record.occupied ||
            strcmp(record.entity_id, entity_id) != 0) {
            continue;
        }
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item,
                                "changed_at_ms",
                                static_cast<double>(record.changed_at_ms));
        add_state_json(item, record.value);
        cJSON_AddItemToArray(items, item);
        ++emitted;
    }
    cJSON_AddNumberToObject(root, "count", emitted);
    return print_json(root, output, output_capacity);
}

bool aquahub_service_build_command(const char *entity_id,
                                   const aquahub::StateValue &value,
                                   char *topic,
                                   size_t topic_capacity,
                                   char *payload,
                                   size_t payload_capacity) {
    if (entity_id == nullptr || topic == nullptr || payload == nullptr) {
        return false;
    }
    RegistryLock lock;
    if (!lock.locked()) {
        return false;
    }
    const aquahub::EntityRecord *record = registry->entity(entity_id);
    if (record == nullptr || !record->descriptor.writable ||
        record->descriptor.critical ||
        !copy_text(topic,
                   topic_capacity,
                   record->descriptor.command_topic)) {
        return false;
    }
    int written = -1;
    switch (record->descriptor.kind) {
    case aquahub::EntityKind::Switch:
    case aquahub::EntityKind::Light:
        if (value.valid && value.type == aquahub::ValueType::Boolean) {
            written = snprintf(payload,
                               payload_capacity,
                               "%s",
                               value.boolean_value ? "ON" : "OFF");
        }
        break;
    case aquahub::EntityKind::Number:
        if (value.valid && value.type == aquahub::ValueType::Number &&
            isfinite(value.number_value) &&
            value.number_value >= record->descriptor.minimum &&
            value.number_value <= record->descriptor.maximum) {
            written = snprintf(payload,
                               payload_capacity,
                               "%.6g",
                               value.number_value);
        }
        break;
    case aquahub::EntityKind::Select:
        if (value.valid && value.type == aquahub::ValueType::Text) {
            for (size_t index = 0U;
                 index < record->descriptor.option_count;
                 ++index) {
                if (strcmp(value.text_value,
                           record->descriptor.options[index]) == 0) {
                    written = snprintf(payload,
                                       payload_capacity,
                                       "%s",
                                       value.text_value);
                    break;
                }
            }
        }
        break;
    case aquahub::EntityKind::Button:
        written = snprintf(payload, payload_capacity, "PRESS");
        break;
    case aquahub::EntityKind::Sensor:
    case aquahub::EntityKind::BinarySensor:
        break;
    }
    return written > 0 && static_cast<size_t>(written) < payload_capacity;
}
