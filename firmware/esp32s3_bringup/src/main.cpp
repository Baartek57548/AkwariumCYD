#include <Arduino.h>
#include <esp_heap_caps.h>

namespace {

constexpr uint32_t kExpectedFlashBytes = 16UL * 1024UL * 1024UL;
constexpr uint32_t kExpectedPsramBytes = 8UL * 1024UL * 1024UL;
constexpr uint32_t kMinimumUsablePsramBytes = kExpectedPsramBytes - 64UL * 1024UL;
constexpr size_t kRgb565FrameBytes = 800UL * 480UL * sizeof(uint16_t);
constexpr uint32_t kReportIntervalMs = 5000UL;
bool diagnosticPass = false;

bool testFrameBuffer() {
    uint8_t *const frame = static_cast<uint8_t *>(
        heap_caps_malloc(kRgb565FrameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (frame == nullptr) {
        Serial.println("FAIL: Cannot allocate one 800x480 RGB565 frame in PSRAM.");
        return false;
    }

    // Write and read every byte; allocation alone does not prove the external
    // RAM is accessible throughout the area required by the RGB scanout.
    for (size_t i = 0; i < kRgb565FrameBytes; ++i) {
        frame[i] = static_cast<uint8_t>((i * 31U) ^ (i >> 8U));
    }

    bool valid = true;
    for (size_t i = 0; i < kRgb565FrameBytes; ++i) {
        if (frame[i] != static_cast<uint8_t>((i * 31U) ^ (i >> 8U))) {
            Serial.printf("FAIL: PSRAM frame readback mismatch at byte %u.\n",
                          static_cast<unsigned>(i));
            valid = false;
            break;
        }
    }

    heap_caps_free(frame);
    if (valid) {
        Serial.println("PASS: 800x480 RGB565 framebuffer allocation and readback.");
    }
    return valid;
}

void reportMemory() {
    Serial.printf("Flash: %u bytes; usable PSRAM heap: %u bytes; free PSRAM: %u bytes; "
                  "free internal heap: %u bytes.\n",
                  static_cast<unsigned>(ESP.getFlashChipSize()),
                  static_cast<unsigned>(ESP.getPsramSize()),
                  static_cast<unsigned>(ESP.getFreePsram()),
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("AquaCYD ESP32-S3 N16R8 bring-up: no external GPIO configured by the application.");
    reportMemory();

    const bool flashOk = ESP.getFlashChipSize() == kExpectedFlashBytes;
    // Arduino reports PSRAM as managed heap, excluding allocator metadata;
    // its usable size can be slightly below the chip's nominal 8 MiB.
    const bool psramOk = psramFound() && ESP.getPsramSize() >= kMinimumUsablePsramBytes;
    if (!flashOk) {
        Serial.println("FAIL: Detected flash size differs from the expected 16 MiB.");
    }
    if (!psramOk) {
        Serial.println("FAIL: OPI PSRAM is absent or has less than 8 MiB minus heap overhead.");
    }

    const bool frameOk = psramOk && testFrameBuffer();
    diagnosticPass = flashOk && psramOk && frameOk;
    Serial.println(diagnosticPass ? "RESULT: PASS" : "RESULT: FAIL");
}

void loop() {
    static uint32_t lastReportMs = 0;
    const uint32_t nowMs = millis();
    if (static_cast<uint32_t>(nowMs - lastReportMs) >= kReportIntervalMs) {
        lastReportMs = nowMs;
        reportMemory();
        Serial.println(diagnosticPass ? "RESULT: PASS" : "RESULT: FAIL");
    }
    delay(20);
}
