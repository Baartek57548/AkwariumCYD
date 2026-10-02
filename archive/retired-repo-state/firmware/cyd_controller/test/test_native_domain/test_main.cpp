#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <unity.h>

#include "aquarium_automation.h"
#include "aquarium_schedule.h"
#include "admin_session.h"
#include "aquael_light_controller.h"
#include "control_modes.h"
#include "dev_simulator.h"
#include "idempotency_ledger.h"
#include "ota_package.h"
#include "ota_safety_policy.h"
#include "sensor_calibration.h"
#include "web_activity_tracker.h"
#include "wifi_retry_policy.h"
#include "uart_protocol.h"

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

static void test_gas_control_is_mutually_exclusive() {
    aquarium::GasControlInput input = {true, true, true, true, true, true, 6.95f, 6.80f};
    aquarium::GasControlOutput output = aquarium::evaluate_gas_control(input);
    TEST_ASSERT_TRUE(output.co2On);
    TEST_ASSERT_FALSE(output.aeratorOn);

    input.ph = 6.70f;
    output = aquarium::evaluate_gas_control(input);
    TEST_ASSERT_FALSE(output.co2On);
    TEST_ASSERT_TRUE(output.aeratorOn);

}

static void test_ato_requires_low_valid_level_and_obeys_interlocks() {
    aquarium::AtoControlInput input = {true, true, true, false, true, false, false};
    TEST_ASSERT_TRUE(aquarium::evaluate_ato_control(input));

    input.waterLevelHigh = true;
    TEST_ASSERT_FALSE(aquarium::evaluate_ato_control(input));
    input.waterLevelHigh = false;
    input.waterLevelMaxReached = true;
    TEST_ASSERT_FALSE(aquarium::evaluate_ato_control(input));
    input.waterLevelMaxReached = false;
    input.waterLevelMaxValid = false;
    TEST_ASSERT_FALSE(aquarium::evaluate_ato_control(input));
    input.waterLevelMaxValid = true;
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
        true, 4.50f
    };
    const unsigned int flags = aquarium::evaluate_alarm_flags(input);
    TEST_ASSERT_BITS_HIGH(aquarium::AlarmTemperatureHigh, flags);
    TEST_ASSERT_BITS_HIGH(aquarium::AlarmPhOutOfRange, flags);
    TEST_ASSERT_BITS_HIGH(aquarium::AlarmWaterLevelLow, flags);
    TEST_ASSERT_BITS_HIGH(aquarium::AlarmSupplyLow, flags);
    TEST_ASSERT_EQUAL_UINT(4U, aquarium::alarm_count(flags));
}

static void test_alarm_flags_report_required_sensor_and_actuator_health() {
    aquarium::AlarmInput input = {
        false, NAN,
        false, NAN,
        false, false,
        false, NAN,
        aquarium::AlarmSensorTemperature |
            aquarium::AlarmSensorPh |
            aquarium::AlarmSensorWaterLevel,
        aquarium::AlarmSensorTemperature |
            aquarium::AlarmSensorPh,
        aquarium::AlarmSensorPh,
        true,
        true
    };
    const unsigned int flags = aquarium::evaluate_alarm_flags(input);
    TEST_ASSERT_BITS_HIGH(aquarium::AlarmSensorMissing, flags);
    TEST_ASSERT_BITS_HIGH(aquarium::AlarmSensorStale, flags);
    TEST_ASSERT_BITS_HIGH(aquarium::AlarmSensorBusFault, flags);
    TEST_ASSERT_BITS_HIGH(aquarium::AlarmActuatorWriteFailed, flags);
    TEST_ASSERT_EQUAL_UINT(4U, aquarium::alarm_count(flags));
}

static void test_alarm_stability_filter_rejects_threshold_chatter() {
    aquarium::AlarmStabilityFilter filter;
    const unsigned int temperature =
        aquarium::AlarmTemperatureHigh;

    TEST_ASSERT_EQUAL_UINT(
        aquarium::AlarmNone,
        filter.update(temperature));
    TEST_ASSERT_EQUAL_UINT(
        aquarium::AlarmNone,
        filter.update(aquarium::AlarmNone));
    TEST_ASSERT_EQUAL_UINT(
        aquarium::AlarmNone,
        filter.update(temperature));
    TEST_ASSERT_EQUAL_UINT(
        temperature,
        filter.update(temperature));

    TEST_ASSERT_EQUAL_UINT(
        temperature,
        filter.update(aquarium::AlarmNone));
    TEST_ASSERT_EQUAL_UINT(
        temperature,
        filter.update(temperature));
    TEST_ASSERT_EQUAL_UINT(
        temperature,
        filter.update(aquarium::AlarmNone));
    TEST_ASSERT_EQUAL_UINT(
        temperature,
        filter.update(aquarium::AlarmNone));
    TEST_ASSERT_EQUAL_UINT(
        aquarium::AlarmNone,
        filter.update(aquarium::AlarmNone));
}

static void test_alarm_stability_filter_raises_safety_alarms_immediately() {
    aquarium::AlarmStabilityFilter filter;
    const unsigned int immediate =
        aquarium::AlarmActuatorWriteFailed;

    TEST_ASSERT_EQUAL_UINT(
        immediate,
        filter.update(immediate));
    TEST_ASSERT_EQUAL_UINT(
        immediate,
        filter.update(aquarium::AlarmNone));
    TEST_ASSERT_EQUAL_UINT(
        immediate,
        filter.update(aquarium::AlarmNone));
    TEST_ASSERT_EQUAL_UINT(
        aquarium::AlarmNone,
        filter.update(aquarium::AlarmNone));
}

static void test_sensor_calibration_and_temperature_compensation() {
    aquarium::SensorCalibration calibration =
        aquarium::default_sensor_calibration();
    calibration.ph_low_raw = 1000;
    calibration.ph_high_raw = 3000;
    calibration.ph_low_reference = 4.01f;
    calibration.ph_high_reference = 6.86f;
    calibration.ec_reference_raw = 12000;
    calibration.ec_reference_us_cm = 1413.0f;
    TEST_ASSERT_TRUE(aquarium::validate_sensor_calibration(calibration));
    TEST_ASSERT_FLOAT_WITHIN(
        0.001f, 4.01f, aquarium::calibrate_ph(1000, calibration));
    TEST_ASSERT_FLOAT_WITHIN(
        0.001f, 6.86f, aquarium::calibrate_ph(3000, calibration));
    TEST_ASSERT_FLOAT_WITHIN(
        0.001f, 5.435f, aquarium::calibrate_ph(2000, calibration));
    TEST_ASSERT_FLOAT_WITHIN(
        0.01f,
        1413.0f,
        aquarium::calibrate_ec(12000, 25.0f, true, calibration));
    TEST_ASSERT_TRUE(
        aquarium::calibrate_ec(12000, 30.0f, true, calibration) < 1413.0f);

    calibration.ph_high_raw = calibration.ph_low_raw;
    TEST_ASSERT_FALSE(aquarium::validate_sensor_calibration(calibration));
    TEST_ASSERT_TRUE(isnan(aquarium::calibrate_ph(2000, calibration)));
}

static void test_sensor_median_filter_rejects_single_spike() {
    aquarium::MedianFilter filter;
    TEST_ASSERT_EQUAL_size_t(0U, filter.size());
    filter.push(1000);
    filter.push(1002);
    filter.push(4095);
    filter.push(999);
    const float median = filter.push(1001);
    TEST_ASSERT_EQUAL_size_t(5U, filter.size());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1001.0f, median);
    filter.reset();
    TEST_ASSERT_EQUAL_size_t(0U, filter.size());
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

}

static void test_web_activity_tracker_capacity_timeout_and_release() {
    aquarium::WebActivityTracker tracker(15000U);
    TEST_ASSERT_FALSE(tracker.touch("bad!", 0U));
    TEST_ASSERT_TRUE(tracker.touch("client_001", 100U));
    TEST_ASSERT_TRUE(tracker.touch("client_002", 200U));
    TEST_ASSERT_EQUAL_UINT8(2U, tracker.active_count(300U));
    char active_ids[aquarium::WebActivityTracker::kCapacity]
                   [aquarium::WebActivityTracker::kMaxSessionIdLength + 1U] = {};
    TEST_ASSERT_EQUAL_UINT8(2U, tracker.copy_active_ids(
        active_ids,
        aquarium::WebActivityTracker::kCapacity,
        300U));
    TEST_ASSERT_EQUAL_STRING("client_001", active_ids[0]);
    TEST_ASSERT_EQUAL_STRING("client_002", active_ids[1]);
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

static void test_ota_finalize_order_and_boot_recovery_are_fail_safe() {
    using aquarium::OtaBootRecovery;
    using aquarium::OtaFinalizeStep;

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(OtaFinalizeStep::MarkNativeValid),
        static_cast<uint8_t>(
            aquarium::next_ota_finalize_step(true, true)));
    // A failed native mark-valid leaves both flags unchanged, so persistence
    // cannot be attempted and the native step is retried.
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(OtaFinalizeStep::MarkNativeValid),
        static_cast<uint8_t>(
            aquarium::next_ota_finalize_step(true, true)));
    // After native success, a failed NVS write leaves persistent_pending set
    // and retries persistence without repeating the native operation.
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(OtaFinalizeStep::PersistAcceptedState),
        static_cast<uint8_t>(
            aquarium::next_ota_finalize_step(false, true)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(OtaFinalizeStep::Complete),
        static_cast<uint8_t>(
            aquarium::next_ota_finalize_step(false, false)));

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(OtaBootRecovery::TrackPendingBoot),
        static_cast<uint8_t>(
            aquarium::evaluate_ota_boot_recovery(true, true, false)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(OtaBootRecovery::FinalizeAcceptedState),
        static_cast<uint8_t>(
            aquarium::evaluate_ota_boot_recovery(true, false, false)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(OtaBootRecovery::ClearAbortedState),
        static_cast<uint8_t>(
            aquarium::evaluate_ota_boot_recovery(true, false, true)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(OtaBootRecovery::None),
        static_cast<uint8_t>(
            aquarium::evaluate_ota_boot_recovery(false, true, false)));
}

static void write_u16_le(uint8_t *target, uint16_t value) {
    target[0] = static_cast<uint8_t>(value & 0xFFU);
    target[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
}

static void write_u32_le(uint8_t *target, uint32_t value) {
    target[0] = static_cast<uint8_t>(value & 0xFFU);
    target[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
    target[2] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
    target[3] = static_cast<uint8_t>((value >> 24U) & 0xFFU);
}

static void build_valid_ota_header(uint8_t *header,
                                   aquarium::OtaPackageTarget target,
                                   const char *version,
                                   uint32_t security_version) {
    memset(header, 0, aquarium::kOtaPackageHeaderBytes);
    memcpy(header, "AQCYDOTA", 8U);
    write_u16_le(header + 8U, aquarium::kOtaPackageFormatVersion);
    write_u16_le(
        header + 10U,
        static_cast<uint16_t>(aquarium::kOtaPackageHeaderBytes));
    header[12U] = aquarium::kOtaPackageAlgorithmRsa3072PssSha256;
    header[13U] = static_cast<uint8_t>(target);
    write_u32_le(header + 16U, 1724576U);
    write_u32_le(header + 20U, security_version);
    for (uint8_t index = 0U; index < 32U; ++index) {
        header[24U + index] = static_cast<uint8_t>(index + 1U);
    }
    snprintf(reinterpret_cast<char *>(header + 56U), 16U, "%s", version);
    snprintf(reinterpret_cast<char *>(header + 72U), 16U, "%s", "aquacyd-cyd");
    memcpy(header + 88U, "9470c281de5f898f", 16U);
    snprintf(
        reinterpret_cast<char *>(header + 104U),
        20U,
        "%s",
        "fae2a25e95e66141e36");
    write_u16_le(
        header + 124U,
        aquarium::kOtaPackageMinimumBootloaderVersion);
    for (size_t index = aquarium::kOtaPackageSignedMetadataBytes;
         index < aquarium::kOtaPackageHeaderBytes;
         ++index) {
        header[index] = static_cast<uint8_t>((index % 251U) + 1U);
    }
}

static void test_ota_package_header_and_policy_accept_valid_release() {
    uint8_t header[aquarium::kOtaPackageHeaderBytes] = {};
    build_valid_ota_header(
        header, aquarium::OtaPackageTarget::Ili9341, "5.1.0", 1U);
    aquarium::OtaPackageMetadata metadata = {};
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(aquarium::OtaPackageValidation::Valid),
        static_cast<uint8_t>(aquarium::parse_ota_package_header(
            header, sizeof(header), &metadata)));
    TEST_ASSERT_EQUAL_STRING("5.1.0", metadata.firmware_version);
    TEST_ASSERT_EQUAL_STRING("aquacyd-cyd", metadata.product_id);
    TEST_ASSERT_EQUAL_STRING("9470c281de5f898f", metadata.key_id);
    TEST_ASSERT_EQUAL_UINT32(1724576U, metadata.image_bytes);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(aquarium::OtaPackageValidation::Valid),
        static_cast<uint8_t>(aquarium::validate_ota_package_metadata(
            metadata,
            aquarium::OtaPackageTarget::Ili9341,
            "5.0.0",
            1U,
            aquarium::kOtaPackageMinimumBootloaderVersion,
            "9470c281de5f898f",
            1966080U)));
}

static void test_ota_package_rejects_wrong_target_downgrade_and_security_rollback() {
    uint8_t header[aquarium::kOtaPackageHeaderBytes] = {};
    build_valid_ota_header(
        header, aquarium::OtaPackageTarget::St7789, "4.9.9", 1U);
    aquarium::OtaPackageMetadata metadata = {};
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(aquarium::OtaPackageValidation::Valid),
        static_cast<uint8_t>(aquarium::parse_ota_package_header(
            header, sizeof(header), &metadata)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(aquarium::OtaPackageValidation::WrongTarget),
        static_cast<uint8_t>(aquarium::validate_ota_package_metadata(
            metadata,
            aquarium::OtaPackageTarget::Ili9341,
            "5.0.0",
            1U,
            1U,
            "9470c281de5f898f",
            1966080U)));
    metadata.target = aquarium::OtaPackageTarget::Ili9341;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(aquarium::OtaPackageValidation::DowngradeBlocked),
        static_cast<uint8_t>(aquarium::validate_ota_package_metadata(
            metadata,
            aquarium::OtaPackageTarget::Ili9341,
            "5.0.0",
            1U,
            1U,
            "9470c281de5f898f",
            1966080U)));
    snprintf(metadata.firmware_version, sizeof(metadata.firmware_version), "%s", "5.1.0");
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            aquarium::OtaPackageValidation::SecurityRollbackBlocked),
        static_cast<uint8_t>(aquarium::validate_ota_package_metadata(
            metadata,
            aquarium::OtaPackageTarget::Ili9341,
            "5.0.0",
            2U,
            1U,
            "9470c281de5f898f",
            1966080U)));
}

static void test_ota_package_rejects_noncanonical_or_truncated_headers() {
    uint8_t header[aquarium::kOtaPackageHeaderBytes] = {};
    build_valid_ota_header(
        header, aquarium::OtaPackageTarget::Ili9341, "5.1.0", 1U);
    aquarium::OtaPackageMetadata metadata = {};
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(aquarium::OtaPackageValidation::HeaderTooShort),
        static_cast<uint8_t>(aquarium::parse_ota_package_header(
            header, sizeof(header) - 1U, &metadata)));
    header[0] ^= 0x01U;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(aquarium::OtaPackageValidation::InvalidMagic),
        static_cast<uint8_t>(aquarium::parse_ota_package_header(
            header, sizeof(header), &metadata)));
    header[0] ^= 0x01U;
    header[127U] = 1U;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(aquarium::OtaPackageValidation::InvalidFlags),
        static_cast<uint8_t>(aquarium::parse_ota_package_header(
            header, sizeof(header), &metadata)));
}

static void test_semantic_version_comparison_is_canonical_and_overflow_safe() {
    TEST_ASSERT_EQUAL_INT(0, aquarium::compare_semantic_versions("5.1.0", "5.1.0"));
    TEST_ASSERT_EQUAL_INT(1, aquarium::compare_semantic_versions("5.10.0", "5.9.9"));
    TEST_ASSERT_EQUAL_INT(-1, aquarium::compare_semantic_versions("4.9.9", "5.0.0"));
    TEST_ASSERT_EQUAL_INT(
        INT_MIN,
        aquarium::compare_semantic_versions("05.1.0", "5.1.0"));
    TEST_ASSERT_EQUAL_INT(
        INT_MIN,
        aquarium::compare_semantic_versions("4294967296.0.0", "5.1.0"));
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

static void test_ato_runtime_safety_limit_enforced_during_manual_override() {
    aquarium::ControlModeManager modes;
    aquarium::RuntimeLimiter ato_limiter;

    const uint32_t start_ms = 1000U;
    const uint32_t limit_ms = 120000U; // 120 seconds ATO limit

    // Automatic demand is false
    const bool auto_water_fill = false;

    // User enables manual override for water dosing (ATO pump)
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(aquarium::ControlModeResult::Applied),
        static_cast<uint8_t>(modes.set_override(
            aquarium::OutputTarget::WaterDosing, true, 3600U, start_ms)));

    // Verify manual override forces candidate to true
    bool candidate = modes.resolve(aquarium::OutputTarget::WaterDosing, auto_water_fill, start_ms);
    TEST_ASSERT_TRUE(candidate);

    // Initial evaluation: pump turns ON and starts tracking runtime
    bool tripped = false;
    bool on = ato_limiter.update(candidate, start_ms, limit_ms, &tripped);
    TEST_ASSERT_TRUE(on);
    TEST_ASSERT_FALSE(tripped);
    TEST_ASSERT_EQUAL_UINT32(start_ms, ato_limiter.started_ms);

    // Mid-run: pump is still allowed ON, start timer is NOT reset
    on = ato_limiter.update(candidate, start_ms + 60000U, limit_ms, &tripped);
    TEST_ASSERT_TRUE(on);
    TEST_ASSERT_FALSE(tripped);
    TEST_ASSERT_EQUAL_UINT32(start_ms, ato_limiter.started_ms);

    // Just before limit: still allowed
    on = ato_limiter.update(candidate, start_ms + limit_ms - 1U, limit_ms, &tripped);
    TEST_ASSERT_TRUE(on);
    TEST_ASSERT_FALSE(tripped);

    // Limit exceeded: actuator MUST be cut off and limit tripped/latched
    on = ato_limiter.update(candidate, start_ms + limit_ms, limit_ms, &tripped);
    TEST_ASSERT_FALSE(on);
    TEST_ASSERT_TRUE(tripped);
    TEST_ASSERT_TRUE(ato_limiter.limit_latched);

    // Subsequent evaluations: manual override is STILL true, but actuator remains forced OFF
    candidate = modes.resolve(aquarium::OutputTarget::WaterDosing, auto_water_fill, start_ms + limit_ms + 10000U);
    TEST_ASSERT_TRUE(candidate);
    on = ato_limiter.update(candidate, start_ms + limit_ms + 10000U, limit_ms, &tripped);
    TEST_ASSERT_FALSE(on);
    TEST_ASSERT_FALSE(tripped); // tripped only pulses once upon transition
    TEST_ASSERT_TRUE(ato_limiter.limit_latched);

    // Clear latch (e.g. high float switch trip or manual reset)
    ato_limiter.clear_latch();
    TEST_ASSERT_FALSE(ato_limiter.limit_latched);
    TEST_ASSERT_EQUAL_UINT32(0U, ato_limiter.started_ms);
}

static void test_co2_runtime_safety_limit_enforced_during_manual_override() {
    aquarium::ControlModeManager modes;
    aquarium::RuntimeLimiter co2_limiter;

    const uint32_t start_ms = 5000U;
    const uint32_t limit_ms = 540U * 60000UL; // 540 minutes CO2 limit

    // Automatic demand is false
    const bool auto_co2 = false;

    // User enables manual override for CO2
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(aquarium::ControlModeResult::Applied),
        static_cast<uint8_t>(modes.set_override(
            aquarium::OutputTarget::Co2, true, 86400U, start_ms)));

    bool candidate = modes.resolve(aquarium::OutputTarget::Co2, auto_co2, start_ms);
    TEST_ASSERT_TRUE(candidate);

    // Initial evaluation
    bool tripped = false;
    bool on = co2_limiter.update(candidate, start_ms, limit_ms, &tripped);
    TEST_ASSERT_TRUE(on);
    TEST_ASSERT_FALSE(tripped);
    TEST_ASSERT_EQUAL_UINT32(start_ms, co2_limiter.started_ms);

    // Limit exceeded: actuator MUST be cut off and limit latched
    on = co2_limiter.update(candidate, start_ms + limit_ms, limit_ms, &tripped);
    TEST_ASSERT_FALSE(on);
    TEST_ASSERT_TRUE(tripped);
    TEST_ASSERT_TRUE(co2_limiter.limit_latched);

    // Manual override cannot keep CO2 running past safety limit
    on = co2_limiter.update(candidate, start_ms + limit_ms + 50000U, limit_ms, &tripped);
    TEST_ASSERT_FALSE(on);
    TEST_ASSERT_TRUE(co2_limiter.limit_latched);
}

static void test_feeding_schedule_trigger_succeeds_under_simulated_tick_jitter() {
    aquarium::FeedingTriggerLatch latch;
    const aquarium::TimeOfDay feeding_time = {14U, 0U}; // 14:00
    const uint16_t target_minute = 14U * 60U;          // 840 minutes

    // 13:59:59 - not feeding time yet
    TEST_ASSERT_FALSE(latch.evaluate(target_minute - 1U, 59U, feeding_time, 1));
    TEST_ASSERT_FALSE(aquarium::feeding_due(target_minute - 1U, 59U, feeding_time));

    // Jitter occurrence: tick drifts past second 0, arrives at second 1 (14:00:01)
    // Both feeding_due and FeedingTriggerLatch must recognize the trigger!
    TEST_ASSERT_TRUE(aquarium::feeding_due(target_minute, 1U, feeding_time));
    TEST_ASSERT_TRUE(latch.evaluate(target_minute, 1U, feeding_time, 1));

    // Next tick at second 2 (14:00:02): latch prevents duplicate feeding
    TEST_ASSERT_FALSE(latch.evaluate(target_minute, 2U, feeding_time, 1));

    // Tick at second 59: still latched
    TEST_ASSERT_FALSE(latch.evaluate(target_minute, 59U, feeding_time, 1));

    // Next minute (14:01:00): not feeding time
    TEST_ASSERT_FALSE(latch.evaluate(target_minute + 1U, 0U, feeding_time, 1));

    // Next day at 14:00:03 (second 0 skipped again on day 2): successfully triggers!
    TEST_ASSERT_TRUE(latch.evaluate(target_minute, 3U, feeding_time, 2));
}

static void test_runtime_limiter_boot_at_millis_zero_and_wraparound() {
    aquarium::RuntimeLimiter limiter;
    bool tripped = false;

    // Boot at millis() == 0: must survive without false tripping
    bool on = limiter.update(true, 0U, 5000U, &tripped);
    TEST_ASSERT_TRUE(on);
    TEST_ASSERT_FALSE(tripped);
    TEST_ASSERT_FALSE(limiter.limit_latched);

    // After 4999ms: still running
    on = limiter.update(true, 4999U, 5000U, &tripped);
    TEST_ASSERT_TRUE(on);
    TEST_ASSERT_FALSE(tripped);

    // At 5000ms: limit reached, cuts off and latches
    on = limiter.update(true, 5000U, 5000U, &tripped);
    TEST_ASSERT_FALSE(on);
    TEST_ASSERT_TRUE(tripped);
    TEST_ASSERT_TRUE(limiter.limit_latched);

    // Unlatching
    limiter.clear_latch();
    TEST_ASSERT_FALSE(limiter.limit_latched);

    // 32-bit wrap-around test across 0xFFFFFFFF
    limiter.reset();
    uint32_t start_ms = 0xFFFFFFF0U;
    uint32_t limit_ms = 50U;
    TEST_ASSERT_TRUE(limiter.update(true, start_ms, limit_ms, &tripped));
    TEST_ASSERT_TRUE(limiter.update(true, 0xFFFFFFFEU, limit_ms, &tripped));
    TEST_ASSERT_TRUE(limiter.update(true, 0x00000010U, limit_ms, &tripped));
    // 0x25 - 0xFFFFFFF0 = 53 >= 50
    bool on_wrap = limiter.update(true, 0x00000025U, limit_ms, &tripped);
    TEST_ASSERT_FALSE(on_wrap);
    TEST_ASSERT_TRUE(tripped);
    TEST_ASSERT_TRUE(limiter.limit_latched);
}

static void test_feeding_schedule_day_latch_persists_across_minutes() {
    aquarium::FeedingTriggerLatch latch;
    const aquarium::TimeOfDay feeding_time = {14U, 0U};
    const uint16_t target_minute = 14U * 60U; // 840

    // Day 1: Trigger at 14:00
    TEST_ASSERT_TRUE(latch.evaluate(target_minute, 0U, feeding_time, 100));

    // Minute advances to 14:01 on Day 1
    TEST_ASSERT_FALSE(latch.evaluate(target_minute + 1U, 0U, feeding_time, 100));

    // Even if time matches target again on same day, day latch prevents repeat:
    TEST_ASSERT_FALSE(latch.evaluate(target_minute, 0U, feeding_time, 100));

    // Day 2 arrives (day_key 101): new day allows feeding to trigger again
    TEST_ASSERT_TRUE(latch.evaluate(target_minute, 0U, feeding_time, 101));
}

// ==============================================================================
// Milestone 1: UART Protocol NDJSON Unit Tests (EnterOta, ACK, Telemetry OTA)
// ==============================================================================

static void test_uart_protocol_enter_ota_serialization_round_trip_with_credentials() {
    AquaProtocol::AquariumCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = AquaProtocol::CommandType::EnterOta;
    strncpy(cmd.ota_ssid, "HomeAquarium_2.4G", sizeof(cmd.ota_ssid) - 1);
    cmd.ota_ssid[sizeof(cmd.ota_ssid) - 1] = '\0';
    strncpy(cmd.ota_pass, "SuperSecretPassword123!", sizeof(cmd.ota_pass) - 1);
    cmd.ota_pass[sizeof(cmd.ota_pass) - 1] = '\0';

    String json = AquaProtocol::serializeCommand(cmd);
    TEST_ASSERT_TRUE(json.length() > 0);
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"cmd\":\"enter_ota\""));
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"ssid\":\"HomeAquarium_2.4G\""));
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"pass\":\"SuperSecretPassword123!\""));

    AquaProtocol::AquariumMessage msg;
    memset(&msg, 0, sizeof(msg));
    bool parsed = AquaProtocol::parseMessage(json.c_str(), msg);
    TEST_ASSERT_TRUE(parsed);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AquaProtocol::MessageType::Command), static_cast<uint8_t>(msg.type));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AquaProtocol::CommandType::EnterOta), static_cast<uint8_t>(msg.command.type));
    TEST_ASSERT_EQUAL_STRING("HomeAquarium_2.4G", msg.command.ota_ssid);
    TEST_ASSERT_EQUAL_STRING("SuperSecretPassword123!", msg.command.ota_pass);
}

static void test_uart_protocol_enter_ota_serialization_default_credentials_omitted() {
    AquaProtocol::AquariumCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = AquaProtocol::CommandType::EnterOta;
    cmd.ota_ssid[0] = '\0';
    cmd.ota_pass[0] = '\0';

    String json = AquaProtocol::serializeCommand(cmd);
    TEST_ASSERT_TRUE(json.length() > 0);
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"cmd\":\"enter_ota\""));
    TEST_ASSERT_NULL(strstr(json.c_str(), "\"ssid\""));
    TEST_ASSERT_NULL(strstr(json.c_str(), "\"pass\""));

    AquaProtocol::AquariumMessage msg;
    memset(&msg, 0, sizeof(msg));
    bool parsed = AquaProtocol::parseMessage(json.c_str(), msg);
    TEST_ASSERT_TRUE(parsed);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AquaProtocol::MessageType::Command), static_cast<uint8_t>(msg.type));
    TEST_ASSERT_EQUAL_STRING("", msg.command.ota_ssid);
    TEST_ASSERT_EQUAL_STRING("", msg.command.ota_pass);
}

static void test_uart_protocol_enter_ota_deserialization_buffer_boundary_safety() {
    const char *long_json =
        "{\"type\":\"cmd\",\"cmd\":\"enter_ota\","
        "\"ssid\":\"1234567890123456789012345678901234567890\","
        "\"pass\":\"12345678901234567890123456789012345678901234567890123456789012345678901234567890\"}";

    AquaProtocol::AquariumMessage msg;
    memset(&msg, 0, sizeof(msg));
    bool parsed = AquaProtocol::parseMessage(long_json, msg);
    TEST_ASSERT_TRUE(parsed);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AquaProtocol::CommandType::EnterOta), static_cast<uint8_t>(msg.command.type));
    TEST_ASSERT_TRUE(strlen(msg.command.ota_ssid) <= 32);
    TEST_ASSERT_TRUE(strlen(msg.command.ota_pass) <= 64);
    TEST_ASSERT_EQUAL_UINT8('\0', msg.command.ota_ssid[sizeof(msg.command.ota_ssid) - 1]);
    TEST_ASSERT_EQUAL_UINT8('\0', msg.command.ota_pass[sizeof(msg.command.ota_pass) - 1]);
}

static void test_uart_protocol_ack_enter_ota_sta_success() {
    String ack_json = AquaProtocol::serializeAck("enter_ota", true, "STA:192.168.1.150");
    TEST_ASSERT_TRUE(ack_json.length() > 0);
    TEST_ASSERT_NOT_NULL(strstr(ack_json.c_str(), "\"cmd\":\"enter_ota\""));
    TEST_ASSERT_NOT_NULL(strstr(ack_json.c_str(), "\"ok\":true"));

    AquaProtocol::AquariumMessage msg;
    memset(&msg, 0, sizeof(msg));
    bool parsed = AquaProtocol::parseMessage(ack_json.c_str(), msg);
    TEST_ASSERT_TRUE(parsed);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AquaProtocol::MessageType::Ack), static_cast<uint8_t>(msg.type));
    TEST_ASSERT_EQUAL_STRING("enter_ota", msg.ack.cmd);
    TEST_ASSERT_TRUE(msg.ack.ok);
    TEST_ASSERT_EQUAL_STRING("STA:192.168.1.150", msg.ack.message);
}

static void test_uart_protocol_ack_enter_ota_ap_fallback() {
    String ack_json = AquaProtocol::serializeAck("enter_ota", true, "AP:192.168.4.1");
    AquaProtocol::AquariumMessage msg;
    memset(&msg, 0, sizeof(msg));
    bool parsed = AquaProtocol::parseMessage(ack_json.c_str(), msg);
    TEST_ASSERT_TRUE(parsed);
    TEST_ASSERT_TRUE(msg.ack.ok);
    TEST_ASSERT_EQUAL_STRING("AP:192.168.4.1", msg.ack.message);
}

static void test_uart_protocol_ack_status_string_compatibility() {
    const char *ack_status_ok = "{\"type\":\"ack\",\"cmd\":\"enter_ota\",\"status\":\"ok\",\"msg\":\"OTA Ready\"}";
    AquaProtocol::AquariumMessage msg;
    memset(&msg, 0, sizeof(msg));
    bool parsed = AquaProtocol::parseMessage(ack_status_ok, msg);
    TEST_ASSERT_TRUE(parsed);
    TEST_ASSERT_TRUE(msg.ack.ok);
    TEST_ASSERT_EQUAL_STRING("OTA Ready", msg.ack.message);

    const char *ack_status_err = "{\"type\":\"ack\",\"cmd\":\"enter_ota\",\"status\":\"error\",\"msg\":\"Relay fail\"}";
    memset(&msg, 0, sizeof(msg));
    parsed = AquaProtocol::parseMessage(ack_status_err, msg);
    TEST_ASSERT_TRUE(parsed);
    TEST_ASSERT_FALSE(msg.ack.ok);
    TEST_ASSERT_EQUAL_STRING("Relay fail", msg.ack.message);
}

static void test_uart_protocol_ack_missing_optional_message() {
    const char *ack_no_msg = "{\"type\":\"ack\",\"cmd\":\"enter_ota\",\"ok\":true}";
    AquaProtocol::AquariumMessage msg;
    memset(&msg, 0, sizeof(msg));
    bool parsed = AquaProtocol::parseMessage(ack_no_msg, msg);
    TEST_ASSERT_TRUE(parsed);
    TEST_ASSERT_TRUE(msg.ack.ok);
    TEST_ASSERT_EQUAL_STRING("", msg.ack.message);
}

static void test_uart_protocol_telemetry_ota_active_serialization_round_trip() {
    AquaProtocol::AquariumTelemetry t;
    memset(&t, 0, sizeof(t));
    t.timestamp = 1773852100UL;
    t.temperature = 24.80f;
    t.temp_target = 25.00f;
    t.temp_hysteresis = 0.30f;
    t.ph = 6.85f;
    t.ec = 415.0f;
    t.relay_states = 0;
    t.alarms = 0;
    t.water_level_low = false;
    t.water_level_max_reached = true;
    t.water_level_max_valid = true;
    t.uptime_seconds = 300UL;
    t.safe_mode = true;
    t.light_mode = 1;
    t.ota_mode = true;
    strncpy(t.ota_ip, "192.168.1.150", sizeof(t.ota_ip) - 1);
    t.ota_ip[sizeof(t.ota_ip) - 1] = '\0';

    String line = AquaProtocol::serializeTelemetry(t);
    TEST_ASSERT_TRUE(line.length() > 0);
    TEST_ASSERT_NOT_NULL(strstr(line.c_str(), "\"ota_mode\":true"));
    TEST_ASSERT_NOT_NULL(strstr(line.c_str(), "\"ota_ip\":\"192.168.1.150\""));
    TEST_ASSERT_NOT_NULL(strstr(line.c_str(), "\"safe\":true"));
    TEST_ASSERT_NOT_NULL(strstr(line.c_str(), "\"w_max\":true"));
    TEST_ASSERT_NOT_NULL(strstr(line.c_str(), "\"w_max_valid\":true"));

    AquaProtocol::AquariumMessage msg;
    memset(&msg, 0, sizeof(msg));
    bool parsed = AquaProtocol::parseMessage(line.c_str(), msg);
    TEST_ASSERT_TRUE(parsed);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AquaProtocol::MessageType::Telemetry), static_cast<uint8_t>(msg.type));
    TEST_ASSERT_TRUE(msg.telemetry.ota_mode);
    TEST_ASSERT_EQUAL_STRING("192.168.1.150", msg.telemetry.ota_ip);
    TEST_ASSERT_TRUE(msg.telemetry.safe_mode);
    TEST_ASSERT_TRUE(msg.telemetry.water_level_max_reached);
    TEST_ASSERT_TRUE(msg.telemetry.water_level_max_valid);
    TEST_ASSERT_TRUE(msg.telemetry.water_level_max_present);
}

static void test_uart_protocol_telemetry_ota_normal_mode_inactive() {
    AquaProtocol::AquariumTelemetry t;
    memset(&t, 0, sizeof(t));
    t.timestamp = 1773852100UL;
    t.temperature = 25.0f;
    t.temp_target = 25.0f;
    t.temp_hysteresis = 0.3f;
    t.ph = 7.0f;
    t.ec = 400.0f;
    t.water_level_max_reached = false;
    t.water_level_max_valid = true;
    t.ota_mode = false;
    t.ota_ip[0] = '\0';

    String line = AquaProtocol::serializeTelemetry(t);
    TEST_ASSERT_TRUE(line.length() > 0);
    TEST_ASSERT_NOT_NULL(strstr(line.c_str(), "\"ota_mode\":false"));
    TEST_ASSERT_NULL(strstr(line.c_str(), "\"ota_ip\""));

    AquaProtocol::AquariumMessage msg;
    memset(&msg, 0, sizeof(msg));
    bool parsed = AquaProtocol::parseMessage(line.c_str(), msg);
    TEST_ASSERT_TRUE(parsed);
    TEST_ASSERT_FALSE(msg.telemetry.ota_mode);
    TEST_ASSERT_EQUAL_STRING("", msg.telemetry.ota_ip);
    TEST_ASSERT_FALSE(msg.telemetry.water_level_max_reached);
    TEST_ASSERT_TRUE(msg.telemetry.water_level_max_valid);
    TEST_ASSERT_TRUE(msg.telemetry.water_level_max_present);
}

static void test_uart_protocol_telemetry_backward_compatibility_legacy_frames() {
    const char *legacy_frame =
        "{\"type\":\"telemetry\",\"ts\":1700000000,\"temp\":25.50,\"temp_tgt\":25.00,"
        "\"temp_hyst\":0.30,\"ph\":7.10,\"ec\":420.0,\"relays\":3,\"alarms\":0,"
        "\"w_low\":false,\"uptime\":3600,\"safe\":false,\"lmode\":1}";

    AquaProtocol::AquariumMessage msg;
    memset(&msg, 0, sizeof(msg));
    bool parsed = AquaProtocol::parseMessage(legacy_frame, msg);
    TEST_ASSERT_TRUE(parsed);
    TEST_ASSERT_FALSE(msg.telemetry.ota_mode);
    TEST_ASSERT_EQUAL_STRING("", msg.telemetry.ota_ip);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.50f, msg.telemetry.temperature);
    TEST_ASSERT_EQUAL_UINT8(3, msg.telemetry.relay_states);
    TEST_ASSERT_FALSE(msg.telemetry.water_level_max_valid);
    TEST_ASSERT_FALSE(msg.telemetry.water_level_max_present);
}

static void test_uart_protocol_telemetry_ota_fallback_key_support() {
    const char *short_key_frame =
        "{\"type\":\"telemetry\",\"ts\":1700000000,\"temp\":25.0,\"temp_tgt\":25.0,"
        "\"temp_hyst\":0.3,\"ph\":7.0,\"ec\":400.0,\"relays\":0,\"alarms\":0,"
        "\"w_low\":false,\"uptime\":100,\"safe\":true,\"lmode\":1,"
        "\"ota\":true,\"ota_ip\":\"192.168.4.1\"}";

    AquaProtocol::AquariumMessage msg;
    memset(&msg, 0, sizeof(msg));
    bool parsed = AquaProtocol::parseMessage(short_key_frame, msg);
    TEST_ASSERT_TRUE(parsed);
    TEST_ASSERT_TRUE(msg.telemetry.ota_mode);
    TEST_ASSERT_EQUAL_STRING("192.168.4.1", msg.telemetry.ota_ip);
}

static void test_uart_protocol_telemetry_ip_buffer_bounds_check() {
    const char *long_ip_frame =
        "{\"type\":\"telemetry\",\"ts\":1700000000,\"temp\":25.0,\"temp_tgt\":25.0,"
        "\"temp_hyst\":0.3,\"ph\":7.0,\"ec\":400.0,\"relays\":0,\"alarms\":0,"
        "\"w_low\":false,\"uptime\":100,\"safe\":true,\"lmode\":1,"
        "\"ota_mode\":true,\"ota_ip\":\"2001:0db8:85a3:0000:0000:8a2e:0370:7334\"}";

    AquaProtocol::AquariumMessage msg;
    memset(&msg, 0, sizeof(msg));
    bool parsed = AquaProtocol::parseMessage(long_ip_frame, msg);
    TEST_ASSERT_TRUE(parsed);
    TEST_ASSERT_TRUE(msg.telemetry.ota_mode);
    TEST_ASSERT_TRUE(strlen(msg.telemetry.ota_ip) <= 15);
    TEST_ASSERT_EQUAL_UINT8('\0', msg.telemetry.ota_ip[sizeof(msg.telemetry.ota_ip) - 1]);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_regular_and_wrapped_windows);
    RUN_TEST(test_schedule_modes_override_window);
    RUN_TEST(test_factory_light_profile_boundaries);
    RUN_TEST(test_factory_schedule_outputs);
    RUN_TEST(test_thermostat_hysteresis_and_invalid_input);
    RUN_TEST(test_gas_control_is_mutually_exclusive);
    RUN_TEST(test_ato_requires_low_valid_level_and_obeys_interlocks);
    RUN_TEST(test_alarm_flags_and_count);
    RUN_TEST(test_alarm_flags_report_required_sensor_and_actuator_health);
    RUN_TEST(test_alarm_stability_filter_rejects_threshold_chatter);
    RUN_TEST(test_alarm_stability_filter_raises_safety_alarms_immediately);
    RUN_TEST(test_sensor_calibration_and_temperature_compensation);
    RUN_TEST(test_sensor_median_filter_rejects_single_spike);
    RUN_TEST(test_dev_simulator_is_deterministic_and_coherent);
    RUN_TEST(test_web_activity_tracker_capacity_timeout_and_release);
    RUN_TEST(test_wifi_retry_policy_handles_capacity_and_millis_wrap);
    RUN_TEST(test_timed_overrides_expire_and_safety_modes_win);
    RUN_TEST(test_control_mode_validation_and_target_aliases);
    RUN_TEST(test_idempotency_cache_detects_duplicates_and_conflicts);
    RUN_TEST(test_admin_session_rate_limit_token_and_expiry);
    RUN_TEST(test_admin_session_lockout_survives_millis_wrap);
    RUN_TEST(test_ota_preflight_and_pending_verify_policy);
    RUN_TEST(test_ota_finalize_order_and_boot_recovery_are_fail_safe);
    RUN_TEST(test_ota_package_header_and_policy_accept_valid_release);
    RUN_TEST(test_ota_package_rejects_wrong_target_downgrade_and_security_rollback);
    RUN_TEST(test_ota_package_rejects_noncanonical_or_truncated_headers);
    RUN_TEST(test_semantic_version_comparison_is_canonical_and_overflow_safe);
    RUN_TEST(test_aquael_profile_calibrates_then_cycles_without_blocking);
    RUN_TEST(test_aquael_front_and_rear_state_is_independent);
    RUN_TEST(test_aquael_quick_power_cycle_advances_and_long_off_resets_day);
    RUN_TEST(test_aquael_exact_five_second_boundary_still_advances);
    RUN_TEST(test_aquael_calibration_is_safe_at_millis_zero);
    RUN_TEST(test_ato_runtime_safety_limit_enforced_during_manual_override);
    RUN_TEST(test_co2_runtime_safety_limit_enforced_during_manual_override);
    RUN_TEST(test_feeding_schedule_trigger_succeeds_under_simulated_tick_jitter);
    RUN_TEST(test_runtime_limiter_boot_at_millis_zero_and_wraparound);
    RUN_TEST(test_feeding_schedule_day_latch_persists_across_minutes);

    // Milestone 1: UART Protocol NDJSON tests
    RUN_TEST(test_uart_protocol_enter_ota_serialization_round_trip_with_credentials);
    RUN_TEST(test_uart_protocol_enter_ota_serialization_default_credentials_omitted);
    RUN_TEST(test_uart_protocol_enter_ota_deserialization_buffer_boundary_safety);
    RUN_TEST(test_uart_protocol_ack_enter_ota_sta_success);
    RUN_TEST(test_uart_protocol_ack_enter_ota_ap_fallback);
    RUN_TEST(test_uart_protocol_ack_status_string_compatibility);
    RUN_TEST(test_uart_protocol_ack_missing_optional_message);
    RUN_TEST(test_uart_protocol_telemetry_ota_active_serialization_round_trip);
    RUN_TEST(test_uart_protocol_telemetry_ota_normal_mode_inactive);
    RUN_TEST(test_uart_protocol_telemetry_backward_compatibility_legacy_frames);
    RUN_TEST(test_uart_protocol_telemetry_ota_fallback_key_support);
    RUN_TEST(test_uart_protocol_telemetry_ip_buffer_bounds_check);

    return UNITY_END();
}
