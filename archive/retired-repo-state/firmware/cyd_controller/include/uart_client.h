#ifndef UART_CLIENT_H
#define UART_CLIENT_H

#include <Arduino.h>
#include "uart_protocol.h"

class UartClient {
public:
    UartClient();

    void begin();
    void update();

    // Dostęp do stanu sterownika
    bool isControllerOnline() const;
    const AquaProtocol::AquariumTelemetry &getLatestTelemetry() const;

    // Wysyłanie komend do WeMos
    bool sendSetRelay(uint8_t channel, bool state);
    bool sendSetTemp(float target, float hyst);
    bool sendSetSchedule(uint8_t dev, uint8_t on_h, uint8_t on_m, uint8_t off_h, uint8_t off_m, bool en);
    bool sendFeedTrigger();
    bool sendSyncLights();
    bool sendSyncTime(uint32_t epoch);
    bool sendClearAlarm();
    bool sendSafeMode(bool enable);
    bool sendEnterOta(const char *ssid = nullptr, const char *pass = nullptr);

    // Śledzenie stanu WeMos OTA
    bool isWemosInOta() const;
    bool hasWemosOtaAck() const;
    bool isWemosOtaAckOk() const;
    const char *getWemosOtaIp() const;
    uint32_t getWemosOtaElapsedMs() const;
    void resetWemosOtaState();

    // Dostęp do komunikatów ACK
    const AquaProtocol::AquariumAck &getLatestAck() const;
    bool hasNewAck() const;
    void clearNewAck();

    // Dźwięk alarmu na CYD (GPIO26)
    void checkAlarmAudio();

private:
    AquaProtocol::AquariumTelemetry latest_telemetry_;
    uint32_t last_rx_ms_;
    volatile bool online_;
    char rx_buf_[512];
    size_t rx_idx_;
    bool rx_discard_until_eol_;

    uint32_t last_audio_beep_ms_;
    bool audio_beeping_;

    AquaProtocol::AquariumAck latest_ack_;
    uint32_t last_ack_ms_;
    volatile bool has_new_ack_;

    volatile bool wemos_in_ota_;
    volatile bool wemos_ota_ack_received_;
    volatile bool wemos_ota_ack_ok_;
    char wemos_ota_ip_[16];
    uint32_t wemos_ota_start_ms_;

    static void extractOtaIp(const char *src, char *dst, size_t dst_size);
    void processLine(const char *line);
    bool sendCommand(const AquaProtocol::AquariumCommand &cmd);
};

extern UartClient g_uart_client;

#endif // UART_CLIENT_H
