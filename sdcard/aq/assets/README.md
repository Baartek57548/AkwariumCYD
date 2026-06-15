# Assets

Store optional display assets here. Keep frequently used navigation symbols in
firmware fonts or LVGL symbols. The SD card is better suited for larger or
rarely used assets.

Recommended formats:

- Icons: RGB565 raw files with a fixed pixel size per folder.
- Images: RGB565 raw files when RAM is tight.
- Fonts: binary font files only when the firmware has a loader for them.

Avoid loading many small files every frame. Load assets when a screen opens,
cache only one or two if needed, and release them when the screen closes.
