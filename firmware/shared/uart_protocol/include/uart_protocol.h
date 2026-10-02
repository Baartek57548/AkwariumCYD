#ifndef AQUARIUM_UART_PROTOCOL_H
#define AQUARIUM_UART_PROTOCOL_H

#if defined(ARDUINO)
#include <Arduino.h>
#else
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <string>
#include <cstdio>

// Lightweight Arduino String compatibility shim for desktop/native environments
class String : public std::string {
public:
    String() : std::string() {}
    String(const char *s) : std::string(s ? s : "") {}
    String(const std::string &s) : std::string(s) {}
    String(float val, int decimals = 2) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.*f", decimals, static_cast<double>(val));
        assign(buf);
    }
    String(double val, int decimals = 2) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.*f", decimals, val);
        assign(buf);
    }
    String(int val) : std::string(std::to_string(val)) {}
    String(unsigned int val) : std::string(std::to_string(val)) {}
    String(long val) : std::string(std::to_string(val)) {}
    String(unsigned long val) : std::string(std::to_string(val)) {}

    const char* c_str() const { return std::string::c_str(); }
    size_t length() const { return std::string::length(); }
    void reserve(size_t res) { std::string::reserve(res); }
};
#endif
#include <stdint.h>

namespace AquaProtocol {

constexpr uint32_t UART_BAUD_RATE = 115200UL;
constexpr uint32_t TELEMETRY_INTERVAL_MS = 1000UL;
constexpr uint32_t OFFLINE_TIMEOUT_MS = 3000UL;

// Indeksy przekaźników
enum RelayIndex : uint8_t {
    RELAY_LIGHT_A = 0,
    RELAY_LIGHT_B = 1,
    RELAY_FILTER = 2,
    RELAY_AERATOR = 3,
    RELAY_HEATER = 4,
    RELAY_CO2 = 5,
    RELAY_FEEDER = 6,
    RELAY_SPARE = 7,
    RELAY_COUNT = 8
};

// Flagi alarmów (bitmaska)
enum AlarmFlag : uint16_t {
    ALARM_NONE = 0,
    ALARM_OVERHEAT = (1U << 0),
    ALARM_UNDERHEAT = (1U << 1),
    ALARM_RESERVED_INPUT = (1U << 2),
    ALARM_WATER_LOW = (1U << 3),
    ALARM_TEMP_SENSOR_FAIL = (1U << 4),
    ALARM_HEATER_TIMEOUT = (1U << 5),
    ALARM_ATO_TIMEOUT = (1U << 6),
    ALARM_I2C_FAIL = (1U << 7)
};

// Typy komend przesyłanych z CYD do WeMos
enum class CommandType : uint8_t {
    Unknown = 0,
    SetRelay,
    SetTempTarget,
    SetSchedule,
    FeedTrigger,
    SyncLights,
    SyncTime,
    ClearAlarm,
    GetStatus,
    SetSafeMode,
    EnterOta
};

// Typy wiadomości w protokole
enum class MessageType : uint8_t {
    Unknown = 0,
    Telemetry,
    Command,
    Ack,
    TimeSync
};

// Struktura telemetrii (WeMos -> CYD)
struct AquariumTelemetry {
    uint32_t timestamp;        // Czas unix epoch z DS3231
    float temperature;         // Temp wody DS18B20 (°C)
    float temp_target;         // Temp zadana (°C)
    float temp_hysteresis;     // Histereza (°C)
    float ph;                  // Odczyn pH z ADS1115
    float ec;                  // Przewodność EC
    uint8_t relay_states;      // Bity 0..7 stanów załączenia przekaźników
    uint16_t alarms;           // Maska AlarmFlag
    bool water_level_low;      // Pływak poziomu wody
    bool water_level_max_reached; // Górny pływak osiągnął poziom MAX
    bool water_level_max_valid; // Odczyt obu pływaków w bieżącej ramce jest ważny
    bool water_level_max_present; // Odbiornik: starsza ramka nie zna pól MAX
    uint32_t uptime_seconds;   // Uptime sterownika WeMos
    bool safe_mode;            // Czy sterownik jest w trybie awaryjnym
    uint8_t light_mode;        // Tryb oświetlenia Aquael (1, 2, 3)
    bool ota_mode;             // true jeśli aktywny jest tryb serwisowy OTA
    char ota_ip[16];           // Adres IP WeMos w trybie OTA
};

// Struktura pojedynczej komendy (CYD -> WeMos)
struct AquariumCommand {
    CommandType type;
    uint8_t channel;           // Dla SetRelay (0..7)
    bool state;                // Dla SetRelay (0 lub 1)
    float temp_target;         // Dla SetTempTarget
    float temp_hysteresis;     // Dla SetTempTarget
    uint8_t schedule_dev;      // 0=Light, 1=Filter, 2=Aerator, 3=CO2
    uint8_t on_hour;
    uint8_t on_minute;
    uint8_t off_hour;
    uint8_t off_minute;
    bool schedule_enabled;
    uint32_t epoch_time;       // Dla SyncTime
    char ota_ssid[33];         // Dla EnterOta: opcjonalny SSID sieci Wi-Fi
    char ota_pass[65];         // Dla EnterOta: opcjonalne hasło WPA2
};

// Struktura potwierdzenia ACK
struct AquariumAck {
    char cmd[24];
    bool ok;
    char message[48];
};

// Obiekt reprezentujący zdeserializowaną wiadomość
struct AquariumMessage {
    MessageType type;
    AquariumTelemetry telemetry;
    AquariumCommand command;
    AquariumAck ack;
    uint32_t sync_time_epoch;
};

// Funkcje serializacji
String serializeTelemetry(const AquariumTelemetry &t);
String serializeCommand(const AquariumCommand &cmd);
String serializeAck(const char *cmd, bool ok, const char *msg = nullptr);
String serializeTimeSync(uint32_t epoch);

// Funkcja parsowania linii NDJSON
bool parseMessage(const char *json_line, AquariumMessage &out_msg);

} // namespace AquaProtocol

#endif // AQUARIUM_UART_PROTOCOL_H
