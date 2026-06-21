#define USE_ST7789_DRIVER 1 // Zmień na 1, jeśli ekran po wgraniu nadal będzie rozjechany (warianty CYD ze sterownikiem ST7789)

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lvgl.h>
#include <esp_heap_caps.h>
#include <SD.h>
#include "hal_display.h"
#include "hal_sd.h"

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
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;
constexpr uint16_t LVGL_DRAW_BUFFER_LINES = 20;
constexpr uint16_t RGB565_STREAM_ROWS = 8;
// Callback przesyłania pikseli do wyświetlacza dla LVGL (bezpośredni, stabilny zapis)
static void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    
    lcd.startWrite();
    lcd.setAddrWindow(area->x1, area->y1, w, h);
    lcd.writePixels((uint16_t*)&color_p->full, w * h);
    lcd.endWrite();
    
    lv_disp_flush_ready(disp);
}

// Callback odczytu panelu dotykowego dla LVGL
static void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
    uint16_t touchX, touchY;
    bool touched = lcd.getTouch(&touchX, &touchY);
    
    if (!touched) {
        data->state = LV_INDEV_STATE_REL;
    } else {
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

        // Diagnostyczny log szeregowy (nie częściej niż co 100ms)
        static unsigned long last_log = 0;
        if (millis() - last_log > 100) {
            Serial.printf("TOUCH: RawX=%d, RawY=%d | MappedX=%d, MappedY=%d\n", 
                          touchX, touchY, data->point.x, data->point.y);
            last_log = millis();
        }
    }
}

void hal_display_init(void) {
    // Inicjalizacja podświetlenia ekranu (GPIO 21 na CYD)
    pinMode(21, OUTPUT);
    digitalWrite(21, HIGH); // Włącz podświetlenie

    // Inicjalizacja LovyanGFX
    lcd.init();
    lcd.initDMA();                // Initialize Direct Memory Access for SPI display writes
    lcd.invertDisplay(true); // Wymuszenie inwersji kolorów w celu poprawnego wyświetlania motywu ciemnego
    lcd.setRotation(3); // Obrót o 180 stopni (odwrócona pozioma landscape, 320x240)
    lcd.fillScreen(TFT_BLACK);

    // Dynamiczna alokacja buforów podwójnego buforowania w pamięci DMA
    // Single DMA-capable buffering saves SRAM for LVGL objects and SD streaming.
    buf1 = (lv_color_t *)heap_caps_malloc(320 * LVGL_DRAW_BUFFER_LINES * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (buf1 == nullptr) {
        Serial.println("HAL ERROR: DMA buffer allocation failed!");
        while (1) { delay(1000); }
    }
    Serial.printf("HAL: Single LVGL draw buffer (%u lines) allocated in DMA-capable SRAM.\n",
                  static_cast<unsigned>(LVGL_DRAW_BUFFER_LINES));

    // LVGL renders through a small partial buffer to preserve internal SRAM.
    lv_disp_draw_buf_init(&draw_buf, buf1, nullptr, 320 * LVGL_DRAW_BUFFER_LINES);

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
}

bool hal_display_draw_rgb565_file(const char *path, uint16_t width, uint16_t height) {
    if (path == nullptr || path[0] == '\0' || width == 0 || height == 0) {
        Serial.println("HAL DISPLAY: invalid RGB565 draw request.");
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

    const size_t expected_size = static_cast<size_t>(width) * static_cast<size_t>(height) * sizeof(uint16_t);
    if (file.size() != expected_size) {
        Serial.printf("HAL DISPLAY: RGB565 size mismatch for %s: got %u, expected %u\n",
                      path,
                      static_cast<unsigned>(file.size()),
                      static_cast<unsigned>(expected_size));
        file.close();
        return false;
    }

    const size_t row_bytes = static_cast<size_t>(width) * sizeof(uint16_t);
    uint16_t *line_buffer = static_cast<uint16_t *>(heap_caps_malloc(row_bytes * RGB565_STREAM_ROWS,
                                                                     MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (line_buffer == nullptr) {
        Serial.println("HAL DISPLAY: RGB565 stream buffer allocation failed.");
        file.close();
        return false;
    }

    bool ok = true;
    lcd.startWrite();
    for (uint16_t y = 0; y < height; y = static_cast<uint16_t>(y + RGB565_STREAM_ROWS)) {
        const uint16_t remaining_rows = static_cast<uint16_t>(height - y);
        const uint16_t rows = remaining_rows < RGB565_STREAM_ROWS ? remaining_rows : RGB565_STREAM_ROWS;
        const size_t bytes_to_read = row_bytes * rows;
        const size_t bytes_read = file.read(reinterpret_cast<uint8_t *>(line_buffer), bytes_to_read);
        if (bytes_read != bytes_to_read) {
            Serial.printf("HAL DISPLAY: short read in %s at y=%u: got %u, expected %u\n",
                          path,
                          static_cast<unsigned>(y),
                          static_cast<unsigned>(bytes_read),
                          static_cast<unsigned>(bytes_to_read));
            ok = false;
            break;
        }
        lcd.setAddrWindow(0, y, width, rows);
        lcd.writePixels(line_buffer, static_cast<uint32_t>(width) * rows, true);
    }
    lcd.endWrite();

    heap_caps_free(line_buffer);
    file.close();
    return ok;
}

bool hal_display_play_rgb565_sequence(const char *frame_pattern,
                                      uint16_t frame_count,
                                      uint16_t width,
                                      uint16_t height,
                                      uint16_t fps) {
    if (frame_pattern == nullptr || frame_pattern[0] == '\0' || frame_count == 0 || fps == 0) {
        Serial.println("HAL DISPLAY: invalid RGB565 animation request.");
        return false;
    }

    const uint32_t frame_delay_ms = 1000UL / fps;
    char frame_path[128];
    bool played_any = false;

    for (uint16_t i = 0; i < frame_count; ++i) {
        const int written = snprintf(frame_path, sizeof(frame_path), frame_pattern, static_cast<unsigned>(i));
        if (written <= 0 || written >= static_cast<int>(sizeof(frame_path))) {
            Serial.println("HAL DISPLAY: RGB565 frame path is too long.");
            return false;
        }

        const uint32_t frame_start = millis();
        if (!hal_display_draw_rgb565_file(frame_path, width, height)) {
            Serial.printf("HAL DISPLAY: animation stopped at frame %u.\n", static_cast<unsigned>(i));
            return false;
        }
        played_any = true;

        const uint32_t elapsed = millis() - frame_start;
        if (elapsed < frame_delay_ms) {
            delay(frame_delay_ms - elapsed);
        }
        yield();
    }

    return played_any;
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
        
        *x = mappedX;
        *y = mappedY;
    }
    return touched;
}

void hal_display_loop_cb(void) {
    // Pusta pętla - spłukiwanie odbywa się synchronicznie i niezawodnie
}
