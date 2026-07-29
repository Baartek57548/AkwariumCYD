#include "aquacyd_link_protocol.h"

#include <string.h>

namespace aquacyd {
namespace link {
namespace {

constexpr uint8_t kMagic0 = 0x41U;
constexpr uint8_t kMagic1 = 0x51U;
constexpr uint8_t kTelemetrySchemaVersion = 1U;
constexpr uint8_t kCommandSchemaVersion = 1U;
constexpr uint8_t kAcknowledgementSchemaVersion = 1U;
constexpr size_t kTelemetryPayloadSize = 39U;
constexpr size_t kCommandPayloadSize = 23U;
constexpr size_t kAcknowledgementPayloadSize = 16U;

void write_u16(uint8_t *destination, uint16_t value) {
    destination[0] = static_cast<uint8_t>(value);
    destination[1] = static_cast<uint8_t>(value >> 8U);
}

void write_u32(uint8_t *destination, uint32_t value) {
    destination[0] = static_cast<uint8_t>(value);
    destination[1] = static_cast<uint8_t>(value >> 8U);
    destination[2] = static_cast<uint8_t>(value >> 16U);
    destination[3] = static_cast<uint8_t>(value >> 24U);
}

void write_u64(uint8_t *destination, uint64_t value) {
    for (uint8_t index = 0U; index < 8U; ++index) {
        destination[index] =
            static_cast<uint8_t>(value >> (static_cast<uint64_t>(index) * 8U));
    }
}

uint16_t read_u16(const uint8_t *source) {
    return static_cast<uint16_t>(source[0]) |
           static_cast<uint16_t>(source[1]) << 8U;
}

uint32_t read_u32(const uint8_t *source) {
    return static_cast<uint32_t>(source[0]) |
           static_cast<uint32_t>(source[1]) << 8U |
           static_cast<uint32_t>(source[2]) << 16U |
           static_cast<uint32_t>(source[3]) << 24U;
}

uint64_t read_u64(const uint8_t *source) {
    uint64_t value = 0U;
    for (uint8_t index = 0U; index < 8U; ++index) {
        value |= static_cast<uint64_t>(source[index])
                 << (static_cast<uint64_t>(index) * 8U);
    }
    return value;
}

bool is_valid_command_action(CommandAction action) {
    const uint8_t value = static_cast<uint8_t>(action);
    return value >= static_cast<uint8_t>(CommandAction::SetOutput) &&
           value <= static_cast<uint8_t>(CommandAction::SynchronizeTime);
}

bool is_valid_command_target(CommandTarget target) {
    return static_cast<uint8_t>(target) <=
           static_cast<uint8_t>(CommandTarget::AutomaticTopOff);
}

bool is_valid_acknowledgement_status(AcknowledgementStatus status) {
    return static_cast<uint8_t>(status) <=
           static_cast<uint8_t>(AcknowledgementStatus::Expired);
}

} // namespace

SequenceWindow::SequenceWindow()
    : initialized_(false), boot_id_(0U), newest_sequence_(0U) {
}

bool SequenceWindow::accept(uint32_t boot_id, uint32_t sequence) {
    if (boot_id == 0U || sequence == 0U) {
        return false;
    }
    if (!initialized_ || boot_id_ != boot_id) {
        initialized_ = true;
        boot_id_ = boot_id;
        newest_sequence_ = sequence;
        return true;
    }
    if (static_cast<int32_t>(sequence - newest_sequence_) <= 0) {
        return false;
    }
    newest_sequence_ = sequence;
    return true;
}

void SequenceWindow::reset() {
    initialized_ = false;
    boot_id_ = 0U;
    newest_sequence_ = 0U;
}

uint32_t SequenceWindow::boot_id() const {
    return boot_id_;
}

uint32_t SequenceWindow::newest_sequence() const {
    return newest_sequence_;
}

bool is_valid_message_type(MessageType type) {
    const uint8_t value = static_cast<uint8_t>(type);
    return value >= static_cast<uint8_t>(MessageType::Hello) &&
           value <= static_cast<uint8_t>(MessageType::Heartbeat);
}

bool is_expired(const Frame &frame, uint32_t now_ms) {
    return frame.ttl_ms != 0U &&
           static_cast<uint32_t>(now_ms - frame.issued_at_ms) > frame.ttl_ms;
}

uint32_t crc32(const uint8_t *data, size_t length) {
    if (data == nullptr && length != 0U) {
        return 0U;
    }
    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t index = 0U; index < length; ++index) {
        crc ^= data[index];
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask =
                static_cast<uint32_t>(-static_cast<int32_t>(crc & 1U));
            crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
        }
    }
    return ~crc;
}

DecodeStatus encode_frame(const Frame &frame,
                          uint8_t *output,
                          size_t output_capacity,
                          size_t *output_length) {
    if (output == nullptr || output_length == nullptr) {
        return DecodeStatus::NullArgument;
    }
    *output_length = 0U;
    if (!is_valid_message_type(frame.type)) {
        return DecodeStatus::InvalidMessageType;
    }
    if (frame.source_id == 0U || frame.boot_id == 0U || frame.sequence == 0U) {
        return DecodeStatus::InvalidIdentity;
    }
    if (frame.payload_length > kMaximumPayloadSize) {
        return DecodeStatus::TooLarge;
    }
    const size_t required =
        kHeaderSize + static_cast<size_t>(frame.payload_length) + kCrcSize;
    if (required > output_capacity) {
        return DecodeStatus::OutputTooSmall;
    }

    output[0] = kMagic0;
    output[1] = kMagic1;
    output[2] = kProtocolVersion;
    output[3] = static_cast<uint8_t>(kHeaderSize);
    output[4] = static_cast<uint8_t>(frame.type);
    output[5] = frame.flags;
    write_u16(output + 6U, frame.payload_length);
    write_u32(output + 8U, frame.source_id);
    write_u32(output + 12U, frame.boot_id);
    write_u32(output + 16U, frame.sequence);
    write_u32(output + 20U, frame.acknowledged_sequence);
    write_u32(output + 24U, frame.issued_at_ms);
    write_u32(output + 28U, frame.ttl_ms);
    if (frame.payload_length != 0U) {
        memcpy(output + kHeaderSize, frame.payload, frame.payload_length);
    }
    write_u32(output + required - kCrcSize,
              crc32(output, required - kCrcSize));
    *output_length = required;
    return DecodeStatus::Ok;
}

DecodeStatus decode_frame(const uint8_t *data,
                          size_t length,
                          Frame *output) {
    if (data == nullptr || output == nullptr) {
        return DecodeStatus::NullArgument;
    }
    if (length < kHeaderSize + kCrcSize) {
        return DecodeStatus::Truncated;
    }
    if (length > kEspNowMaximumFrameSize) {
        return DecodeStatus::TooLarge;
    }
    if (data[0] != kMagic0 || data[1] != kMagic1) {
        return DecodeStatus::InvalidMagic;
    }
    if (data[2] != kProtocolVersion) {
        return DecodeStatus::UnsupportedVersion;
    }
    if (data[3] != kHeaderSize) {
        return DecodeStatus::InvalidHeader;
    }

    const MessageType type = static_cast<MessageType>(data[4]);
    if (!is_valid_message_type(type)) {
        return DecodeStatus::InvalidMessageType;
    }
    const uint16_t payload_length = read_u16(data + 6U);
    const size_t expected =
        kHeaderSize + static_cast<size_t>(payload_length) + kCrcSize;
    if (payload_length > kMaximumPayloadSize || expected != length) {
        return DecodeStatus::InvalidLength;
    }
    const uint32_t expected_crc = read_u32(data + length - kCrcSize);
    if (expected_crc != crc32(data, length - kCrcSize)) {
        return DecodeStatus::CrcMismatch;
    }

    Frame decoded = {};
    decoded.type = type;
    decoded.flags = data[5];
    decoded.payload_length = payload_length;
    decoded.source_id = read_u32(data + 8U);
    decoded.boot_id = read_u32(data + 12U);
    decoded.sequence = read_u32(data + 16U);
    decoded.acknowledged_sequence = read_u32(data + 20U);
    decoded.issued_at_ms = read_u32(data + 24U);
    decoded.ttl_ms = read_u32(data + 28U);
    if (decoded.source_id == 0U ||
        decoded.boot_id == 0U ||
        decoded.sequence == 0U) {
        return DecodeStatus::InvalidIdentity;
    }
    if (payload_length != 0U) {
        memcpy(decoded.payload, data + kHeaderSize, payload_length);
    }
    *output = decoded;
    return DecodeStatus::Ok;
}

DecodeStatus encode_telemetry_payload(const TelemetryPayload &payload,
                                      uint8_t *output,
                                      size_t output_capacity,
                                      uint16_t *output_length) {
    if (output == nullptr || output_length == nullptr) {
        return DecodeStatus::NullArgument;
    }
    *output_length = 0U;
    if (output_capacity < kTelemetryPayloadSize) {
        return DecodeStatus::OutputTooSmall;
    }
    output[0] = kTelemetrySchemaVersion;
    write_u16(output + 1U, payload.flags);
    write_u32(output + 3U, static_cast<uint32_t>(payload.temperature_milli_c));
    write_u32(output + 7U, static_cast<uint32_t>(payload.ph_milli));
    write_u32(output + 11U, static_cast<uint32_t>(payload.ec_milli_us_cm));
    write_u16(output + 15U, payload.ldr_raw);
    write_u16(output + 17U, payload.relay_bits);
    write_u32(output + 19U, payload.alarm_flags);
    write_u32(output + 23U, payload.uptime_seconds);
    write_u32(output + 27U, payload.free_heap_bytes);
    write_u16(output + 31U, static_cast<uint16_t>(payload.wifi_rssi_dbm));
    write_u32(output + 33U, payload.configuration_revision);
    write_u16(output + 37U, 0U);
    *output_length = static_cast<uint16_t>(kTelemetryPayloadSize);
    return DecodeStatus::Ok;
}

DecodeStatus decode_telemetry_payload(const uint8_t *data,
                                      size_t length,
                                      TelemetryPayload *output) {
    if (data == nullptr || output == nullptr) {
        return DecodeStatus::NullArgument;
    }
    if (length != kTelemetryPayloadSize) {
        return DecodeStatus::InvalidLength;
    }
    if (data[0] != kTelemetrySchemaVersion || read_u16(data + 37U) != 0U) {
        return DecodeStatus::UnsupportedVersion;
    }
    TelemetryPayload decoded = {};
    decoded.flags = read_u16(data + 1U);
    decoded.temperature_milli_c = static_cast<int32_t>(read_u32(data + 3U));
    decoded.ph_milli = static_cast<int32_t>(read_u32(data + 7U));
    decoded.ec_milli_us_cm = static_cast<int32_t>(read_u32(data + 11U));
    decoded.ldr_raw = read_u16(data + 15U);
    decoded.relay_bits = read_u16(data + 17U);
    decoded.alarm_flags = read_u32(data + 19U);
    decoded.uptime_seconds = read_u32(data + 23U);
    decoded.free_heap_bytes = read_u32(data + 27U);
    decoded.wifi_rssi_dbm = static_cast<int16_t>(read_u16(data + 31U));
    decoded.configuration_revision = read_u32(data + 33U);
    *output = decoded;
    return DecodeStatus::Ok;
}

DecodeStatus encode_command_payload(const CommandPayload &payload,
                                    uint8_t *output,
                                    size_t output_capacity,
                                    uint16_t *output_length) {
    if (output == nullptr || output_length == nullptr) {
        return DecodeStatus::NullArgument;
    }
    *output_length = 0U;
    if (output_capacity < kCommandPayloadSize) {
        return DecodeStatus::OutputTooSmall;
    }
    if (payload.command_id == 0U ||
        !is_valid_command_action(payload.action) ||
        !is_valid_command_target(payload.target)) {
        return DecodeStatus::InvalidIdentity;
    }
    output[0] = kCommandSchemaVersion;
    write_u64(output + 1U, payload.command_id);
    output[9] = static_cast<uint8_t>(payload.action);
    output[10] = static_cast<uint8_t>(payload.target);
    write_u32(output + 11U, static_cast<uint32_t>(payload.value));
    write_u32(output + 15U, payload.duration_ms);
    write_u32(output + 19U, payload.expected_configuration_revision);
    *output_length = static_cast<uint16_t>(kCommandPayloadSize);
    return DecodeStatus::Ok;
}

DecodeStatus decode_command_payload(const uint8_t *data,
                                    size_t length,
                                    CommandPayload *output) {
    if (data == nullptr || output == nullptr) {
        return DecodeStatus::NullArgument;
    }
    if (length != kCommandPayloadSize) {
        return DecodeStatus::InvalidLength;
    }
    if (data[0] != kCommandSchemaVersion) {
        return DecodeStatus::UnsupportedVersion;
    }
    CommandPayload decoded = {};
    decoded.command_id = read_u64(data + 1U);
    decoded.action = static_cast<CommandAction>(data[9]);
    decoded.target = static_cast<CommandTarget>(data[10]);
    decoded.value = static_cast<int32_t>(read_u32(data + 11U));
    decoded.duration_ms = read_u32(data + 15U);
    decoded.expected_configuration_revision = read_u32(data + 19U);
    if (decoded.command_id == 0U ||
        !is_valid_command_action(decoded.action) ||
        !is_valid_command_target(decoded.target)) {
        return DecodeStatus::InvalidIdentity;
    }
    *output = decoded;
    return DecodeStatus::Ok;
}

DecodeStatus encode_acknowledgement_payload(
    const AcknowledgementPayload &payload,
    uint8_t *output,
    size_t output_capacity,
    uint16_t *output_length) {
    if (output == nullptr || output_length == nullptr) {
        return DecodeStatus::NullArgument;
    }
    *output_length = 0U;
    if (output_capacity < kAcknowledgementPayloadSize) {
        return DecodeStatus::OutputTooSmall;
    }
    if (payload.command_id == 0U ||
        !is_valid_acknowledgement_status(payload.status)) {
        return DecodeStatus::InvalidIdentity;
    }
    output[0] = kAcknowledgementSchemaVersion;
    write_u64(output + 1U, payload.command_id);
    output[9] = static_cast<uint8_t>(payload.status);
    write_u16(output + 10U, payload.reason_code);
    write_u32(output + 12U, payload.configuration_revision);
    *output_length = static_cast<uint16_t>(kAcknowledgementPayloadSize);
    return DecodeStatus::Ok;
}

DecodeStatus decode_acknowledgement_payload(
    const uint8_t *data,
    size_t length,
    AcknowledgementPayload *output) {
    if (data == nullptr || output == nullptr) {
        return DecodeStatus::NullArgument;
    }
    if (length != kAcknowledgementPayloadSize) {
        return DecodeStatus::InvalidLength;
    }
    if (data[0] != kAcknowledgementSchemaVersion) {
        return DecodeStatus::UnsupportedVersion;
    }
    AcknowledgementPayload decoded = {};
    decoded.command_id = read_u64(data + 1U);
    decoded.status = static_cast<AcknowledgementStatus>(data[9]);
    decoded.reason_code = read_u16(data + 10U);
    decoded.configuration_revision = read_u32(data + 12U);
    if (decoded.command_id == 0U ||
        !is_valid_acknowledgement_status(decoded.status)) {
        return DecodeStatus::InvalidIdentity;
    }
    *output = decoded;
    return DecodeStatus::Ok;
}

DecodeStatus cobs_encode(const uint8_t *data,
                         size_t length,
                         uint8_t *output,
                         size_t output_capacity,
                         size_t *output_length) {
    if ((data == nullptr && length != 0U) ||
        output == nullptr ||
        output_length == nullptr) {
        return DecodeStatus::NullArgument;
    }
    *output_length = 0U;
    if (length > kEspNowMaximumFrameSize) {
        return DecodeStatus::TooLarge;
    }
    if (output_capacity == 0U) {
        return DecodeStatus::OutputTooSmall;
    }

    size_t read_index = 0U;
    size_t write_index = 1U;
    size_t code_index = 0U;
    uint8_t code = 1U;
    while (read_index < length) {
        if (data[read_index] == 0U) {
            if (code_index >= output_capacity) {
                return DecodeStatus::OutputTooSmall;
            }
            output[code_index] = code;
            code_index = write_index++;
            code = 1U;
            ++read_index;
            continue;
        }
        if (write_index >= output_capacity) {
            return DecodeStatus::OutputTooSmall;
        }
        output[write_index++] = data[read_index++];
        ++code;
        if (code == 0xFFU) {
            if (code_index >= output_capacity) {
                return DecodeStatus::OutputTooSmall;
            }
            output[code_index] = code;
            code_index = write_index++;
            code = 1U;
        }
    }
    if (code_index >= output_capacity) {
        return DecodeStatus::OutputTooSmall;
    }
    output[code_index] = code;
    *output_length = write_index;
    return DecodeStatus::Ok;
}

DecodeStatus cobs_decode(const uint8_t *data,
                         size_t length,
                         uint8_t *output,
                         size_t output_capacity,
                         size_t *output_length) {
    if (data == nullptr || output == nullptr || output_length == nullptr) {
        return DecodeStatus::NullArgument;
    }
    *output_length = 0U;
    if (length == 0U || length > kMaximumCobsFrameSize) {
        return DecodeStatus::CobsMalformed;
    }

    size_t read_index = 0U;
    size_t write_index = 0U;
    while (read_index < length) {
        const uint8_t code = data[read_index++];
        if (code == 0U ||
            static_cast<size_t>(code - 1U) > length - read_index) {
            return DecodeStatus::CobsMalformed;
        }
        for (uint8_t index = 1U; index < code; ++index) {
            if (write_index >= output_capacity) {
                return DecodeStatus::OutputTooSmall;
            }
            output[write_index++] = data[read_index++];
        }
        if (code != 0xFFU && read_index < length) {
            if (write_index >= output_capacity) {
                return DecodeStatus::OutputTooSmall;
            }
            output[write_index++] = 0U;
        }
    }
    *output_length = write_index;
    return DecodeStatus::Ok;
}

} // namespace link
} // namespace aquacyd
