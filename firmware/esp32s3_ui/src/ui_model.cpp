#include "ui_model.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace ui_model {
namespace {

const size_t kModuleCount = static_cast<size_t>(ModuleId::Count);
const Metadata kMetadata[kModuleCount] = {
    {ModuleId::FrontLight, "Lampa przednia", "Przód", Category::Lighting, Icon::Sun, true},
    {ModuleId::RearLight, "Lampa tylna", "Tył", Category::Lighting, Icon::Sun, true},
    {ModuleId::Filter, "Filtr", "Filtr", Category::Water, Icon::Water, true},
    {ModuleId::Air, "Napowietrzanie", "Powietrze", Category::Water, Icon::Air, true},
    {ModuleId::Heater, "Grzałka", "Grzałka", Category::Water, Icon::Heat, true},
    {ModuleId::Feeder, "Karmnik", "Karmnik", Category::Feeding, Icon::Food, true},
    {ModuleId::Co2, "Elektrozawór CO2", "CO2", Category::Chemistry, Icon::Gas, true},
    {ModuleId::Ph, "Pomiar pH", "pH", Category::Chemistry, Icon::Drop, false},
    {ModuleId::Ec, "Przewodność EC", "EC", Category::Chemistry, Icon::Gauge, false},
    {ModuleId::Ato, "Poziom wody / ATO", "ATO", Category::Water, Icon::Level, false},
    {ModuleId::Flow, "Przepływ", "Przepływ", Category::Water, Icon::Flow, false}};
const Metadata kInvalidMetadata = {ModuleId::Count, "Nieznany modul", "Brak",
                                   Category::Water, Icon::Gauge,      false};
const Module kInvalidModule = {Mode::Off, State::Unknown, LightMode::Off, false, false, false};
Snapshot s;
LogEntry sLogs[kLogCapacity];
uint8_t sLogWrite = 0;
uint8_t sLogCount = 0;
uint32_t sLogRevision = 0;
uint32_t sNowMs = 0;
uint32_t sFeedingMs = 0;
uint32_t sServiceMs = 0;
uint32_t sDoseMs = 0;
uint32_t sDoseStarted = 0, sMotorReportAt = 0;
uint16_t sPauseMask = 0;
uint32_t sManualMs[kModuleCount];
uint32_t sLightMs[2];
uint64_t sElapsedMs = 0;
LightMode sServiceSavedProfiles[2] = {LightMode::Day, LightMode::Daybreak};
uint16_t sClockBase = 872;
uint8_t sWeekdayBase = 0;
HistorySample sHistory[kHistoryCapacity];
HistoryClock sHistoryClock = nullptr;
uint8_t sHistoryWrite = 0, sHistoryCount = 0;
uint32_t sLastHistoryMs = 0;
HistorySample sArchive[kHistoryArchiveCapacity];
HeaterEvent sHeaterEvents[kHeaterEventCapacity];
uint16_t sArchiveWrite = 0, sArchiveCount = 0, sHeaterWrite = 0, sHeaterCount = 0;
uint32_t sLastArchiveSeconds = 0, sHistoryRevision = 0;
WaterEvent sWaterEvents[kWaterEventCapacity];
AtoDose sAtoDoses[kAtoDoseCapacity];
uint16_t sWaterWrite = 0, sWaterCount = 0;
uint16_t sAtoWrite = 0, sAtoCount = 0;
uint32_t sLastFeedingDay[2] = {UINT32_MAX, UINT32_MAX};
uint64_t sDoseGuardUntil = 0;

bool validModule(ModuleId id) { return static_cast<size_t>(id) < kModuleCount; }
bool validMode(Mode mode) { return static_cast<unsigned>(mode) <= static_cast<unsigned>(Mode::Off); }
bool validLightMode(LightMode mode) {
    return static_cast<unsigned>(mode) <= static_cast<unsigned>(LightMode::Off);
}
bool validDays(uint8_t mask) { return mask != 0 && (mask & 0x80u) == 0; }
uint32_t secondsCeiling(uint32_t milliseconds) { return (milliseconds + 999u) / 1000u; }
Validation success() { return {Error::None, "Zapisano ustawienia"}; }
Validation failure(Error error, const char *message) { return {error, message}; }

uint32_t wallTime() { return sHistoryClock ? sHistoryClock() : 0; }
uint32_t feedingDay() {
    const uint32_t stamp = wallTime();
    return stamp ? stamp / 86400u : (sClockBase + sElapsedMs / 60000u) / 1440u;
}
LogEntry *record(LogLevel level, const char *text) {
    if (!text || static_cast<unsigned>(level) > static_cast<unsigned>(LogLevel::Alarm))
        return nullptr;
    char message[96];
    snprintf(message, sizeof(message), "%s", text);
    // Coalesce repeated transport messages without allocating or hiding distinct causes.
    for (unsigned offset = 0; offset < sLogCount; ++offset) {
        LogEntry &previous = sLogs[(sLogWrite + kLogCapacity - 1u - offset) % kLogCapacity];
        if (!previous.cause && previous.level == level && !strcmp(previous.message, message) &&
            sNowMs - previous.lastAtMs <= 60000u) {
            if (previous.repeats < UINT16_MAX)
                ++previous.repeats;
            previous.lastAtMs = sNowMs;
            previous.lastWallSeconds = wallTime();
            previous.sequence = ++sLogRevision;
            return &previous;
        }
    }
    ++sLogRevision;
    LogEntry &entry = sLogs[sLogWrite];
    entry = LogEntry{};
    entry.atMs = sNowMs;
    entry.lastAtMs = sNowMs;
    entry.wallSeconds = entry.lastWallSeconds = wallTime();
    entry.repeats = 1;
    entry.sequence = sLogRevision;
    entry.level = level;
    snprintf(entry.message, sizeof(entry.message), "%s", message);
    sLogWrite = static_cast<uint8_t>((sLogWrite + 1u) % kLogCapacity);
    if (sLogCount < kLogCapacity)
        ++sLogCount;
    return &entry;
}
void recordAlarmChanges(uint16_t before) {
    static const char *names[] = {"Przegrzanie wody",           "pH poza zakresem",
                                  "Brak danych poziomu wody",   "Brak łączności ze sterownikiem",
                                  "Niski poziom wody",          "Sprzeczne wskazania pływaków",
                                  "Temperatura poza zakresem",  "EC poza zakresem",
                                  "Brak wiarygodnych pomiarów", "Niepewna dawka karmnika"};
    for (unsigned bit = 0; bit < 10; ++bit) {
        const uint16_t cause = 1u << bit;
        if (!((before ^ s.alarmFlags) & cause))
            continue;
        LogEntry *entry = nullptr;
        for (unsigned index = 0; index < sLogCount; ++index) {
            LogEntry &candidate = sLogs[(sLogWrite + kLogCapacity - 1u - index) % kLogCapacity];
            if (candidate.cause == cause) {
                entry = &candidate;
                break;
            }
        }
        const bool active = (s.alarmFlags & cause) != 0;
        if (active && !entry) {
            entry = record(bit == 0 || bit == 5 ? LogLevel::Alarm : LogLevel::Warning, names[bit]);
            if (!entry)
                continue;
            entry->cause = cause;
        } else if (active && entry) {
            if (entry->repeats < UINT16_MAX)
                ++entry->repeats;
            entry->lastAtMs = sNowMs;
            entry->lastWallSeconds = wallTime();
            entry->sequence = ++sLogRevision;
        }
        if (entry) {
            entry->active = active;
            entry->endedWallSeconds = active ? 0 : wallTime();
            entry->sequence = ++sLogRevision;
        }
    }
}
void completeDose() {
    s.lastFeedingKnown = true;
    s.lastFeedingWallSeconds = wallTime();
    s.lastFeedingSource = s.motor.source;
}

void recordModule(ModuleId id, const char *action) {
    char text[96];
    snprintf(text, sizeof(text), "%s - %s", metadata(id).title, action);
    record(LogLevel::Info, text);
}
void sampleHistory() {
    const HistorySample sample{static_cast<uint32_t>(sElapsedMs / 1000u),
                               sHistoryClock ? sHistoryClock() : 0, s.temperature, s.ph, s.ec};
    sHistory[sHistoryWrite] = sample;
    sHistoryWrite = static_cast<uint8_t>((sHistoryWrite + 1u) % kHistoryCapacity);
    if (sHistoryCount < kHistoryCapacity)
        ++sHistoryCount;
    sLastHistoryMs = sNowMs;
    if (!sArchiveCount || sample.atSeconds - sLastArchiveSeconds >= 300u) {
        sArchive[sArchiveWrite] = sample;
        sArchiveWrite = (sArchiveWrite + 1u) % kHistoryArchiveCapacity;
        if (sArchiveCount < kHistoryArchiveCapacity)
            ++sArchiveCount;
        sLastArchiveSeconds = sample.atSeconds;
    }
    ++sHistoryRevision;
}
void recordHeaterState() {
    const State state = s.modules[static_cast<size_t>(ModuleId::Heater)].state;
    const uint16_t previous = (sHeaterWrite + kHeaterEventCapacity - 1u) % kHeaterEventCapacity;
    if (sHeaterCount && sHeaterEvents[previous].state == state)
        return;
    const uint32_t at = static_cast<uint32_t>(sElapsedMs / 1000u);
    // Multiple changes inside one second have no measurable duration on this chart.
    if (sHeaterCount && sHeaterEvents[previous].atSeconds == at)
        sHeaterEvents[previous].state = state;
    else {
        sHeaterEvents[sHeaterWrite] = {at, state};
        sHeaterWrite = (sHeaterWrite + 1u) % kHeaterEventCapacity;
        if (sHeaterCount < kHeaterEventCapacity)
            ++sHeaterCount;
    }
    ++sHistoryRevision;
}
void recordWaterState() {
    const WaterState state = waterState();
    const uint16_t previous = (sWaterWrite + kWaterEventCapacity - 1u) % kWaterEventCapacity;
    if (sWaterCount && sWaterEvents[previous].state == state)
        return;
    const uint32_t at = static_cast<uint32_t>(sElapsedMs / 1000u);
    if (sWaterCount && sWaterEvents[previous].atSeconds == at)
        sWaterEvents[previous].state = state;
    else {
        sWaterEvents[sWaterWrite] = {at, state};
        sWaterWrite = (sWaterWrite + 1u) % kWaterEventCapacity;
        if (sWaterCount < kWaterEventCapacity)
            ++sWaterCount;
    }
    ++sHistoryRevision;
}

void updateDerived() {
    const uint16_t previousAlarms = s.alarmFlags;
    s.alarmFlags = s.connected ? AlarmNone : AlarmOffline;
    if (s.motor.phase == FeederPhase::Fault)
        s.alarmFlags |= AlarmFeeder;
    const Measurement measurements[] = {s.temperature, s.ph, s.ec};
    const uint16_t rangeFlags[] = {AlarmTemperatureRange, AlarmPh, AlarmEcRange};
    if (s.connected) {
        for (unsigned kind = 0; kind < 3; ++kind) {
            const Measurement &reading = measurements[kind];
            if (!reading.valid || reading.stale || !isfinite(reading.value))
                s.alarmFlags |= AlarmSensorUnknown;
            else if (reading.value < s.ranges[kind].minimum || reading.value > s.ranges[kind].maximum)
                s.alarmFlags |= rangeFlags[kind];
        }
        if (s.temperature.valid && !s.temperature.stale && s.temperature.value >= 28.5f)
            s.alarmFlags |= AlarmTemperature;
        if (!s.levelMin.valid || !s.levelMax.valid)
            s.alarmFlags |= AlarmLevelUnknown;
        else if (s.levelMin.active && s.levelMax.active)
            s.alarmFlags |= AlarmLevelFault;
        else if (s.levelMin.active)
            s.alarmFlags |= AlarmLowWater;
    }
    if (s.alarmFlags & ~previousAlarms)
        s.alarmAcknowledged = false;
    recordAlarmChanges(previousAlarms);
    s.feedingActive = sFeedingMs != 0;
    s.serviceActive = sServiceMs != 0;
    s.feedingRemainingSeconds = secondsCeiling(sFeedingMs);
    s.dispensing = sDoseMs != 0;
    s.filterPauseRemainingSeconds = s.feedingRemainingSeconds;
    s.serviceRemainingSeconds = secondsCeiling(sServiceMs);
    for (size_t i = 0; i < kModuleCount; ++i) {
        Module &item = s.modules[i];
        const ModuleId id = static_cast<ModuleId>(i);
        item.known = s.connected;
        item.transitioning = i < 2 && sLightMs[i] != 0;
        if (!s.connected) {
            item.state = State::Unknown;
            continue;
        }
        if (!kMetadata[i].controllable) {
            if (id == ModuleId::Ato) {
                item.known = s.levelMin.valid && s.levelMax.valid;
                item.state = item.known ? State::Blocked : State::Unknown;
            } else if (id == ModuleId::Ph) {
                item.known = s.ph.valid && !s.ph.stale && isfinite(s.ph.value);
                item.state =
                    !item.known ? State::Unknown : ((s.alarmFlags & AlarmPh) ? State::Warning : State::On);
            } else if (id == ModuleId::Ec) {
                item.known = s.ec.valid && !s.ec.stale && isfinite(s.ec.value);
                item.state = !item.known                     ? State::Unknown
                             : (s.alarmFlags & AlarmEcRange) ? State::Warning
                                                             : State::On;
            } else {
                item.known = s.flowValid;
                item.state = !s.flowValid ? State::Unknown : (s.flowDetected ? State::On : State::Warning);
            }
            continue;
        }
        if (s.serviceActive) {
            bool enabled = false;
            if (id == ModuleId::FrontLight || id == ModuleId::RearLight)
                enabled = id == s.service.primaryLight || s.service.secondaryLight;
            else if (id == ModuleId::Filter)
                enabled = s.service.filter;
            else if (id == ModuleId::Air)
                enabled = s.service.air;
            else if (id == ModuleId::Co2)
                enabled = s.service.co2;
            else if (id == ModuleId::Heater)
                enabled = s.service.heater && s.temperature.valid && !s.temperature.stale &&
                          s.temperature.value < 28.5f && !(s.alarmFlags & AlarmTemperature);
            item.state = enabled ? State::On : State::Blocked;
            continue;
        }
        if (feedingPauses(id)) {
            item.state = State::Blocked;
            continue;
        }
        if (id == ModuleId::Heater && ((s.alarmFlags & AlarmTemperature) || !s.temperature.valid ||
                                       s.temperature.stale || !isfinite(s.temperature.value))) {
            item.state = State::Blocked;
            continue;
        }
        if (id == ModuleId::Feeder) {
            // A feeder is momentary. A desired ON mode never means continuous dosing.
            item.state = sDoseMs != 0 ? State::On : State::Off;
            continue;
        }
        if (item.mode != Mode::Auto) {
            item.state = item.mode == Mode::On ? State::On : State::Off;
            continue;
        }
        if (id == ModuleId::FrontLight || id == ModuleId::RearLight) {
            const bool scheduled = scheduleContains(s.schedules[i], demoMinute(), weekday());
            item.lightMode = !scheduled ? LightMode::Off
                             : s.lightPrograms[i].enabled && s.schedules[i].mode == ScheduleMode::Timed
                                 ? lightModeFor(s.schedules[i], s.lightPrograms[i], demoMinute(), weekday())
                                 : id == ModuleId::FrontLight ? LightMode::Day : LightMode::Daybreak;
            item.state = scheduled && item.lightMode != LightMode::Off ? State::On : State::Off;
        } else if (id == ModuleId::Heater) {
            if (s.temperature.value < s.heater.target - s.heater.hysteresis)
                item.state = State::On;
            else if (s.temperature.value >= s.heater.target || item.state != State::On)
                item.state = State::Off;
        } else {
            item.state = scheduleContains(s.schedules[i], demoMinute(), weekday()) ? State::On : State::Off;
        }
    }
    recordHeaterState();
    recordWaterState();
}

void finishFeeding(bool cancelled) {
    sFeedingMs = 0;
    s.feedingDurationSeconds = 0;
    sDoseMs = 0;
    if (cancelled && (s.motor.phase == FeederPhase::Moving || s.motor.phase == FeederPhase::Waiting))
        s.motor.phase = FeederPhase::Idle;
    for (size_t i = 0; i < kModuleCount; ++i)
        s.modules[i].mode = Mode::Auto;
    if (s.scenario == Scenario::Feeding)
        s.scenario = Scenario::DemoHealthy;
    record(LogLevel::Info,
           cancelled ? "Anulowano karmienie; przywrocono tryby" : "Koniec karmienia; przywrocono tryby");
}

void finishService(bool cancelled) {
    sServiceMs = 0;
    for (unsigned lamp = 0; lamp < 2; ++lamp)
        s.modules[lamp].lightMode = sServiceSavedProfiles[lamp];
    s.service = ServiceConfig{ModuleId::FrontLight, false, false, false, false, false};
    for (size_t i = 0; i < kModuleCount; ++i)
        s.modules[i].mode = Mode::Auto;
    if (s.scenario == Scenario::Service)
        s.scenario = Scenario::DemoHealthy;
    record(LogLevel::Info,
           cancelled ? "Zakonczono serwis; tryby AUTO" : "Koniec czasu serwisu; tryby AUTO");
}

Validation canChangeModule(ModuleId id) {
    if (!validModule(id))
        return failure(Error::InvalidModule, "Nieznany modul");
    if (!metadata(id).controllable)
        return failure(Error::NotControllable, id == ModuleId::Ato ? "ATO: brak przypisanego wyjscia pompy"
                                                                   : "Ten modul udostepnia pomiar");
    if (!s.connected)
        return failure(Error::NotControllable, "Scenariusz offline: stan modulu nieznany");
    if (sFeedingMs != 0 || sServiceMs != 0)
        return failure(Error::Busy, "Najpierw zakoncz karmienie lub serwis");
    if (module(id).transitioning)
        return failure(Error::Busy, "Poczekaj na zakończenie zmiany profilu lampy");
    return success();
}

} // namespace

void setHistoryClock(HistoryClock clock) { sHistoryClock = clock; }
void init(uint32_t nowMs) {
    s = Snapshot{};
    memset(sLogs, 0, sizeof(sLogs));
    sLogWrite = 0;
    sLogCount = 0;
    sNowMs = nowMs;
    sFeedingMs = sServiceMs = sDoseMs = 0;
    memset(sManualMs, 0, sizeof(sManualMs));
    memset(sLightMs, 0, sizeof(sLightMs));
    sElapsedMs = 0;
    sClockBase = 872;
    sWeekdayBase = 0;
    sHistoryWrite = sHistoryCount = 0;
    sLastHistoryMs = nowMs;
    sArchiveWrite = sArchiveCount = sHeaterWrite = sHeaterCount = 0;
    sLastArchiveSeconds = 0;
    sHistoryRevision = 0;
    sWaterWrite = sWaterCount = 0;
    sAtoWrite = sAtoCount = 0;
    sLastFeedingDay[0] = sLastFeedingDay[1] = UINT32_MAX;
    sDoseGuardUntil = 0;
    s.service = ServiceConfig{ModuleId::FrontLight, false, false, false, false, false};
    s.demo = true;
    s.feeding = FeedingConfig{0x7f, 2, {540, 1080}, 300};
    s.feeder = FeederConfig{90, outputBit(ModuleId::Filter) | outputBit(ModuleId::Co2)};
    s.motor.source = FeederSource::LocalModel;
    s.heater = HeaterConfig{25.0f, 0.3f};
    s.ranges[0] = SensorRange{24.0f, 26.0f};
    s.ranges[1] = SensorRange{6.5f, 7.5f};
    s.ranges[2] = SensorRange{300.0f, 700.0f};
    for (size_t i = 0; i < kModuleCount; ++i) {
        s.modules[i] =
            Module{Mode::Auto, State::Unknown, LightMode::Off, kMetadata[i].controllable, false, false};
        s.schedules[i] = Schedule{540, 1260, ScheduleMode::Disabled, 0x7f};
    }
    s.schedules[static_cast<size_t>(ModuleId::FrontLight)].mode = ScheduleMode::Timed;
    s.schedules[static_cast<size_t>(ModuleId::RearLight)].mode = ScheduleMode::Timed;
    s.lightPrograms[0] = LightProgram{600, 1080, 1140, true};
    s.lightPrograms[1] = LightProgram{600, 1080, 1140, true};
    s.schedules[static_cast<size_t>(ModuleId::Filter)].mode = ScheduleMode::Always;
    s.schedules[static_cast<size_t>(ModuleId::Heater)].mode = ScheduleMode::Always;
    s.schedules[static_cast<size_t>(ModuleId::Air)] = Schedule{1320, 360, ScheduleMode::Timed, 0x7f};
    s.schedules[static_cast<size_t>(ModuleId::Co2)] = Schedule{450, 1170, ScheduleMode::Timed, 0x7f};
    s.modules[static_cast<size_t>(ModuleId::FrontLight)].lightMode = LightMode::Day;
    s.modules[static_cast<size_t>(ModuleId::RearLight)].lightMode = LightMode::Daybreak;
    setScenario(Scenario::DemoHealthy);
    record(LogLevel::Info, "Uruchomiono panel");
    sampleHistory();
}

void tick(uint32_t nowMs) {
    // Unsigned subtraction also handles the normal millis() rollover.
    const uint32_t elapsed = nowMs - sNowMs;
    sNowMs = nowMs;
    const uint16_t oldMinute = demoMinute();
    const uint8_t oldWeekday = weekday();
    sElapsedMs += elapsed;
    bool timersChanged = oldMinute != demoMinute() || oldWeekday != weekday();
    const uint32_t cooldown = sDoseGuardUntil > sElapsedMs
                                  ? static_cast<uint32_t>((sDoseGuardUntil - sElapsedMs + 999u) / 1000u)
                                  : 0;
    timersChanged |= cooldown != s.feedingCooldownSeconds;
    s.feedingCooldownSeconds = cooldown;
    if (static_cast<uint32_t>(nowMs - sLastHistoryMs) >= 15000u) {
        sampleHistory();
        ++s.revision;
    }
    bool startedFeeding = false;
    // A clock jump never replays missed doses; only an observed minute boundary may start one.
    if (timersChanged && elapsed <= 90000u && s.connected && !s.serviceActive && !s.feedingActive &&
        (s.feeding.dayMask & (1u << weekday()))) {
        const uint32_t day = feedingDay();
        for (uint8_t feed = 0; feed < s.feeding.count; ++feed)
            if (demoMinute() == s.feeding.times[feed] && sLastFeedingDay[feed] != day) {
                sLastFeedingDay[feed] = day;
                startedFeeding = startFeeding(s.feeding.pauseSeconds, true).ok();
                if (!startedFeeding && s.feedingCooldownSeconds)
                    record(LogLevel::Info, "Pominięto dawkę automatyczną: niedawno rozpoczęto karmienie");
                break;
            }
    }
    for (size_t i = 0; i < kModuleCount; ++i) {
        if (sManualMs[i]) {
            sManualMs[i] = elapsed >= sManualMs[i] ? 0 : sManualMs[i] - elapsed;
            const uint32_t remaining = secondsCeiling(sManualMs[i]);
            timersChanged |= remaining != s.manualRemainingSeconds[i];
            s.manualRemainingSeconds[i] = remaining;
            if (!remaining) {
                s.modules[i].mode = Mode::Auto;
                recordModule(static_cast<ModuleId>(i), "powrót AUTO po czasie");
            }
        }
        if (i < 2 && sLightMs[i]) {
            sLightMs[i] = elapsed >= sLightMs[i] ? 0 : sLightMs[i] - elapsed;
            timersChanged |= sLightMs[i] == 0;
        }
    }
    if (sFeedingMs == 0 && sServiceMs == 0) {
        if (timersChanged) {
            updateDerived();
            ++s.revision;
        }
        return;
    }
    const uint32_t oldFeedingSeconds = s.feedingRemainingSeconds;
    const uint32_t oldServiceSeconds = s.serviceRemainingSeconds;
    const bool dosingBefore = sDoseMs != 0;
    const FeederPhase phaseBefore = s.motor.phase;
    if (sDoseMs != 0 && !startedFeeding) {
        const uint32_t age = sNowMs - sDoseStarted;
        if (s.motor.source == FeederSource::LocalModel) {
            s.motor.phase = age < 150 ? FeederPhase::Waiting : FeederPhase::Moving;
            s.motor.positionValid = age >= 150;
            s.motor.positionDegrees = s.motor.targetDegrees * fminf(1.0f, age / 2000.0f);
            if (age >= 2000) {
                s.motor.phase = FeederPhase::Complete;
                s.motor.positionDegrees = s.motor.targetDegrees;
                sDoseMs = 0;
                completeDose();
                record(LogLevel::Info, "Model karmnika: obrót zakończony");
            }
        } else if ((s.motor.phase == FeederPhase::Waiting && age >= 8000u) || age >= 120000u ||
                   (s.motor.phase == FeederPhase::Moving && sNowMs - sMotorReportAt >= 8000u)) {
            s.motor.phase = FeederPhase::Fault;
            record(LogLevel::Alarm,
                   "Karmnik: brak potwierdzenia pozycji / nie ponawiaj dawki automatycznie");
            finishFeeding(true);
        }
    }
    if (sFeedingMs != 0 && !startedFeeding) {
        if (elapsed >= sFeedingMs && sDoseMs == 0)
            finishFeeding(false);
        else
            sFeedingMs = elapsed >= sFeedingMs ? 1 : sFeedingMs - elapsed;
    }
    if (sServiceMs != 0) {
        if (elapsed >= sServiceMs)
            finishService(false);
        else
            sServiceMs -= elapsed;
    }
    updateDerived();
    if (oldFeedingSeconds != s.feedingRemainingSeconds || oldServiceSeconds != s.serviceRemainingSeconds ||
        dosingBefore != (sDoseMs != 0) || phaseBefore != s.motor.phase)
        ++s.revision;
}

void setScenario(Scenario scenario) {
    if (static_cast<unsigned>(scenario) > static_cast<unsigned>(Scenario::LevelFault))
        return;
    if (sFeedingMs != 0)
        finishFeeding(true);
    if (sServiceMs != 0)
        finishService(true);
    memset(sManualMs, 0, sizeof(sManualMs));
    memset(s.manualRemainingSeconds, 0, sizeof(s.manualRemainingSeconds));
    memset(sLightMs, 0, sizeof(sLightMs));
    for (size_t i = 0; i < kModuleCount; ++i)
        s.modules[i].mode = Mode::Auto;
    s.scenario = scenario;
    s.connected = scenario != Scenario::Offline;
    s.demo = true;
    s.temperature = Measurement{24.8f, s.connected, !s.connected};
    s.ph = Measurement{6.92f, s.connected, !s.connected};
    s.ec = Measurement{460.0f, s.connected, !s.connected};
    s.flowDetected = s.connected;
    s.flowValid = s.connected;
    s.levelMin = LevelInput{false, s.connected};
    s.levelMax = LevelInput{true, s.connected};
    s.alarmAcknowledged = false;
    if (scenario == Scenario::Alarm) {
        s.temperature.value = 28.7f;
        s.ph.value = 8.30f;
        s.levelMax.valid = false;
    }
    if (scenario == Scenario::LowWater) {
        s.levelMin.active = true;
        s.levelMax.active = false;
    } else if (scenario == Scenario::LevelFault) {
        s.levelMin.active = true;
        s.levelMax.active = true;
    }
    updateDerived();
    ++s.revision;
    if (!s.alarmFlags)
        record(LogLevel::Info, scenarioName(scenario));
    if (scenario == Scenario::Feeding)
        startFeeding(s.feeding.pauseSeconds, true);
    else if (scenario == Scenario::Service)
        startService(900);
}

const Snapshot &snapshot() { return s; }
const Metadata &metadata(ModuleId id) {
    return validModule(id) ? kMetadata[static_cast<size_t>(id)] : kInvalidMetadata;
}
const Module &module(ModuleId id) {
    return validModule(id) ? s.modules[static_cast<size_t>(id)] : kInvalidModule;
}

const char *modeName(Mode mode) {
    switch (mode) {
    case Mode::Auto:
        return "AUTO";
    case Mode::On:
        return "WŁ.";
    case Mode::Off:
        return "WYŁ.";
    }
    return "?";
}
const char *stateName(State state) {
    switch (state) {
    case State::Unknown:
        return "Nieznany";
    case State::On:
        return "Aktywny";
    case State::Off:
        return "Wylaczony";
    case State::Blocked:
        return "Wstrzymany";
    case State::Warning:
        return "Uwaga";
    }
    return "?";
}
const char *lightModeName(LightMode mode) {
    switch (mode) {
    case LightMode::Day:
        return "Dzień";
    case LightMode::Daybreak:
        return "Świt / zmierzch";
    case LightMode::Night:
        return "Noc";
    case LightMode::Off:
        return "Wyłączona";
    }
    return "?";
}
const char *scenarioName(Scenario scenario) {
    switch (scenario) {
    case Scenario::DemoHealthy:
        return "Normalna praca";
    case Scenario::Offline:
        return "Brak polaczenia";
    case Scenario::Alarm:
        return "Aktywne alarmy";
    case Scenario::Feeding:
        return "Karmienie";
    case Scenario::Service:
        return "Serwis";
    case Scenario::LowWater:
        return "Niski poziom wody";
    case Scenario::LevelFault:
        return "Sprzeczne pływaki";
    }
    return "Nieznany scenariusz";
}

Validation validateSchedule(const Schedule &draft) {
    if (static_cast<unsigned>(draft.mode) > static_cast<unsigned>(ScheduleMode::Always))
        return failure(Error::InvalidMode, "Nieprawidlowy tryb harmonogramu");
    if (draft.startMinute >= 1440 || draft.endMinute >= 1440)
        return failure(Error::InvalidTime, "Godzina musi nalezec do zakresu 00:00-23:59");
    if (!validDays(draft.dayMask))
        return failure(Error::InvalidDays, "Wybierz co najmniej jeden dzien tygodnia");
    if (draft.mode == ScheduleMode::Timed && draft.startMinute == draft.endMinute)
        return failure(Error::EmptyInterval, "Poczatek i koniec nie moga byc jednakowe");
    return success();
}

Validation applySchedule(ModuleId id, const Schedule &draft) {
    if (!validModule(id))
        return failure(Error::InvalidModule, "Nieznany modul");
    if (!metadata(id).controllable)
        return failure(Error::NotControllable, "Modul pomiarowy nie ma harmonogramu wyjscia");
    const Validation result = validateSchedule(draft);
    if (!result.ok())
        return result;
    if (id == ModuleId::FrontLight || id == ModuleId::RearLight) {
        const Validation program = validateLightProgram(draft, s.lightPrograms[static_cast<size_t>(id)]);
        if (!program.ok())
            return program;
    }
    s.schedules[static_cast<size_t>(id)] = draft;
    ++s.revision;
    recordModule(id, "zapisano harmonogram w RAM");
    return success();
}

bool scheduleContains(const Schedule &schedule, uint16_t minute, uint8_t weekday) {
    if (!validateSchedule(schedule).ok() || minute >= 1440 || weekday >= 7 ||
        schedule.mode == ScheduleMode::Disabled)
        return false;
    if (schedule.mode == ScheduleMode::Always)
        return (schedule.dayMask & (1u << weekday)) != 0;
    if (schedule.startMinute < schedule.endMinute)
        return (schedule.dayMask & (1u << weekday)) != 0 && minute >= schedule.startMinute &&
               minute < schedule.endMinute;
    // The after-midnight tail belongs to the previous day's enabled interval.
    if (minute >= schedule.startMinute)
        return (schedule.dayMask & (1u << weekday)) != 0;
    const uint8_t previousDay = static_cast<uint8_t>((weekday + 6u) % 7u);
    return minute < schedule.endMinute && (schedule.dayMask & (1u << previousDay)) != 0;
}
Validation validateLightProgram(const Schedule &schedule, const LightProgram &program) {
    if (program.dayStartMinute >= 1440 || program.duskStartMinute >= 1440 ||
        program.nightStartMinute >= 1440)
        return failure(Error::InvalidTime, "Profil lampy: godzina poza zakresem 00:00–23:59");
    if (!program.enabled || schedule.mode != ScheduleMode::Timed)
        return success();
    const Validation scheduleResult = validateSchedule(schedule);
    if (!scheduleResult.ok())
        return scheduleResult;
    const uint16_t duration = static_cast<uint16_t>((schedule.endMinute + 1440u -
                                                     schedule.startMinute) % 1440u);
    const uint16_t day = static_cast<uint16_t>((program.dayStartMinute + 1440u -
                                                schedule.startMinute) % 1440u);
    const uint16_t dusk = static_cast<uint16_t>((program.duskStartMinute + 1440u -
                                                 schedule.startMinute) % 1440u);
    const uint16_t night = static_cast<uint16_t>((program.nightStartMinute + 1440u -
                                                  schedule.startMinute) % 1440u);
    if (duration < 4 || day == 0 || day >= dusk || dusk >= night || night >= duration)
        return failure(Error::InvalidTime,
                       "Ustaw kolejno: Świt → Dzień → Zmierzch → Noc → Wyłączenie");
    return success();
}
LightMode lightModeFor(const Schedule &schedule, const LightProgram &program, uint16_t minute,
                       uint8_t day) {
    if (!program.enabled || schedule.mode != ScheduleMode::Timed ||
        !scheduleContains(schedule, minute, day) || !validateLightProgram(schedule, program).ok())
        return LightMode::Off;
    const uint16_t elapsed = static_cast<uint16_t>((minute + 1440u - schedule.startMinute) % 1440u);
    const uint16_t dayStart = static_cast<uint16_t>((program.dayStartMinute + 1440u -
                                                     schedule.startMinute) % 1440u);
    const uint16_t duskStart = static_cast<uint16_t>((program.duskStartMinute + 1440u -
                                                      schedule.startMinute) % 1440u);
    const uint16_t nightStart = static_cast<uint16_t>((program.nightStartMinute + 1440u -
                                                       schedule.startMinute) % 1440u);
    return elapsed < dayStart ? LightMode::Daybreak
           : elapsed < duskStart ? LightMode::Day
           : elapsed < nightStart ? LightMode::Daybreak
                                  : LightMode::Night;
}

Validation validateFeeding(const FeedingConfig &draft) {
    if (!validDays(draft.dayMask))
        return failure(Error::InvalidDays, "Wybierz co najmniej jeden dzien karmienia");
    if (draft.count != 1 && draft.count != 2)
        return failure(Error::InvalidCount, "Wybierz jedno lub dwa karmienia dziennie");
    for (uint8_t i = 0; i < draft.count; ++i)
        if (draft.times[i] >= 1440)
            return failure(Error::InvalidTime, "Nieprawidlowa godzina karmienia");
    if (draft.count == 2 && draft.times[0] == draft.times[1])
        return failure(Error::DuplicateTime, "Godziny karmienia musza sie roznic");
    if (draft.pauseSeconds == 0 || draft.pauseSeconds > kMaximumPauseSeconds)
        return failure(Error::InvalidDuration, "Przerwa filtra: od 1 do 3600 sekund");
    return success();
}

Validation applyFeeding(const FeedingConfig &draft) {
    const Validation result = validateFeeding(draft);
    if (!result.ok())
        return result;
    s.feeding = draft;
    ++s.revision;
    record(LogLevel::Info, "Zapisano harmonogram karmienia");
    return success();
}

Validation validateHeater(const HeaterConfig &draft) {
    if (!isfinite(draft.target) || draft.target < 18.0f || draft.target > 28.0f)
        return failure(Error::InvalidTarget, "Cel: 18,0–28,0°C; odcięcie bezpieczeństwa 28,5°C");
    if (!isfinite(draft.hysteresis) || draft.hysteresis < 0.1f || draft.hysteresis > 2.0f)
        return failure(Error::InvalidHysteresis, "Histereza: 0.1-2.0 C");
    return success();
}

Validation applyHeater(const HeaterConfig &draft) {
    const Validation result = validateHeater(draft);
    if (!result.ok())
        return result;
    s.heater = draft;
    updateDerived();
    ++s.revision;
    record(LogLevel::Info, "Zapisano nastawy temperatury");
    return success();
}
Validation validateRange(uint8_t kind, const SensorRange &range) {
    if (kind > 2 || !isfinite(range.minimum) || !isfinite(range.maximum) || range.minimum >= range.maximum)
        return failure(Error::InvalidRange, "Dolna granica musi być mniejsza od górnej");
    const float lower[] = {10.0f, 0.0f, 0.0f};
    const float upper[] = {28.0f, 14.0f, 10000.0f};
    if (range.minimum < lower[kind] || range.maximum > upper[kind])
        return failure(Error::InvalidRange, "Zakres poza granicami czujnika lub ochrony temperatury");
    return success();
}
Validation applyRange(uint8_t kind, const SensorRange &range) {
    const Validation result = validateRange(kind, range);
    if (!result.ok())
        return result;
    s.ranges[kind] = range;
    s.alarmAcknowledged = false;
    updateDerived();
    ++s.revision;
    record(LogLevel::Info, "Zmieniono zakres normy pomiaru");
    return success();
}

Validation setModuleMode(ModuleId id, Mode mode) {
    const Validation result = canChangeModule(id);
    if (!result.ok())
        return result;
    if (!validMode(mode))
        return failure(Error::InvalidMode, "Nieprawidlowy tryb modulu");
    if (id == ModuleId::Feeder && mode == Mode::On)
        return failure(Error::NotControllable, "Dawkowanie uruchom przyciskiem karmienia");
    s.modules[static_cast<size_t>(id)].mode = mode;
    updateDerived();
    ++s.revision;
    recordModule(id, modeName(mode));
    return success();
}

Validation toggleModule(ModuleId id) {
    if (!validModule(id))
        return failure(Error::InvalidModule, "Nieznany modul");
    return setModuleMode(id, module(id).state == State::On ? Mode::Off : Mode::On);
}

uint16_t demoMinute() { return static_cast<uint16_t>((sClockBase + sElapsedMs / 60000u) % 1440u); }
uint8_t weekday() {
    return static_cast<uint8_t>((sWeekdayBase + (sClockBase + sElapsedMs / 60000u) / 1440u) % 7u);
}
void setClock(uint16_t minute, uint8_t day) {
    if (minute >= 1440 || day > 6)
        return;
    const uint32_t passedMinutes = sElapsedMs / 60000u;
    sClockBase = static_cast<uint16_t>((minute + 1440u - passedMinutes % 1440u) % 1440u);
    const uint32_t days = (sClockBase + passedMinutes) / 1440u;
    sWeekdayBase = static_cast<uint8_t>((day + 7u - days % 7u) % 7u);
    updateDerived();
    ++s.revision;
}

Validation setTimedMode(ModuleId id, Mode mode, uint32_t seconds) {
    if (mode != Mode::Auto && (seconds < 30 || seconds > 86400))
        return failure(Error::InvalidDuration, "Wymuszenie: 30 s–24 h");
    Validation result = setModuleMode(id, mode);
    if (!result.ok())
        return result;
    const size_t i = static_cast<size_t>(id);
    sManualMs[i] = mode == Mode::Auto ? 0 : seconds * 1000u;
    s.manualRemainingSeconds[i] = secondsCeiling(sManualMs[i]);
    return result;
}

Validation setLightMode(ModuleId id, LightMode mode) {
    if (id != ModuleId::FrontLight && id != ModuleId::RearLight)
        return failure(Error::InvalidModule, "Profil dostepny tylko dla lamp");
    const Validation result = canChangeModule(id);
    if (!result.ok())
        return result;
    if (!validLightMode(mode))
        return failure(Error::InvalidMode, "Nieprawidlowy profil lampy");
    Module &item = s.modules[static_cast<size_t>(id)];
    item.lightMode = mode;
    item.mode = mode == LightMode::Off ? Mode::Off : Mode::On;
    sLightMs[static_cast<size_t>(id)] = 8500;
    sManualMs[static_cast<size_t>(id)] = 900000;
    s.manualRemainingSeconds[static_cast<size_t>(id)] = 900;
    updateDerived();
    ++s.revision;
    recordModule(id, lightModeName(mode));
    return success();
}

static void beginFeeding(uint16_t pauseSeconds, bool dose, bool additional) {
    memset(sManualMs, 0, sizeof(sManualMs));
    memset(s.manualRemainingSeconds, 0, sizeof(s.manualRemainingSeconds));
    sFeedingMs = static_cast<uint32_t>(pauseSeconds) * 1000u;
    s.feedingDurationSeconds = pauseSeconds;
    sDoseMs = dose ? 1 : 0;
    sDoseStarted = sMotorReportAt = sNowMs;
    sPauseMask = s.feeder.pauseMask;
    if (++s.motor.job == 0)
        ++s.motor.job;
    s.motor.targetDegrees = s.feeder.rotationDegrees;
    s.motor.positionDegrees = 0;
    s.motor.positionValid = false;
    s.motor.phase = dose ? FeederPhase::Waiting : FeederPhase::Idle;
    if (dose) {
        sDoseGuardUntil = sElapsedMs + static_cast<uint64_t>(s.feeder.minimumIntervalMinutes) * 60000u;
        s.feedingCooldownSeconds = static_cast<uint32_t>(s.feeder.minimumIntervalMinutes) * 60u;
    }
    updateDerived();
    ++s.revision;
    record(LogLevel::Info,
           additional ? "Karmienie: dodatkowa dawka po PIN administratora"
           : dose ? "Karmienie: jedna dawka / pauza wybranych wyjść"
                  : "Pauza wybranych wyjść bez dawki");
}

Validation startFeeding(uint16_t pauseSeconds, bool dose) {
    if (!s.connected)
        return failure(Error::NotControllable, "Brak polaczenia ze sterownikiem");
    if (sFeedingMs != 0 || sServiceMs != 0)
        return failure(Error::Busy, "Karmienie lub serwis jest juz aktywny");
    if (s.motor.phase == FeederPhase::Fault)
        return failure(Error::Busy, "Niepewna dawka / sprawdź karmnik w Centrum uwagi");
    if (pauseSeconds == 0 || pauseSeconds > kMaximumPauseSeconds)
        return failure(Error::InvalidDuration, "Czas karmienia: od 1 do 3600 sekund");
    if (dose && sElapsedMs < sDoseGuardUntil)
        return failure(Error::Busy, "Ochrona przed kolejną dawką / poczekaj na koniec odstępu");
    beginFeeding(pauseSeconds, dose, false);
    return success();
}
Validation startAdditionalDose(uint16_t pauseSeconds, uint32_t completedJob, const char *adminPin) {
    if (!adminPin || strcmp(adminPin, "2177") != 0)
        return failure(Error::InvalidPin, "Błędny PIN administratora");
    if (!s.connected)
        return failure(Error::NotControllable, "Brak polaczenia ze sterownikiem");
    if (completedJob == 0 || completedJob != s.motor.job)
        return failure(Error::Busy, "Stan karmnika zmienił się / rozpocznij ponownie");
    if (sServiceMs != 0 || sDoseMs != 0 || s.motor.phase != FeederPhase::Complete ||
        !s.lastFeedingKnown)
        return failure(Error::Busy, "Poczekaj na potwierdzenie zakończenia poprzedniej dawki");
    if (pauseSeconds == 0 || pauseSeconds > kMaximumPauseSeconds)
        return failure(Error::InvalidDuration, "Czas karmienia: od 1 do 3600 sekund");
    if (sElapsedMs >= sDoseGuardUntil)
        return failure(Error::Busy, "Odstęp minął / rozpocznij zwykłe karmienie");
    // A confirmed dose may be repeated once per PIN entry; the new dose restarts the pause and guard.
    beginFeeding(pauseSeconds, true, true);
    return success();
}
Validation validateFeeder(const FeederConfig &draft) {
    if (draft.rotationDegrees < 1 || draft.rotationDegrees > 360)
        return failure(Error::InvalidTarget, "Obrót karmnika: 1–360 stopni");
    if (draft.pauseMask & ~kFeedingOutputMask)
        return failure(Error::InvalidModule, "Nieprawidłowe wyjście do pauzy karmienia");
    if (draft.minimumIntervalMinutes < 1 || draft.minimumIntervalMinutes > 60)
        return failure(Error::InvalidDuration, "Odstęp między dawkami: 1–60 minut");
    return success();
}
NextFeeding nextFeeding() {
    const uint16_t now = demoMinute();
    const uint32_t day = feedingDay();
    for (unsigned offset = 0; offset <= 7; ++offset) {
        if (!(s.feeding.dayMask & (1u << ((weekday() + offset) % 7))))
            continue;
        uint16_t best = UINT16_MAX;
        for (unsigned index = 0; index < s.feeding.count; ++index) {
            const uint16_t minute = s.feeding.times[index];
            if ((!offset && minute <= now) || sLastFeedingDay[index] == day + offset)
                continue;
            const uint32_t stamp = wallTime();
            const uint32_t second = stamp ? stamp % 60u : (sElapsedMs / 1000u) % 60u;
            const uint32_t until = (offset * 1440u + minute - now) * 60u - second;
            if (until < s.feedingCooldownSeconds)
                continue;
            if (minute < best)
                best = minute;
        }
        if (best != UINT16_MAX)
            return {true, best, static_cast<uint8_t>(offset)};
    }
    return {false, 0, 0};
}
bool feedingPauses(ModuleId id) {
    return validModule(id) && s.feedingActive && (sPauseMask & outputBit(id));
}
void setFeederSource(FeederSource source) {
    if (!s.feedingActive && static_cast<unsigned>(source) <= 1)
        s.motor.source = source;
}
bool applyFeederPosition(uint32_t job, float degrees, bool complete) {
    if (!s.feedingActive || !sDoseMs || s.motor.source != FeederSource::Controller || job != s.motor.job ||
        !isfinite(degrees) || degrees < 0 || degrees > s.motor.targetDegrees ||
        (s.motor.positionValid && degrees < s.motor.positionDegrees) ||
        (complete && fabsf(degrees - s.motor.targetDegrees) > 0.5f))
        return false;
    s.motor.positionValid = true;
    s.motor.positionDegrees = degrees;
    s.motor.phase = complete ? FeederPhase::Complete : FeederPhase::Moving;
    sMotorReportAt = sNowMs;
    if (complete) {
        sDoseMs = 0;
        completeDose();
        record(LogLevel::Info, "Karmnik: sterownik potwierdził zakończenie obrotu");
    }
    updateDerived();
    ++s.revision;
    return true;
}
Validation acknowledgeFeederFault() {
    if (s.motor.phase != FeederPhase::Fault || s.feedingActive)
        return failure(Error::Busy, "Brak niepewnej dawki do potwierdzenia");
    s.motor.phase = FeederPhase::Idle;
    updateDerived();
    ++s.revision;
    record(LogLevel::Info, "Użytkownik potwierdził sprawdzenie karmnika / nie ponowiono dawki");
    return success();
}

void cancelFeeding() {
    if (sFeedingMs == 0)
        return;
    finishFeeding(true);
    updateDerived();
    ++s.revision;
}

Validation startService(uint16_t seconds) {
    if (!s.connected)
        return failure(Error::NotControllable, "Brak polaczenia ze sterownikiem");
    if (sFeedingMs != 0 || sServiceMs != 0)
        return failure(Error::Busy, "Karmienie lub serwis jest juz aktywny");
    if (seconds == 0 || seconds > kMaximumServiceSeconds)
        return failure(Error::InvalidDuration, "Serwis: od 1 do 7200 sekund");
    sServiceMs = static_cast<uint32_t>(seconds) * 1000u;
    s.service.secondaryLight = s.service.filter = s.service.air = s.service.heater = s.service.co2 = false;
    // The working light is a service override, independent of the saved Aquael night/off profile.
    for (size_t lamp = 0; lamp < 2; ++lamp) {
        sServiceSavedProfiles[lamp] = s.modules[lamp].lightMode;
        s.modules[lamp].lightMode = LightMode::Day;
        sLightMs[lamp] = 0;
    }
    memset(sManualMs, 0, sizeof(sManualMs));
    memset(s.manualRemainingSeconds, 0, sizeof(s.manualRemainingSeconds));
    updateDerived();
    ++s.revision;
    record(LogLevel::Info, "Serwis; jedna lampa ON, pozostałe wyjścia OFF; powrót AUTO po czasie");
    return success();
}

void cancelService() {
    if (sServiceMs == 0)
        return;
    finishService(true);
    updateDerived();
    ++s.revision;
}

Validation chooseServiceLight(ModuleId id) {
    if (id != ModuleId::FrontLight && id != ModuleId::RearLight)
        return failure(Error::InvalidModule, "Wybierz lampę przednią lub tylną");
    s.service.primaryLight = id;
    updateDerived();
    ++s.revision;
    if (s.serviceActive)
        recordModule(id, "podstawowe światło serwisowe");
    return success();
}
Validation setServiceOutput(ModuleId id, bool enabled) {
    if (!s.connected || !s.serviceActive)
        return failure(Error::Busy, "Najpierw rozpocznij serwis");
    if (id == s.service.primaryLight)
        return failure(Error::ProtectedLight, "Lampa podstawowa pozostaje włączona podczas serwisu");
    bool *setting = nullptr;
    if (id == ModuleId::FrontLight || id == ModuleId::RearLight)
        setting = &s.service.secondaryLight;
    else if (id == ModuleId::Filter)
        setting = &s.service.filter;
    else if (id == ModuleId::Air)
        setting = &s.service.air;
    else if (id == ModuleId::Co2)
        setting = &s.service.co2;
    else if (id == ModuleId::Heater) {
        if (enabled && (!s.temperature.valid || s.temperature.stale || !isfinite(s.temperature.value) ||
                        s.temperature.value >= 28.5f))
            return failure(Error::UnsafeTemperature, "Grzanie zablokowane: sprawdź pomiar temperatury");
        setting = &s.service.heater;
    }
    if (!setting)
        return failure(Error::NotControllable, "To wyjście nie jest dostępne w serwisie");
    *setting = enabled;
    updateDerived();
    ++s.revision;
    recordModule(id, enabled ? "ON w serwisie" : "OFF w serwisie");
    return success();
}
WaterState waterState() {
    if (!s.connected || !s.levelMin.valid || !s.levelMax.valid)
        return WaterState::Unknown;
    if (s.levelMin.active && s.levelMax.active)
        return WaterState::Conflict;
    if (s.levelMin.active)
        return WaterState::Low;
    return s.levelMax.active ? WaterState::Normal : WaterState::Between;
}
Health health() {
    if (s.alarmFlags & AlarmTemperature)
        return {Severity::Critical, "Za wysoka temperatura", "Grzanie odcięte / sprawdź akwarium"};
    if (waterState() == WaterState::Conflict)
        return {Severity::Critical, "Sprzeczne wskazania pływaków", "MIN i MAX aktywne / sprawdź czujniki"};
    if (!s.connected)
        return {Severity::Warning, "Brak łącza ze sterownikiem", "Pomiary i stany wyjść są nieznane"};
    if (s.alarmFlags & AlarmFeeder)
        return {Severity::Warning, "Karmnik bez potwierdzenia",
                "Sprawdź silnik i łącze / dawka nieponowiona"};
    if (waterState() == WaterState::Unknown)
        return {Severity::Warning, "Brak danych poziomu wody", "Dolewanie zablokowane / sprawdź MIN i MAX"};
    if (waterState() == WaterState::Low)
        return {Severity::Warning, "Niski poziom wody", "Sprawdź wodę / pompa ATO nie jest przypisana"};
    if (s.alarmFlags & AlarmPh)
        return {Severity::Warning, "pH poza zakresem", "Sprawdź pomiar i warunki w akwarium"};
    if (s.alarmFlags & AlarmTemperatureRange)
        return {Severity::Warning, "Temperatura poza zakresem", "Sprawdź ustawiony zakres i termostat"};
    if (s.alarmFlags & AlarmEcRange)
        return {Severity::Warning, "EC poza zakresem", "Sprawdź przewodność i ustawione granice"};
    if (!s.temperature.valid || s.temperature.stale || !s.ph.valid || s.ph.stale || !s.ec.valid ||
        s.ec.stale)
        return {Severity::Warning, "Niepełne dane z czujników", "Sprawdź wiarygodność odczytów"};
    return {Severity::Normal, "Wszystko w normie", "Czujniki nie zgłaszają alarmów / szczegóły →"};
}
DomainConfig configuration() {
    DomainConfig config{};
    memcpy(config.schedules, s.schedules, sizeof(config.schedules));
    config.feeding = s.feeding;
    config.feeder = s.feeder;
    memcpy(config.lightPrograms, s.lightPrograms, sizeof(config.lightPrograms));
    config.heater = s.heater;
    memcpy(config.ranges, s.ranges, sizeof(config.ranges));
    return config;
}
Validation validateConfiguration(const DomainConfig &config) {
    for (size_t i = 0; i < kModuleCount; ++i) {
        const Validation result = validateSchedule(config.schedules[i]);
        if (!result.ok())
            return result;
    }
    for (unsigned lamp = 0; lamp < 2; ++lamp) {
        const Validation result = validateLightProgram(config.schedules[lamp], config.lightPrograms[lamp]);
        if (!result.ok())
            return result;
    }
    Validation result = validateFeeding(config.feeding);
    if (!result.ok())
        return result;
    result = validateFeeder(config.feeder);
    if (!result.ok())
        return result;
    result = validateHeater(config.heater);
    if (!result.ok())
        return result;
    for (unsigned kind = 0; kind < 3; ++kind) {
        result = validateRange(kind, config.ranges[kind]);
        if (!result.ok())
            return result;
    }
    return success();
}
Validation restoreConfiguration(const DomainConfig &config) {
    const Validation result = validateConfiguration(config);
    if (!result.ok())
        return result;
    memcpy(s.schedules, config.schedules, sizeof(s.schedules));
    memcpy(s.lightPrograms, config.lightPrograms, sizeof(s.lightPrograms));
    s.feeding = config.feeding;
    s.feeder = config.feeder;
    s.heater = config.heater;
    memcpy(s.ranges, config.ranges, sizeof(s.ranges));
    updateDerived();
    ++s.revision;
    return success();
}
uint8_t historyCount() { return sHistoryCount; }
const HistorySample *historyAt(uint8_t index) {
    if (index >= sHistoryCount)
        return nullptr;
    const uint8_t position =
        static_cast<uint8_t>((sHistoryWrite + kHistoryCapacity - sHistoryCount + index) % kHistoryCapacity);
    return &sHistory[position];
}
uint16_t historyArchiveCount() { return sArchiveCount; }
const HistorySample *historyArchiveAt(uint16_t index) {
    if (index >= sArchiveCount)
        return nullptr;
    return &sArchive[(sArchiveWrite + kHistoryArchiveCapacity - sArchiveCount + index) %
                     kHistoryArchiveCapacity];
}
uint16_t heaterEventCount() { return sHeaterCount; }
const HeaterEvent *heaterEventAt(uint16_t index) {
    if (index >= sHeaterCount)
        return nullptr;
    return &sHeaterEvents[(sHeaterWrite + kHeaterEventCapacity - sHeaterCount + index) %
                          kHeaterEventCapacity];
}
uint32_t historyRevision() { return sHistoryRevision; }
uint16_t waterEventCount() { return sWaterCount; }
const WaterEvent *waterEventAt(uint16_t index) {
    if (index >= sWaterCount)
        return nullptr;
    return &sWaterEvents[(sWaterWrite + kWaterEventCapacity - sWaterCount + index) % kWaterEventCapacity];
}
uint16_t atoDoseCount() { return sAtoCount; }
const AtoDose *atoDoseAt(uint16_t index) {
    if (index >= sAtoCount)
        return nullptr;
    return &sAtoDoses[(sAtoWrite + kAtoDoseCapacity - sAtoCount + index) % kAtoDoseCapacity];
}
float estimatedAtoLitres(const AtoDose &dose) {
    return dose.durationMs * (kNominalAtoLitresPerMinute / 60000.0f);
}
bool recordAtoDose(uint32_t durationMs) {
    if (!durationMs || durationMs > 3600000u || durationMs > sElapsedMs)
        return false;
    const AtoDose dose{static_cast<uint32_t>((sElapsedMs - durationMs) / 1000u),
                       static_cast<uint32_t>(sElapsedMs / 1000u), durationMs};
    const auto *previous = atoDoseAt(sAtoCount ? sAtoCount - 1 : 0);
    if (previous && (dose.startSeconds < previous->endSeconds || dose.endSeconds <= previous->endSeconds))
        return false;
    sAtoDoses[sAtoWrite] = dose;
    sAtoWrite = (sAtoWrite + 1u) % kAtoDoseCapacity;
    if (sAtoCount < kAtoDoseCapacity)
        ++sAtoCount;
    ++sHistoryRevision;
    ++s.revision;
    record(LogLevel::Info, "Zarejestrowano raport dolewki ATO");
    return true;
}
void clearHistory() {
    sHistoryWrite = sHistoryCount = 0;
    sArchiveWrite = sArchiveCount = sHeaterWrite = sHeaterCount = 0;
    sWaterWrite = sWaterCount = 0;
    sAtoWrite = sAtoCount = 0;
    recordHeaterState();
    recordWaterState();
    sampleHistory();
    ++s.revision;
}
void clearLog() {
    sLogWrite = sLogCount = 0;
    record(LogLevel::Info, "Wyczyszczono dziennik");
    ++s.revision;
}
void recordPanelEvent(const char *text, LogLevel level) {
    if (text) {
        record(level, text);
        ++s.revision;
    }
}

void acknowledgeAlarms() {
    if (s.alarmFlags == AlarmNone || s.alarmAcknowledged)
        return;
    s.alarmAcknowledged = true;
    ++s.revision;
    record(LogLevel::Warning, "Potwierdzono alarm; przyczyna nadal aktywna");
}

uint8_t logCount() { return sLogCount; }
uint32_t logRevision() { return sLogRevision; }
const LogEntry *logAt(uint8_t newestIndex) {
    if (newestIndex >= sLogCount)
        return nullptr;
    // A coalesced entry may be older in the ring but newest in the event chronology.
    for (unsigned offset = 0; offset < sLogCount; ++offset) {
        const auto &candidate = sLogs[(sLogWrite + kLogCapacity - 1u - offset) % kLogCapacity];
        unsigned newer = 0;
        for (unsigned other = 0; other < sLogCount; ++other) {
            const auto &item = sLogs[(sLogWrite + kLogCapacity - 1u - other) % kLogCapacity];
            newer += static_cast<int32_t>(item.sequence - candidate.sequence) > 0;
        }
        if (newer == newestIndex)
            return &candidate;
    }
    return nullptr;
}

} // namespace ui_model
