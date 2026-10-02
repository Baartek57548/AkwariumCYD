#include "ui_model.h"
#include <cmath>
#include <cstdio>
namespace {
uint32_t fixtureWallTime = 0;
uint32_t sampleWallTime() { return fixtureWallTime; }
} // namespace

bool ui_model_checks() {
    using namespace ui_model;
    int failures = 0, checks = 0;
    auto require = [&](bool success, const char *name) {
        ++checks;
        if (!success) {
            std::fprintf(stderr, "FAIL MODEL: %s\n", name);
            ++failures;
        }
    };
    setHistoryClock(nullptr);
    init(0);
    require(snapshot().demo, "explicit demo");
    require(!module(ModuleId::Ato).outputAssigned, "ATO without pump output");
    require(snapshot().levelMax.active && snapshot().alarmFlags == 0, "MAX is a normal stop condition");
    Schedule overnight{1320, 360, ScheduleMode::Timed, 1};
    require(scheduleContains(overnight, 1380, 0), "overnight starts Monday");
    require(scheduleContains(overnight, 180, 1), "overnight tail Tuesday");
    require(!scheduleContains(overnight, 180, 0), "disabled Sunday has no Monday tail");
    require(!scheduleContains(overnight, 360, 1), "end excluded");
    LightProgram program{1380, 60, 180, true};
    require(validateLightProgram(overnight, program).ok(), "overnight light profiles are ordered");
    require(lightModeFor(overnight, program, 1330, 0) == LightMode::Daybreak &&
                lightModeFor(overnight, program, 1400, 0) == LightMode::Day &&
                lightModeFor(overnight, program, 90, 1) == LightMode::Daybreak &&
                lightModeFor(overnight, program, 240, 1) == LightMode::Night,
            "four lighting phases cross midnight");
    require(lightModeFor(overnight, program, 360, 1) == LightMode::Off &&
                lightModeFor(overnight, program, 90, 0) == LightMode::Off,
            "profile respects end and prior-day mask");
    program.duskStartMinute = 1200;
    require(!validateLightProgram(overnight, program).ok(), "reject unordered lamp transitions");
    require(!validateSchedule({0, 0, ScheduleMode::Timed, 127}).ok(), "reject empty interval");
    require(!validateSchedule({1, 2, ScheduleMode::Timed, 0}).ok(), "reject no days");
    require(!validateSchedule({1440, 2, ScheduleMode::Timed, 127}).ok(), "reject invalid hour");
    FeedingConfig feed{127, 2, {540, 540}, 300};
    require(!validateFeeding(feed).ok(), "reject duplicate feed times");
    require(!validateHeater({28.4f, 0.3f}).ok(), "target below overheat cutoff");
    require(!validateHeater({NAN, 0.3f}).ok(), "reject NaN");
    require(setTimedMode(ModuleId::Filter, Mode::Off, 30).ok(), "timed override starts");
    tick(29000);
    require(module(ModuleId::Filter).mode == Mode::Off, "override held before deadline");
    tick(30000);
    require(module(ModuleId::Filter).mode == Mode::Auto, "override returns AUTO");
    require(startFeeding(60, true).ok(), "feeding starts");
    require(snapshot().dispensing && snapshot().feedingDurationSeconds == 60,
            "feeding animation has truthful dose phase and full duration");
    require(module(ModuleId::Filter).state == State::Blocked &&
                module(ModuleId::Co2).state == State::Blocked,
            "feeding pauses filter and CO2");
    require(!startService(30).ok(), "prevent conflicting service");
    tick(90000);
    require(!snapshot().feedingActive && module(ModuleId::Filter).mode == Mode::Auto,
            "feeding timeout restores AUTO");
    require(!snapshot().dispensing && snapshot().feedingDurationSeconds == 0,
            "feeding expiration clears animation metadata");
    require(startService(60).ok(), "service starts");
    require(module(ModuleId::Heater).state == State::Blocked, "service pauses heater");
    cancelService();
    require(module(ModuleId::Heater).mode == Mode::Auto, "service cancel restores AUTO");
    require(setLightMode(ModuleId::FrontLight, LightMode::Night).ok(), "lamp transition starts");
    require(module(ModuleId::FrontLight).transitioning, "lamp is pending");
    require(!setLightMode(ModuleId::FrontLight, LightMode::Day).ok(), "reject overlapping transition");
    tick(98500);
    require(!module(ModuleId::FrontLight).transitioning, "lamp settles");
    setScenario(Scenario::Alarm);
    uint16_t flags = snapshot().alarmFlags;
    acknowledgeAlarms();
    require(snapshot().alarmAcknowledged && snapshot().alarmFlags == flags,
            "acknowledgment preserves causes");
    require(module(ModuleId::Heater).state == State::Blocked, "overheat blocks heater");
    require(!snapshot().levelMax.valid, "unknown MAX stays invalid");
    setScenario(Scenario::Offline);
    require(!snapshot().temperature.valid && module(ModuleId::Filter).state == State::Unknown,
            "offline unknown state");
    require(!startFeeding(60).ok(), "offline action rejected");
    init(0xffffff00u);
    require(startService(1).ok(), "rollover timer starts");
    tick(744u);
    require(!snapshot().serviceActive, "millis rollover handled");
    init(0);
    require(chooseServiceLight(ModuleId::RearLight).ok(), "select rear working light before service");
    require(startService(60).ok(), "start service with selected light");
    require(module(ModuleId::RearLight).state == State::On &&
                module(ModuleId::FrontLight).state != State::On,
            "only selected light starts on");
    require(!snapshot().service.filter && !snapshot().service.air && !snapshot().service.heater &&
                !snapshot().service.co2,
            "service extras start off");
    require(!setServiceOutput(ModuleId::RearLight, false).ok(), "cannot extinguish mandatory light");
    require(setServiceOutput(ModuleId::FrontLight, true).ok() &&
                module(ModuleId::FrontLight).state == State::On,
            "optional second light on");
    require(setServiceOutput(ModuleId::FrontLight, false).ok() &&
                module(ModuleId::RearLight).state == State::On,
            "optional light off leaves mandatory light");
    require(chooseServiceLight(ModuleId::FrontLight).ok() &&
                module(ModuleId::FrontLight).state == State::On &&
                module(ModuleId::RearLight).state != State::On,
            "swap working light preserves illumination");
    require(setServiceOutput(ModuleId::Filter, true).ok() && module(ModuleId::Filter).state == State::On,
            "service filter toggle works");
    require(!setServiceOutput(ModuleId::Feeder, true).ok(), "feeder is unavailable in service");
    require(!chooseServiceLight(ModuleId::Heater).ok(), "working light must be a lamp");
    tick(60000);
    require(!snapshot().serviceActive && module(ModuleId::Filter).mode == Mode::Auto &&
                module(ModuleId::RearLight).lightMode == LightMode::Day,
            "timeout restores automatic outputs and saved lamp profile");
    setScenario(Scenario::Alarm);
    require(startService(60).ok() && !setServiceOutput(ModuleId::Heater, true).ok(),
            "service cannot bypass overheat protection");
    cancelService();
    setScenario(Scenario::LowWater);
    require(waterState() == WaterState::Low && health().severity == Severity::Warning, "low water warning");
    setScenario(Scenario::LevelFault);
    require(waterState() == WaterState::Conflict && health().severity == Severity::Critical,
            "contradictory floats critical");
    setScenario(Scenario::Offline);
    require(waterState() == WaterState::Unknown, "offline level unknown");
    setScenario(Scenario::DemoHealthy);
    require(health().severity == Severity::Normal, "healthy status");
    require(!validateRange(0, {26, 24}).ok() && !validateRange(1, {NAN, 7}).ok() &&
                !validateRange(2, {0, 11000}).ok(),
            "range validation rejects invalid bounds");
    require(applyRange(1, {7.0f, 7.5f}).ok() && (snapshot().alarmFlags & AlarmPh),
            "range changes alarm without changing reading");
    acknowledgeAlarms();
    require(applyRange(2, {100, 400}).ok() && !snapshot().alarmAcknowledged &&
                (snapshot().alarmFlags & AlarmEcRange),
            "new cause requires acknowledgment");
    init(0);
    for (uint32_t sample = 1; sample <= 80; ++sample)
        tick(sample * 15000u);
    require(historyCount() == kHistoryCapacity && historyAt(0)->atSeconds == 255 &&
                historyAt(63)->atSeconds == 1200,
            "history bounded ring preserves chronological samples");
    require(!historyAt(64), "out of range history index");
    clearHistory();
    require(historyCount() == 1 && historyAt(0)->temperature.value == snapshot().temperature.value,
            "clear restarts with current reading");
    setScenario(Scenario::Offline);
    tick(1215000);
    require(!historyAt(1)->temperature.valid, "missing samples remain gaps");
    init(0);
    require(applyFeeding({127, 1, {873, 1080}, 60}).ok(), "automatic feeding configuration");
    tick(60000);
    require(snapshot().feedingActive && snapshot().feedingRemainingSeconds == 60 &&
                module(ModuleId::Feeder).state == State::On,
            "minute boundary starts full pause and dose");
    tick(62000);
    require(module(ModuleId::Feeder).state != State::On && snapshot().feedingRemainingSeconds == 58,
            "dose stops after two seconds");
    require(!snapshot().dispensing && snapshot().feedingDurationSeconds == 60,
            "pause cannot animate another dose");
    tick(120000);
    require(!snapshot().feedingActive, "scheduled pause expires");
    setClock(872, 0);
    tick(180000);
    require(!snapshot().feedingActive, "clock rewind cannot duplicate same daily dose");
    init(0);
    applyFeeding({127, 1, {873, 1080}, 60});
    tick(120000);
    require(!snapshot().feedingActive, "missed dose not replayed on large tick");
    init(0);
    require(!validateFeeder({0, 0}).ok() && !validateFeeder({361, 0}).ok(), "rotation bounds");
    require(!validateFeeder({90, outputBit(ModuleId::Ato)}).ok() &&
                !validateFeeder({90, outputBit(ModuleId::Feeder)}).ok(),
            "sensor and feeder cannot be pause outputs");
    auto config = configuration();
    config.feeder = {120, outputBit(ModuleId::Air) | outputBit(ModuleId::FrontLight)};
    require(restoreConfiguration(config).ok(), "configure feeder outputs");
    require(startFeeding(1).ok() && feedingPauses(ModuleId::Air) && feedingPauses(ModuleId::FrontLight) &&
                !feedingPauses(ModuleId::Filter),
            "only selected outputs pause");
    const uint32_t firstJob = snapshot().motor.job;
    require(!startFeeding(1).ok() && snapshot().motor.job == firstJob, "duplicate dose rejected");
    tick(1100);
    require(snapshot().feedingActive && snapshot().motor.positionValid &&
                snapshot().motor.positionDegrees > 0,
            "short pause cannot truncate the motor rotation");
    tick(2100);
    require(!snapshot().feedingActive && snapshot().motor.phase == FeederPhase::Complete &&
                snapshot().motor.positionDegrees == 120,
            "local model completes exact configured angle");
    setFeederSource(FeederSource::Controller);
    tick(600000);
    require(startFeeding(60).ok() && !snapshot().motor.positionValid &&
                snapshot().motor.phase == FeederPhase::Waiting,
            "controller source waits for feedback");
    const uint32_t job = snapshot().motor.job;
    require(!applyFeederPosition(job - 1, 10, false) && !applyFeederPosition(job, NAN, false) &&
                !applyFeederPosition(job, -1, false) && !applyFeederPosition(job, 121, false) &&
                !applyFeederPosition(job, 10, true),
            "invalid or stale feedback rejected");
    require(applyFeederPosition(job, 60.25f, false) && snapshot().motor.positionDegrees == 60.25f,
            "exact reported position drives progress");
    require(!applyFeederPosition(job, 30, false), "out of order position cannot rewind dose");
    require(applyFeederPosition(job, 120, true) && !snapshot().dispensing && snapshot().feedingActive,
            "completion ACK finishes motor but not configured pause");
    require(!applyFeederPosition(job, 120, true), "duplicate completion ignored");
    cancelFeeding();
    require(!startFeeding(60).ok(), "cancellation cannot bypass dose interval");
    tick(1200000);
    require(startFeeding(60).ok(), "next distinct job allowed after minimum interval");
    tick(1208100);
    require(snapshot().motor.phase == FeederPhase::Fault && !snapshot().feedingActive &&
                !feedingPauses(ModuleId::Air),
            "missing feedback times out without physical success");
    bool feedbackLogged = false;
    for (unsigned index = 0; index < logCount(); ++index)
        feedbackLogged |= logAt(index)->level == LogLevel::Alarm;
    require(feedbackLogged, "missing feedback recorded as important");
    require((snapshot().alarmFlags & AlarmFeeder) && health().severity == Severity::Warning &&
                !startFeeding(60).ok(),
            "uncertain dose stays visible and blocks retry");
    require(acknowledgeFeederFault().ok() && !(snapshot().alarmFlags & AlarmFeeder) &&
                snapshot().motor.job == job + 1 && !snapshot().feedingActive,
            "explicit inspection clears fault without starting another dose");
    init(UINT32_MAX - 1000u);
    setFeederSource(FeederSource::Controller);
    startFeeding(60);
    tick(7100u);
    require(snapshot().motor.phase == FeederPhase::Fault, "feedback timeout survives millis wrap");
    init(0);
    for (uint32_t index = 1; index <= 900; ++index)
        tick(index * 300000u);
    require(historyArchiveCount() == kHistoryArchiveCapacity && historyArchiveAt(0)->atSeconds == 10800 &&
                historyArchiveAt(kHistoryArchiveCapacity - 1)->atSeconds == 270000,
            "coarse archive bounded and retains 72 hours");
    require(!historyArchiveAt(kHistoryArchiveCapacity), "archive invalid index rejected");
    require(historyAt(0)->atSeconds == 251100, "fast ring retains separate recent samples");
    require(!recordAtoDose(0) && !recordAtoDose(3600001), "ATO rejects empty or implausible report");
    require(recordAtoDose(60000) && atoDoseCount() == 1 &&
                std::fabs(estimatedAtoLitres(*atoDoseAt(0)) - 1.0f) < 0.0001f,
            "ATO volume is explicitly nominal one litre per minute");
    require(!recordAtoDose(60000), "duplicate ATO completion ignored");
    tick(273600000);
    require(recordAtoDose(30000) && atoDoseAt(1)->startSeconds - atoDoseAt(0)->endSeconds == 3570,
            "ATO interval measured from previous end to next start");
    require(std::fabs(estimatedAtoLitres(*atoDoseAt(1)) - 0.5f) < 0.0001f && !atoDoseAt(2),
            "ATO fractional volume and bounds");
    for (uint32_t index = 1; index <= kAtoDoseCapacity + 6u; ++index) {
        tick(273600000 + index * 120000u);
        recordAtoDose(60000);
    }
    require(atoDoseCount() == kAtoDoseCapacity &&
                atoDoseAt(kAtoDoseCapacity - 1)->endSeconds > atoDoseAt(0)->endSeconds,
            "ATO records bounded chronologically");
    clearHistory();
    require(historyArchiveCount() == 1 && atoDoseCount() == 0 && waterEventCount() == 1 &&
                heaterEventCount() == 1,
            "history reset resets all related bounded journals");
    init(0);
    require(!recordAtoDose(60000), "ATO duration cannot precede panel start");
    tick(1000);
    setScenario(Scenario::LowWater);
    require(waterEventAt(waterEventCount() - 1)->state == WaterState::Low,
            "water changes recorded independently from dosing");
    tick(2000);
    setScenario(Scenario::Offline);
    require(waterEventAt(waterEventCount() - 1)->state == WaterState::Unknown &&
                heaterEventAt(heaterEventCount() - 1)->state == State::Unknown,
            "offline gap is not fabricated OFF or normal water");
    tick(3000);
    setScenario(Scenario::DemoHealthy);
    require(applyHeater({25.2f, 0.3f}).ok() && module(ModuleId::Heater).state == State::On,
            "heater switches ON below lower threshold");
    require(applyHeater({25.0f, 0.3f}).ok() && module(ModuleId::Heater).state == State::On,
            "heater holds ON inside hysteresis");
    require(applyHeater({24.7f, 0.3f}).ok() && module(ModuleId::Heater).state == State::Off,
            "heater switches OFF above target");
    setHistoryClock(sampleWallTime);
    fixtureWallTime = 0;
    init(0);
    require(historyAt(0)->wallSeconds == 0, "missing clock recorded explicitly");
    fixtureWallTime = 843912000;
    tick(15000);
    require(historyAt(1)->wallSeconds == fixtureWallTime && historyAt(0)->wallSeconds == 0,
            "new clock cannot retroactively timestamp old samples");
    fixtureWallTime -= 3600;
    tick(30000);
    require(historyAt(1)->wallSeconds == 843912000 && historyAt(2)->wallSeconds == fixtureWallTime &&
                historyAt(2)->atSeconds > historyAt(1)->atSeconds,
            "clock rewind preserves sampled times and monotonic chronology");
    fixtureWallTime += 86400;
    tick(300000);
    require(historyArchiveAt(historyArchiveCount() - 1)->wallSeconds == fixtureWallTime,
            "archive copies original timestamp");
    fixtureWallTime = 0;
    tick(315000);
    require(historyAt(historyCount() - 1)->wallSeconds == 0, "lost clock remains unknown");
    init(0);
    clearLog();
    fixtureWallTime = 843912000;
    recordPanelEvent("Błąd transportu", LogLevel::Warning);
    const uint32_t firstWall = logAt(0)->wallSeconds;
    tick(1000);
    recordPanelEvent("Inna informacja");
    fixtureWallTime += 1;
    recordPanelEvent("Błąd transportu", LogLevel::Warning);
    require(logAt(0)->repeats == 2 && logAt(0)->wallSeconds == firstWall &&
                logAt(0)->lastWallSeconds == fixtureWallTime,
            "duplicate event preserves first time and records latest occurrence");
    require(logCount() == 3, "duplicate event does not consume another slot");
    fixtureWallTime = 0;
    recordPanelEvent("Nieznany czas");
    require(logAt(0)->wallSeconds == 0, "event without clock remains unknown");
    fixtureWallTime = firstWall + 60;
    require(logAt(0)->wallSeconds == 0, "clock alignment cannot rewrite existing event");
    setScenario(Scenario::LowWater);
    bool causeActive = false;
    for (unsigned index = 0; index < logCount(); ++index)
        causeActive |= logAt(index)->cause == AlarmLowWater && logAt(index)->active;
    require(causeActive, "alarm has an active cause event");
    setScenario(Scenario::DemoHealthy);
    bool causeEnded = false;
    for (unsigned index = 0; index < logCount(); ++index)
        causeEnded |= logAt(index)->cause == AlarmLowWater && !logAt(index)->active &&
                      logAt(index)->endedWallSeconds == fixtureWallTime;
    require(causeEnded, "resolved alarm retains time of resolution");
    setScenario(Scenario::LowWater);
    bool causeRepeated = false;
    for (unsigned index = 0; index < logCount(); ++index)
        causeRepeated |=
            logAt(index)->cause == AlarmLowWater && logAt(index)->active && logAt(index)->repeats == 2;
    require(causeRepeated, "recurring cause increments its existing event");
    init(0);
    recordPanelEvent("Niski poziom wody", LogLevel::Warning);
    recordPanelEvent("Informacja pomiędzy przyczynami");
    setScenario(Scenario::LowWater);
    require(logAt(0)->cause == AlarmLowWater && logAt(0)->active && logAt(0)->repeats == 2,
            "coalesced cause binds to its own record instead of the latest physical ring slot");
    for (unsigned index = 0; index < 80; ++index) {
        char message[32];
        std::snprintf(message, sizeof(message), "Zdarzenie %u", index);
        recordPanelEvent(message);
    }
    require(logCount() == kLogCapacity && !logAt(kLogCapacity), "extended event records remain bounded");
    init(0);
    require(!validateFeeder({90, 0, 0}).ok() && !validateFeeder({90, 0, 61}).ok(), "dose interval bounds");
    auto guardedConfig = configuration();
    guardedConfig.feeder.minimumIntervalMinutes = 1;
    restoreConfiguration(guardedConfig);
    require(!snapshot().lastFeedingKnown && snapshot().feedingCooldownSeconds == 0,
            "no fabricated last dose at boot");
    require(startFeeding(1).ok(), "guarded dose starts immediately");
    tick(2000);
    require(snapshot().lastFeedingKnown && snapshot().lastFeedingWallSeconds == fixtureWallTime &&
                snapshot().lastFeedingSource == FeederSource::LocalModel,
            "local completion stores its source and time");
    require(!startFeeding(1).ok() && snapshot().feedingCooldownSeconds == 58,
            "interval remains after short pause");
    setClock(10, 2);
    require(!startFeeding(1).ok(), "clock correction cannot bypass monotonic guard");
    tick(59999);
    require(!startFeeding(1).ok(), "one millisecond before guard expiry rejected");
    tick(60000);
    require(startFeeding(1).ok(), "exact minimum interval permits new dose");
    cancelFeeding();
    require(!startFeeding(1).ok(), "partially cancelled dose also retains guard");
    init(UINT32_MAX - 1000u);
    guardedConfig = configuration();
    guardedConfig.feeder.minimumIntervalMinutes = 1;
    restoreConfiguration(guardedConfig);
    startFeeding(1);
    tick(1000);
    require(!startFeeding(1).ok(), "guard survives millis rollover");
    tick(58999);
    require(startFeeding(1).ok(), "guard expires across millis rollover");
    init(0);
    guardedConfig = configuration();
    guardedConfig.feeder.minimumIntervalMinutes = 1;
    restoreConfiguration(guardedConfig);
    require(startFeeding(10).ok(), "first dose begins before admin repeat");
    tick(2000);
    const uint32_t firstAdminJob = snapshot().motor.job;
    require(snapshot().feedingActive && snapshot().motor.phase == FeederPhase::Complete,
            "admin repeat is offered only after a confirmed dose");
    require(startAdditionalDose(10, firstAdminJob, nullptr).error == Error::InvalidPin &&
                startAdditionalDose(10, firstAdminJob, "1234").error == Error::InvalidPin &&
                snapshot().motor.job == firstAdminJob,
            "missing or ordinary panel PIN cannot bypass dose interval");
    require(startAdditionalDose(10, firstAdminJob, "2177").ok() &&
                snapshot().motor.job != firstAdminJob &&
                snapshot().motor.phase == FeederPhase::Waiting &&
                snapshot().feedingCooldownSeconds == 60,
            "admin PIN starts exactly one extra dose and restarts the guard during pause");
    const uint32_t secondAdminJob = snapshot().motor.job;
    require(!startAdditionalDose(10, firstAdminJob, "2177").ok() &&
                !startAdditionalDose(10, secondAdminJob, "2177").ok(),
            "stale authorization and overlapping motor jobs are rejected");
    tick(4000);
    tick(14000);
    require(!snapshot().feedingActive && snapshot().feedingCooldownSeconds == 48 &&
                startAdditionalDose(10, secondAdminJob, "2177").ok(),
            "admin PIN also permits another confirmed dose after the pause ends");
    const uint32_t thirdAdminJob = snapshot().motor.job;
    cancelFeeding();
    require(!startAdditionalDose(10, thirdAdminJob, "2177").ok(),
            "cancelled unconfirmed dose cannot be repeated through admin override");
    init(0);
    applyFeeding({127, 2, {873, 1080}, 1});
    require(nextFeeding().available && nextFeeding().minute == 873 && nextFeeding().daysAway == 0,
            "nearest scheduled dose selected");
    startFeeding(1);
    tick(2000);
    require(nextFeeding().minute == 1080, "next dose excludes slot blocked by recent manual feeding");
    const uint32_t guardedJob = snapshot().motor.job;
    tick(60000);
    require(!snapshot().feedingActive && snapshot().motor.job == guardedJob,
            "nearby automatic dose skipped without double feeding");
    tick(120000);
    require(snapshot().motor.job == guardedJob, "skipped automatic dose is not replayed");
    init(0);
    setFeederSource(FeederSource::Controller);
    startFeeding(1);
    cancelFeeding();
    require(!snapshot().lastFeedingKnown,
            "cancelled unconfirmed motor cannot become last completed feeding");
    fixtureWallTime = 0;
    init(0);
    guardedConfig = configuration();
    guardedConfig.feeder.minimumIntervalMinutes = 1;
    restoreConfiguration(guardedConfig);
    applyFeeding({127, 2, {873, 1080}, 1});
    tick(5000);
    startFeeding(1);
    tick(7000);
    require(nextFeeding().minute == 1080, "next slot accounts for seconds before the minute boundary");
    setHistoryClock(nullptr);
    std::printf("MODEL %s checks=%d failures=%d\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures == 0;
}
