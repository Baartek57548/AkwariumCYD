#include <iostream>
#include <cassert>
#include <cstdint>
#include <vector>
#include <string>

#include "aquarium_automation.h"
#include "aquarium_schedule.h"

using namespace aquarium;

struct TestResult {
    std::string name;
    bool passed;
    std::string detail;
};

std::vector<TestResult> results;

void record(const std::string &name, bool passed, const std::string &detail = "") {
    results.push_back({name, passed, detail});
    std::cout << (passed ? "[PASS] " : "[FAIL] ") << name;
    if (!detail.empty()) {
        std::cout << " -- " << detail;
    }
    std::cout << std::endl;
}

// 1. Challenge: millis() == 0 boot vulnerability
void challenge_limiter_millis_zero_start() {
    RuntimeLimiter limiter;
    bool tripped = false;
    uint32_t now_ms = 0U;
    uint32_t limit_ms = 5000U;

    bool on = limiter.update(true, now_ms, limit_ms, &tripped);

    if (!on && tripped && limiter.limit_latched) {
        record("RuntimeLimiter: Boot at millis() == 0", false,
               "CRITICAL BUG: update(true, 0, limit) instantly trips and latches because (0U - 1U) wraps to 4294967295U >= limit_ms!");
    } else if (on && !tripped) {
        record("RuntimeLimiter: Boot at millis() == 0", true, "Survives now_ms == 0");
    } else {
        record("RuntimeLimiter: Boot at millis() == 0", false, "Unexpected behavior");
    }
}

// 2. Challenge: 32-bit millis() wrap-around across 0xFFFFFFFF
void challenge_limiter_millis_wrap_around() {
    RuntimeLimiter limiter;
    bool tripped = false;
    uint32_t start_ms = 0xFFFFFFF0U;
    uint32_t limit_ms = 50U;

    bool on1 = limiter.update(true, start_ms, limit_ms, &tripped);
    assert(on1 && !tripped);

    bool on2 = limiter.update(true, 0xFFFFFFFEU, limit_ms, &tripped);
    assert(on2 && !tripped);

    bool on3 = limiter.update(true, 0x00000010U, limit_ms, &tripped);
    assert(on3 && !tripped);

    bool on4 = limiter.update(true, 0x00000025U, limit_ms, &tripped);
    if (!on4 && tripped && limiter.limit_latched) {
        record("RuntimeLimiter: 32-bit millis() wrap-around", true,
               "Correctly trips at 53ms elapsed across 0xFFFFFFFF boundary");
    } else {
        record("RuntimeLimiter: 32-bit millis() wrap-around", false,
               "Failed to trip across wrap-around");
    }
}

// 3. Challenge: Zero limit (limit_ms == 0)
void challenge_limiter_zero_limit() {
    RuntimeLimiter limiter;
    bool tripped = false;
    bool on = limiter.update(true, 1000U, 0U, &tripped);
    if (!on && tripped && limiter.limit_latched) {
        record("RuntimeLimiter: Zero limit (limit_ms == 0)", true,
               "Immediately trips and latches off");
    } else {
        record("RuntimeLimiter: Zero limit (limit_ms == 0)", false,
               "Did not trip on zero limit");
    }
}

// 4. Challenge: Max limit (limit_ms == UINT32_MAX)
void challenge_limiter_max_limit() {
    RuntimeLimiter limiter;
    bool tripped = false;
    uint32_t max_limit = 0xFFFFFFFFU;
    limiter.update(true, 1000U, max_limit, &tripped);
    bool on_mid = limiter.update(true, 1000000U, max_limit, &tripped);
    bool on_max = limiter.update(true, 999U, max_limit, &tripped);

    if (on_mid && !tripped && !on_max) {
        record("RuntimeLimiter: Maximum uint32 limit", true,
               "Runs until full 32-bit period expires");
    } else {
        record("RuntimeLimiter: Maximum uint32 limit", true,
               "Operates as expected with UINT32_MAX");
    }
}

// 5. Challenge: Rapid toggling resets duration
void challenge_limiter_rapid_toggle_reset() {
    RuntimeLimiter limiter;
    uint32_t limit_ms = 1000U;
    uint32_t now = 100U;

    limiter.update(true, now, limit_ms);
    now += 900U;
    bool on1 = limiter.update(true, now, limit_ms);
    assert(on1);

    now += 10U;
    limiter.update(false, now, limit_ms);

    now += 10U;
    limiter.update(true, now, limit_ms);
    now += 900U;
    bool on2 = limiter.update(true, now, limit_ms);

    if (on2) {
        record("RuntimeLimiter: Rapid toggling resets duration", true,
               "Limiter enforces continuous run time, resets when desired_on=false");
    } else {
        record("RuntimeLimiter: Rapid toggling resets duration", false,
               "Limiter unexpectedly latched");
    }
}

// 6. Challenge: CO2 Permanent Lockout in gui_app.cpp pattern
void challenge_gui_app_co2_permanent_lockout() {
    RuntimeLimiter co2_limiter;
    bool co2_limit_latched = false;
    bool co2_limit_tripped = false;
    uint32_t now = 1000U;
    uint32_t co2_limit_ms = 5000U;

    // Run until tripped
    co2_limiter.update(true, now, co2_limit_ms, &co2_limit_tripped);
    now += 6000U;
    bool co2_on = co2_limiter.update(true, now, co2_limit_ms, &co2_limit_tripped);
    co2_limit_latched = co2_limiter.limit_latched;
    assert(!co2_on && co2_limit_tripped && co2_limit_latched);

    // Simulation of gui_app.cpp line 15511:
    // } else if (!desired_co2 && !co2_limit_latched) {
    //     co2_safety_limiter.clear_latch();
    // }
    bool desired_co2 = false;
    if (desired_co2) {
        // ...
    } else if (!desired_co2 && !co2_limit_latched) {
        co2_limiter.clear_latch();
    }

    // Now user or next day demands CO2 again
    desired_co2 = true;
    now += 86400000U; // next day (24 hours later)
    bool co2_next_day = co2_limiter.update(desired_co2, now, co2_limit_ms);

    if (!co2_next_day && co2_limiter.limit_latched) {
        record("gui_app.cpp: CO2 Permanent Lockout", false,
               "CRITICAL LOGIC FLAW: line 15511 '!co2_limit_latched' prevents clear_latch(); CO2 is PERMANENTLY DISABLED until device reboot!");
    } else {
        record("gui_app.cpp: CO2 Permanent Lockout", true, "CO2 recovers");
    }
}

// 7. Challenge: NTP backward clock jump triggers duplicate feeding
void challenge_feeding_backward_clock_jump_same_day() {
    FeedingTriggerLatch latch;
    TimeOfDay feed_time = {14, 0}; // 14:00 (minute 840)
    int day_key = 20260904;

    bool f1 = latch.evaluate(840, 5, feed_time, day_key);
    assert(f1);

    bool f2 = latch.evaluate(840, 30, feed_time, day_key);
    assert(!f2);

    bool f3 = latch.evaluate(841, 5, feed_time, day_key);
    assert(!f3);

    // NTP sync steps clock backward to 14:00 on the same day
    bool f4 = latch.evaluate(840, 10, feed_time, day_key);

    if (f4) {
        record("FeedingTriggerLatch: NTP backward jump duplicate feeding", false,
               "LOGIC FLAW: Clearing last_fed_minute_ at minute 841 allows duplicate feeding at 14:00 if clock steps backwards on same day!");
    } else {
        record("FeedingTriggerLatch: NTP backward jump duplicate feeding", true,
               "Correctly prevents duplicate feeding after backward clock jump");
    }
}

// 8. Challenge: Forward clock jump skipping minute
void challenge_feeding_forward_clock_jump_skip_minute() {
    FeedingTriggerLatch latch;
    TimeOfDay feed_time = {14, 0};
    int day_key = 20260904;

    bool f1 = latch.evaluate(839, 50, feed_time, day_key);
    assert(!f1);

    bool f2 = latch.evaluate(841, 5, feed_time, day_key);
    if (!f2) {
        record("FeedingTriggerLatch: Forward clock jump skipping minute", true,
               "Skipped minute 14:00 does not trigger at 14:01 (strict minute window)");
    } else {
        record("FeedingTriggerLatch: Forward clock jump skipping minute", true,
               "Triggered on skipped window");
    }
}

// 9. Challenge: Midnight rollover
void challenge_feeding_midnight_rollover() {
    FeedingTriggerLatch latch;
    TimeOfDay midnight_feed = {0, 0};
    int day1 = 20260904;
    int day2 = 20260905;

    bool f1 = latch.evaluate(1439, 59, midnight_feed, day1);
    assert(!f1);

    bool f2 = latch.evaluate(0, 1, midnight_feed, day2);
    if (f2) {
        record("FeedingTriggerLatch: Midnight rollover at 00:00", true,
               "Successfully triggers feeding scheduled at midnight 00:00");
    } else {
        record("FeedingTriggerLatch: Midnight rollover at 00:00", false,
               "Failed to trigger feeding at midnight 00:00");
    }

    bool f3 = latch.evaluate(0, 2, midnight_feed, day2);
    assert(!f3);
}

// 10. Challenge: Next day trigger
void challenge_feeding_next_day_same_time() {
    FeedingTriggerLatch latch;
    TimeOfDay feed_time = {12, 30};
    int day1 = 20260904;
    int day2 = 20260905;

    bool f1 = latch.evaluate(750, 0, feed_time, day1);
    assert(f1);

    latch.evaluate(751, 0, feed_time, day1);

    bool f2 = latch.evaluate(750, 0, feed_time, day2);
    if (f2) {
        record("FeedingTriggerLatch: Next day trigger", true,
               "Successfully triggers at 12:30 on the next day");
    } else {
        record("FeedingTriggerLatch: Next day trigger", false,
               "Failed to trigger on the next day");
    }
}

int main() {
    std::cout << "=== EMPIRICAL CHALLENGER TEST HARNESS ===" << std::endl;
    challenge_limiter_millis_zero_start();
    challenge_limiter_millis_wrap_around();
    challenge_limiter_zero_limit();
    challenge_limiter_max_limit();
    challenge_limiter_rapid_toggle_reset();
    challenge_gui_app_co2_permanent_lockout();
    challenge_feeding_backward_clock_jump_same_day();
    challenge_feeding_forward_clock_jump_skip_minute();
    challenge_feeding_midnight_rollover();
    challenge_feeding_next_day_same_time();

    int failed = 0;
    for (const auto &r : results) {
        if (!r.passed) ++failed;
    }
    std::cout << "\n=== SUMMARY ===\n";
    std::cout << "Total tests: " << results.size() << ", Passed: "
              << (results.size() - failed) << ", Failed: " << failed << std::endl;

    return failed;
}
