# History

Suggested file name:

```text
YYYY-MM.bin
```

Use fixed-size binary records. This keeps the file compact and allows the
firmware to seek directly to a time range without parsing the whole month.

The chart view should load a bounded number of samples into RAM, such as 32 or
64 records.
