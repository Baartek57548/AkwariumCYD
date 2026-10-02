#include "panel_hal.h"
#include "ui_navigation.h"

#include <Arduino.h>
#include <Wire.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <lvgl.h>

#include <cstring>

namespace {

constexpr int kWidth = 800;
constexpr int kHeight = 480;
constexpr int kRgbPins[16] = {
    15, 16, 17, 18, 21,     // B3..B7
    9,  10, 11, 12, 13, 14, // G2..G7
    4,  5,  6,  7,  8       // R3..R7
};
constexpr uint8_t kTouchAddresses[2] = {0x5D, 0x14};
constexpr uint16_t kProductIdRegister = 0x8140;
constexpr uint16_t kStatusRegister = 0x814E;
constexpr uint16_t kPointRegister = 0x814F;
constexpr uint32_t kTouchReleaseTimeoutMs = 90;
constexpr uint32_t kTouchRetryIntervalMs = 3000;
constexpr size_t kDrawRows = 16;

esp_lcd_panel_handle_t rgbPanel = nullptr;
uint16_t *framebuffer = nullptr;
lv_color_t *drawBuffer = nullptr;
lv_disp_draw_buf_t lvDrawBuffer;
lv_disp_drv_t lvDisplayDriver;
lv_indev_drv_t lvTouchDriver;
uint8_t touchAddress = 0;
uint32_t lastTouchEventMs = 0;
uint32_t lastTouchRetryMs = 0;
uint32_t touchCount = 0;
lv_point_t touchPoint = {0, 0};
bool touching = false;
bool wireReady = false;
uint8_t touchReadFailures = 0;

void logError(const char *stage, esp_err_t error) {
    Serial.printf("[S3 UI] %s: %s (%d)\n", stage, esp_err_to_name(error), static_cast<int>(error));
}

bool readTouchRegister(uint8_t address, uint16_t reg, uint8_t *out, size_t length) {
    if (out == nullptr || length == 0 || length > 16)
        return false;
    Wire.beginTransmission(address);
    if (Wire.write(static_cast<uint8_t>(reg >> 8)) != 1 || Wire.write(static_cast<uint8_t>(reg)) != 1) {
        Wire.endTransmission(true);
        return false;
    }
    if (Wire.endTransmission(false) != 0) {
        return false;
    }
    if (Wire.requestFrom(address, length, true) != length) {
        while (Wire.available())
            Wire.read();
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        const int value = Wire.read();
        if (value < 0)
            return false;
        out[i] = static_cast<uint8_t>(value);
    }
    return true;
}

bool acknowledgeTouch(uint8_t address) {
    Wire.beginTransmission(address);
    if (Wire.write(static_cast<uint8_t>(kStatusRegister >> 8)) != 1 ||
        Wire.write(static_cast<uint8_t>(kStatusRegister)) != 1 ||
        Wire.write(static_cast<uint8_t>(0)) != 1) {
        Wire.endTransmission(true);
        return false;
    }
    return Wire.endTransmission(true) == 0;
}

bool detectTouch() {
    for (uint8_t address : kTouchAddresses) {
        uint8_t id[4] = {};
        if (readTouchRegister(address, kProductIdRegister, id, sizeof(id)) && id[0] == '9' &&
            id[1] == '1' && id[2] == '1') {
            touchAddress = address;
            touchReadFailures = 0;
            Serial.printf("[S3 UI] GT911 at 0x%02X\n", touchAddress);
            return true;
        }
    }
    touchAddress = 0;
    return false;
}

void resetTouch() {
    pinMode(47, OUTPUT);
    digitalWrite(47, LOW);
    delay(20);
    digitalWrite(47, HIGH);
    delay(100);
}

void flushDisplay(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *pixels) {
    if (framebuffer != nullptr && area != nullptr && pixels != nullptr && area->x1 >= 0 && area->y1 >= 0 &&
        area->x2 < kWidth && area->y2 < kHeight) {
        const size_t width = static_cast<size_t>(area->x2 - area->x1 + 1);
        const size_t height = static_cast<size_t>(area->y2 - area->y1 + 1);
        for (size_t row = 0; row < height; ++row) {
            uint16_t *destination = framebuffer + static_cast<size_t>(area->y1 + row) * kWidth + area->x1;
            memcpy(destination, pixels + row * width, width * sizeof(uint16_t));
        }
    }
    lv_disp_flush_ready(driver);
}

void readTouch(lv_indev_drv_t *, lv_indev_data_t *data) {
    const uint32_t now = millis();
    if (wireReady && touchAddress != 0) {
        uint8_t status = 0;
        if (!readTouchRegister(touchAddress, kStatusRegister, &status, 1)) {
            if (++touchReadFailures >= 10) {
                touchAddress = 0;
                touchReadFailures = 0;
                touching = false;
                lastTouchRetryMs = now;
                Serial.println("[S3 UI] GT911 disconnected; retry scheduled");
            }
        } else if ((status & 0x80U) != 0U) {
            touchReadFailures = 0;
            const uint8_t count = status & 0x0FU;
            if (count > 0 && count <= 5) {
                uint8_t point[8] = {};
                if (readTouchRegister(touchAddress, kPointRegister, point, sizeof(point))) {
                    const uint16_t x = static_cast<uint16_t>(point[1] | (point[2] << 8));
                    const uint16_t y = static_cast<uint16_t>(point[3] | (point[4] << 8));
                    if (x < kWidth && y < kHeight) {
                        touchPoint.x = x;
                        touchPoint.y = y;
                        touching = true;
                        lastTouchEventMs = now;
                        ++touchCount;
                    }
                }
            } else {
                touching = false;
            }
            if (!acknowledgeTouch(touchAddress)) {
                Serial.println("[S3 UI] GT911 status acknowledge failed");
            }
        } else {
            touchReadFailures = 0;
        }
    }
    if (static_cast<uint32_t>(now - lastTouchEventMs) > kTouchReleaseTimeoutMs) {
        touching = false;
    }
    data->point = touchPoint;
    data->state = touching ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}

bool createRgbPanel() {
    esp_lcd_rgb_panel_config_t config = {};
    config.clk_src = LCD_CLK_SRC_PLL160M;
    config.timings.pclk_hz = 16000000;
    config.timings.h_res = kWidth;
    config.timings.v_res = kHeight;
    config.timings.hsync_pulse_width = 4;
    config.timings.hsync_back_porch = 40;
    config.timings.hsync_front_porch = 48;
    config.timings.vsync_pulse_width = 4;
    config.timings.vsync_back_porch = 32;
    config.timings.vsync_front_porch = 13;
    config.timings.flags.pclk_active_neg = 1;
    config.data_width = 16;
    config.bits_per_pixel = 16;
    config.num_fbs = 1;
    config.bounce_buffer_size_px = kWidth * 10;
    config.dma_burst_size = 64;
    config.hsync_gpio_num = 2;
    config.vsync_gpio_num = 39;
    config.de_gpio_num = 40;
    config.pclk_gpio_num = 1;
    config.disp_gpio_num = -1;
    for (size_t index = 0; index < 16; ++index)
        config.data_gpio_nums[index] = kRgbPins[index];
    config.flags.fb_in_psram = 1;

    esp_err_t error = esp_lcd_new_rgb_panel(&config, &rgbPanel);
    if (error != ESP_OK) {
        logError("create RGB panel", error);
        return false;
    }
    error = esp_lcd_panel_reset(rgbPanel);
    if (error == ESP_OK)
        error = esp_lcd_panel_init(rgbPanel);
    if (error == ESP_OK) {
        void *buffer = nullptr;
        error = esp_lcd_rgb_panel_get_frame_buffer(rgbPanel, 1, &buffer);
        framebuffer = static_cast<uint16_t *>(buffer);
        if (error == ESP_OK && framebuffer == nullptr)
            error = ESP_ERR_NO_MEM;
    }
    if (error != ESP_OK) {
        logError("initialize RGB framebuffer", error);
        esp_lcd_panel_del(rgbPanel);
        rgbPanel = nullptr;
        return false;
    }
    memset(framebuffer, 0, static_cast<size_t>(kWidth) * kHeight * sizeof(uint16_t));
    return true;
}

} // namespace

bool panel_hal_init() {
    if (!psramFound()) {
        Serial.println("[S3 UI] PSRAM unavailable; panel not started");
        return false;
    }
    if (!createRgbPanel())
        return false;

    drawBuffer = static_cast<lv_color_t *>(
        heap_caps_malloc(static_cast<size_t>(kWidth) * kDrawRows * sizeof(lv_color_t),
                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (drawBuffer == nullptr) {
        Serial.println("[S3 UI] LVGL draw buffer allocation failed");
        esp_lcd_panel_del(rgbPanel);
        rgbPanel = nullptr;
        framebuffer = nullptr;
        return false;
    }
    lv_disp_draw_buf_init(&lvDrawBuffer, drawBuffer, nullptr, kWidth * kDrawRows);
    lv_disp_drv_init(&lvDisplayDriver);
    lvDisplayDriver.hor_res = kWidth;
    lvDisplayDriver.ver_res = kHeight;
    lvDisplayDriver.flush_cb = flushDisplay;
    lvDisplayDriver.draw_buf = &lvDrawBuffer;
    if (lv_disp_drv_register(&lvDisplayDriver) == nullptr) {
        Serial.println("[S3 UI] LVGL display registration failed");
        heap_caps_free(drawBuffer);
        drawBuffer = nullptr;
        esp_lcd_panel_del(rgbPanel);
        rgbPanel = nullptr;
        framebuffer = nullptr;
        return false;
    }

    resetTouch();
    wireReady = Wire.begin(41, 42, 100000);
    if (wireReady) {
        Wire.setTimeOut(25);
        detectTouch();
    }
    if (!wireReady || touchAddress == 0) {
        Serial.println("[S3 UI] GT911 unavailable; LCD UI remains active");
    }
    lv_indev_drv_init(&lvTouchDriver);
    lvTouchDriver.type = LV_INDEV_TYPE_POINTER;
    lvTouchDriver.read_cb = readTouch;
    lvTouchDriver.gesture_limit = ui_navigation::kGestureLimit;
    lvTouchDriver.gesture_min_velocity = ui_navigation::kGestureMinVelocity;
    lv_indev_drv_register(&lvTouchDriver);
    Serial.printf("[S3 UI] RGB 800x480 ready; free PSRAM %u bytes\n",
                  static_cast<unsigned>(ESP.getFreePsram()));
    return true;
}

void panel_hal_service() {
    if (wireReady && touchAddress == 0 &&
        static_cast<uint32_t>(millis() - lastTouchRetryMs) >= kTouchRetryIntervalMs) {
        lastTouchRetryMs = millis();
        detectTouch();
    }
}

bool panel_hal_touch_ready() { return touchAddress != 0; }

uint32_t panel_hal_touch_count() { return touchCount; }

bool panel_hal_begin_flash_write() {
    if (!rgbPanel)
        return true;
    const esp_err_t result = esp_lcd_panel_del(rgbPanel);
    if (result != ESP_OK) {
        logError("stop RGB before NVS", result);
        return false;
    }
    rgbPanel = nullptr;
    framebuffer = nullptr;
    return true;
}
bool panel_hal_end_flash_write() {
    if (!rgbPanel && !createRgbPanel())
        return false;
    if (lv_disp_get_default())
        lv_obj_invalidate(lv_scr_act());
    return true;
}
