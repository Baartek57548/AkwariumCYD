# Icon Assets

These icons mirror the symbols currently used by the LVGL GUI. They are stored
as lightweight SVG source files in fixed display buckets:

```text
24/name.svg
32/name.svg
48/name.svg
```

The current firmware does not load these files yet. They are prepared for the
next display optimization step, where screens can request only the assets they
need while opening the active page.

Use `manifest.csv` as the stable mapping between firmware concepts, current
LVGL symbols, and SD card file names.
