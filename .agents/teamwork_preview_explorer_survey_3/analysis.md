# Step 0 Survey Analysis: SD Card, Web/API & Build Subsystems

**Author**: Explorer 3 (SD Card, Web/API & Build Subsystem Explorer)  
**Date**: 2026-09-04  
**Scope**: Requirement R3 (SD Card & Web/API Subsystem Consistency), PlatformIO Build Targets, and Verification Infrastructure.

---

## Executive Summary

A comprehensive read-only audit of the SD card subsystem, Web/API server, web assets pipeline, and build environments for the ESP32-CYD (ESP32-2432S028) aquarium controller revealed critical architectural vulnerabilities, concurrency bugs, and state management gaps:

1. **Severe Inversion of Responsibilities**: `ota_http_server.handleClient()` runs inside `runtime_controller.cpp:io_task` on Core 0 (the 10ms real-time sensor/actuator loop) while holding `GuiMutexGuard`, causing HTTP streaming and OTA uploads to block real-time control, starve the LVGL UI loop on Core 1, and risk triggering the 4-second safety supervisor watchdog.
2. **Missing SD / SPI Bus Mutex**: FreeRTOS tasks (Core 1 UI telemetry history writing, Core 0 `io_task` web serving, and Core 0 `remote_alarm_relay_task`) access the SD card simultaneously with no mutex protection.
3. **Permanent SD Mount Latch & Runtime Removal Hang**: `sd_mounted` is set once and never invalidated upon card removal or I/O failure. Runtime card removal causes `/api/status` to stall indefinitely traversing FAT tables, while re-inserting the card never triggers re-initialization.
4. **Zero SPI Mount Throttling**: When an SD card is absent, `ota_portal_sd_ready()` repeatedly calls `hal_sd_init()` without cooldown on every HTTP request and polling cycle.
5. **Write-Only Relay Mapping**: Web wizard saves `/aq/config/relays.json`, but firmware never parses or loads it on boot.
6. **Tight Flash & Web Asset Budgets**: Flash usage in `esp32dev-espnow` is at 96.9% (60 KB headroom), while web assets occupy 99.7% of raw and 97.8% of gzip budget limits.

---

## 1. SD Card Driver, Pinout & Hardware Architecture

### 1.1 Pin Configuration & Peripheral Allocation
- **SD Card Interface**:
  - CS: GPIO 5
  - SCLK: GPIO 18
  - MISO: GPIO 19
  - MOSI: GPIO 23
  - Bus Host: VSPI (`SPIClass sd_spi(VSPI);` in `firmware/cyd_controller/src/hal_sd.cpp`)
  - Operating Frequency: 20 MHz (`HwConfig::SdCard::SPI_FREQUENCY_HZ`)
- **Display Interface**:
  - CS: GPIO 15, DC: GPIO 2, SCLK: GPIO 14, MISO: GPIO 12, MOSI: GPIO 13
  - Bus Host: HSPI (`SPI2_HOST` in `firmware/cyd_controller/src/hal_display.cpp`)
  - Operating Frequency: 40 MHz write, 16 MHz read
- **Touch Interface**:
  - CS: GPIO 33, SCLK: GPIO 25, MOSI: GPIO 32, MISO: GPIO 39, IRQ: GPIO 36
  - Bus Host: Software Bit-Bang SPI (`HwConfig::Touch::SPI_HOST = -1`)
  - Operating Frequency: 1 MHz

### 1.2 Hardware Contention Assessment
- **Finding**: On the hardware level, Display (HSPI), SD Card (VSPI), and Touch (Software SPI) operate on independent GPIO pins and separate host peripherals. There are no shared SPI pins between the LCD and the SD card.
- **Hardware Limitation**: Standard ESP32-2432S028 (CYD) boards do not connect the Card Detect (CD) pin of the micro-SD socket to any ESP32 GPIO. Card presence can only be determined through software I/O transactions.

---

## 2. SD Card Error Handling, State Consistency & Concurrency Audit

### 2.1 Missing Mutex on SD Operations Across FreeRTOS Tasks
- **Observation**:
  - `firmware/cyd_controller/src/hal_sd.cpp` defines `sd_spi(VSPI)` and `sd_mounted` with no mutex or semaphore.
  - SD operations occur concurrently across three separate FreeRTOS task contexts:
    1. **UI Task (Core 1)**: `main.cpp:loop()` -> `apply_telemetry()` -> `gui_update_metrics()` -> `update_charts_data()` -> `history_archive_append_sample()` (`gui_app.cpp:6788` `SD.open(path, FILE_APPEND)`).
    2. **Real-time I/O Task (Core 0)**: `runtime_controller.cpp:io_task()` -> `gui_app_service_background()` -> `ota_http_server.handleClient()` -> reads assets, streams downloads, or writes WiFi/relay profiles.
    3. **Remote Alarm Task (Core 0)**: `remote_alarm_relay.cpp:relay_task()` -> `remote_alarm_relay_service()` -> checks `SD.exists(RELAY_CA_PATH)` and reads certificate.
- **Impact**: Without mutual exclusion on the `sd_spi` bus and FatFS filesystem layer, concurrent access causes race conditions, corrupted FAT tables, and SPI transaction aborts.

### 2.2 Permanent `sd_mounted` Latch & Runtime Card Removal Failure
- **Observation**:
  - In `hal_sd.cpp`, `sd_mounted` is set to `true` on successful `SD.begin()`.
  - There is no unmount mechanism, no runtime I/O error notification, and `sd_mounted` is never set to `false` when a read/write fails.
  - `hal_sd_is_mounted()` returns `true` indefinitely once set.
- **Impact**:
  - **Card Removal**: When an SD card is physically removed during operation, `hal_sd_is_mounted()` remains `true`. All subsequent file operations attempt communication with a missing card.
  - **Re-insertion**: When a card is re-inserted, `hal_sd_init()` starts with `if (sd_mounted) return true;`, so it never re-initializes the card. The card remains unusable until a hard reboot.

### 2.3 `/api/status` Stalling via `SD.totalBytes()` and `SD.usedBytes()`
- **Observation**:
  - `gui_app.cpp:7185-7188` (`ota_portal_handle_status`):
    ```cpp
    const bool sd_ready_for_status = ota_portal_sd_ready();
    const uint64_t sd_total_bytes = sd_ready_for_status ? SD.totalBytes() : 0ULL;
    const uint64_t sd_used_bytes = sd_ready_for_status ? SD.usedBytes() : 0ULL;
    ```
  - In ESP32 Arduino `SDClass`, both `totalBytes()` and `usedBytes()` call `esp_vfs_fat_info("/sd", ...)` which invokes FatFS `f_getfree()`.
  - `f_getfree()` scans the entire File Allocation Table sector by sector over SPI.
- **Impact**:
  - Every status poll (every 1-2 seconds) triggers two full FAT scans.
  - If the card was removed while `sd_mounted == true`, `f_getfree()` hangs on multiple SPI timeouts, causing UI freezes and risking watchdog resets.

### 2.4 Unthrottled Mount Retries on Missing Card
- **Observation**:
  - `ota_portal_sd_ready()` in `gui_app.cpp:6443`:
    ```cpp
    static bool ota_portal_sd_ready() {
        return hal_sd_is_mounted() || hal_sd_init();
    }
    ```
  - When no card is inserted, `hal_sd_is_mounted()` is `false`, so `hal_sd_init()` runs `sd_spi.begin()` and `SD.begin(...)`.
  - There is zero cooldown or rate limiting.
- **Impact**: Every incoming web request and background poll runs the entire SD initialization sequence, causing SPI timeout delays and degrading system responsiveness.

### 2.5 Write-Only Relay Configuration (`relays.json`)
- **Observation**:
  - `gui_app.cpp:8780` handles action `save_relays` and writes to `/aq/config/relays.json`.
  - `web/relays-wizard.js:328` informs the user: *"Konfiguracja zostanie zapisana w pliku /config/relays.json na karcie SD."* (path discrepancy vs `/aq/config/relays.json`).
  - Search across the entire codebase confirms that `/aq/config/relays.json` is **never loaded** during boot or runtime. Hardware channel mappings remain hardcoded in `config.h`.

---

## 3. Web Server & REST API Audit

### 3.1 Architectural Flaw: Web Server Executed Inside Real-Time `io_task`
- **Observation**:
  - `runtime_controller.cpp:227-238`:
    ```cpp
    void io_task(void *) {
        ...
        for (;;) {
            ...
            runtime_safety_heartbeat(RuntimeSafetyTask::Io, now_ms, ESP.getFreeHeap());
            gui_app_service_background();
            ...
            vTaskDelayUntil(&next_wake, pdMS_TO_TICKS(IO_TASK_PERIOD_MS));
        }
    }
    ```
  - `gui_app.cpp:14905`:
    ```cpp
    void gui_app_service_background(void) {
        GuiMutexGuard guard(50U);
        if (!guard.locked() || !gui_ready) return;
        ...
        ota_http_server.handleClient();
        ...
    }
    ```
- **Consequences**:
  1. **Real-time disruption**: Real-time sensor polling (DS18B20 OneWire state machine, MCP23017, ADS1115) and relay control are suspended while `ota_http_server.handleClient()` streams data.
  2. **GUI Mutex contention**: The GUI mutex is held during `handleClient()`. If a web client downloads a large asset (e.g. `index.html` 153 KB, `style.css` 190 KB), Core 1's LVGL loop (`service_lvgl`) times out on `gui_app_lock(50U)`, dropping display frames and touch events.
  3. **Safety Watchdog Trip**: `runtime_safety.cpp` enforces `HEARTBEAT_TIMEOUT_MS = 4000U` (4 seconds) for `RuntimeSafetyTask::Io`. A slow client connection or large OTA upload (`/update`) taking >4 seconds starves the `io_task` loop, triggering an emergency restart.
  4. **Stack exhaustion risk**: `io_task` has a stack size of 8,192 bytes (`IO_TASK_STACK_BYTES`). Web handlers allocate large buffers (e.g., `ota_portal_handle_v2_capabilities` allocates `char response[3072];` on the stack), consuming ~38% of the stack in a single frame.

### 3.2 Concurrency & Connection Handling
- **Observation**: ESP32 Arduino `WebServer` is synchronous and single-client. Incoming HTTP requests are processed sequentially. If a client stalls, all subsequent requests in the lwIP TCP backlog queue wait.
- **Web Activity Tracker**: `WebActivityTracker` manages up to 4 sessions (`kCapacity = 4`) with session timeout tracking. When web activity is detected, `gui_web_focus_blocks_local_ui()` limits local UI redraws, but does not solve the underlying thread blocking.

### 3.3 Server-Sent Events (SSE) `/api/events` Discrepancy
- **Observation**:
  - `gui_app.cpp:8900`:
    ```cpp
    static void ota_portal_handle_events() {
        ota_portal_mark_web_activity();
        ota_portal_no_cache();
        ota_http_server.sendHeader("Connection", "close");
        ota_http_server.send(200, "text/event-stream", "event: ready\ndata: {}\n\n");
    }
    ```
  - The firmware sends `event: ready` and immediately closes the TCP connection.
  - In contrast, `tools/dev-server/server.js` maintains an open SSE connection (`Connection: keep-alive`) broadcasting `status` and `logs`.
  - In `web/app-core.js:17`, `ENABLE_EVENT_STREAM = false;` is hardcoded. If enabled, the browser's native `EventSource` would enter an endless reconnect cycle every 3 seconds.

### 3.4 Missing Content-Types in Static File Server
- **Observation**:
  - `gui_app.cpp:6390` (`ota_portal_content_type`):
    Handles `.html`, `.css`, `.js`, `.json`, `.csv`, `.txt`, `.log`, `.cfg`, `.bin`, `.aqbin`, `.aqfw`, `.gz`.
    Defaults to `application/octet-stream`.
  - **Missing Types**:
    - `.png` -> returns `application/octet-stream` (shipped: `aquacyd-icon-192.png`, `aquacyd-icon-512.png`).
    - `.webmanifest` -> returns `application/octet-stream` (shipped: `manifest.webmanifest`).
    - `.svg`, `.ico`, `.woff2` -> return `application/octet-stream`.
- **Impact**: Strict browsers and mobile PWA install engines reject icons and web app manifests served with `application/octet-stream`.

---

## 4. Fallback Modes Audit

| Subsystem | Primary Storage | Fallback Mechanism | Status / Risk |
|---|---|---|---|
| **WiFi Credentials** | NVS (`wifi_credential_store`) | Metadata on SD (`/aq/config/wifi/*.cfg`) | **Robust**: Passwords never stored on SD; NVS checked first. |
| **Firmware Settings (`cfg`)** | NVS (`Preferences` "aquarium") | Defaults in `config.h` | **Robust**: NVS persistence functional. |
| **Sensor Calibration** | NVS (`sensor_calibration_store`) | Factory defaults | **Robust**: NVS persistence functional. |
| **Relay Mapping** | SD (`/aq/config/relays.json`) | Hardcoded in `config.h` | **Defective**: SD file is written but never read back. |
| **Web UI (`index.html`)** | SD (`/aq/ota/index.html[.gz]`) | PROGMEM fallback HTML (`OTA_PORTAL_FALLBACK_INDEX`) | **Partial**: Minimal emergency update page in flash. |
| **Web Assets (CSS, JS, PWA)** | SD (`/aq/ota/*`) | None (302 redirect to `/`) | **Critical**: No offline/flash fallback for full web panel. |
| **Flash SPIFFS Partition** | Flash (`min_spiffs.csv` 192KB) | None | **Unused**: Partition exists but is never mounted or used by firmware. |

---

## 5. Web Assets Pipeline & Build Environment Audit

### 5.1 Web Assets Build (`tools/build-web-assets.js`)
- **Process**:
  1. Reads manifest contract `tools/web-assets.json` (21 files).
  2. Minifies PWA scripts and generates raster icons.
  3. Copies files from `web/` to `sdcard/aq/ota/`.
  4. Gzips compressible assets using RFC 1952 normalized OS byte (`0xff`).
- **Budgets vs Actuals**:
  - Raw Budget: 819,200 B | **Actual: 816,772 B** (99.7% used — only 2,428 B remaining).
  - Gzip Budget: 163,840 B | **Actual: 160,278 B** (97.8% used — only 3,562 B remaining).
  - Largest Raw Asset (`style.css`): 190,999 B (budget: 360,000 B).
  - Largest Gzip Asset (`style.css.gz`): 28,886 B (budget: 65,000 B).

### 5.2 PlatformIO Environments (`firmware/cyd_controller/platformio.ini`)

| Environment | Description | Flash Used / Limit | RAM Used / Limit | Test / Verification Status |
|---|---|---|---|---|
| `esp32dev` | Production CYD (ILI9341) | 1,890,085 B (96.1%) / 1.875 MB | 120,188 B (36.7%) / 320 KB | Verified: Compiles successfully (74.8s). |
| `esp32dev-dev` | Dev/simulated sensors, log lvl 3 | 1,903,653 B (96.8%) / 1.875 MB | 121,876 B (37.2%) / 320 KB | Verified: Compiles successfully (177.7s). |
| `esp32dev-espnow` | Production with ESP-NOW link | 1,905,497 B (96.9%) / 1.875 MB | 122,948 B (37.5%) / 320 KB | Verified: Compiles successfully (50.1s; 60 KB headroom). |
| `esp32dev-st7789` | Panel variant ST7789 | ~1.89 MB (96.1%) / 1.875 MB | ~120 KB (36.7%) / 320 KB | Shares codebase with `esp32dev` (`CYD_PANEL_ST7789=1`). |
| `native` | Unity native tests (x86_64) | N/A | N/A | Verified: 40/40 test cases pass (3.5s). |

### 5.3 Test Suite Verification
- `npm run test:api`: 16/16 tests pass against `tools/dev-server`.
- `pio test -e native`: 40/40 Unity unit tests pass.
- `npm run build:web-assets`: Builds clean, enforces gzip normalization.
