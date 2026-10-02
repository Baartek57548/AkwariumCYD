#ifndef ARDUINO_H
#define ARDUINO_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <string>
#include <cstdio>

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
};

#endif // ARDUINO_H
