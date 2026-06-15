# Config

Configuration files in this directory are intended to be copied, edited, and
renamed by the user before the firmware starts consuming them.

Example workflow:

```text
ui.cfg.example          -> ui.cfg
calibration.cfg.example -> calibration.cfg
schedules.cfg.example   -> schedules.cfg
wifi_profiles.cfg.example -> wifi_profiles.cfg
```

The active runtime configuration can still live in ESP32 NVS/Preferences. SD
config files are intended for import, export, diagnostics, and recovery.
