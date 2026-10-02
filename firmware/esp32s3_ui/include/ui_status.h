#ifndef AQUABOX_UI_STATUS_H
#define AQUABOX_UI_STATUS_H

#include <stdint.h>

namespace ui_status {

enum class WirelessMode : uint8_t { Off, Disconnected, Home, AccessPoint, HomeAndAp, Unknown };
enum class ClockSource : uint8_t { Interface, System };

struct WirelessObservation {
    bool available = true;
    bool enabled = false;
    bool stationConnected = false;
    bool apActive = false;
    int16_t rssi = 0;
    uint8_t clients = 0;
    char stationName[33] = "";
    char apName[33] = "";
};
struct Wireless {
    WirelessMode mode = WirelessMode::Off;
    bool signalValid = false;
    uint8_t signalLevel = 0;
    int16_t rssi = 0;
    uint8_t clients = 0;
    char stationName[33] = "";
    char apName[33] = "";
};
struct Clock {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    ClockSource source;
    Clock(uint8_t hours = 0, uint8_t minutes = 0, ClockSource clockSource = ClockSource::Interface,
          uint8_t seconds = 0)
        : hour(hours), minute(minutes), second(seconds), source(clockSource) {}
};
struct Status {
    Wireless wireless;
    Clock clock;
};

void init();
Status read(uint16_t interfaceMinute);
Wireless classify(const WirelessObservation &observation);
const char *modeCaption(WirelessMode mode);

#ifdef AQUARIUM_UI_HOST_PREVIEW
void previewNetwork(const WirelessObservation &observation);
bool previewClock(int hour, int minute, bool systemClock);
#endif

} // namespace ui_status
#endif
