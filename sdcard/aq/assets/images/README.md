# Images

Image assets are grouped by runtime purpose.

- `backgrounds`: UI backgrounds loaded on demand.
- `splash`: startup or loading images, including the prepared welcome animation.
- `warnings`: alarm and critical-state images.

Prefer screen-ready files for the CYD display. For the current hardware target,
that means `320x240` pixels. Raw RGB565 files are larger than JPG files, but
they can be streamed with much less decoding overhead.
