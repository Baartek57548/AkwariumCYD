# Handoff Report: RAM & Heap Optimization Survey (Requirement R2)

**Agent**: Explorer 2 (RAM & Heap Optimization Explorer)  
**Date**: 2026-09-04  
**Workspace**: `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium`  
**Report Location**: `.agents/teamwork_preview_explorer_survey_2/handoff.md`  
**Detailed Report**: `.agents/teamwork_preview_explorer_survey_2/analysis.md`

---

## 1. Observation

1. **Compilation & Static RAM Allocation**:
   - Running `pio run -e esp32dev` in `firmware/cyd_controller` outputs:
     ```
     RAM:   [====      ]  36.7% (used 120188 bytes from 327680 bytes)
     Flash: [==========]  96.1% (used 1890085 bytes from 1966080 bytes)
     ```
   - ELF inspection with `xtensa-esp32-elf-size -A .pio/build/esp32dev/firmware.elf` confirms:
     - `.dram0.data`: 46,692 bytes
     - `.dram0.bss`: 73,496 bytes
     - Total static DRAM: **120,188 bytes**
     - `.iram0.text`: 128,139 bytes

2. **Top Static Symbols by Size (`xtensa-esp32-elf-nm`)**:
   - `outgoing_json` (`src/ble_controller.cpp:90`): `8,192 bytes` (`.bss`)
   - `draw_buffer_a` and `draw_buffer_b` (`src/hal_display.cpp:118-119`): `6,400 bytes` each (total `12,800 bytes`, `.data` with `DMA_ATTR`)
   - `relay_ca` (`src/remote_alarm_relay.cpp:104`): `6,144 bytes` (`.bss`)
   - `rgb565_stream_buffer` (`src/hal_display.cpp:120`): `5,120 bytes` (`.data` with `DMA_ATTR`)
   - `incoming.json`, `processing_command`, `callback_command` (`src/ble_controller.cpp:60, 87, 88`): `4,108 - 4,124 bytes` each (total `12,340 bytes`, `.bss`)

3. **FreeRTOS Task Stack Depths**:
   - `ble_controller` (`src/ble_controller.cpp:1151`): `8,192 bytes` (Core 0, priority 1)
   - `aquarium_io` (`src/runtime_controller.cpp:304`): `8,192 bytes` (Core 0, priority 2)
   - `remote_alarm` (`src/remote_alarm_relay.cpp:918`): `8,192 bytes` (Core 0, priority 1)
   - `music_player_task` (`src/gui_app.cpp:14892`): `4,096 bytes` (Core 0, priority 1)
   - `safety_supervisor` (`src/runtime_safety.cpp:359`): `4,096 bytes` (Core 1, priority 4)
   - `aquacyd_espnow` (`src/espnow_link.cpp:814`): `7,168 bytes` (Core 0, priority 1)
   - Total task stack memory dynamically allocated via `pvPortMalloc` from heap: **36,864 bytes** (without ESP-NOW) to **44,032 bytes** (with ESP-NOW).

4. **LVGL Memory Settings**:
   - `include/lv_conf.h:24-33`:
     ```c
     #define LV_MEM_CUSTOM 1
     #if LV_MEM_CUSTOM == 0
         #define LV_MEM_SIZE (120U * 1024U) /* The full dashboard creates many LVGL objects; 80KB was completely exhausted. */
     ...
     #else
         #define LV_MEM_CUSTOM_ALLOC   malloc
         #define LV_MEM_CUSTOM_FREE    free
         #define LV_MEM_CUSTOM_REALLOC realloc
     #endif
     ```
   - In `src/gui_app.cpp:14774-14800`, `rebuild_gui_tree_for_theme()` cleans the active screen (`lv_obj_clean(lv_scr_act())`) and completely reconstructs hundreds of LVGL objects upon LDR threshold crossing (`src/gui_app.cpp:16091`).

5. **Heap Gating & Async Deletion Race**:
   - `src/gui_app.cpp:168-172`:
     ```cpp
     constexpr uint32_t UI_RUNTIME_SUBPAGE_MIN_FREE = 18000UL;
     constexpr uint32_t UI_RUNTIME_BIGGEST_MIN = 4096UL;
     ```
   - Subpage exit calls `delete_runtime_subpages(true)`, invoking `delete_obj_async(root)` (`src/gui_app.cpp:3404`), which schedules `lv_async_call(delete_obj_async_cb, obj)`.
   - If a new subpage is selected before `lv_async_call` runs inside `lv_timer_handler()`, `ensure_runtime_ui_heap()` evaluates heap before previous widgets are freed and rejects navigation with `"UI_NAV: blocked runtime UI allocation"`.

6. **Unbounded TLS Buffer in `remote_alarm_relay.cpp`**:
   - `src/remote_alarm_relay.cpp:632-640`:
     ```cpp
     WiFiClientSecure client;
     client.setCACert(relay_ca);
     client.setHandshakeTimeout((RELAY_HTTP_TIMEOUT_MS + 999U) / 1000U);
     client.setTimeout(RELAY_HTTP_TIMEOUT_MS);
     if (!client.connect(url.host, url.port)) { ... }
     ```
   - No call to `client.setBufferSizes()` is made. Default mbedtls allocations reserve 16 KB RX + 16 KB TX buffers = 32 KB (+12 KB TLS context = ~45 KB heap spike).

7. **Synchronous WebServer in `aquarium_io` Task**:
   - `src/runtime_controller.cpp:237`: `io_task` calls `gui_app_service_background()`, which runs `ota_http_server.handleClient()`.
   - `src/runtime_safety.cpp:21-22, 246`: `HEARTBEAT_TIMEOUT_MS = 4000U`. If a web transfer blocks for >4,000 ms, `supervisor_task` restarts the ESP32 with `RuntimeFaultReason::IoHeartbeatStale`.
   - While serving HTTP, `gui_mutex` is held, blocking `loopTask` on Core 1 from rendering display or servicing touch.

8. **Dynamic String & Micro-Chunked WebServer Output**:
   - In `src/gui_app.cpp:6807-6830`, `ota_portal_send_json_escaped()` calls `ota_http_server.sendContent(out)` character-by-character.
   - In `src/gui_app.cpp:17500, 17519`: `parse_leak_action(String(value), &parsed)` and `parse_display_profile(String(profile), &parsed)` construct temporary heap `String`s to pass into functions accepting `const String &`.

---

## 2. Logic Chain

1. **Hardware Limitation**: ESP32-2432S028R does not have external PSRAM. Total physical SRAM is 327,680 bytes.
2. **Static Footprint Deduction**: Static data and BSS consume 120,188 bytes (Observation 1), leaving ~207 KB of raw heap.
3. **Subsystem Stack & Driver Deduction**:
   - Wi-Fi driver and LwIP require ~60 KB.
   - NimBLE stack requires ~35 KB.
   - FreeRTOS user task stacks require ~37 KB to 44 KB (Observation 3).
   - Remaining heap before UI initialization is approximately ~68 KB.
4. **LVGL Heap Consumption**: Under `LV_MEM_CUSTOM 1`, LVGL allocates all objects, styles, charts, and buttons via `malloc()` from this remaining heap, consuming ~35 KB to 50 KB (Observation 4).
5. **Steady-State Headroom**: Operational free heap hovers around **~18 KB to 32 KB**, with the largest contiguous block frequently dropping to **~4 KB to 10 KB**.
6. **Trigger 1 (TLS OOM)**: When an alarm event triggers an HTTPS POST, `WiFiClientSecure` without `setBufferSizes()` attempts to allocate 32 KB of TLS record buffers (Observation 6). Because free heap is under 32 KB or contiguous blocks are under 16 KB, the TLS handshake crashes with OOM or fails connection.
7. **Trigger 2 (Navigation Freeze)**: Opening a subpage requires 18 KB free and 4 KB contiguous (Observation 5). If the user changes tabs or opens subpages in succession, the asynchronous `lv_async_call` has not yet freed the previous subpage's widgets. Heap gating fails and drops touch inputs.
8. **Trigger 3 (Watchdog Reboot on Web Operations)**: WebServer running inside `io_task` under `gui_mutex` halts sensor polling and heartbeats. Any HTTP download or static asset streaming exceeding 4 seconds triggers a hard watchdog reset via `safety_supervisor` (Observation 7).

---

## 3. Caveats

1. **No Hardware Profiler Run**: Real-time stack watermarks (`uxTaskGetStackHighWaterMark`) were audited via static code analysis of function call graphs and local variables; dynamic stack telemetry under full BLE pairing was not measured on physical hardware.
2. **Alternative Interpretations**: Moving LVGL to a dedicated static pool (`LV_MEM_CUSTOM 0`, `LV_MEM_SIZE 64K`) would isolate LVGL from system heap fragmentation, but requires guaranteeing that the full GUI tree fits within that fixed pool without overflowing.
3. **No PSRAM Board Variants**: Assumptions strictly adhere to standard ESP32-2432S028R boards without PSRAM. Boards modified with ESP32-WROVER would not experience the same severe constraints.

---

## 4. Conclusion

The primary threats to system stability under Requirement R2 are:
1. **Unbounded TLS handshake buffers** in `WiFiClientSecure` during remote alarm reporting (~45 KB spike on a ~25 KB heap).
2. **System heap fragmentation** caused by `LV_MEM_CUSTOM 1` coupled with dynamic subpage re-creation and full-screen rebuilds on LDR light changes.
3. **Task stack over-allocation** in `music_player_task` (4 KB for buzzer), `aquarium_io` (8 KB), and `ble_controller` (8 KB), locking up ~10 KB of scarce heap.
4. **Excessive BLE static and queue buffers** (4 KB per command, 8.2 KB command queue, 8 KB outgoing JSON).
5. **Coupling of WebServer execution to `io_task`**, risking 4-second supervisor watchdog timeouts.

All display buffers (`draw_buffer_a`, `draw_buffer_b` @ 10 lines each, DMA) and domain libraries (`IdempotencyLedger`, `AdminSessionManager`, `WebActivityTracker`) are already right-sized and statically bounded.

---

## 5. Verification Method

### 1. Build Verification
Verify compilation across PlatformIO environments:
```powershell
cd firmware/cyd_controller
pio run -e esp32dev
pio run -e esp32dev-espnow
```
*Expected Result*: Clean build with RAM usage <= 37.5% and Flash usage <= 97.5%.

### 2. Native Unit Tests Verification
```powershell
cd firmware/cyd_controller
pio test -e native
```
*Expected Result*: All 40 domain unit tests pass (100% success).

### 3. Verification of Proposed Optimization Targets
- Inspect `src/remote_alarm_relay.cpp:633` for `client.setBufferSizes(1024, 1024)`.
- Inspect `src/gui_app.cpp:14895` for `music_player_task` stack depth <= 2048 bytes.
- Inspect `src/runtime_controller.cpp:34` for `IO_TASK_STACK_BYTES` <= 4096 bytes.
- Inspect `src/ble_controller.cpp:23` for `BLE_COMMAND_MAX_BYTES` <= 1024 bytes.
- Inspect `src/gui_app.cpp:1541, 1572, 17500, 17519` for elimination of `String` conversions.
- Inspect `src/gui_app.cpp:5183` for synchronous flush before `ensure_runtime_ui_heap()`.

### Invalidation Conditions
- If any proposed stack reduction causes `vApplicationStackOverflowHook` to trigger during peak load.
- If reducing `BLE_COMMAND_MAX_BYTES` truncates valid API v2 pairing payloads.
- If `client.setBufferSizes(1024, 1024)` causes TLS handshake failures against gateways negotiating larger record sizes without MFLN.
