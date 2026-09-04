# Handoff Report — Explorer 3 (SD Card, Web/API & Build Subsystems)

**Milestone**: Step 0 Survey  
**Working Directory**: `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\teamwork_preview_explorer_survey_3`  
**Target Recipient**: Orchestrator (ID: `56ceb5af-6a46-4981-bf39-e3e616dc0656`)

---

## 1. Observation

### 1.1 Concurrency & Architecture Observations
- **Observation 1 (Web Server in Core 0 Real-time Task)**: In `firmware/cyd_controller/src/runtime_controller.cpp:227-237`, `io_task` is created as the 10ms real-time loop (`vTaskDelayUntil(&next_wake, pdMS_TO_TICKS(10U))`). At line 237, it directly calls `gui_app_service_background()`. In `firmware/cyd_controller/src/gui_app.cpp:14905-14994`, `gui_app_service_background()` acquires `GuiMutexGuard guard(50U);` and calls `ota_http_server.handleClient();` at line 14994.
- **Observation 2 (Watchdog Timeout)**: In `firmware/cyd_controller/src/runtime_safety.cpp:21`, `constexpr uint32_t HEARTBEAT_TIMEOUT_MS = 4000U;` sets the watchdog timeout for `RuntimeSafetyTask::Io` to 4 seconds. When a slow HTTP transfer or upload occurs in `handleClient()`, `io_task` does not reach its heartbeat at line 235.
- **Observation 3 (Multi-task SD Access Without Mutex)**: 
  - `firmware/cyd_controller/src/hal_sd.cpp` contains `SPIClass sd_spi(VSPI);` and `bool sd_mounted = false;` without any FreeRTOS mutex or synchronization primitive.
  - Core 1 calls `SD.open(path, FILE_APPEND)` in `gui_app.cpp:6788` (`history_archive_append_sample`) from `main.cpp:loop()`.
  - Core 0 calls `SD.open()`, `SD.exists()`, `SD.totalBytes()`, and `SD.usedBytes()` in `gui_app.cpp:7186, 9025, 9085, 9405` inside `io_task`.
  - Core 0 calls `SD.exists(RELAY_CA_PATH)` and `SD.open(RELAY_CA_PATH)` in `remote_alarm_relay.cpp:339, 344` inside `relay_task`.

### 1.2 State & Error Handling Observations
- **Observation 4 (Permanent SD Mount State)**: In `firmware/cyd_controller/src/hal_sd.cpp:15-53`, `sd_mounted` is set to `true` at line 29 (`sd_mounted = SD.begin(...)`) and only set to `false` if initial `card_type == CARD_NONE`. There is no unmount function, no error listener, and `sd_mounted` is never set to `false` on subsequent I/O failures. Line 17 returns `true` unconditionally if `sd_mounted == true`.
- **Observation 5 (Unthrottled SPI Begin Storm)**: In `firmware/cyd_controller/src/gui_app.cpp:6443`, `ota_portal_sd_ready()` executes `return hal_sd_is_mounted() || hal_sd_init();`. When no card is inserted, every call runs `hal_sd_init()`, executing `sd_spi.begin()` and `SD.begin(...)` without any backoff or cooldown.
- **Observation 6 (Status Endpoint FAT Traversal)**: In `firmware/cyd_controller/src/gui_app.cpp:7186-7187`, `ota_portal_handle_status()` calls `SD.totalBytes()` and `SD.usedBytes()`. In Arduino ESP32 `SDClass`, both invoke `esp_vfs_fat_info("/sd", ...)` which executes FatFS `f_getfree()` to scan the entire cluster table over SPI.
- **Observation 7 (Write-Only Relays Configuration)**: In `firmware/cyd_controller/src/gui_app.cpp:8780-8796`, action `save_relays` writes JSON to `/aq/config/relays.json`. Ripgrep and Select-String confirm that `/aq/config/relays.json` is never read, opened, or parsed anywhere else in the repository.
- **Observation 8 (Static File Content-Type Gaps)**: In `firmware/cyd_controller/src/gui_app.cpp:6390-6408`, `ota_portal_content_type()` lacks mappings for `.png`, `.webmanifest`, `.svg`, `.ico`, and `.woff2`, returning `application/octet-stream`.

### 1.3 Build and Resource Limit Observations
- **Observation 9 (Flash Memory Budget)**: 
  - `pio run -e esp32dev`: Flash used 1,890,085 B (96.1% of 1,966,080 B limit).
  - `pio run -e esp32dev-espnow`: Flash used 1,905,497 B (96.9% of 1,966,080 B limit; 60,583 B remaining).
  - `pio run -e esp32dev-dev`: Flash used 1,903,653 B (96.8% of 1,966,080 B limit).
- **Observation 10 (Web Asset Budget)**: Running `node tools/build-web-assets.js` produces 21 files: 816,772 B raw (budget: 819,200 B; 2,428 B remaining) and 160,278 B gzip (budget: 163,840 B; 3,562 B remaining).
- **Observation 11 (Unit & Integration Tests)**:
  - `pio test -e native`: 40/40 Unity tests succeed in 3.47s.
  - `npm run test:api`: 16/16 node tests succeed in 504ms.

---

## 2. Logic Chain

1. **Safety Watchdog Trip Mechanism**:
   - `io_task` is pinned to Core 0 with a 10ms period to service critical sensors and relays (Observation 1).
   - `gui_app_service_background()` is called from inside `io_task` and runs `ota_http_server.handleClient()` while holding `GuiMutexGuard` (Observation 1).
   - Serving a large static file (`style.css` 190 KB or `index.html` 153 KB) or handling an OTA update (`/update` up to 1.9 MB) over a slow or high-latency WiFi link blocks `handleClient()` for seconds.
   - Because `io_task` is blocked in `handleClient()`, it cannot call `runtime_safety_heartbeat(RuntimeSafetyTask::Io, ...)` (Observation 1).
   - `runtime_safety_supervisor` checks heartbeats every 250ms and trips when elapsed time exceeds 4000ms (`HEARTBEAT_TIMEOUT_MS`) (Observation 2).
   - *Inference*: Any sustained web download, slow client transfer, or OTA upload that takes >4s will trigger an emergency watchdog reboot of the controller. Furthermore, holding `GuiMutexGuard` during network I/O starves Core 1's UI thread, causing LVGL to drop display frames and ignore touch input.

2. **Filesystem and SPI Bus Corruption Mechanism**:
   - `hal_sd.cpp` initializes a single `sd_spi` VSPI bus instance without any mutex (Observation 3).
   - Core 1 appends telemetry samples to `/aq/data/history/YYYY-MM.aqbin` every 60s (Observation 3).
   - Core 0 concurrently streams static files or processes file downloads over the same VSPI bus (Observation 3).
   - Because FatFS and `SDClass` in ESP32 Arduino are not thread-safe and share the underlying SPI hardware registers, concurrent file operations corrupt directory records and sector reads/writes.

3. **Runtime Card Removal and Hang Mechanism**:
   - Once mounted, `sd_mounted` is latched `true` permanently (Observation 4).
   - When the user physically removes the card, `hal_sd_is_mounted()` still returns `true`.
   - Polling `/api/status` calls `SD.totalBytes()` and `SD.usedBytes()` (Observation 6), which call `f_getfree()` to traverse FAT sectors over SPI.
   - Because no card is present, SPI transfers timeout on each sector read, blocking the calling thread for seconds on every status request.
   - When the card is re-inserted, `hal_sd_init()` exits early because `sd_mounted` is `true`, leaving the newly inserted card uninitialized.

4. **Configuration Inconsistency**:
   - The web wizard allows configuring 8 relay channels and saves them to `/aq/config/relays.json` (Observation 7).
   - However, the firmware never reads `/aq/config/relays.json` during boot or runtime (Observation 7).
   - *Inference*: User relay configurations saved on the SD card are purely cosmetic and have zero effect on controller relay assignments.

---

## 3. Caveats

- Hardware testing was performed via static analysis, unit testing, and compilation inspection; physical CYD board runtime execution with physical SD card hot-plugging was not conducted directly.
- The 192KB SPIFFS partition in `min_spiffs.csv` is present in flash, but since firmware flash usage is already at ~96.9%, storing uncompressed web assets in flash is impossible without external partition restructuring or flash expansion (e.g. 8MB/16MB).
- No caveats regarding code paths or build environments: all targets and scripts were inspected and verified.

---

## 4. Conclusion

The SD card and Web/API subsystems currently violate Requirement R3 in multiple areas:
1. **Critical Thread Coupling**: `WebServer::handleClient()` must be decoupled from the 10ms real-time `io_task` and run in an independent, lower-priority task (or non-blocking loop on Core 1) without holding the GUI mutex during network I/O.
2. **Missing SD Mutex**: A dedicated recursive mutex (`hal_sd_lock()` / `hal_sd_unlock()`) must protect all `SD.` and `sd_spi` access across tasks.
3. **Dynamic SD Health & Mount State**: `hal_sd` must track runtime I/O failures, invalidate `sd_mounted` upon read/write error or card removal, implement an exponential backoff/cooldown (e.g., 5s) for mount retries, and cache `totalBytes`/`usedBytes` rather than scanning FAT on every status poll.
4. **Content-Type & Route Alignment**: `ota_portal_content_type()` must include `.png` (`image/png`) and `.webmanifest` (`application/manifest+json`).
5. **Relay Profile Semantics**: Either implement `/aq/config/relays.json` loading into runtime channel mappings upon boot, or explicitly document in the API contract and UI that relay mapping is read-only in firmware.
6. **Flash Headroom Constraint**: Flash usage is at 96.9% in `esp32dev-espnow`. Any new code must not increase flash footprint by more than ~40-50 KB without partition resizing.

---

## 5. Verification Method

To independently verify these findings, run the following commands from the repository root:

1. **Verify Native Test Suite**:
   ```bash
   cd firmware/cyd_controller
   pio test -e native
   ```
   *Expected*: 40 test cases pass.

2. **Verify All PlatformIO Builds**:
   ```bash
   cd firmware/cyd_controller
   pio run -e esp32dev
   pio run -e esp32dev-espnow
   pio run -e esp32dev-dev
   ```
   *Expected*: Builds pass; inspect memory report showing `esp32dev-espnow` flash usage at 96.9% (1,905,497 bytes out of 1,966,080 bytes).

3. **Verify Web Assets & API Test Suite**:
   ```bash
   npm run build:web-assets
   npm run test:api
   ```
   *Expected*: Assets build at 816,772 B raw / 160,278 B gzip; all 16 node tests pass.

4. **Codebase Inspection**:
   - Inspect `firmware/cyd_controller/src/runtime_controller.cpp:237` to verify `gui_app_service_background()` inside `io_task`.
   - Inspect `firmware/cyd_controller/src/gui_app.cpp:14905, 14994` to verify `GuiMutexGuard` wrapping `handleClient()`.
   - Inspect `firmware/cyd_controller/src/hal_sd.cpp` to verify absence of mutex and unmount logic.
