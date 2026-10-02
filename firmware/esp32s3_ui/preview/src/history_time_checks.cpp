#include "ui_history_time.h"
#include <cstdio>
#include <cstring>
bool ui_history_time_checks() {
    using namespace ui_history_time;
    unsigned checks = 0, failures = 0;
    auto require = [&](bool result, const char *name) {
        ++checks;
        if (!result) {
            ++failures;
            std::fprintf(stderr, "FAIL HISTORY_TIME: %s\n", name);
        }
    };
    const uint32_t midnight = encode(2026, 9, 28, 23, 59, 59);
    char text[48];
    format(midnight, text, sizeof(text));
    require(!std::strcmp(text, "23:59:59"), "exact seconds");
    format(midnight + 1, text, sizeof(text), true);
    require(!std::strcmp(text, "29.09 00:00:00"), "midnight retains date");
    format(encode(2028, 2, 29, 23, 59, 59) + 1, text, sizeof(text), true);
    require(!std::strcmp(text, "01.03 00:00:00"), "leap day rollover");
    format(encode(2026, 12, 31, 23, 59, 59) + 1, text, sizeof(text), true);
    require(!std::strcmp(text, "01.01 00:00:00"), "year rollover");
    require(!encode(2026, 2, 29, 12, 0, 0), "invalid leap day");
    require(!encode(2026, 13, 1, 12, 0, 0) && !encode(2026, 0, 1, 12, 0, 0), "month bounds");
    require(!encode(2026, 1, 0, 12, 0, 0) && !encode(2026, 4, 31, 12, 0, 0), "day bounds");
    require(!encode(2023, 1, 1, 0, 0, 0) && !encode(2100, 1, 1, 0, 0, 0), "clock validity interval");
    require(!encode(2026, 1, 1, 24, 0, 0) && !encode(2026, 1, 1, 0, 60, 0) && !encode(2026, 1, 1, 0, 0, 60),
            "time bounds");
    format(0, text, sizeof(text));
    require(!std::strcmp(text, "--:--:--"), "unknown is not fabricated time");
    format(UINT32_MAX, text, sizeof(text));
    require(!std::strcmp(text, "--:--:--"), "invalid future stamp");
    format(encode(2099, 12, 31, 23, 59, 59), text, sizeof(text), true);
    require(!std::strcmp(text, "31.12 23:59:59"), "upper bound representable");
    require(continuous(midnight, midnight + 15, 15), "ordinary sample interval");
    require(continuous(midnight, midnight + 17, 15), "two second rounding tolerance");
    require(!continuous(midnight, midnight + 18, 15), "clock correction detected");
    require(!continuous(midnight, midnight - 3585, 15), "DST rollback detected without resorting samples");
    require(continuous(0, midnight, 15), "unknown clock does not discard sensor data");
    text[0] = 'X';
    format(midnight, text, 0);
    require(text[0] == 'X', "zero capacity untouched");
    format(midnight, nullptr, 0);
    char tiny[2] = {'X', 'Y'};
    format(midnight, tiny, sizeof(tiny));
    require(tiny[1] == 0, "bounded output terminated");
    std::printf("HISTORY_TIME PASS checks=%u failures=%u\n", checks, failures);
    return !failures;
}
