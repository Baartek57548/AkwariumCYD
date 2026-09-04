# Project: ESP32-CYD Aquarium Controller Audit, Stability & RAM Optimization

## Architecture
- **Hardware**: ESP32-2432S028R ("CYD" Cheap Yellow Display), 320 KB internal SRAM, 4 MB Flash (no external PSRAM).
- **Display & Touch**: ST7789/ILI9341 display via HSPI + DMA, XPT2046 resistive touch via SPI.
- **Peripherals**: DS18B20 1-Wire temperature, ADS1115 I2C ADC (pH, sensor inputs), MCP23017 I2C expander (relays), LDR analog light sensor, buzzer PWM, LEDC backlight PWM.
- **Storage & Connectivity**: MicroSD card via VSPI, Wi-Fi Station & AP mode, Async/Sync WebServer, REST API, OTA portal, NimBLE server, ESP-NOW link.
- **RTOS Architecture**: FreeRTOS dual core:
  - Core 0: Real-time I/O loop (\io_task\), BLE controller (\le_controller\), Remote Alarm (\
emote_alarm\), ESP-NOW (\quacyd_espnow\).
  - Core 1: Arduino \loopTask\ (LVGL UI loop, touch polling, animation), Safety Supervisor (\safety_supervisor\).

## Feature Inventory
| # | Feature | Description | Milestone | Source |
|---|---------|-------------|-----------|--------|
| 1 | Manual Override Safety Limits | Enforce ATO & CO2 maximum runtime limits even when in manual override mode | M1 | Survey (Explorer 1) |
| 2 | Robust Feeding Schedule Trigger | Replace fragile exact second matching (\sc == 0\) with a window/minute-latch trigger | M1 | Survey (Explorer 1) |
| 3 | Supervisor Deadlock Detection | Ensure safety supervisor detects \gui_mutex\ starvation/deadlock | M1 | Survey (Explorer 1) |
| 4 | Safe Sleep / Watchdog Integration | Prevent 10s light sleep from tripping 4s supervisor watchdog and fix GPIO 21 pin conflict | M1 | Survey (Explorer 1) |
| 5 | ADS1115 Non-Blocking I2C Bus | Prevent I2C bus lock from being held during conversion wait delays | M1 | Survey (Explorer 1) |
| 6 | Domain Unit Test Expansion | Expand native domain unit tests to cover safety limiters, schedule jitter, and calibration edge cases | M1 | Survey (Explorer 1) |
| 7 | TLS Client Buffer Clamping | Add \client.setBufferSizes(1024, 1024)\ to \WiFiClientSecure\ to eliminate 45 KB TLS heap spike | M2 | Survey (Explorer 2) |
| 8 | FreeRTOS Task Stack Right-Sizing | Reduce \music_player_task\ (2KB), \io_task\ (4KB), \le_controller\ (5KB) stacks safely | M2 | Survey (Explorer 2) |
| 9 | BLE Buffer & Queue Optimization | Reduce \BLE_COMMAND_MAX_BYTES\ (1024) and clamp static queues to reclaim SRAM | M2 | Survey (Explorer 2) |
| 10 | UI Subpage Deletion & Heap Gating | Fix race between \delete_obj_async\ and \ensure_runtime_ui_heap\ to prevent touch lockout | M2 | Survey (Explorer 2) |
| 11 | Hot Path String Allocation Cleanup | Eliminate temporary dynamic \String\ allocations in telemetry and config parsing | M2 | Survey (Explorer 2) |
| 12 | SD Card SPI Mutex Synchronization | Add recursive mutex for \SD.\ and \sd_spi\ to protect concurrent access across tasks | M3 | Survey (Explorer 3) |
| 13 | Web Server Decoupling from Real-Time I/O | Decouple \handleClient()\ from 10ms \io_task\ to avoid blocking sensors, UI, and watchdog | M3 | Survey (Explorer 3) |
| 14 | Dynamic SD Card Health & Cooldown | Detect card removal/I/O errors, unmount properly, enforce 5s retry cooldown on failure | M3 | Survey (Explorer 3) |
| 15 | Web Portal MIME Types & Asset Serving | Add \.png\ and \.webmanifest\ MIME types; verify static web asset serving pipeline | M3 | Survey (Explorer 3) |
| 16 | Full E2E & PlatformIO Test Verification | Verify 100% build pass on \esp32dev\, \esp32dev-espnow\, native tests, and npm API tests | M4 | Survey (All Explorers) |

## Milestones
| # | Name | Scope | Dependencies | Status |
|---|------|-------|-------------|--------|
| 1 | M1: Firmware Logic & Stability | Fix manual override limits, feeding schedule trigger, watchdog deadlock detection, safe sleep, ADS1115 I2C delay, unit test coverage | none | IN_PROGRESS |
| 2 | M2: RAM & Heap Optimization | Clamp TLS buffers, right-size FreeRTOS stacks, optimize BLE buffers, fix UI heap gating race, eliminate String churn | M1 | PLANNED |
| 3 | M3: SD Card & Web/API Subsystem | Implement SD/SPI mutex, decouple WebServer from 10ms io_task, add dynamic SD health & cooldown, fix MIME types | M2 | PLANNED |
| 4 | M4: Final Verification & Audit Readiness | Run native tests, all PlatformIO builds, npm tests, Challenger stress tests, and Forensic Auditor verification | M1, M2, M3 | PLANNED |

## Code Layout
- firmware/cyd_controller/src/gui_app.cpp\: Main application, UI integration, schedule and automation dispatch, WebServer portal.
- firmware/cyd_controller/src/runtime_controller.cpp\: Core 0 \io_task\ management, background task dispatch.
- firmware/cyd_controller/src/runtime_safety.cpp\: Core 1 \safety_supervisor\ task, watchdog tracking.
- firmware/cyd_controller/src/remote_alarm_relay.cpp\: Secure HTTPS webhook alerting, TLS client management.
- firmware/cyd_controller/src/ble_controller.cpp\: NimBLE server and BLE command processing.
- firmware/cyd_controller/src/hal_sd.cpp\: SD card driver and VSPI initialization.
- firmware/cyd_controller/src/hal_adc.cpp\: ADS1115 I2C ADC readings and bus locking.
- firmware/cyd_controller/lib/\: Decoupled domain libraries (\quarium_automation\, \quarium_schedule\, \control_modes\, \sensor_calibration\, etc.).
- firmware/cyd_controller/test/test_native_domain/\: Native Unity unit tests.
- \	ools/\: Web asset build scripts (\uild-web-assets.js\) and test suites (\	est-api.js\).

## Interface Contracts
### Safety & Automation Limits
- \
untime.waterFillOn\ and \
untime.co2On\ MUST respect duration limits (\to_max_limit_ms\, \co2_max_limit_ms\) regardless of whether requested by automation or manual override.
### Concurrency & SD Storage
- All SD operations MUST acquire \hal_sd_lock(timeout)\ before accessing \SD.\ or \sd_spi\ and release via \hal_sd_unlock()\.
- \WebServer::handleClient()\ MUST NOT be called within \io_task\ holding \gui_mutex\ for prolonged periods.

