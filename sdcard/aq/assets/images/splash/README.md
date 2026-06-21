# Splash Assets

The welcome animation is prepared from the user-provided MP4 source.

Runtime-friendly files:

```text
welcome.anim
welcome-320x240.rgb565
frames/welcome_000.rgb565 ... frames/welcome_039.rgb565
```

Preview and source files:

```text
welcome-320x240.jpg
welcome-contact-sheet.jpg
welcome-source.mp4
```

The source MP4 is `1280x720`, `24 fps`, and `10 s`. The runtime animation uses the first `5 s`, sampled to `8 fps`, center-cropped to the `320x240` CYD landscape display. Each RGB565 frame is `153600` bytes.

The firmware streams frames from SD in small line chunks instead of loading a full frame into RAM.
