#include "alarm_event_queue.h"

#include <Preferences.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

namespace {

constexpr char ALARM_QUEUE_NAMESPACE[] = "aq_alarm_evt";
constexpr char ALARM_QUEUE_KEY[] = "ring";
constexpr uint32_t ALARM_QUEUE_MAGIC = 0x31564541UL; // AEV1
constexpr uint16_t ALARM_QUEUE_VERSION = 1U;

struct __attribute__((packed)) PersistentAlarmQueue {
    uint32_t magic;
    uint16_t version;
    uint8_t count;
    uint8_t next;
    uint32_t boot_id;
    uint32_t next_sequence;
    unsigned int active_flags;
    AlarmTransitionEvent events[ALARM_EVENT_QUEUE_CAPACITY];
    uint32_t crc32;
};

StaticSemaphore_t alarm_mutex_storage;
SemaphoreHandle_t alarm_mutex =
    xSemaphoreCreateMutexStatic(&alarm_mutex_storage);
PersistentAlarmQueue queue_state = {};
AlarmTransitionCallback transition_callback = nullptr;

uint32_t crc32_bytes(const void *buffer, size_t length) {
    uint32_t crc = 0xFFFFFFFFUL;
    const uint8_t *bytes = static_cast<const uint8_t *>(buffer);
    for (size_t index = 0U; index < length; ++index) {
        crc ^= bytes[index];
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            crc = (crc & 1U) != 0U
                      ? (crc >> 1U) ^ 0xEDB88320UL
                      : crc >> 1U;
        }
    }
    return ~crc;
}

uint32_t queue_crc(const PersistentAlarmQueue &queue) {
    return crc32_bytes(
        &queue, sizeof(queue) - sizeof(queue.crc32));
}

bool queue_valid(const PersistentAlarmQueue &queue) {
    return queue.magic == ALARM_QUEUE_MAGIC &&
           queue.version == ALARM_QUEUE_VERSION &&
           queue.count <= ALARM_EVENT_QUEUE_CAPACITY &&
           queue.next < ALARM_EVENT_QUEUE_CAPACITY &&
           queue.crc32 == queue_crc(queue);
}

bool lock_queue() {
    return alarm_mutex != nullptr &&
           xSemaphoreTake(alarm_mutex, pdMS_TO_TICKS(100U)) == pdTRUE;
}

bool persist_queue_locked() {
    queue_state.magic = ALARM_QUEUE_MAGIC;
    queue_state.version = ALARM_QUEUE_VERSION;
    queue_state.crc32 = queue_crc(queue_state);
    Preferences storage;
    if (!storage.begin(ALARM_QUEUE_NAMESPACE, false)) {
        return false;
    }
    const bool saved =
        storage.putBytes(
            ALARM_QUEUE_KEY,
            &queue_state,
            sizeof(queue_state)) == sizeof(queue_state);
    storage.end();
    return saved;
}

} // namespace

bool alarm_event_queue_initialize(uint32_t boot_id) {
    PersistentAlarmQueue loaded = {};
    Preferences storage;
    if (storage.begin(ALARM_QUEUE_NAMESPACE, false)) {
        const size_t bytes =
            storage.getBytes(ALARM_QUEUE_KEY, &loaded, sizeof(loaded));
        storage.end();
        if (bytes != sizeof(loaded) || !queue_valid(loaded)) {
            memset(&loaded, 0, sizeof(loaded));
        }
    }
    if (!lock_queue()) {
        return false;
    }
    queue_state = loaded;
    queue_state.boot_id = boot_id;
    queue_state.next_sequence = 0U;
    queue_state.active_flags = 0U;
    const bool saved = persist_queue_locked();
    xSemaphoreGive(alarm_mutex);
    return saved;
}

bool alarm_event_queue_update(unsigned int active_flags,
                              uint32_t timestamp,
                              bool timestamp_reliable) {
    AlarmTransitionEvent event = {};
    AlarmTransitionCallback callback = nullptr;
    if (!lock_queue()) {
        return false;
    }
    const unsigned int previous = queue_state.active_flags;
    if (previous == active_flags) {
        xSemaphoreGive(alarm_mutex);
        return true;
    }
    event.boot_id = queue_state.boot_id;
    event.event_sequence = ++queue_state.next_sequence;
    event.timestamp = timestamp;
    event.nonce = esp_random();
    event.active_flags = active_flags;
    event.raised_flags = active_flags & ~previous;
    event.cleared_flags = previous & ~active_flags;
    event.timestamp_reliable = timestamp_reliable;
    queue_state.events[queue_state.next] = event;
    queue_state.next = static_cast<uint8_t>(
        (queue_state.next + 1U) % ALARM_EVENT_QUEUE_CAPACITY);
    if (queue_state.count < ALARM_EVENT_QUEUE_CAPACITY) {
        ++queue_state.count;
    }
    queue_state.active_flags = active_flags;
    const bool saved = persist_queue_locked();
    callback = transition_callback;
    xSemaphoreGive(alarm_mutex);
    if (callback != nullptr) {
        callback(event);
    }
    return saved;
}

size_t alarm_event_queue_snapshot(AlarmTransitionEvent *out,
                                  size_t capacity) {
    if (out == nullptr || capacity == 0U || !lock_queue()) {
        return 0U;
    }
    const size_t count =
        queue_state.count < capacity ? queue_state.count : capacity;
    const size_t skip =
        static_cast<size_t>(queue_state.count) - count;
    const size_t oldest =
        (static_cast<size_t>(queue_state.next) +
         ALARM_EVENT_QUEUE_CAPACITY -
         static_cast<size_t>(queue_state.count)) %
        ALARM_EVENT_QUEUE_CAPACITY;
    for (size_t index = 0U; index < count; ++index) {
        const size_t source =
            (oldest + skip + index) %
            ALARM_EVENT_QUEUE_CAPACITY;
        out[index] = queue_state.events[source];
    }
    xSemaphoreGive(alarm_mutex);
    return count;
}

void alarm_event_queue_set_callback(AlarmTransitionCallback callback) {
    if (!lock_queue()) {
        return;
    }
    transition_callback = callback;
    xSemaphoreGive(alarm_mutex);
}

unsigned int alarm_event_queue_active_flags(void) {
    if (!lock_queue()) {
        return 0U;
    }
    const unsigned int flags = queue_state.active_flags;
    xSemaphoreGive(alarm_mutex);
    return flags;
}
