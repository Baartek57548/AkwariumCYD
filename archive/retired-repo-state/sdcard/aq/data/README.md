# Runtime Data

The firmware can write long-lived runtime data here without keeping it in RAM.

Recommended retention:

- `history`: monthly binary files.
- `logs`: daily text files.
- `diagnostics`: daily or per-boot diagnostic files.

The UI should read only the range that is needed for the current screen.
