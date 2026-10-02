# Themes

Theme files use ASCII `key=value` records. They are prepared for future runtime
theme loading from SD.

The current firmware does not consume these files yet. During the LVGL lazy page
work, these values can be used to build lightweight styles without recompiling
the firmware for color changes.

Color values use `RRGGBB` hexadecimal notation without a leading `#`.
