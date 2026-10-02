#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>

#include "panel_hal.h"
#include "ui_app.h"
#include "ui_io.h"

namespace {
StaticTask_t displayControl;
StackType_t displayStack[12288 / sizeof(StackType_t)];
void displayTask(void *) {
    if (!psramFound()) {
        Serial.println("[S3 UI] PSRAM unavailable; LVGL cannot start safely");
        vTaskDelete(nullptr);
    }
    lv_init();
    if (!panel_hal_init()) {
        Serial.println("[S3 UI] LCD initialization failed; no UI task started");
        vTaskDelete(nullptr);
    }
    if (!ui_app_init()) {
        Serial.println("[S3 UI] LVGL UI construction failed");
        vTaskDelete(nullptr);
    }
    Serial.println("[AquaBox] Core 1 RGB/LVGL/touch; core 0 WiFi/IO; UART disconnected");
    uint32_t lastTickMs = millis();
    for (;;) {
        const uint32_t now = millis();
        lv_tick_inc(now - lastTickMs);
        lastTickMs = now;
        // RGB and LVGL have exactly one owner, including the cross-core flash handshake.
        if (ui_io::videoService()) {
            panel_hal_service();
            ui_app_service();
            lv_timer_handler();
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}
} // namespace
void setup() {
    Serial.begin(115200);
    if (!ui_io::start() ||
        !xTaskCreateStaticPinnedToCore(displayTask, "aquabox-display", sizeof(displayStack), nullptr, 4,
                                       displayStack, &displayControl, 1))
        Serial.println("[AquaBox] Task creation failed; outputs remain disabled");
}
void loop() { vTaskDelay(pdMS_TO_TICKS(1000)); }
