#ifndef AQUABOX_UI_RUNTIME_H
#define AQUABOX_UI_RUNTIME_H

#include "ui_model.h"
#include "ui_theme.h"
#include <stdint.h>

namespace ui_runtime {
enum class NetworkMode : uint8_t { Off, Home, AccessPoint };
struct Preferences {
    bool night = false;
    bool feedback = true;
    bool reducedMotion = false;
    bool lock = false;
    bool ntp = true;
    uint16_t sleepMinutes = 5;
    NetworkMode network = NetworkMode::Off;
    char ssid[33] = "";
    char password[65] = "";
    char apPassword[17] = "";
    char pin[5] = "1234";
    bool autoTheme = false;
    char solarCity[33] = "Warszawa";
    ui_theme::Settings colors;
};
struct Configuration {
    Preferences prefs;
    ui_model::DomainConfig domain;
};
struct DateTime {
    int year, month, day, hour, minute, dst;
    DateTime(int y = 2026, int m = 9, int d = 28, int h = 14, int min = 32, int daylight = -1)
        : year(y), month(m), day(d), hour(h), minute(min), dst(daylight) {}
};
struct Result {
    bool ok;
    const char *message;
};
void init();
bool load(Configuration &config);
Result validate(const Configuration &config);
bool equivalent(const Configuration &first, const Configuration &second);
Result save(const Configuration &config);
Result resetStorage();
Result applyNetwork(const Preferences &prefs);
void service();
void serviceIo();
const char *networkMessage();
void networkDetails(char *text, size_t capacity);
DateTime dateTime();
uint32_t historyTime();
Result setDateTime(const DateTime &time);
Result validateDateTime(const DateTime &time);
bool ambiguousTime(const DateTime &time);
Result syncTime();
Result verifyFirmware();
const char *firmwareMessage();
void requestRestart();
bool storageHealthy();
bool takeVideoRefresh();
bool nightTheme(const Preferences &prefs);
void configureSolar(const Preferences &prefs);
bool takeSolarThemeChange();
void solarStatus(char *text, size_t capacity);
void serviceSolarIo();

#ifdef AQUARIUM_UI_HOST_PREVIEW
void resetFixtures();
void failNextSave();
void storeLegacyFixture(const Configuration &config, bool corrupt = false);
void storeV5Fixture(const Configuration &config, bool corrupt = false);
void storeV6Fixture(const Configuration &config, bool corrupt = false);
void storeV7Fixture(const Configuration &config, bool corrupt = false);
void storeV8Fixture(const Configuration &config, bool corrupt = false);
void storeV9Fixture(const Configuration &config, uint8_t preset, bool corrupt = false);
void storeInvalidColorsFixture(const Configuration &config);
uint32_t restartCount();
uint32_t saveCount();
#endif
} // namespace ui_runtime
#endif
