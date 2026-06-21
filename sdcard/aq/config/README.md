# Config

Configuration files in this directory are intended to be copied, edited, and
renamed by the user before the firmware starts consuming them.

Example workflow:

```text
ui.cfg.example          -> ui.cfg
calibration.cfg.example -> calibration.cfg
schedules.cfg.example   -> schedules.cfg
wifi_profiles.cfg.example -> wifi_profiles.cfg
device.cfg.example -> device.cfg
safe_mode.flag.example -> safe_mode.flag
```

The active runtime configuration can still live in ESP32 NVS/Preferences. SD
config files are intended for import, export, diagnostics, and recovery.

Wi-Fi credentials confirmed by a successful ESP32 connection are written by the
firmware to:

```text
/aq/config/wifi/profile_<ssid_hash>.cfg
```

These files contain the SSID, password, last IP, RSSI, and an update timestamp.
They are stored as plain text on the SD card by design, so treat the card as
sensitive material.

`safe_mode.flag` is designed as a recovery switch. After firmware support is
added, placing this file on the SD card should request a minimal boot path that
does not load heavy screens, splash animation, Wi-Fi, OTA, or audio.
