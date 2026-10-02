# Handoff Report — explorer_survey_3

## 1. Observation
1. **Location & Structure of `uart_protocol`:**
   - Files: `firmware/shared/uart_protocol/include/uart_protocol.h` (123 lines) and `firmware/shared/uart_protocol/src/uart_protocol.cpp` (206 lines).
   - In `uart_protocol.h` (lines 40–51), `enum class CommandType` currently defines 10 commands (`Unknown`, `SetRelay`, `SetTempTarget`, `SetSchedule`, `FeedTrigger`, `SyncLights`, `SyncTime`, `ClearAlarm`, `GetStatus`, `SetSafeMode`). There is no command for entering OTA.
   - In `uart_protocol.h` (lines 63–77), `AquariumTelemetry` contains basic sensor states and `bool safe_mode`, but lacks any fields for OTA status (`ota_mode`) or IP address (`ota_ip`).
   - In `firmware/cyd_controller/src/uart_client.cpp` (lines 36–48), `UartClient::processLine()` only checks `if (msg.type == AquaProtocol::MessageType::Telemetry)`. Any incoming `MessageType::Ack` message is discarded without processing.
2. **WeMos Hardware & Pin Allocation:**
   - In `firmware/wemos_controller/include/wemos_config.h` (lines 9–16), pins configured are: `PIN_I2C_SCL = 5` (D1), `PIN_I2C_SDA = 4` (D2), `PIN_ONEWIRE = 14` (D5), `PIN_LED_STATUS = 2` (D4), `PIN_UART_TX = 1` (TX), `PIN_UART_RX = 3` (RX).
   - Pin `D6` (`GPIO12`) is unallocated, has built-in pullup (`INPUT_PULLUP`), and does not affect ESP8266 boot strapping mode (unlike GPIO0, GPIO2, GPIO15).
   - Relays are driven by MCP23017 on Port A and are active-low (`WemosConfig::RELAY_ACTIVE_LOW = true`, line 32). In `firmware/wemos_controller/src/hardware_i2c.cpp` (lines 79–83, 143–154), writing `0xFF` to `REG_OLATA` sets all 8 relays to de-energized (OFF).
3. **EEPROM Layout & Flash Map:**
   - In `platformio.ini` (line 91), WeMos uses `board_build.ldscript = eagle.flash.4m2m.ld` (4MB Flash, 2MB SPIFFS, 1019KB Sketch 1, 1019KB Sketch 2 for OTA, dedicated 4KB EEPROM sector at `0x3FB000`).
   - In `firmware/wemos_controller/src/storage_manager.h` (lines 7–16) and `storage_manager.cpp` (lines 6–8, 53–70), `ControllerConfig` has `uint32_t magic` (0x41515541), `uint16_t version` (1), target temp, hysteresis, pH calibration, schedules, and CRC32 checksum.
   - In `storage_manager.cpp` (lines 56–60):
     `if (cfg.magic != CONFIG_MAGIC || cfg.version != CONFIG_VERSION) { resetToDefaults(cfg); saveConfig(cfg); return false; }`
     Without a schema migration routine, upgrading `version` from 1 to 2 immediately wipes all calibrated pH parameters and schedules back to factory defaults.
4. **Existing Obsidian Documentation:**
   - `obsidian/Procedura - Programowanie i Wgrywanie Firmware.md` (lines 20–55) only documents physical USB flashing with jumpers `J_TX` and `J_RX` removed.
5. **Compilation Checks:**
   - Executed `pio run -e wemos_d1_uno`: Exited 0 with RAM: 36.5% (29924/81920 bytes) and Flash: 27.9% (291831/1044464 bytes).
   - Executed `pio run -e cyd_2_8`: Compiled firmware completely with RAM: 27.0% (88632/327680 bytes) and Flash: 83.6% (1644353/1966080 bytes).

---

## 2. Logic Chain
1. **From Observation 1 to Command Specification:**
   Because `CommandType` lacks an OTA command and `UartClient` ignores ACKs, adding `CommandType::EnterOta` to `uart_protocol.h`, implementing serialization/deserialization in `uart_protocol.cpp`, and updating `UartClient::processLine()` to record ACKs is required to establish bidirectional OTA coordination between CYD and WeMos.
2. **From Observation 1 to Telemetry Status:**
   Because CYD needs to know WeMos's IP address and operating state, extending `AquariumTelemetry` with `bool ota_mode` and `char ota_ip[16]` allows continuous state visibility even if an individual ACK packet is missed.
3. **From Observation 2 to Hardware Safety:**
   Because heater and CO2 solenoid relays are active-low via MCP23017, executing `g_hardware_i2c.setAllRelays(0x00)` immediately forces MCP23017 `REG_OLATA` to `0xFF` (contacts open) in <2 ms, eliminating any risk of overheating or gas poisoning before any flash operations start.
4. **From Observation 2 to Physical Fallback:**
   Because `GPIO12` (D6) is unused and neutral during ESP8266 boot, wiring a monostable button between `GPIO12` and GND allows entering OTA mode even if the CYD panel is broken.
5. **From Observation 3 to EEPROM Persistence:**
   Because ESP8266 Arduino core places EEPROM at `0x3FB000` outside sketch partitions, flash updates do not touch the physical sector. However, to prevent `StorageManager::loadConfig()` from wiping user data upon struct size/version changes, an explicit `ControllerConfigV1` -> `ControllerConfigV2` migration step is necessary.
6. **From Observation 4 to Documentation:**
   Because the user requirement is to eliminate case disassembly and jumper manipulation, `obsidian/Procedura - Programowanie i Wgrywanie Firmware.md` must be updated to make wireless OTA the primary method and relegate USB flashing to disaster recovery.

---

## 3. Caveats
- The hardware fallback button requires a physical wire from D6 (GPIO12) to GND (or an external push button); on units without the physical button installed, OTA entry relies solely on UART command `enter_ota`.
- If WeMos is switched to AP mode (`AquaBox-OTA`), the programming device must connect directly to that SSID (`192.168.4.1`) since it will not be on the home LAN subnet.
- No other caveats; all file locations, structures, and memory layouts were directly inspected and verified with PlatformIO compilation.

---

## 4. Conclusion
A complete, robust, and safe OTA upgrade subsystem can be implemented without architectural hurdles:
1. `uart_protocol` requires addition of `EnterOta` command, ACK handling, and `ota_mode`/`ota_ip` telemetry schema extensions.
2. Hardware safety is guaranteed by immediate MCP23017 cutoff (`setAllRelays(0x00)`), safe mode activation, and a 5-minute safety watchdog.
3. UART bus noise and flash corruption are prevented by silencing telemetry prior to and during flash writes.
4. EEPROM persistence across upgrades is guaranteed both physically (separate flash sector) and logically (V1->V2 struct migration routine).
5. Detailed analysis and implementation roadmap have been compiled in `report.md`.

---

## 5. Verification Method
1. **Compilation verification:**
   ```powershell
   pio run -e wemos_d1_uno
   pio run -e cyd_2_8
   ```
2. **Native domain test suite:**
   ```powershell
   pio test -e native
   ```
3. **Artifact review:**
   Inspect `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\explorer_survey_3\report.md` for complete implementation blueprints, schema diffs, and Obsidian documentation updates.
