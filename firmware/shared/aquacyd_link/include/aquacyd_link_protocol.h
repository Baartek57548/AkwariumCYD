#ifndef AQUACYD_LINK_PROTOCOL_H
#define AQUACYD_LINK_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

namespace aquacyd {
namespace link {

constexpr uint8_t kProtocolVersion = 1U;
constexpr size_t kHeaderSize = 32U;
constexpr size_t kCrcSize = 4U;
constexpr size_t kEspNowMaximumFrameSize = 250U;
constexpr size_t kMaximumPayloadSize =
    kEspNowMaximumFrameSize - kHeaderSize - kCrcSize;
constexpr size_t kMaximumCobsFrameSize =
    kEspNowMaximumFrameSize + (kEspNowMaximumFrameSize / 254U) + 1U;

enum class MessageType : uint8_t {
    Hello = 1U,
    Telemetry = 2U,
    Alarm = 3U,
    Command = 4U,
    Acknowledgement = 5U,
    Configuration = 6U,
    TimeSync = 7U,
    Heartbeat = 8U
};

enum FrameFlags : uint8_t {
    FlagNone = 0U,
    FlagAckRequested = 1U << 0U,
    FlagResponse = 1U << 1U,
    FlagUrgent = 1U << 2U
};

enum class DecodeStatus : uint8_t {
    Ok = 0U,
    NullArgument,
    Truncated,
    TooLarge,
    InvalidMagic,
    UnsupportedVersion,
    InvalidHeader,
    InvalidMessageType,
    InvalidIdentity,
    InvalidLength,
    CrcMismatch,
    CobsMalformed,
    OutputTooSmall
};

struct Frame {
    MessageType type;
    uint8_t flags;
    uint32_t source_id;
    uint32_t boot_id;
    uint32_t sequence;
    uint32_t acknowledged_sequence;
    uint32_t issued_at_ms;
    uint32_t ttl_ms;
    uint16_t payload_length;
    uint8_t payload[kMaximumPayloadSize];
};

enum TelemetryFlags : uint16_t {
    TelemetryTemperatureValid = 1U << 0U,
    TelemetryPhValid = 1U << 1U,
    TelemetryEcValid = 1U << 2U,
    TelemetryLdrValid = 1U << 3U,
    TelemetryWaterLevelLow = 1U << 4U,
    TelemetryLeakDetected = 1U << 5U,
    TelemetryControllerSafe = 1U << 6U,
    TelemetryConfigurationValid = 1U << 7U
};

enum RelayStateFlags : uint16_t {
    RelayLightPrimaryOn = 1U << 0U,
    RelayLightSecondaryOn = 1U << 1U,
    RelayFilterOn = 1U << 2U,
    RelayAeratorOn = 1U << 3U,
    RelayHeaterOn = 1U << 4U
};

struct SchedulePayload {
    uint8_t mode;
    uint8_t profile;
    uint16_t start_minute;
    uint16_t end_minute;
};

struct TelemetryPayload {
    uint16_t flags;
    int32_t temperature_milli_c;
    int32_t ph_milli;
    int32_t ec_milli_us_cm;
    uint16_t ldr_raw;
    uint16_t relay_bits;
    uint32_t alarm_flags;
    uint32_t uptime_seconds;
    uint32_t free_heap_bytes;
    int16_t wifi_rssi_dbm;
    uint32_t configuration_revision;
    int32_t target_temperature_milli_c;
    uint16_t temperature_hysteresis_milli_c;
    uint8_t heater_mode;
    SchedulePayload light_primary_schedule;
    SchedulePayload light_secondary_schedule;
    SchedulePayload filter_schedule;
    SchedulePayload aerator_schedule;
};

enum class CommandAction : uint8_t {
    SetOutput = 1U,
    SetMode = 2U,
    SetSetpoint = 3U,
    TriggerFeed = 4U,
    AcknowledgeAlarm = 5U,
    RequestSnapshot = 6U,
    SynchronizeTime = 7U,
    SetSchedule = 8U
};

enum class CommandTarget : uint8_t {
    Controller = 0U,
    LightPrimary = 1U,
    LightSecondary = 2U,
    Filter = 3U,
    Aerator = 4U,
    Heater = 5U,
    Co2 = 6U,
    Feeder = 7U,
    AutomaticTopOff = 8U
};

struct CommandPayload {
    uint64_t command_id;
    CommandAction action;
    CommandTarget target;
    int32_t value;
    uint32_t duration_ms;
    uint32_t expected_configuration_revision;
};

enum class AcknowledgementStatus : uint8_t {
    Accepted = 0U,
    Duplicate = 1U,
    Rejected = 2U,
    Conflict = 3U,
    Invalid = 4U,
    Busy = 5U,
    Expired = 6U
};

struct AcknowledgementPayload {
    uint64_t command_id;
    AcknowledgementStatus status;
    uint16_t reason_code;
    uint32_t configuration_revision;
};

class SequenceWindow {
public:
    SequenceWindow();

    bool accept(uint32_t boot_id, uint32_t sequence);
    void reset();
    uint32_t boot_id() const;
    uint32_t newest_sequence() const;

private:
    bool initialized_;
    uint32_t boot_id_;
    uint32_t newest_sequence_;
};

bool is_valid_message_type(MessageType type);
bool is_expired(const Frame &frame, uint32_t now_ms);
uint32_t crc32(const uint8_t *data, size_t length);

DecodeStatus encode_frame(const Frame &frame,
                          uint8_t *output,
                          size_t output_capacity,
                          size_t *output_length);
DecodeStatus decode_frame(const uint8_t *data,
                          size_t length,
                          Frame *output);

DecodeStatus encode_telemetry_payload(const TelemetryPayload &payload,
                                      uint8_t *output,
                                      size_t output_capacity,
                                      uint16_t *output_length);
DecodeStatus decode_telemetry_payload(const uint8_t *data,
                                      size_t length,
                                      TelemetryPayload *output);

DecodeStatus encode_command_payload(const CommandPayload &payload,
                                    uint8_t *output,
                                    size_t output_capacity,
                                    uint16_t *output_length);
DecodeStatus decode_command_payload(const uint8_t *data,
                                    size_t length,
                                    CommandPayload *output);

/**
 * Packs one complete schedule into the fixed command fields. Modes 0..2,
 * profiles 0..3 (automatic cycle, day, daybreak, night) and minutes 0..1439
 * are accepted. Non-light consumers ignore profile but it must be in range.
 */
bool pack_schedule_command(uint8_t mode,
                           uint8_t profile,
                           uint16_t start_minute,
                           uint16_t end_minute,
                           int32_t *value,
                           uint32_t *duration);

/** Validates and unpacks a schedule encoded by pack_schedule_command(). */
bool unpack_schedule_command(int32_t value,
                             uint32_t duration,
                             SchedulePayload *output);

/**
 * Packs the complete thermostat setting. Heater mode 0 means threshold
 * regulation and 1 means off. Target is 18..30 °C in milli-degrees and
 * hysteresis is 0.1..5 °C in milli-degrees.
 */
bool pack_temperature_command(uint8_t heater_mode,
                              int32_t target_milli_c,
                              uint16_t hysteresis_milli_c,
                              int32_t *value,
                              uint32_t *duration);

/** Validates and unpacks a thermostat command. */
bool unpack_temperature_command(int32_t value,
                                uint32_t duration,
                                uint8_t *heater_mode,
                                int32_t *target_milli_c,
                                uint16_t *hysteresis_milli_c);

DecodeStatus encode_acknowledgement_payload(
    const AcknowledgementPayload &payload,
    uint8_t *output,
    size_t output_capacity,
    uint16_t *output_length);
DecodeStatus decode_acknowledgement_payload(
    const uint8_t *data,
    size_t length,
    AcknowledgementPayload *output);

DecodeStatus cobs_encode(const uint8_t *data,
                         size_t length,
                         uint8_t *output,
                         size_t output_capacity,
                         size_t *output_length);
DecodeStatus cobs_decode(const uint8_t *data,
                         size_t length,
                         uint8_t *output,
                         size_t output_capacity,
                         size_t *output_length);

} // namespace link
} // namespace aquacyd

#endif
