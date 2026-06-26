# Page Turn Monochrome Band Scan Experiment v13

This experiment is reader-page-turn only.  It is intended to test a simpler text-page model:

- Ignore the previous frame.
- Treat the current target frame as monochrome.
- Drive each physical pixel/column directly from the current frame:
  - target `00` = white drive
  - target `01`, `10`, or `11` = black drive
- Do not use the normal 64-pixel chunk darker/lighter scheduler for the page-turn path.
- Use band-major timing so a small group of rows completes all passes before moving to the next band.

## Default experiment settings

The tunable constants are near the top of `lib/EPD_Painter/EPD_Painter.cpp`:

```cpp
#define EPD_PAGE_TURN_BAND_ROWS 6
#define EPD_PAGE_TURN_PASS_COUNT 13
#define EPD_PAGE_TURN_PASS_DELAY_MS 1
#define EPD_PAGE_TURN_BLACK_DRIVE 0x55
#define EPD_PAGE_TURN_WHITE_DRIVE 0xAA
```

Default scan order:

```text
rows 0..5    pass 0..12
rows 6..11   pass 0..12
rows 12..17  pass 0..12
...
```

## What changed from v12

v12 still used the normal packed framebuffer conversion path:

```text
64-pixel chunk -> darker or lighter waveform path
pixel within chunk -> active or neutral
```

v13 uses a direct current-frame monochrome row builder for the page-turn path:

```text
each packed target pixel -> black drive or white drive
```

This makes the page-turn experiment easier to reason about for black/white text, but it is not a general grayscale waveform solution.

## Expected risks

Because this intentionally does not compare with the previous screen state, it can overdrive pixels that are already correct.  It may improve scan visibility, but ghosting, contrast, and long-term DC balance must be checked on real hardware.

If black and white are reversed on hardware, swap:

```cpp
#define EPD_PAGE_TURN_BLACK_DRIVE 0xAA
#define EPD_PAGE_TURN_WHITE_DRIVE 0x55
```
