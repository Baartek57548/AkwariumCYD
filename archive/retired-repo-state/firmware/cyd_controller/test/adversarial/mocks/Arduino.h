#ifndef MOCK_ARDUINO_H
#define MOCK_ARDUINO_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <string>
#include <cstdio>
#include <deque>
#include <vector>

#define OUTPUT 1
#define INPUT 0
#define HIGH 1
#define LOW 0
#define SERIAL_8N1 0x800001c

// Mock hardware state
extern uint32_t g_mock_millis;
inline uint32_t millis() { return g_mock_millis; }

extern uint8_t g_speaker_pin_state;
extern int g_speaker_pin_mode;
extern std::vector<std::pair<uint32_t, uint8_t>> g_speaker_transitions;

inline void pinMode(uint8_t pin, uint8_t mode) {
    if (pin == 26) g_speaker_pin_mode = mode;
}

inline void digitalWrite(uint8_t pin, uint8_t val) {
    if (pin == 26) {
        g_speaker_pin_state = val;
        g_speaker_transitions.push_back(std::make_pair(g_mock_millis, val));
    }
}

// Include uart_protocol.h to get String and AquaProtocol definitions
#include "uart_protocol.h"

// Mock Serial stream
class MockSerial {
public:
    std::deque<char> rx_queue;
    std::vector<std::string> tx_lines;
    size_t write_calls = 0U;

    void begin(uint32_t /*baud*/) {}
    void begin(uint32_t /*baud*/, uint32_t /*config*/, int8_t /*rx_pin*/, int8_t /*tx_pin*/) {}
    bool setRxBufferSize(size_t /*size*/) { return true; }
    void setDebugOutput(bool /*enabled*/) {}

    int available() {
        return static_cast<int>(rx_queue.size());
    }

    int read() {
        if (rx_queue.empty()) return -1;
        char c = rx_queue.front();
        rx_queue.pop_front();
        return static_cast<uint8_t>(c);
    }

    size_t println(const String &s) {
        tx_lines.push_back(std::string(s.c_str()));
        return s.length() + 2;
    }

    size_t println(const char *s) {
        tx_lines.push_back(std::string(s ? s : ""));
        return (s ? strlen(s) : 0) + 2;
    }

    size_t write(const uint8_t *data, size_t length) {
        if (data == nullptr || length == 0U) {
            return 0U;
        }

        ++write_calls;
        std::string frame(reinterpret_cast<const char *>(data), length);
        while (!frame.empty() &&
               (frame.back() == '\n' || frame.back() == '\r')) {
            frame.pop_back();
        }
        tx_lines.push_back(frame);
        return length;
    }

    void feed(const std::string &data) {
        for (char c : data) {
            rx_queue.push_back(c);
        }
    }

    void feedLine(const std::string &line) {
        feed(line + "\n");
    }

    void clearTx() {
        tx_lines.clear();
        write_calls = 0U;
    }

    void clearRx() {
        rx_queue.clear();
    }
};

extern MockSerial Serial;

#endif // MOCK_ARDUINO_H
