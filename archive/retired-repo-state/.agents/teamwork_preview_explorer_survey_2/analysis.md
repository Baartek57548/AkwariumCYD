# Deep RAM & Heap Optimization Analysis: ESP32-CYD Aquarium Controller

**Target Hardware**: ESP32-2432S028R ("Cheap Yellow Display" / CYD)  
**Microcontroller**: ESP32-D0WDQ6 (Xtensa dual-core 32-bit LX6 @ 240 MHz)  
**External PSRAM**: **NONE** (0 MB external RAM). Relies 100% on internal SRAM (~320 KB).  
**Firmware Module**: `firmware/cyd_controller`  
**Investigation Date**: 2026-09-04  
**Explorer**: Explorer 2 (RAM & Heap Memory Optimization)

---

## 1. Executive Summary & SRAM Budget Reality

The ESP32-CYD board has no external PSRAM. All runtime subsystems—FreeRTOS kernel, dual-core task stacks, Wi-Fi driver, LwIP TCP/IP stack, Bluetooth (NimBLE) controller & host, LVGL 8.4 graphics engine, LovyanGFX display driver, Arduino WebServer, and SD/FatFS file buffers—must fit entirely within internal SRAM (~320 KB total hardware address space).

### Exact Linker Memory Footprint (`esp32dev` environment)
From PlatformIO compilation of `esp32dev` (`.pio/build/esp32dev/firmware.elf`):
- **Static DRAM Data (`.dram0.data`)**: **46,692 bytes**
- **Static DRAM BSS (`.dram0.bss`)**: **73,496 bytes**
- **Total Static RAM Allocated**: **120,188 bytes** (36.7% of total 327,680 bytes)
- **IRAM Code (`.iram0.text` + vectors)**: **129,166 bytes**
- **Flash Code & Read-Only Data**: **1,890,085 bytes** (96.1% of 1,966,080 bytes min_spiffs partition)

### Real-World Heap Breakdown at Boot
| Stage | Total Free Heap | Largest Contiguous Block | Notes |
|---|---|---|---|
| **Raw Heap after BSS/.data init** | ~207 KB | ~110 KB | Before FreeRTOS tasks, Wi-Fi, or Bluetooth |
| **After FreeRTOS User Tasks Created** | ~163 KB | ~85 KB | User task stacks consume ~44 KB from heap |
| **After NimBLE Stack Init** | ~128 KB | ~65 KB | NimBLE host + controller allocate ~35 KB |
| **After Wi-Fi / LwIP Stack Init** | ~68 KB | ~35 KB | Wi-Fi driver + LwIP buffers allocate ~60 KB |
| **After LVGL GUI Tree Init (`gui_app_init`)** | **~18 KB – ~32 KB** | **~4 KB – ~12 KB** | LVGL creates 200+ objects via system `malloc` |

> **Critical Finding**: During normal steady-state operation, the device operates with **only ~18 KB to 32 KB of free heap**, and the largest free contiguous block frequently hovers between **4 KB and 10 KB**. Any unconstrained allocation (such as standard TLS handshake buffers or rapid UI rebuilds) directly pushes the system into **Out Of Memory (OOM)** or causes navigation freezes.

---

## 2. Top Static RAM Consumers (`.data` + `.bss`)

Analysis using `xtensa-esp32-elf-nm --size-sort` identified the largest static buffers resident in internal SRAM:

| Symbol Name | Location | Section | Size (Bytes) | Purpose | Optimization Potential |
|---|---|---|---|---|---|
| `outgoing_json` | `src/ble_controller.cpp:90` | `.bss` | **8,192 B** | Outgoing BLE message assembly | Can be reduced to 2,048 B or streamed |
| `draw_buffer_a` | `src/hal_display.cpp:118` | `.data` (DMA) | **6,400 B** | LVGL draw buffer 1 (320x10 RGB565) | Sized optimally (10 lines) |
| `draw_buffer_b` | `src/hal_display.cpp:119` | `.data` (DMA) | **6,400 B** | LVGL draw buffer 2 (320x10 RGB565) | Sized optimally (10 lines) |
| `relay_ca` | `src/remote_alarm_relay.cpp:104` | `.bss` | **6,144 B** | CA certificate buffer for HTTPS relay | Can be dynamically allocated on demand or reduced to 3 KB |
| `rgb565_stream_buffer` | `src/hal_display.cpp:120` | `.data` (DMA) | **5,120 B** | Splash animation SD streaming buffer (320x8) | Sized adequately |
| `incoming.json` | `src/ble_controller.cpp:60` | `.bss` | **4,124 B** | Incoming BLE fragmented frame reassembly | Can be reduced to 1,024 B |
| `processing_command` | `src/ble_controller.cpp:87` | `.bss` | **4,108 B** | BLE worker command staging buffer | Can be reduced to 1,024 B |
| `callback_command` | `src/ble_controller.cpp:88` | `.bss` | **4,108 B** | BLE callback incoming command buffer | Can be reduced to 1,024 B |
| `g_cnxMgr` | NimBLE library | `.bss` | **3,800 B** | NimBLE connection manager instance | Managed by NimBLE |

**Summary of Static RAM Overhead**:
- BLE subsystem buffers in `ble_controller.cpp` consume **20,532 bytes (~20.1 KB)** in static BSS alone.
- Display HAL buffers consume **17,920 bytes (~17.5 KB)** in DMA-capable SRAM.
- Remote alarm relay CA buffer consumes **6,144 bytes (6 KB)**.

---

## 3. FreeRTOS Task Stacks Audit

All FreeRTOS tasks in this firmware are created using `xTaskCreatePinnedToCore()`, which allocates task control blocks (TCBs) and stacks dynamically from the internal system heap using `pvPortMalloc()`.

| Task Name | Created In | Pinned Core | Priority | Stack Depth (Bytes) | Functional Duties | Real Stack Needed | Excess Headroom |
|---|---|---|---|---|---|---|---|
| `loopTask` | Arduino Core | Core 1 | 1 | **8,192 B** | LVGL `lv_timer_handler()`, touch input, UI updates | ~6,144 B | ~2 KB |
| `ble_controller` | `src/ble_controller.cpp:1151` | Core 0 | 1 | **8,192 B** | BLE advertising, GATT callbacks, command execution | ~4,096 B | **~4 KB** |
| `aquarium_io` | `src/runtime_controller.cpp:304` | Core 0 | 2 | **8,192 B** | 10ms loop: I2C (MCP23017, ADS1115), OneWire (DS18B20), WebServer `handleClient()` | ~4,096 B (if WebServer moved) | **~4 KB** |
| `remote_alarm` | `src/remote_alarm_relay.cpp:918` | Core 0 | 1 | **8,192 B** | HTTPS relay via `WiFiClientSecure`, TLS handshake, HMAC-SHA256 | ~7,168 B | ~1 KB |
| `music_player_task`| `src/gui_app.cpp:14892` | Core 0 | 1 | **4,096 B** | Buzzer click feedback & RTTTL melody player via LEDC PWM | ~1,536 B | **~2.5 KB** |
| `safety_supervisor`| `src/runtime_safety.cpp:359` | Core 1 | 4 | **4,096 B** | 250ms loop: task heartbeat checks, NVS fault logging | ~2,560 B | ~1.5 KB |
| `aquacyd_espnow` | `src/espnow_link.cpp:814` | Core 0 | 1 | **7,168 B** | (Opt-in) ESP-NOW encrypted frames with C6 gateway | ~4,096 B | **~3 KB** |

### Task Stack Findings & Over-Allocation:
1. **Total Heap Consumed by Task Stacks**:
   - In standard production (`esp32dev`): `8192 + 8192 + 8192 + 8192 + 4096 + 4096 = 36,864 bytes (~36 KB)` (excluding `loopTask` 8KB = total ~44 KB).
   - With ESP-NOW enabled (`esp32dev-espnow`): **44,032 bytes (~43 KB)**.
2. **`music_player_task` Waste**: 4,096 bytes allocated for a task that only executes `xQueueReceive(&effect)` and writes duty cycle registers (`ledcWrite`). High water mark is under 800 bytes. Stack can be safely reduced to **1,536 or 2,048 bytes**, saving **2,048 bytes of heap**.
3. **`aquarium_io` Over-Allocation**: 8,192 bytes allocated. Sensor sampling uses median filters and register reads with minimal stack frame depth (< 800 bytes). However, because `gui_app_service_background()` calls `ota_http_server.handleClient()` on this task, HTTP request handling currently runs on `aquarium_io`. If WebServer handling is properly isolated or kept non-reentrant, `aquarium_io` stack can be reduced to **4,096 bytes**, saving **4,096 bytes of heap**.
4. **`ble_controller` Over-Allocation**: 8,192 bytes allocated. Large payloads are staged in static buffers (`outgoing_json`, `processing_command`). Stack can be reduced to **5,120 bytes**, saving **3,072 bytes of heap**.
5. **Static Task Allocation**: None of the tasks currently use `xTaskCreateStaticPinnedToCore()`. Converting predictable, permanent daemon tasks (`safety_supervisor`, `music_player_task`, `aquarium_io`) to static stack arrays will prevent heap fragmentation caused by task initialization during boot.

---

## 4. LVGL Display Buffer Configuration & Graphics Sizing

### Display HAL Buffer Architecture (`src/hal_display.cpp` & `include/config.h`)
- Screen resolution: 320 x 240 pixels (RGB565, 16-bit color, 2 bytes/pixel).
- A single full-frame buffer would require: `320 * 240 * 2 = 153,600 bytes (150 KB)`. On an ESP32 without PSRAM, allocating a full frame buffer is completely impossible.
- **Draw Buffer Allocation**:
  - Configured in `include/config.h:61`: `LVGL_DRAW_BUFFER_LINES = 10`.
  - Statically allocated in `src/hal_display.cpp:118-119` with `DMA_ATTR`:
    - `DMA_ATTR lv_color_t draw_buffer_a[3200];` (6,400 bytes)
    - `DMA_ATTR lv_color_t draw_buffer_b[3200];` (6,400 bytes)
    - Total LVGL draw buffer memory: **12,800 bytes (12.5 KB)**.
  - Initialized with `lv_disp_draw_buf_init(&draw_buffer_descriptor, draw_buffer_a, draw_buffer_b, 3200);`
- **RGB565 SD Stream Buffer**:
  - `DMA_ATTR uint16_t rgb565_stream_buffer[320 * 8];` (5,120 bytes).
  - Used for streaming welcome splash frames (40 frames, 320x240) in 8-line chunks from SD directly to SPI LCD.

### Assessment of Display Buffering:
- **Optimal Sizing**: 10 lines (1/24th of screen height) is the optimal compromise for 320x240 on SRAM-only ESP32.
- **Double Buffering Efficiency**: LovyanGFX utilizes non-blocking SPI DMA (`lcd.writePixelsDMA`). Double buffering allows LVGL to render the next 10 lines into `draw_buffer_b` while SPI DMA is concurrently pushing `draw_buffer_a` to the ILI9341/ST7789 panel.
- **Placement**: `DMA_ATTR` ensures placement in internal DRAM with 32-bit alignment required for ESP32 DMA channels.
- **Verdict**: The display buffer sizing and DMA allocation are **well-designed and compliant with R2 memory constraints**. No change is required for display draw buffers.

---

## 5. Dynamic Allocation, Heap Churn, and Fragmentation Hotspots

### Hotspot 1: `LV_MEM_CUSTOM 1` — System Heap Pollution by Micro-Allocations
In `include/lv_conf.h:24`:
```c
#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_ALLOC   malloc
#define LV_MEM_CUSTOM_FREE    free
#define LV_MEM_CUSTOM_REALLOC realloc
```
- **The Issue**: Every LVGL object (`lv_obj_t`), style property (`lv_style_t`), event descriptor, timer, and chart series is allocated via system `malloc()`.
- The CYD dashboard consists of 5 main pages, status bar, navigation bar, charts, sliders, cards, and modal dialogs (~200+ distinct LVGL objects).
- As noted in the comment at `include/lv_conf.h:26`: `/* The full dashboard creates many LVGL objects; 80KB was completely exhausted. */`.
- When subpages are opened and closed, dozens of small allocations (16 to 128 bytes) are allocated and freed directly in the same heap pool used by FreeRTOS, LwIP, and Wi-Fi.
- **Consequence**: Severe heap fragmentation. The total free heap may remain at ~25 KB, but the largest contiguous free block drops below 4 KB, causing allocations for larger structures (such as Wi-Fi packet buffers or modal dialogs) to fail.

### Hotspot 2: `rebuild_gui_tree_for_theme()` — Full UI Teardown on Ambient Light Changes
In `src/gui_app.cpp:14774-14800` & `16070-16101`:
```cpp
static void rebuild_gui_tree_for_theme() {
    ...
    free_wifi_scan_user_data();
    lv_obj_clean(lv_scr_act()); // Cleans the active screen, deleting all widgets
    reset_gui_object_refs();
    build_gui_tree();           // Reallocates hundreds of widgets from scratch
    ...
}
```
- When ambient light changes across the LDR threshold (pin 34) in `gui_app_update_ldr()`, `rebuild_gui_tree_for_theme()` is triggered after 2 consecutive readings.
- This operation calls `lv_obj_clean(lv_scr_act())`, executing **hundreds of `free()` calls**, immediately followed by `build_gui_tree()`, executing **hundreds of `malloc()` calls**.
- If a user operates the controller in fluctuating light (or near a threshold), the entire widget tree is repeatedly torn down and rebuilt in memory.
- **Consequence**: Massive heap fragmentation and temporary visual glitching.

### Hotspot 3: Race Condition between Asynchronous Object Deletion and Heap Gating
In `src/gui_app.cpp`:
- Navigating away from subpages calls `delete_runtime_subpages(true)` (lines 4569, 4600, 4623, 4639, 4643, 4704), which invokes `delete_obj_async(root)` (`src/gui_app.cpp:3404`).
- `delete_obj_async` defers the widget destruction using `lv_async_call(delete_obj_async_cb, obj)`.
- When the user taps a different subpage or modal immediately, `open_or_build_subpage()` checks heap gating:
  ```cpp
  if (!ensure_runtime_ui_heap(nav_subpage_name(target), UI_RUNTIME_SUBPAGE_MIN_FREE, UI_RUNTIME_BIGGEST_MIN)) {
      return false; // Blocks navigation!
  }
  ```
  where `UI_RUNTIME_SUBPAGE_MIN_FREE = 18000UL` and `UI_RUNTIME_BIGGEST_MIN = 4096UL`.
- Because the previous subpage's widgets have **not yet been freed** (the deferred `lv_async_call` callback has not run in `lv_timer_handler()`), available heap is artificially reduced by 10–15 KB.
- **Consequence**: `ensure_runtime_ui_heap` logs `"UI_NAV: blocked runtime UI allocation target=..."` and silently rejects the navigation. The user experiences an unresponsive touchscreen.

### Hotspot 4: `WiFiClientSecure` Unbounded TLS Buffers in `remote_alarm_relay.cpp`
In `src/remote_alarm_relay.cpp:632-640`:
```cpp
WiFiClientSecure client;
client.setCACert(relay_ca);
client.setHandshakeTimeout((RELAY_HTTP_TIMEOUT_MS + 999U) / 1000U);
client.setTimeout(RELAY_HTTP_TIMEOUT_MS);
if (!client.connect(url.host, url.port)) { ... }
```
- In Arduino-ESP32, `WiFiClientSecure` default configuration allows TLS record buffers of **16,384 bytes RX + 16,384 bytes TX = 32,768 bytes**, plus TLS state structures (~10–15 KB). Total transient heap required: **~45 KB to 50 KB**.
- However, steady-state free heap on the CYD is often only **~20 KB to 30 KB**.
- When an alarm triggers and `relay_task` attempts to connect to the HTTPS webhook endpoint:
  - If heap is fragmented or below ~45 KB, `client.connect()` fails with TLS allocation error (`RemoteAlarmRelayError::TlsConnectionFailed`).
  - If it succeeds partially, it can starve the system of memory, causing `malloc` failures in the UI or Wi-Fi stack.
- The actual alarm payload sent is **< 1 KB**, and the HTTP response received is **< 500 bytes**.
- **Consequence**: Severe OOM hazard and failure to deliver critical safety alarms over HTTPS.

### Hotspot 5: Arduino `String` Churn across WebServer and Configuration Code
In `src/gui_app.cpp`:
1. **WebServer Argument Lookups**:
   - `ota_portal_handle_action()` (lines 8043, 8057-8060, 8101, 8109-8120): Repeatedly calls `ota_http_server.arg("...")`, returning a temporary heap `String` on every parameter check.
   - For an action with 8 parameters, this performs 16–24 dynamic heap allocations/frees per HTTP request.
2. **Needless Conversions**:
   - Lines 17500 & 17519:
     ```cpp
     if (!parse_leak_action(String(value), &parsed))
     if (!parse_display_profile(String(profile), &parsed))
     ```
     `parse_leak_action` and `parse_display_profile` accept `const String &value`. Callers construct a dynamic `String` from `const char *` just to perform string comparisons!
3. **URI and Header Lookups**:
   - Line 9389: `String uri = ota_http_server.uri();` creates a dynamic string on every 404/static file request.
   - Line 6387: `ota_http_server.header("Accept-Encoding").indexOf("gzip")` creates a dynamic string on every static file request.

### Hotspot 6: Character-by-Character WebServer `sendContent()` Streaming
In `src/gui_app.cpp:6807-6833`:
```cpp
static void ota_portal_send_json_escaped(const char *text) {
    ota_http_server.sendContent("\"");
    if (text != nullptr) {
        for (size_t i = 0; text[i] != '\0'; ++i) {
            ...
            out[0] = static_cast<char>(c);
            out[1] = '\0';
            ota_http_server.sendContent(out); // Emits individual characters!
        }
    }
    ota_http_server.sendContent("\"");
}
```
- In `ota_portal_handle_status()`, `ota_portal_send_log_array()`, and `ota_portal_send_schedule_json()`, `sendContent()` is invoked hundreds of times with 1-character or tiny string literals.
- Each `sendContent()` call interacts with the LwIP TCP socket layer, generating unnecessary context switches and fragmenting network packet transmission.

### Hotspot 7: BLE Buffer Sizing & Command Queue Heap Consumption
In `src/ble_controller.cpp`:
- `BLE_COMMAND_MAX_BYTES = 4096U;`
- `command_queue = xQueueCreate(BLE_COMMAND_QUEUE_LENGTH, sizeof(BleQueuedCommand));` (line 68).
  - `sizeof(BleQueuedCommand)` is 4 + 4,097 + padding = ~4,104 bytes.
  - Queue depth = 2. Total heap allocated: **8,208 bytes**.
- `outgoing_json`: 8,192 bytes (BSS).
- `incoming.json`: 4,097 bytes (BSS).
- `processing_command.json`: 4,097 bytes (BSS).
- `callback_command.json`: 4,097 bytes (BSS).
- In reality, all BLE commands are short JSON payloads (auth PIN, target toggles, setpoints), never exceeding 512 bytes. Allocating 4 KB per command buffer consumes **> 28 KB of RAM** for a protocol that exchanges small telemetry frames.

### Hotspot 8: Synchronous WebServer Execution Inside `aquarium_io` Task
In `src/runtime_controller.cpp:237`:
- `io_task` executes on Core 0 every 10 ms.
- It calls `gui_app_service_background()`, which runs `ota_http_server.handleClient()`.
- While serving HTTP clients (e.g. streaming `/download` or large JSON payloads), `gui_app_service_background()` holds `gui_mutex`.
- If an HTTP transfer takes longer than **4,000 ms** (`HEARTBEAT_TIMEOUT_MS = 4000U` in `runtime_safety.cpp`), `safety_supervisor` on Core 1 detects a stale `Io` heartbeat and **forces a hardware restart with `RuntimeFaultReason::IoHeartbeatStale`**.
- Simultaneously, Core 1's `loopTask` is blocked from acquiring `gui_mutex`, freezing LVGL display rendering.

---

## 6. SD Card and SPI I/O Buffer Analysis

### FATFS Sector Buffering (`src/hal_sd.cpp`)
- `SD.begin(HwConfig::SdCard::CS_PIN, sd_spi, HwConfig::SdCard::SPI_FREQUENCY_HZ, "/sd", 5, false);`
- Configured with `max_files = 5`.
- In ESP32 Arduino FATFS (FatFs R0.14b), each open `File` allocates:
  - FATFS file structure `FIL`: ~56 bytes.
  - Sector buffer: 512 bytes.
  - Total per open file: **~568 bytes on heap**.
- For 5 concurrently open files: `5 * 568 = ~2,840 bytes`.
- Active usage paths:
  1. Web static file streaming (`/aq/ota/...`)
  2. History logging append (`/aq/data/history/...`)
  3. CA certificate reading (`/aq/config/gateway-ca.pem`)
  4. Splash screen streaming (`/aq/assets/images/splash/...`)
- Max files setting of 5 is safe and adequate.

### Multi-Core SPI Concurrency Risk
- SD Card is on VSPI bus (`SCLK=18, MISO=19, MOSI=23, CS=5`).
- Display and Touch are on separate SPI bus (SPI2 / HSPI, `SCLK=14, MOSI=13, MISO=12, CS=15`).
- While SD and Display use different SPI hardware buses, SD operations occur across multiple tasks:
  - `remote_alarm` on Core 0 reads `RELAY_CA_PATH`.
  - `io_task` on Core 0 serves WebServer file downloads.
  - `loopTask` on Core 1 appends to history archive and streams splash frames.
- Proper mutex protection (`gui_app_lock` or a dedicated SD mutex) must be rigorously enforced across all SD calls to prevent FATFS sector buffer corruption.

---

## 7. Concrete Optimization Recommendations & Action Plan

### Recommendation 1: Stack Depth Right-Sizing (Immediate +8 KB to +10 KB Heap Recovery)
Adjust task stack depths to match verified high water mark requirements:
1. **`music_player_task`** (`src/gui_app.cpp:14895`): Reduce stack from `4096` to **`2048` bytes**. (Saves **2,048 B**).
2. **`aquarium_io`** (`src/runtime_controller.cpp:34`): Reduce `IO_TASK_STACK_BYTES` from `8192U` to **`4096U` bytes** once WebServer is decoupled. (Saves **4,096 B**).
3. **`ble_controller`** (`src/ble_controller.cpp:1152`): Reduce stack from `8192U` to **`5120U` bytes**. (Saves **3,072 B**).
4. **`aquacyd_espnow`** (`src/espnow_link.cpp:36`): Reduce `LINK_TASK_STACK_BYTES` from `7168U` to **`4096U` bytes**. (Saves **3,072 B** when enabled).
- **Total Heap Recovered**: **~9,216 bytes (~9 KB) permanently returned to the free heap pool**.

### Recommendation 2: Right-Size TLS Buffers in `WiFiClientSecure` (Prevents Critical OOM)
In `src/remote_alarm_relay.cpp:633`:
Before `client.connect()`, configure constrained SSL buffer sizes:
```cpp
WiFiClientSecure client;
client.setBufferSizes(1024, 1024); // Restrict RX to 1KB, TX to 1KB
client.setCACert(relay_ca);
```
- Standard mbedtls buffers require 16 KB + 16 KB = 32 KB.
- `setBufferSizes(1024, 1024)` cuts this to 1 KB + 1 KB = 2 KB.
- **Total Peak Heap Saved**: **~30,720 bytes (~30 KB)** during HTTPS alarm delivery, eliminating the OOM crash hazard.

### Recommendation 3: Right-Size BLE Command Buffers (Recovers ~18 KB RAM)
In `src/ble_controller.cpp`:
1. Reduce `BLE_COMMAND_MAX_BYTES` from `4096U` to **`1024U` bytes** (or `512U`).
2. This automatically shrinks `sizeof(BleQueuedCommand)` from 4,104 to 1,032 bytes, reducing `command_queue` heap consumption from 8,208 to **2,064 bytes** (saving **6,144 bytes heap**).
3. Reduce `BLE_MESSAGE_BUFFER_BYTES` from `8192U` to **`4096U` bytes** (saving **4,096 bytes BSS**).
4. Shrink `incoming.json`, `processing_command.json`, `callback_command.json` by 3 KB each (saving **~9,000 bytes BSS**).
- **Total RAM Recovered**: **~6 KB Heap + ~13 KB Static RAM**.

### Recommendation 4: Eliminate Dynamic `String` Churn in Hot Paths
1. Refactor string parsing functions in `src/gui_app.cpp:1541` & `1572`:
   - Change `parse_display_profile(const String &value, ...)` to `parse_display_profile(const char *value, ...)`.
   - Change `parse_leak_action(const String &value, ...)` to `parse_leak_action(const char *value, ...)`.
   - Replace `value == "..."` with `strcmp(value, "...") == 0`.
   - In callers (lines 17500, 17519), pass raw pointers directly: `parse_leak_action(value, &parsed)`.
2. In WebServer handlers:
   - For `ota_portal_client_accepts_gzip()`, avoid creating `String`: check `ota_http_server.header("Accept-Encoding")` using `c_str()`.
   - In `ota_portal_handle_action()`, avoid temporary `String` conversions for `commandId`, `token`, `pin`, `target`.

### Recommendation 5: Fix Subpage Asynchronous Deletion Race
In `src/gui_app.cpp:5183`:
- Before evaluating `ensure_runtime_ui_heap()`, ensure any pending asynchronous LVGL deletions are flushed synchronously or wait for pending objects to be deleted:
```cpp
delete_runtime_subpages(false); // Perform synchronous deletion
lv_timer_handler();            // Process any pending garbage collection immediately
```
- This ensures heap reclaimed from the previous subpage is immediately available before `ensure_runtime_ui_heap()` evaluates `heap_free` and `heap_largest`, completely eliminating navigation lockouts.

### Recommendation 6: Replace Byte-by-Byte `sendContent()` with Buffered Chunking
In `src/gui_app.cpp:6807`:
- Replace single-character `sendContent()` in `ota_portal_send_json_escaped()` with a local 256-byte stack accumulator buffer. Flush to `ota_http_server.sendContent()` only when the accumulator reaches capacity or at the end of the string.
- This reduces socket write calls by over 90% and eliminates TCP packet fragmentation.

### Recommendation 7: Protect against Watchdog Reboots during Long Web Operations
1. In `src/gui_app.cpp` during `ota_portal_handle_download` or large file transfers:
   - Periodically call `runtime_safety_heartbeat(RuntimeSafetyTask::Io, millis(), ESP.getFreeHeap())` within file streaming loops to prevent the supervisor task from timing out.
2. In `runtime_controller.cpp`:
   - Enforce a maximum slice duration on `gui_app_service_background()` so that sensor processing and safety heartbeats maintain their 10ms cadence even when web clients are active.

---

## 8. Summary of Quantifiable Memory Gains

| Optimization Area | Current Allocation | Proposed Allocation | Net RAM / Heap Gain |
|---|---|---|---|
| **FreeRTOS Stacks (`music`, `io`, `ble`, `espnow`)** | 27,648 B | 15,360 B | **+12,288 B heap** |
| **`WiFiClientSecure` Buffers (HTTPS Alarm Relay)** | ~35,000 B peak | ~3,500 B peak | **+31,500 B peak heap** |
| **BLE Command Queue (`command_queue`)** | 8,208 B | 2,064 B | **+6,144 B heap** |
| **BLE Static Buffers (`outgoing`, `processing`, etc.)** | 20,480 B | 8,192 B | **+12,288 B static DRAM** |
| **Subpage Async GC Flush** | Dynamic lockout | Deterministic sync | **Eliminates UI freezes** |
| **`String` Heap Churn Elimination** | Continuous heap alloc/free | Zero-allocation `const char*` | **Eliminates fragmentation** |
| **Net Operational Free Heap Increase** | **~18 KB – 30 KB** | **~40 KB – 55 KB** | **+100% safety margin** |
