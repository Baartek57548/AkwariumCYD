#define LGFX_USE_V1

#include <LovyanGFX.hpp>
#include <SD.h>
#include <esp_attr.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>

#include "config.h"
#include "hal_display.h"
#include "hal_sd.h"

namespace {

#if defined(CYD_PANEL_ST7789) && CYD_PANEL_ST7789
using CydPanel = lgfx::Panel_ST7789;
constexpr bool PANEL_INVERT_COLORS = true;
#else
using CydPanel = lgfx::Panel_ILI9341;
constexpr bool PANEL_INVERT_COLORS = false;
#endif

class LGFX final : public lgfx::LGFX_Device {
public:
    LGFX() {
        {
            auto cfg = bus_.config();
            cfg.spi_host = SPI2_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = HwConfig::Display::SPI_WRITE_FREQUENCY_HZ;
            cfg.freq_read = HwConfig::Display::SPI_READ_FREQUENCY_HZ;
            cfg.pin_mosi = HwConfig::Display::MOSI_PIN;
            cfg.pin_miso = HwConfig::Display::MISO_PIN;
            cfg.pin_sclk = HwConfig::Display::SCLK_PIN;
            cfg.pin_dc = HwConfig::Display::DC_PIN;
            bus_.config(cfg);
            panel_.setBus(&bus_);
        }

        {
            auto cfg = panel_.config();
            cfg.pin_cs = HwConfig::Display::CS_PIN;
            cfg.pin_rst = HwConfig::Display::RESET_PIN;
            cfg.pin_busy = HwConfig::Display::BUSY_PIN;
            cfg.memory_width = HwConfig::Display::PANEL_WIDTH;
            cfg.memory_height = HwConfig::Display::PANEL_HEIGHT;
            cfg.panel_width = HwConfig::Display::PANEL_WIDTH;
            cfg.panel_height = HwConfig::Display::PANEL_HEIGHT;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.readable = true;
            cfg.invert = PANEL_INVERT_COLORS;
            cfg.rgb_order = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = false;
            panel_.config(cfg);
        }

        {
            auto cfg = touch_.config();
            cfg.spi_host = HwConfig::Touch::SPI_HOST;
            cfg.pin_cs = HwConfig::Touch::CS_PIN;
            cfg.pin_sclk = HwConfig::Touch::SCLK_PIN;
            cfg.pin_mosi = HwConfig::Touch::MOSI_PIN;
            cfg.pin_miso = HwConfig::Touch::MISO_PIN;
            cfg.pin_int = HwConfig::Touch::IRQ_PIN;
            cfg.x_min = HwConfig::Touch::X_MIN;
            cfg.x_max = HwConfig::Touch::X_MAX;
            cfg.y_min = HwConfig::Touch::Y_MIN;
            cfg.y_max = HwConfig::Touch::Y_MAX;
            cfg.freq = HwConfig::Touch::SPI_FREQUENCY_HZ;
            cfg.bus_shared = false;
            touch_.config(cfg);
            panel_.setTouch(&touch_);
        }

        setPanel(&panel_);
    }

private:
    CydPanel panel_;
    lgfx::Bus_SPI bus_;
    lgfx::Touch_XPT2046 touch_;
};

enum class TouchPhase : uint8_t {
    RELEASED,
    PRESS_DEBOUNCE,
    PRESSED,
    RELEASE_DEBOUNCE
};

struct TouchFilterState {
    TouchPhase phase = TouchPhase::RELEASED;
    uint32_t phase_started_ms = 0U;
    int16_t x = 0;
    int16_t y = 0;
};

static_assert(LV_COLOR_DEPTH == 16, "The CYD display HAL requires LVGL RGB565.");
static_assert(HwConfig::Display::LVGL_DRAW_BUFFER_LINES > 0U,
              "The LVGL draw buffer must contain at least one line.");

constexpr size_t LVGL_DRAW_BUFFER_PIXELS =
    static_cast<size_t>(HwConfig::Display::WIDTH) *
    HwConfig::Display::LVGL_DRAW_BUFFER_LINES;
constexpr size_t LVGL_TOTAL_DRAW_BUFFER_BYTES =
    2U * LVGL_DRAW_BUFFER_PIXELS * sizeof(lv_color_t);
constexpr size_t EXPECTED_LVGL_DRAW_BUFFER_BYTES =
    static_cast<size_t>(HwConfig::Display::WIDTH) * 20U * sizeof(lv_color_t);

static_assert(LVGL_TOTAL_DRAW_BUFFER_BYTES == EXPECTED_LVGL_DRAW_BUFFER_BYTES,
              "Two 10-line buffers must preserve the original 12.8 KB SRAM budget.");

LGFX lcd;
lv_disp_draw_buf_t draw_buffer_descriptor;
DMA_ATTR lv_color_t draw_buffer_a[LVGL_DRAW_BUFFER_PIXELS];
DMA_ATTR lv_color_t draw_buffer_b[LVGL_DRAW_BUFFER_PIXELS];
DMA_ATTR uint16_t rgb565_stream_buffer[
    static_cast<size_t>(HwConfig::Display::WIDTH) *
    HwConfig::Display::RGB565_STREAM_ROWS];
lv_disp_drv_t display_driver;
lv_indev_drv_t input_driver;
lv_disp_drv_t *pending_flush_driver = nullptr;

uint8_t display_brightness_percent = 100U;
uint32_t display_last_touch_ms = 0U;
bool display_consume_touch_until_release = false;
bool display_wake_release_pending = false;
uint32_t display_wake_release_started_ms = 0U;
TouchFilterState touch_filter;

int16_t clamp_coordinate(int32_t value, int16_t maximum) {
    if (value < 0) {
        return 0;
    }
    if (value > maximum) {
        return maximum;
    }
    return static_cast<int16_t>(value);
}

void reset_touch_filter() {
    touch_filter.phase = TouchPhase::RELEASED;
    touch_filter.phase_started_ms = 0U;
}

void map_touch_coordinates(uint16_t raw_x,
                           uint16_t raw_y,
                           int16_t &mapped_x,
                           int16_t &mapped_y) {
    mapped_x = clamp_coordinate(
        static_cast<int32_t>(raw_x),
        static_cast<int16_t>(HwConfig::Display::WIDTH - 1U));

    const int32_t y = HwConfig::Touch::INVERT_Y
        ? static_cast<int32_t>(HwConfig::Display::HEIGHT - 1U) -
              static_cast<int32_t>(raw_y)
        : static_cast<int32_t>(raw_y);
    mapped_y = clamp_coordinate(
        y,
        static_cast<int16_t>(HwConfig::Display::HEIGHT - 1U));
}

int16_t stabilize_axis(int16_t stable_value, int16_t sample_value) {
    const int16_t delta = static_cast<int16_t>(sample_value - stable_value);
    const int16_t absolute_delta = delta < 0
        ? static_cast<int16_t>(-delta)
        : delta;

    if (absolute_delta <= HwConfig::Touch::POSITION_HYSTERESIS_PX) {
        return stable_value;
    }
    if (absolute_delta >= HwConfig::Touch::POSITION_SNAP_THRESHOLD_PX) {
        return sample_value;
    }
    return static_cast<int16_t>(
        static_cast<int32_t>(stable_value) + static_cast<int32_t>(delta) / 2);
}

void stabilize_touch_position(int16_t sample_x, int16_t sample_y) {
    touch_filter.x = stabilize_axis(touch_filter.x, sample_x);
    touch_filter.y = stabilize_axis(touch_filter.y, sample_y);
}

bool consume_wake_touch(bool raw_pressed, uint32_t now_ms) {
    if (display_consume_touch_until_release) {
        if (raw_pressed) {
            display_wake_release_pending = false;
        } else if (!display_wake_release_pending) {
            display_wake_release_pending = true;
            display_wake_release_started_ms = now_ms;
        } else if (static_cast<uint32_t>(
                       now_ms - display_wake_release_started_ms) >=
                   HwConfig::Touch::WAKE_RELEASE_DEBOUNCE_MS) {
            display_consume_touch_until_release = false;
            display_wake_release_pending = false;
            reset_touch_filter();
        }
        return true;
    }

    if (raw_pressed && display_brightness_percent == 0U) {
        display_consume_touch_until_release = true;
        display_wake_release_pending = false;
        reset_touch_filter();
        return true;
    }

    return false;
}

void log_touch_sample(uint16_t raw_x,
                      uint16_t raw_y,
                      int16_t mapped_x,
                      int16_t mapped_y,
                      uint32_t now_ms) {
    if (!HwConfig::Touch::DEBUG_LOGGING) {
        return;
    }

    static uint32_t last_log_ms = 0U;
    if (static_cast<uint32_t>(now_ms - last_log_ms) < 100U) {
        return;
    }

    Serial.printf("TOUCH: RawX=%u, RawY=%u | MappedX=%d, MappedY=%d\n",
                  static_cast<unsigned>(raw_x),
                  static_cast<unsigned>(raw_y),
                  mapped_x,
                  mapped_y);
    last_log_ms = now_ms;
}

bool read_filtered_touch(int16_t &x, int16_t &y) {
    uint16_t raw_x = 0U;
    uint16_t raw_y = 0U;
    const bool raw_pressed = lcd.getTouch(&raw_x, &raw_y);
    const uint32_t now_ms = millis();

    if (raw_pressed) {
        display_last_touch_ms = now_ms;
    }

    if (consume_wake_touch(raw_pressed, now_ms)) {
        return false;
    }

    int16_t sample_x = touch_filter.x;
    int16_t sample_y = touch_filter.y;
    if (raw_pressed) {
        map_touch_coordinates(raw_x, raw_y, sample_x, sample_y);
        log_touch_sample(raw_x, raw_y, sample_x, sample_y, now_ms);
    }

    switch (touch_filter.phase) {
        case TouchPhase::RELEASED:
            if (raw_pressed) {
                touch_filter.phase = TouchPhase::PRESS_DEBOUNCE;
                touch_filter.phase_started_ms = now_ms;
                touch_filter.x = sample_x;
                touch_filter.y = sample_y;
            }
            return false;

        case TouchPhase::PRESS_DEBOUNCE:
            if (!raw_pressed) {
                reset_touch_filter();
                return false;
            }
            stabilize_touch_position(sample_x, sample_y);
            if (static_cast<uint32_t>(now_ms - touch_filter.phase_started_ms) <
                HwConfig::Touch::PRESS_DEBOUNCE_MS) {
                return false;
            }
            touch_filter.phase = TouchPhase::PRESSED;
            break;

        case TouchPhase::PRESSED:
            if (!raw_pressed) {
                touch_filter.phase = TouchPhase::RELEASE_DEBOUNCE;
                touch_filter.phase_started_ms = now_ms;
            } else {
                stabilize_touch_position(sample_x, sample_y);
            }
            break;

        case TouchPhase::RELEASE_DEBOUNCE:
            if (raw_pressed) {
                touch_filter.phase = TouchPhase::PRESSED;
                stabilize_touch_position(sample_x, sample_y);
            } else if (static_cast<uint32_t>(
                           now_ms - touch_filter.phase_started_ms) >=
                       HwConfig::Touch::RELEASE_DEBOUNCE_MS) {
                reset_touch_filter();
                return false;
            }
            break;
    }

    x = touch_filter.x;
    y = touch_filter.y;
    return true;
}

void complete_pending_lvgl_flush(bool wait_for_dma) {
    if (pending_flush_driver == nullptr) {
        return;
    }
    if (!wait_for_dma && lcd.dmaBusy()) {
        return;
    }

    lcd.waitDMA();
    lcd.endWrite();

    lv_disp_drv_t *completed_driver = pending_flush_driver;
    pending_flush_driver = nullptr;
    lv_disp_flush_ready(completed_driver);
}

void drain_pending_lvgl_flushes() {
    while (pending_flush_driver != nullptr) {
        complete_pending_lvgl_flush(true);
    }
}

void display_flush_callback(lv_disp_drv_t *driver,
                            const lv_area_t *area,
                            lv_color_t *pixels) {
    if (driver == nullptr || area == nullptr || pixels == nullptr) {
        if (driver != nullptr) {
            lv_disp_flush_ready(driver);
        }
        return;
    }

    if (pending_flush_driver != nullptr) {
        complete_pending_lvgl_flush(true);
    }

    const int32_t width = area->x2 - area->x1 + 1;
    const int32_t height = area->y2 - area->y1 + 1;
    if (width <= 0 || height <= 0) {
        lv_disp_flush_ready(driver);
        return;
    }

    lcd.startWrite();
    lcd.setAddrWindow(area->x1, area->y1, width, height);
    pending_flush_driver = driver;
    lcd.writePixelsDMA(
        reinterpret_cast<const uint16_t *>(pixels),
        width * height,
        LV_COLOR_16_SWAP == 0);
}

void touch_read_callback(lv_indev_drv_t *driver, lv_indev_data_t *data) {
    (void)driver;
    if (data == nullptr) {
        return;
    }

    int16_t x = 0;
    int16_t y = 0;
    if (!read_filtered_touch(x, y)) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }

    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PR;
}

void wait_for_frame_deadline(uint32_t frame_started_ms,
                             uint32_t frame_duration_ms) {
    const uint32_t elapsed_ms =
        static_cast<uint32_t>(millis() - frame_started_ms);
    if (elapsed_ms >= frame_duration_ms) {
        taskYIELD();
        return;
    }

    const uint32_t remaining_ms = frame_duration_ms - elapsed_ms;
    const TickType_t ticks = static_cast<TickType_t>(
        (remaining_ms + portTICK_PERIOD_MS - 1U) / portTICK_PERIOD_MS);
    vTaskDelay(ticks > 0 ? ticks : 1);
}

} // namespace

void hal_display_init(void) {
    pinMode(HwConfig::Backlight::PIN, OUTPUT);
    digitalWrite(HwConfig::Backlight::PIN, HIGH);

    lcd.init();
    lcd.initDMA();
    lcd.setRotation(HwConfig::Display::ROTATION);
    lcd.fillScreen(TFT_BLACK);

    lv_disp_draw_buf_init(&draw_buffer_descriptor,
                          draw_buffer_a,
                          draw_buffer_b,
                          LVGL_DRAW_BUFFER_PIXELS);

    lv_disp_drv_init(&display_driver);
    display_driver.hor_res = HwConfig::Display::WIDTH;
    display_driver.ver_res = HwConfig::Display::HEIGHT;
    display_driver.flush_cb = display_flush_callback;
    display_driver.draw_buf = &draw_buffer_descriptor;
    lv_disp_drv_register(&display_driver);

    lv_indev_drv_init(&input_driver);
    input_driver.type = LV_INDEV_TYPE_POINTER;
    input_driver.read_cb = touch_read_callback;
    lv_indev_drv_register(&input_driver);

    ledcSetup(HwConfig::Backlight::LEDC_CHANNEL,
              HwConfig::Backlight::PWM_FREQUENCY_HZ,
              HwConfig::Backlight::PWM_RESOLUTION_BITS);
    ledcAttachPin(HwConfig::Backlight::PIN,
                  HwConfig::Backlight::LEDC_CHANNEL);
    hal_display_set_brightness(display_brightness_percent);

    Serial.printf("HAL DISPLAY: two DMA buffers, %u lines each, %u bytes total; panel=%s.\n",
                  static_cast<unsigned>(
                      HwConfig::Display::LVGL_DRAW_BUFFER_LINES),
                  static_cast<unsigned>(LVGL_TOTAL_DRAW_BUFFER_BYTES),
#if defined(CYD_PANEL_ST7789) && CYD_PANEL_ST7789
                  "ST7789"
#else
                  "ILI9341"
#endif
    );
}

void hal_display_set_brightness(uint8_t percent) {
    display_brightness_percent = percent > 100U ? 100U : percent;
    const uint32_t duty =
        (static_cast<uint32_t>(display_brightness_percent) *
             HwConfig::Backlight::PWM_MAX_DUTY +
         50U) /
        100U;
    ledcWrite(HwConfig::Backlight::LEDC_CHANNEL, duty);
}

uint8_t hal_display_get_brightness(void) {
    return display_brightness_percent;
}

uint32_t hal_display_last_touch_ms(void) {
    return display_last_touch_ms;
}

bool hal_display_draw_rgb565_file(const char *path,
                                  uint16_t width,
                                  uint16_t height) {
    if (path == nullptr || path[0] == '\0' || width == 0U || height == 0U) {
        Serial.println("HAL DISPLAY: invalid RGB565 draw request.");
        return false;
    }
    if (width > HwConfig::Display::WIDTH ||
        height > HwConfig::Display::HEIGHT) {
        Serial.printf("HAL DISPLAY: RGB565 dimensions %ux%u exceed panel %ux%u.\n",
                      static_cast<unsigned>(width),
                      static_cast<unsigned>(height),
                      static_cast<unsigned>(HwConfig::Display::WIDTH),
                      static_cast<unsigned>(HwConfig::Display::HEIGHT));
        return false;
    }
    if (!hal_sd_is_mounted()) {
        Serial.println("HAL DISPLAY: SD is not mounted; RGB565 draw skipped.");
        return false;
    }

    File file = SD.open(path, FILE_READ);
    if (!file) {
        Serial.printf("HAL DISPLAY: missing RGB565 file: %s\n", path);
        return false;
    }

    const size_t expected_size =
        static_cast<size_t>(width) * height * sizeof(uint16_t);
    if (file.size() != expected_size) {
        Serial.printf(
            "HAL DISPLAY: RGB565 size mismatch for %s: got %u, expected %u\n",
            path,
            static_cast<unsigned>(file.size()),
            static_cast<unsigned>(expected_size));
        file.close();
        return false;
    }

    const size_t row_bytes = static_cast<size_t>(width) * sizeof(uint16_t);
    drain_pending_lvgl_flushes();

    bool ok = true;
    lcd.startWrite();
    for (uint16_t y = 0U; y < height;
         y = static_cast<uint16_t>(
             y + HwConfig::Display::RGB565_STREAM_ROWS)) {
        const uint16_t remaining_rows = static_cast<uint16_t>(height - y);
        const uint16_t rows =
            remaining_rows < HwConfig::Display::RGB565_STREAM_ROWS
                ? remaining_rows
                : HwConfig::Display::RGB565_STREAM_ROWS;
        const size_t bytes_to_read = row_bytes * rows;
        const size_t bytes_read =
            file.read(
                reinterpret_cast<uint8_t *>(rgb565_stream_buffer),
                bytes_to_read);
        if (bytes_read != bytes_to_read) {
            Serial.printf(
                "HAL DISPLAY: short read in %s at y=%u: got %u, expected %u\n",
                path,
                static_cast<unsigned>(y),
                static_cast<unsigned>(bytes_read),
                static_cast<unsigned>(bytes_to_read));
            ok = false;
            break;
        }

        lcd.setAddrWindow(0, y, width, rows);
        lcd.writePixels(rgb565_stream_buffer,
                        static_cast<uint32_t>(width) * rows,
                        true);
    }
    lcd.endWrite();

    file.close();
    return ok;
}

bool hal_display_play_rgb565_sequence(const char *frame_pattern,
                                      uint16_t frame_count,
                                      uint16_t width,
                                      uint16_t height,
                                      uint16_t fps) {
    if (frame_pattern == nullptr ||
        frame_pattern[0] == '\0' ||
        frame_count == 0U ||
        fps == 0U) {
        Serial.println("HAL DISPLAY: invalid RGB565 animation request.");
        return false;
    }

    const uint32_t frame_duration_ms = 1000UL / fps;
    char frame_path[128];
    bool played_any = false;

    for (uint16_t index = 0U; index < frame_count; ++index) {
        const int written = snprintf(frame_path,
                                     sizeof(frame_path),
                                     frame_pattern,
                                     static_cast<unsigned>(index));
        if (written <= 0 ||
            written >= static_cast<int>(sizeof(frame_path))) {
            Serial.println("HAL DISPLAY: RGB565 frame path is too long.");
            return false;
        }

        const uint32_t frame_started_ms = millis();
        if (!hal_display_draw_rgb565_file(frame_path, width, height)) {
            Serial.printf(
                "HAL DISPLAY: animation stopped at frame %u.\n",
                static_cast<unsigned>(index));
            return false;
        }
        played_any = true;
        wait_for_frame_deadline(frame_started_ms, frame_duration_ms);
    }

    return played_any;
}

bool hal_display_get_touch(int16_t *x, int16_t *y) {
    if (x == nullptr || y == nullptr) {
        Serial.println("HAL DISPLAY: touch output pointer is null.");
        return false;
    }

    int16_t filtered_x = 0;
    int16_t filtered_y = 0;
    if (!read_filtered_touch(filtered_x, filtered_y)) {
        return false;
    }

    *x = filtered_x;
    *y = filtered_y;
    return true;
}

void hal_display_loop_cb(void) {
    complete_pending_lvgl_flush(false);
}
