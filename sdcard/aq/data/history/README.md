# History

Current download format:

```text
/api/history.csv
```

Columns:

```text
schema_version,generated_epoch,index,epoch,temp_c,temp_valid,ph,ph_valid,ldr,ldr_valid,heap_bytes,heater_on
```

Rules:

- Empty sensor fields mean that the sensor was unavailable.
- `*_valid=0` must be treated as missing data, not as zero.
- `epoch` is the sample timestamp in Unix seconds.
- `generated_epoch` is the export timestamp in Unix seconds.

Recommended analysis flow:

```text
CSV from /api/history.csv -> spreadsheet / Python / R
```

Long-term archive format:

```text
YYYY-MM.aqbin
```

The firmware appends one fixed-size binary record every 60 seconds. This keeps
SD writes compact and allows firmware or external tools to seek directly to a
time range without parsing the whole month. The on-screen chart still keeps only
a bounded RAM buffer.

File header, little-endian:

```text
uint32 magic        0x31485141 ("AQH1")
uint16 version      1
uint16 header_size  32
uint16 record_size  18
uint16 year
uint8  month
uint8  flags
uint32 created_epoch
uint8  reserved[14]
```

Record, little-endian:

```text
uint32 epoch
int16  temp_c_x100   INT16_MIN means unavailable
int16  ph_x1000      INT16_MIN means unavailable
int16  ldr           -1 means unavailable
uint32 heap_bytes
uint8  flags         bit0=temp, bit1=pH, bit2=LDR, bit3=heater_on
uint8  reserved[3]
```

Retention rule:

- The firmware keeps at least 1 MB free on the SD card.
- If the card is too full, the oldest `YYYY-MM.aqbin` file is removed first.
- If only the current month can be cleaned, the current file is compacted by
  dropping its oldest records and keeping newer records.
