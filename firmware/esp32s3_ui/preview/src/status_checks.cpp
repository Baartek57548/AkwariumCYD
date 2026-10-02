#include "ui_status.h"
#include <cstdio>
#include <cstring>

bool ui_status_checks() {
    using namespace ui_status;
    unsigned failures = 0, checks = 0;
    auto require = [&](bool success, const char *name) {
        ++checks;
        if (!success) {
            ++failures;
            std::fprintf(stderr, "FAIL STATUS: %s\n", name);
        }
    };
    WirelessObservation observation;
    require(classify(observation).mode == WirelessMode::Off, "radio off");
    observation.available = false;
    require(classify(observation).mode == WirelessMode::Unknown, "read failure remains unknown");
    observation.available = true;
    observation.stationConnected = true;
    require(classify(observation).mode == WirelessMode::Off, "disabled radio rejects stale association");
    observation.enabled = true;
    observation.stationConnected = false;
    require(classify(observation).mode == WirelessMode::Disconnected, "configured STA is not connected");
    observation.stationConnected = true;
    const int signals[] = {-126, -81, -80, -71, -70, -61, -60, -1};
    const unsigned strengths[] = {1, 1, 2, 2, 3, 3, 4, 4};
    for (unsigned i = 0; i < sizeof(signals) / sizeof(signals[0]); ++i) {
        observation.rssi = static_cast<int16_t>(signals[i]);
        const Wireless wifi = classify(observation);
        require(wifi.mode == WirelessMode::Home && wifi.signalValid && wifi.signalLevel == strengths[i],
                "RSSI thresholds");
    }
    const int invalidSignals[] = {0, 1, -127, -128};
    for (int invalid : invalidSignals) {
        observation.rssi = static_cast<int16_t>(invalid);
        require(!classify(observation).signalValid && classify(observation).signalLevel == 0,
                "invalid RSSI does not invent strength");
    }
    observation.stationConnected = false;
    observation.apActive = true;
    observation.clients = 2;
    Wireless wifi = classify(observation);
    require(wifi.mode == WirelessMode::AccessPoint && wifi.clients == 2 && !wifi.signalValid,
            "AP has clients but no received-router RSSI");
    observation.stationConnected = true;
    observation.rssi = -65;
    wifi = classify(observation);
    require(wifi.mode == WirelessMode::HomeAndAp && wifi.signalLevel == 3 && wifi.clients == 2,
            "STA and AP coexist without losing signal");
    std::memset(observation.stationName, 'x', 32);
    observation.stationName[4] = '\n';
    wifi = classify(observation);
    require(std::strlen(wifi.stationName) == 32 && wifi.stationName[4] == ' ', "bounded sanitized SSID");
    require(std::strcmp(modeCaption(WirelessMode::Home), "Dom") == 0 &&
                std::strcmp(modeCaption(WirelessMode::AccessPoint), "AP") == 0,
            "compact mode captions");
    init();
    require(read(872).clock.hour == 14 && read(872).clock.minute == 32 &&
                read(872).clock.source == ClockSource::Interface,
            "explicit interface clock fallback");
    require(read(1441).clock.hour == 0 && read(1441).clock.minute == 1, "clock minute normalized");
    require(!previewClock(24, 0, true) && !previewClock(0, 60, true), "reject invalid clock fields");
    require(previewClock(23, 59, true) && read(872).clock.hour == 23 &&
                read(872).clock.source == ClockSource::System,
            "system clock takes priority");
    init();
    std::printf("STATUS %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures == 0;
}
