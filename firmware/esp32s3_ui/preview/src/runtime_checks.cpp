#include "ui_model.h"
#include "ui_runtime.h"
#include <Arduino.h>
#include <cstdio>
#include <cstring>

bool ui_runtime_checks() {
    using namespace ui_runtime;
    int checks = 0, failures = 0;
    auto require = [&](bool success, const char *name) {
        ++checks;
        if (!success) {
            ++failures;
            std::fprintf(stderr, "FAIL RUNTIME: %s\n", name);
        }
    };
    resetFixtures();
    ui_model::init(0);
    Configuration config;
    require(!load(config) && storageHealthy(), "missing storage loads defaults without error");
    require(config.prefs.network == NetworkMode::Off && std::strlen(config.prefs.apPassword) >= 8,
            "radio off and generated AP key");
    require(validate(config).ok, "valid defaults");
    require(!config.prefs.autoTheme && std::strcmp(config.prefs.solarCity, "Warszawa") == 0,
            "solar theme starts opt-in with explicit Polish location");
    require(config.prefs.colors.preset[0] == 0 && config.prefs.colors.preset[1] == 0,
            "new color settings start with the current AquaBox palette");
    config.prefs.autoTheme = true;
    std::snprintf(config.prefs.solarCity, sizeof(config.prefs.solarCity), "Gdańsk");
    config.prefs.night = true;
    config.prefs.colors.preset[0] = 2;
    config.prefs.colors.preset[1] = 1;
    config.domain.ranges[0] = {23, 27};
    require(save(config).ok, "save complete configuration");
    Configuration restored;
    require(load(restored) && restored.prefs.night && restored.domain.ranges[0].maximum == 27,
            "reload persisted preferences and ranges");
    require(restored.prefs.autoTheme && std::strcmp(restored.prefs.solarCity, "Gdańsk") == 0,
            "automatic theme and city persist");
    require(restored.prefs.colors.preset[0] == 2 && restored.prefs.colors.preset[1] == 1,
            "independent light and dark presets persist");
    config.prefs.night = false;
    failNextSave();
    require(!save(config).ok, "injected save failure reported");
    require(load(restored) && restored.prefs.night, "failed write preserves previous configuration");
    std::snprintf(config.prefs.pin, sizeof(config.prefs.pin), "12x4");
    require(!validate(config).ok, "reject non-digit PIN");
    std::snprintf(config.prefs.pin, sizeof(config.prefs.pin), "9876");
    config.prefs.network = NetworkMode::Home;
    config.prefs.ssid[0] = 0;
    require(!validate(config).ok, "station needs SSID");
    std::snprintf(config.prefs.ssid, sizeof(config.prefs.ssid), "Jamnik");
    std::snprintf(config.prefs.password, sizeof(config.prefs.password), "short");
    require(!validate(config).ok, "reject short WPA password");
    config.prefs.password[0] = 0;
    require(validate(config).ok, "open station network allowed explicitly");
    require(applyNetwork(config.prefs).ok && syncTime().ok, "station permits NTP request");
    config.prefs.network = NetworkMode::AccessPoint;
    require(applyNetwork(config.prefs).ok && !syncTime().ok, "AP is not NTP uplink");
    config.prefs.network = NetworkMode::Off;
    require(applyNetwork(config.prefs).ok && !syncTime().ok, "off has no NTP uplink");
    require(!setDateTime({2026, 2, 29, 12, 0}).ok, "reject non-leap February 29");
    require(setDateTime({2028, 2, 29, 23, 59}).ok, "valid leap day");
    require(ui_model::demoMinute() == 1439, "manual time updates model clock");
    require(!setDateTime({2026, 3, 29, 2, 30}).ok, "reject missing DST hour");
    require(!setDateTime({2026, 10, 25, 2, 30}).ok, "reject ambiguous autumn hour without occurrence");
    require(setDateTime({2026, 10, 25, 2, 30, 1}).ok, "first autumn occurrence accepted explicitly");
    require(setDateTime({2026, 10, 25, 2, 30, 0}).ok, "second autumn occurrence accepted explicitly");
    require(!setDateTime({2026, 12, 1, 12, 0, 2}).ok, "invalid DST selector rejected");
    require(equivalent(config, config), "configuration equality ignores padding");
    Configuration modified = config;
    modified.prefs.ntp = !modified.prefs.ntp;
    require(!equivalent(config, modified), "NTP change is durable configuration change");
    modified = config;
    modified.prefs.autoTheme = !modified.prefs.autoTheme;
    require(!equivalent(config, modified), "automatic theme change detected");
    modified = config;
    std::snprintf(modified.prefs.solarCity, sizeof(modified.prefs.solarCity), "Kraków");
    require(!equivalent(config, modified), "solar city change detected");
    modified = config;
    ++modified.prefs.colors.preset[0];
    require(!equivalent(config, modified), "light theme change detected");
    modified = config;
    ++modified.prefs.colors.preset[1];
    require(!equivalent(config, modified), "dark theme change detected");
    modified = config;
    modified.domain.ranges[0].maximum += 1;
    require(!equivalent(config, modified), "range change detected");
    require(verifyFirmware().ok, "host firmware verification path");
    require(resetStorage().ok && !load(restored) && !restored.prefs.night &&
                restored.prefs.network == NetworkMode::Off,
            "reset scoped configuration");
    config.prefs.night = true;
    config.prefs.network = NetworkMode::Off;
    config.domain.feeder = {180, ui_model::outputBit(ui_model::ModuleId::Air)};
    require(save(config).ok && load(restored) && restored.domain.feeder.rotationDegrees == 180 &&
                restored.domain.feeder.pauseMask == ui_model::outputBit(ui_model::ModuleId::Air),
            "feeder settings are durable");
    modified = config;
    ++modified.domain.feeder.rotationDegrees;
    require(!equivalent(config, modified), "rotation change detected");
    modified = config;
    modified.domain.feeder.pauseMask = 0;
    require(!equivalent(config, modified), "pause mask change detected");
    storeLegacyFixture(config);
    require(load(restored) && restored.prefs.night &&
                !restored.prefs.autoTheme &&
                restored.domain.ranges[0].maximum == config.domain.ranges[0].maximum &&
                restored.domain.feeder.rotationDegrees == 90 &&
                restored.domain.feeder.pauseMask == (ui_model::outputBit(ui_model::ModuleId::Filter) |
                                                     ui_model::outputBit(ui_model::ModuleId::Co2)),
            "v4 migration preserves configuration and initializes feeder");
    storeLegacyFixture(config, true);
    require(!load(restored) && !storageHealthy(), "corrupt legacy CRC cannot pass migration");
    storeV5Fixture(config);
    require(load(restored) && restored.domain.feeder.rotationDegrees == 180 &&
                restored.domain.feeder.pauseMask == ui_model::outputBit(ui_model::ModuleId::Air) &&
                restored.domain.feeder.minimumIntervalMinutes == 10 && restored.prefs.night &&
                !restored.prefs.autoTheme,
            "v5 migration preserves feeder and network preferences with safe interval");
    storeV5Fixture(config, true);
    require(!load(restored) && !storageHealthy(), "corrupt v5 CRC cannot pass migration");
    config.domain.feeder.minimumIntervalMinutes = 7;
    require(save(config).ok && load(restored) && restored.domain.feeder.minimumIntervalMinutes == 7,
            "v6 persists minimum dose interval");
    modified = config;
    modified.domain.feeder.minimumIntervalMinutes = 8;
    require(!equivalent(config, modified), "dose interval change detected");
    storeV6Fixture(config);
    require(load(restored) && restored.domain.feeder.minimumIntervalMinutes == 7 &&
                !restored.domain.lightPrograms[0].enabled && !restored.prefs.autoTheme,
            "v6 migration keeps schedules and uses opt-in new features");
    storeV6Fixture(config, true);
    require(!load(restored) && !storageHealthy(), "corrupt v6 CRC rejected");
    storeV7Fixture(config);
    require(load(restored) && restored.domain.lightPrograms[0].enabled ==
                config.domain.lightPrograms[0].enabled && !restored.prefs.autoTheme,
            "v7 migration preserves lamp programs and leaves solar opt-in");
    storeV7Fixture(config, true);
    require(!load(restored) && !storageHealthy(), "corrupt v7 CRC rejected");
    storeV8Fixture(config);
    require(load(restored) && restored.prefs.autoTheme &&
                std::strcmp(restored.prefs.solarCity, "Gdańsk") == 0 &&
                restored.domain.feeder.minimumIntervalMinutes == config.domain.feeder.minimumIntervalMinutes &&
                restored.prefs.colors.preset[0] == 0 && restored.prefs.colors.preset[1] == 0,
            "v8 migration preserves existing settings and initializes colors safely");
    storeV8Fixture(config, true);
    require(!load(restored) && !storageHealthy(), "corrupt v8 CRC rejected");
    storeV9Fixture(config, 3);
    require(load(restored) && restored.prefs.colors.preset[0] == 3 &&
                restored.prefs.colors.preset[1] == 3 && restored.prefs.autoTheme &&
                std::strcmp(restored.prefs.solarCity, "Gdańsk") == 0 &&
                restored.domain.feeder.minimumIntervalMinutes == config.domain.feeder.minimumIntervalMinutes,
            "v9 migration keeps the named preset for both modes and unrelated settings");
    require(save(restored).ok && load(restored) && restored.prefs.colors.preset[0] == 3 &&
                restored.prefs.colors.preset[1] == 3,
            "migrated v9 preferences are durable in v10 storage");
    storeV9Fixture(config, 255);
    require(load(restored) && storageHealthy() && restored.prefs.colors.preset[0] == 0 &&
                restored.prefs.colors.preset[1] == 0 && restored.prefs.autoTheme,
            "invalid v9 theme resets only the palette");
    storeV9Fixture(config, 2, true);
    require(!load(restored) && !storageHealthy(), "corrupt v9 CRC rejected");
    storeInvalidColorsFixture(config);
    require(load(restored) && storageHealthy() && restored.prefs.colors.preset[0] == 0 &&
                restored.prefs.colors.preset[1] == 0 && restored.prefs.night == config.prefs.night &&
                restored.domain.feeder.minimumIntervalMinutes == config.domain.feeder.minimumIntervalMinutes,
            "invalid stored colors fall back without discarding other configuration");
    resetFixtures();
    std::printf("RUNTIME %s checks=%d failures=%d\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures == 0;
}
