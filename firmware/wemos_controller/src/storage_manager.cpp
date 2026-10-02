#include "storage_manager.h"
#include "wemos_config.h"
#include <EEPROM.h>

namespace {
constexpr uint32_t CONFIG_MAGIC = 0x41515541; // "AQUA"
constexpr uint16_t CONFIG_VERSION = 1;
}

StorageManager g_storage_manager;

StorageManager::StorageManager()
{
}

uint32_t StorageManager::calculateChecksum(const ControllerConfig &cfg)
{
    const uint8_t *p = reinterpret_cast<const uint8_t*>(&cfg);
    size_t len = sizeof(ControllerConfig) - sizeof(uint32_t); // bez pola checksum
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= p[i];
        for (uint8_t j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
        }
    }
    return ~crc;
}

void StorageManager::resetToDefaults(ControllerConfig &cfg)
{
    cfg.magic = CONFIG_MAGIC;
    cfg.version = CONFIG_VERSION;
    cfg.target_temp = WemosConfig::DEFAULT_TEMP_TARGET;
    cfg.hysteresis = WemosConfig::DEFAULT_TEMP_HYSTERESIS;
    cfg.ph_cal_offset = 0.0f;
    cfg.ph_cal_slope = 1.0f;

    cfg.schedules[0] = { true, 8, 0, 20, 0 };   // Światło 8:00 - 20:00
    cfg.schedules[1] = { true, 0, 0, 23, 59 };  // Filtr 24h
    cfg.schedules[2] = { true, 22, 0, 6, 0 };   // Napowietrzacz 22:00 - 6:00
    cfg.schedules[3] = { true, 7, 30, 19, 30 }; // CO2 7:30 - 19:30

    cfg.checksum = calculateChecksum(cfg);
}

bool StorageManager::begin()
{
    EEPROM.begin(sizeof(ControllerConfig));
    return true;
}

bool StorageManager::loadConfig(ControllerConfig &cfg)
{
    EEPROM.get(0, cfg);
    if (cfg.magic != CONFIG_MAGIC || cfg.version != CONFIG_VERSION) {
        resetToDefaults(cfg);
        saveConfig(cfg);
        return false;
    }

    uint32_t expected_cs = calculateChecksum(cfg);
    if (cfg.checksum != expected_cs) {
        resetToDefaults(cfg);
        saveConfig(cfg);
        return false;
    }

    return true;
}

bool StorageManager::saveConfig(const ControllerConfig &cfg)
{
    ControllerConfig copy = cfg;
    copy.magic = CONFIG_MAGIC;
    copy.version = CONFIG_VERSION;
    copy.checksum = calculateChecksum(copy);

    EEPROM.put(0, copy);
    return EEPROM.commit();
}
