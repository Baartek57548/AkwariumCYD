# Inter for the AquaCYD ESP32-S3 interface

The bundled source is Inter Regular (the default instance of the Inter
variable font). The font is distributed under SIL Open Font License 1.1;
the complete, unmodified license is in `OFL.txt`.

- Copyright: 2020 The Inter Project Authors.
- Project: https://github.com/rsms/inter
- Distribution: https://github.com/google/fonts/tree/0b58fb370093f9a9f4ff785d94405710b79de67c/ofl/inter
- Source file: `Inter[opsz,wght].ttf`, stored here as `Inter-Variable.ttf`.
- Source SHA-256: `29160a80ff49ddcab2c97711247e08b1fab27a484a329ce8b813d820dc559031`.
- License source: https://raw.githubusercontent.com/google/fonts/0b58fb370093f9a9f4ff785d94405710b79de67c/ofl/inter/OFL.txt

The generated LVGL C assets are rasterized subsets of this font. They retain
the same license. They use 4-bit grayscale antialiasing, uncompressed glyphs
and kerning classes for predictable rendering cost on the microcontroller.

Sizes: 14, 16, 20, 24 and 44 px. Every size includes printable ASCII,
`ąćęłńóśźżĄĆĘŁŃÓŚŹŻ` and `° µ × – • … → ←`. Private-use LVGL icons are
intentionally excluded and should use LVGL's built-in icon font separately.

Regenerate all assets from the project root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/fonts/generate_ui_fonts.ps1
```

The script verifies the source checksum, runs the pinned `lv_font_conv@1.5.3`
converter through npm and checks that every requested glyph was emitted.
Node.js/npm are required; the source font itself is bundled for repeatability.
