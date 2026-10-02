#include <Arduino.h>
#include <Wire.h>

#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>

#include <cstddef>
#include <cstdint>

namespace {

constexpr int kWidth = 800;
constexpr int kHeight = 480;
constexpr size_t kPixelCount = static_cast<size_t>(kWidth) * kHeight;
constexpr size_t kFrameBytes = kPixelCount * sizeof(uint16_t);

constexpr int kPixelClock = 1;
constexpr int kHsync = 2;
constexpr int kVsync = 39;
constexpr int kDataEnable = 40;
constexpr int kTouchSda = 41;
constexpr int kTouchScl = 42;
constexpr int kTouchReset = 47;

// IDF expects D0..D15, not the physical order of pins on the Waveshare header.
constexpr int kRgb565Pins[16] = {
    15, 16, 17, 18, 21,  // B3..B7
    9, 10, 11, 12, 13, 14,  // G2..G7
    4, 5, 6, 7, 8  // R3..R7
};

constexpr uint16_t kColorBars[8] = {
    0xF800,  // red
    0x07E0,  // green
    0x001F,  // blue
    0xFFE0,  // yellow
    0x07FF,  // cyan
    0xF81F,  // magenta
    0xFFFF,  // white
    0x0000   // black
};

constexpr uint8_t kTouchAddresses[] = {0x5D, 0x14};
constexpr uint16_t kProductIdRegister = 0x8140;
constexpr uint16_t kTouchStatusRegister = 0x814E;
constexpr uint16_t kTouchPointRegister = 0x8150;
constexpr uint32_t kAnimationIntervalMs = 1000;
constexpr uint32_t kTouchPollIntervalMs = 30;
constexpr uint32_t kTouchLogIntervalMs = 100;
constexpr uint32_t kHeartbeatIntervalMs = 3000;

esp_lcd_panel_handle_t panel = nullptr;
uint16_t* frame = nullptr;
uint8_t touchAddress = 0;
bool touchBusReady = false;
uint32_t lastAnimationMs = 0;
uint32_t lastTouchPollMs = 0;
uint32_t lastTouchLogMs = 0;
uint32_t lastHeartbeatMs = 0;
uint32_t animationStep = 0;
int touchX = -1;
int touchY = -1;

void logLine(const char* message) {
  Serial.println(message);
  Serial0.println(message);
}

void printError(const char* operation, esp_err_t error) {
  char message[128] = {};
  snprintf(message, sizeof(message), "ERROR: %s: %s (%d)", operation,
           esp_err_to_name(error), static_cast<int>(error));
  logLine(message);
}

bool initPanel() {
  logLine("STAGE: checking PSRAM");
  if (!psramFound()) {
    logLine("ERROR: PSRAM is unavailable; RGB framebuffer was not started.");
    return false;
  }

  esp_lcd_rgb_panel_config_t config = {};
  config.clk_src = LCD_CLK_SRC_PLL160M;
  // These conservative values are a starting point for the bare Waveshare panel.
  // Physical testing may require porch and PCLK adjustment.
  config.timings.pclk_hz = 16000000;
  config.timings.h_res = kWidth;
  config.timings.v_res = kHeight;
  config.timings.hsync_pulse_width = 8;
  config.timings.hsync_back_porch = 40;
  config.timings.hsync_front_porch = 40;
  config.timings.vsync_pulse_width = 4;
  config.timings.vsync_back_porch = 16;
  config.timings.vsync_front_porch = 16;
  config.data_width = 16;
  config.sram_trans_align = 64;
  config.psram_trans_align = 64;
  config.hsync_gpio_num = kHsync;
  config.vsync_gpio_num = kVsync;
  config.de_gpio_num = kDataEnable;
  config.pclk_gpio_num = kPixelClock;
  for (size_t index = 0; index < 16; ++index) {
    config.data_gpio_nums[index] = kRgb565Pins[index];
  }
  config.disp_gpio_num = -1;  // DISP is pulled high on the panel; no ESP GPIO.
  config.flags.fb_in_psram = 1;

  logLine("STAGE: creating RGB panel");
  esp_err_t result = esp_lcd_new_rgb_panel(&config, &panel);
  if (result != ESP_OK) {
    printError("esp_lcd_new_rgb_panel", result);
    return false;
  }

  logLine("STAGE: resetting RGB peripheral");
  result = esp_lcd_panel_reset(panel);
  if (result == ESP_OK) {
    logLine("STAGE: starting RGB peripheral");
    result = esp_lcd_panel_init(panel);
  }
  if (result != ESP_OK) {
    printError("RGB panel initialization", result);
    esp_lcd_panel_del(panel);
    panel = nullptr;
    return false;
  }

  logLine("STAGE: allocating RGB565 drawing buffer");
  frame = static_cast<uint16_t*>(
      heap_caps_malloc(kFrameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (frame == nullptr) {
    Serial.printf("ERROR: cannot allocate %u-byte RGB565 drawing buffer.\n",
                  static_cast<unsigned>(kFrameBytes));
    esp_lcd_panel_del(panel);
    panel = nullptr;
    return false;
  }

  Serial.printf("RGB panel ready: %dx%d, 16 MHz PCLK, PSRAM free: %u bytes.\n",
                kWidth, kHeight, static_cast<unsigned>(ESP.getFreePsram()));
  return true;
}

void setPixel(int x, int y, uint16_t color) {
  if (x >= 0 && x < kWidth && y >= 0 && y < kHeight) {
    frame[static_cast<size_t>(y) * kWidth + x] = color;
  }
}

void fillFrame() {
  for (int y = 0; y < kHeight; ++y) {
    uint16_t* row = frame + static_cast<size_t>(y) * kWidth;
    for (int x = 0; x < kWidth; ++x) {
      row[x] = kColorBars[x / (kWidth / 8)];
    }
  }

  // A moving white/black stripe confirms that the image is being refreshed.
  const int stripeY = static_cast<int>((animationStep * 37U) % (kHeight - 24));
  for (int y = stripeY; y < stripeY + 8; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      setPixel(x, y, ((x / 20) & 1) == 0 ? 0xFFFF : 0x0000);
    }
  }

  if (touchX >= 0 && touchY >= 0) {
    // The black surround keeps the white touch marker visible on every bar.
    for (int offset = -17; offset <= 17; ++offset) {
      for (int thickness = -2; thickness <= 2; ++thickness) {
        setPixel(touchX + offset, touchY + thickness, 0x0000);
        setPixel(touchX + thickness, touchY + offset, 0x0000);
      }
    }
    for (int offset = -14; offset <= 14; ++offset) {
      setPixel(touchX + offset, touchY, 0xFFFF);
      setPixel(touchX, touchY + offset, 0xFFFF);
    }
  }
}

bool showFrame() {
  fillFrame();
  const esp_err_t result =
      esp_lcd_panel_draw_bitmap(panel, 0, 0, kWidth, kHeight, frame);
  if (result != ESP_OK) {
    printError("esp_lcd_panel_draw_bitmap", result);
    return false;
  }
  return true;
}

bool readRegister(uint8_t address, uint16_t reg, uint8_t* data, size_t size) {
  if (data == nullptr || size == 0 || size > 32) {
    return false;
  }

  Wire.beginTransmission(address);
  const bool addressWritten =
      Wire.write(static_cast<uint8_t>(reg >> 8)) == 1 &&
      Wire.write(static_cast<uint8_t>(reg)) == 1;
  if (!addressWritten) {
    Wire.endTransmission(true);
    return false;
  }
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  if (Wire.requestFrom(address, size, true) != size) {
    while (Wire.available() > 0) {
      Wire.read();
    }
    return false;
  }
  for (size_t index = 0; index < size; ++index) {
    const int value = Wire.read();
    if (value < 0) {
      return false;
    }
    data[index] = static_cast<uint8_t>(value);
  }
  return true;
}

bool writeRegister(uint8_t address, uint16_t reg, uint8_t value) {
  Wire.beginTransmission(address);
  if (Wire.write(static_cast<uint8_t>(reg >> 8)) != 1 ||
      Wire.write(static_cast<uint8_t>(reg)) != 1 ||
      Wire.write(value) != 1) {
    Wire.endTransmission(true);
    return false;
  }
  return Wire.endTransmission(true) == 0;
}

void initTouch() {
  logLine("STAGE: resetting GT911 via the upper-left RST on GPIO47");
  pinMode(kTouchReset, OUTPUT);
  digitalWrite(kTouchReset, LOW);
  delay(15);
  digitalWrite(kTouchReset, HIGH);
  delay(120);

  touchBusReady = Wire.begin(kTouchSda, kTouchScl, 100000);
  if (!touchBusReady) {
    Serial.println("ERROR: cannot initialize GT911 I2C on GPIO41/42.");
    return;
  }
  Wire.setTimeOut(25);

  for (uint8_t address : kTouchAddresses) {
    uint8_t productId[4] = {};
    if (readRegister(address, kProductIdRegister, productId,
                     sizeof(productId)) &&
        productId[0] >= '0' && productId[0] <= '9') {
      touchAddress = address;
      Serial.printf("GT911 found at 0x%02X, product ID: %c%c%c%c.\n",
                    touchAddress, productId[0], productId[1], productId[2],
                    productId[3]);
      return;
    }
  }
  Serial.println("WARNING: GT911 not found at 0x5D or 0x14; LCD test continues.");
}

bool pollTouch() {
  if (!touchBusReady || touchAddress == 0) {
    return false;
  }

  uint8_t status = 0;
  if (!readRegister(touchAddress, kTouchStatusRegister, &status, 1)) {
    return false;
  }
  if ((status & 0x80) == 0) {
    return false;
  }

  const uint8_t pointCount = status & 0x0F;
  bool changed = false;
  if (pointCount > 0 && pointCount <= 5) {
    uint8_t point[8] = {};
    if (readRegister(touchAddress, kTouchPointRegister, point,
                     sizeof(point))) {
      const int x = point[1] | (static_cast<int>(point[2]) << 8);
      const int y = point[3] | (static_cast<int>(point[4]) << 8);
      if (x >= 0 && x < kWidth && y >= 0 && y < kHeight) {
        changed = x != touchX || y != touchY;
        touchX = x;
        touchY = y;
        const uint32_t now = millis();
        if (changed || now - lastTouchLogMs >= kTouchLogIntervalMs) {
          Serial.printf("TOUCH: id=%u x=%d y=%d contacts=%u\n", point[0], x,
                        y, pointCount);
          lastTouchLogMs = now;
        }
      } else {
        Serial.printf("WARNING: GT911 point outside panel: x=%d y=%d\n", x,
                      y);
      }
    }
  } else if (pointCount > 5) {
    Serial.printf("WARNING: invalid GT911 contact count: %u\n", pointCount);
  } else if (touchX >= 0 || touchY >= 0) {
    touchX = -1;
    touchY = -1;
    changed = true;
  }

  if (!writeRegister(touchAddress, kTouchStatusRegister, 0)) {
    Serial.println("WARNING: cannot acknowledge GT911 touch status.");
  }
  return changed;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  Serial0.begin(115200);
  const uint32_t connectionStartMs = millis();
  while (!Serial && millis() - connectionStartMs < 8000) {
    delay(20);
  }
  logLine("ESP32-S3 / Waveshare 16249 RGB565 + GT911 diagnostic v2");
  logLine("GPIO19/20 are reserved for native USB and never driven.");

  if (!initPanel()) {
    logLine("RESULT: FAIL (RGB panel initialization)");
    return;
  }
  logLine("STAGE: drawing first color-bar frame");
  if (!showFrame()) {
    logLine("RESULT: FAIL (first RGB frame)");
    heap_caps_free(frame);
    frame = nullptr;
    esp_lcd_panel_del(panel);
    panel = nullptr;
    return;
  }

  logLine("STAGE: probing GT911");
  initTouch();
  logLine("RESULT: RGB active; check the bars, moving stripe and touch.");
  lastAnimationMs = millis();
  lastTouchPollMs = lastAnimationMs;
  lastHeartbeatMs = lastAnimationMs;
}

void loop() {
  if (panel == nullptr || frame == nullptr) {
    const uint32_t now = millis();
    if (now - lastHeartbeatMs >= kHeartbeatIntervalMs) {
      lastHeartbeatMs = now;
      logLine("ALIVE: panel test stopped after an error");
    }
    delay(1000);
    return;
  }

  const uint32_t now = millis();
  bool needsRefresh = false;
  if (now - lastTouchPollMs >= kTouchPollIntervalMs) {
    lastTouchPollMs = now;
    needsRefresh = pollTouch();
  }
  if (now - lastAnimationMs >= kAnimationIntervalMs) {
    lastAnimationMs = now;
    ++animationStep;
    needsRefresh = true;
  }
  if (needsRefresh && !showFrame()) {
    logLine("RESULT: FAIL (RGB refresh)");
    heap_caps_free(frame);
    frame = nullptr;
    esp_lcd_panel_del(panel);
    panel = nullptr;
  }
  if (now - lastHeartbeatMs >= kHeartbeatIntervalMs) {
    lastHeartbeatMs = now;
    logLine("ALIVE: RGB panel loop running");
  }
  delay(5);
}
