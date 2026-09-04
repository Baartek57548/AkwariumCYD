# Master Execution Plan: ESP32-CYD Aquarium Controller Audit & Optimization

## Objective
Comprehensive audit of firmware logic, stability, RAM & heap optimization, SD card subsystem, and Web/API consistency for ESP32-CYD aquarium controller, ensuring full compliance with R1, R2, R3, build targets, and unit/E2E test suites.

## Step 0: Initial Codebase Survey & Requirement Mapping
- Explorer 1 (survey_firmware_logic): Analyze firmware architecture, sensors, relays, PWM, FreeRTOS tasks, state machines, BLE/ESP-NOW, error handling (R1).
- Explorer 2 (survey_memory_ram): Analyze memory footprint, dynamic allocations, heap usage, LVGL display buffers, FreeRTOS task stacks, potential leaks/fragmentation (R2).
- Explorer 3 (survey_sd_web): Analyze SD card driver, SPI bus arbitration, Web server & REST API, Web asset structure, build tools and test suites (R3, PlatformIO, npm).

## Step 1: Synthesis & Project Structure
- Synthesize survey findings into PROJECT.md (Architecture, Feature Inventory, Milestones, Code Layout, Interface Contracts).
- Set up TEST_INFRA.md for native and E2E verification tracking.

## Step 2: Implementation Milestones
- Milestone 1: Firmware Logic & Stability (R1) - Fix race conditions, logic errors, state machines, sensors/actuators.
- Milestone 2: RAM & Heap Memory Optimization (R2) - Eliminate dynamic allocations in hot loops, optimize LVGL buffers, resize task stacks safely.
- Milestone 3: SD Card & Web/API Consistency (R3) - Robust error handling for SD missing/corrupt/SPI busy, Web API sync, web assets.
- Milestone 4: Verification & Coverage Hardening - PlatformIO builds (esp32dev), native unit tests (pio test -e native), npm test suites (
pm run test:api, 
pm run build:web-assets), Challenger and Forensic Auditor sign-off.
