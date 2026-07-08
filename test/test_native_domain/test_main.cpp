#include <limits.h>
#include <math.h>
#include <stdint.h>

#include <unity.h>

#include "aquarium_automation.h"
#include "aquarium_schedule.h"
#include "dev_simulator.h"
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
    return UNITY_END();
}
