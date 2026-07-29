#ifndef AQUARIUM_ALARM_EVENT_QUEUE_H
#define AQUARIUM_ALARM_EVENT_QUEUE_H

#include <Arduino.h>

constexpr size_t ALARM_EVENT_QUEUE_CAPACITY = 16U;

struct AlarmTransitionEvent {
    uint32_t boot_id;
    uint32_t event_sequence;
    uint32_t timestamp;
    uint32_t nonce;
    unsigned int active_flags;
    unsigned int raised_flags;
    unsigned int cleared_flags;
    bool timestamp_reliable;
};

using AlarmTransitionCallback =
    void (*)(const AlarmTransitionEvent &event);

bool alarm_event_queue_initialize(uint32_t boot_id);

/**
 * Adds one record only when the active alarm bitmask changes. The bounded ring
 * is persisted in NVS and is suitable for a future HTTPS/MQTT relay consumer.
 */
bool alarm_event_queue_update(unsigned int active_flags,
                              uint32_t timestamp,
                              bool timestamp_reliable);

size_t alarm_event_queue_snapshot(AlarmTransitionEvent *out,
                                  size_t capacity);

void alarm_event_queue_set_callback(AlarmTransitionCallback callback);
unsigned int alarm_event_queue_active_flags(void);

#endif // AQUARIUM_ALARM_EVENT_QUEUE_H
