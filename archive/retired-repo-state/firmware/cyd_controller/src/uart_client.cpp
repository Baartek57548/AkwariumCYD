#include "uart_client.h"
#include "config.h"

namespace {

constexpr size_t UART_RX_BUFFER_SIZE = 1024U;
constexpr size_t UART_RX_BYTES_PER_UPDATE = 192U;
constexpr uint32_t WEMOS_OTA_SAFETY_TIMEOUT_MS = 360000UL;

static_assert(AquaProtocol::UART_BAUD_RATE == HwConfig::UartConsole::BAUD,
              "CYD and AquaBox UART baud rates must stay identical.");

bool is_valid_ipv4(const char *candidate)
{
    if (candidate == nullptr || candidate[0] == '\0') {
        return false;
    }

    const char *cursor = candidate;
    for (uint8_t octet = 0U; octet < 4U; ++octet) {
        uint16_t value = 0U;
        uint8_t digits = 0U;
        while (*cursor >= '0' && *cursor <= '9') {
            value = static_cast<uint16_t>(value * 10U +
                                          static_cast<uint16_t>(*cursor - '0'));
            ++digits;
            if (value > 255U || digits > 3U) {
                return false;
            }
            ++cursor;
        }
        if (digits == 0U) {
            return false;
        }
        if (octet < 3U) {
            if (*cursor != '.') {
                return false;
            }
            ++cursor;
        }
    }
    return *cursor == '\0';
}

} // namespace

UartClient g_uart_client;

UartClient::UartClient()
    : last_rx_ms_(0),
      online_(false),
      rx_idx_(0),
      rx_discard_until_eol_(false),
      last_audio_beep_ms_(0),
      audio_beeping_(false),
      last_ack_ms_(0),
      has_new_ack_(false),
      wemos_in_ota_(false),
      wemos_ota_ack_received_(false),
      wemos_ota_ack_ok_(false),
      wemos_ota_start_ms_(0)
{
    memset(&latest_telemetry_, 0, sizeof(latest_telemetry_));
    memset(&latest_ack_, 0, sizeof(latest_ack_));
    wemos_ota_ip_[0] = '\0';
    latest_telemetry_.temp_target = 25.0f;
    latest_telemetry_.temp_hysteresis = 0.3f;
    latest_telemetry_.ph = 7.0f;
}

void UartClient::begin()
{
    Serial.setRxBufferSize(UART_RX_BUFFER_SIZE);
    Serial.setDebugOutput(false);
    Serial.begin(AquaProtocol::UART_BAUD_RATE,
                 SERIAL_8N1,
                 HwConfig::UartConsole::RX_PIN,
                 HwConfig::UartConsole::TX_PIN);
    pinMode(HwConfig::Audio::SPEAKER_PIN, OUTPUT);
    digitalWrite(HwConfig::Audio::SPEAKER_PIN, LOW);
}

bool UartClient::isControllerOnline() const
{
    return online_;
}

const AquaProtocol::AquariumTelemetry &UartClient::getLatestTelemetry() const
{
    return latest_telemetry_;
}

void UartClient::processLine(const char *line)
{
    AquaProtocol::AquariumMessage msg;
    if (!AquaProtocol::parseMessage(line, msg)) {
        return;
    }

    const uint32_t now_ms = millis();

    if (msg.type == AquaProtocol::MessageType::Telemetry) {
        latest_telemetry_ = msg.telemetry;
        last_rx_ms_ = now_ms;
        online_ = true;

        if (msg.telemetry.ota_mode) {
            wemos_in_ota_ = true;
            if (msg.telemetry.ota_ip[0] != '\0') {
                strncpy(wemos_ota_ip_, msg.telemetry.ota_ip, sizeof(wemos_ota_ip_) - 1);
                wemos_ota_ip_[sizeof(wemos_ota_ip_) - 1] = '\0';
            }
            if (wemos_ota_start_ms_ == 0) {
                wemos_ota_start_ms_ = now_ms;
            }
        } else if (wemos_in_ota_) {
            // WeMos powrócił z trybu OTA do normalnej pracy po resecie
            resetWemosOtaState();
        }
    } else if (msg.type == AquaProtocol::MessageType::Ack) {
        last_rx_ms_ = now_ms;
        online_ = true;
        latest_ack_ = msg.ack;
        last_ack_ms_ = now_ms;
        has_new_ack_ = true;

        if (strcmp(msg.ack.cmd, "enter_ota") == 0) {
            wemos_ota_ack_received_ = true;
            wemos_ota_ack_ok_ = msg.ack.ok;
            if (msg.ack.ok) {
                wemos_in_ota_ = true;
                if (wemos_ota_start_ms_ == 0) {
                    wemos_ota_start_ms_ = now_ms;
                }
                if (msg.ack.message[0] != '\0') {
                    extractOtaIp(msg.ack.message, wemos_ota_ip_, sizeof(wemos_ota_ip_));
                }
            }
        }
    }
}

void UartClient::update()
{
    uint32_t now_ms = millis();

    // Bound the work per I/O tick.  At 115200 Bd this budget still exceeds the
    // bytes that arrive in 10 ms, while a noisy peer cannot starve Wi-Fi or the
    // safety heartbeat.
    size_t bytes_processed = 0U;
    while (bytes_processed < UART_RX_BYTES_PER_UPDATE && Serial.available() > 0) {
        char c = static_cast<char>(Serial.read());
        ++bytes_processed;
        if (c == '\n' || c == '\r') {
            if (!rx_discard_until_eol_ && rx_idx_ > 0U) {
                rx_buf_[rx_idx_] = '\0';
                processLine(rx_buf_);
            }
            rx_idx_ = 0U;
            rx_discard_until_eol_ = false;
        } else if (!rx_discard_until_eol_ && rx_idx_ < sizeof(rx_buf_) - 1U) {
            rx_buf_[rx_idx_++] = c;
        } else {
            // Ignore the rest of an oversized frame.  Restarting at the next
            // delimiter prevents a malformed tail from being parsed as a new command.
            rx_idx_ = 0U;
            rx_discard_until_eol_ = true;
        }
    }

    // 2. Detekcja braku łączności (timeout 3s)
    // Gdy WeMos jest w trybie OTA, celowo wycisza telemetrię UART.
    // Wstrzymujemy standardowy 3-sekundowy timeout, dopóki trwa tryb OTA,
    // z zachowaniem nadrzędnego 6-minutowego watchdoga bezpieczeństwa.
    if (wemos_in_ota_) {
        if (now_ms - wemos_ota_start_ms_ > WEMOS_OTA_SAFETY_TIMEOUT_MS) {
            resetWemosOtaState();
            online_ = false;
        }
    } else if (online_ && (now_ms - last_rx_ms_ > AquaProtocol::OFFLINE_TIMEOUT_MS)) {
        online_ = false;
    }

    // 3. Obsługa sygnału dźwiękowego alarmu na buzzerze CYD
    checkAlarmAudio();
}

void UartClient::checkAlarmAudio()
{
    uint32_t now_ms = millis();

    // Wycisz brzęczyk alarmu, gdy WeMos jest w trybie OTA lub offline
    if (online_ && !wemos_in_ota_ && latest_telemetry_.alarms != AquaProtocol::ALARM_NONE) {
        // Przerywany dźwięk alarmowy co 1 sekundę (100 ms pisk)
        if (now_ms - last_audio_beep_ms_ >= 1000UL) {
            last_audio_beep_ms_ = now_ms;
            audio_beeping_ = true;
            digitalWrite(HwConfig::Audio::SPEAKER_PIN, HIGH);
        } else if (audio_beeping_ && (now_ms - last_audio_beep_ms_ >= 150UL)) {
            digitalWrite(HwConfig::Audio::SPEAKER_PIN, LOW);
            audio_beeping_ = false;
        }
    } else {
        if (audio_beeping_) {
            digitalWrite(HwConfig::Audio::SPEAKER_PIN, LOW);
            audio_beeping_ = false;
        }
    }
}

bool UartClient::sendCommand(const AquaProtocol::AquariumCommand &cmd)
{
    // Gdy WeMos jest w trybie OTA, blokujemy wszelkie zwykłe komendy UART,
    // aby nie zakłócać procesu programowania pamięci Flash na ESP8266.
    if (wemos_in_ota_ && cmd.type != AquaProtocol::CommandType::EnterOta) {
        return false;
    }

    String line = AquaProtocol::serializeCommand(cmd);
    if (line.length() == 0) {
        return false;
    }

    // Print::println(String) performs one write for the JSON and another for
    // CRLF. Commands originate on both ESP32 cores, so that split permits a
    // second command to land between them. One HardwareSerial buffer write is
    // guarded by the framework's UART mutex and preserves an entire NDJSON
    // frame in FIFO order.
    line += '\n';
    const size_t frame_length = line.length();
    const size_t queued = Serial.write(
        reinterpret_cast<const uint8_t *>(line.c_str()), frame_length);
    return queued == frame_length;
}

bool UartClient::sendSetRelay(uint8_t channel, bool state)
{
    AquaProtocol::AquariumCommand cmd;
    cmd.type = AquaProtocol::CommandType::SetRelay;
    cmd.channel = channel;
    cmd.state = state;
    return sendCommand(cmd);
}

bool UartClient::sendSetTemp(float target, float hyst)
{
    AquaProtocol::AquariumCommand cmd;
    cmd.type = AquaProtocol::CommandType::SetTempTarget;
    cmd.temp_target = target;
    cmd.temp_hysteresis = hyst;
    return sendCommand(cmd);
}

bool UartClient::sendSetSchedule(uint8_t dev, uint8_t on_h, uint8_t on_m, uint8_t off_h, uint8_t off_m, bool en)
{
    AquaProtocol::AquariumCommand cmd;
    cmd.type = AquaProtocol::CommandType::SetSchedule;
    cmd.schedule_dev = dev;
    cmd.on_hour = on_h;
    cmd.on_minute = on_m;
    cmd.off_hour = off_h;
    cmd.off_minute = off_m;
    cmd.schedule_enabled = en;
    return sendCommand(cmd);
}

bool UartClient::sendFeedTrigger()
{
    AquaProtocol::AquariumCommand cmd;
    cmd.type = AquaProtocol::CommandType::FeedTrigger;
    return sendCommand(cmd);
}

bool UartClient::sendSyncLights()
{
    AquaProtocol::AquariumCommand cmd;
    cmd.type = AquaProtocol::CommandType::SyncLights;
    return sendCommand(cmd);
}

bool UartClient::sendSyncTime(uint32_t epoch)
{
    AquaProtocol::AquariumCommand cmd;
    cmd.type = AquaProtocol::CommandType::SyncTime;
    cmd.epoch_time = epoch;
    return sendCommand(cmd);
}

bool UartClient::sendClearAlarm()
{
    AquaProtocol::AquariumCommand cmd;
    cmd.type = AquaProtocol::CommandType::ClearAlarm;
    return sendCommand(cmd);
}

bool UartClient::sendSafeMode(bool enable)
{
    AquaProtocol::AquariumCommand cmd;
    cmd.type = AquaProtocol::CommandType::SetSafeMode;
    cmd.state = enable;
    return sendCommand(cmd);
}

bool UartClient::sendEnterOta(const char *ssid, const char *pass)
{
    AquaProtocol::AquariumCommand cmd = {};
    cmd.type = AquaProtocol::CommandType::EnterOta;
    if (ssid != nullptr && ssid[0] != '\0') {
        strncpy(cmd.ota_ssid, ssid, sizeof(cmd.ota_ssid) - 1);
        cmd.ota_ssid[sizeof(cmd.ota_ssid) - 1] = '\0';
    } else {
        cmd.ota_ssid[0] = '\0';
    }
    if (pass != nullptr && pass[0] != '\0') {
        strncpy(cmd.ota_pass, pass, sizeof(cmd.ota_pass) - 1);
        cmd.ota_pass[sizeof(cmd.ota_pass) - 1] = '\0';
    } else {
        cmd.ota_pass[0] = '\0';
    }

    wemos_ota_ack_received_ = false;
    wemos_ota_ack_ok_ = false;
    wemos_ota_ip_[0] = '\0';
    wemos_ota_start_ms_ = millis();

    const bool sent = sendCommand(cmd);
    if (!sent) {
        resetWemosOtaState();
    }
    return sent;
}

bool UartClient::isWemosInOta() const
{
    return wemos_in_ota_;
}

bool UartClient::hasWemosOtaAck() const
{
    return wemos_ota_ack_received_;
}

bool UartClient::isWemosOtaAckOk() const
{
    return wemos_ota_ack_ok_;
}

const char *UartClient::getWemosOtaIp() const
{
    return wemos_ota_ip_;
}

uint32_t UartClient::getWemosOtaElapsedMs() const
{
    if (!wemos_in_ota_ || wemos_ota_start_ms_ == 0) {
        return 0;
    }
    return millis() - wemos_ota_start_ms_;
}

const AquaProtocol::AquariumAck &UartClient::getLatestAck() const
{
    return latest_ack_;
}

bool UartClient::hasNewAck() const
{
    return has_new_ack_;
}

void UartClient::clearNewAck()
{
    has_new_ack_ = false;
}

void UartClient::resetWemosOtaState()
{
    wemos_in_ota_ = false;
    wemos_ota_ack_received_ = false;
    wemos_ota_ack_ok_ = false;
    wemos_ota_ip_[0] = '\0';
    wemos_ota_start_ms_ = 0;
}

void UartClient::extractOtaIp(const char *src, char *dst, size_t dst_size)
{
    if (src == nullptr || dst == nullptr || dst_size == 0) {
        return;
    }
    dst[0] = '\0';

    const char *p = src;
    if (strncmp(p, "STA:", 4) == 0) {
        p += 4;
    } else if (strncmp(p, "AP:", 3) == 0) {
        p += 3;
    }

    while (*p == ' ') {
        p++;
    }

    char candidate[16] = {};
    size_t i = 0U;
    while (*p != '\0' && *p != ' ' && *p != '\r' && *p != '\n') {
        if (i >= sizeof(candidate) - 1U) {
            return;
        }
        candidate[i++] = *p++;
    }
    candidate[i] = '\0';

    if (!is_valid_ipv4(candidate)) {
        return;
    }

    strncpy(dst, candidate, dst_size - 1U);
    dst[dst_size - 1U] = '\0';
}
