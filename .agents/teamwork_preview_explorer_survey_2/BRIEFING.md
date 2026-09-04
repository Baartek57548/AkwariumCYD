# BRIEFING — 2026-09-04T09:55:00Z

## Mission
Conduct a deep read-only inspection of memory usage, dynamic allocations, and resource sizing in `firmware/cyd_controller` focusing on Requirement R2 (RAM & Heap Memory Optimization on ESP32-2432S028R without PSRAM).

## 🔒 My Identity
- Archetype: explorer
- Roles: RAM & Heap Optimization Explorer
- Working directory: c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\teamwork_preview_explorer_survey_2
- Original parent: 56ceb5af-6a46-4981-bf39-e3e616dc0656
- Milestone: Step 0 Survey

## 🔒 Key Constraints
- Read-only investigation — do NOT implement / do NOT modify source code files
- Focus on Requirement R2 (RAM & Heap Memory Optimization) on ESP32-2432S028R (CYD, no external PSRAM, internal SRAM only)
- Output analysis.md, handoff.md, progress.md in working directory
- Communicate completion and findings to parent via send_message

## Current Parent
- Conversation ID: 56ceb5af-6a46-4981-bf39-e3e616dc0656
- Updated: 2026-09-04T10:07:00Z

## Investigation State
- **Explored paths**:
  - `platformio.ini` (build profiles: `esp32dev`, `esp32dev-dev`, `esp32dev-espnow`, `esp32dev-st7789`, `native`)
  - `include/lv_conf.h` (LVGL 8.4 configuration, `LV_MEM_CUSTOM 1`)
  - `include/config.h` (HW pinning, display 10-line buffer, RGB565 stream rows)
  - `src/main.cpp` (setup, loop, task starts, telemetry propagation)
  - `src/hal_display.cpp` (dual 10-line DMA draw buffers, LovyanGFX, touch filter)
  - `src/hal_sd.cpp` (VSPI bus, FATFS `max_files = 5`)
  - `src/runtime_controller.cpp` (`aquarium_io` task, 8KB stack, `gui_app_service_background`)
  - `src/ble_controller.cpp` (`ble_controller` task, 8KB stack, 20KB BSS buffers, 8.2KB command queue)
  - `src/remote_alarm_relay.cpp` (`remote_alarm` task, 8KB stack, unbounded TLS buffer hazard)
  - `src/runtime_safety.cpp` (`safety_supervisor` task, 4KB stack, 4000ms watchdog timeout)
  - `src/gui_app.cpp` (18,413 lines: LVGL tree, subpage lifecycle, theme rebuild, WebServer handlers, String churn, `sendContent` micro-chunking)
  - `lib/aquarium_domain` (verified static structures: `IdempotencyLedger`, `AdminSessionManager`, `WebActivityTracker`)
- **Key findings**:
  - Static DRAM is 120,188 bytes (36.7% of 320 KB), leaving only ~207 KB raw heap.
  - After Wi-Fi (~60KB), NimBLE (~35KB), task stacks (~44KB), and LVGL widgets (~40KB), steady-state free heap is only ~18 KB - 32 KB.
  - Top OOM risk: `WiFiClientSecure` in `remote_alarm_relay.cpp` lacks `setBufferSizes()`, causing 32KB mbedtls buffer allocation on alarm events.
  - Subpage navigation freeze: Async deletion (`delete_obj_async`) leaves memory un-reclaimed when immediate navigation occurs, failing `ensure_runtime_ui_heap()`.
  - Watchdog reboot risk: WebServer runs inside `io_task` under `gui_mutex`; slow web operations > 4s trigger supervisor restart.
  - Stack over-allocation: `music_player_task`, `aquarium_io`, and `ble_controller` can yield ~10 KB heap recovery.
  - BLE buffer over-allocation: `BLE_COMMAND_MAX_BYTES` (4KB) wastes >18 KB RAM across queue and BSS.
- **Unexplored areas**: None for survey Step 0; full scope covered.

## Key Decisions Made
- Confirmed display double-buffering (dual 10 lines, 12.8 KB DMA) is already optimal and requires no changes.
- Formulated concrete replacement strategies: TLS buffer clamping to 1KB, task stack right-sizing, BLE queue/buffer downsizing, `String` elimination, and WebServer output chunking.
- Completed comprehensive `analysis.md` and 5-component `handoff.md`.

## Artifact Index
- c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\teamwork_preview_explorer_survey_2\DISPATCH.md — Received instructions
- c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\teamwork_preview_explorer_survey_2\BRIEFING.md — Persistent context & memory
- c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\teamwork_preview_explorer_survey_2\progress.md — Liveness heartbeat & progress log
- c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\teamwork_preview_explorer_survey_2\analysis.md — Comprehensive RAM & Heap optimization analysis
- c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\teamwork_preview_explorer_survey_2\handoff.md — 5-component handoff report
