#ifndef AQUARIUM_EVENTS_H
#define AQUARIUM_EVENTS_H

#include <Arduino.h>

enum class SensorId : uint8_t {
    Temp = 0,
    Ph,
    Ec,
    WaterLevel,
    Flow,
    Leak,
    Co2,
    Ldr,
    Heap,
    CpuTemp,
    Uptime,
    Count
};

struct SensorSample {
    SensorId id;
    float value;
    uint32_t timestampMs;
    bool valid;
};

enum class CommandType : uint8_t {
    SetLightMode = 0,
    SetDeviceMode,
    SetHeaterSetpoint,
    SetCo2Mode,
    TriggerFeed,
    EnterServiceMode,
    ExitServiceMode,
    StartCalibrationPoint,
    RequestOta
};

struct Command {
    CommandType type;
    int32_t a;
    int32_t b;
    float f;
};

using SensorSampleCallback = void (*)(const SensorSample &sample);

bool events_init(void);
bool events_publish_sample(const SensorSample &sample);
bool events_poll_sample(SensorSample &out);
bool events_publish_command(const Command &command);
bool events_poll_command(Command &out);
bool events_subscribe(SensorId id, SensorSampleCallback callback);
uint32_t events_sample_overflow_count(void);

#endif
