# cydAquarium SD Layout

This directory is the root used by the firmware on the SD card.

## Directory Map

```text
/aq/
  assets/
    icons/
      24/
      32/
      48/
    images/
      splash/
      backgrounds/
      warnings/
    fonts/
  config/
    backup/
    ui.cfg.example
    calibration.cfg.example
    schedules.cfg.example
    wifi_profiles.cfg.example
  data/
    history/
    logs/
    diagnostics/
  ota/
    version.txt.example
  tmp/
```

## RAM Rule

The SD card stores persistent data and optional assets only. It must not be
treated as an extension of LVGL RAM. Active screens, labels, styles, charts,
touch state, and display buffers still have to fit in internal ESP32 memory.

The intended runtime model is:

- RAM: status bar, navigation bar, current page, current modal, last chart
  samples, and a short visible log window.
- SD: full measurement history, full logs, heap diagnostics, backups, large
  images, rare icons, and optional offline firmware images.

## File Format Policy

- Config files use ASCII `key=value` records.
- Runtime logs use one line per event.
- Long measurement history should use compact binary records.
- Asset names should be lowercase and use hyphens instead of spaces.
- The firmware should ignore unknown keys to allow forward-compatible config
  files.

## Recommended History Record

The binary history files in `data/history` should use fixed-size records so the
firmware can seek without loading a full file into RAM.

```text
record_version:  uint8
flags:           uint8
relay_mask:      uint16
unix_time:       uint32
temperature_x10: int16
ph_x100:         uint16
ec_mv:           uint16
ldr_raw:         uint16
heap_free:       uint32
crc16:           uint16
```

Suggested file name:

```text
/aq/data/history/YYYY-MM.bin
```

The chart screen should load only the requested range into a small RAM buffer,
for example 32 or 64 points.
