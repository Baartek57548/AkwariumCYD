#include "ui_status.h"
#include "ui_io.h"
#include <Arduino.h>
#include <cstring>

#ifndef AQUARIUM_UI_HOST_PREVIEW
#include <cstdlib>
#include <ctime>
#endif

namespace ui_status {
namespace {
void copyName(char *destination, const char *source) {
    unsigned index = 0;
    // SSIDs have at most 32 bytes; embedded control characters cannot break the dialog layout.
    while (index < 32 && source[index]) {
        const unsigned char byte = static_cast<unsigned char>(source[index]);
        destination[index] = byte < 32 || byte == 127 ? ' ' : source[index];
        ++index;
    }
    destination[index] = 0;
}
#ifdef AQUARIUM_UI_HOST_PREVIEW
WirelessObservation previewObservation;
bool previewSystemClock = false;
uint16_t previewMinute = 0;
uint32_t previewClockStart = 0;
#else
bool timezoneReady = false;
#endif
} // namespace

Wireless classify(const WirelessObservation &observation) {
    Wireless result;
    if (!observation.available) {
        result.mode = WirelessMode::Unknown;
        return result;
    }
    if (!observation.enabled)
        return result;
    result.mode = observation.stationConnected
                      ? (observation.apActive ? WirelessMode::HomeAndAp : WirelessMode::Home)
                      : (observation.apActive ? WirelessMode::AccessPoint : WirelessMode::Disconnected);
    if (observation.stationConnected) {
        copyName(result.stationName, observation.stationName);
        result.signalValid = observation.rssi > -127 && observation.rssi < 0;
        if (result.signalValid) {
            result.rssi = observation.rssi;
            result.signalLevel = observation.rssi >= -60   ? 4
                                 : observation.rssi >= -70 ? 3
                                 : observation.rssi >= -80 ? 2
                                                           : 1;
        }
    }
    if (observation.apActive) {
        result.clients = observation.clients;
        copyName(result.apName, observation.apName);
    }
    return result;
}
const char *modeCaption(WirelessMode mode) {
    switch (mode) {
    case WirelessMode::Off:
        return "Wi-Fi wył.";
    case WirelessMode::Disconnected:
        return "Brak sieci";
    case WirelessMode::Home:
        return "Dom";
    case WirelessMode::AccessPoint:
        return "AP";
    case WirelessMode::HomeAndAp:
        return "Dom + AP";
    case WirelessMode::Unknown:
        return "Brak danych";
    }
    return "Brak danych";
}
void init() {
#ifdef AQUARIUM_UI_HOST_PREVIEW
    previewObservation = WirelessObservation{};
    previewSystemClock = false;
#else
    // POSIX rules include the Warsaw daylight-saving transitions without fixed UTC offsets.
    timezoneReady = setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1) == 0;
    if (timezoneReady)
        tzset();
#endif
}
Status read(uint16_t interfaceMinute) {
    Status status;
    status.clock.hour = (interfaceMinute % 1440) / 60;
    status.clock.minute = interfaceMinute % 60;
    status.clock.second = static_cast<uint8_t>((millis() / 1000u) % 60u);
#ifdef AQUARIUM_UI_HOST_PREVIEW
    status.wireless = classify(previewObservation);
    if (previewSystemClock) {
        const uint32_t elapsed = static_cast<uint32_t>(millis() - previewClockStart);
        const uint32_t passed = elapsed / 60000u;
        const uint16_t minute = static_cast<uint16_t>((previewMinute + passed) % 1440);
        status.clock = {static_cast<uint8_t>(minute / 60), static_cast<uint8_t>(minute % 60),
                        ClockSource::System, static_cast<uint8_t>((elapsed / 1000u) % 60u)};
    }
#else
    status.wireless = classify(ui_io::radio());
    const time_t epoch = time(nullptr);
    struct tm local {};
    if (timezoneReady && localtime_r(&epoch, &local) && local.tm_year >= 124 && local.tm_year <= 199 &&
        local.tm_hour >= 0 && local.tm_hour < 24 && local.tm_min >= 0 && local.tm_min < 60) {
        status.clock = {static_cast<uint8_t>(local.tm_hour), static_cast<uint8_t>(local.tm_min),
                        ClockSource::System, static_cast<uint8_t>(local.tm_sec)};
    }
#endif
    return status;
}
#ifdef AQUARIUM_UI_HOST_PREVIEW
void previewNetwork(const WirelessObservation &observation) { previewObservation = observation; }
bool previewClock(int hour, int minute, bool systemClock) {
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59)
        return false;
    previewMinute = static_cast<uint16_t>(hour * 60 + minute);
    previewClockStart = millis();
    previewSystemClock = systemClock;
    return true;
}
#endif
} // namespace ui_status
