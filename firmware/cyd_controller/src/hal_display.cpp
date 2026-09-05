#define USE_ST7789_DRIVER 1 // Warianty CYD ze sterownikiem ST7789 (zgodne z cydAquarium backup)

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <SD.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>

#include "config.h"
#include "hal_display.h"
#include "hal_sd.h"
#include "runtime_safety.h"

// Definicja niestandardowej klasy LovyanGFX dla płytek Cheap Yellow Display (ESP32-2432S028R)
class LGFX : public lgfx::LGFX_Device {
#if USE_ST7789_DRIVER
    lgfx::Panel_ST7789   _panel_instance;
#else
    lgfx::Panel_ILI9341  _panel_instance;
#endif
    lgfx::Bus_SPI       _bus_instance;
    lgfx::Touch_XPT2046 _touch_instance;

public:
    LGFX(void) {
        // Konfiguracja magistrali SPI dla wyświetlacza (HSPI / SPI2)
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host = SPI2_HOST;     // Kontroler HSPI
            cfg.spi_mode = 0;
            cfg.freq_write = 40000000;    // High performance 40 MHz SPI write speed
            cfg.freq_read  = 16000000;    // 16 MHz dla odczytu
            cfg.pin_mosi = 13;            // Pin MOSI wyświetlacza na CYD
            cfg.pin_miso = 12;            // Pin MISO wyświetlacza na CYD
            cfg.pin_sclk = 14;            // Pin SCK wyświetlacza na CYD
            cfg.pin_dc   = 2;             // Pin Data/Command wyświetlacza na CYD
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }

        // Konfiguracja panelu LCD (ILI9341 lub ST7789)
        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs = 15;              // Chip Select wyświetlacza (GPIO 15)
            cfg.pin_rst = -1;             // Pin RESET (podłączony do pinu EN)
            cfg.pin_busy = -1;
            cfg.memory_width = 240;
            cfg.memory_height = 320;
            cfg.panel_width = 240;
            cfg.panel_height = 320;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.readable = true;
            
#if USE_ST7789_DRIVER
            cfg.invert = true;            // Ustawiono na true, aby poprawnie wyświetlić motyw ciemny na ST7789 (zapobiega inwersji kolorów)
#else
            cfg.invert = false;
#endif
            cfg.rgb_order = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = false;       // Wyłączona współdzielona magistrala
            _panel_instance.config(cfg);
        }

        // Konfiguracja panelu dotykowego XPT2046 (Programowe SPI - Bit-bang)
        {
            auto cfg = _touch_instance.config();
            cfg.spi_host = -1;            // Włączenie trybu bit-bang w LovyanGFX
            cfg.pin_cs = 33;              // Chip Select panelu dotykowego na CYD (GPIO 33)
            cfg.pin_sclk = 25;            // Clock panelu dotykowego (GPIO 25)
            cfg.pin_mosi = 32;            // MOSI panelu dotykowego (GPIO 32)
            cfg.pin_miso = 39;            // MISO panelu dotykowego (GPIO 39)
            cfg.pin_int = 36;             // Pin przerwania IRQ panelu dotykowego (GPIO 36)
            
            // Domyślne wartości kalibracji dla CYD 2.8"
            cfg.x_min = 300;
            cfg.x_max = 3900;
            cfg.y_min = 200;
            cfg.y_max = 3700;
            cfg.freq = 1000000;           // Bezpieczna częstotliwość dla programowego SPI dotyku
            cfg.bus_shared = false;       // Osobne piny fizyczne
            _touch_instance.config(cfg);
            _panel_instance.setTouch(&_touch_instance);
        }
        setPanel(&_panel_instance);
    }
};

static LGFX lcd;

// Zmienne sterowników LVGL
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1 = nullptr;
static lv_color_t *buf2 = nullptr;
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;

static uint32_t last_touch_timestamp = 0;
static uint8_t display_brightness_percent = 100;
static uint8_t configured_active_brightness = 100;

DMA_ATTR uint16_t rgb565_stream_buffer[
    static_cast<size_t>(HwConfig::Display::WIDTH) *
    HwConfig::Display::RGB565_STREAM_ROWS];

// Callback przesyłania pikseli do wyświetlacza dla LVGL (bezpośredni, stabilny zapis z cydAquarium backup)
static void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    
    lcd.startWrite();
    lcd.setAddrWindow(area->x1, area->y1, w, h);
    lcd.writePixels((uint16_t*)&color_p->full, w * h);
    lcd.endWrite();
    
    lv_disp_flush_ready(disp);
}

// Callback odczytu panelu dotykowego dla LVGL (dokładny algorytm z cydAquarium backup)
static void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
    (void)indev_driver;
    uint16_t touchX, touchY;
    bool touched = lcd.getTouch(&touchX, &touchY);
    static bool waking_from_blank = false;
    
    if (!touched) {
        data->state = LV_INDEV_STATE_REL;
        waking_from_blank = false;
    } else {
        // Jeśli ekran był wygaszony (jasność 0), pierwsze dotknięcie wybudza ekran,
        // ale ignorujemy wciśnięcie w LVGL, aby nie kliknąć w ciemno przypadkowego przycisku.
        if (display_brightness_percent == 0 || waking_from_blank) {
            waking_from_blank = true;
            hal_display_set_brightness(configured_active_brightness > 0 ? configured_active_brightness : 50);
            last_touch_timestamp = millis();
            data->state = LV_INDEV_STATE_REL;
            return;
        }

        data->state = LV_INDEV_STATE_PR;
        
        // LovyanGFX automatycznie skaluje i rotuje współrzędne, ale oś Y jest odwrócona fizycznie
        int16_t mappedX = touchX;
        int16_t mappedY = 239 - touchY;
        
        // Zabezpieczenie zakresu współrzędnych
        if (mappedX < 0) mappedX = 0;
        if (mappedX >= 320) mappedX = 319;
        if (mappedY < 0) mappedY = 0;
        if (mappedY >= 240) mappedY = 239;
        
        data->point.x = mappedX;
        data->point.y = mappedY;
        last_touch_timestamp = millis();

#if defined(CORE_DEBUG_LEVEL) && (CORE_DEBUG_LEVEL >= 4)
        // Diagnostyczny log szeregowy (tylko w trybie verbose debug)
        static unsigned long last_log = 0;
        if (millis() - last_log > 100) {
            Serial.printf("TOUCH: RawX=%d, RawY=%d | MappedX=%d, MappedY=%d\n", 
                          touchX, touchY, data->point.x, data->point.y);
            last_log = millis();
        }
#endif
    }
}

void hal_display_init(void) {
    // Inicjalizacja podświetlenia ekranu (GPIO 21 na CYD ze sprzętowym PWM LEDC)
    ledcSetup(HwConfig::Backlight::LEDC_CHANNEL,
              HwConfig::Backlight::PWM_FREQUENCY_HZ,
              HwConfig::Backlight::PWM_RESOLUTION_BITS);
    ledcAttachPin(HwConfig::Backlight::PIN, HwConfig::Backlight::LEDC_CHANNEL);
    hal_display_set_brightness(100);

    // Inicjalizacja LovyanGFX
    lcd.init();
    lcd.initDMA();                // Initialize Direct Memory Access for SPI display writes
    lcd.invertDisplay(true); // Wymuszenie inwersji kolorów w celu poprawnego wyświetlania motywu ciemnego
    lcd.setRotation(3); // Obrót o 180 stopni (odwrócona pozioma landscape, 320x240)
    lcd.fillScreen(TFT_BLACK);

    // Dynamiczna alokacja buforów podwójnego buforowania w pamięci DMA
    buf1 = (lv_color_t *)heap_caps_malloc(320 * 40 * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    buf2 = (lv_color_t *)heap_caps_malloc(320 * 40 * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (buf1 == nullptr || buf2 == nullptr) {
        Serial.println("HAL ERROR: DMA buffer allocation failed!");
        while (1) { delay(1000); }
    }
    Serial.println("HAL: Double buffers (40 lines) allocated in DMA-capable SRAM.");

    // Initialize LVGL draw buffer with double buffering
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, 320 * 40);

    // Inicjalizacja i rejestracja sterownika ekranu w LVGL
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 320;
    disp_drv.ver_res = 240;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    // Inicjalizacja i rejestracja sterownika dotyku w LVGL
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);

    Serial.println("HAL DISPLAY: panel ST7789 initialized (cydAquarium backup profile, rotation=3).");
}

bool hal_display_get_touch(int16_t *x, int16_t *y) {
    uint16_t tx, ty;
    bool touched = lcd.getTouch(&tx, &ty);
    if (touched) {
        int16_t mappedX = (int16_t)tx;
        int16_t mappedY = 239 - (int16_t)ty;
        
        if (mappedX < 0) mappedX = 0;
        if (mappedX >= 320) mappedX = 319;
        if (mappedY < 0) mappedY = 0;
        if (mappedY >= 240) mappedY = 239;
        
        if (x != nullptr) *x = mappedX;
        if (y != nullptr) *y = mappedY;
        last_touch_timestamp = millis();
    }
    return touched;
}

void hal_display_loop_cb(void) {
    // Pusta pętla - spłukiwanie odbywa się synchronicznie i niezawodnie (wzorzec z cydAquarium backup)
}

void hal_display_set_brightness(uint8_t percent) {
    display_brightness_percent = percent > 100U ? 100U : percent;
    if (display_brightness_percent > 0U) {
        configured_active_brightness = display_brightness_percent;
    }
    uint32_t duty = (static_cast<uint32_t>(display_brightness_percent) * HwConfig::Backlight::PWM_MAX_DUTY) / 100U;
    ledcWrite(HwConfig::Backlight::LEDC_CHANNEL, duty);
}

uint8_t hal_display_get_brightness(void) {
    return display_brightness_percent;
}

uint32_t hal_display_last_touch_ms(void) {
    return last_touch_timestamp;
}

bool hal_display_draw_rgb565_file(const char *path,
                                  uint16_t width,
                                  uint16_t height) {
    if (path == nullptr || path[0] == '\0' || width == 0U || height == 0U) {
        return false;
    }
    if (width > HwConfig::Display::WIDTH || height > HwConfig::Display::HEIGHT) {
        return false;
    }
    if (!hal_sd_is_mounted()) {
        return false;
    }

    File file = SD.open(path, FILE_READ);
    if (!file) {
        return false;
    }

    const size_t expected_size = static_cast<size_t>(width) * height * sizeof(uint16_t);
    if (file.size() != expected_size) {
        file.close();
        return false;
    }

    const size_t row_bytes = static_cast<size_t>(width) * sizeof(uint16_t);

    bool ok = true;
    lcd.startWrite();
    for (uint16_t y = 0U; y < height; y = static_cast<uint16_t>(y + HwConfig::Display::RGB565_STREAM_ROWS)) {
        const uint16_t remaining_rows = static_cast<uint16_t>(height - y);
        const uint16_t rows = remaining_rows < HwConfig::Display::RGB565_STREAM_ROWS
                                  ? remaining_rows
                                  : HwConfig::Display::RGB565_STREAM_ROWS;
        const size_t bytes_to_read = row_bytes * rows;
        const size_t bytes_read = file.read(
            reinterpret_cast<uint8_t *>(rgb565_stream_buffer),
            bytes_to_read);
        if (bytes_read != bytes_to_read) {
            ok = false;
            break;
        }

        lcd.setAddrWindow(0, y, width, rows);
        lcd.writePixels(rgb565_stream_buffer, static_cast<uint32_t>(width) * rows, true);
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
    if (frame_pattern == nullptr || frame_pattern[0] == '\0' || frame_count == 0U || fps == 0U) {
        return false;
    }

    const uint32_t frame_duration_ms = 1000U / fps;
    char frame_path[96];

    for (uint16_t frame = 0U; frame < frame_count; ++frame) {
        const uint32_t frame_started_ms = millis();
        runtime_safety_heartbeat(RuntimeSafetyTask::Ui, frame_started_ms, ESP.getFreeHeap());
        snprintf(frame_path, sizeof(frame_path), frame_pattern, static_cast<unsigned>(frame));

        if (!hal_display_draw_rgb565_file(frame_path, width, height)) {
            return false;
        }

        const uint32_t elapsed_ms = static_cast<uint32_t>(millis() - frame_started_ms);
        if (elapsed_ms < frame_duration_ms) {
            vTaskDelay(pdMS_TO_TICKS(frame_duration_ms - elapsed_ms));
        } else {
            taskYIELD();
        }
    }
    return true;
}
