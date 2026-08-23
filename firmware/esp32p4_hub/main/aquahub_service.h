#ifndef AQUAHUB_SERVICE_H
#define AQUAHUB_SERVICE_H

#include <stddef.h>
#include <stdint.h>

#include "aquahub_registry.h"
#include "aquahub_automation.h"

using AquaHubServicePublishCallback =
    bool (*)(const char *topic,
             const char *payload,
             int qos,
             bool retained,
             void *context);

struct AquaHubSummary {
    uint16_t device_count;
    uint16_t online_device_count;
    uint16_t entity_count;
    uint16_t writable_entity_count;
    uint32_t accepted_messages;
    uint32_t rejected_messages;
    uint32_t registry_revision;
};

bool aquahub_service_initialize();
void aquahub_service_set_publisher(AquaHubServicePublishCallback publisher,
                                   void *context);

/**
 * Ingests one message observed by the local MQTT broker. The broker callback
 * remains non-blocking: parsing is bounded by the discovery and state limits.
 */
void aquahub_service_handle_mqtt(const char *client_id,
                                 const char *topic,
                                 const char *payload,
                                 size_t payload_length,
                                 int qos,
                                 bool retained,
                                 uint64_t received_at_ms);

AquaHubSummary aquahub_service_summary();

bool aquahub_service_write_system_json(char *output,
                                       size_t output_capacity,
                                       uint64_t uptime_ms,
                                       uint32_t free_heap_bytes);
bool aquahub_service_write_devices_json(char *output,
                                        size_t output_capacity);
bool aquahub_service_write_entities_json(char *output,
                                         size_t output_capacity,
                                         size_t offset,
                                         size_t limit);
bool aquahub_service_write_history_json(char *output,
                                        size_t output_capacity,
                                        const char *entity_id,
                                        size_t limit);
bool aquahub_service_write_automations_json(char *output,
                                            size_t output_capacity);
aquahub::AutomationStatus aquahub_service_upsert_automation(
    const aquahub::AutomationRule &rule);
aquahub::AutomationStatus aquahub_service_remove_automation(const char *id);

/**
 * Validates a generic entity command and serializes the MQTT topic/payload.
 * Critical AquaCYD operations continue to use their dedicated ACK contract.
 */
bool aquahub_service_build_command(const char *entity_id,
                                   const aquahub::StateValue &value,
                                   char *topic,
                                   size_t topic_capacity,
                                   char *payload,
                                   size_t payload_capacity);

#endif
