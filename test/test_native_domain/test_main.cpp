#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include <unity.h>

#include "aquarium_automation.h"
#include "aquarium_schedule.h"
#include "admin_session.h"
#include "aquael_light_controller.h"
#include "control_modes.h"
#include "dev_simulator.h"
#include "idempotency_ledger.h"
#include "ota_safety_policy.h"
#include "web_activity_tracker.h"
#include "wifi_retry_policy.h"

using aquarium::LightProfile;
using aquarium::ScheduleMode;
using aquarium::TimeWindow;

extern "C" void setUp(void) {
}

extern "C" void tearDown(void) {
}

static void test_regular_and_wrapped_windows() {
    const TimeWindow daytime = {{10U, 0U}, {22U, 0U}};
    TEST_ASSERT_FALSE(aquarium::is_within_window(9U * 60U + 59U, daytime));
    TEST_ASSERT_TRUE(aquarium::is_within_window(10U * 60U, daytime));
    TEST_ASSERT_TRUE(aquarium::is_within_window(21U * 60U + 59U, daytime));
    TEST_ASSERT_FALSE(aquarium::is_within_window(22U * 60U, daytime));

    const TimeWindow quiet = {{20U, 0U}, {10U, 0U}};
    TEST_ASSERT_TRUE(aquarium::is_within_window(23U * 60U, quiet));
    TEST_ASSERT_TRUE(aquarium::is_within_window(5U * 60U, quiet));
    TEST_ASSERT_FALSE(aquarium::is_within_window(12U * 60U, quiet));
    TEST_ASSERT_FALSE(aquarium::is_within_window(0U, {{5U, 0U}, {5U, 0U}}));
}

static void test_schedule_modes_override_window() {
    const TimeWindow window = {{10U, 0U}, {11U, 0U}};
    TEST_ASSERT_TRUE(aquarium::schedule_active(ScheduleMode::AlwaysOn, 0U, window));
    TEST_ASSERT_FALSE(aquarium::schedule_active(ScheduleMode::AlwaysOff, 10U * 60U + 30U, window));
    TEST_ASSERT_TRUE(aquarium::schedule_active(ScheduleMode::Schedule, 10U * 60U + 30U, window));
}

static void test_factory_light_profile_boundaries() {
    LightProfile profile = LightProfile::Day;
    TEST_ASSERT_FALSE(aquarium::factory_light_profile_at(9U * 60U + 59U, &profile));
    TEST_ASSERT_TRUE(aquarium::factory_light_profile_at(10U * 60U, &profile));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LightProfile::Daybreak), static_cast<uint8_t>(profile));
    TEST_ASSERT_TRUE(aquarium::factory_light_profile_at(10U * 60U + 30U, &profile));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LightProfile::Day), static_cast<uint8_t>(profile));
    TEST_ASSERT_TRUE(aquarium::factory_light_profile_at(20U * 60U, &profile));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LightProfile::Daybreak), static_cast<uint8_t>(profile));
    TEST_ASSERT_TRUE(aquarium::factory_light_profile_at(21U * 60U, &profile));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LightProfile::Night), static_cast<uint8_t>(profile));
    TEST_ASSERT_FALSE(aquarium::factory_light_profile_at(22U * 60U, &profile));
}

static void test_factory_schedule_outputs() {
    const aquarium::FactoryScheduleState at_1400 = aquarium::factory_schedule_at(14U * 60U, 0U);
    TEST_ASSERT_TRUE(at_1400.lightOn);
    TEST_ASSERT_TRUE(at_1400.light2On);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LightProfile::Day), static_cast<uint8_t>(at_1400.light2Profile));
    TEST_ASSERT_TRUE(at_1400.filterOn);
    TEST_ASSERT_TRUE(at_1400.gasWindowActive);
    TEST_ASSERT_TRUE(at_1400.feedingDue);
    TEST_ASSERT_TRUE(at_1400.heaterMonitoringActive);
    TEST_ASSERT_TRUE(at_1400.waterLevelMonitoringActive);
    TEST_ASSERT_TRUE(at_1400.leakMonitoringActive);

    const aquarium::FactoryScheduleState at_2030 = aquarium::factory_schedule_at(20U * 60U + 30U, 0U);
    TEST_ASSERT_TRUE(at_2030.lightOn);
    TEST_ASSERT_TRUE(at_2030.light2On);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LightProfile::Daybreak), static_cast<uint8_t>(at_2030.light2Profile));
    TEST_ASSERT_FALSE(at_2030.filterOn);
    TEST_ASSERT_FALSE(at_2030.gasWindowActive);
}

static void test_thermostat_hysteresis_and_invalid_input() {
    aquarium::ThermostatInput input = {true, true, true, true, 25.0f, 26.0f, 0.5f, false};
    TEST_ASSERT_TRUE(aquarium::thermostat_next_state(input));
    input.temperatureC = 25.7f;
    input.previousOn = true;
    TEST_ASSERT_TRUE(aquarium::thermostat_next_state(input));
    input.temperatureC = 26.0f;
    TEST_ASSERT_FALSE(aquarium::thermostat_next_state(input));
    input.temperatureValid = false;
    input.previousOn = true;
    TEST_ASSERT_FALSE(aquarium::thermostat_next_state(input));
}

static void test_gas_control_is_mutually_exclusive_and_leak_safe() {
    aquarium::GasControlInput input = {true, true, true, true, true, true, 6.95f, 6.80f, false};
    aquarium::GasControlOutput output = aquarium::evaluate_gas_control(input);
    TEST_ASSERT_TRUE(output.co2On);
    TEST_ASSERT_FALSE(output.aeratorOn);

    input.ph = 6.70f;
    output = aquarium::evaluate_gas_control(input);
    TEST_ASSERT_FALSE(output.co2On);
    TEST_ASSERT_TRUE(output.aeratorOn);

    input.leakDetected = true;
    output = aquarium::evaluate_gas_control(input);
    TEST_ASSERT_FALSE(output.co2On);
    TEST_ASSERT_FALSE(output.aeratorOn);
}

static void test_ato_requires_low_valid_level_and_obeys_interlocks() {
    aquarium::AtoControlInput input = {true, true, true, false, false, false};
    TEST_ASSERT_TRUE(aquarium::evaluate_ato_control(input));

    input.waterLevelHigh = true;
    TEST_ASSERT_FALSE(aquarium::evaluate_ato_control(input));
    input.waterLevelHigh = false;
    input.leakDetected = true;
    TEST_ASSERT_FALSE(aquarium::evaluate_ato_control(input));
    input.leakDetected = false;
    input.timeoutLatched = true;
    TEST_ASSERT_FALSE(aquarium::evaluate_ato_control(input));
    input.timeoutLatched = false;
    input.waterLevelValid = false;
    TEST_ASSERT_FALSE(aquarium::evaluate_ato_control(input));
}

static void test_alarm_flags_and_count() {
    const aquarium::AlarmInput input = {
        true, 29.0f,
        true, 8.2f,
        true, false,
        true, true,
        true, 4.50f
    };
    const unsigned int flags = aquarium::evaluate_alarm_flags(input);
    TEST_ASSERT_BITS_HIGH(aquarium::AlarmTemperatureHigh, flags);
    TEST_ASSERT_BITS_HIGH(aquarium::AlarmPhOutOfRange, flags);
    TEST_ASSERT_BITS_HIGH(aquarium::AlarmWaterLevelLow, flags);
    TEST_ASSERT_BITS_HIGH(aquarium::AlarmLeak, flags);
    TEST_ASSERT_BITS_HIGH(aquarium::AlarmSupplyLow, flags);
    TEST_ASSERT_EQUAL_UINT(5U, aquarium::alarm_count(flags));
}

static void test_dev_simulator_is_deterministic_and_coherent() {
    aquarium::DevSimulator first(1234U);
    aquarium::DevSimulator second(1234U);
    const aquarium::DevSnapshot &a = first.step(1000U, 10U * 60U + 30U, 1U);
    const aquarium::DevSnapshot &b = second.step(1000U, 10U * 60U + 30U, 1U);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, a.temperatureC, b.temperatureC);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, a.ph, b.ph);
    TEST_ASSERT_EQUAL_INT(a.ldr, b.ldr);
    TEST_ASSERT_TRUE(a.lightOn);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LightProfile::Day), static_cast<uint8_t>(a.lightProfile));
    TEST_ASSERT_TRUE(a.light2On);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LightProfile::Day), static_cast<uint8_t>(a.light2Profile));
    TEST_ASSERT_TRUE(a.filterOn);
    TEST_ASSERT_TRUE(a.phRaw > 0);
    TEST_ASSERT_TRUE(a.ecRaw > 0);

    const aquarium::DevSnapshot &water_alarm = first.step(600000U, 10U * 60U, 0U);
    TEST_ASSERT_FALSE(water_alarm.waterLevelHigh);
    TEST_ASSERT_BITS_HIGH(aquarium::AlarmWaterLevelLow, water_alarm.alarmFlags);

    const aquarium::DevSnapshot &leak_alarm = first.step(900000U, 10U * 60U, 0U);
    TEST_ASSERT_TRUE(leak_alarm.leakDetected);
    TEST_ASSERT_FALSE(leak_alarm.co2On);
    TEST_ASSERT_FALSE(leak_alarm.aeratorOn);
}

static void test_web_activity_tracker_capacity_timeout_and_release() {
    aquarium::WebActivityTracker tracker(15000U);
    TEST_ASSERT_FALSE(tracker.touch("bad!", 0U));
    TEST_ASSERT_TRUE(tracker.touch("client_001", 100U));
    TEST_ASSERT_TRUE(tracker.touch("client_002", 200U));
    TEST_ASSERT_EQUAL_UINT8(2U, tracker.active_count(300U));
    TEST_ASSERT_TRUE(tracker.release("client_001"));
    TEST_ASSERT_EQUAL_UINT8(1U, tracker.active_count(400U));
    TEST_ASSERT_EQUAL_UINT8(0U, tracker.active_count(16000U));
}

static void test_wifi_retry_policy_handles_capacity_and_millis_wrap() {
    aquarium::WifiRetryPolicy policy(5U, 300U);
    TEST_ASSERT_TRUE(policy.retry_allowed(100U));
    policy.on_disconnect(4U, 100U);
    TEST_ASSERT_TRUE(policy.retry_allowed(100U));
    policy.on_disconnect(5U, 100U);
    TEST_ASSERT_EQUAL_UINT32(300U, policy.remaining_ms(100U));
    TEST_ASSERT_FALSE(policy.retry_allowed(399U));
    TEST_ASSERT_TRUE(policy.retry_allowed(400U));

    policy.on_disconnect(5U, UINT32_MAX - 100U);
    TEST_ASSERT_FALSE(policy.retry_allowed(UINT32_MAX - 50U));
    TEST_ASSERT_TRUE(policy.retry_allowed(199U));
    policy.on_connected();
    TEST_ASSERT_TRUE(policy.retry_allowed(0U));
}

static void test_timed_overrides_expire_and_safety_modes_win() {
    aquarium::ControlModeManager modes;
    const uint32_t start = UINT32_MAX - 10000U;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(aquarium::ControlModeResult::Applied),
        static_cast<uint8_t>(modes.set_override(
            aquarium::OutputTarget::Light1, true, 30U, start)));
    bool managed = false;
    TEST_ASSERT_TRUE(modes.resolve(
        aquarium::OutputTarget::Light1, false, start + 1000U, &managed));
    TEST_ASSERT_TRUE(managed);
    TEST_ASSERT_FALSE(modes.resolve(
        aquarium::OutputTarget::Light1, false, start + 30000U, &managed));
    TEST_ASSERT_FALSE(managed);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(aquarium::ControlModeResult::Applied),
        static_cast<uint8_t>(modes.start_feeding(600U, 100U)));
    TEST_ASSERT_FALSE(modes.resolve(
        aquarium::OutputTarget::Filter, true, 200U, &managed));
    TEST_ASSERT_TRUE(managed);
    TEST_ASSERT_TRUE(modes.resolve(
        aquarium::OutputTarget::Aeration, true, 200U, &managed));
    TEST_ASSERT_FALSE(managed);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(aquarium::ControlModeResult::Applied),
        static_cast<uint8_t>(modes.start_service(900U, 300U)));
    TEST_ASSERT_FALSE(modes.resolve(
        aquarium::OutputTarget::Aeration, true, 400U, &managed));
    TEST_ASSERT_TRUE(managed);
    const aquarium::OperatingModeSnapshot snapshot = modes.mode_snapshot(400U);
    TEST_ASSERT_TRUE(snapshot.service_active);
    TEST_ASSERT_FALSE(snapshot.feeding_active);
}

static void test_control_mode_validation_and_target_aliases() {
    aquarium::ControlModeManager modes;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(aquarium::OutputTarget::WaterDosing),
        static_cast<uint8_t>(aquarium::ControlModeManager::parse_target("ato")));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(aquarium::OutputTarget::Invalid),
        static_cast<uint8_t>(aquarium::ControlModeManager::parse_target("unknown")));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(aquarium::ControlModeResult::InvalidDuration),
        static_cast<uint8_t>(modes.set_override(
            aquarium::OutputTarget::Filter, false, 29U, 0U)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(aquarium::ControlModeResult::InvalidDuration),
        static_cast<uint8_t>(modes.start_service(7201U, 0U)));
}

static void test_idempotency_cache_detects_duplicates_and_conflicts() {
    aquarium::IdempotencyLedger ledger;
    aquarium::CachedCommandResult stored = {true, "ok", "accepted"};
    const uint32_t fingerprint = aquarium::IdempotencyLedger::fingerprint("set:light1:true");
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(aquarium::CommandLookup::NewCommand),
        static_cast<uint8_t>(ledger.lookup(
            "cmd-00000001", fingerprint, 100U, nullptr)));
    TEST_ASSERT_TRUE(ledger.remember(
        "cmd-00000001", fingerprint, stored, 100U));
    aquarium::CachedCommandResult replay = {};
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(aquarium::CommandLookup::Duplicate),
        static_cast<uint8_t>(ledger.lookup(
            "cmd-00000001", fingerprint, 200U, &replay)));
    TEST_ASSERT_TRUE(replay.success);
    TEST_ASSERT_EQUAL_STRING("ok", replay.code);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(aquarium::CommandLookup::Conflict),
        static_cast<uint8_t>(ledger.lookup(
            "cmd-00000001",
            aquarium::IdempotencyLedger::fingerprint("set:light1:false"),
            300U,
            nullptr)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(aquarium::CommandLookup::NewCommand),
        static_cast<uint8_t>(ledger.lookup(
            "cmd-00000001", fingerprint,
            100U + aquarium::IdempotencyLedger::kRetentionMs,
            nullptr)));
}

static void test_admin_session_rate_limit_token_and_expiry() {
    aquarium::AdminSessionManager sessions;
    const uint32_t entropy[4] = {
        0x01234567UL, 0x89ABCDEFUL, 0x10203040UL, 0x50607080UL
    };
    char token[aquarium::AdminSessionManager::kTokenBytes] = {};
    for (uint8_t attempt = 0U;
         attempt < aquarium::AdminSessionManager::kMaxFailedPinAttempts - 1U;
         ++attempt) {
        const aquarium::AuthenticationStatus status =
            sessions.authenticate(false, 100U + attempt, entropy, token, sizeof(token));
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<uint8_t>(aquarium::AuthenticationResult::InvalidPin),
            static_cast<uint8_t>(status.result));
    }
    aquarium::AuthenticationStatus status =
        sessions.authenticate(false, 200U, entropy, token, sizeof(token));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(aquarium::AuthenticationResult::RateLimited),
        static_cast<uint8_t>(status.result));
    status = sessions.authenticate(true, 300U, entropy, token, sizeof(token));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(aquarium::AuthenticationResult::RateLimited),
        static_cast<uint8_t>(status.result));
    status = sessions.authenticate(
        true,
        200U + aquarium::AdminSessionManager::kLockoutMs,
        entropy,
        token,
        sizeof(token));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(aquarium::AuthenticationResult::Authenticated),
        static_cast<uint8_t>(status.result));
    TEST_ASSERT_EQUAL_UINT32(32U, strlen(token));
    TEST_ASSERT_TRUE(sessions.validate(
        token, 200U + aquarium::AdminSessionManager::kLockoutMs + 1U));
    TEST_ASSERT_FALSE(sessions.validate(
        token,
        200U + aquarium::AdminSessionManager::kLockoutMs +
            aquarium::AdminSessionManager::kSessionTtlMs));
}

static void test_admin_session_lockout_survives_millis_wrap() {
    aquarium::AdminSessionManager sessions;
    const uint32_t entropy[4] = {
        0x11223344UL, 0x55667788UL, 0x99AABBCCUL, 0xDDEEFF00UL
    };
    char token[aquarium::AdminSessionManager::kTokenBytes] = {};
    const uint32_t lockout_start =
        UINT32_MAX - aquarium::AdminSessionManager::kLockoutMs + 1U;
    for (uint8_t attempt = 0U;
         attempt < aquarium::AdminSessionManager::kMaxFailedPinAttempts;
         ++attempt) {
        sessions.authenticate(
            false, lockout_start, entropy, token, sizeof(token));
    }
    aquarium::AuthenticationStatus status = sessions.authenticate(
        true, lockout_start + 1U, entropy, token, sizeof(token));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(aquarium::AuthenticationResult::RateLimited),
        static_cast<uint8_t>(status.result));

    status = sessions.authenticate(
        true,
        lockout_start + aquarium::AdminSessionManager::kLockoutMs,
        entropy,
        token,
        sizeof(token));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(aquarium::AuthenticationResult::Authenticated),
        static_cast<uint8_t>(status.result));
}

static void test_ota_preflight_and_pending_verify_policy() {
    aquarium::OtaPreflightInput input = {
        900000U, 1300000U, 64000U, false, true, true
    };
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(aquarium::OtaPreflightResult::Ready),
        static_cast<uint8_t>(aquarium::evaluate_ota_preflight(input)));
    input.feeder_active = true;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(aquarium::OtaPreflightResult::UnsafeActivity),
        static_cast<uint8_t>(aquarium::evaluate_ota_preflight(input)));
    input.feeder_active = false;
    input.configuration_backed_up = false;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(aquarium::OtaPreflightResult::BackupFailed),
        static_cast<uint8_t>(aquarium::evaluate_ota_preflight(input)));

    aquarium::BootValidationPolicy policy;
    policy.begin(100U, true);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(aquarium::BootValidationDecision::Wait),
        static_cast<uint8_t>(policy.evaluate(1000U, true, 64000U)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(aquarium::BootValidationDecision::MarkValid),
        static_cast<uint8_t>(policy.evaluate(
            100U + aquarium::BootValidationPolicy::kValidationWindowMs,
            true,
            64000U)));

    aquarium::BootValidationPolicy unhealthy;
    unhealthy.begin(0U, true);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(aquarium::BootValidationDecision::Rollback),
        static_cast<uint8_t>(unhealthy.evaluate(1000U, false, 8000U)));
}

static void apply_aquael_decision(aquarium::AquaelLightController &controller,
                                  uint32_t now_ms) {
    const aquarium::AquaelDriveDecision decision = controller.poll(now_ms);
    if (decision.write_required) {
        controller.acknowledge_write(true, now_ms);
    }
}

static void test_aquael_profile_calibrates_then_cycles_without_blocking() {
    aquarium::AquaelLightController light;
    light.request(true, aquarium::AquaelProfile::Night);

    apply_aquael_decision(light, 100U);
    aquarium::AquaelLightSnapshot snapshot = light.snapshot(100U);
    TEST_ASSERT_FALSE(snapshot.relay_on);
    TEST_ASSERT_FALSE(snapshot.known);
    TEST_ASSERT_TRUE(snapshot.transitioning);

    apply_aquael_decision(
        light, 100U + aquarium::AquaelLightController::kResetOffMs - 1U);
    TEST_ASSERT_FALSE(light.snapshot(
        100U + aquarium::AquaelLightController::kResetOffMs - 1U).relay_on);

    const uint32_t reset_on =
        100U + aquarium::AquaelLightController::kResetOffMs;
    apply_aquael_decision(light, reset_on);
    snapshot = light.snapshot(reset_on);
    TEST_ASSERT_TRUE(snapshot.relay_on);
    TEST_ASSERT_TRUE(snapshot.known);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(aquarium::AquaelProfile::Day),
        static_cast<uint8_t>(snapshot.profile));

    const uint32_t first_cycle_off =
        reset_on + aquarium::AquaelLightController::kOnSettleMs;
    apply_aquael_decision(light, first_cycle_off);
    TEST_ASSERT_FALSE(light.snapshot(first_cycle_off).relay_on);
    apply_aquael_decision(
        light, first_cycle_off + aquarium::AquaelLightController::kCycleOffMs);
    snapshot = light.snapshot(
        first_cycle_off + aquarium::AquaelLightController::kCycleOffMs);
    TEST_ASSERT_TRUE(snapshot.relay_on);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(aquarium::AquaelProfile::Daybreak),
        static_cast<uint8_t>(snapshot.profile));

    const uint32_t second_cycle_off =
        first_cycle_off + aquarium::AquaelLightController::kCycleOffMs +
        aquarium::AquaelLightController::kOnSettleMs;
    apply_aquael_decision(light, second_cycle_off);
    apply_aquael_decision(
        light, second_cycle_off + aquarium::AquaelLightController::kCycleOffMs);
    snapshot = light.snapshot(
        second_cycle_off + aquarium::AquaelLightController::kCycleOffMs +
        aquarium::AquaelLightController::kOnSettleMs);
    TEST_ASSERT_TRUE(snapshot.relay_on);
    TEST_ASSERT_TRUE(snapshot.known);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(aquarium::AquaelProfile::Night),
        static_cast<uint8_t>(snapshot.profile));
}

static void test_aquael_front_and_rear_state_is_independent() {
    aquarium::AquaelLightController front;
    aquarium::AquaelLightController rear;
    front.request(true, aquarium::AquaelProfile::Day);
    rear.request(false, aquarium::AquaelProfile::Night);
    apply_aquael_decision(front, 10U);
    apply_aquael_decision(rear, 10U);
    apply_aquael_decision(
        front, 10U + aquarium::AquaelLightController::kResetOffMs);
    const aquarium::AquaelLightSnapshot front_snapshot = front.snapshot(
        10U + aquarium::AquaelLightController::kResetOffMs);
    const aquarium::AquaelLightSnapshot rear_snapshot = rear.snapshot(
        10U + aquarium::AquaelLightController::kResetOffMs);
    TEST_ASSERT_TRUE(front_snapshot.relay_on);
    TEST_ASSERT_TRUE(front_snapshot.known);
    TEST_ASSERT_FALSE(rear_snapshot.relay_on);
    TEST_ASSERT_FALSE(rear_snapshot.known);
}

static void test_aquael_quick_power_cycle_advances_and_long_off_resets_day() {
    aquarium::AquaelLightController light;
    light.request(true, aquarium::AquaelProfile::Day);
    apply_aquael_decision(light, 100U);
    const uint32_t calibrated_on =
        100U + aquarium::AquaelLightController::kResetOffMs;
    apply_aquael_decision(light, calibrated_on);
    apply_aquael_decision(
        light,
        calibrated_on + aquarium::AquaelLightController::kOnSettleMs);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(aquarium::AquaelProfile::Day),
        static_cast<uint8_t>(light.snapshot(calibrated_on).profile));

    const uint32_t quick_off = calibrated_on + 1000U;
    light.request(false, aquarium::AquaelProfile::Day);
    apply_aquael_decision(light, quick_off);
    TEST_ASSERT_FALSE(light.snapshot(quick_off).relay_on);
    TEST_ASSERT_TRUE(light.snapshot(quick_off).known);

    light.request(true, aquarium::AquaelProfile::Daybreak);
    const uint32_t quick_on =
        quick_off + aquarium::AquaelLightController::kCycleOffMs;
    apply_aquael_decision(light, quick_on);
    TEST_ASSERT_TRUE(light.snapshot(quick_on).relay_on);
    TEST_ASSERT_TRUE(light.snapshot(quick_on).known);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(aquarium::AquaelProfile::Daybreak),
        static_cast<uint8_t>(light.snapshot(quick_on).profile));

    const uint32_t long_off = quick_on + 1000U;
    light.request(false, aquarium::AquaelProfile::Daybreak);
    apply_aquael_decision(light, long_off);
    const uint32_t after_documented_reset =
        long_off + aquarium::AquaelLightController::kProfileToggleMaxOffMs + 1U;
    apply_aquael_decision(light, after_documented_reset);
    TEST_ASSERT_FALSE(light.snapshot(after_documented_reset).known);

    light.request(true, aquarium::AquaelProfile::Day);
    apply_aquael_decision(light, after_documented_reset);
    TEST_ASSERT_TRUE(light.snapshot(after_documented_reset).relay_on);
    TEST_ASSERT_TRUE(light.snapshot(after_documented_reset).known);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(aquarium::AquaelProfile::Day),
        static_cast<uint8_t>(light.snapshot(after_documented_reset).profile));
}

static void test_aquael_exact_five_second_boundary_still_advances() {
    aquarium::AquaelLightController light;
    light.request(true, aquarium::AquaelProfile::Day);
    apply_aquael_decision(light, 1U);
    const uint32_t calibrated_on =
        1U + aquarium::AquaelLightController::kResetOffMs;
    apply_aquael_decision(light, calibrated_on);
    apply_aquael_decision(
        light,
        calibrated_on + aquarium::AquaelLightController::kOnSettleMs);

    const uint32_t off_at = calibrated_on + 1000U;
    light.request(false, aquarium::AquaelProfile::Day);
    apply_aquael_decision(light, off_at);
    light.request(true, aquarium::AquaelProfile::Daybreak);
    const uint32_t on_at =
        off_at + aquarium::AquaelLightController::kProfileToggleMaxOffMs;
    apply_aquael_decision(light, on_at);

    const aquarium::AquaelLightSnapshot snapshot = light.snapshot(on_at);
    TEST_ASSERT_TRUE(snapshot.relay_on);
    TEST_ASSERT_TRUE(snapshot.known);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(aquarium::AquaelProfile::Daybreak),
        static_cast<uint8_t>(snapshot.profile));
}

static void test_aquael_calibration_is_safe_at_millis_zero() {
    aquarium::AquaelLightController light;
    light.request(true, aquarium::AquaelProfile::Day);

    aquarium::AquaelDriveDecision decision = light.poll(0U);
    TEST_ASSERT_FALSE(decision.write_required);
    decision = light.poll(0U);
    TEST_ASSERT_FALSE(decision.write_required);
    TEST_ASSERT_FALSE(light.snapshot(0U).relay_on);

    decision = light.poll(aquarium::AquaelLightController::kResetOffMs);
    TEST_ASSERT_TRUE(decision.write_required);
    TEST_ASSERT_TRUE(decision.relay_on);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_regular_and_wrapped_windows);
    RUN_TEST(test_schedule_modes_override_window);
    RUN_TEST(test_factory_light_profile_boundaries);
    RUN_TEST(test_factory_schedule_outputs);
    RUN_TEST(test_thermostat_hysteresis_and_invalid_input);
    RUN_TEST(test_gas_control_is_mutually_exclusive_and_leak_safe);
    RUN_TEST(test_ato_requires_low_valid_level_and_obeys_interlocks);
    RUN_TEST(test_alarm_flags_and_count);
    RUN_TEST(test_dev_simulator_is_deterministic_and_coherent);
    RUN_TEST(test_web_activity_tracker_capacity_timeout_and_release);
    RUN_TEST(test_wifi_retry_policy_handles_capacity_and_millis_wrap);
    RUN_TEST(test_timed_overrides_expire_and_safety_modes_win);
    RUN_TEST(test_control_mode_validation_and_target_aliases);
    RUN_TEST(test_idempotency_cache_detects_duplicates_and_conflicts);
    RUN_TEST(test_admin_session_rate_limit_token_and_expiry);
    RUN_TEST(test_admin_session_lockout_survives_millis_wrap);
    RUN_TEST(test_ota_preflight_and_pending_verify_policy);
    RUN_TEST(test_aquael_profile_calibrates_then_cycles_without_blocking);
    RUN_TEST(test_aquael_front_and_rear_state_is_independent);
    RUN_TEST(test_aquael_quick_power_cycle_advances_and_long_off_resets_day);
    RUN_TEST(test_aquael_exact_five_second_boundary_still_advances);
    RUN_TEST(test_aquael_calibration_is_safe_at_millis_zero);
    return UNITY_END();
}
