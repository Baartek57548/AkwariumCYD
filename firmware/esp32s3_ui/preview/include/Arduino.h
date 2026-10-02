#ifndef AQUARIUM_UI_PREVIEW_ARDUINO_H
#define AQUARIUM_UI_PREVIEW_ARDUINO_H

#include <cstddef>
#include <cstdint>

uint32_t millis();

// Host diagnostics are deterministic; no emulated hardware actions are exposed.
struct PreviewEsp {
    uint32_t getFreeHeap() const { return 256U * 1024U; }
    uint32_t getFreePsram() const { return 6U * 1024U * 1024U; }
};

extern PreviewEsp ESP;

#endif

