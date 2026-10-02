#ifndef AQUARIUM_S3_UI_MODEL_H
#define AQUARIUM_S3_UI_MODEL_H

#include <stddef.h>
#include <stdint.h>

namespace ui_model {

enum class ModuleId : uint8_t {
    FrontLight,
    RearLight,
    Filter,
    Air,
    Heater,
    Feeder,
    Co2,
    Ph,
    Ec,
    Ato,
    Flow,
    Count
};
enum class Category : uint8_t { Lighting, Water, Feeding, Chemistry };
enum class Icon : uint8_t { Sun, Water, Air, Heat, Food, Gas, Drop, Gauge, Level, Flow };
enum class Mode : uint8_t { Auto, On, Off };
enum class State : uint8_t { Unknown, On, Off, Blocked, Warning };
enum class LightMode : uint8_t { Day, Daybreak, Night, Off };
enum class Scenario : uint8_t { DemoHealthy, Offline, Alarm, Feeding, Service, LowWater, LevelFault };
enum class ScheduleMode : uint8_t { Disabled, Timed, Always };
enum class Error : uint8_t {
    None,
    InvalidModule,
    NotControllable,
    Busy,
    InvalidMode,
    InvalidTime,
    EmptyInterval,
    InvalidDays,
    InvalidCount,
    DuplicateTime,
    InvalidDuration,
    InvalidTarget,
    InvalidHysteresis,
    ProtectedLight,
    UnsafeTemperature,
    InvalidRange,
    InvalidPin
};
enum class LogLevel : uint8_t { Info, Warning, Alarm };

struct Metadata {
    ModuleId id;
    const char *title;
    const char *shortTitle;
    Category category;
    Icon icon;
    bool controllable;
};

struct Measurement {
    float value;
    bool valid;
    bool stale;
};
// MIN.active means a low-level request; MAX.active means the target level is reached.
struct LevelInput {
    bool active;
    bool valid;
};
struct Module {
    Mode mode;
    State state;
    LightMode lightMode;
    bool outputAssigned;
    bool known;
    bool transitioning;
};
struct Schedule {
    uint16_t startMinute;
    uint16_t endMinute;
    ScheduleMode mode;
    uint8_t dayMask;
};
// Transition times are wall-clock minutes; an overnight program is anchored to its start day.
struct LightProgram {
    uint16_t dayStartMinute;
    uint16_t duskStartMinute;
    uint16_t nightStartMinute;
    bool enabled;
};
struct FeedingConfig {
    uint8_t dayMask;
    uint8_t count;
    uint16_t times[2];
    uint16_t pauseSeconds;
};
constexpr uint16_t outputBit(ModuleId id) { return 1u << static_cast<unsigned>(id); }
constexpr uint16_t kFeedingOutputMask = outputBit(ModuleId::FrontLight) | outputBit(ModuleId::RearLight) |
                                        outputBit(ModuleId::Filter) | outputBit(ModuleId::Air) |
                                        outputBit(ModuleId::Heater) | outputBit(ModuleId::Co2);
struct FeederConfig {
    uint16_t rotationDegrees;
    uint16_t pauseMask;
    uint16_t minimumIntervalMinutes;
    FeederConfig(uint16_t degrees = 90, uint16_t mask = 0, uint16_t interval = 10)
        : rotationDegrees(degrees), pauseMask(mask), minimumIntervalMinutes(interval) {}
};
enum class FeederSource : uint8_t { LocalModel, Controller };
enum class FeederPhase : uint8_t { Idle, Waiting, Moving, Complete, Fault };
struct FeederProgress {
    uint32_t job;
    uint16_t targetDegrees;
    float positionDegrees;
    bool positionValid;
    FeederSource source;
    FeederPhase phase;
};
struct HeaterConfig {
    float target;
    float hysteresis;
};
struct SensorRange {
    float minimum;
    float maximum;
};
struct Validation {
    Error error;
    const char *message;
    bool ok() const { return error == Error::None; }
};

enum AlarmFlag : uint16_t {
    AlarmNone = 0,
    AlarmTemperature = 1u << 0,
    AlarmPh = 1u << 1,
    AlarmLevelUnknown = 1u << 2,
    AlarmOffline = 1u << 3,
    AlarmLowWater = 1u << 4,
    AlarmLevelFault = 1u << 5,
    AlarmTemperatureRange = 1u << 6,
    AlarmEcRange = 1u << 7,
    AlarmSensorUnknown = 1u << 8,
    AlarmFeeder = 1u << 9
};
enum class Severity : uint8_t { Normal, Information, Warning, Critical };
enum class WaterState : uint8_t { Normal, Between, Low, Unknown, Conflict };
struct Health {
    Severity severity;
    const char *title;
    const char *detail;
};
struct ServiceConfig {
    ModuleId primaryLight;
    bool secondaryLight;
    bool filter;
    bool air;
    bool heater;
    bool co2;
};
struct HistorySample {
    uint32_t atSeconds;
    uint32_t wallSeconds;
    Measurement temperature;
    Measurement ph;
    Measurement ec;
};
struct HeaterEvent {
    uint32_t atSeconds;
    State state;
};
struct WaterEvent {
    uint32_t atSeconds;
    WaterState state;
};
struct AtoDose {
    uint32_t startSeconds, endSeconds, durationMs;
};
struct DomainConfig {
    Schedule schedules[static_cast<size_t>(ModuleId::Count)];
    FeedingConfig feeding;
    HeaterConfig heater;
    SensorRange ranges[3];
    FeederConfig feeder;
    LightProgram lightPrograms[2];
};
struct LogEntry {
    uint32_t atMs;
    uint32_t wallSeconds, lastWallSeconds, endedWallSeconds;
    uint32_t lastAtMs, sequence;
    uint16_t repeats, cause;
    bool active;
    LogLevel level;
    char message[96];
};
struct NextFeeding {
    bool available;
    uint16_t minute;
    uint8_t daysAway;
};

struct Snapshot {
    bool demo;
    Scenario scenario;
    bool connected;
    Measurement temperature;
    Measurement ph;
    Measurement ec;
    bool flowDetected;
    bool flowValid;
    LevelInput levelMin;
    LevelInput levelMax;
    Module modules[static_cast<size_t>(ModuleId::Count)];
    Schedule schedules[static_cast<size_t>(ModuleId::Count)];
    LightProgram lightPrograms[2];
    FeedingConfig feeding;
    FeederConfig feeder;
    FeederProgress motor;
    HeaterConfig heater;
    SensorRange ranges[3];
    bool feedingActive;
    bool serviceActive;
    ServiceConfig service;
    uint32_t feedingRemainingSeconds;
    uint32_t feedingDurationSeconds;
    bool dispensing;
    bool lastFeedingKnown;
    uint32_t lastFeedingWallSeconds;
    FeederSource lastFeedingSource;
    uint32_t feedingCooldownSeconds;
    uint32_t filterPauseRemainingSeconds;
    uint32_t serviceRemainingSeconds;
    uint16_t alarmFlags;
    bool alarmAcknowledged;
    uint32_t revision;
    uint32_t manualRemainingSeconds[static_cast<size_t>(ModuleId::Count)];
};

static const uint8_t kLogCapacity = 24;
static const uint16_t kMaximumPauseSeconds = 3600;
static const uint16_t kMaximumServiceSeconds = 7200;
static const uint8_t kHistoryCapacity = 64;
// The fast ring preserves 15-second detail; the fixed archive retains over 72 hours at 5 minutes.
static const uint16_t kHistoryArchiveCapacity = 865;
static const uint16_t kHeaterEventCapacity = 1024;
static const uint16_t kWaterEventCapacity = 1024;
static const uint16_t kAtoDoseCapacity = 512;
constexpr float kNominalAtoLitresPerMinute = 1.0f;

void init(uint32_t nowMs = 0);
using HistoryClock = uint32_t (*)();
// Install on the UI/model owner. The provider is called only when a sample is captured.
void setHistoryClock(HistoryClock clock);
void tick(uint32_t nowMs);
void setScenario(Scenario scenario);
const Snapshot &snapshot();
const Metadata &metadata(ModuleId id);
const Module &module(ModuleId id);
const char *modeName(Mode mode);
const char *stateName(State state);
const char *lightModeName(LightMode mode);
const char *scenarioName(Scenario scenario);

Validation validateSchedule(const Schedule &draft);
Validation applySchedule(ModuleId id, const Schedule &draft);
Validation validateLightProgram(const Schedule &schedule, const LightProgram &program);
LightMode lightModeFor(const Schedule &schedule, const LightProgram &program, uint16_t minute,
                       uint8_t weekday);
bool scheduleContains(const Schedule &schedule, uint16_t minute, uint8_t weekday);
Validation validateFeeding(const FeedingConfig &draft);
Validation applyFeeding(const FeedingConfig &draft);
Validation validateFeeder(const FeederConfig &draft);
bool feedingPauses(ModuleId id);
// Call only on the model/UI owner after a transport event has crossed the I/O queue.
bool applyFeederPosition(uint32_t job, float degrees, bool complete);
Validation acknowledgeFeederFault();
void setFeederSource(FeederSource source);
Validation validateHeater(const HeaterConfig &draft);
Validation applyHeater(const HeaterConfig &draft);
Validation validateRange(uint8_t kind, const SensorRange &range);
Validation applyRange(uint8_t kind, const SensorRange &range);
Validation setModuleMode(ModuleId id, Mode mode);
Validation setTimedMode(ModuleId id, Mode mode, uint32_t seconds);
uint16_t demoMinute();
Validation toggleModule(ModuleId id);
Validation setLightMode(ModuleId id, LightMode mode);
Validation startFeeding(uint16_t pauseSeconds, bool dose = true);
Validation startAdditionalDose(uint16_t pauseSeconds, uint32_t completedJob, const char *adminPin);
NextFeeding nextFeeding();
void cancelFeeding();
Validation startService(uint16_t seconds);
void cancelService();
Validation chooseServiceLight(ModuleId id);
Validation setServiceOutput(ModuleId id, bool enabled);
WaterState waterState();
Health health();
uint8_t weekday();
void setClock(uint16_t minute, uint8_t weekday);
DomainConfig configuration();
Validation validateConfiguration(const DomainConfig &config);
Validation restoreConfiguration(const DomainConfig &config);
uint8_t historyCount();
const HistorySample *historyAt(uint8_t oldestIndex);
uint16_t historyArchiveCount();
const HistorySample *historyArchiveAt(uint16_t oldestIndex);
uint16_t heaterEventCount();
const HeaterEvent *heaterEventAt(uint16_t oldestIndex);
uint32_t historyRevision();
uint16_t waterEventCount();
const WaterEvent *waterEventAt(uint16_t oldestIndex);
uint16_t atoDoseCount();
const AtoDose *atoDoseAt(uint16_t oldestIndex);
// Ingest a completed controller report on the UI/model owner; this never actuates a pump.
bool recordAtoDose(uint32_t durationMs);
float estimatedAtoLitres(const AtoDose &dose);
void clearHistory();
void clearLog();
void recordPanelEvent(const char *text, LogLevel level = LogLevel::Info);
void acknowledgeAlarms();
uint8_t logCount();
uint32_t logRevision();
// Index zero is the newest entry. Returns nullptr for an invalid index.
const LogEntry *logAt(uint8_t newestIndex);

} // namespace ui_model

#endif
