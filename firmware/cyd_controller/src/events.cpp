#include "events.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

namespace {

constexpr UBaseType_t SAMPLE_QUEUE_LENGTH = 16;
constexpr UBaseType_t COMMAND_QUEUE_LENGTH = 8;
constexpr uint8_t MAX_SUBSCRIBERS_PER_SENSOR = 4;
constexpr TickType_t COMMAND_SEND_TIMEOUT_TICKS = pdMS_TO_TICKS(20);

QueueHandle_t sample_queue = nullptr;
QueueHandle_t command_queue = nullptr;
SemaphoreHandle_t subscriber_mutex = nullptr;
portMUX_TYPE sample_overflows_mux = portMUX_INITIALIZER_UNLOCKED;
uint32_t sample_overflows = 0;

SensorSampleCallback subscribers[static_cast<uint8_t>(SensorId::Count)][MAX_SUBSCRIBERS_PER_SENSOR] = {};

bool valid_sensor_id(SensorId id)
{
    return static_cast<uint8_t>(id) < static_cast<uint8_t>(SensorId::Count);
}

void notify_subscribers(const SensorSample &sample)
{
    if (!valid_sensor_id(sample.id) || subscriber_mutex == nullptr) {
        return;
    }

    SensorSampleCallback local_callbacks[MAX_SUBSCRIBERS_PER_SENSOR] = {};
    if (xSemaphoreTake(subscriber_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        const uint8_t idx = static_cast<uint8_t>(sample.id);
        for (uint8_t i = 0; i < MAX_SUBSCRIBERS_PER_SENSOR; ++i) {
            local_callbacks[i] = subscribers[idx][i];
        }
        xSemaphoreGive(subscriber_mutex);
    }

    for (SensorSampleCallback callback : local_callbacks) {
        if (callback != nullptr) {
            callback(sample);
        }
    }
}

} // namespace

bool events_init(void)
{
    if (sample_queue == nullptr) {
        sample_queue = xQueueCreate(SAMPLE_QUEUE_LENGTH, sizeof(SensorSample));
    }
    if (command_queue == nullptr) {
        command_queue = xQueueCreate(COMMAND_QUEUE_LENGTH, sizeof(Command));
    }
    if (subscriber_mutex == nullptr) {
        subscriber_mutex = xSemaphoreCreateMutex();
    }

    return sample_queue != nullptr && command_queue != nullptr && subscriber_mutex != nullptr;
}

bool events_publish_sample(const SensorSample &sample)
{
    if (sample_queue == nullptr || !valid_sensor_id(sample.id)) {
        return false;
    }

    if (xQueueSend(sample_queue, &sample, 0) == pdTRUE) {
        return true;
    }

    SensorSample discarded = {};
    if (xQueueReceive(sample_queue, &discarded, 0) == pdTRUE) {
        portENTER_CRITICAL(&sample_overflows_mux);
        ++sample_overflows;
        portEXIT_CRITICAL(&sample_overflows_mux);
    }

    return xQueueSend(sample_queue, &sample, 0) == pdTRUE;
}

bool events_poll_sample(SensorSample &out)
{
    if (sample_queue == nullptr) {
        return false;
    }

    if (xQueueReceive(sample_queue, &out, 0) != pdTRUE) {
        return false;
    }

    notify_subscribers(out);
    return true;
}

bool events_publish_command(const Command &command)
{
    if (command_queue == nullptr) {
        return false;
    }

    return xQueueSend(command_queue, &command, COMMAND_SEND_TIMEOUT_TICKS) == pdTRUE;
}

bool events_poll_command(Command &out)
{
    if (command_queue == nullptr) {
        return false;
    }

    return xQueueReceive(command_queue, &out, 0) == pdTRUE;
}

bool events_subscribe(SensorId id, SensorSampleCallback callback)
{
    if (!valid_sensor_id(id) || callback == nullptr || subscriber_mutex == nullptr) {
        return false;
    }

    if (xSemaphoreTake(subscriber_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
        return false;
    }

    bool added = false;
    const uint8_t idx = static_cast<uint8_t>(id);
    for (uint8_t i = 0; i < MAX_SUBSCRIBERS_PER_SENSOR; ++i) {
        if (subscribers[idx][i] == callback) {
            added = true;
            break;
        }
        if (subscribers[idx][i] == nullptr) {
            subscribers[idx][i] = callback;
            added = true;
            break;
        }
    }

    xSemaphoreGive(subscriber_mutex);
    return added;
}

uint32_t events_sample_overflow_count(void)
{
    portENTER_CRITICAL(&sample_overflows_mux);
    const uint32_t overflows = sample_overflows;
    portEXIT_CRITICAL(&sample_overflows_mux);
    return overflows;
}
