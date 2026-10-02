# Firmware Logic & Stability Analysis (Requirement R1 Survey)

**Target System**: `firmware/cyd_controller` (ESP32-WROOM / CYD - Cheap Yellow Display)  
**Date**: 2026-09-04  
**Investigator**: Explorer 1 (Firmware Logic & Stability Explorer)  
**Baseline Verification**:
- Native Unit Tests (`pio test -e native`): **40/40 PASSED** (4.80s)
- Target Firmware Compilation (`pio run -e esp32dev`): **SUCCESS** (Flash: 96.1% [1.89 MB/1.97 MB], RAM: 36.7% [120 KB static])

---

## 1. Executive Summary

A deep, read-only architectural investigation of `firmware/cyd_controller` was conducted across hardware abstraction layers (HAL), domain automation rules, FreeRTOS multitasking, safety supervisory subsystems, external communications (BLE, ESP-NOW, HTTPS), and unit test coverage.

### Key Strengths:
1. **Modular Domain Logic**: Automation rules (`aquarium_automation.cpp`), schedule engine (`aquarium_schedule.cpp`), Aquael light controller (`aquael_light_controller.cpp`), and control mode resolution (`control_modes.cpp`) are strictly decoupled from hardware APIs and compile cleanly in native x86/x64 test environments.
2. **Dual-Layer Safety Supervisor**: A dedicated high-priority FreeRTOS task (`safety_supervisor`, Core 0, priority 3) monitors software task heartbeats, FreeRTOS heap exhaustion, and triggers an emergency hardware shutdown pin.
3. **Robust Calibration Store**: 2-point and 3-point polynomial sensor calibration (`sensor_calibration_store.cpp`) includes CRC32 integrity verification, sane bounds checking, and NVS persistence.

### Critical Vulnerabilities & Architectural Defects:
1. **ATO & CO2 Safety Limits Bypassed by Manual Override**: In `gui_app.cpp:15366-15457`, safety runtime limits (`water_timeout_seconds` and `co2_max_time_minutes`) reset their elapsed timers whenever automatic demand is false. Downstream manual overrides force relays ON without any watchdog enforcement, risking indefinite water filling (tank overflow/flood) or lethal CO2 overdosing for up to 24 hours.
2. **Feeding Trigger Skipped via `sc == 0` Second Matching**: In `gui_app.cpp:15583` and `aquarium_schedule.cpp:66`, scheduled feeding checks `now.second == 0`. Telemetry queue transfers and `gui_mutex` contention introduce jitter > 50ms, causing the clock to skip second 0 (e.g. 10:30:59 -> 10:31:01), resulting in missed daily feedings.
3. **Deadlock Invisibility in FreeRTOS Safety Supervisor**: `io_task` and `loop()` (UI) feed their heartbeats unconditionally at the top of their respective loops *before* acquiring `gui_mutex`. If `gui_mutex` is permanently locked or heavily contended (e.g. during slow HTTP streaming or BLE transfers), background services fail to run, yet heartbeats are fed continuously, rendering the safety watchdog blind to system lockups.
4. **Light Sleep Triggers Watchdog Panic**: `light_sleep_authorized` calls `esp_light_sleep_start()` for 10 seconds. Upon waking, elapsed `millis()` exceeds the 4-second heartbeat threshold in `safety_supervisor`, causing an immediate software watchdog reset. Furthermore, it improperly invokes `digitalWrite` on GPIO 21 (which is managed by LEDC PWM).
5. **Data Corruption on Shared SD Card / SPI Bus**: `hal_sd.cpp` lacks a synchronization mutex. Core 1 writes sensor history (`history_archive_append_sample`) while Core 0 reads SD files for web server streaming and reads the root CA certificate for HTTPS webhooks, creating a race condition that corrupts FatFs structures.
6. **I2C Bus Contention from Polling ADS1115**: `hal_adc.cpp` holds `hal_i2c_bus_lock` in a busy/delay wait loop for up to 20ms during ADC conversion, stalling MCP23017 relay updates and digital input scans.
7. **Sensor Inventory Clarification**: The codebase contains **no DHT sensor implementation**. Temperature sensing is exclusively implemented via Maxim/Dallas DS18B20 1-Wire sensors on GPIO 17.

---

## 2. Sensor Reading Logic & Hardware Abstraction

### 2.1 Temperature Sensing (DS18B20 on 1-Wire)
- **Implementation**: `hal_onewire_bus.cpp` / `hal_onewire_bus.h`
- **Bus Configuration**: Single-pin bitbang 1-Wire bus on GPIO 17 (`PIN_ONEWIRE` in `config.h`).
- **Addressing & ROM Matching**:
  - `hal_onewire_bus_read_temperature()` uses skip-ROM (`0xCC`) if single sensor is attached, or match-ROM (`0x55`) with 64-bit address.
  - Supports family codes `0x28` (DS18B20), `0x10` (DS18S20), and `0x22` (DS1822).
- **Integrity & Filtering**:
  - Full 9-byte scratchpad read followed by Dallas Maxim 8-bit CRC validation (`CRC8 = X^8 + X^5 + X^4 + 1`).
  - Rejects power-on reset value (`85.0°C`) if scratchpad count indicates default state.
  - Range validation strictly enforces `-10.0°C <= temp <= 60.0°C`. Out-of-bounds or CRC failures return `NAN`.
- **Conversion Delay**: Requires 750ms for 12-bit conversion. Handled asynchronously by triggering conversion on cycle $N$ and reading scratchpad on cycle $N+1$ to avoid blocking the FreeRTOS task.

### 2.2 Analog-to-Digital Conversion (ADS1115 & Internal ADC)
- **External ADC (ADS1115)**:
  - **Implementation**: `hal_adc.cpp` / `hal_adc.h` on I2C address `0x48`.
  - **Channels**:
    * Channel 0 (A0): Analog pH sensor (`PH_PIN` analog signal).
    * Channel 1 (A1): Analog Electrical Conductivity (EC / TDS) sensor.
    * Channel 2 (A2): Water Level analog sensor (or differential voltage).
  - **I2C Bus Lock Contention Vulnerability**:
    ```cpp
    // hal_adc.cpp:166-203
    if (hal_i2c_bus_lock(portMAX_DELAY)) {
        // writes config register to trigger single-shot conversion
        // then enters polling loop while HOLDING the bus lock:
        while (!(read_register(ADS1115_REG_CONFIG) & 0x8000)) {
            vTaskDelay(pdMS_TO_TICKS(1)); // Holds bus lock across FreeRTOS context switches!
        }
        hal_i2c_bus_unlock();
    }
    ```
    *Impact*: While waiting 8ms-20ms for conversion, `io_task` locks out any other task attempting to control MCP23017 relays or read button inputs.
- **Internal ADC (LDR / Ambient Light)**:
  - **Implementation**: GPIO 34 (`PIN_LDR_ADC`).
  - Uses `analogReadMilliVolts()` with 11dB attenuation (`ADC_ATTEN_DB_11`) and 8-sample multisampling to smooth ESP32 ADC non-linearity.

### 2.3 Water Level & Digital Inputs (MCP23017)
- **Implementation**: `hal_mcp23017.cpp` on I2C address `0x27` (Port B pins 0..7).
- **Inputs**:
  - `PIN_MCP_FLOAT_MIN` (PB0): Low water float switch (sump low level).
  - `PIN_MCP_FLOAT_MAX` (PB1): High water float switch (sump high / overflow).
  - `PIN_MCP_OPTICAL_LEVEL` (PB2): Optical water level sensor.
  - `PIN_MCP_LEAK_DETECTOR` (PB3): Sump / floor water leak sensor.
- **Debouncing**: Software integrator debounce (100ms stable state required before state transition).
- **Polarity Inversion**: MCP23017 enables internal 100kΩ pullups (`GPPU`). Configurable active-low / active-high inversion flag in `sensor_config_t`.

### 2.4 Sensor Calibration Subsystem
- **Implementation**: `sensor_calibration.cpp`, `sensor_calibration_store.cpp`
- **pH Calibration**: Supports 2-point (pH 4.00, pH 7.00) or 3-point (pH 4.00, pH 7.00, pH 10.01) linear/quadratic regression.
- **Temperature Compensation**: Corrects pH reading via Nernst equation slope adjustment:
  $$pH_{compensated} = pH_{raw} \times \frac{T_K}{298.15K}$$
- **Integrity**: Non-Volatile Storage (NVS) records are protected by CRC32. Corrupted records fall back to factory default slope/offset (`default_ph_cal`).

---

## 3. Relay & Actuator Control Architecture

### 3.1 Relay Bank (MCP23017 Port A)
- **Implementation**: `hal_mcp23017.cpp:112-160`
- **Output Channels**:
  | Logical Relay ID | Function | Default Fail-Safe State |
  | :--- | :--- | :--- |
  | `RELAY_FILTER` (0) | Primary Filtration Pump | ON (Active) |
  | `RELAY_HEATER` (1) | Submersible Heater | OFF (Inactive) |
  | `RELAY_CHILLER` (2) | Water Cooler / Fan | OFF (Inactive) |
  | `RELAY_CO2` (3) | CO2 Solenoid Valve | OFF (Inactive) |
  | `RELAY_AIR` (4) | Aerator / Air Pump | OFF (Inactive) |
  | `RELAY_LIGHT_MAIN` (5)| Main Aquarium Light | OFF (Inactive) |
  | `RELAY_LIGHT_AUX` (6) | Sump / Refugium Light | OFF (Inactive) |
  | `RELAY_ATO` (7) | Auto Top-Off Dosing Pump | OFF (Inactive) |
- **Active-Low Inversion**:
  Relay driver boards are active-low. `hal_mcp23017_set_relays(uint8_t logical_mask)` executes:
  ```cpp
  uint8_t physical_byte = ~(logical_mask);
  ```
  - *Power-on Glitch Protection*: `hal_mcp23017_init()` writes `0xFF` to `OLATA` *before* configuring `IODIRA` as outputs, preventing relay chatter during boot.

### 3.2 Aquael Light Controller (Pulse-Based State Machine)
- **Implementation**: `aquael_light_controller.cpp` / `aquael_light_controller.h`
- **Mechanism**: Aquael Leddy lights switch modes (Day -> Daybreak -> Night) by cycling AC power off and on within a 1-second pulse window:
  - Switching from `Day` to `Daybreak`: 1 pulse.
  - Switching from `Daybreak` to `Night`: 1 pulse.
  - Switching from `Night` to `Day`: 1 pulse (or cycle power off for > 5s).
- **State Machine Guard**: Maintains internal state `AquaelMode current_mode`. When commanded to change state, it asserts an asynchronous pulse queue with minimum power-off duration ($t_{off} \approx 600\text{ms}$) and power-on dwell ($t_{on} \approx 800\text{ms}$), blocking conflicting commands until the transition completes.

### 3.3 Interlocks & Anti-Chatter Protections
- **Heater vs. Chiller Conflict Prevention**:
  ```cpp
  // aquarium_automation.cpp:25-50
  if (temp_c < cfg.temp_target - cfg.temp_hysteresis) {
      heater_demand = true;
      cooler_demand = false;
  } else if (temp_c > cfg.temp_target + cfg.temp_hysteresis) {
      heater_demand = false;
      cooler_demand = true;
  } else {
      // Deadband: keep current state or turn off if both active
  }
  ```
  - Strict mutual exclusion: Code guarantees `!(heater_on && cooler_on)`. If both are asserted, `cooler_demand` is dropped and a safety warning is flagged.
- **Minimum Cycle Timers**: Relays enforce minimum off-time (e.g. 180s for chiller compressor) to prevent short-cycling.

---

## 4. Schedules, Operating Modes & Safety Flaws

### 4.1 Operating Modes
The system implements five mutually exclusive modes (`control_modes.h`):
1. **AUTO**: Fully autonomous execution of sensor-driven automation and diurnal schedules.
2. **MANUAL**: User-defined manual relay overrides with timeout decay.
3. **FEEDING**: Filters and wavemakers shut off for $N$ minutes; light dimmed; auto-restores to AUTO.
4. **MAINTENANCE**: All pumps, heaters, and ATO disabled during water changes.
5. **EMERGENCY**: System-wide fail-safe shutdown triggered by leak sensor, thermal runaway, or software watchdog.

### 4.2 Defect 1: Safety Timeout Bypass on Manual Overrides (ATO & CO2)
- **Location**: `firmware/cyd_controller/src/gui_app.cpp:15366-15457`
- **Mechanism**:
  ```cpp
  // Step 1: Automatic logic evaluates runtime limiters:
  if (automation_co2_demand) {
      if (co2_started_ms == 0) co2_started_ms = now_ms;
      if (now_ms - co2_started_ms > co2_max_limit_ms) {
          runtime.co2On = false; // Tripped!
      } else {
          runtime.co2On = true;
      }
  } else {
      co2_started_ms = 0; // RESET TIMER IF AUTOMATIC DEMAND IS FALSE!
      runtime.co2On = false;
  }

  // Step 2: Manual override resolution happens AFTERWARDS:
  control_modes.resolve(&runtime);
  // If user turned CO2 or ATO relay ON manually:
  // runtime.co2On is forced to TRUE!
  ```
- **Consequence**:
  Because `co2_started_ms` and `ato_started_ms` are continuously reset to 0 in the `else` branch, the safety timeout never triggers during manual mode. If a user turns on the ATO pump or CO2 solenoid via the UI or Web API with the default 24-hour manual override timeout, the pump will run continuously until the reservoir is empty or the tank overflows.

### 4.3 Defect 2: Missed Feeding Schedules due to Exact `sc == 0` Comparison
- **Location**: `gui_app.cpp:15583` and `aquarium_schedule.cpp:66`
- **Code**:
  ```cpp
  if (hr == feed_hr && mn == feed_min && sc == 0) {
      trigger_feeding_mode();
  }
  ```
- **Mechanism**:
  `gui_app` telemetry loop is scheduled every 1000ms. If `gui_mutex` is held by a web server request or LVGL redraw for >50ms, the next iteration ticks at second 1 instead of second 0.
- **Consequence**: The condition `sc == 0` evaluates to false, completely skipping scheduled feeding for that day.
- **Solution**: Implement edge-triggered minute state tracking:
  ```cpp
  static int last_feeding_minute = -1;
  int current_minute = hr * 60 + mn;
  if (current_minute == (feed_hr * 60 + feed_min) && last_feeding_minute != current_minute) {
      last_feeding_minute = current_minute;
      trigger_feeding_mode();
  }
  ```

---

## 5. FreeRTOS Tasks, Concurrency & Synchronization

### 5.1 FreeRTOS Task Matrix

| Task Name | Function | Core | Priority | Stack Size | Periodicity | Responsibilities |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| `loopTask` (UI) | `src/main.cpp` / `gui_app` | 1 | 1 | 8192 B | ~20ms | LVGL display rendering, touch inputs, Web server background service, BLE updates |
| `io_task` | `src/runtime_controller.cpp` | 0 | 2 | 4096 B | 100ms | 1-Wire DS18B20 reads, ADS1115 ADC conversion, MCP23017 relay & GPIO updates |
| `safety_task`| `src/runtime_safety.cpp` | 0 | 3 | 3072 B | 100ms | Software heartbeat monitoring, emergency pin check, heap check, fail-safe latch |

### 5.2 Synchronization & Inter-Task Communication
1. **`gui_mutex` (Recursive Mutex)**:
   - Primary lock protecting application state, UI components, settings, and network operations.
   - Acquired via `gui_app_lock(timeout_ms)` and released via `gui_app_unlock()`.
2. **`hal_i2c_bus_lock` (Binary Mutex)**:
   - Synchronizes access to the hardware I2C bus between MCP23017 and ADS1115.
3. **`runtime_telemetry_queue` (Queue Handle, 1 element, overwrite)**:
   - Transfers sensor telemetry snapshot (`runtime_telemetry_snapshot_t`) from `io_task` (Core 0) to `loopTask` (Core 1).

### 5.3 Concurrency & Stability Defects

#### Defect 3: Deadlock Invisibility in `safety_supervisor`
- **Location**: `src/runtime_controller.cpp:234` and `src/main.cpp:281`
- **Mechanism**:
  ```cpp
  // io_task (Core 0):
  void io_task(void* arg) {
      while (true) {
          runtime_safety_heartbeat(TASK_ID_IO); // HEARTBEAT FED UNCONDITIONALLY HERE
          
          if (!gui_app_lock(pdMS_TO_TICKS(50))) {
              vTaskDelay(pdMS_TO_TICKS(10));
              continue; // Lock failed, but heartbeat was already fed!
          }
          // Do critical sensor processing...
          gui_app_unlock();
          vTaskDelay(pdMS_TO_TICKS(100));
      }
  }
  ```
- **Impact**:
  If a deadlock or high lock contention occurs on `gui_mutex` (e.g. Web server handling slow HTTP transfer while holding lock), `io_task` and `loopTask` will continually fail `gui_app_lock`, skip processing, loop around, and feed their heartbeats. The safety supervisor perceives both tasks as healthy while all relay switching, sensor processing, and user controls are completely frozen.

#### Defect 4: Light Sleep Watchdog Panic & GPIO Conflict
- **Location**: `gui_app.cpp:5731-5743`
- **Code**:
  ```cpp
  void light_sleep_authorized() {
      digitalWrite(21, LOW); // GPIO 21 is LEDC Backlight PWM channel!
      esp_sleep_enable_timer_wakeup(10000000ULL); // 10 seconds sleep
      esp_light_sleep_start();
  }
  ```
- **Impact**:
  1. `esp_light_sleep_start()` suspends CPU execution for 10 seconds.
  2. FreeRTOS timers and task scheduling halt during light sleep. Upon waking, `millis()` reflects the 10-second leap.
  3. The `safety_supervisor` task detects `millis() - last_heartbeat > 4000ms`, flags `UiHeartbeatStale`, and immediately issues a software reboot.
  4. Writing `digitalWrite(21, LOW)` detaches the pin from the ESP32 LEDC PWM peripheral, corrupting backlight brightness control upon wake.

#### Defect 5: Unprotected Concurrent SD Card Access
- **Location**: `hal_sd.cpp`, `gui_app.cpp:16420`, `remote_alarm_relay.cpp:344`
- **Mechanism**:
  - `history_archive_append_sample()` runs on Core 1 every 60 seconds, appending data to `/sdcard/history.csv`.
  - `ota_http_server.streamFile()` runs on Core 0 when a web client requests history downloads or UI assets.
  - `remote_alarm_relay` reads `/sdcard/ca.pem` on Core 0 during alert dispatch.
  - There is **no mutex** around the SD SPI bus or FatFs API calls.
- **Impact**: Concurrent open/read/write calls to the underlying SPI FatFs driver trigger silent filesystem corruption and hardware lockups.

#### Defect 6: Dead Code in Event Pub-Sub System
- **Location**: `events.cpp`
- **Observation**:
  - `events.cpp` dynamically allocates `sample_queue` (16 elements) and `command_queue` (8 elements) on startup.
  - `events_publish_sample()` is called every second from `main.cpp` for 6 distinct metrics.
  - In `main.cpp:drain_sensor_events()`, the queue is popped and immediately discarded.
  - `events_subscribe()` and `events_publish_command()` have **zero callers** in the entire codebase.
- **Impact**: Unnecessary FreeRTOS queue overhead and CPU churn on every sensor cycle.

---

## 6. Communication Subsystems

### 6.1 BLE Controller (`ble_controller.cpp`)
- **Stack**: NimBLE (Apache NimBLE for ESP32).
- **Service UUID**: Custom 128-bit UUID for Aquarium Service.
- **Characteristics**:
  - Telemetry Characteristic (Read/Notify): Transmits packed binary sensor state.
  - Command Characteristic (Write): Receives remote control commands.
- **Security Assessment**:
  - Unauthenticated GATT write permissions: Any device within Bluetooth range can write to the command characteristic and toggle high-power AC relays (heaters, pumps) without pairing or PIN authentication.

### 6.2 ESP-NOW Link (`espnow_link.cpp`)
- **Protocol**: 2.4 GHz ESP-NOW wireless frames.
- **Payload Framing**:
  ```cpp
  struct espnow_packet_t {
      uint8_t magic;      // 0xA5
      uint8_t msg_type;   // Telemetry / Satellite Node
      uint16_t seq_num;
      uint8_t payload[64];
      uint32_t crc32;
  };
  ```
- **Validation**: Full CRC32 check on reception. Rejects out-of-sequence packets to prevent replay attacks.

### 6.3 Remote Alarm Relay (`remote_alarm_relay.cpp`)
- **Function**: Dispatches critical alarms (thermal runaway, leak detected) to an HTTPS webhook endpoint.
- **Security**:
  - HMAC-SHA256 request signing using pre-shared secret key.
  - Validates TLS certificate using CA stored in flash/SD.

---

## 7. Native Unit Test Suite & Coverage Gaps

### 7.1 Current Test Baseline
- **Location**: `test/test_native_domain/test_main.cpp`
- **Execution**: `pio test -e native` -> **40 passed, 0 failed, 4.80s**.
- **Tested Modules**:
  1. `aquarium_automation`: Hysteresis logic, heater/cooler deadband, ATO water level interlocks.
  2. `aquarium_schedule`: Time-of-day calculation, diurnal ramp percentages.
  3. `aquael_light_controller`: Pulse count tracking, state transition timing.
  4. `control_modes`: Mode prioritization (Emergency > Maintenance > Feeding > Manual > Auto).
  5. `sensor_calibration`: 2-point and 3-point calibration curve computation.

### 7.2 Critical Unit Test Gaps (Actionable for Phase 1)

| Subsystem / Function | Missing Test Case | Risk Addressed |
| :--- | :--- | :--- |
| **HAL DS18B20 CRC8** | CRC8 validation polynomial against known Maxim test vectors (`0x00`, `0x55`, valid scratchpad, corrupt scratchpad). | Undetected temperature read errors causing false thermal runaway alarms. |
| **HAL Relay Bit Inversion** | Active-low bit inversion logic (`logical_to_physical_relay_byte`). | Inverted relay states causing heater/chiller to run in reverse. |
| **Safety Limiter Overrides** | Verify safety limiters (ATO timeout & CO2 max duration) remain active and enforced even when manual override is engaged. | Tank flood or CO2 poisoning during manual override. |
| **Minute Boundary Trigger** | Verify schedule triggering across jittered second intervals (e.g. 59 -> 01 transition). | Skipped daily feeding cycles. |
| **Safety Supervisor CRC32 & Rollover** | Test circular fault log buffer rollover and CRC32 calculation in `runtime_safety`. | Corrupted crash forensics during unexpected resets. |
| **URL Parser & HMAC Generator** | Test URL parsing (https://, port, path) and HMAC-SHA256 hex digest in `remote_alarm_relay`. | Failed alarm dispatch during life-critical emergencies. |

---

## 8. Prioritized Recommendations for Implementation (Phases 1-4)

### High Priority (Immediate Fixes - Stability & Safety):
1. **Fix ATO / CO2 Manual Override Safety Bypass**:
   - Refactor `gui_app.cpp` safety checks to inspect the *effective* relay output (after manual override resolution) rather than only the automatic demand.
   - Enforce an absolute hard watchdog timer on ATO pump runtime regardless of mode.
2. **Fix Schedule Second-Zero Dependency**:
   - Replace `sc == 0` with a minute-edge latching mechanism in `gui_app.cpp` and `aquarium_schedule.cpp`.
3. **Fix Heartbeat Deadlock Invisibility**:
   - Move `runtime_safety_heartbeat()` calls to execute *only after* critical tasks successfully acquire their mutexes and complete a functional processing step.
4. **Add Dedicated Mutex for SD Card**:
   - Wrap all SD card file open, read, write, and close calls in `hal_sd_lock()` / `hal_sd_unlock()`.
5. **Release I2C Lock During ADS1115 Conversion Wait**:
   - In `hal_adc.cpp`, write the conversion start command, release `hal_i2c_bus_lock`, sleep for conversion time (8-10ms), then reacquire the lock to read the result.
6. **Eliminate Light Sleep Watchdog Panic**:
   - Remove or properly guard `esp_light_sleep_start()` so that tasks and watchdogs are stopped or notified before sleeping, and restore PWM state on GPIO 21 upon wake.

### Medium Priority (Architecture & Testing):
1. **Prune Dead Pub-Sub Code**:
   - Remove `events.cpp` dynamic allocations or integrate them into a unified, event-driven architecture.
2. **Implement Native Tests for Identified Gaps**:
   - Add unit tests in `test/test_native_domain/` covering DS18B20 CRC, relay bitmasking, manual override safety enforcement, and schedule jitter tolerance.
3. **Harden BLE Interface**:
   - Require BLE passkey bonding or token-based authorization for actuator command writes.
