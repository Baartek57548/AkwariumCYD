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

The `icons` directory contains SVG source icons that match the symbols currently
used by the firmware. See `icons/manifest.csv` before wiring runtime loading.

The `images/backgrounds` directory contains the selected aquarium rockscape
background as both a JPG preview and a display-ready RGB565 stream.
