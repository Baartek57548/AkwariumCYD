#include "ui_runtime.h"
#include "ui_history_time.h"
#include "ui_io.h"
#include "ui_status.h"
#include <Arduino.h>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#ifndef AQUARIUM_UI_HOST_PREVIEW
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#endif

#ifndef AQUARIUM_UI_HOST_PREVIEW
#include "panel_hal.h"
#include <WiFi.h>
#include <esp_app_desc.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_random.h>
#include <esp_sntp.h>
#include <esp_wifi.h>
#include <nvs.h>
#include <sys/time.h>
#endif

namespace ui_runtime {
namespace {
constexpr uint32_t kMagic = 0x41514234u;
constexpr uint16_t kVersion = 10;
constexpr const char *kTimezone = "CET-1CEST,M3.5.0,M10.5.0/3";
#ifndef AQUARIUM_UI_HOST_PREVIEW
// Open-Meteo currently chains both endpoints to this self-signed ISRG root.
// Verify hostname and chain after the clock is valid; never accept arbitrary TLS peers.
constexpr char kSolarCa[] = R"pem(-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
)pem";
#endif
struct Packet {
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    Configuration config;
    uint32_t crc;
};
// Version 8 predates user-selected colors. Keep its exact preference prefix so
// an upgrade preserves time, network and automation settings already in NVS.
struct V8Preferences {
    bool night, feedback, reducedMotion, lock, ntp;
    uint16_t sleepMinutes;
    NetworkMode network;
    char ssid[33], password[65], apPassword[17], pin[5];
    bool autoTheme;
    char solarCity[33];
};
static_assert(sizeof(V8Preferences) == 164 && offsetof(V8Preferences, solarCity) == 130,
              "V8 preference layout must match the deployed NVS image");
struct V8Configuration {
    V8Preferences prefs;
    ui_model::DomainConfig domain;
};
struct V8Packet {
    uint32_t magic;
    uint16_t version, length;
    V8Configuration config;
    uint32_t crc;
};
// Preserve the deployed shared-preset/RGB layout only for reading old NVS.
// The new configuration stores two palette IDs and no editable RGB values.
struct V9Colors {
    uint8_t preset, customMask[2];
    uint32_t custom[2][ui_theme::kRoleCount];
};
struct V9Preferences { V8Preferences base; V9Colors colors; };
static_assert(sizeof(V9Colors) == 44 && sizeof(V9Preferences) == 208 &&
                  offsetof(V9Preferences, colors) == 164,
              "V9 preference layout must match the deployed NVS image");
struct V9Configuration { V9Preferences prefs; ui_model::DomainConfig domain; };
struct V9Packet {
    uint32_t magic;
    uint16_t version, length;
    V9Configuration config;
    uint32_t crc;
};
struct V7Preferences {
    bool night, feedback, reducedMotion, lock, ntp;
    uint16_t sleepMinutes;
    NetworkMode network;
    char ssid[33], password[65], apPassword[17], pin[5];
};
struct V7Domain {
    ui_model::Schedule schedules[static_cast<size_t>(ui_model::ModuleId::Count)];
    ui_model::FeedingConfig feeding;
    ui_model::HeaterConfig heater;
    ui_model::SensorRange ranges[3];
    ui_model::FeederConfig feeder;
    ui_model::LightProgram lightPrograms[2];
};
struct V7Configuration { V7Preferences prefs; V7Domain domain; };
struct V7Packet {
    uint32_t magic;
    uint16_t version, length;
    V7Configuration config;
    uint32_t crc;
};
V7Preferences previousPrefs(const Preferences &prefs) {
    V7Preferences old{};
    old.night = prefs.night;
    old.feedback = prefs.feedback;
    old.reducedMotion = prefs.reducedMotion;
    old.lock = prefs.lock;
    old.ntp = prefs.ntp;
    old.sleepMinutes = prefs.sleepMinutes;
    old.network = prefs.network;
    std::memcpy(old.ssid, prefs.ssid, sizeof(old.ssid));
    std::memcpy(old.password, prefs.password, sizeof(old.password));
    std::memcpy(old.apPassword, prefs.apPassword, sizeof(old.apPassword));
    std::memcpy(old.pin, prefs.pin, sizeof(old.pin));
    return old;
}
Preferences migratedPrefs(const V7Preferences &old) {
    Preferences prefs{};
    prefs.night = old.night;
    prefs.feedback = old.feedback;
    prefs.reducedMotion = old.reducedMotion;
    prefs.lock = old.lock;
    prefs.ntp = old.ntp;
    prefs.sleepMinutes = old.sleepMinutes;
    prefs.network = old.network;
    std::memcpy(prefs.ssid, old.ssid, sizeof(old.ssid));
    std::memcpy(prefs.password, old.password, sizeof(old.password));
    std::memcpy(prefs.apPassword, old.apPassword, sizeof(old.apPassword));
    std::memcpy(prefs.pin, old.pin, sizeof(old.pin));
    return prefs;
}
V8Preferences previousV8Prefs(const Preferences &prefs) {
    V8Preferences old{};
    old.night = prefs.night;
    old.feedback = prefs.feedback;
    old.reducedMotion = prefs.reducedMotion;
    old.lock = prefs.lock;
    old.ntp = prefs.ntp;
    old.sleepMinutes = prefs.sleepMinutes;
    old.network = prefs.network;
    std::memcpy(old.ssid, prefs.ssid, sizeof(old.ssid));
    std::memcpy(old.password, prefs.password, sizeof(old.password));
    std::memcpy(old.apPassword, prefs.apPassword, sizeof(old.apPassword));
    std::memcpy(old.pin, prefs.pin, sizeof(old.pin));
    old.autoTheme = prefs.autoTheme;
    std::memcpy(old.solarCity, prefs.solarCity, sizeof(old.solarCity));
    return old;
}
Preferences migratedV8Prefs(const V8Preferences &old) {
    Preferences prefs{};
    prefs.night = old.night;
    prefs.feedback = old.feedback;
    prefs.reducedMotion = old.reducedMotion;
    prefs.lock = old.lock;
    prefs.ntp = old.ntp;
    prefs.sleepMinutes = old.sleepMinutes;
    prefs.network = old.network;
    std::memcpy(prefs.ssid, old.ssid, sizeof(prefs.ssid));
    std::memcpy(prefs.password, old.password, sizeof(prefs.password));
    std::memcpy(prefs.apPassword, old.apPassword, sizeof(prefs.apPassword));
    std::memcpy(prefs.pin, old.pin, sizeof(prefs.pin));
    prefs.autoTheme = old.autoTheme;
    std::memcpy(prefs.solarCity, old.solarCity, sizeof(prefs.solarCity));
    return prefs;
}
// Preserve the exact v4 layout so a UI upgrade never silently discards saved schedules or Wi-Fi.
struct LegacyDomain {
    ui_model::Schedule schedules[static_cast<size_t>(ui_model::ModuleId::Count)];
    ui_model::FeedingConfig feeding;
    ui_model::HeaterConfig heater;
    ui_model::SensorRange ranges[3];
};
struct LegacyConfiguration {
    V7Preferences prefs;
    LegacyDomain domain;
};
struct LegacyPacket {
    uint32_t magic;
    uint16_t version, length;
    LegacyConfiguration config;
    uint32_t crc;
};
struct PreviousDomain {
    ui_model::Schedule schedules[static_cast<size_t>(ui_model::ModuleId::Count)];
    ui_model::FeedingConfig feeding;
    ui_model::HeaterConfig heater;
    ui_model::SensorRange ranges[3];
    struct {
        uint16_t rotationDegrees, pauseMask;
    } feeder;
};
struct PreviousConfiguration {
    V7Preferences prefs;
    PreviousDomain domain;
};
struct PreviousPacket {
    uint32_t magic;
    uint16_t version, length;
    PreviousConfiguration config;
    uint32_t crc;
};
struct V6Domain {
    ui_model::Schedule schedules[static_cast<size_t>(ui_model::ModuleId::Count)];
    ui_model::FeedingConfig feeding;
    ui_model::HeaterConfig heater;
    ui_model::SensorRange ranges[3];
    ui_model::FeederConfig feeder;
};
struct V6Configuration {
    V7Preferences prefs;
    V6Domain domain;
};
struct V6Packet {
    uint32_t magic;
    uint16_t version, length;
    V6Configuration config;
    uint32_t crc;
};
static_assert(sizeof(Packet) != sizeof(LegacyPacket) && sizeof(Packet) != sizeof(PreviousPacket) &&
                  sizeof(Packet) != sizeof(V6Packet) && sizeof(Packet) != sizeof(V7Packet) &&
                  sizeof(Packet) != sizeof(V8Packet) && sizeof(Packet) != sizeof(V9Packet),
              "Current NVS packets must have a distinct size for legacy routing");
std::atomic<bool> storageOk{true};
bool videoRefresh = false, connectedBefore = false;
bool networkConnecting = false, restartPending = false;
bool clockObserved = false;
std::atomic<bool> clockAlignmentRequested{false};
std::atomic<uint32_t> solarMinutes{0};
std::atomic<uint32_t> solarDate{0};
std::atomic<uint32_t> solarRevision{0};
uint32_t seenSolarRevision = 0;
char configuredSolarCity[33] = "Warszawa";
bool solarEnabled = false;
#ifndef AQUARIUM_UI_HOST_PREVIEW
portMUX_TYPE solarMux = portMUX_INITIALIZER_UNLOCKED;
uint32_t solarRetryAt = 0;
uint32_t fetchedCityHash = 0;
#endif
uint32_t networkStart = 0, restartAt = 0;
Preferences runningPrefs;
char networkState[96] = "Wi-Fi wyłączone";
char firmwareState[144] = "Kontrola obrazu nie została uruchomiona";
#ifdef AQUARIUM_UI_HOST_PREVIEW
Packet hostPacket{};
LegacyPacket hostLegacy{};
PreviousPacket hostPrevious{};
V6Packet hostV6{};
V7Packet hostV7{};
V8Packet hostV8{};
V9Packet hostV9{};
bool hostPreviousStored = false;
bool hostLegacyStored = false;
bool hostV6Stored = false;
bool hostV7Stored = false;
bool hostV8Stored = false;
bool hostV9Stored = false;
bool hostStored = false, nextSaveFails = false;
uint32_t hostRestarts = 0;
uint32_t hostSaves = 0;
DateTime hostTime;
uint32_t hostHistoryTime = 0, hostHistoryStart = 0;
#endif

uint32_t checksum(const void *data, size_t length) {
    const uint8_t *bytes = static_cast<const uint8_t *>(data);
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
size_t boundedLength(const char *text, size_t capacity) {
    size_t length = 0;
    while (length < capacity && text[length])
        ++length;
    return length;
}
bool validDate(const DateTime &value) {
    if (value.year < 2024 || value.year > 2099 || value.month < 1 || value.month > 12 || value.hour < 0 ||
        value.hour > 23 || value.minute < 0 || value.minute > 59 || value.dst < -1 || value.dst > 1)
        return false;
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = value.year % 4 == 0 && (value.year % 100 != 0 || value.year % 400 == 0);
    const int maximum = days[value.month - 1] + (value.month == 2 && leap ? 1 : 0);
    return value.day >= 1 && value.day <= maximum;
}
bool validPacket(const Packet &packet) {
    return packet.magic == kMagic && packet.version == kVersion && packet.length == sizeof(Configuration) &&
           packet.crc == checksum(&packet, offsetof(Packet, crc)) && validate(packet.config).ok;
}
void createApPassword(char *password, size_t capacity) {
#ifdef AQUARIUM_UI_HOST_PREVIEW
    std::snprintf(password, capacity, "AB-test-9347");
#else
    std::snprintf(password, capacity, "AB-%08lX", static_cast<unsigned long>(esp_random()));
#endif
}
Result writePacket(const Packet &packet, bool erase) {
#ifdef AQUARIUM_UI_HOST_PREVIEW
    if (nextSaveFails) {
        nextSaveFails = false;
        storageOk = false;
        return {false, "Błąd zapisu / poprzednie ustawienia pozostają bez zmian"};
    }
    hostPacket = packet;
    hostLegacyStored = false;
    hostPreviousStored = false;
    hostV6Stored = false;
    hostV7Stored = false;
    hostV8Stored = false;
    hostV9Stored = false;
    ++hostSaves;
    hostStored = !erase;
    storageOk = true;
    return {true, erase ? "Przywrócono ustawienia panelu" : "Zapisano w pamięci panelu"};
#else
    // The IO worker writes only after the display core has acknowledged a safe RGB pause.
    if (ui_io::displayCore())
        return {false, "Zapis wymaga kolejki I/O"};
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open("aquabox", NVS_READWRITE, &handle);
    if (error == ESP_OK) {
        error = erase ? nvs_erase_key(handle, "config")
                      : nvs_set_blob(handle, "config", &packet, sizeof(packet));
        if (erase && error == ESP_ERR_NVS_NOT_FOUND)
            error = ESP_OK;
        if (error == ESP_OK)
            error = nvs_commit(handle);
        nvs_close(handle);
    }
    storageOk = error == ESP_OK;
    if (error != ESP_OK)
        return {false, "Błąd pamięci NVS / ustawienia nie zostały zapisane"};
    return {true, erase ? "Przywrócono ustawienia panelu" : "Zapisano w pamięci panelu"};
#endif
}
} // namespace

void init() {
    restartPending = clockObserved = false;
    solarMinutes.store(0);
    solarDate.store(0);
    solarRevision.store(0);
    seenSolarRevision = 0;
#ifndef AQUARIUM_UI_HOST_PREVIEW
    solarRetryAt = fetchedCityHash = 0;
#endif
#ifdef AQUARIUM_UI_HOST_PREVIEW
    runningPrefs = Preferences{};
    networkConnecting = connectedBefore = false;
#endif
#ifndef AQUARIUM_UI_HOST_PREVIEW
    setenv("TZ", kTimezone, 1);
    tzset();
#endif
#ifdef AQUARIUM_UI_HOST_PREVIEW
    std::snprintf(networkState, sizeof(networkState), "Wi-Fi wyłączone");
#endif
}
bool load(Configuration &config) {
    config.prefs = Preferences{};
    config.domain = ui_model::configuration();
    Packet packet{};
    LegacyPacket legacy{};
    PreviousPacket previous{};
    V6Packet v6{};
    V7Packet v7{};
    V8Packet v8{};
    V9Packet v9{};
    bool isLegacy = false;
    bool isPrevious = false;
    bool isV6 = false;
    bool isV7 = false;
    bool isV8 = false;
    bool isV9 = false;
    bool present = false;
#ifdef AQUARIUM_UI_HOST_PREVIEW
    present = hostStored;
    packet = hostPacket;
    if (hostLegacyStored) {
        present = isLegacy = true;
        packet = Packet{};
        legacy = hostLegacy;
    }
    if (hostPreviousStored) {
        present = isPrevious = true;
        isLegacy = false;
        previous = hostPrevious;
    }
    if (hostV6Stored) {
        present = isV6 = true;
        isLegacy = isPrevious = false;
        v6 = hostV6;
    }
    if (hostV7Stored) {
        present = isV7 = true;
        isLegacy = isPrevious = isV6 = false;
        v7 = hostV7;
    }
    if (hostV8Stored) {
        present = isV8 = true;
        isLegacy = isPrevious = isV6 = isV7 = false;
        v8 = hostV8;
    }
    if (hostV9Stored) {
        present = isV9 = true;
        isLegacy = isPrevious = isV6 = isV7 = isV8 = false;
        v9 = hostV9;
    }
#else
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open("aquabox", NVS_READONLY, &handle);
    if (error == ESP_OK) {
        size_t length = 0;
        error = nvs_get_blob(handle, "config", nullptr, &length);
        if (error == ESP_OK &&
            (length == sizeof(packet) || length == sizeof(legacy) ||
             length == sizeof(previous) || length == sizeof(v6) || length == sizeof(v7) ||
             length == sizeof(v8) || length == sizeof(v9))) {
            isLegacy = length == sizeof(legacy);
            isPrevious = length == sizeof(previous);
            isV6 = length == sizeof(v6);
            isV7 = length == sizeof(v7);
            isV8 = length == sizeof(v8);
            isV9 = length == sizeof(v9);
            void *target = isLegacy     ? static_cast<void *>(&legacy)
                           : isPrevious ? static_cast<void *>(&previous)
                           : isV6       ? static_cast<void *>(&v6)
                           : isV7       ? static_cast<void *>(&v7)
                           : isV8       ? static_cast<void *>(&v8)
                           : isV9       ? static_cast<void *>(&v9)
                                        : &packet;
            error = nvs_get_blob(handle, "config", target, &length);
            present = error == ESP_OK;
        } else if (error == ESP_OK)
            error = ESP_ERR_NVS_INVALID_LENGTH;
        storageOk = error == ESP_OK || error == ESP_ERR_NVS_NOT_FOUND;
        nvs_close(handle);
    } else
        storageOk = error == ESP_ERR_NVS_NOT_FOUND;
#endif
    if (present) {
        if (isPrevious && previous.magic == kMagic && previous.version == 5 &&
            previous.length == sizeof(PreviousConfiguration) &&
            previous.crc == checksum(&previous, offsetof(PreviousPacket, crc))) {
            packet = Packet{};
            packet.config = config;
            packet.magic = kMagic;
            packet.version = kVersion;
            packet.length = sizeof(Configuration);
            packet.config.prefs = migratedPrefs(previous.config.prefs);
            const auto &old = previous.config.domain;
            auto &updated = packet.config.domain;
            std::memcpy(updated.schedules, old.schedules, sizeof(old.schedules));
            updated.feeding = old.feeding;
            updated.heater = old.heater;
            std::memcpy(updated.ranges, old.ranges, sizeof(old.ranges));
            updated.feeder = {old.feeder.rotationDegrees, old.feeder.pauseMask, 10};
            for (auto &program : updated.lightPrograms)
                program.enabled = false;
            packet.crc = checksum(&packet, offsetof(Packet, crc));
        } else if (isPrevious)
            packet = Packet{};
        if (isLegacy && legacy.magic == kMagic && legacy.version == 4 &&
            legacy.length == sizeof(LegacyConfiguration) &&
            legacy.crc == checksum(&legacy, offsetof(LegacyPacket, crc))) {
            packet.magic = kMagic;
            packet.version = kVersion;
            packet.length = sizeof(Configuration);
            packet.config = config;
            packet.config.prefs = migratedPrefs(legacy.config.prefs);
            const auto &old = legacy.config.domain;
            auto &updated = packet.config.domain;
            updated.feeder = {90, ui_model::outputBit(ui_model::ModuleId::Filter) |
                                      ui_model::outputBit(ui_model::ModuleId::Co2)};
            std::memcpy(updated.schedules, old.schedules, sizeof(old.schedules));
            updated.feeding = old.feeding;
            updated.heater = old.heater;
            std::memcpy(updated.ranges, old.ranges, sizeof(old.ranges));
            for (auto &program : updated.lightPrograms)
                program.enabled = false;
            packet.crc = checksum(&packet, offsetof(Packet, crc));
        }
        if (isV6 && v6.magic == kMagic && v6.version == 6 &&
            v6.length == sizeof(V6Configuration) &&
            v6.crc == checksum(&v6, offsetof(V6Packet, crc))) {
            packet = Packet{};
            packet.magic = kMagic;
            packet.version = kVersion;
            packet.length = sizeof(Configuration);
            packet.config = config;
            packet.config.prefs = migratedPrefs(v6.config.prefs);
            const auto &old = v6.config.domain;
            auto &updated = packet.config.domain;
            std::memcpy(updated.schedules, old.schedules, sizeof(old.schedules));
            updated.feeding = old.feeding;
            updated.heater = old.heater;
            std::memcpy(updated.ranges, old.ranges, sizeof(old.ranges));
            updated.feeder = old.feeder;
            for (auto &program : updated.lightPrograms)
                program.enabled = false;
            packet.crc = checksum(&packet, offsetof(Packet, crc));
        } else if (isV6)
            packet = Packet{};
        if (isV7 && v7.magic == kMagic && v7.version == 7 &&
            v7.length == sizeof(V7Configuration) &&
            v7.crc == checksum(&v7, offsetof(V7Packet, crc))) {
            packet = Packet{};
            packet.magic = kMagic;
            packet.version = kVersion;
            packet.length = sizeof(Configuration);
            packet.config = config;
            packet.config.prefs = migratedPrefs(v7.config.prefs);
            const auto &old = v7.config.domain;
            auto &updated = packet.config.domain;
            std::memcpy(updated.schedules, old.schedules, sizeof(old.schedules));
            updated.feeding = old.feeding;
            updated.heater = old.heater;
            std::memcpy(updated.ranges, old.ranges, sizeof(old.ranges));
            updated.feeder = old.feeder;
            std::memcpy(updated.lightPrograms, old.lightPrograms, sizeof(old.lightPrograms));
            packet.crc = checksum(&packet, offsetof(Packet, crc));
        } else if (isV7)
            packet = Packet{};
        if (isV8 && v8.magic == kMagic && v8.version == 8 &&
            v8.length == sizeof(V8Configuration) &&
            v8.crc == checksum(&v8, offsetof(V8Packet, crc))) {
            packet = Packet{};
            packet.magic = kMagic;
            packet.version = kVersion;
            packet.length = sizeof(Configuration);
            packet.config = config;
            packet.config.prefs = migratedV8Prefs(v8.config.prefs);
            packet.config.domain = v8.config.domain;
            packet.crc = checksum(&packet, offsetof(Packet, crc));
        } else if (isV8)
            packet = Packet{};
        if (isV9 && v9.magic == kMagic && v9.version == 9 &&
            v9.length == sizeof(V9Configuration) &&
            v9.crc == checksum(&v9, offsetof(V9Packet, crc))) {
            packet = Packet{};
            packet.magic = kMagic;
            packet.version = kVersion;
            packet.length = sizeof(Configuration);
            packet.config = config;
            packet.config.prefs = migratedV8Prefs(v9.config.prefs.base);
            packet.config.domain = v9.config.domain;
            const uint8_t preset = v9.config.prefs.colors.preset < 4
                                       ? v9.config.prefs.colors.preset : 0;
            packet.config.prefs.colors.preset[0] = preset;
            packet.config.prefs.colors.preset[1] = preset;
            packet.crc = checksum(&packet, offsetof(Packet, crc));
        } else if (isV9)
            packet = Packet{};
        if (packet.magic == kMagic && packet.version == kVersion &&
            packet.length == sizeof(Configuration) &&
            packet.crc == checksum(&packet, offsetof(Packet, crc)) &&
            !ui_theme::valid(packet.config.prefs.colors)) {
            // Corrupt theme data must not erase unrelated schedules, network
            // credentials or feeding rules from an otherwise valid packet.
            packet.config.prefs.colors = ui_theme::Settings{};
            packet.crc = checksum(&packet, offsetof(Packet, crc));
            ui_model::recordPanelEvent("Nieprawidłowe kolory / przywrócono motyw AquaBox",
                                       ui_model::LogLevel::Warning);
        }
        if (validPacket(packet)) {
            config = packet.config;
            storageOk = true;
        } else {
            storageOk = false;
            ui_model::recordPanelEvent("Nieprawidłowa konfiguracja NVS / użyto ustawień domyślnych",
                                       ui_model::LogLevel::Warning);
        }
    }
    if (!config.prefs.apPassword[0])
        createApPassword(config.prefs.apPassword, sizeof(config.prefs.apPassword));
#ifdef AQUARIUM_UI_HOST_PREVIEW
    runningPrefs = config.prefs;
#endif
    return present && storageOk;
}
Result validate(const Configuration &config) {
    const Preferences &prefs = config.prefs;
    if (prefs.sleepMinutes > 60 || static_cast<unsigned>(prefs.network) > 2)
        return {false, "Nieprawidłowe ustawienia panelu"};
    if (!ui_theme::valid(prefs.colors))
        return {false, "Kolory: nieprawidłowa wartość"};
    if (boundedLength(prefs.ssid, sizeof(prefs.ssid)) > 32 ||
        boundedLength(prefs.password, sizeof(prefs.password)) > 63 ||
        boundedLength(prefs.apPassword, sizeof(prefs.apPassword)) > 16)
        return {false, "Przekroczona długość nazwy sieci lub hasła"};
    const size_t cityLength = boundedLength(prefs.solarCity, sizeof(prefs.solarCity));
    if (cityLength < 2 || cityLength >= sizeof(prefs.solarCity))
        return {false, "Miejscowość: wpisz od 2 do 32 znaków"};
    if (boundedLength(prefs.pin, sizeof(prefs.pin)) != 4)
        return {false, "PIN musi zawierać dokładnie cztery cyfry"};
    for (unsigned i = 0; i < 4; ++i)
        if (prefs.pin[i] < '0' || prefs.pin[i] > '9')
            return {false, "PIN może zawierać tylko cyfry"};
    if (prefs.network == NetworkMode::Home) {
        const size_t passwordLength = std::strlen(prefs.password);
        if (!prefs.ssid[0] || (passwordLength && passwordLength < 8))
            return {false, "Podaj SSID i hasło WPA 8–63 znaki lub puste dla sieci otwartej"};
    }
    if (prefs.network == NetworkMode::AccessPoint && std::strlen(prefs.apPassword) < 8)
        return {false, "Hasło AP musi mieć 8–16 znaków"};
    const ui_model::Validation domain = ui_model::validateConfiguration(config.domain);
    return {domain.ok(), domain.message};
}
Result save(const Configuration &config) {
    const Result validation = validate(config);
    if (!validation.ok)
        return validation;
    Packet packet{};
    packet.magic = kMagic;
    packet.version = kVersion;
    packet.length = sizeof(Configuration);
    packet.config = config;
    packet.crc = checksum(&packet, offsetof(Packet, crc));
    const Result result = writePacket(packet, false);
#ifdef AQUARIUM_UI_HOST_PREVIEW
    if (result.ok)
        runningPrefs = config.prefs;
#endif
    return result;
}
bool equivalent(const Configuration &first, const Configuration &second) {
    const auto &a = first.prefs;
    const auto &b = second.prefs;
    if (a.night != b.night || a.feedback != b.feedback || a.reducedMotion != b.reducedMotion ||
        a.lock != b.lock || a.ntp != b.ntp || a.sleepMinutes != b.sleepMinutes || a.network != b.network ||
        std::strcmp(a.ssid, b.ssid) || std::strcmp(a.password, b.password) ||
        std::strcmp(a.apPassword, b.apPassword) || std::strcmp(a.pin, b.pin) ||
        a.autoTheme != b.autoTheme || std::strcmp(a.solarCity, b.solarCity) ||
        a.colors.preset[0] != b.colors.preset[0] || a.colors.preset[1] != b.colors.preset[1])
        return false;
    const auto &x = first.domain;
    const auto &y = second.domain;
    for (unsigned index = 0; index < static_cast<unsigned>(ui_model::ModuleId::Count); ++index) {
        const auto &s = x.schedules[index];
        const auto &t = y.schedules[index];
        if (s.startMinute != t.startMinute || s.endMinute != t.endMinute || s.mode != t.mode ||
            s.dayMask != t.dayMask)
            return false;
    }
    for (unsigned lamp = 0; lamp < 2; ++lamp) {
        const auto &program = x.lightPrograms[lamp];
        const auto &other = y.lightPrograms[lamp];
        if (program.enabled != other.enabled || program.dayStartMinute != other.dayStartMinute ||
            program.duskStartMinute != other.duskStartMinute ||
            program.nightStartMinute != other.nightStartMinute)
            return false;
    }
    for (unsigned index = 0; index < 3; ++index)
        if (x.ranges[index].minimum != y.ranges[index].minimum ||
            x.ranges[index].maximum != y.ranges[index].maximum)
            return false;
    return x.heater.target == y.heater.target && x.heater.hysteresis == y.heater.hysteresis &&
           x.feeder.rotationDegrees == y.feeder.rotationDegrees &&
           x.feeder.minimumIntervalMinutes == y.feeder.minimumIntervalMinutes &&
           x.feeder.pauseMask == y.feeder.pauseMask && x.feeding.count == y.feeding.count &&
           x.feeding.dayMask == y.feeding.dayMask && x.feeding.pauseSeconds == y.feeding.pauseSeconds &&
           x.feeding.times[0] == y.feeding.times[0] && x.feeding.times[1] == y.feeding.times[1];
}
Result resetStorage() { return writePacket(Packet{}, true); }
Result applyNetwork(const Preferences &prefs) {
#ifndef AQUARIUM_UI_HOST_PREVIEW
    if (ui_io::displayCore()) {
        Configuration config;
        config.prefs = prefs;
        config.domain = ui_model::configuration();
        const auto validation = validate(config);
        return validation.ok ? ui_io::submit(ui_io::Operation::Network, &config) : validation;
    }
#else
    Configuration check{};
    check.prefs = prefs;
    check.domain = ui_model::configuration();
    const Result validation = validate(check);
    if (!validation.ok)
        return validation;
#endif
    runningPrefs = prefs;
    networkConnecting = false;
    connectedBefore = false;
#ifdef AQUARIUM_UI_HOST_PREVIEW
    ui_status::WirelessObservation observation;
    observation.enabled = prefs.network != NetworkMode::Off;
    observation.stationConnected = prefs.network == NetworkMode::Home;
    observation.apActive = prefs.network == NetworkMode::AccessPoint;
    observation.rssi = -64;
    std::snprintf(observation.stationName, sizeof(observation.stationName), "%s", prefs.ssid);
    std::snprintf(observation.apName, sizeof(observation.apName), "AquaBox-Setup");
    ui_status::previewNetwork(observation);
#else
    if (prefs.network == NetworkMode::Off) {
        wifi_mode_t mode = WIFI_MODE_NULL;
        if (esp_wifi_get_mode(&mode) == ESP_OK && mode != WIFI_MODE_NULL && !WiFi.mode(WIFI_OFF))
            return {false, "Nie udało się wyłączyć Wi-Fi"};
    } else {
        WiFi.persistent(false);
        if (!WiFi.mode(prefs.network == NetworkMode::Home ? WIFI_STA : WIFI_AP))
            return {false, "Nie udało się uruchomić Wi-Fi"};
        if (prefs.network == NetworkMode::Home) {
            WiFi.setAutoReconnect(true);
            if (WiFi.begin(prefs.ssid, prefs.password) == WL_CONNECT_FAILED)
                return {false, "Nie udało się rozpocząć połączenia z routerem"};
            networkConnecting = true;
            networkStart = millis();
        } else if (!WiFi.softAP("AquaBox-Setup", prefs.apPassword, 1, false, 4))
            return {false, "Nie udało się uruchomić punktu dostępowego"};
    }
    if (!prefs.ntp)
        esp_sntp_stop();
#endif
    std::snprintf(networkState, sizeof(networkState), "%s",
                  prefs.network == NetworkMode::Off    ? "Wi-Fi wyłączone"
                  : prefs.network == NetworkMode::Home ? "Łączenie z routerem / limit 20 s"
                                                       : "AP AquaBox-Setup uruchomiony");
#ifdef AQUARIUM_UI_HOST_PREVIEW
    ui_model::recordPanelEvent(networkState);
#else
    ui_io::publishNetwork(networkState);
#endif
    return {true, networkState};
}
void serviceIo() {
#ifndef AQUARIUM_UI_HOST_PREVIEW
    if (runningPrefs.network == NetworkMode::Home) {
        const bool connected = WiFi.status() == WL_CONNECTED;
        if (connected && !connectedBefore) {
            networkConnecting = false;
            std::snprintf(networkState, sizeof(networkState), "Połączono z siecią domową");
            if (runningPrefs.ntp)
                configTzTime(kTimezone, "pool.ntp.org", "time.cloudflare.com");
        } else if (networkConnecting && millis() - networkStart >= 20000u) {
            networkConnecting = false;
            WiFi.disconnect(false, false);
            std::snprintf(networkState, sizeof(networkState), "Brak połączenia / sprawdź SSID i hasło");
        } else if (!connected && connectedBefore) {
            std::snprintf(networkState, sizeof(networkState), "Utracono połączenie z routerem");
        }
        connectedBefore = connected;
        ui_io::publishNetwork(networkState);
    }
#endif
}
bool nightTheme(const Preferences &prefs) {
    if (!prefs.autoTheme)
        return prefs.night;
    const uint32_t packed = solarMinutes.load();
    if (!packed)
        return prefs.night;
    const uint16_t sunrise = static_cast<uint16_t>(packed >> 16);
    const uint16_t sunset = static_cast<uint16_t>(packed);
    const uint16_t minute = ui_model::demoMinute();
    return minute < sunrise || minute >= sunset;
}
void configureSolar(const Preferences &prefs) {
#ifdef AQUARIUM_UI_HOST_PREVIEW
    std::snprintf(configuredSolarCity, sizeof(configuredSolarCity), "%s", prefs.solarCity);
    solarEnabled = prefs.autoTheme;
#else
    portENTER_CRITICAL(&solarMux);
    const bool changed = std::strncmp(configuredSolarCity, prefs.solarCity,
                                      sizeof(configuredSolarCity)) != 0;
    std::memcpy(configuredSolarCity, prefs.solarCity, sizeof(configuredSolarCity));
    solarEnabled = prefs.autoTheme;
    if (changed) {
        solarRetryAt = 0;
        solarMinutes.store(0);
        solarDate.store(0);
        solarRevision.fetch_add(1);
    }
    portEXIT_CRITICAL(&solarMux);
#endif
}
bool takeSolarThemeChange() {
    const uint32_t current = solarRevision.load();
    if (current == seenSolarRevision)
        return false;
    seenSolarRevision = current;
    return true;
}
void solarStatus(char *text, size_t capacity) {
    if (!text || !capacity)
        return;
    const uint32_t packed = solarMinutes.load();
    if (!packed) {
        std::snprintf(text, capacity, "Brak danych o słońcu / używana paleta ręczna");
        return;
    }
    const unsigned sunrise = packed >> 16;
    const unsigned sunset = packed & 0xffffu;
    std::snprintf(text, capacity, "Świt %02u:%02u  •  Zmierzch %02u:%02u%s",
                  sunrise / 60, sunrise % 60, sunset / 60, sunset % 60,
                  solarDate.load() == static_cast<uint32_t>(dateTime().year * 10000 +
                      dateTime().month * 100 + dateTime().day) ? "" : " / ostatni odczyt");
}
void serviceSolarIo() {
#ifndef AQUARIUM_UI_HOST_PREVIEW
    char city[33];
    bool enabled;
    uint32_t retryAt;
    portENTER_CRITICAL(&solarMux);
    std::memcpy(city, configuredSolarCity, sizeof(city));
    enabled = solarEnabled;
    retryAt = solarRetryAt;
    portEXIT_CRITICAL(&solarMux);
    const uint32_t now = millis();
    if (!enabled || runningPrefs.network != NetworkMode::Home || WiFi.status() != WL_CONNECTED ||
        (retryAt && static_cast<int32_t>(now - retryAt) < 0))
        return;
    const DateTime local = dateTime();
    if (local.year < 2024)
        return;
    const uint32_t today = static_cast<uint32_t>(local.year * 10000 + local.month * 100 + local.day);
    uint32_t hash = 2166136261u;
    for (const unsigned char *c = reinterpret_cast<const unsigned char *>(city); *c; ++c)
        hash = (hash ^ *c) * 16777619u;
    if (solarDate.load() == today && fetchedCityHash == hash)
        return;
    portENTER_CRITICAL(&solarMux);
    solarRetryAt = now + 3600000u;
    portEXIT_CRITICAL(&solarMux);
    String encoded;
    for (const unsigned char *c = reinterpret_cast<const unsigned char *>(city); *c; ++c) {
        if ((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') ||
            (*c >= '0' && *c <= '9') || *c == '-' || *c == '_')
            encoded += static_cast<char>(*c);
        else {
            char hex[4];
            std::snprintf(hex, sizeof(hex), "%%%02X", *c);
            encoded += hex;
        }
    }
    WiFiClientSecure client;
    client.setCACert(kSolarCa);
    HTTPClient http;
    http.setTimeout(5000);
    const String search = "https://geocoding-api.open-meteo.com/v1/search?name=" + encoded +
                          "&count=1&language=pl&countryCode=PL&format=json";
    if (!http.begin(client, search))
        return;
    const int geocodeCode = http.GET();
    const String geocodeBody = geocodeCode == 200 && http.getSize() <= 4096 ? http.getString() : String();
    StaticJsonDocument<512> geocodeFilter;
    geocodeFilter["results"][0]["latitude"] = true;
    geocodeFilter["results"][0]["longitude"] = true;
    StaticJsonDocument<512> location;
    const bool locationOk = !geocodeBody.isEmpty() && geocodeBody.length() <= 4096 &&
        deserializeJson(location, geocodeBody, DeserializationOption::Filter(geocodeFilter),
                        DeserializationOption::NestingLimit(8)) == DeserializationError::Ok &&
        location["results"][0]["latitude"].is<float>() &&
        location["results"][0]["longitude"].is<float>();
    const float latitude = location["results"][0]["latitude"] | 0.0f;
    const float longitude = location["results"][0]["longitude"] | 0.0f;
    http.end();
    if (!locationOk || latitude < -90.0f || latitude > 90.0f || longitude < -180.0f ||
        longitude > 180.0f)
        return;
    char forecast[232];
    std::snprintf(forecast, sizeof(forecast),
                  "https://api.open-meteo.com/v1/forecast?latitude=%.5f&longitude=%.5f"
                  "&daily=sunrise,sunset&timezone=Europe%%2FWarsaw&forecast_days=1",
                  latitude, longitude);
    if (!http.begin(client, forecast))
        return;
    const int forecastCode = http.GET();
    const String forecastBody = forecastCode == 200 && http.getSize() <= 4096 ? http.getString() : String();
    StaticJsonDocument<384> dailyFilter;
    dailyFilter["daily"]["time"][0] = true;
    dailyFilter["daily"]["sunrise"][0] = true;
    dailyFilter["daily"]["sunset"][0] = true;
    StaticJsonDocument<512> daily;
    const bool forecastOk = !forecastBody.isEmpty() && forecastBody.length() <= 4096 &&
        deserializeJson(daily, forecastBody, DeserializationOption::Filter(dailyFilter),
                        DeserializationOption::NestingLimit(8)) == DeserializationError::Ok;
    http.end();
    if (!forecastOk)
        return;
    const char *day = daily["daily"]["time"][0] | "";
    const char *dawn = daily["daily"]["sunrise"][0] | "";
    const char *dusk = daily["daily"]["sunset"][0] | "";
    int year, month, date, dawnHour, dawnMinute, duskHour, duskMinute;
    if (std::sscanf(day, "%d-%d-%d", &year, &month, &date) != 3 ||
        std::sscanf(dawn, "%*d-%*d-%*dT%d:%d", &dawnHour, &dawnMinute) != 2 ||
        std::sscanf(dusk, "%*d-%*d-%*dT%d:%d", &duskHour, &duskMinute) != 2 ||
        static_cast<uint32_t>(year * 10000 + month * 100 + date) != today ||
        dawnHour < 0 || dawnHour > 23 || dawnMinute < 0 || dawnMinute > 59 ||
        duskHour < 0 || duskHour > 23 || duskMinute < 0 || duskMinute > 59)
        return;
    const uint16_t sunrise = static_cast<uint16_t>(dawnHour * 60 + dawnMinute);
    const uint16_t sunset = static_cast<uint16_t>(duskHour * 60 + duskMinute);
    if (sunrise >= sunset)
        return;
    solarMinutes.store((static_cast<uint32_t>(sunrise) << 16) | sunset);
    solarDate.store(today);
    fetchedCityHash = hash;
    solarRevision.fetch_add(1);
#endif
}
void service() {
#ifndef AQUARIUM_UI_HOST_PREVIEW
    const time_t epoch = time(nullptr);
    struct tm local {};
    if (localtime_r(&epoch, &local) && local.tm_year >= 124 && local.tm_year <= 199) {
        const uint16_t minute = local.tm_hour * 60 + local.tm_min;
        const uint16_t difference = (minute + 1440u - ui_model::demoMinute()) % 1440u;
        const uint8_t day = static_cast<uint8_t>((local.tm_wday + 6) % 7);
        if (!clockObserved || clockAlignmentRequested.exchange(false) || ui_model::weekday() != day ||
            (difference > 1 && difference < 1439)) {
            ui_model::setClock(minute, day);
            clockObserved = true;
        }
    }
#endif
    if (restartPending && static_cast<int32_t>(millis() - restartAt) >= 0) {
        restartPending = false;
#ifdef AQUARIUM_UI_HOST_PREVIEW
        ++hostRestarts;
#else
        ESP.restart();
#endif
    }
}
const char *networkMessage() { return networkState; }
void networkDetails(char *text, size_t capacity) {
#ifdef AQUARIUM_UI_HOST_PREVIEW
    std::snprintf(text, capacity, "%s\nAdres IP: %s\nUART WeMos: niepodłączony", networkState,
                  runningPrefs.network == NetworkMode::Off ? "–" : "192.168.4.1");
#else
    ui_io::networkDetails(text, capacity);
#endif
}
DateTime dateTime() {
#ifdef AQUARIUM_UI_HOST_PREVIEW
    DateTime value = hostTime;
    const ui_status::Clock clock = ui_status::read(ui_model::demoMinute()).clock;
    value.hour = clock.hour;
    value.minute = clock.minute;
    return value;
#else
    DateTime value;
    const time_t epoch = time(nullptr);
    struct tm local {};
    if (localtime_r(&epoch, &local) && local.tm_year >= 124 && local.tm_year <= 199) {
        value.year = local.tm_year + 1900;
        value.month = local.tm_mon + 1;
        value.day = local.tm_mday;
        value.hour = local.tm_hour;
        value.minute = local.tm_min;
    } else {
        value.hour = ui_model::demoMinute() / 60;
        value.minute = ui_model::demoMinute() % 60;
    }
    return value;
#endif
}
uint32_t historyTime() {
#ifdef AQUARIUM_UI_HOST_PREVIEW
    if (!hostHistoryTime ||
        ui_status::read(ui_model::demoMinute()).clock.source != ui_status::ClockSource::System)
        return 0;
    return hostHistoryTime + static_cast<uint32_t>(millis() - hostHistoryStart) / 1000u;
#else
    const time_t epoch = time(nullptr);
    struct tm local {};
    if (!localtime_r(&epoch, &local))
        return 0;
    return ui_history_time::encode(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour,
                                   local.tm_min, local.tm_sec);
#endif
}
bool ambiguousTime(const DateTime &value) {
    const int a = (14 - value.month) / 12;
    const int y = value.year - a, m = value.month + 12 * a - 2;
    const int weekday = (value.day + y + y / 4 - y / 100 + y / 400 + 31 * m / 12) % 7;
    return value.month == 10 && value.hour == 2 && weekday == 0 && value.day >= 25;
}
Result validateDateTime(const DateTime &value) {
    if (!validDate(value))
        return {false, "Nieprawidłowa data lub godzina / sprawdź liczbę dni miesiąca"};
    const int a = (14 - value.month) / 12;
    const int y = value.year - a, m = value.month + 12 * a - 2;
    const int dayOfWeek = (value.day + y + y / 4 - y / 100 + y / 400 + 31 * m / 12) % 7;
    if (value.month == 3 && value.hour == 2 && dayOfWeek == 0 && value.day >= 25)
        return {false, "Ta godzina nie istnieje podczas zmiany czasu letniego"};
    if (ambiguousTime(value) && value.dst == -1)
        return {false, "Godzina występuje dwukrotnie / wybierz czas letni lub zimowy"};
    return {true, "Poprawna data i czas"};
}
Result setDateTime(const DateTime &value) {
    const auto validation = validateDateTime(value);
    if (!validation.ok)
        return validation;
    const int a = (14 - value.month) / 12;
    const int y = value.year - a, m = value.month + 12 * a - 2;
    const int dayOfWeek = (value.day + y + y / 4 - y / 100 + y / 400 + 31 * m / 12) % 7;
#ifndef AQUARIUM_UI_HOST_PREVIEW
    if (ui_io::displayCore())
        return ui_io::submit(ui_io::Operation::Clock, nullptr, &value);
#endif
#ifndef AQUARIUM_UI_HOST_PREVIEW
    struct tm local {};
    local.tm_year = value.year - 1900;
    local.tm_mon = value.month - 1;
    local.tm_mday = value.day;
    local.tm_hour = value.hour;
    local.tm_min = value.minute;
    local.tm_isdst = ambiguousTime(value) ? value.dst : -1;
    const time_t epoch = mktime(&local);
    if (epoch == static_cast<time_t>(-1) || local.tm_hour != value.hour || local.tm_min != value.minute)
        return {false, "Ta godzina nie istnieje podczas zmiany czasu letniego"};
#endif
#ifdef AQUARIUM_UI_HOST_PREVIEW
    hostTime = value;
    hostHistoryTime =
        ui_history_time::encode(value.year, value.month, value.day, value.hour, value.minute, 0);
    hostHistoryStart = millis();
    ui_status::previewClock(value.hour, value.minute, true);
#else
    const timeval instant{epoch, 0};
    if (settimeofday(&instant, nullptr) != 0)
        return {false, "Nie udało się ustawić zegara systemowego"};
    // A deliberate one-minute correction must not be swallowed by the drift tolerance.
    clockAlignmentRequested.store(true);
#endif
#ifdef AQUARIUM_UI_HOST_PREVIEW
    ui_model::setClock(static_cast<uint16_t>(value.hour * 60 + value.minute),
                       static_cast<uint8_t>((dayOfWeek + 6) % 7));
#endif
    return {true, "Ustawiono zegar / po odłączeniu zasilania wymagany RTC lub NTP"};
}
Result syncTime() {
#ifndef AQUARIUM_UI_HOST_PREVIEW
    if (ui_io::displayCore())
        return ui_io::submit(ui_io::Operation::Sync);
#endif
#ifdef AQUARIUM_UI_HOST_PREVIEW
    if (ui_status::read(0).wireless.mode != ui_status::WirelessMode::Home)
        return {false, "NTP wymaga połączenia z siecią domową"};
#else
    if (WiFi.status() != WL_CONNECTED)
        return {false, "NTP wymaga połączenia z siecią domową"};
    configTzTime(kTimezone, "pool.ntp.org", "time.cloudflare.com");
#endif
    return {true, "Uruchomiono NTP / zegar zmieni się po odpowiedzi serwera"};
}
Result verifyFirmware() {
#ifndef AQUARIUM_UI_HOST_PREVIEW
    if (ui_io::displayCore())
        return ui_io::submit(ui_io::Operation::Verify);
#endif
#ifdef AQUARIUM_UI_HOST_PREVIEW
    std::snprintf(firmwareState, sizeof(firmwareState), "Kontrola obrazu: poprawna (test hosta)");
#else
    uint8_t hash[32]{};
    const esp_partition_t *partition = esp_ota_get_running_partition();
    const esp_err_t result = partition ? esp_partition_get_sha256(partition, hash) : ESP_ERR_NOT_FOUND;
    if (result != ESP_OK) {
        std::snprintf(firmwareState, sizeof(firmwareState), "Kontrola obrazu: błąd %d",
                      static_cast<int>(result));
        return {false, firmwareState};
    }
    char digest[65]{};
    for (unsigned i = 0; i < 32; ++i)
        std::snprintf(digest + i * 2, 3, "%02x", hash[i]);
    std::snprintf(firmwareState, sizeof(firmwareState), "Kontrola obrazu: poprawna\nSHA-256: %.16s",
                  digest);
#endif
    return {true, firmwareState};
}
const char *firmwareMessage() { return firmwareState; }
void requestRestart() {
    restartAt = millis() + 600;
    restartPending = true;
}
bool storageHealthy() { return storageOk; }
bool takeVideoRefresh() {
    const bool changed = videoRefresh;
    videoRefresh = false;
    return changed;
}
#ifdef AQUARIUM_UI_HOST_PREVIEW
void resetFixtures() {
    hostStored = nextSaveFails = false;
    hostPacket = Packet{};
    hostLegacyStored = false;
    hostPreviousStored = false;
    hostV6Stored = false;
    hostV7Stored = false;
    hostV8Stored = false;
    hostV9Stored = false;
    hostRestarts = 0;
    hostSaves = 0;
    hostTime = DateTime{};
    hostHistoryTime = hostHistoryStart = 0;
    storageOk = true;
    init();
}
void failNextSave() { nextSaveFails = true; }
void storeLegacyFixture(const Configuration &config, bool corrupt) {
    hostPreviousStored = false;
    hostV6Stored = hostV7Stored = false;
    hostV8Stored = false;
    hostV9Stored = false;
    hostLegacy = LegacyPacket{};
    hostLegacy.magic = kMagic;
    hostLegacy.version = 4;
    hostLegacy.length = sizeof(LegacyConfiguration);
    hostLegacy.config.prefs = previousPrefs(config.prefs);
    auto &old = hostLegacy.config.domain;
    std::memcpy(old.schedules, config.domain.schedules, sizeof(old.schedules));
    old.feeding = config.domain.feeding;
    old.heater = config.domain.heater;
    std::memcpy(old.ranges, config.domain.ranges, sizeof(old.ranges));
    hostLegacy.crc = checksum(&hostLegacy, offsetof(LegacyPacket, crc)) ^ (corrupt ? 1u : 0u);
    hostLegacyStored = true;
}
void storeV5Fixture(const Configuration &config, bool corrupt) {
    hostV9Stored = false;
    hostPrevious = PreviousPacket{};
    hostPrevious.magic = kMagic;
    hostPrevious.version = 5;
    hostPrevious.length = sizeof(PreviousConfiguration);
    hostPrevious.config.prefs = previousPrefs(config.prefs);
    auto &old = hostPrevious.config.domain;
    std::memcpy(old.schedules, config.domain.schedules, sizeof(old.schedules));
    old.feeding = config.domain.feeding;
    old.heater = config.domain.heater;
    std::memcpy(old.ranges, config.domain.ranges, sizeof(old.ranges));
    old.feeder = {config.domain.feeder.rotationDegrees, config.domain.feeder.pauseMask};
    hostPrevious.crc = checksum(&hostPrevious, offsetof(PreviousPacket, crc)) ^ (corrupt ? 1u : 0u);
    hostPreviousStored = true;
    hostLegacyStored = false;
    hostV6Stored = false;
    hostV7Stored = false;
    hostV8Stored = false;
}
void storeV6Fixture(const Configuration &config, bool corrupt) {
    hostV9Stored = false;
    hostV6 = V6Packet{};
    hostV6.magic = kMagic;
    hostV6.version = 6;
    hostV6.length = sizeof(V6Configuration);
    hostV6.config.prefs = previousPrefs(config.prefs);
    auto &old = hostV6.config.domain;
    std::memcpy(old.schedules, config.domain.schedules, sizeof(old.schedules));
    old.feeding = config.domain.feeding;
    old.heater = config.domain.heater;
    std::memcpy(old.ranges, config.domain.ranges, sizeof(old.ranges));
    old.feeder = config.domain.feeder;
    hostV6.crc = checksum(&hostV6, offsetof(V6Packet, crc)) ^ (corrupt ? 1u : 0u);
    hostV6Stored = true;
    hostLegacyStored = hostPreviousStored = hostV7Stored = hostV8Stored = false;
}
void storeV7Fixture(const Configuration &config, bool corrupt) {
    hostV9Stored = false;
    hostV7 = V7Packet{};
    hostV7.magic = kMagic;
    hostV7.version = 7;
    hostV7.length = sizeof(V7Configuration);
    hostV7.config.prefs = previousPrefs(config.prefs);
    auto &old = hostV7.config.domain;
    std::memcpy(old.schedules, config.domain.schedules, sizeof(old.schedules));
    old.feeding = config.domain.feeding;
    old.heater = config.domain.heater;
    std::memcpy(old.ranges, config.domain.ranges, sizeof(old.ranges));
    old.feeder = config.domain.feeder;
    std::memcpy(old.lightPrograms, config.domain.lightPrograms, sizeof(old.lightPrograms));
    hostV7.crc = checksum(&hostV7, offsetof(V7Packet, crc)) ^ (corrupt ? 1u : 0u);
    hostV7Stored = true;
    hostLegacyStored = hostPreviousStored = hostV6Stored = false;
    hostV8Stored = false;
}
void storeV8Fixture(const Configuration &config, bool corrupt) {
    hostV9Stored = false;
    hostV8 = V8Packet{};
    hostV8.magic = kMagic;
    hostV8.version = 8;
    hostV8.length = sizeof(V8Configuration);
    hostV8.config.prefs = previousV8Prefs(config.prefs);
    hostV8.config.domain = config.domain;
    hostV8.crc = checksum(&hostV8, offsetof(V8Packet, crc)) ^ (corrupt ? 1u : 0u);
    hostV8Stored = true;
    hostLegacyStored = hostPreviousStored = hostV6Stored = hostV7Stored = false;
}
void storeV9Fixture(const Configuration &config, uint8_t preset, bool corrupt) {
    hostV9 = V9Packet{};
    hostV9.magic = kMagic;
    hostV9.version = 9;
    hostV9.length = sizeof(V9Configuration);
    hostV9.config.prefs.base = previousV8Prefs(config.prefs);
    hostV9.config.prefs.colors.preset = preset;
    hostV9.config.prefs.colors.customMask[0] = 1u << static_cast<unsigned>(ui_theme::Role::Header);
    hostV9.config.prefs.colors.custom[0][static_cast<unsigned>(ui_theme::Role::Header)] = 0x103C22;
    hostV9.config.domain = config.domain;
    hostV9.crc = checksum(&hostV9, offsetof(V9Packet, crc)) ^ (corrupt ? 1u : 0u);
    hostV9Stored = true;
    hostLegacyStored = hostPreviousStored = hostV6Stored = hostV7Stored = hostV8Stored = false;
}
void storeInvalidColorsFixture(const Configuration &config) {
    hostPacket = Packet{};
    hostPacket.magic = kMagic;
    hostPacket.version = kVersion;
    hostPacket.length = sizeof(Configuration);
    hostPacket.config = config;
    hostPacket.config.prefs.colors.preset[1] = ui_theme::kPresetCount;
    hostPacket.crc = checksum(&hostPacket, offsetof(Packet, crc));
    hostStored = true;
    hostLegacyStored = hostPreviousStored = hostV6Stored = hostV7Stored = hostV8Stored = hostV9Stored = false;
}
uint32_t restartCount() { return hostRestarts; }
uint32_t saveCount() { return hostSaves; }
#endif
} // namespace ui_runtime
