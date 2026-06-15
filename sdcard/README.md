This directory is the prepared SD card image for cydAquarium.

Copy the contents of this directory to the root of a FAT32-formatted SD card.
After copying, the card root should contain the `aq` directory.

The firmware should treat files under `aq/config` as human-editable input and
files under `aq/data` as runtime output. Large UI assets can live under
`aq/assets`, but active LVGL objects and draw buffers always remain in RAM.
