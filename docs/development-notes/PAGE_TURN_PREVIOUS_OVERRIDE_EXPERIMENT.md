# Page Turn Previous Override Experiment

This build starts from the v14 soft-gray band scan experiment and keeps the
reader-page-turn-only band-major path.

## Purpose

The test checks whether using a very light previous-frame rule can reduce
black over-thickening / black bleed while preserving the v14 per-pixel soft
schedule for the remaining cases.

## Timing

Default parameters are in `lib/EPD_Painter/EPD_Painter.cpp`:

```cpp
#define EPD_PAGE_TURN_BAND_ROWS 6
#define EPD_PAGE_TURN_PASS_COUNT 8
#define EPD_PAGE_TURN_PASS_DELAY_MS 1
#define EPD_PAGE_TURN_BLACK_DRIVE 0x55
#define EPD_PAGE_TURN_WHITE_DRIVE 0xAA
#define EPD_PAGE_TURN_SPECIAL_DRIVE 0xFF
```

The page-turn path still scans in 6-row bands:

```text
rows 0..5    pass 0..7
rows 6..11   pass 0..7
rows 12..17  pass 0..7
...
```

## Previous-frame override

For each physical pixel/column, the previous software screen buffer is checked
first:

```text
previous 00 white → lighter × 1, neutral × 7
previous 11 black → darker  × 1, neutral × 7
previous 01/10    → use the current-frame schedule
```

## Current-frame schedule for previous gray pixels

If the previous pixel was gray (`01` or `10`), this build applies the requested
current-frame schedule:

```text
current 00 white → lighter × 5, special × 1, neutral × 2
current 01 gray1 → lighter × 5, darker  × 1, neutral × 2
current 10 gray2 → lighter × 5, darker  × 2, neutral × 1
current 11 black → darker  × 5, special × 1, neutral × 2
```

## Notes

- This path does not use the normal 64-pixel chunk darker/lighter scheduler.
- It is per-pixel/column within each physical row.
- It only affects reader page turns through `PAGE_TURN_REFRESH`.
- Home, File Browser, Settings, Reader Menu, and other UI screens keep the
  normal painter path.
