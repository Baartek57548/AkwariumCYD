# Splash Assets

The welcome animation is prepared from the user-provided MP4 source.

Runtime-friendly files:

```text
welcome.anim
welcome-320x240.rgb565
frames/welcome_000.rgb565
frames/welcome_001.rgb565
frames/welcome_002.rgb565
frames/welcome_003.rgb565
frames/welcome_004.rgb565
frames/welcome_005.rgb565
frames/welcome_006.rgb565
frames/welcome_007.rgb565
frames/welcome_008.rgb565
frames/welcome_009.rgb565
frames/welcome_010.rgb565
frames/welcome_011.rgb565
frames/welcome_012.rgb565
frames/welcome_013.rgb565
frames/welcome_014.rgb565
frames/welcome_015.rgb565
```

Preview and source files:

```text
welcome-320x240.jpg
welcome-contact-sheet.jpg
welcome-source.mp4
```

The source MP4 is `1280x720`, `24 fps`, and `10 s`. The runtime animation uses
the first `2 s`, sampled to `8 fps`, center-cropped to the `320x240` CYD
landscape display. Each RGB565 frame is `153600` bytes.

The firmware should stream frames from SD in small line chunks instead of
loading a full frame into RAM.
